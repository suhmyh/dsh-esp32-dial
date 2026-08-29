// Copyright (c) 2026 DSH ESP32 dial project.
// SPDX-License-Identifier: MIT
//
// App container for the round desktop. Individual apps own their content; the
// shell owns navigation and guarantees that approval overlays remain modal.

#pragma once

#include <Arduino.h>
#include <lvgl.h>

enum class AppId : uint8_t {
  Desktop,
  Codex,
  PocketWatch,
  Scoreboard,
  Settings,
};

class AppShell {
 public:
  void begin();
  void open(AppId app);
  void toggleDesktop();
  void onCodexBackgroundTap();

  bool isDesktop() const { return active_ == AppId::Desktop; }
  AppId activeApp() const { return active_; }

 private:
  lv_obj_t* root_ = nullptr;
  lv_obj_t* launcherPage_ = nullptr;
  lv_obj_t* appPage_ = nullptr;
  lv_obj_t* title_ = nullptr;
  lv_obj_t* subtitle_ = nullptr;
  lv_obj_t* appTitle_ = nullptr;
  lv_obj_t* appBody_ = nullptr;
  lv_obj_t* scoreLeft_ = nullptr;
  lv_obj_t* scoreRight_ = nullptr;
  int scoreLeftValue_ = 0;
  int scoreRightValue_ = 0;
  AppId active_ = AppId::Codex;
  unsigned long lastCodexTapMs_ = 0;

  void buildLauncher();
  void buildAppPage();
  void showLauncher();
  void showApp(AppId app);
  void updateScoreLabels();

  static void launcherButtonClicked(lv_event_t* event);
  static void appBackClicked(lv_event_t* event);
  static void scoreButtonClicked(lv_event_t* event);
};

extern AppShell appShell;
