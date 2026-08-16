// Copyright (c) 2026 DSH ESP32 dial project.
// SPDX-License-Identifier: MIT
//
// LVGL round-dial UI implementation. See DialUi.h for the layout rationale.

#include "DialUi.h"

#include <string.h>

#include "Config.h"

// The QR widget comes from LVGL's extras, which `lvgl.h` already includes; it
// exists only when lv_conf.h sets LV_USE_QRCODE, so fail loudly at compile time
// rather than at runtime on a dial that cannot show its own setup code.
#if LV_USE_QRCODE == 0
#error "LV_USE_QRCODE must be 1 in lv_conf.h for the provisioning screen"
#endif

// ── LVGL colours ─────────────────────────────────────────────────────────
// DeepSeek brand blue, plus clean signal colours for the five phases.
static constexpr lv_color_t kBlue = LV_COLOR_MAKE(0x1E, 0x88, 0xE5);
static constexpr lv_color_t kDarkBg = LV_COLOR_MAKE(0x0D, 0x11, 0x1D);
static constexpr lv_color_t kGreen = LV_COLOR_MAKE(0x4C, 0xAF, 0x50);
static constexpr lv_color_t kRed = LV_COLOR_MAKE(0xE5, 0x39, 0x35);
static constexpr lv_color_t kYellow = LV_COLOR_MAKE(0xFF, 0xCC, 0x00);
static constexpr lv_color_t kGray = LV_COLOR_MAKE(0x55, 0x55, 0x55);
static constexpr lv_color_t kWhite = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF);
static constexpr lv_color_t kDim = LV_COLOR_MAKE(0x22, 0x22, 0x33);

// ── static members ───────────────────────────────────────────────────────
lv_style_t DialUi::mainStyle_;
bool DialUi::stylesInitialised_ = false;
DialUi dialUi;  // global instance

// ── colour mapping ───────────────────────────────────────────────────────
lv_color_t DialUi::colorForPhase(DialPhase phase, bool stale) {
  if (stale) return kGray;
  switch (phase) {
    case DialPhase::Idle:     return kDim;
    case DialPhase::Working:  return kBlue;
    case DialPhase::Waiting:  return kYellow;
    case DialPhase::Done:     return kGreen;
    case DialPhase::Error:    return kRed;
    case DialPhase::Offline:  return kGray;
  }
  return kGray;
}

// ── constructor & initialisation ─────────────────────────────────────────

DialUi::DialUi() {}

void DialUi::begin() {
  if (!stylesInitialised_) {
    lv_style_init(&mainStyle_);
    lv_style_set_bg_color(&mainStyle_, kDarkBg);
    lv_style_set_text_color(&mainStyle_, kWhite);
    lv_style_set_radius(&mainStyle_, 180);  // round clip
    stylesInitialised_ = true;
  }

  lv_obj_add_style(lv_scr_act(), &mainStyle_, 0);
  lv_obj_set_style_bg_color(lv_scr_act(), kDarkBg, 0);
  lv_obj_set_style_radius(lv_scr_act(), 180, 0);  // clip to circle

  buildMainScreen();
  buildAskOverlay();
  buildProvisioningScreen();

  // Start in the offline state: nothing has been heard from the bridge yet, and
  // the dial must never imply otherwise.
  setPhaseLabel(DialPhase::Offline);
  lv_obj_set_style_bg_color(statusBg_, colorForPhase(DialPhase::Offline, false), 0);
  lv_label_set_text(titleLabel_, "DSH");
  lv_label_set_text(detailLabel_, "connecting...");
  updateArc(0);
}

// ── main screen layout ───────────────────────────────────────────────────

void DialUi::buildMainScreen() {
  lv_obj_t* scr = lv_scr_act();

  // Background circle — fills the 360×360 area so the phase colour is visible.
  statusBg_ = lv_obj_create(scr);
  lv_obj_set_size(statusBg_, 360, 360);
  lv_obj_set_pos(statusBg_, 0, 0);
  lv_obj_set_style_radius(statusBg_, 180, 0);
  lv_obj_set_style_border_width(statusBg_, 0, 0);
  lv_obj_set_style_bg_color(statusBg_, kDim, 0);
  lv_obj_set_style_bg_opa(statusBg_, 255, 0);

  // A long press anywhere on the dial re-opens provisioning, so a change of
  // network never forces a cable. The arc above swallows its own presses, so
  // the gesture has to live on the full-screen background.
  lv_obj_add_event_cb(statusBg_, [](lv_event_t* event) {
    dialUi.onBackgroundLongPress();
  }, LV_EVENT_LONG_PRESSED, nullptr);

  // Context pressure arc → outer ring, indicator mode.
  arc_ = lv_arc_create(scr);
  lv_obj_set_size(arc_, 340, 340);
  lv_obj_set_pos(arc_, 10, 10);
  lv_arc_set_mode(arc_, LV_ARC_MODE_NORMAL);
  lv_arc_set_range(arc_, 0, 100);
  lv_arc_set_value(arc_, 0);
  lv_arc_set_bg_angles(arc_, 0, 360);
  lv_arc_set_angles(arc_, 0, 0);
  lv_obj_set_style_arc_width(arc_, 8, 0);
  lv_obj_set_style_arc_color(arc_, kGray, LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(arc_, 80, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc_, kDim, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(arc_, 30, LV_PART_MAIN);
  lv_obj_clear_flag(arc_, LV_OBJ_FLAG_CLICKABLE);

  // Phase word — large, centred. It sits inside the ring.
  phaseLabel_ = lv_label_create(scr);
  lv_obj_set_pos(phaseLabel_, 180, 140);
  lv_obj_set_style_text_font(phaseLabel_, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(phaseLabel_, kWhite, 0);
  lv_label_set_text(phaseLabel_, "初始");
  lv_obj_center(phaseLabel_);

  // Title — session title, below the phase word.
  titleLabel_ = lv_label_create(scr);
  lv_obj_align(titleLabel_, LV_ALIGN_TOP_MID, 0, 48);
  lv_obj_set_style_text_font(titleLabel_, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(titleLabel_, kWhite, 0);
  lv_label_set_long_mode(titleLabel_, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(titleLabel_, 280);
  lv_label_set_text(titleLabel_, "");

  // Detail — current action, bottom.
  detailLabel_ = lv_label_create(scr);
  lv_obj_align(detailLabel_, LV_ALIGN_BOTTOM_MID, 0, -24);
  lv_obj_set_style_text_font(detailLabel_, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(detailLabel_, kGray, 0);
  lv_label_set_long_mode(detailLabel_, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(detailLabel_, 300);
  lv_label_set_text(detailLabel_, "");
}

// ── ask overlay ──────────────────────────────────────────────────────────

void DialUi::buildAskOverlay() {
  askBg_ = lv_obj_create(lv_scr_act());
  lv_obj_set_size(askBg_, 360, 360);
  lv_obj_set_pos(askBg_, 0, 0);
  lv_obj_set_style_radius(askBg_, 180, 0);
  lv_obj_set_style_border_width(askBg_, 0, 0);
  lv_obj_set_style_bg_color(askBg_, kYellow, 0);
  lv_obj_set_style_bg_opa(askBg_, 240, 0);
  lv_obj_add_flag(askBg_, LV_OBJ_FLAG_HIDDEN);

  askTitle_ = lv_label_create(askBg_);
  lv_obj_align(askTitle_, LV_ALIGN_TOP_MID, 0, 32);
  lv_obj_set_style_text_font(askTitle_, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(askTitle_, kDarkBg, 0);
  lv_label_set_long_mode(askTitle_, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(askTitle_, 300);
  lv_label_set_text(askTitle_, "");

  askBody_ = lv_label_create(askBg_);
  lv_obj_align(askBody_, LV_ALIGN_TOP_MID, 0, 80);
  lv_obj_set_style_text_font(askBody_, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(askBody_, LV_COLOR_MAKE(0x33, 0x33, 0x33), 0);
  lv_label_set_long_mode(askBody_, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(askBody_, 280);
  lv_label_set_text(askBody_, "");

  // Allow button — left side
  askBtnAllow_ = lv_btn_create(askBg_);
  lv_obj_set_size(askBtnAllow_, 120, 48);
  lv_obj_align(askBtnAllow_, LV_ALIGN_BOTTOM_MID, -70, -24);
  lv_obj_set_style_bg_color(askBtnAllow_, kGreen, 0);
  lv_obj_add_event_cb(askBtnAllow_, askButtonClicked, LV_EVENT_CLICKED,
                      reinterpret_cast<void*>(static_cast<uintptr_t>(0)));
  lv_obj_t* lblAllow = lv_label_create(askBtnAllow_);
  lv_label_set_text(lblAllow, "允许");
  lv_obj_center(lblAllow);

  // Deny button — right side
  askBtnDeny_ = lv_btn_create(askBg_);
  lv_obj_set_size(askBtnDeny_, 120, 48);
  lv_obj_align(askBtnDeny_, LV_ALIGN_BOTTOM_MID, 70, -24);
  lv_obj_set_style_bg_color(askBtnDeny_, kRed, 0);
  lv_obj_add_event_cb(askBtnDeny_, askButtonClicked, LV_EVENT_CLICKED,
                      reinterpret_cast<void*>(static_cast<uintptr_t>(1)));
  lv_obj_t* lblDeny = lv_label_create(askBtnDeny_);
  lv_label_set_text(lblDeny, "拒绝");
  lv_obj_center(lblDeny);
}

// ── state update ─────────────────────────────────────────────────────────

void DialUi::setState(const JsonDocument& doc) {
  // The provisioning screen owns the display while it is up: a state frame
  // arriving mid-setup must not paint over the credentials the user is reading.
  if (provisioning_) return;

  const char* phaseStr = doc["phase"] | "";
  DialPhase newPhase = DialPhase::Idle;

  if (strcmp(phaseStr, "working") == 0) newPhase = DialPhase::Working;
  else if (strcmp(phaseStr, "waiting") == 0) newPhase = DialPhase::Waiting;
  else if (strcmp(phaseStr, "done") == 0) newPhase = DialPhase::Done;
  else if (strcmp(phaseStr, "error") == 0) newPhase = DialPhase::Error;
  else if (strcmp(phaseStr, "idle") == 0) newPhase = DialPhase::Idle;
  else if (strcmp(phaseStr, "offline") == 0) newPhase = DialPhase::Offline;

  // If waiting, we have an ask overlay; the state update does not replace it.
  if (waiting_) return;

  phase_ = newPhase;

  // `done` is a transient that auto-dismisses; track its start.
  if (newPhase == DialPhase::Done) {
    doneShowMs_ = millis();
  } else {
    doneShowMs_ = 0;
  }

  // Update the arc for context pressure, then the labels and background colour.
  uint8_t ctx = doc["ctx"] | 0;
  updateArc(ctx);

  setPhaseLabel(newPhase);
  lv_obj_set_style_bg_color(statusBg_, colorForPhase(newPhase, stale_), 0);

  const char* title = doc["title"] | "";
  if (strlen(title) > 0) lv_label_set_text(titleLabel_, title);
  lv_label_set_text(detailLabel_, doc["detail"] | "");
}

void DialUi::updateArc(uint8_t ctxPercent) {
  if (arc_ == nullptr) return;
  lv_arc_set_value(arc_, ctxPercent);
  // The indicator colour tracks the pressure level: green → yellow → red.
  lv_color_t arcColor;
  if (ctxPercent < 50) arcColor = kGreen;
  else if (ctxPercent < 80) arcColor = kYellow;
  else arcColor = kRed;
  lv_obj_set_style_arc_color(arc_, arcColor, LV_PART_INDICATOR);
}

void DialUi::setPhaseLabel(DialPhase phase) {
  if (phaseLabel_ == nullptr) return;
  const char* text;
  switch (phase) {
    case DialPhase::Idle:    text = "·"; break;
    case DialPhase::Working: text = "⋯"; break;
    case DialPhase::Waiting: text = "?!"; break;
    case DialPhase::Done:    text = "✓"; break;
    case DialPhase::Error:   text = "✕"; break;
    case DialPhase::Offline: text = "⌀"; break;
    default:                 text = "?"; break;
  }
  lv_label_set_text(phaseLabel_, text);
}

// ── ask overlay ──────────────────────────────────────────────────────────

void DialUi::showAsk(const JsonDocument& doc) {
  // While provisioning, there is no bridge link to answer over.
  if (provisioning_) return;

  waiting_ = true;
  phase_ = DialPhase::Waiting;

  // Copy the ask data into the active struct.
  const char* id = doc["id"] | "";
  strncpy(activeAsk_.id, id, sizeof(activeAsk_.id) - 1);
  activeAsk_.id[sizeof(activeAsk_.id) - 1] = '\0';

  const char* title = doc["title"] | "";
  strncpy(activeAsk_.title, title, sizeof(activeAsk_.title) - 1);
  activeAsk_.title[sizeof(activeAsk_.title) - 1] = '\0';

  const char* body = doc["body"] | "";
  strncpy(activeAsk_.body, body, sizeof(activeAsk_.body) - 1);
  activeAsk_.body[sizeof(activeAsk_.body) - 1] = '\0';

  activeAsk_.expiresAt = doc["expiresAt"] | (millis() + DSH_ASK_TIMEOUT_MS);
  activeAsk_.optionCount = 0;

  JsonArrayConst options = doc["options"].as<JsonArrayConst>();
  for (JsonObjectConst opt : options) {
    if (activeAsk_.optionCount >= 3) break;
    strncpy(activeAsk_.options[activeAsk_.optionCount].id, opt["id"] | "", 14);
    strncpy(activeAsk_.options[activeAsk_.optionCount].label, opt["label"] | "", 10);
    const char* style = opt["style"] | "";
    activeAsk_.options[activeAsk_.optionCount].primary = (strcmp(style, "primary") == 0);
    activeAsk_.optionCount++;
  }

  // Update the overlay UI.
  lv_label_set_text(askTitle_, title);
  lv_label_set_text(askBody_, body);

  // Show/hide buttons based on option count.
  if (activeAsk_.optionCount >= 1) {
    lv_obj_clear_flag(askBtnAllow_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* lbl = lv_obj_get_child(askBtnAllow_, 0);
    if (lbl) lv_label_set_text(lbl, activeAsk_.options[0].label);
    lv_obj_set_style_bg_color(askBtnAllow_,
        activeAsk_.options[0].primary ? kGreen : kRed, 0);
  }
  if (activeAsk_.optionCount >= 2) {
    lv_obj_clear_flag(askBtnDeny_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* lbl = lv_obj_get_child(askBtnDeny_, 0);
    if (lbl) lv_label_set_text(lbl, activeAsk_.options[1].label);
    lv_obj_set_style_bg_color(askBtnDeny_,
        activeAsk_.options[1].primary ? kGreen : kRed, 0);
  }
  if (activeAsk_.optionCount < 2) {
    lv_obj_add_flag(askBtnDeny_, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_clear_flag(askBg_, LV_OBJ_FLAG_HIDDEN);
}

void DialUi::dismissAsk() {
  waiting_ = false;
  lv_obj_add_flag(askBg_, LV_OBJ_FLAG_HIDDEN);
  if (pendingAnswer_) {
    free(pendingAnswer_);
    pendingAnswer_ = nullptr;
  }
}

// ── touch dispatch ───────────────────────────────────────────────────────

/**
 * Record the chosen option so the main loop can ship it.
 *
 * The UI does not send: it owns the screen, and the network belongs to the
 * caller. Keeping the send out of the callback also keeps LVGL's event context
 * free of blocking socket writes.
 */
void DialUi::recordAnswer(const char* optionId) {
  if (pendingAnswer_ != nullptr) free(pendingAnswer_);
  pendingAnswer_ = strdup(optionId);
}

namespace {
/**
 * LVGL click handler for the two ask buttons.
 *
 * The option index rides in the button's user data, so one handler serves both
 * and the mapping stays next to the layout that created them.
 */
void askButtonClicked(lv_event_t* event) {
  const auto index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
  dialUi.onAskButton(static_cast<uint8_t>(index));
}
}  // namespace

/** Resolve a button index to its option id and record it. */
void DialUi::onAskButton(uint8_t index) {
  if (!waiting_ || index >= activeAsk_.optionCount) return;
  recordAnswer(activeAsk_.options[index].id);
}

/**
 * A long press on the dial background asks the main loop to reopen setup.
 *
 * The main loop owns the mode machine, so this only raises a flag; the loop
 * notices it on the next pass, drops the bridge link, and starts the portal.
 * Nothing here blocks, and nothing here touches the link the loop is using.
 */
void DialUi::onBackgroundLongPress() {
  if (provisioning_ || waiting_) return;  // already asking for attention
  reprovisionRequested_ = true;
}

// ── provisioning screen ──────────────────────────────────────────────────

void DialUi::buildProvisioningScreen() {
  provBg_ = lv_obj_create(lv_scr_act());
  lv_obj_set_size(provBg_, 360, 360);
  lv_obj_set_pos(provBg_, 0, 0);
  lv_obj_set_style_radius(provBg_, 180, 0);
  lv_obj_set_style_border_width(provBg_, 0, 0);
  lv_obj_set_style_bg_color(provBg_, kBlue, 0);
  lv_obj_set_style_bg_opa(provBg_, 255, 0);
  lv_obj_clear_flag(provBg_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(provBg_, LV_OBJ_FLAG_HIDDEN);

  // Reason for setup mode, at the top of the circle.
  provTitle_ = lv_label_create(provBg_);
  lv_obj_align(provTitle_, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_set_style_text_font(provTitle_, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(provTitle_, kWhite, 0);
  lv_obj_set_style_text_align(provTitle_, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(provTitle_, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(provTitle_, 260);
  lv_label_set_text(provTitle_, "");

  // QR code, centred. A phone camera reads it and joins the AP with no typing.
  provQr_ = lv_qrcode_create(provBg_, 132, kDarkBg, kWhite);
  lv_obj_align(provQr_, LV_ALIGN_CENTER, 0, -8);

  // AP credentials, printed for the case where the camera cannot be used.
  provCreds_ = lv_label_create(provBg_);
  lv_obj_align(provCreds_, LV_ALIGN_BOTTOM_MID, 0, -56);
  lv_obj_set_style_text_font(provCreds_, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(provCreds_, kWhite, 0);
  lv_obj_set_style_text_align(provCreds_, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(provCreds_, "");

  // Portal URL to open once joined.
  provUrl_ = lv_label_create(provBg_);
  lv_obj_align(provUrl_, LV_ALIGN_BOTTOM_MID, 0, -32);
  lv_obj_set_style_text_font(provUrl_, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(provUrl_, kYellow, 0);
  lv_label_set_text(provUrl_, "");
}

void DialUi::showProvisioning(const char* apSsid, const char* apPassword,
                              const char* portalUrl, const char* reason) {
  if (provBg_ == nullptr) return;
  provisioning_ = true;

  lv_label_set_text(provTitle_, reason != nullptr ? reason : "设备配置");

  // A WIFI: URI makes the phone offer to join the network directly. WPA is
  // hard-coded because deriveCredentials always sets a password.
  char wifiUri[128];
  snprintf(wifiUri, sizeof(wifiUri), "WIFI:T:WPA;S:%s;P:%s;;",
           apSsid != nullptr ? apSsid : "", apPassword != nullptr ? apPassword : "");
  lv_qrcode_update(provQr_, wifiUri, strlen(wifiUri));

  char creds[96];
  snprintf(creds, sizeof(creds), "%s\n%s", apSsid != nullptr ? apSsid : "",
           apPassword != nullptr ? apPassword : "");
  lv_label_set_text(provCreds_, creds);

  lv_label_set_text(provUrl_, portalUrl != nullptr ? portalUrl : "");

  // The provisioning screen outranks both the dial and any stale ask.
  lv_obj_add_flag(askBg_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(provBg_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(provBg_);
}

void DialUi::hideProvisioning() {
  if (provBg_ == nullptr) return;
  provisioning_ = false;
  lv_obj_add_flag(provBg_, LV_OBJ_FLAG_HIDDEN);
}

// ── connection status ────────────────────────────────────────────────────

/**
 * Report link progress on the detail line while no state frame has arrived.
 *
 * Kept separate from setState so a connection message can never be mistaken for
 * session data: the phase stays Offline until the bridge actually speaks.
 */
void DialUi::setConnecting(const char* message) {
  if (provisioning_ || waiting_) return;
  phase_ = DialPhase::Offline;
  setPhaseLabel(DialPhase::Offline);
  lv_obj_set_style_bg_color(statusBg_, colorForPhase(DialPhase::Offline, false), 0);
  lv_label_set_text(titleLabel_, "DSH");
  lv_label_set_text(detailLabel_, message != nullptr ? message : "");
  updateArc(0);
}

// ── backlight ────────────────────────────────────────────────────────────

void DialUi::setBacklight(uint8_t level) {
  backlight_ = level;
  // Backlight control is handled outside this class via the LEDC API.
}

// ── main loop call ───────────────────────────────────────────────────────

void DialUi::loop() {
  lv_timer_handler();

  // Auto-dismiss the `done` phase after 4 s.
  if (doneShowMs_ > 0 && millis() - doneShowMs_ > 4000) {
    doneShowMs_ = 0;
    if (phase_ == DialPhase::Done) {
      phase_ = DialPhase::Idle;
      setPhaseLabel(DialPhase::Idle);
      lv_obj_set_style_bg_color(statusBg_, colorForPhase(DialPhase::Idle, stale_), 0);
    }
  }

  // Auto-dismiss the ask overlay on expiry.
  if (waiting_ && millis() > activeAsk_.expiresAt) {
    dismissAsk();
  }
}