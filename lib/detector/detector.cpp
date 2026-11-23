#include "detector.h"

#include <string.h>

namespace {
int popcount32(uint32_t v) {
  int n = 0;
  for (; v; v &= v - 1) n++;
  return n;
}
float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
bool inBand(float hz, float lo, float hi) { return hz >= lo && hz <= hi; }
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

Detector::Detector(const DetectorConfig& cfg) : cfg_(cfg) {
  if (cfg_.confirm_window > kMaxWindow) cfg_.confirm_window = kMaxWindow;
  if (cfg_.confirm_min > cfg_.confirm_window) cfg_.confirm_min = cfg_.confirm_window;
  reset();
}

void Detector::reset() {
  state_ = DetState::Idle;
  history_ = 0;
  memset(quality_, 0, sizeof(quality_));
  last_holding_ = false;
  last_dom_hz_ = 0;
  onset_ms_ = state_since_ms_ = last_update_ms_ = 0;
  last_steady_ = true;
  quiet_s_ = 0;
  alert_confidence_ = 0;
}

bool Detector::isCandidate(const Features& f) const {
  return f.acc_rms >= cfg_.acc_rms_min && f.gyr_rms >= cfg_.gyr_rms_min &&
         f.acc_rms_1s >= cfg_.acc_rms_min * cfg_.recent_scale &&
         f.gyr_rms_1s >= cfg_.gyr_rms_min * cfg_.recent_scale &&
         inBand(f.acc_dom_hz, cfg_.band_lo_hz, cfg_.band_hi_hz) &&
         inBand(f.gyr_dom_hz, cfg_.band_lo_hz, cfg_.band_hi_hz) &&
         f.acc_rhythm >= cfg_.rhythm_min && f.gyr_rhythm >= cfg_.rhythm_min;
}

// Looser test: is an ongoing event still going on? Also used to decide whether
// the wrist is shaking too much for a cancel to be trusted.
bool Detector::isHolding(const Features& f) const {
  return f.acc_rms >= cfg_.acc_rms_min * cfg_.hold_scale &&
         f.gyr_rms >= cfg_.gyr_rms_min * cfg_.hold_scale &&
         inBand(f.gyr_dom_hz, cfg_.band_lo_hz, cfg_.band_hi_hz) &&
         f.gyr_rhythm >= cfg_.hold_rhythm_min;
}

// Steady enough that a double-tap is a deliberate gesture: no rhythm and no
// strong rotation in the last window.
bool Detector::isSteady(const Features& f) const {
  return !isHolding(f) && f.gyr_rms < cfg_.gyr_rms_min * cfg_.hold_scale;
}

Activity Detector::classify(const Features& f) const {
  if (isCandidate(f)) return Activity::Rhythmic;
  if (f.acc_rms < 0.02f && f.gyr_rms < 5.0f) return Activity::Still;
  const bool swing = f.gyr_dom_hz >= 0.6f && f.gyr_dom_hz < cfg_.band_lo_hz &&
                     f.gyr_rhythm >= 0.3f;
  if (swing && f.acc_rms >= 0.05f) return Activity::Walking;
  if (f.acc_rms >= 0.15f || f.gyr_rms >= 60.0f) return Activity::Active;
  return Activity::Light;
}

uint8_t Detector::candidateCount() const {
  const uint32_t mask = cfg_.confirm_window >= 32 ? 0xFFFFFFFFu
                                                  : (1u << cfg_.confirm_window) - 1;
  return popcount32(history_ & mask);
}

// Confidence = 60% persistence (share of recent windows that were candidates)
//            + 40% rhythm quality (how clean the rhythm was in those windows).
uint8_t Detector::confidence() const {
  float qsum = 0;
  int qn = 0;
  for (int i = 0; i < cfg_.confirm_window; i++) {
    if (history_ & (1u << i)) {
      qsum += quality_[i];
      qn++;
    }
  }
  const float persist = float(qn) / cfg_.confirm_window;
  const float quality = qn ? clamp01((qsum / qn) / 0.6f) : 0;
  return uint8_t(100.0f * (0.6f * persist + 0.4f * quality) + 0.5f);
}

DetEvent Detector::make(EventType t, uint32_t now_ms, uint8_t conf) const {
  DetEvent e;
  e.type = t;
  e.confidence = conf;
  e.duration_s = onset_ms_ && now_ms > onset_ms_ ? uint16_t((now_ms - onset_ms_ + 500) / 1000) : 0;
  e.dom_hz = last_dom_hz_;
  return e;
}

void Detector::toCooldown(uint32_t now_ms) {
  state_ = DetState::Cooldown;
  state_since_ms_ = now_ms;
}

DetEvent Detector::update(const Features& f, uint32_t now_ms) {
  const bool cand = isCandidate(f);
  last_holding_ = isHolding(f);
  last_steady_ = isSteady(f);

  // Shift the per-window history by one second.
  history_ = (history_ << 1) | (cand ? 1u : 0u);
  memmove(&quality_[1], &quality_[0], sizeof(float) * (kMaxWindow - 1));
  quality_[0] = cand ? 0.5f * (f.acc_rhythm + f.gyr_rhythm) : 0;
  if (cand) last_dom_hz_ = f.gyr_dom_hz;

  switch (state_) {
    case DetState::Idle: {
      if (candidateCount() >= cfg_.confirm_min) {
        // Onset = the oldest candidate window in the confirm span.
        int oldest = 0;
        for (int i = 0; i < cfg_.confirm_window; i++)
          if (history_ & (1u << i)) oldest = i;
        onset_ms_ = now_ms - uint32_t(oldest) * 1000;
        state_ = DetState::PreAlert;
        state_since_ms_ = now_ms;
        return make(EventType::PreAlert, now_ms, confidence());
      }
      return DetEvent();
    }
    case DetState::PreAlert: {
      // No cancel in time -> alert, whether or not the motion continues:
      // the wearer may be unable to respond.
      if (now_ms - state_since_ms_ >= uint32_t(cfg_.cancel_window_s) * 1000) {
        state_ = DetState::Alert;
        state_since_ms_ = last_update_ms_ = now_ms;
        quiet_s_ = 0;
        alert_confidence_ = confidence();
        return make(EventType::Alert, now_ms, alert_confidence_);
      }
      return DetEvent();
    }
    case DetState::Alert: {
      quiet_s_ = last_holding_ ? 0 : quiet_s_ + 1;
      if (quiet_s_ >= cfg_.end_quiet_s) {
        DetEvent e = make(EventType::Ended, now_ms, alert_confidence_);
        e.duration_s = e.duration_s > quiet_s_ ? e.duration_s - quiet_s_ : 0;
        toCooldown(now_ms);
        return e;
      }
      if (now_ms - last_update_ms_ >= uint32_t(cfg_.update_every_s) * 1000) {
        last_update_ms_ = now_ms;
        return make(EventType::Update, now_ms, alert_confidence_);
      }
      return DetEvent();
    }
    case DetState::Cooldown: {
      if (now_ms - state_since_ms_ >= uint32_t(cfg_.cooldown_s) * 1000) {
        state_ = DetState::Idle;
        history_ = 0;
        memset(quality_, 0, sizeof(quality_));
        onset_ms_ = 0;
      }
      return DetEvent();
    }
  }
  return DetEvent();
}

DetEvent Detector::requestCancel(CancelSource src, uint32_t now_ms) {
  if (state_ != DetState::PreAlert && state_ != DetState::Alert) return DetEvent();
  const uint8_t conf = state_ == DetState::Alert ? alert_confidence_ : confidence();
  if (src == CancelSource::Tap) {
    // Violent clonic jerks can look like taps, and a seizure must never cancel
    // its own alert. Taps only count in the pre-alert, with the wrist steady.
    if (state_ != DetState::PreAlert || !last_steady_)
      return make(EventType::CancelRejected, now_ms, conf);
  }
  DetEvent e = make(EventType::Cancelled, now_ms, conf);
  toCooldown(now_ms);
  return e;
}
