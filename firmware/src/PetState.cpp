// Copyright (c) 2026 DSH ESP32 dial project.
// SPDX-License-Identifier: MIT

#include "PetState.h"

#include <string.h>

static PetPhase parsePhase(const char* value) {
  if (strcmp(value, "working") == 0) return PetPhase::Working;
  if (strcmp(value, "thinking") == 0) return PetPhase::Thinking;
  if (strcmp(value, "streaming") == 0) return PetPhase::Streaming;
  if (strcmp(value, "waiting") == 0) return PetPhase::Waiting;
  if (strcmp(value, "done") == 0) return PetPhase::Done;
  if (strcmp(value, "error") == 0) return PetPhase::Error;
  if (strcmp(value, "idle") == 0) return PetPhase::Idle;
  if (strcmp(value, "offline") == 0) return PetPhase::Offline;
  return PetPhase::Idle;
}

bool petStateFromJson(const JsonDocument& doc, PetState& out) {
  out = PetState{};
  out.phase = parsePhase(doc["phase"] | "idle");
  out.contextPercent = static_cast<uint8_t>(doc["ctx"] | 0);
  out.batteryPercent = static_cast<uint8_t>(doc["battery"] | 0);
  out.charging = doc["charging"] | false;

  const char* title = doc["title"] | "";
  const char* detail = doc["detail"] | "";
  const char* clock = doc["clock"] | "";
  strncpy(out.title, title, sizeof(out.title) - 1);
  strncpy(out.detail, detail, sizeof(out.detail) - 1);
  strncpy(out.clock, clock, sizeof(out.clock) - 1);

  uint8_t row = 0;
  JsonArrayConst acts = doc["acts"].as<JsonArrayConst>();
  for (JsonObjectConst act : acts) {
    if (row >= kPetActivityLines) break;
    const char* text = act["t"] | "";
    strncpy(out.activities[row].text, text,
            sizeof(out.activities[row].text) - 1);
    out.activities[row].running = act["r"] | false;
    ++row;
  }
  return true;
}
