// Copyright (c) 2026 DSH ESP32 dial project.
// SPDX-License-Identifier: MIT
//
// LVGL round-dial UI for the DSH status dial.
//
// Three screens share the 360×360 circular display:
//
//  * the dial — an outer arc for context pressure, a centred phase glyph, and
//    title/detail lines. A glance must tell the phases apart without reading.
//  * the ask overlay — a yellow card with the question and its buttons, shown
//    when DSH is blocked on a decision. This is the device's reason to exist.
//  * the provisioning screen — AP credentials and a QR code, shown when the
//    dial has no usable WiFi/bridge configuration and has opened its portal.

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>

/** The five dial states, matching the bridge's `phase` field. */
enum class DialPhase : uint8_t {
  Idle,
  Working,
  Thinking,    // waiting for the first LLM token
  Streaming,   // LLM tokens are arriving (输出中)
  Waiting,
  Done,
  Error,
  Offline   // bridge disconnected — not a bridge phase, injected by firmware
};

/** One ask button description. */
struct AskOption {
  char id[16];
  char label[12];
  /** Indicates which button is the primary (green) vs danger (red). */
  bool primary;
};

/** An ask that the device must present. */
struct Ask {
  char id[16];
  char title[48];
  char body[160];
  AskOption options[3];
  uint8_t optionCount;  // 1-3
  unsigned long expiresAt;  // ms timestamp
};

/**
 * One line of the working-phase activity list.
 *
 * The dial shows what DSH is *doing*, not just that it is busy: a tool call
 * naming a real file or command is the difference between a progress spinner and
 * a status display. Three lines is what the circle fits without shrinking the
 * text past reading size at arm's length.
 */
struct ActivityLine {
  char text[44];
  bool running;  // true = happening now (highlighted), false = finished
};

/** How many activity lines the working phase keeps on screen. */
constexpr uint8_t kActivityLines = 3;

class DialUi {
 public:
  DialUi();

  /** Call once in setup() after LVGL is initialised. */
  void begin();

  /** Call in the main loop; dispatches LVGL timer tasks. */
  void loop();

  // ── state frame ──────────────────────────────────────────────────────
  /** Update the dial from a bridge `state` frame. */
  void setState(const JsonDocument& doc);

  // ── ask frame ────────────────────────────────────────────────────────
  /** Show an ask overlay. */
  void showAsk(const JsonDocument& doc);

  /** Dismiss the ask overlay (user answered, or it expired). */
  void dismissAsk();

  /** True while the ask overlay is shown. */
  bool isWaiting() const { return waiting_; }

  /** True while the `done` phase is showing (transient). */
  bool isDoneShowing() const { return doneShowMs_ > 0; }

  /** True while the `offline` state is showing. */
  bool isOffline() const { return phase_ == DialPhase::Offline; }

  /** The current phase for the idle-dimming decision. */
  DialPhase phase() const { return phase_; }

  /** True when the last state frame is older than the freshness threshold. */
  void setStale(bool stale) { stale_ = stale; }

  /** The id of the ask currently shown — this is what an answer must carry. */
  const char* askId() const { return activeAsk_.id; }

  /** Return the text of the last ask for debugging. */
  const char* askTitle() const { return activeAsk_.title; }

  /** Show a bridge-connection status line while offline (no state frames yet). */
  void setConnecting(const char* message);

  /**
   * Show the provisioning screen: AP name, password, portal URL, and a QR code
   * that joins the AP directly.
   *
   * A phone camera pointed at a WIFI: QR joins the network without typing, and
   * the printed credentials cover the case where the camera cannot.
   */
  void showProvisioning(const char* apSsid, const char* apPassword,
                        const char* portalUrl, const char* reason);

  /** Leave the provisioning screen and return to the dial. */
  void hideProvisioning();

  /** True while the provisioning screen is shown. */
  bool isProvisioning() const { return provisioning_; }

  /** True when the user long-pressed the dial background to re-open setup. */
  bool hasReprovisionRequest() const { return reprovisionRequested_; }

  /** Acknowledge and clear the request. */
  void clearReprovisionRequest() { reprovisionRequested_ = false; }

  /**
   * True when a button callback has produced an answer awaiting delivery.
   *
   * Touch itself is handled by LVGL: the port's read callback owns the CST816
   * and clears its state, so the dial registers ordinary LVGL button events
   * rather than polling coordinates. The callback records the chosen option id
   * here, and the main loop ships it.
   */
  bool hasAnswer() const { return pendingAnswer_ != nullptr; }

  /** Take ownership of the pending answer string (caller frees it). */
  char* takeAnswer() { char* r = pendingAnswer_; pendingAnswer_ = nullptr; return r; }

  /** Record an answer; called by the LVGL button event handler. */
  void recordAnswer(const char* optionId);

  // ── brightness ───────────────────────────────────────────────────────
  void setBacklight(uint8_t level);

 private:
  // LVGL objects
  lv_obj_t* arc_ = nullptr;          // context pressure ring
  lv_obj_t* phaseLabel_ = nullptr;   // phase icon (small, above the title)
  lv_obj_t* whaleIcon_ = nullptr;   // DeepSeek whale image, top-right
  lv_obj_t* titleLabel_ = nullptr;   // session title
  lv_obj_t* detailLabel_ = nullptr;  // current action
  lv_obj_t* statusBg_ = nullptr;     // phase-colour background circle

  // Idle face: a desk device is a clock whenever it is not working.
  lv_obj_t* clockLabel_ = nullptr;   // HH:MM, idle only
  lv_obj_t* clockSubLabel_ = nullptr;

  // Working face: the tool-call activity list.
  lv_obj_t* activityRows_[kActivityLines] = {nullptr};

  // Always-on footer: link, signal, battery — device health at a glance.
  // Icon and text are separate labels because the icon needs Montserrat (which
  // carries LVGL built-in symbols at 0xF1xx) while the text needs SIMSUN (which
  // has Chinese glyphs but no LVGL symbols at all — see the font torture chart
  // in DialUi.cpp for the full list of characters that render as boxes).
  lv_obj_t* footerIcon_ = nullptr;   // LVGL symbol (Montserrat)
  lv_obj_t* footerText_ = nullptr;   // "已连接" / "离线" (SIMSUN)
  lv_obj_t* footerRight_ = nullptr;  // battery — SIMSUN, ASCII only
  lv_obj_t* ctxLabel_ = nullptr;     // "上下文 xx%" — arc-adjacent, idle only

  // Idle face: three-column layout — 4 stat rows left, clock centre, 4 right.
  // Value labels (white) and name labels (gray) are separate to avoid recolor
  // parsing issues — each label has exactly one colour.
  lv_obj_t* idleColLeft_[4] = {nullptr};   // value text, right-aligned
  lv_obj_t* idleColRight_[4] = {nullptr};  // value text, left-aligned
  lv_obj_t* idleNameLeft_[4] = {nullptr};  // name text, right-aligned, gray
  lv_obj_t* idleNameRight_[4] = {nullptr}; // name text, left-aligned, gray

  // Ask overlay
  lv_obj_t* askBg_ = nullptr;        // yellow overlay
  lv_obj_t* askTitle_ = nullptr;     // ask question
  lv_obj_t* askBody_ = nullptr;      // ask detail
  lv_obj_t* askBtnAllow_ = nullptr;
  lv_obj_t* askBtnDeny_ = nullptr;
  lv_obj_t* askBtnThird_ = nullptr;  // third option, when the ask has one

  // Provisioning screen
  lv_obj_t* provBg_ = nullptr;
  lv_obj_t* provTitle_ = nullptr;
  lv_obj_t* provQr_ = nullptr;
  lv_obj_t* provCreds_ = nullptr;
  lv_obj_t* provUrl_ = nullptr;
  bool provisioning_ = false;
  bool reprovisionRequested_ = false;

  // State
  DialPhase phase_ = DialPhase::Offline;
  unsigned long doneShowMs_ = 0;     // remaining ms of the done flash
  bool stale_ = false;
  bool waiting_ = false;
  Ask activeAsk_;
  char* pendingAnswer_ = nullptr;

  // Phase→background colour lookup
  static lv_color_t colorForPhase(DialPhase phase, bool stale);

  // Background colour transition animation state
  lv_color_t prevBgColor_ = LV_COLOR_MAKE(0x0D, 0x11, 0x1D);
  lv_anim_t bgAnim_;
  static void bgAnimCb(void* obj, int32_t v);

  // Whale breathing animation (thinking / streaming)
  lv_anim_t whaleAnim_;
  static void whaleAnimCb(void* obj, int32_t v);
  void startWhaleAnim();
  void stopWhaleAnim();

  // Layout helpers
  void buildMainScreen();
  void buildAskOverlay();
  void buildProvisioningScreen();
  void updateArc(uint8_t ctxPercent);
  void setPhaseLabel(DialPhase phase);
  void setActivity(const JsonDocument& doc);   // working-phase tool-call list
  void setStats(const JsonDocument& doc);      // idle-phase counters ticker
  void setFooter(const JsonDocument& doc);     // link / signal / battery
  void onAskButton(uint8_t index);
  void onBackgroundLongPress();

  /**
   * LVGL click trampoline for the ask buttons.
   *
   * A static member rather than a free function so it can reach the private
   * handlers above; the chosen option index rides in the event's user data, so
   * one callback serves every button.
   */
  static void askButtonClicked(lv_event_t* event);

  /** LVGL long-press trampoline for the dial background. */
  static void backgroundLongPressed(lv_event_t* event);

  // LVGL style
  static lv_style_t mainStyle_;
  static bool stylesInitialised_;

  // Backlight PWM
  uint8_t backlight_ = 90;
};

// Global instance — the firmware does one thing, so one dial is enough.
extern DialUi dialUi;