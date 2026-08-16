// Copyright (c) 2026 DSH ESP32 dial project.
// SPDX-License-Identifier: MIT
//
// Serial console for inspecting and changing settings over USB.
//
// The web portal is the primary path, but while the board is on a cable the
// console is faster and it works even when WiFi is the thing that is broken —
// which is exactly when configuration needs changing. It accepts the same edits
// as the form.

#pragma once

#include <Arduino.h>

class Console {
 public:
  void begin();

  /** Read and dispatch any complete line. Call from the main loop. */
  void loop();

 private:
  void execute(const String& line);
  void printHelp() const;
  void printStatus() const;

  String buffer_;
};

extern Console console;