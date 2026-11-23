#include "leds.h"

namespace led {
namespace {
constexpr LedStep kBootSteps[] = {{GREEN, 100}, {OFF, 100}, {GREEN, 100}, {OFF, 100}, {GREEN, 100}, {OFF, 300}};
constexpr LedStep kWaitingSteps[] = {{BLUE, 15}, {OFF, 4985}};
constexpr LedStep kConnectedSteps[] = {{GREEN, 15}, {OFF, 4985}};
constexpr LedStep kUnacknowledgedSteps[] = {{RED, 30}, {OFF, 2970}};
constexpr LedStep kPreAlertSteps[] = {{AMBER, 250}, {OFF, 250}};
constexpr LedStep kAlertSteps[] = {{RED, 100}, {OFF, 100}};
constexpr LedStep kCancelledSteps[] = {{GREEN, 1500}, {OFF, 200}};
constexpr LedStep kRejectedSteps[] = {{RED, 60}, {OFF, 60}, {RED, 60}, {OFF, 60}, {RED, 60}, {OFF, 300}};
constexpr LedStep kFaultSteps[] = {{PURPLE, 150}, {OFF, 150}, {PURPLE, 150}, {OFF, 1550}};
constexpr LedStep kTestSteps[] = {{RED, 100}, {OFF, 100}, {BLUE, 100}, {OFF, 100}, {RED, 100},
                                 {OFF, 100},  {BLUE, 100}, {OFF, 100}, {RED, 100}, {OFF, 300}};

SoftwareTimer timer;
const LedPattern* background = nullptr;
const LedPattern* playing = nullptr;
volatile uint8_t step_index = 0;

void show(uint8_t color) {
  // The XIAO's RGB LED is active-low.
  digitalWrite(LED_RED, (color & RED) ? LOW : HIGH);
  digitalWrite(LED_GREEN, (color & GREEN) ? LOW : HIGH);
  digitalWrite(LED_BLUE, (color & BLUE) ? LOW : HIGH);
}

// Shows the next step and sets the timer for when it ends.
void nextStep() {
  if (playing && step_index >= playing->count) {
    if (!playing->loop) playing = background;  // one-shot pattern finished
    step_index = 0;
  }
  if (!playing) {
    show(OFF);
    return;
  }
  const LedStep& step = playing->steps[step_index++];
  show(step.color);
  timer.setPeriod(step.ms);  // also restarts the one-shot timer
}

void onTimer(TimerHandle_t) { nextStep(); }
}  // namespace

// {steps, how many steps, loop forever?}
#define COUNT(steps) uint8_t(sizeof(steps) / sizeof(steps[0]))
const LedPattern kBoot = {kBootSteps, COUNT(kBootSteps), false};
const LedPattern kWaitingForPhone = {kWaitingSteps, COUNT(kWaitingSteps), true};
const LedPattern kPhoneConnected = {kConnectedSteps, COUNT(kConnectedSteps), true};
const LedPattern kAlertUnacknowledged = {kUnacknowledgedSteps, COUNT(kUnacknowledgedSteps), true};
const LedPattern kPreAlert = {kPreAlertSteps, COUNT(kPreAlertSteps), true};
const LedPattern kAlert = {kAlertSteps, COUNT(kAlertSteps), true};
const LedPattern kCancelled = {kCancelledSteps, COUNT(kCancelledSteps), false};
const LedPattern kRejected = {kRejectedSteps, COUNT(kRejectedSteps), false};
const LedPattern kFault = {kFaultSteps, COUNT(kFaultSteps), true};
const LedPattern kTest = {kTestSteps, COUNT(kTestSteps), false};

void begin() {
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  show(OFF);
  timer.begin(100, onTimer, nullptr, false);
}

void setBackground(const LedPattern& p) {
  if (background == &p) return;
  const bool showing_background = playing == background;
  background = &p;
  if (showing_background) play(p);
}

void play(const LedPattern& p) {
  timer.stop();
  playing = &p;
  step_index = 0;
  nextStep();
}
}  // namespace led
