// Seizure detector: a rule-based classifier plus an alert state machine.
//
// Every second it gets one Features window. A window is a "candidate" when it
// shows strong, rhythmic 2-8 Hz motion in BOTH the accelerometer and the gyro.
//
//   IDLE      -> PRE_ALERT  when 8 of the last 10 windows were candidates
//   PRE_ALERT -> ALERT      after 10 s unless cancelled (no answer = alert)
//   ALERT     -> COOLDOWN   after 15 s without rhythmic motion ("ended")
//   COOLDOWN  -> IDLE       after 60 s, so one seizure gives one alert
#pragma once
#include <stdint.h>

#include "features.h"

struct DetectorConfig {
  // A window is a candidate when all of these hold...
  float acc_rms_min = 0.30f;   // g
  float gyr_rms_min = 100.0f;  // deg/s
  float band_lo_hz = 2.0f;     // dominant frequency, both sensors
  float band_hi_hz = 8.0f;
  float rhythm_min = 0.30f;    // both sensors
  // ...and the newest second alone reaches this share of the RMS minimums.
  // Without it, one 5 s burst would fill ~8 overlapping 4 s windows.
  float newest_second_scale = 0.7f;

  // Looser test for "still going" once an alert is out (hysteresis).
  float ongoing_scale = 0.5f;  // share of the RMS minimums
  float ongoing_rhythm_min = 0.20f;

  // Timing. One window per second, so windows = seconds.
  uint8_t confirm_windows = 10;  // look at the last 10 windows...
  uint8_t confirm_min = 8;       // ...8 must be candidates
  uint16_t cancel_window_s = 10;
  uint16_t end_quiet_s = 15;
  uint16_t cooldown_s = 60;
  uint16_t update_every_s = 30;
};

// A double-tap on the watch only counts during the pre-alert and while the
// wrist is steady (clonic jerks can look like taps). A phone command always counts.
enum class CancelSource : uint8_t { Tap, Phone };

enum class DetState : uint8_t { Idle, PreAlert, Alert, Cooldown };

enum class EventType : uint8_t {
  None,
  PreAlert,        // pattern confirmed, cancel window open
  Alert,           // suspected seizure
  Update,          // alert still going on
  Ended,           // rhythmic motion stopped
  Cancelled,       // wearer cancelled
  CancelRejected,  // tap ignored: wrist still moving, or alert already sent
};

struct DetEvent {
  EventType type = EventType::None;
  uint8_t confidence = 0;   // 0..100
  uint16_t duration_s = 0;  // since the rhythmic motion began
  float dom_hz = 0;         // its frequency (gyro)
};

enum class Activity : uint8_t { Still, Light, Walking, Active, Rhythmic };

const char* stateName(DetState s);
const char* eventName(EventType t);
const char* activityName(Activity a);

class Detector {
 public:
  explicit Detector(const DetectorConfig& config = DetectorConfig());

  // Call once per Features window (every second). Returns at most one event.
  DetEvent update(const Features& f, uint32_t now_ms);
  DetEvent requestCancel(CancelSource source, uint32_t now_ms);

  bool isCandidate(const Features& f) const;
  bool isOngoing(const Features& f) const;
  Activity classify(const Features& f) const;  // rough label for the status line

  DetState state() const { return state_; }
  uint8_t candidateCount() const;  // among the last confirm_windows windows
  uint8_t confidence() const;      // 0..100

 private:
  static constexpr int kMaxWindows = 32;  // bits in candidates_

  DetEvent makeEvent(EventType type, uint32_t now_ms, uint8_t confidence) const;
  void enterCooldown(uint32_t now_ms);
  void clearHistory();

  DetectorConfig cfg_;
  DetState state_ = DetState::Idle;
  uint32_t candidates_ = 0;           // bit i = the window i seconds ago was a candidate
  float quality_[kMaxWindows] = {0};  // that window's rhythm, 0 if not a candidate
  bool ongoing_ = false;              // newest window passed isOngoing()
  bool steady_ = true;                // newest window was calm enough to trust a tap
  float rhythm_hz_ = 0;               // gyro frequency of the newest candidate
  uint32_t onset_ms_ = 0;             // when the rhythmic motion began
  uint32_t state_start_ms_ = 0;
  uint32_t last_update_ms_ = 0;
  uint16_t quiet_s_ = 0;              // seconds in a row without ongoing motion
  uint8_t alert_confidence_ = 0;      // frozen when the alert goes out
};
