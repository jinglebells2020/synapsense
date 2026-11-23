// Seizure detector: a staged rule-based classifier plus an alert state machine.
//
//   candidate window  : strong, rhythmic 2-8 Hz motion in BOTH accel and gyro
//   confirmation      : >= 8 candidate windows out of the last 10 (~10 s)
//   PRE_ALERT         : wearer can cancel for 10 s (double-tap, only when still)
//   ALERT             : sent if not cancelled; lasts until 15 s without rhythm
//   COOLDOWN          : 60 s with no new alerts (one alert per event)
#pragma once
#include <stdint.h>

#include "features.h"

struct DetectorConfig {
  // Candidate (trigger) thresholds.
  float acc_rms_min = 0.30f;   // g
  float gyr_rms_min = 100.0f;  // deg/s
  float band_lo_hz = 2.0f;
  float band_hi_hz = 8.0f;
  float rhythm_min = 0.30f;
  // The newest second must also be moving (the 4 s window alone would let a
  // 5 s burst count as ~8 s of shaking).
  float recent_scale = 0.7f;
  // Looser "still seizing" thresholds (hysteresis), as a fraction of the above.
  float hold_scale = 0.5f;
  float hold_rhythm_min = 0.20f;
  // Timing (one window per second).
  uint8_t confirm_window = 10;  // look at the last N windows
  uint8_t confirm_min = 8;      // of which this many must be candidates
  uint16_t cancel_window_s = 10;
  uint16_t end_quiet_s = 15;
  uint16_t cooldown_s = 60;
  uint16_t update_every_s = 30;
};

// Where a cancel came from. A double-tap on the watch is only trusted during
// the pre-alert and only while the wrist is steady; a phone command is always
// trusted (someone made a deliberate choice on the phone).
enum class CancelSource : uint8_t { Tap, Phone };

enum class DetState : uint8_t { Idle, PreAlert, Alert, Cooldown };

enum class EventType : uint8_t {
  None,
  PreAlert,        // pattern confirmed, cancel window open
  Alert,           // suspected seizure, notify caregivers
  Update,          // alert still ongoing
  Ended,           // rhythmic motion stopped
  Cancelled,       // wearer cancelled (pre-alert or after alert)
  CancelRejected,  // tap cancel ignored: wrist still moving, or alert already out
};

struct DetEvent {
  EventType type = EventType::None;
  uint8_t confidence = 0;  // 0..100
  uint16_t duration_s = 0; // seconds since rhythmic motion began
  float dom_hz = 0;
};

enum class Activity : uint8_t { Still, Light, Walking, Active, Rhythmic };

const char* stateName(DetState s);
const char* eventName(EventType t);
const char* activityName(Activity a);

class Detector {
 public:
  explicit Detector(const DetectorConfig& cfg = DetectorConfig());

  // Call once per feature window (every second). Returns at most one event.
  DetEvent update(const Features& f, uint32_t now_ms);
  // Wearer asked to cancel (double-tap or phone command).
  DetEvent requestCancel(CancelSource src, uint32_t now_ms);
  void reset();

  bool isCandidate(const Features& f) const;
  bool isHolding(const Features& f) const;
  bool isSteady(const Features& f) const;
  Activity classify(const Features& f) const;

  DetState state() const { return state_; }
  uint8_t candidateCount() const;
  uint8_t confidence() const;
  const DetectorConfig& config() const { return cfg_; }

 private:
  static constexpr int kMaxWindow = 32;
  DetEvent make(EventType t, uint32_t now_ms, uint8_t confidence) const;
  void toCooldown(uint32_t now_ms);

  DetectorConfig cfg_;
  DetState state_ = DetState::Idle;
  uint32_t history_ = 0;          // bit i = the window i seconds ago was a candidate
  float quality_[kMaxWindow] = {0};  // rhythm quality, same indexing as history_
  bool last_holding_ = false;
  bool last_steady_ = true;
  float last_dom_hz_ = 0;
  uint32_t onset_ms_ = 0;         // when the rhythmic run began
  uint32_t state_since_ms_ = 0;
  uint32_t last_update_ms_ = 0;
  uint16_t quiet_s_ = 0;
  uint8_t alert_confidence_ = 0;
};
