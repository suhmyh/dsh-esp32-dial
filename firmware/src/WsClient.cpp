// Copyright (c) 2026 DSH ESP32 dial project.
// SPDX-License-Identifier: MIT
//
// Implementation of the minimal WebSocket client. See WsClient.h for why the
// transport is hand-written.

#include "WsClient.h"

#include <mbedtls/base64.h>

#include "Config.h"

namespace {
/** The magic GUID from RFC6455 §4.2.2, used in the accept-key derivation. */
constexpr const char* kWsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/** Opcodes used by this client. */
constexpr uint8_t kOpText = 0x1;
constexpr uint8_t kOpClose = 0x8;
constexpr uint8_t kOpPing = 0x9;
constexpr uint8_t kOpPong = 0xA;
}  // namespace

void WsClient::begin(const char* host, uint16_t port, const char* token,
                     WsMessageCallback onMessage) {
  host_ = host;
  port_ = port;
  token_ = token;
  onMessage_ = onMessage;
  state_ = WsState::Idle;
  backoffMs_ = DSH_BACKOFF_MIN_MS;
  backoffUntil_ = 0;
}

void WsClient::base64Encode(const uint8_t* data, size_t len, char* out) {
  size_t written = 0;
  mbedtls_base64_encode(reinterpret_cast<unsigned char*>(out), 64, &written,
                        data, len);
  out[written] = '\0';
}

bool WsClient::connect() {
  if (WiFi.status() != WL_CONNECTED) return false;

  state_ = WsState::Connecting;
  tcp_.stop();
  rxLen_ = 0;

  if (!tcp_.connect(host_.c_str(), port_)) {
    Serial.printf("[ws] TCP connect to %s:%u failed\n", host_.c_str(), port_);
    state_ = WsState::Closed;
    return false;
  }
  tcp_.setNoDelay(true);

  if (!handshake()) {
    tcp_.stop();
    state_ = WsState::Closed;
    return false;
  }

  state_ = WsState::Open;
  remoteIp_ = tcp_.remoteIP().toString();
  lastRecvMs_ = millis();
  backoffMs_ = DSH_BACKOFF_MIN_MS;  // a success resets the ladder
  Serial.printf("[ws] open to %s:%u\n", host_.c_str(), port_);
  return true;
}

bool WsClient::handshake() {
  // A random 16-byte nonce, base64'd, per RFC6455 §4.1.
  uint8_t nonce[16];
  for (uint8_t i = 0; i < sizeof(nonce); ++i) nonce[i] = random(0, 256);
  char key[32];
  base64Encode(nonce, sizeof(nonce), key);

  // The token rides in the query string: the bridge compares it in constant
  // time and answers 401 when it does not match.
  tcp_.printf(
      "GET /dev?token=%s HTTP/1.1\r\n"
      "Host: %s:%u\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: %s\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "\r\n",
      token_.c_str(), host_.c_str(), port_, key);

  // Read the status line and headers with a bounded wait.
  const unsigned long deadline = millis() + 6000;
  String head;
  while (millis() < deadline) {
    while (tcp_.available()) {
      head += static_cast<char>(tcp_.read());
      if (head.endsWith("\r\n\r\n")) break;
    }
    if (head.endsWith("\r\n\r\n")) break;
    if (!tcp_.connected()) break;
    delay(10);
  }

  const int lineEnd = head.indexOf("\r\n");
  const String status = lineEnd > 0 ? head.substring(0, lineEnd) : head;
  if (status.indexOf("101") < 0) {
    // 401 here means the token is wrong — the most likely first-run mistake,
    // so say so explicitly rather than reporting a generic failure.
    if (status.indexOf("401") >= 0) {
      Serial.println(
          "[ws] REJECTED 401 — DSH_BRIDGE_TOKEN does not match the bridge. "
          "Copy it from the bridge log or device-token.txt.");
    } else {
      Serial.printf("[ws] handshake failed: %s\n", status.c_str());
    }
    return false;
  }
  return true;
}

bool WsClient::send(const char* json) {
  if (state_ != WsState::Open || !tcp_.connected()) return false;

  const size_t len = strlen(json);
  if (len + 14 > sizeof(txBuf_)) return false;

  size_t o = 0;
  txBuf_[o++] = 0x80 | kOpText;  // FIN + text

  // Client frames MUST be masked (RFC6455 §5.3).
  uint8_t mask[4];
  for (uint8_t i = 0; i < 4; ++i) mask[i] = random(0, 256);

  if (len < 126) {
    txBuf_[o++] = 0x80 | static_cast<uint8_t>(len);
  } else {
    txBuf_[o++] = 0x80 | 126;
    txBuf_[o++] = (len >> 8) & 0xFF;
    txBuf_[o++] = len & 0xFF;
  }
  memcpy(txBuf_ + o, mask, 4);
  o += 4;
  for (size_t i = 0; i < len; ++i) {
    txBuf_[o + i] = static_cast<uint8_t>(json[i]) ^ mask[i & 3];
  }
  o += len;

  const size_t wrote = tcp_.write(txBuf_, o);
  lastSendMs_ = millis();
  return wrote == o;
}

bool WsClient::sendf(const char* fmt, ...) {
  char scratch[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(scratch, sizeof(scratch), fmt, args);
  va_end(args);
  return send(scratch);
}

size_t WsClient::readAvailable() {
  size_t handled = 0;
  while (tcp_.available() > 0 && rxLen_ < sizeof(rxBuf_)) {
    rxBuf_[rxLen_++] = tcp_.read();
    handled++;
  }
  return handled;
}

void WsClient::loop() {
  if (state_ != WsState::Open) return;

  if (!tcp_.connected()) {
    Serial.println("[ws] peer closed");
    state_ = WsState::Closed;
    return;
  }

  readAvailable();

  // Parse as many complete server frames as the buffer holds. Server→client
  // frames are never masked, which keeps this loop short.
  for (;;) {
    if (rxLen_ < 2) return;

    const uint8_t opcode = rxBuf_[0] & 0x0F;
    const bool masked = (rxBuf_[1] & 0x80) != 0;
    size_t len = rxBuf_[1] & 0x7F;
    size_t offset = 2;

    if (len == 126) {
      if (rxLen_ < 4) return;
      len = (static_cast<size_t>(rxBuf_[2]) << 8) | rxBuf_[3];
      offset = 4;
    } else if (len == 127) {
      // A dial frame never approaches 64 KiB; treat it as a protocol fault
      // rather than trying to buffer it.
      Serial.println("[ws] oversized frame, resetting link");
      state_ = WsState::Closed;
      rxLen_ = 0;
      return;
    }

    const size_t maskLen = masked ? 4 : 0;
    if (rxLen_ < offset + maskLen + len) return;  // wait for the rest

    uint8_t* payload = rxBuf_ + offset + maskLen;
    if (masked) {
      const uint8_t* mask = rxBuf_ + offset;
      for (size_t i = 0; i < len; ++i) payload[i] ^= mask[i & 3];
    }

    lastRecvMs_ = millis();

    if (opcode == kOpText && onMessage_ != nullptr) {
      // Terminate in place so the callback can treat it as a C string; the
      // byte overwritten is the start of the next frame, which is copied
      // down immediately after.
      const uint8_t saved = payload[len];
      payload[len] = '\0';
      onMessage_(reinterpret_cast<const char*>(payload), len);
      payload[len] = saved;
    } else if (opcode == kOpPing) {
      // Answer control pings so an intermediary does not time the link out.
      uint8_t pong[2] = {static_cast<uint8_t>(0x80 | kOpPong), 0x80};
      uint8_t maskZero[4] = {0, 0, 0, 0};
      tcp_.write(pong, 2);
      tcp_.write(maskZero, 4);
    } else if (opcode == kOpClose) {
      Serial.println("[ws] close frame");
      state_ = WsState::Closed;
      rxLen_ = 0;
      return;
    }

    // Slide the remainder down.
    const size_t consumed = offset + maskLen + len;
    memmove(rxBuf_, rxBuf_ + consumed, rxLen_ - consumed);
    rxLen_ -= consumed;
  }
}

void WsClient::close() {
  if (tcp_.connected()) {
    uint8_t frame[6] = {static_cast<uint8_t>(0x80 | kOpClose), 0x80, 0, 0, 0, 0};
    tcp_.write(frame, sizeof(frame));
  }
  tcp_.stop();
  state_ = WsState::Idle;
  rxLen_ = 0;
}

bool WsClient::backoffElapsed() const {
  return millis() >= backoffUntil_;
}

void WsClient::updateBackoff() {
  backoffUntil_ = millis() + backoffMs_;
  backoffMs_ = min<unsigned long>(backoffMs_ * 2, DSH_BACKOFF_MAX_MS);
  Serial.printf("[ws] retry in %lums\n", backoffMs_ / 2);
}