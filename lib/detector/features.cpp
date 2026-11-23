#include "features.h"

#include <math.h>
#include <string.h>

namespace {
constexpr float kHighPassAlpha = 0.943f;  // RC / (RC + dt) for 0.5 Hz at 52 Hz
constexpr int kN = Fft::kN;               // the 208-sample window is zero-padded to 256
constexpr float kHzPerBin = float(FeatureExtractor::kFs) / kN;  // ~0.2 Hz
constexpr int kFirstBin = 3;   // ~0.6 Hz: frequency range we look at...
constexpr int kLastBin = 73;   // ...up to ~14.8 Hz
constexpr int kPeakWidth = 2;  // bins on each side that count as part of a peak

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
  memset(accel_mag_, 0, sizeof(accel_mag_));
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
  for (int a = 0; a < 6; a++) {
    prev_out_[a] = kHighPassAlpha * (prev_out_[a] + in[a] - prev_in_[a]);
    prev_in_[a] = in[a];
    axes_[a][next_] = prev_out_[a];
  }
  accel_mag_[next_] = sqrtf(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
  next_ = (next_ + 1) % kWin;
  if (stored_ < kWin) stored_++;

  if (++since_features_ < kHop) return false;
  since_features_ = 0;
  if (stored_ < kWin) return false;  // the first 4 s don't fill a window yet
  compute();
  return true;
}

void FeatureExtractor::compute() {
  // Copy the ring into time order, oldest first.
  static float w[6][kWin];
  static float mag[kWin];
  for (int i = 0; i < kWin; i++) {
    const int j = (next_ + i) % kWin;
    for (int a = 0; a < 6; a++) w[a][i] = axes_[a][j];
    mag[i] = accel_mag_[j];
  }

  Features& f = features_;
  f.acc_rms = rms3(w[0], w[1], w[2], 0, kWin);
  f.gyr_rms = rms3(w[3], w[4], w[5], 0, kWin);
  f.acc_rms_1s = rms3(w[0], w[1], w[2], kWin - kHop, kWin);
  f.gyr_rms_1s = rms3(w[3], w[4], w[5], kWin - kHop, kWin);

  float lowest = mag[0], highest = mag[0], jerk = 0;
  for (int i = 1; i < kWin; i++) {
    lowest = fminf(lowest, mag[i]);
    highest = fmaxf(highest, mag[i]);
    const float dx = w[0][i] - w[0][i - 1];
    const float dy = w[1][i] - w[1][i - 1];
    const float dz = w[2][i] - w[2][i - 1];
    jerk += sqrtf(dx * dx + dy * dy + dz * dz);
  }
  f.acc_p2p = highest - lowest;
  f.jerk = jerk * kFs / (kWin - 1);

  static float power[kN / 2 + 1];
  powerSpectrum(w[0], w[1], w[2], power);
  findRhythm(power, &f.acc_dom_hz, &f.acc_rhythm);
  powerSpectrum(w[3], w[4], w[5], power);
  findRhythm(power, &f.gyr_dom_hz, &f.gyr_rhythm);
}

// Power spectrum of a 3-axis signal: the sum of the three axes' spectra, so
// the result doesn't depend on how the watch sits on the wrist.
// Two real signals fit in one complex FFT (one as the real part, one as the
// imaginary part), so three axes cost two FFTs.
void FeatureExtractor::powerSpectrum(const float* x, const float* y, const float* z, float* power) {
  static float re[kN], im[kN];
  // FFT of a (+ j*b if given): mean removed, Hann window, zero-padded.
  auto fft = [&](const float* a, const float* b) {
    float mean_a = 0, mean_b = 0;
    for (int i = 0; i < kWin; i++) {
      mean_a += a[i];
      if (b) mean_b += b[i];
    }
    mean_a /= kWin;
    mean_b /= kWin;
    for (int i = 0; i < kN; i++) {
      re[i] = i < kWin ? (a[i] - mean_a) * hann_[i] : 0.0f;
      im[i] = i < kWin && b ? (b[i] - mean_b) * hann_[i] : 0.0f;
    }
    fft_.forward(re, im);
  };

  fft(x, y);
  for (int k = 0; k <= kN / 2; k++) {
    // Separate the two: X[k] = (Z[k] + conj Z[N-k]) / 2, Y[k] = (Z[k] - conj Z[N-k]) / 2j.
    const int m = (kN - k) % kN;
    const float xr = 0.5f * (re[k] + re[m]), xi = 0.5f * (im[k] - im[m]);
    const float yr = 0.5f * (im[k] + im[m]), yi = -0.5f * (re[k] - re[m]);
    power[k] = xr * xr + xi * xi + yr * yr + yi * yi;
  }
  fft(z, nullptr);
  for (int k = 0; k <= kN / 2; k++) power[k] += re[k] * re[k] + im[k] * im[k];
}

// Finds the strongest frequency, and what share of all the power sits in that
// peak plus its 2nd harmonic (jerky motion is not a pure sine, so part of its
// rhythm shows up at twice the frequency).
void FeatureExtractor::findRhythm(const float* power, float* dom_hz, float* rhythm) {
  float total = 0;
  int peak = kFirstBin;
  for (int k = kFirstBin; k <= kLastBin; k++) {
    total += power[k];
    if (power[k] > power[peak]) peak = k;
  }
  if (total <= 1e-12f) {
    *dom_hz = *rhythm = 0;
    return;
  }

  // Refine the peak between bins with a parabola through its 3 bins.
  float offset = 0;
  if (peak > kFirstBin && peak < kLastBin) {
    const float a = power[peak - 1], b = power[peak], c = power[peak + 1];
    const float curve = a - 2 * b + c;
    if (fabsf(curve) > 1e-12f) offset = 0.5f * (a - c) / curve;
  }
  *dom_hz = (peak + offset) * kHzPerBin;

  bool counted[kN / 2 + 1] = {false};  // the two ranges can overlap at low frequencies
  float peak_power = 0;
  for (int harmonic = 1; harmonic <= 2; harmonic++) {
    const int center = peak * harmonic;
    for (int k = center - kPeakWidth; k <= center + kPeakWidth; k++) {
      if (k < kFirstBin || k > kLastBin || counted[k]) continue;
      counted[k] = true;
      peak_power += power[k];
    }
  }
  *rhythm = peak_power / total;
}
