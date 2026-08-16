// Copyright (c) 2026 DSH ESP32 dial project.
// SPDX-License-Identifier: MIT
//
// NVS-backed settings implementation. See Settings.h for the rationale.

#include "Settings.h"

#include <Preferences.h>

#include "Config.h"

SettingsStore settingsStore;

namespace {
/** NVS keys. Short names: NVS caps key length at 15 characters. */
constexpr const char* kKeySsid = "ssid";
constexpr const char* kKeyPass = "pass";
constexpr const char* kKeyHost = "host";
constexpr const char* kKeyPort = "port";
constexpr const char* kKeyToken = "token";
constexpr const char* kKeySeeded = "seeded";
}  // namespace

void SettingsStore::begin() {
  Preferences prefs;
  if (!prefs.begin(DSH_NVS_NAMESPACE, /*readOnly=*/false)) {
    // NVS unavailable (corrupt partition, or first use before format). Fall
    // back to the compiled seeds so the dial still runs; nothing persists.
    Serial.println("[settings] NVS unavailable — using compiled defaults only");
    settings_.wifiSsid = DSH_DEFAULT_WIFI_SSID;
    settings_.wifiPassword = DSH_DEFAULT_WIFI_PASSWORD;
    settings_.bridgeHost = DSH_DEFAULT_BRIDGE_HOST;
    settings_.bridgePort = DSH_DEFAULT_BRIDGE_PORT;
    settings_.bridgeToken = DSH_DEFAULT_BRIDGE_TOKEN;
    wasUnconfigured_ = !settings_.isComplete();
    return;
  }

  const bool seeded = prefs.getBool(kKeySeeded, false);
  if (!seeded) {
    // First boot: copy the compiled seeds in, then mark as seeded so later
    // firmware updates never clobber values the user has since changed.
    settings_.wifiSsid = DSH_DEFAULT_WIFI_SSID;
    settings_.wifiPassword = DSH_DEFAULT_WIFI_PASSWORD;
    settings_.bridgeHost = DSH_DEFAULT_BRIDGE_HOST;
    settings_.bridgePort = DSH_DEFAULT_BRIDGE_PORT;
    settings_.bridgeToken = DSH_DEFAULT_BRIDGE_TOKEN;

    prefs.putString(kKeySsid, settings_.wifiSsid);
    prefs.putString(kKeyPass, settings_.wifiPassword);
    prefs.putString(kKeyHost, settings_.bridgeHost);
    prefs.putUShort(kKeyPort, settings_.bridgePort);
    prefs.putString(kKeyToken, settings_.bridgeToken);
    prefs.putBool(kKeySeeded, true);
    Serial.println("[settings] first boot — seeded from Config.h");
  } else {
    settings_.wifiSsid = prefs.getString(kKeySsid, "");
    settings_.wifiPassword = prefs.getString(kKeyPass, "");
    settings_.bridgeHost = prefs.getString(kKeyHost, "");
    settings_.bridgePort = prefs.getUShort(kKeyPort, DSH_DEFAULT_BRIDGE_PORT);
    settings_.bridgeToken = prefs.getString(kKeyToken, "");
    Serial.println("[settings] loaded from NVS");
  }
  prefs.end();

  wasUnconfigured_ = !settings_.isComplete();
  Serial.printf("[settings] %s\n", describe().c_str());
}

bool SettingsStore::save() {
  Preferences prefs;
  if (!prefs.begin(DSH_NVS_NAMESPACE, /*readOnly=*/false)) {
    Serial.println("[settings] save failed: NVS unavailable");
    return false;
  }
  prefs.putString(kKeySsid, settings_.wifiSsid);
  prefs.putString(kKeyPass, settings_.wifiPassword);
  prefs.putString(kKeyHost, settings_.bridgeHost);
  prefs.putUShort(kKeyPort, settings_.bridgePort);
  prefs.putString(kKeyToken, settings_.bridgeToken);
  prefs.putBool(kKeySeeded, true);
  prefs.end();
  Serial.println("[settings] saved");
  return true;
}

bool SettingsStore::setWifi(const String& ssid, const String& password) {
  settings_.wifiSsid = ssid;
  settings_.wifiPassword = password;
  return save();
}

bool SettingsStore::setBridge(const String& host, uint16_t port,
                              const String& token) {
  settings_.bridgeHost = host;
  if (port > 0) settings_.bridgePort = port;
  if (token.length() > 0) settings_.bridgeToken = token;
  return save();
}

bool SettingsStore::factoryReset() {
  Preferences prefs;
  if (!prefs.begin(DSH_NVS_NAMESPACE, /*readOnly=*/false)) return false;
  const bool cleared = prefs.clear();
  prefs.end();
  settings_ = DialSettings{};
  wasUnconfigured_ = true;
  Serial.println("[settings] factory reset");
  return cleared;
}

String SettingsStore::describe() const {
  // The token is masked: this string goes to the serial console, which may be
  // shared in a screenshot when someone asks for help.
  String masked;
  if (settings_.bridgeToken.length() >= 8) {
    masked = settings_.bridgeToken.substring(0, 4) + "…" +
             settings_.bridgeToken.substring(settings_.bridgeToken.length() - 4);
  } else if (settings_.bridgeToken.length() > 0) {
    masked = "(short)";
  } else {
    masked = "(unset)";
  }

  String out = "wifi=";
  out += settings_.wifiSsid.length() > 0 ? settings_.wifiSsid : "(unset)";
  out += " bridge=";
  out += settings_.bridgeHost.length() > 0 ? settings_.bridgeHost : "(unset)";
  out += ":";
  out += String(settings_.bridgePort);
  out += " token=";
  out += masked;
  out += settings_.isComplete() ? " [complete]" : " [INCOMPLETE]";
  return out;
}