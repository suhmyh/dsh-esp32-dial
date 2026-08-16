// Copyright (c) 2026 DSH ESP32 dial project.
// SPDX-License-Identifier: MIT
//
// Serial console implementation. See Console.h for the rationale.

#include "Console.h"

#include <WiFi.h>

#include "Config.h"
#include "DialUi.h"
#include "Settings.h"

Console console;

void Console::begin() {
  buffer_.reserve(128);
  Serial.println("[console] ready — type `help`");
}

void Console::loop() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      String line = buffer_;
      buffer_ = "";
      line.trim();
      if (line.length() > 0) execute(line);
      continue;
    }
    // Drop anything absurdly long rather than growing without bound.
    if (buffer_.length() < 200) buffer_ += c;
  }
}

void Console::printHelp() const {
  Serial.println();
  Serial.println("DSH dial console");
  Serial.println("  help                     这份帮助");
  Serial.println("  status                   当前配置和连接状态");
  Serial.println("  wifi <ssid> [password]   设置 WiFi（保存后重启生效）");
  Serial.println("  bridge <host> [port]     设置桥接地址");
  Serial.println("  token <token>            设置设备令牌");
  Serial.println("  scan                     扫描附近的 WiFi");
  Serial.println("  reboot                   重启");
  Serial.println("  reset                    恢复出厂设置并重启");
  Serial.println();
  Serial.println("例: wifi MyRouter hunter2");
  Serial.println("    bridge 192.168.1.20 3082");
  Serial.println();
}

void Console::printStatus() const {
  Serial.println();
  Serial.printf("settings : %s\n", settingsStore.describe().c_str());
  Serial.printf("wifi     : %s",
                WiFi.status() == WL_CONNECTED ? "connected" : "disconnected");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf(" ip=%s rssi=%ddBm", WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
  }
  Serial.println();
  Serial.printf("screen   : %s\n",
                dialUi.isProvisioning()  ? "provisioning"
                : dialUi.isWaiting()     ? "waiting for a decision"
                : dialUi.isOffline()     ? "offline"
                                         : "dial");
  Serial.printf("heap     : %u bytes free\n", ESP.getFreeHeap());
  Serial.printf("uptime   : %lus\n", millis() / 1000);
  Serial.println();
}

void Console::execute(const String& line) {
  // Split into at most three whitespace-separated arguments; a WiFi password
  // may legitimately contain most other characters, so only the first two
  // boundaries are treated as separators.
  const int firstSpace = line.indexOf(' ');
  const String command = firstSpace < 0 ? line : line.substring(0, firstSpace);
  const String rest =
      firstSpace < 0 ? String("") : line.substring(firstSpace + 1);

  if (command.equalsIgnoreCase("help") || command == "?") {
    printHelp();
    return;
  }

  if (command.equalsIgnoreCase("status")) {
    printStatus();
    return;
  }

  if (command.equalsIgnoreCase("wifi")) {
    if (rest.length() == 0) {
      Serial.println("用法: wifi <ssid> [password]");
      return;
    }
    const int split = rest.indexOf(' ');
    const String ssid = split < 0 ? rest : rest.substring(0, split);
    const String pass = split < 0 ? String("") : rest.substring(split + 1);
    if (settingsStore.setWifi(ssid, pass)) {
      Serial.printf("WiFi 已设为 %s — 输入 reboot 生效\n", ssid.c_str());
    }
    return;
  }

  if (command.equalsIgnoreCase("bridge")) {
    if (rest.length() == 0) {
      Serial.println("用法: bridge <host> [port]");
      return;
    }
    const int split = rest.indexOf(' ');
    const String host = split < 0 ? rest : rest.substring(0, split);
    const uint16_t port =
        split < 0 ? 0 : static_cast<uint16_t>(rest.substring(split + 1).toInt());
    if (settingsStore.setBridge(host, port, String(""))) {
      Serial.printf("桥接已设为 %s:%u — 输入 reboot 生效\n", host.c_str(),
                    port != 0 ? port : settingsStore.get().bridgePort);
    }
    return;
  }

  if (command.equalsIgnoreCase("token")) {
    if (rest.length() == 0) {
      Serial.println("用法: token <token>");
      return;
    }
    if (settingsStore.setBridge(settingsStore.get().bridgeHost,
                                settingsStore.get().bridgePort, rest)) {
      Serial.println("令牌已保存 — 输入 reboot 生效");
    }
    return;
  }

  if (command.equalsIgnoreCase("scan")) {
    Serial.println("扫描中…");
    const int n = WiFi.scanNetworks();
    for (int i = 0; i < n; ++i) {
      Serial.printf("  %-32s %4ddBm %s\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                    WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "开放" : "加密");
    }
    WiFi.scanDelete();
    Serial.printf("共 %d 个网络\n", n);
    return;
  }

  if (command.equalsIgnoreCase("reboot")) {
    Serial.println("重启…");
    delay(200);
    ESP.restart();
    return;
  }

  if (command.equalsIgnoreCase("reset")) {
    Serial.println("恢复出厂设置…");
    settingsStore.factoryReset();
    delay(200);
    ESP.restart();
    return;
  }

  Serial.printf("未知命令: %s（输入 help）\n", command.c_str());
}