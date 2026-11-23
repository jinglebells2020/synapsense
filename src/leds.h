// On-board RGB LED patterns. A FreeRTOS software timer fires only when the
// pattern changes (a heartbeat blip costs ~0.4 wake-ups per second).
#pragma once
#include <Arduino.h>

enum Color : uint8_t { OFF = 0, RED = 1, GREEN = 2, BLUE = 4, AMBER = RED | GREEN, PURPLE = RED | BLUE };

struct LedStep {
  uint8_t color;
  uint16_t ms;
};

struct LedPattern {
  const LedStep* steps;
  uint8_t count;
  bool loop;
};

namespace led {
extern const LedPattern kBoot, kWaitingForPhone, kPhoneConnected, kAlertUnacknowledged, kPreAlert,
    kAlert, kCancelled, kRejected, kFault, kTest;

void begin();
// The looping pattern shown when nothing else is playing.
void setBackground(const LedPattern& p);
// Plays a pattern now. A one-shot pattern returns to the background when done.
void play(const LedPattern& p);
}  // namespace led
