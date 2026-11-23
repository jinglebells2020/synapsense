#include "features.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr float kHighPassAlpha = 0.943f;  // filter constant for a 0.5 Hz high-pass at 52 Hz
constexpr int kN = Fft::kN;               // FFT length: the 208-sample window padded to 256
constexpr float kHzPerBin = float(FeatureExtractor::kFs) / kN;  // each FFT bin is ~0.2 Hz
constexpr int kFirstBin = 3;   // ~0.6 Hz: lowest frequency we look at
constexpr int kLastBin = 73;   // ~14.8 Hz: highest
constexpr int kPeakWidth = 2;  // bins on each side that still count as "the peak" (~0.4 Hz)

// sqrt(mean(x^2 + y^2 + z^2)) over samples [first, end).
float rms3(const float* x, const float* y, const float* z, int first, int end) {
  float sum = 0;
  for (int i = first; i < end; i++) sum += x[i] * x[i] + y[i] * y[i] + z[i] * z[i];
  return sqrtf(sum / (end - first));
}
}  // namespace

FeatureExtractor::FeatureExtractor() {
  for (int i = 0; i < kWin; i++) hann_[i] = 0.5f - 0.5f * cosf(6.28318530718f * i / (kWin - 1));
  reset();
}

void FeatureExtractor::reset() {
  memset(axes_, 0, sizeof(axes_));
  next_ = stored_ = since_features_ = 0;
  filter_started_ = false;
  features_ = Features();
}

bool FeatureExtractor::push(const Sample& s) {
  const float in[6] = {s.ax, s.ay, s.az, s.gx, s.gy, s.gz};
  if (!filter_started_) {  // start the filter from the first sample, not from zero
    memcpy(prev_in_, in, sizeof(prev_in_));
    memset(prev_out_, 0, sizeof(prev_out_));
    filter_started_ = true;
  }
  // High-pass filter: keeps the changes, drops the constant part (gravity, gyro drift).
  for (int a = 0; a < 6; a++) {
    prev_out_[a] = kHighPassAlpha * (prev_out_[a] + in[a] - prev_in_[a]);
    prev_in_[a] = in[a];
    axes_[a][next_] = prev_out_[a];
  }
  next_ = (next_ + 1) % kWin;
  if (stored_ < kWin) stored_++;

  if (++since_features_ < kHop) return false;
  since_features_ = 0;
  if (stored_ < kWin) return false;  // the first 4 s don't fill a window yet
  compute();
  return true;
}

void FeatureExtractor::compute() {
  // Copy the ring buffer into time order, oldest first.
  static float w[6][kWin];
  for (int i = 0; i < kWin; i++) {
    const int j = (next_ + i) % kWin;
    for (int a = 0; a < 6; a++) w[a][i] = axes_[a][j];
  }

  Features& f = features_;
  f.acc_rms = rms3(w[0], w[1], w[2], 0, kWin);
  f.gyr_rms = rms3(w[3], w[4], w[5], 0, kWin);
  f.acc_rms_1s = rms3(w[0], w[1], w[2], kWin - kHop, kWin);
  f.gyr_rms_1s = rms3(w[3], w[4], w[5], kWin - kHop, kWin);

  // Add up the spectra of the three axes, so the result doesn't depend on
  // how the watch sits on the wrist. Then find the rhythm.
  static float power[kN / 2 + 1];
  memset(power, 0, sizeof(power));
  for (int a = 0; a < 3; a++) addPowerSpectrum(w[a], power);
  findRhythm(power, &f.acc_dom_hz, &f.acc_rhythm);

  memset(power, 0, sizeof(power));
  for (int a = 3; a < 6; a++) addPowerSpectrum(w[a], power);
  findRhythm(power, &f.gyr_dom_hz, &f.gyr_rhythm);
}

// FFT of one axis, added onto `power`: how much of the signal is at each frequency.
void FeatureExtractor::addPowerSpectrum(const float* signal, float* power) {
  static float re[kN], im[kN];
  float mean = 0;
  for (int i = 0; i < kWin; i++) mean += signal[i];
  mean /= kWin;
  // Remove the average, fade the edges (Hann window), pad with zeros to 256.
  for (int i = 0; i < kN; i++) {
    re[i] = i < kWin ? (signal[i] - mean) * hann_[i] : 0.0f;
    im[i] = 0;
  }
  fft_.forward(re, im);
  for (int k = 0; k <= kN / 2; k++) power[k] += re[k] * re[k] + im[k] * im[k];
}

// Finds the strongest frequency, and what share of all the power sits at it
// or at twice it. Jerky motion isn't a pure sine wave, so part of its rhythm
// shows up at double the frequency.
void FeatureExtractor::findRhythm(const float* power, float* dom_hz, float* rhythm) {
  int peak = kFirstBin;
  float total = 0;
  for (int k = kFirstBin; k <= kLastBin; k++) {
    total += power[k];
    if (power[k] > power[peak]) peak = k;
  }
  if (total <= 1e-12f) {  // no motion at all
    *dom_hz = *rhythm = 0;
    return;
  }
  *dom_hz = peak * kHzPerBin;

  float at_peak = 0;
  for (int k = kFirstBin; k <= kLastBin; k++) {
    const bool near_peak = abs(k - peak) <= kPeakWidth;
    const bool near_double = abs(k - 2 * peak) <= kPeakWidth;
    if (near_peak || near_double) at_peak += power[k];
  }
  *rhythm = at_peak / total;
}
