// Copyright (c) 2026 DSH ESP32 dial project.
// SPDX-License-Identifier: MIT

#include "AppShell.h"

#include "PetState.h"

LV_FONT_DECLARE(dsh_font_cjk_16);

static constexpr lv_color_t kShellBg = LV_COLOR_MAKE(0x0D, 0x11, 0x1D);
static constexpr lv_color_t kShellBlue = LV_COLOR_MAKE(0x1E, 0x88, 0xE5);
static constexpr lv_color_t kShellGreen = LV_COLOR_MAKE(0x4C, 0xAF, 0x50);
static constexpr lv_color_t kShellPurple = LV_COLOR_MAKE(0x7C, 0x4D, 0xFF);
static constexpr lv_color_t kShellGray = LV_COLOR_MAKE(0x42, 0x47, 0x58);
static constexpr lv_color_t kShellWhite = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF);

AppShell appShell;

static void styleButton(lv_obj_t* button, lv_color_t colour) {
  lv_obj_set_style_radius(button, 14, 0);
  lv_obj_set_style_bg_color(button, colour, 0);
  lv_obj_set_style_bg_opa(button, 220, 0);
  lv_obj_set_style_border_width(button, 0, 0);
}

static lv_obj_t* makeLabel(lv_obj_t* parent, const char* text,
                           lv_coord_t size, lv_color_t colour) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, &dsh_font_cjk_16, 0);
  lv_obj_set_style_text_color(label, colour, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(label, text);
  if (size > 0) lv_obj_set_width(label, size);
  return label;
}

void AppShell::begin() {
  if (root_ != nullptr) return;
  root_ = lv_obj_create(lv_scr_act());
  lv_obj_set_size(root_, 360, 360);
  lv_obj_set_pos(root_, 0, 0);
  lv_obj_set_style_radius(root_, 180, 0);
  lv_obj_set_style_border_width(root_, 0, 0);
  lv_obj_set_style_bg_color(root_, kShellBg, 0);
  lv_obj_set_style_bg_opa(root_, 255, 0);
  lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

  buildLauncher();
  buildAppPage();
  showLauncher();
  open(AppId::Codex);
}

void AppShell::buildLauncher() {
  launcherPage_ = lv_obj_create(root_);
  lv_obj_set_size(launcherPage_, 360, 360);
  lv_obj_set_pos(launcherPage_, 0, 0);
  lv_obj_set_style_bg_opa(launcherPage_, 0, 0);
  lv_obj_set_style_border_width(launcherPage_, 0, 0);
  lv_obj_clear_flag(launcherPage_, LV_OBJ_FLAG_SCROLLABLE);

  title_ = makeLabel(launcherPage_, "小蓝桌面", 300, kShellWhite);
  lv_obj_align(title_, LV_ALIGN_TOP_MID, 0, 24);
  subtitle_ = makeLabel(launcherPage_, "选择一个 App", 300, kShellGray);
  lv_obj_align(subtitle_, LV_ALIGN_TOP_MID, 0, 48);

  // A calm centre badge is temporary until the real sprite renderer is wired
  // in; it gives the shell a clear focal point instead of a button grid.
  lv_obj_t* petBadge = lv_obj_create(launcherPage_);
  lv_obj_set_size(petBadge, 86, 86);
  lv_obj_align(petBadge, LV_ALIGN_CENTER, 0, -30);
  lv_obj_set_style_radius(petBadge, 43, 0);
  lv_obj_set_style_bg_color(petBadge, kShellBlue, 0);
  lv_obj_set_style_bg_opa(petBadge, 230, 0);
  lv_obj_set_style_border_width(petBadge, 0, 0);
  lv_obj_t* petLabel = makeLabel(petBadge, "小蓝", 80, kShellWhite);
  lv_obj_center(petLabel);

  const char* names[] = {"Codex", "时钟", "计分板"};
  const char* hints[] = {"工作状态", "Pocket Watch", "Scoreboard"};
  const lv_color_t colours[] = {kShellBlue, kShellPurple, kShellGreen};
  const lv_coord_t xs[] = {-112, 0, 112};
  for (uint8_t i = 0; i < 3; ++i) {
    lv_obj_t* button = lv_btn_create(launcherPage_);
    lv_obj_set_size(button, 96, 58);
    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, xs[i], -42);
    styleButton(button, colours[i]);
    lv_obj_add_event_cb(button, launcherButtonClicked, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    lv_obj_t* name = makeLabel(button, names[i], 90, kShellWhite);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_t* hint = makeLabel(button, hints[i], 90, kShellWhite);
    lv_obj_set_style_text_opa(hint, 180, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
  }
}

void AppShell::buildAppPage() {
  appPage_ = lv_obj_create(root_);
  lv_obj_set_size(appPage_, 360, 360);
  lv_obj_set_pos(appPage_, 0, 0);
  lv_obj_set_style_bg_opa(appPage_, 0, 0);
  lv_obj_set_style_border_width(appPage_, 0, 0);
  lv_obj_clear_flag(appPage_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(appPage_, LV_OBJ_FLAG_HIDDEN);

  appTitle_ = makeLabel(appPage_, "", 280, kShellWhite);
  lv_obj_align(appTitle_, LV_ALIGN_TOP_MID, 0, 30);
  appBody_ = makeLabel(appPage_, "", 300, kShellWhite);
  lv_obj_set_style_text_font(appBody_, &lv_font_montserrat_32, 0);
  lv_obj_align(appBody_, LV_ALIGN_CENTER, 0, -10);

  lv_obj_t* back = lv_btn_create(appPage_);
  lv_obj_set_size(back, 110, 42);
  lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -28);
  styleButton(back, kShellGray);
  lv_obj_add_event_cb(back, appBackClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* backLabel = makeLabel(back, "返回桌面", 100, kShellWhite);
  lv_obj_center(backLabel);

  lv_obj_t* minus = lv_btn_create(appPage_);
  lv_obj_set_size(minus, 54, 42);
  lv_obj_align(minus, LV_ALIGN_CENTER, -76, 48);
  styleButton(minus, kShellPurple);
  lv_obj_add_event_cb(minus, scoreButtonClicked, LV_EVENT_CLICKED,
                      reinterpret_cast<void*>(static_cast<uintptr_t>(0)));
  lv_obj_t* minusLabel = makeLabel(minus, "-", 50, kShellWhite);
  lv_obj_center(minusLabel);

  lv_obj_t* plus = lv_btn_create(appPage_);
  lv_obj_set_size(plus, 54, 42);
  lv_obj_align(plus, LV_ALIGN_CENTER, 76, 48);
  styleButton(plus, kShellGreen);
  lv_obj_add_event_cb(plus, scoreButtonClicked, LV_EVENT_CLICKED,
                      reinterpret_cast<void*>(static_cast<uintptr_t>(1)));
  lv_obj_t* plusLabel = makeLabel(plus, "+", 50, kShellWhite);
  lv_obj_center(plusLabel);

  scoreLeft_ = makeLabel(appPage_, "0", 60, kShellWhite);
  scoreRight_ = makeLabel(appPage_, "0", 60, kShellWhite);
  lv_obj_set_style_text_font(scoreLeft_, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_font(scoreRight_, &lv_font_montserrat_20, 0);
  lv_obj_align(scoreLeft_, LV_ALIGN_CENTER, -70, -24);
  lv_obj_align(scoreRight_, LV_ALIGN_CENTER, 70, -24);
}

void AppShell::showLauncher() {
  lv_obj_clear_flag(launcherPage_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(appPage_, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(subtitle_, "选择一个 App");
}

void AppShell::showApp(AppId app) {
  lv_obj_add_flag(launcherPage_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(appPage_, LV_OBJ_FLAG_HIDDEN);
  switch (app) {
    case AppId::PocketWatch:
      lv_label_set_text(appTitle_, "Pocket Watch");
      lv_label_set_text(appBody_, "--:--");
      break;
    case AppId::Scoreboard:
      lv_label_set_text(appTitle_, "Scoreboard");
      lv_label_set_text(appBody_, "SCORE");
      break;
    default:
      lv_label_set_text(appTitle_, "");
      lv_label_set_text(appBody_, "");
      break;
  }
}

void AppShell::open(AppId app) {
  if (root_ == nullptr) return;
  active_ = app;
  if (app == AppId::Codex) {
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(root_);
  if (app == AppId::Desktop) showLauncher();
  else showApp(app);
}

void AppShell::toggleDesktop() {
  open(active_ == AppId::Desktop ? AppId::Codex : AppId::Desktop);
}

void AppShell::onCodexBackgroundTap() {
  if (active_ != AppId::Codex) return;
  const unsigned long now = millis();
  if (lastCodexTapMs_ != 0 && now - lastCodexTapMs_ <= 450) {
    lastCodexTapMs_ = 0;
    open(AppId::Desktop);
  } else {
    lastCodexTapMs_ = now;
  }
}

void AppShell::updateScoreLabels() {
  if (scoreLeft_ == nullptr || scoreRight_ == nullptr) return;
  char left[12];
  char right[12];
  snprintf(left, sizeof(left), "%d", scoreLeftValue_);
  snprintf(right, sizeof(right), "%d", scoreRightValue_);
  lv_label_set_text(scoreLeft_, left);
  lv_label_set_text(scoreRight_, right);
}

void AppShell::launcherButtonClicked(lv_event_t* event) {
  const uint8_t index = static_cast<uint8_t>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  if (index == 0) appShell.open(AppId::Codex);
  else if (index == 1) appShell.open(AppId::PocketWatch);
  else appShell.open(AppId::Scoreboard);
}

void AppShell::appBackClicked(lv_event_t* /*event*/) {
  appShell.open(AppId::Desktop);
}

void AppShell::scoreButtonClicked(lv_event_t* event) {
  if (appShell.active_ != AppId::Scoreboard) return;
  const uint8_t index = static_cast<uint8_t>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  if (index == 0) --appShell.scoreLeftValue_;
  else ++appShell.scoreRightValue_;
  appShell.updateScoreLabels();
}
