// Copyright (c) 2026 DSH ESP32 dial project.
// SPDX-License-Identifier: MIT
//
// Runtime-editable settings, persisted in NVS.
//
// WiFi credentials and the bridge address are values a user changes — a new
// router, a moved PC, a rotated token — so they must not require a reflash.
// Everything here lives in flash under DSH_NVS_NAMESPACE and is edited through
// the provisioning portal or the serial console.
//
// The Config.h macros are only first-boot seeds: on the very first start their
// values are copied into NVS, and from then on NVS is the source of truth.

#pragma once

#include <Arduino.h>

struct DialSettings {
  String wifiSsid;
  String wifiPassword;
  String bridgeHost;
  uint16_t bridgePort = 3082;
  String bridgeToken;

  /**
   * True when there is enough information to attempt normal operation.
   *
   * An empty token is treated as unconfigured on purpose: the bridge would
   * answer 401 and the dial would loop forever on a failure the user cannot
   * see. Better to open the portal and ask.
   */
  bool isComplete() const {
    return wifiSsid.length() > 0 && bridgeHost.length() > 0 &&
           bridgeToken.length() > 0;
  }
};

class SettingsStore {
 public:
  /** Open NVS and load settings, seeding from Config.h on first boot. */
  void begin();

  /** The live settings. */
  DialSettings& get() { return settings_; }
  const DialSettings& get() const { return settings_; }

  /** Persist the current settings to NVS. Returns false on write failure. */
  bool save();

  /** Overwrite WiFi credentials and persist. */
  bool setWifi(const String& ssid, const String& password);

  /** Overwrite bridge details and persist. */
  bool setBridge(const String& host, uint16_t port, const String& token);

  /** Erase everything and return to first-boot state. */
  bool factoryReset();

  /** True when this boot found no usable stored configuration. */
  bool wasUnconfigured() const { return wasUnconfigured_; }

  /** Human-readable dump for the serial console (token is masked). */
  String describe() const;

 private:
  DialSettings settings_;
  bool wasUnconfigured_ = true;
};

extern SettingsStore settingsStore;