// On-board RGB LED patterns, driven by a FreeRTOS software timer.
// The timer only wakes the CPU at pattern edges (a heartbeat costs ~0.4
// wake-ups per second), so idle power stays low.
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
extern const LedPattern kBoot, kHeartbeatAdv, kHeartbeatConn, kHeartbeatEvent, kPreAlert,
    kAlert, kCancelled, kRejected, kFault, kTest;

void begin();
// Looping "background" pattern that one-shot patterns return to.
void setBase(const LedPattern& p);
// Plays a pattern now. One-shot patterns fall back to the base pattern.
void play(const LedPattern& p);
}  // namespace led
