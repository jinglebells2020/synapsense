#include "detector.h"

namespace {
bool inBand(float hz, float lo, float hi) { return hz >= lo && hz <= hi; }
float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
bool elapsed(uint32_t since_ms, uint32_t now_ms, uint16_t seconds) {
  return now_ms - since_ms >= uint32_t(seconds) * 1000;
}
}  // namespace

const char* stateName(DetState s) {
  switch (s) {
    case DetState::Idle: return "IDLE";
    case DetState::PreAlert: return "PRE_ALERT";
    case DetState::Alert: return "ALERT";
    case DetState::Cooldown: return "COOLDOWN";
  }
  return "?";
}

const char* eventName(EventType t) {
  switch (t) {
    case EventType::None: return "NONE";
    case EventType::PreAlert: return "PRE_ALERT";
    case EventType::Alert: return "ALERT";
    case EventType::Update: return "UPDATE";
    case EventType::Ended: return "ENDED";
    case EventType::Cancelled: return "CANCELLED";
    case EventType::CancelRejected: return "CANCEL_REJECTED";
  }
  return "?";
}

const char* activityName(Activity a) {
  switch (a) {
    case Activity::Still: return "still";
    case Activity::Light: return "light";
    case Activity::Walking: return "walking";
    case Activity::Active: return "active";
    case Activity::Rhythmic: return "rhythmic";
  }
  return "?";
}

Detector::Detector(const DetectorConfig& config) : cfg_(config) {
  if (cfg_.confirm_windows > kMaxWindows) cfg_.confirm_windows = kMaxWindows;
  if (cfg_.confirm_min > cfg_.confirm_windows) cfg_.confirm_min = cfg_.confirm_windows;
}

bool Detector::isCandidate(const Features& f) const {
  const bool strong = f.acc_rms >= cfg_.acc_rms_min && f.gyr_rms >= cfg_.gyr_rms_min;
  const bool moving_now = f.acc_rms_1s >= cfg_.acc_rms_min * cfg_.newest_second_scale &&
                          f.gyr_rms_1s >= cfg_.gyr_rms_min * cfg_.newest_second_scale;
  const bool in_band = inBand(f.acc_dom_hz, cfg_.band_lo_hz, cfg_.band_hi_hz) &&
                       inBand(f.gyr_dom_hz, cfg_.band_lo_hz, cfg_.band_hi_hz);
  const bool rhythmic = f.acc_rhythm >= cfg_.rhythm_min && f.gyr_rhythm >= cfg_.rhythm_min;
  return strong && moving_now && in_band && rhythmic;
}

bool Detector::isOngoing(const Features& f) const {
  return f.acc_rms >= cfg_.acc_rms_min * cfg_.ongoing_scale &&
         f.gyr_rms >= cfg_.gyr_rms_min * cfg_.ongoing_scale &&
         inBand(f.gyr_dom_hz, cfg_.band_lo_hz, cfg_.band_hi_hz) &&
         f.gyr_rhythm >= cfg_.ongoing_rhythm_min;
}

Activity Detector::classify(const Features& f) const {
  if (isCandidate(f)) return Activity::Rhythmic;
  if (f.acc_rms < 0.02f && f.gyr_rms < 5.0f) return Activity::Still;
  // Arm swing while walking: a regular rotation slower than the seizure band.
  const bool arm_swing = f.gyr_dom_hz >= 0.6f && f.gyr_dom_hz < cfg_.band_lo_hz && f.gyr_rhythm >= 0.3f;
  if (arm_swing && f.acc_rms >= 0.05f) return Activity::Walking;
  if (f.acc_rms >= 0.15f || f.gyr_rms >= 60.0f) return Activity::Active;
  return Activity::Light;
}

uint8_t Detector::candidateCount() const {
  uint8_t count = 0;
  for (int age = 0; age < cfg_.confirm_windows; age++)
    if (was_candidate_[age]) count++;
  return count;
}

// 60% persistence (share of recent windows that were candidates)
// + 40% quality (how clean the rhythm was in those windows).
uint8_t Detector::confidence() const {
  float quality_sum = 0;
  int count = 0;
  for (int age = 0; age < cfg_.confirm_windows; age++) {
    if (was_candidate_[age]) {
      quality_sum += quality_[age];
      count++;
    }
  }
  const float persistence = float(count) / cfg_.confirm_windows;
  const float quality = count ? clamp01((quality_sum / count) / 0.6f) : 0;
  return uint8_t(100.0f * (0.6f * persistence + 0.4f * quality) + 0.5f);
}

DetEvent Detector::makeEvent(EventType type, uint32_t now_ms, uint8_t confidence) const {
  DetEvent e;
  e.type = type;
  e.confidence = confidence;
  if (onset_ms_ && now_ms > onset_ms_) e.duration_s = uint16_t((now_ms - onset_ms_ + 500) / 1000);
  e.dom_hz = rhythm_hz_;
  return e;
}

void Detector::enterCooldown(uint32_t now_ms) {
  state_ = DetState::Cooldown;
  state_start_ms_ = now_ms;
}

void Detector::clearHistory() {
  for (int age = 0; age < kMaxWindows; age++) {
    was_candidate_[age] = false;
    quality_[age] = 0;
  }
  onset_ms_ = 0;
}

DetEvent Detector::update(const Features& f, uint32_t now_ms) {
  const bool candidate = isCandidate(f);
  ongoing_ = isOngoing(f);
  // A tap is only trusted when the wrist shows no rhythm and no strong rotation.
  steady_ = !ongoing_ && f.gyr_rms < cfg_.gyr_rms_min * cfg_.ongoing_scale;

  // Move the history back by one second; the newest window goes in front.
  for (int age = kMaxWindows - 1; age > 0; age--) {
    was_candidate_[age] = was_candidate_[age - 1];
    quality_[age] = quality_[age - 1];
  }
  was_candidate_[0] = candidate;
  quality_[0] = candidate ? 0.5f * (f.acc_rhythm + f.gyr_rhythm) : 0;
  if (candidate) rhythm_hz_ = f.gyr_dom_hz;

  switch (state_) {
    case DetState::Idle: {
      if (candidateCount() < cfg_.confirm_min) break;
      // The motion began at the oldest candidate window in the confirm span.
      int oldest = 0;
      for (int age = 0; age < cfg_.confirm_windows; age++)
        if (was_candidate_[age]) oldest = age;
      onset_ms_ = now_ms - uint32_t(oldest) * 1000;
      state_ = DetState::PreAlert;
      state_start_ms_ = now_ms;
      return makeEvent(EventType::PreAlert, now_ms, confidence());
    }
    case DetState::PreAlert:
      // No cancel in time -> alert, even if the motion has stopped: the wearer
      // may be unable to respond.
      if (!elapsed(state_start_ms_, now_ms, cfg_.cancel_window_s)) break;
      state_ = DetState::Alert;
      state_start_ms_ = last_update_ms_ = now_ms;
      quiet_s_ = 0;
      alert_confidence_ = confidence();
      return makeEvent(EventType::Alert, now_ms, alert_confidence_);

    case DetState::Alert:
      quiet_s_ = ongoing_ ? 0 : quiet_s_ + 1;
      if (quiet_s_ >= cfg_.end_quiet_s) {
        DetEvent e = makeEvent(EventType::Ended, now_ms, alert_confidence_);
        e.duration_s = e.duration_s > quiet_s_ ? e.duration_s - quiet_s_ : 0;  // minus the quiet tail
        enterCooldown(now_ms);
        return e;
      }
      if (elapsed(last_update_ms_, now_ms, cfg_.update_every_s)) {
        last_update_ms_ = now_ms;
        return makeEvent(EventType::Update, now_ms, alert_confidence_);
      }
      break;

    case DetState::Cooldown:
      if (elapsed(state_start_ms_, now_ms, cfg_.cooldown_s)) {
        state_ = DetState::Idle;
        clearHistory();
      }
      break;
  }
  return DetEvent();
}

DetEvent Detector::requestCancel(CancelSource source, uint32_t now_ms) {
  if (state_ != DetState::PreAlert && state_ != DetState::Alert) return DetEvent();
  const uint8_t conf = state_ == DetState::Alert ? alert_confidence_ : confidence();
  // A seizure must never cancel its own alert.
  const bool tap_allowed = state_ == DetState::PreAlert && steady_;
  if (source == CancelSource::Tap && !tap_allowed) return makeEvent(EventType::CancelRejected, now_ms, conf);
  DetEvent e = makeEvent(EventType::Cancelled, now_ms, conf);
  enterCooldown(now_ms);
  return e;
}
