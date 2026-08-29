// Copyright (c) 2026 DSH ESP32 dial project.
// SPDX-License-Identifier: MIT
//
// Transport-neutral state shared by the Codex renderer and the app shell.

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

/** States the host bridge can expose to a renderer. */
enum class PetPhase : uint8_t {
  Idle,
  Working,
  Thinking,
  Streaming,
  Waiting,
  Done,
  Error,
  Offline,
};

/** One compact activity row for a 360px circular display. */
struct PetActivity {
  char text[44] = {};
  bool running = false;
};

constexpr uint8_t kPetActivityLines = 3;

/**
 * The renderer-facing projection of a bridge state frame.
 *
 * Keeping this separate from ArduinoJson makes navigation and animation code
 * independent from the wire format. The JSON document is still passed to the
 * Codex view for structured counters until that view is fully migrated.
 */
struct PetState {
  PetPhase phase = PetPhase::Offline;
  uint8_t contextPercent = 0;
  uint8_t batteryPercent = 0;
  bool charging = false;
  bool stale = false;
  char title[48] = {};
  char detail[160] = {};
  char clock[8] = {};
  PetActivity activities[kPetActivityLines] = {};
};

/** Parse the common state projection without retaining the JSON document. */
bool petStateFromJson(const JsonDocument& doc, PetState& out);
