#include "leds.h"

namespace led {
namespace {
constexpr LedStep kBootSteps[] = {{GREEN, 100}, {OFF, 100}, {GREEN, 100}, {OFF, 100}, {GREEN, 100}, {OFF, 300}};
constexpr LedStep kHbAdvSteps[] = {{BLUE, 15}, {OFF, 4985}};
constexpr LedStep kHbConnSteps[] = {{GREEN, 15}, {OFF, 4985}};
constexpr LedStep kHbEventSteps[] = {{RED, 30}, {OFF, 2970}};
constexpr LedStep kPreSteps[] = {{AMBER, 250}, {OFF, 250}};
constexpr LedStep kAlertSteps[] = {{RED, 100}, {OFF, 100}};
constexpr LedStep kCancelSteps[] = {{GREEN, 1500}, {OFF, 200}};
constexpr LedStep kRejectSteps[] = {{RED, 60}, {OFF, 60}, {RED, 60}, {OFF, 60}, {RED, 60}, {OFF, 300}};
constexpr LedStep kFaultSteps[] = {{PURPLE, 150}, {OFF, 150}, {PURPLE, 150}, {OFF, 1550}};
constexpr LedStep kTestSteps[] = {{RED, 100}, {OFF, 100}, {BLUE, 100}, {OFF, 100}, {RED, 100}, {OFF, 100},
                                 {BLUE, 100}, {OFF, 100}, {RED, 100}, {OFF, 300}};

SoftwareTimer timer;
const LedPattern* base = nullptr;
const LedPattern* current = nullptr;
volatile uint8_t index = 0;

void show(uint8_t c) {
  // The XIAO RGB LED is active-low.
  digitalWrite(LED_RED, (c & RED) ? LOW : HIGH);
  digitalWrite(LED_GREEN, (c & GREEN) ? LOW : HIGH);
  digitalWrite(LED_BLUE, (c & BLUE) ? LOW : HIGH);
}

void step() {
  const LedPattern* p = current;
  if (!p) {
    show(OFF);
    return;
  }
  if (index >= p->count) {
    if (p->loop) {
      index = 0;
    } else {
      current = base;
      index = 0;
      p = current;
      if (!p) {
        show(OFF);
        return;
      }
    }
  }
  const LedStep& s = p->steps[index++];
  show(s.color);
  timer.setPeriod(s.ms);  // also (re)starts the one-shot timer
}

void onTimer(TimerHandle_t) { step(); }
}  // namespace

#define PATTERN(name, steps, loop) \
  const LedPattern name = {steps, sizeof(steps) / sizeof(steps[0]), loop}
PATTERN(kBoot, kBootSteps, false);
PATTERN(kHeartbeatAdv, kHbAdvSteps, true);
PATTERN(kHeartbeatConn, kHbConnSteps, true);
PATTERN(kHeartbeatEvent, kHbEventSteps, true);
PATTERN(kPreAlert, kPreSteps, true);
PATTERN(kAlert, kAlertSteps, true);
PATTERN(kCancelled, kCancelSteps, false);
PATTERN(kRejected, kRejectSteps, false);
PATTERN(kFault, kFaultSteps, true);
PATTERN(kTest, kTestSteps, false);

void begin() {
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  show(OFF);
  timer.begin(100, onTimer, nullptr, false);
}

void setBase(const LedPattern& p) {
  if (base == &p) return;
  const bool was_on_base = current == base;
  base = &p;
  if (was_on_base) play(p);
}

void play(const LedPattern& p) {
  timer.stop();
  current = &p;
  index = 0;
  step();
}
}  // namespace led
