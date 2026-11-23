// Synthetic wrist-motion generators for host tests. Rough physical models, not
// real recordings: they exist to check the detector logic end to end.
#pragma once
#include <math.h>
#include <stdint.h>

#include <functional>
#include <random>

#include "features.h"

constexpr float kPi = 3.14159265f;
constexpr float kDt = 1.0f / FeatureExtractor::kFs;

struct Synth {
  std::mt19937 rng{1234};
  std::normal_distribution<float> n{0.0f, 1.0f};
  float acc_noise = 0.01f;  // g
  float gyr_noise = 1.5f;   // deg/s

  Sample base() {
    // Gravity on z plus sensor noise; wrist held roughly flat.
    return Sample{acc_noise * n(rng), acc_noise * n(rng), 1.0f + acc_noise * n(rng),
                  gyr_noise * n(rng), gyr_noise * n(rng), gyr_noise * n(rng)};
  }
};

// Gaussian pulse train (heel strikes, claps). Returns pulse value at time t.
inline float pulses(float t, float rate_hz, float sigma_s) {
  const float period = 1.0f / rate_hz;
  float ph = fmodf(t, period);
  if (ph > period / 2) ph -= period;
  return expf(-0.5f * (ph / sigma_s) * (ph / sigma_s));
}

using Gen = std::function<Sample(Synth&, float t)>;

inline Gen still() {
  return [](Synth& s, float) { return s.base(); };
}

inline Gen walking(float swing_hz = 0.9f) {
  return [=](Synth& s, float t) {
    Sample x = s.base();
    const float w = 2 * kPi * swing_hz * t;
    x.gy += 100.0f * sinf(w);
    x.ax += 0.25f * sinf(w + 0.5f);
    x.az += 0.5f * pulses(t, 2 * swing_hz, 0.025f);  // heel strikes at step rate
    return x;
  };
}

inline Gen running() {
  return [](Synth& s, float t) {
    Sample x = s.base();
    const float w = 2 * kPi * 1.4f * t;
    x.gy += 300.0f * sinf(w);
    x.gx += 60.0f * sinf(w + 1.0f);
    x.ax += 0.8f * sinf(w + 0.5f);
    x.az += 2.0f * pulses(t, 2.8f, 0.02f);
    x.ay += 0.6f * pulses(t, 2.8f, 0.02f);
    return x;
  };
}

// Tooth brushing: fast back-and-forth translation, little wrist rotation.
inline Gen brushing() {
  return [](Synth& s, float t) {
    Sample x = s.base();
    const float w = 2 * kPi * 4.5f * t;
    x.ax += 0.7f * sinf(w);
    x.ay += 0.2f * sinf(w + 0.8f);
    x.gz += 30.0f * sinf(w + 0.3f);
    return x;
  };
}

// Clapping / bumping the desk: sharp isolated impacts every 3 s.
inline Gen impacts() {
  return [](Synth& s, float t) {
    Sample x = s.base();
    const float p = pulses(t, 1.0f / 3.0f, 0.015f);
    x.ax += 4.0f * p;
    x.gx += 300.0f * p;
    return x;
  };
}

// Waving goodbye: 2.5 Hz, big rotation, but only in 5 s bursts every 20 s.
inline Gen waving_bursts() {
  return [](Synth& s, float t) {
    Sample x = s.base();
    if (fmodf(t, 20.0f) < 5.0f) {
      const float w = 2 * kPi * 2.5f * t;
      x.gz += 350.0f * sinf(w);
      x.ax += 1.0f * sinf(w + 0.6f);
    }
    return x;
  };
}

// Generalized tonic-clonic seizure, starting at t0:
//   10 s tonic (stiffening: small fast tremor),
//   clonic_s of rhythmic jerking, frequency slowing from 5 Hz to 2.5 Hz,
//   then post-ictal stillness.
struct SeizureModel {
  float t0 = 30.0f;
  float tonic_s = 10.0f;
  float clonic_s = 45.0f;
  float acc_amp = 1.2f;    // g
  float gyr_amp = 280.0f;  // deg/s
  float phase = 0;
  float last_t = -1;

  float clonicStart() const { return t0 + tonic_s; }
  float clonicEnd() const { return t0 + tonic_s + clonic_s; }

  Sample at(Synth& s, float t) {
    Sample x = s.base();
    if (t < t0) {  // ordinary light hand movement before the seizure
      x.gy += 10.0f * sinf(2 * kPi * 0.3f * t);
      return x;
    }
    if (t < clonicStart()) {  // tonic phase
      x.ax += 0.08f * sinf(2 * kPi * 11.0f * t);
      x.gx += 8.0f * sinf(2 * kPi * 11.0f * t);
      return x;
    }
    if (t < clonicEnd()) {  // clonic phase
      const float u = (t - clonicStart()) / clonic_s;
      const float f = 5.0f - 2.5f * u;
      if (last_t >= 0) phase += 2 * kPi * f * (t - last_t);
      last_t = t;
      const float jerk = sinf(phase) + 0.45f * sinf(2 * phase);  // not a pure sine
      const float amp = 1.0f - 0.3f * u;                          // weakens over time
      x.gx += gyr_amp * amp * jerk;
      x.gy += 0.4f * gyr_amp * amp * sinf(phase + 0.9f);
      x.ay += acc_amp * amp * sinf(phase + 0.6f) + 0.3f * acc_amp * amp * sinf(2 * phase);
      x.ax += 0.4f * acc_amp * amp * jerk;
      return x;
    }
    return x;  // post-ictal: lying still
  }
};
