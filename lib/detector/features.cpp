#include "features.h"

#include <math.h>
#include <string.h>

namespace {
constexpr float kHpAlpha = 0.943f;  // RC/(RC+dt), fc = 0.5 Hz at 52 Hz
constexpr float kBinHz = float(FeatureExtractor::kFs) / FeatureExtractor::kFft;
// Band used for "total power" and for the dominant-peak search.
constexpr int kBandLo = 3;   // ~0.6 Hz
constexpr int kBandHi = 73;  // ~14.8 Hz
constexpr int kPeakHalfWidth = 2;  // +/- 0.4 Hz counted as "the peak"
}  // namespace

FeatureExtractor::FeatureExtractor() {
  for (int i = 0; i < kWin; i++) {
    hann_[i] = 0.5f - 0.5f * cosf(6.28318530718f * i / (kWin - 1));
  }
  reset();
}

void FeatureExtractor::reset() {
  memset(hp_, 0, sizeof(hp_));
  memset(mag_, 0, sizeof(mag_));
  head_ = filled_ = since_hop_ = 0;
  primed_ = false;
  f_ = Features();
}

bool FeatureExtractor::push(const Sample& s) {
  const float in[6] = {s.ax, s.ay, s.az, s.gx, s.gy, s.gz};
  if (!primed_) {
    for (int i = 0; i < 6; i++) {
      prev_in_[i] = in[i];
      prev_out_[i] = 0;
    }
    primed_ = true;
  }
  for (int i = 0; i < 6; i++) {
    const float out = kHpAlpha * (prev_out_[i] + in[i] - prev_in_[i]);
    prev_in_[i] = in[i];
    prev_out_[i] = out;
    hp_[i][head_] = out;
  }
  mag_[head_] = sqrtf(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
  head_ = (head_ + 1) % kWin;
  if (filled_ < kWin) filled_++;
  if (++since_hop_ < kHop) return false;
  since_hop_ = 0;
  if (filled_ < kWin) return false;
  compute();
  return true;
}

void FeatureExtractor::compute() {
  // Unroll the ring into time order (oldest first).
  static float lin[6][kWin];
  static float mag[kWin];
  for (int i = 0; i < kWin; i++) {
    const int j = (head_ + i) % kWin;
    for (int a = 0; a < 6; a++) lin[a][i] = hp_[a][j];
    mag[i] = mag_[j];
  }

  float acc2 = 0, gyr2 = 0, jerk = 0;
  float mmin = mag[0], mmax = mag[0];
  for (int i = 0; i < kWin; i++) {
    acc2 += lin[0][i] * lin[0][i] + lin[1][i] * lin[1][i] + lin[2][i] * lin[2][i];
    gyr2 += lin[3][i] * lin[3][i] + lin[4][i] * lin[4][i] + lin[5][i] * lin[5][i];
    if (mag[i] < mmin) mmin = mag[i];
    if (mag[i] > mmax) mmax = mag[i];
    if (i > 0) {
      const float dx = lin[0][i] - lin[0][i - 1];
      const float dy = lin[1][i] - lin[1][i - 1];
      const float dz = lin[2][i] - lin[2][i - 1];
      jerk += sqrtf(dx * dx + dy * dy + dz * dz);
    }
  }
  float acc2_1s = 0, gyr2_1s = 0;
  for (int i = kWin - kHop; i < kWin; i++) {
    acc2_1s += lin[0][i] * lin[0][i] + lin[1][i] * lin[1][i] + lin[2][i] * lin[2][i];
    gyr2_1s += lin[3][i] * lin[3][i] + lin[4][i] * lin[4][i] + lin[5][i] * lin[5][i];
  }
  f_.acc_rms_1s = sqrtf(acc2_1s / kHop);
  f_.gyr_rms_1s = sqrtf(gyr2_1s / kHop);
  f_.acc_rms = sqrtf(acc2 / kWin);
  f_.gyr_rms = sqrtf(gyr2 / kWin);
  f_.acc_p2p = mmax - mmin;
  f_.jerk = jerk * kFs / (kWin - 1);

  static float power[kFft / 2 + 1];
  spectrum3(lin[0], lin[1], lin[2], power);
  peakAndRhythm(power, &f_.acc_dom_hz, &f_.acc_rhythm);
  spectrum3(lin[3], lin[4], lin[5], power);
  peakAndRhythm(power, &f_.gyr_dom_hz, &f_.gyr_rhythm);
}

// Sum of the three axes' power spectra. Summing per-axis power makes the
// result independent of how the watch is rotated on the wrist.
// Two real signals share one complex FFT (x in the real part, y in the
// imaginary part), so three axes cost two FFTs.
void FeatureExtractor::spectrum3(const float* x, const float* y, const float* z,
                                 float* power) {
  static float re[kFft], im[kFft];
  const int half = kFft / 2;
  for (int k = 0; k <= half; k++) power[k] = 0;

  auto load = [&](const float* a, const float* b) {
    float ma = 0, mb = 0;
    for (int i = 0; i < kWin; i++) {
      ma += a[i];
      if (b) mb += b[i];
    }
    ma /= kWin;
    mb /= kWin;
    for (int i = 0; i < kFft; i++) {
      if (i < kWin) {
        re[i] = (a[i] - ma) * hann_[i];
        im[i] = b ? (b[i] - mb) * hann_[i] : 0.0f;
      } else {
        re[i] = im[i] = 0;
      }
    }
    fft_.forward(re, im);
  };

  // Pair 1: x + j*y. Split with X[k] = (Z[k] + Z*[N-k]) / 2, Y[k] = (Z[k] - Z*[N-k]) / 2j.
  load(x, y);
  for (int k = 0; k <= half; k++) {
    const int n = (kFft - k) % kFft;
    const float xr = 0.5f * (re[k] + re[n]), xi = 0.5f * (im[k] - im[n]);
    const float yr = 0.5f * (im[k] + im[n]), yi = -0.5f * (re[k] - re[n]);
    power[k] += xr * xr + xi * xi + yr * yr + yi * yi;
  }
  // Third axis on its own.
  load(z, nullptr);
  for (int k = 0; k <= half; k++) power[k] += re[k] * re[k] + im[k] * im[k];
}

void FeatureExtractor::peakAndRhythm(const float* power, float* dom_hz, float* rhythm) {
  float total = 0;
  int peak = kBandLo;
  for (int k = kBandLo; k <= kBandHi; k++) {
    total += power[k];
    if (power[k] > power[peak]) peak = k;
  }
  if (total <= 1e-12f) {
    *dom_hz = 0;
    *rhythm = 0;
    return;
  }
  // Parabolic interpolation around the peak bin for a finer frequency.
  float delta = 0;
  if (peak > kBandLo && peak < kBandHi) {
    const float a = power[peak - 1], b = power[peak], c = power[peak + 1];
    const float den = a - 2 * b + c;
    if (fabsf(den) > 1e-12f) delta = 0.5f * (a - c) / den;
  }
  *dom_hz = (peak + delta) * kBinHz;

  // Power at the peak and at its 2nd harmonic (jerky motion is not a pure sine).
  bool counted[kFft / 2 + 1] = {false};
  float in_peak = 0;
  for (int h = 1; h <= 2; h++) {
    const int c = peak * h;
    for (int k = c - kPeakHalfWidth; k <= c + kPeakHalfWidth; k++) {
      if (k < kBandLo || k > kBandHi || counted[k]) continue;
      counted[k] = true;
      in_peak += power[k];
    }
  }
  *rhythm = in_peak / total;
}
