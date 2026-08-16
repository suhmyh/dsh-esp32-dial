// Copyright (c) 2026 DSH ESP32 dial project.
// SPDX-License-Identifier: MIT
//
// Minimal RFC6455 WebSocket client for the DSH bridge.
//
// Written from the RFC rather than pulled from a library because the wire
// contract is tiny: one JSON text frame per direction, mask on client→server
// frames, and a handshake that carries the pre-shared token. Keeping the
// transport local makes the firmware's network behaviour auditable and removes
// a library dependency for a device that does exactly one thing.

#pragma once

#include <Arduino.h>
#include <WiFi.h>

/** Callback for each complete text payload received from the bridge. */
using WsMessageCallback = void (*)(const char* text, size_t length);

enum class WsState {
  Idle,        // not connected, not trying
  Connecting,  // TCP + handshake in flight
  Open,        // handshake done, frames flowing
  Closed       // was open, now dropping; backoff applies
};

class WsClient {
 public:
  void begin(const char* host, uint16_t port, const char* token,
             WsMessageCallback onMessage);
  void loop();

  bool connect();                              // blocking connect+handshake
  bool send(const char* json);                 // one JSON text frame
  bool sendf(const char* fmt, ...);            // formatted frame
  void close();

  WsState state() const { return state_; }
  bool isOpen() const { return state_ == WsState::Open; }
  bool backoffElapsed() const;
  void updateBackoff();
  unsigned long lastRecvMs() const { return lastRecvMs_; }
  const String& remoteIp() const { return remoteIp_; }

 private:
  bool handshake();
  size_t readAvailable();                      // parse RX frames into buffer
  static void base64Encode(const uint8_t* data, size_t len, char* out);

  String host_;
  uint16_t port_ = 3082;
  String token_;
  WsMessageCallback onMessage_ = nullptr;

  WiFiClient tcp_;
  WsState state_ = WsState::Idle;
  String remoteIp_;

  // RX reassembly
  uint8_t rxBuf_[4096];
  size_t rxLen_ = 0;
  // TX scratch
  uint8_t txBuf_[2048];

  unsigned long lastRecvMs_ = 0;
  unsigned long lastSendMs_ = 0;
  unsigned long backoffUntil_ = 0;
  unsigned long backoffMs_ = 1000;
};