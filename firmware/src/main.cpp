// Copyright (c) 2026 DSH ESP32 dial project.
// SPDX-License-Identifier: MIT
//
// DSH status dial firmware — entry point.
//
// The device shows DSH's state on a 360×360 round screen and takes approve/deny
// decisions by touch. It runs one of two modes, chosen at boot and switchable at
// runtime without a reflash:
//
//   Run mode         — join the stored WiFi, connect to the bridge, show status.
//   Provisioning mode — become an access point and serve a setup form, entered
//                       when there is no usable configuration or the stored
//                       network cannot be joined.
//
// The serial console accepts the same edits as the web form (`help` lists them),
// which is useful while the board is still on a USB cable.

#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>

#include "AudioFeedback.h"
#include "Config.h"
#include "Console.h"
#include "DialUi.h"
#include "Display_ST77916.h"
#include "I2C_Driver.h"
#include "LvglPort.h"
#include "Provision.h"
#include "Settings.h"
#include "Touch_CST816.h"
#include "WsClient.h"

// ── globals ───────────────────────────────────────────────────────────────

WsClient ws;
AudioFeedback audio;
bool audioReady = false;

/** What the firmware is currently doing. */
enum class AppMode : uint8_t {
  Provisioning,  // AP + setup portal
  Connecting,    // joining the stored WiFi
  Running        // WiFi up, bridge link managed
};

static AppMode mode = AppMode::Connecting;

// Battery telemetry. This board has charge management but no fuel gauge on the
// I2C bus, so the reported figure is a placeholder until a divider read on the
// battery rail is added; the bridge treats it as advisory.
static uint8_t batteryPercent = 100;
static bool batteryCharging = false;

static unsigned long lastPingMs = 0;
static unsigned long lastActivityMs = 0;  // last state change or interaction
static unsigned long wifiAttemptStartedMs = 0;
static unsigned long linkDownSinceMs = 0;  // first WL_DISCONNECTED sighting

/**
 * How long to wait for the stored network before opening the portal.
 *
 * Long enough for a slow router to answer, short enough that a user standing in
 * front of a dial with a wrong password is not left guessing.
 */
static constexpr unsigned long kWifiJoinTimeoutMs = 20000;

// ── JSON heap ─────────────────────────────────────────────────────────────
// One reusable document: bridge frames are small and arrive one at a time, so a
// static buffer avoids fragmenting the heap over days of uptime.
static StaticJsonDocument<2048> jsonDoc;

// ── forward declarations ──────────────────────────────────────────────────

void onWsMessage(const char* text, size_t length);
void sendHello();
void enterProvisioning(const char* reason);
void beginWifiJoin();
void serviceProvisioning();
void serviceConnecting();
void serviceRunning();

// ── setup ─────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n════════════════════════════════");
  Serial.println("DSH dial v0.6.0 — ESP32-S3-Touch-LCD-1.85B");
  Serial.println("════════════════════════════════\n");

  // ── I2C bus (shared by touch, IMU, RTC) ──────────────────────────────
  I2C_Init();
  Serial.println("[i2c] bus ready");

  // ── Display ───────────────────────────────────────────────────────────
  // LCD_Init() calls pinMode(RST, OUTPUT), ST77916_Init(), and Touch_Init()
  // internally, so the three do not need to be called separately.
  LCD_Init();
  Backlight_Init();
  Set_Backlight(DSH_FULL_BRIGHTNESS);
  Serial.println("[lcd] 360×360 ready");

  // ── LVGL + display port ───────────────────────────────────────────────
  // LvglPort_Init() calls lv_init() itself and allocates its draw buffers in
  // PSRAM, so it must not be preceded by another lv_init().
  LvglPort_Init();
  Serial.println("[lvgl] 8.4.0 ready (display + touch registered)");

  // ── Audio ──────────────────────────────────────────────────────────────
  // The 1.85B carries a PCM5101 DAC while the vendored AudioFeedback targets an
  // ES8311 codec (the sibling board's part), so sound is optional: begin()
  // reporting false leaves the dial fully functional and silent.
  if (audio.begin()) {
    audioReady = true;
    Serial.println("[audio] codec ready");
  } else {
    Serial.println("[audio] codec unavailable — dial continues without sound");
  }

  // ── Dial UI (needs LVGL, builds the screens) ───────────────────────────
  dialUi.begin();
  Serial.println("[ui] layouts ready");

  // ── Settings ───────────────────────────────────────────────────────────
  settingsStore.begin();
  console.begin();

  // ── Choose the starting mode ───────────────────────────────────────────
  if (!settingsStore.get().isComplete()) {
    enterProvisioning("首次配置");
  } else {
    beginWifiJoin();
  }

  lastPingMs = millis();
  Serial.println("[setup] complete — type `help` for console commands");
}

// ── main loop ─────────────────────────────────────────────────────────────

void loop() {
  // The console works in every mode, so a USB-connected board is never stuck.
  console.loop();

  switch (mode) {
    case AppMode::Provisioning: serviceProvisioning(); break;
    case AppMode::Connecting:   serviceConnecting();   break;
    case AppMode::Running:      serviceRunning();      break;
  }

  // LVGL runs in every mode: the provisioning screen needs redraws too.
  dialUi.loop();

  // ── Backlight ─────────────────────────────────────────────────────────
  if (mode == AppMode::Provisioning) {
    // Setup instructions must stay readable for as long as they are on screen.
    Set_Backlight(DSH_FULL_BRIGHTNESS);
  } else if (dialUi.isWaiting() || dialUi.isDoneShowing()) {
    Set_Backlight(DSH_FULL_BRIGHTNESS);
  } else if (DSH_DIM_AFTER_MS > 0 &&
             millis() - lastActivityMs > DSH_DIM_AFTER_MS &&
             !dialUi.isOffline()) {
    Set_Backlight(DSH_DIM_BRIGHTNESS);
  } else {
    Set_Backlight(DSH_FULL_BRIGHTNESS);
  }
}

// ── mode: provisioning ────────────────────────────────────────────────────

void enterProvisioning(const char* reason) {
  mode = AppMode::Provisioning;
  ws.close();
  provisioner.begin(reason);

  char url[32];
  snprintf(url, sizeof(url), "http://%s",
           WiFi.softAPIP().toString().c_str());
  dialUi.showProvisioning(provisioner.apSsid().c_str(),
                          provisioner.apPassword().c_str(), url, reason);
}

void serviceProvisioning() {
  provisioner.loop();

  // A save reboots the device, so reaching here means nothing was saved yet.
  // Time the portal out only when there is a stored config worth retrying;
  // an unconfigured dial waits indefinitely rather than looping pointlessly.
  if (settingsStore.get().isComplete() &&
      provisioner.uptimeMs() > DSH_AP_FALLBACK_TIMEOUT_MS) {
    Serial.println("[prov] timeout — retrying the stored network");
    provisioner.end();
    dialUi.hideProvisioning();
    beginWifiJoin();
  }
}

// ── mode: connecting ──────────────────────────────────────────────────────

void beginWifiJoin() {
  const DialSettings& cfg = settingsStore.get();
  mode = AppMode::Connecting;
  wifiAttemptStartedMs = millis();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);  // Disable modem sleep: the ESP32's power-save
  // mode periodically drops the connection to scan for better APs, which on a
  // stationary dial with a known-good AP causes spurious disconnects every few
  // minutes. The dial is always plugged in; saving milliwatts is not worth the
  // instability.
  Serial.printf("[wifi] joining %s\n", cfg.wifiSsid.c_str());
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());

  char msg[64];
  snprintf(msg, sizeof(msg), "WiFi: %s", cfg.wifiSsid.c_str());
  dialUi.setConnecting(msg);
}

void serviceConnecting() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected, IP %s\n",
                  WiFi.localIP().toString().c_str());

    const DialSettings& cfg = settingsStore.get();
    ws.begin(cfg.bridgeHost.c_str(), cfg.bridgePort, cfg.bridgeToken.c_str(),
             onWsMessage);
    mode = AppMode::Running;
    lastActivityMs = millis();
    dialUi.setConnecting("连接桥接…");
    return;
  }

  // Joining failed. The likeliest causes are a wrong password or a network that
  // is simply gone, and both are fixed by the same portal, so open it.
  if (millis() - wifiAttemptStartedMs > kWifiJoinTimeoutMs) {
    Serial.println("[wifi] join timed out — opening provisioning portal");
    enterProvisioning("WiFi 连接失败");
  }
}

// ── mode: running ─────────────────────────────────────────────────────────

void serviceRunning() {
  // The dial can be asked to re-enter setup without a cable: a long press on
  // the screen raises this request, and reconfiguration is a deliberate act.
  if (dialUi.hasReprovisionRequest()) {
    dialUi.clearReprovisionRequest();
    Serial.println("[ui] long press — reopening provisioning");
    enterProvisioning("重新配置");
    return;
  }

  // A dropped network sends the dial back through the joining path, which opens
  // the portal if the network stays unavailable.
  //
  // A single missing status reading is not a dropped network: WiFi.status()
  // reports WL_DISCONNECTED during ordinary events like an AP beacon miss or a
  // DHCP renewal, and the ESP32's own auto-reconnect usually restores the link
  // within a second. Tearing down the socket on the first such reading is what
  // made the dial drop its link "偶尔" for no visible reason — and worse, each
  // teardown risked opening the provisioning portal over a working network.
  // So require the link to be down continuously before believing it.
  if (WiFi.status() != WL_CONNECTED) {
    if (linkDownSinceMs == 0) {
      linkDownSinceMs = millis();
      return;  // first sighting: give auto-reconnect a chance
    }
    if (millis() - linkDownSinceMs < DSH_LINK_GRACE_MS) return;
    Serial.println("[wifi] link lost");
    linkDownSinceMs = 0;
    ws.close();
    dialUi.setConnecting("WiFi 断开，重连中");
    beginWifiJoin();
    return;
  }
  linkDownSinceMs = 0;

  ws.loop();

  if (ws.isOpen()) {
    if (millis() - lastPingMs > DSH_PING_MS) {
      ws.sendf("{\"t\":\"ping\",\"battery\":%u,\"charging\":%s}",
               batteryPercent, batteryCharging ? "true" : "false");
      lastPingMs = millis();
    }
    // A TCP connection can stay open long after the far end stops answering:
    // the bridge process dying, or the FRP tunnel dropping, leaves the socket
    // established with nothing behind it. Without this check the dial would
    // hold that dead socket forever, showing stale data and never reconnecting.
    // Every pong updates lastRecvMs, so silence past several ping periods means
    // the link is gone regardless of what the socket claims.
    if (millis() - ws.lastRecvMs() > DSH_WS_SILENCE_MS) {
      Serial.println("[ws] no traffic within timeout — reconnecting");
      ws.close();
      dialUi.setConnecting("桥接无响应，重连中");
      return;
    }
    // Silence past the freshness window means the numbers on screen are no
    // longer known to be current, so the dial says so rather than lying.
    dialUi.setStale(millis() - ws.lastRecvMs() >= DSH_FRESHNESS_MS);
  } else if (ws.backoffElapsed()) {
    if (ws.connect()) {
      sendHello();
      Serial.println("[ws] connected");
      lastActivityMs = millis();
    } else {
      ws.updateBackoff();
    }
  }

  // Touch is not polled here: LvglPort's read callback owns the CST816 and
  // clears its state, so a second reader would race it. Button presses arrive
  // through the LVGL event callbacks registered in DialUi, which record an
  // answer for this loop to ship.
  if (dialUi.hasAnswer()) {
    char* answer = dialUi.takeAnswer();
    if (answer != nullptr) {
      if (ws.isOpen()) {
        ws.sendf("{\"t\":\"answer\",\"id\":\"%s\",\"choice\":\"%s\"}",
                 dialUi.askId(), answer);
        Serial.printf("[ws] answer sent: %s → %s\n", dialUi.askId(), answer);
      } else {
        Serial.println("[ws] answer dropped: link is down");
      }
      free(answer);
    }
    dialUi.dismissAsk();
    lastActivityMs = millis();
  }
}

// ── WebSocket message handler ─────────────────────────────────────────────

void onWsMessage(const char* text, size_t length) {
  const DeserializationError err = deserializeJson(jsonDoc, text, length);
  if (err) {
    Serial.printf("[ws] JSON parse error: %s\n", err.c_str());
    return;
  }

  const char* type = jsonDoc["t"] | "";
  if (strcmp(type, "state") == 0) {
    dialUi.setState(jsonDoc);
    lastActivityMs = millis();

  } else if (strcmp(type, "ask") == 0) {
    dialUi.showAsk(jsonDoc);
    lastActivityMs = millis();
    if (audioReady) audio.completionChime();

  } else if (strcmp(type, "beep") == 0) {
    if (audioReady) audio.tap();

  } else if (strcmp(type, "pong") == 0) {
    // Heartbeat acknowledged; lastRecvMs was already updated by the reader.

  } else if (strcmp(type, "speak") == 0) {
    // Reserved for a later firmware revision that adds TTS output.

  } else {
    // Unknown frames are tolerated: the bridge may add types this build has
    // never heard of, and a dial that reboots on one would be worse than a dial
    // that ignores it.
    Serial.printf("[ws] ignoring unknown frame type: %s\n", type);
  }
}

// ── hello ─────────────────────────────────────────────────────────────────

void sendHello() {
  ws.sendf(
      "{\"t\":\"hello\",\"fw\":\"dsh-dial/0.6.0\",\"board\":\"ESP32-S3-Touch-LCD-1.85B\""
      ",\"battery\":%u,\"charging\":%s}",
      batteryPercent, batteryCharging ? "true" : "false");
}