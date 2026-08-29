// Copyright (c) 2026 DSH ESP32 dial project.
// SPDX-License-Identifier: MIT
//
// The launcher is a small LVGL 8 port of the visual system used by
// STUPIDDDD0/waveshare-ESP32-S3-Touch-LCD-1.85B-desktop (ESP-Brookesia):
// fixed status bar, four-column app grid, page indicator and clean app pages.
// The original project uses ESP-IDF/LVGL 9, while this firmware uses
// Arduino/LVGL 8, so only the UI data/layout is ported here.

#include "AppShell.h"

#include <time.h>

#include "PetState.h"

LV_FONT_DECLARE(lv_font_simsun_16_cjk);

static constexpr lv_color_t kBg = LV_COLOR_MAKE(0x1A, 0x1A, 0x1A);
static constexpr lv_color_t kPanel = LV_COLOR_MAKE(0x27, 0x29, 0x31);
static constexpr lv_color_t kPanelPressed = LV_COLOR_MAKE(0x35, 0x38, 0x44);
static constexpr lv_color_t kWhite = LV_COLOR_MAKE(0xF8, 0xF9, 0xFC);
static constexpr lv_color_t kMuted = LV_COLOR_MAKE(0x9A, 0xA0, 0xAE);
static constexpr lv_color_t kBlue = LV_COLOR_MAKE(0x2D, 0x98, 0xDA);
static constexpr lv_color_t kPurple = LV_COLOR_MAKE(0x8E, 0x6C, 0xFF);
static constexpr lv_color_t kGreen = LV_COLOR_MAKE(0x20, 0xBF, 0x6B);
static constexpr lv_color_t kOrange = LV_COLOR_MAKE(0xF2, 0x9E, 0x38);

AppShell appShell;

namespace {

static lv_obj_t* label(lv_obj_t* parent, const char* text, lv_color_t color,
                        lv_coord_t width = LV_SIZE_CONTENT) {
  lv_obj_t* obj = lv_label_create(parent);
  lv_obj_set_style_text_font(obj, &lv_font_simsun_16_cjk, 0);
  lv_obj_set_style_text_color(obj, color, 0);
  lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(obj, text != nullptr ? text : "");
  if (width != LV_SIZE_CONTENT) lv_obj_set_width(obj, width);
  return obj;
}

static lv_obj_t* glyph(lv_obj_t* parent, const char* text, lv_color_t color,
                        lv_coord_t size = 34) {
  lv_obj_t* obj = lv_label_create(parent);
  lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(obj, color, 0);
  lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_size(obj, size, size);
  lv_label_set_text(obj, text);
  lv_obj_center(obj);
  return obj;
}

static void cardStyle(lv_obj_t* card, lv_color_t color) {
  lv_obj_set_style_radius(card, 18, 0);
  lv_obj_set_style_bg_color(card, color, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 10, 0);
  lv_obj_set_style_shadow_opa(card, 45, 0);
  lv_obj_set_style_shadow_color(card, LV_COLOR_MAKE(0x00, 0x00, 0x00), 0);
  lv_obj_set_style_bg_color(card, kPanelPressed, LV_STATE_PRESSED);
}

static void flatStyle(lv_obj_t* obj) {
  lv_obj_set_style_bg_opa(obj, 0, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

}  // namespace

void AppShell::begin() {
  if (root_ != nullptr) return;

  root_ = lv_obj_create(lv_scr_act());
  lv_obj_set_size(root_, 360, 360);
  lv_obj_set_pos(root_, 0, 0);
  lv_obj_set_style_radius(root_, 180, 0);
  lv_obj_set_style_bg_color(root_, kBg, 0);
  lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(root_, 0, 0);
  lv_obj_set_style_pad_all(root_, 0, 0);
  lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

  buildLauncher();
  buildAppPage();

  active_ = AppId::Desktop;
  showLauncher();
  open(AppId::Codex);

  // The upstream desktop keeps the clock in the fixed status bar. Updating it
  // from an LVGL timer avoids doing UI work in the Arduino loop and keeps all
  // label mutations on LVGL's normal timer path.
  lv_timer_create([](lv_timer_t* timer) {
    AppShell* shell = static_cast<AppShell*>(timer->user_data);
    if (shell == nullptr || shell->statusClock_ == nullptr) return;
    time_t now = time(nullptr);
    struct tm info;
    localtime_r(&now, &info);
    char text[8];
    snprintf(text, sizeof(text), "%02d:%02d", info.tm_hour, info.tm_min);
    lv_label_set_text(shell->statusClock_, text);
  }, 1000, this);
}

void AppShell::buildLauncher() {
  launcherPage_ = lv_obj_create(root_);
  lv_obj_set_size(launcherPage_, 360, 360);
  lv_obj_set_pos(launcherPage_, 0, 0);
  flatStyle(launcherPage_);

  // Fixed status bar, matching the upstream phone desktop's compact top row.
  lv_obj_t* status = lv_obj_create(launcherPage_);
  lv_obj_set_size(status, 286, 28);
  lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 12);
  flatStyle(status);
  statusClock_ = label(status, "--:--", kWhite, 70);
  lv_obj_align(statusClock_, LV_ALIGN_LEFT_MID, 2, 0);
  statusWifi_ = label(status, LV_SYMBOL_WIFI, kMuted, 28);
  lv_obj_set_style_text_font(statusWifi_, &lv_font_montserrat_16, 0);
  lv_obj_align(statusWifi_, LV_ALIGN_RIGHT_MID, -40, 0);
  statusBattery_ = label(status, LV_SYMBOL_BATTERY_3, kMuted, 28);
  lv_obj_set_style_text_font(statusBattery_, &lv_font_montserrat_16, 0);
  lv_obj_align(statusBattery_, LV_ALIGN_RIGHT_MID, -4, 0);

  title_ = label(launcherPage_, "小蓝桌面", kWhite, 260);
  lv_obj_align(title_, LV_ALIGN_TOP_MID, 0, 42);
  subtitle_ = label(launcherPage_, "选择一个应用", kMuted, 260);
  lv_obj_align(subtitle_, LV_ALIGN_TOP_MID, 0, 64);

  // Four equal app slots are intentional: this is the most recognizable
  // part of the upstream AppLauncher and leaves enough air on a round panel.
  const char* names[] = {"Codex", "时钟", "计分板", "设置"};
  const char* icons[] = {LV_SYMBOL_PLAY, LV_SYMBOL_REFRESH, LV_SYMBOL_LIST,
                         LV_SYMBOL_SETTINGS};
  const lv_color_t colors[] = {kBlue, kPurple, kGreen, kOrange};
  for (uint8_t i = 0; i < 4; ++i) {
    lv_obj_t* card = lv_btn_create(launcherPage_);
    lv_obj_set_size(card, 70, 92);
    lv_obj_align(card, LV_ALIGN_TOP_LEFT, 18 + static_cast<lv_coord_t>(i) * 82,
                 94);
    cardStyle(card, colors[i]);
    lv_obj_add_event_cb(card, launcherButtonClicked, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    lv_obj_t* icon = glyph(card, icons[i], kWhite, 36);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 13);
    lv_obj_t* name = label(card, names[i], kWhite, 66);
    lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -10);
  }

  // A single status card gives the home page a useful focal point without
  // stacking another translucent screen over DialUi (the old cause of ghosting).
  lv_obj_t* statusCard = lv_obj_create(launcherPage_);
  lv_obj_set_size(statusCard, 286, 68);
  lv_obj_align(statusCard, LV_ALIGN_TOP_MID, 0, 206);
  lv_obj_set_style_radius(statusCard, 18, 0);
  lv_obj_set_style_bg_color(statusCard, kPanel, 0);
  lv_obj_set_style_bg_opa(statusCard, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(statusCard, 1, 0);
  lv_obj_set_style_border_color(statusCard, LV_COLOR_MAKE(0x3B, 0x3E, 0x4A), 0);
  lv_obj_set_style_pad_all(statusCard, 12, 0);
  lv_obj_clear_flag(statusCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* statusCaption = label(statusCard, "桌宠状态", kMuted, 80);
  lv_obj_align(statusCaption, LV_ALIGN_TOP_LEFT, 0, 0);
  statusValue_ = label(statusCard, "离线 · 等待桥接", kWhite, 190);
  lv_obj_align(statusValue_, LV_ALIGN_TOP_LEFT, 72, 0);
  lv_obj_t* hint = label(statusCard, "双击工作页返回桌面", kMuted, 250);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  // Upstream uses a pill-shaped page indicator. There is one launcher page for
  // now, but keeping the indicator makes adding a second page non-breaking.
  lv_obj_t* dot = lv_obj_create(launcherPage_);
  lv_obj_set_size(dot, 34, 7);
  lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_radius(dot, 4, 0);
  lv_obj_set_style_bg_color(dot, kWhite, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
}

void AppShell::buildAppPage() {
  appPage_ = lv_obj_create(root_);
  lv_obj_set_size(appPage_, 360, 360);
  lv_obj_set_pos(appPage_, 0, 0);
  flatStyle(appPage_);
  lv_obj_add_flag(appPage_, LV_OBJ_FLAG_HIDDEN);
}

void AppShell::showLauncher() {
  lv_obj_clear_flag(launcherPage_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(appPage_, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(subtitle_, "选择一个应用");
  lv_label_set_text(statusValue_, active_ == AppId::Desktop ? "选择应用" : "离线 · 等待桥接");
}

void AppShell::showApp(AppId app) {
  // Build only the active page. This removes stale label/image nodes before a
  // new app appears, which prevents the old sibling-layer ghosting artifact.
  lv_obj_clean(appPage_);
  lv_obj_clear_flag(appPage_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(launcherPage_, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* top = lv_obj_create(appPage_);
  lv_obj_set_size(top, 320, 42);
  lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 12);
  flatStyle(top);
  lv_obj_t* back = lv_btn_create(top);
  lv_obj_set_size(back, 42, 38);
  lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
  cardStyle(back, kPanel);
  lv_obj_add_event_cb(back, appBackClicked, LV_EVENT_CLICKED, nullptr);
  glyph(back, LV_SYMBOL_LEFT, kWhite, 28);

  const char* title = "应用";
  if (app == AppId::PocketWatch) title = "Pocket Watch";
  else if (app == AppId::Scoreboard) title = "Scoreboard";
  else if (app == AppId::Settings) title = "设置";
  lv_obj_t* heading = label(top, title, kWhite, 220);
  lv_obj_align(heading, LV_ALIGN_CENTER, 0, 0);

  if (app == AppId::PocketWatch) {
    lv_obj_t* clock = label(appPage_, "--:--", kWhite, 260);
    lv_obj_set_style_text_font(clock, &lv_font_montserrat_32, 0);
    lv_obj_align(clock, LV_ALIGN_CENTER, 0, -18);
    lv_obj_t* caption = label(appPage_, "本地时间", kMuted, 220);
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, 26);
  } else if (app == AppId::Scoreboard) {
    appBody_ = label(appPage_, "SCOREBOARD", kWhite, 260);
    lv_obj_set_style_text_font(appBody_, &lv_font_montserrat_20, 0);
    lv_obj_align(appBody_, LV_ALIGN_CENTER, 0, -50);
    scoreLeft_ = label(appPage_, "0", kWhite, 80);
    scoreRight_ = label(appPage_, "0", kWhite, 80);
    lv_obj_set_style_text_font(scoreLeft_, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_font(scoreRight_, &lv_font_montserrat_32, 0);
    lv_obj_align(scoreLeft_, LV_ALIGN_CENTER, -74, -2);
    lv_obj_align(scoreRight_, LV_ALIGN_CENTER, 74, -2);
    const char* buttons[] = {"-", "+"};
    const lv_color_t buttonColors[] = {kPurple, kGreen};
    for (uint8_t i = 0; i < 2; ++i) {
      lv_obj_t* button = lv_btn_create(appPage_);
      lv_obj_set_size(button, 56, 42);
      lv_obj_align(button, LV_ALIGN_CENTER, i == 0 ? -74 : 74, 48);
      cardStyle(button, buttonColors[i]);
      lv_obj_add_event_cb(button, scoreButtonClicked, LV_EVENT_CLICKED,
                          reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
      lv_obj_t* text = label(button, buttons[i], kWhite, 50);
      lv_obj_set_style_text_font(text, &lv_font_montserrat_20, 0);
      lv_obj_center(text);
    }
    updateScoreLabels();
  } else if (app == AppId::Settings) {
    lv_obj_t* body = label(appPage_, "WiFi · 桥接 · 亮度", kWhite, 290);
    lv_obj_align(body, LV_ALIGN_CENTER, 0, -18);
    lv_obj_t* hint = label(appPage_, "长按工作页可重新配置", kMuted, 290);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 18);
  } else {
    lv_obj_t* body = label(appPage_, "桌宠工作状态由 Codex 桥接驱动", kWhite, 290);
    lv_obj_align(body, LV_ALIGN_CENTER, 0, -18);
    lv_obj_t* hint = label(appPage_, "双击背景返回桌面", kMuted, 250);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 18);
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
  lv_obj_invalidate(root_);
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
  else if (index == 2) appShell.open(AppId::Scoreboard);
  else appShell.open(AppId::Settings);
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
