// Copyright (c) 2026 DSH ESP32 dial project.
// SPDX-License-Identifier: MIT
//
// Provisioning portal: configure the dial from a phone, with no reflash.
//
// When the stored settings are incomplete — or the stored network cannot be
// joined — the dial becomes an access point and serves a small form at
// http://192.168.4.1. The screen shows the AP name, its password, and that URL,
// so no cable, app, or second computer is involved.
//
// Design notes
// ------------
// * The form lists nearby networks from a real scan, so the SSID is chosen
//   rather than typed — the most common provisioning mistake is a typo in an
//   invisible field.
// * Submitting saves to NVS and reboots. Rebooting (rather than reconnecting in
//   place) guarantees every subsystem sees the new values; the dial is back in
//   about two seconds.
// * The AP password is derived from the chip MAC: unique per board, stable
//   across reboots, and printable on screen. An open AP would let any passer-by
//   post a bridge address to the device.

#pragma once

#include <Arduino.h>

class Provisioner {
 public:
  /**
   * Start the access point and the configuration web server.
   *
   * @param reason shown on the dial so the user knows why setup opened
   *               (e.g. "首次配置" or "WiFi 连接失败").
   */
  void begin(const char* reason);

  /** Service pending HTTP requests. Call from the main loop. */
  void loop();

  /** Shut the AP and server down. */
  void end();

  /** True while the portal is running. */
  bool isActive() const { return active_; }

  /** The AP SSID currently being broadcast. */
  const String& apSsid() const { return apSsid_; }

  /** The AP password currently required. */
  const String& apPassword() const { return apPassword_; }

  /** Milliseconds since the portal opened. */
  unsigned long uptimeMs() const;

  /** True once a client has saved settings (a reboot follows). */
  bool didSave() const { return saved_; }

 private:
  void handleRoot();
  void handleCurrent();
  void handleSave();
  void handleScan();
  void handleNotFound();

  /** Derive a stable per-device AP name and password from the chip MAC. */
  void deriveCredentials();

  bool active_ = false;
  bool saved_ = false;
  String apSsid_;
  String apPassword_;
  String reason_;
  unsigned long startedMs_ = 0;
  /** Scratch buffer for /current, so the response needs no heap churn. */
  char jsonBuf_[224] = {0};
};

extern Provisioner provisioner;