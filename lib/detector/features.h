// Turns a stream of IMU samples into per-second movement features.
//
// Every kHop samples (1 s) it looks back over the last kWin samples (4 s) and
// computes energy, rhythm and dominant-frequency features for both the
// accelerometer and the gyroscope.
#pragma once
#include <stdint.h>

#include "fft.h"

struct Sample {
  float ax, ay, az;  // acceleration, g
  float gx, gy, gz;  // angular rate, deg/s
};

struct Features {
  float acc_rms = 0;     // RMS of high-passed acceleration (gravity removed), g
  float gyr_rms = 0;     // RMS of high-passed angular rate, deg/s
  float acc_rms_1s = 0;  // same, newest 1 s only (is it moving *now*?)
  float gyr_rms_1s = 0;
  float acc_p2p = 0;     // peak-to-peak of raw acceleration magnitude, g
  float jerk = 0;        // mean |d(accel)/dt|, g/s
  float acc_dom_hz = 0;  // dominant frequency of acceleration, Hz
  float gyr_dom_hz = 0;  // dominant frequency of angular rate, Hz
  float acc_rhythm = 0;  // share of band power at the peak (+ 2nd harmonic), 0..1
  float gyr_rhythm = 0;  // same for angular rate, 0..1
};

class FeatureExtractor {
 public:
  static constexpr int kFs = 52;     // sample rate, Hz
  static constexpr int kWin = 208;   // window length: 4 s
  static constexpr int kHop = 52;    // new features every 1 s
  static constexpr int kFft = Fft::kN;

  FeatureExtractor();
  // Adds one sample. Returns true when a fresh set of features is ready.
  bool push(const Sample& s);
  const Features& features() const { return f_; }
  void reset();

 private:
  void compute();
  // Combined power spectrum of three axes, returned in power[0..kFft/2].
  void spectrum3(const float* x, const float* y, const float* z, float* power);
  static void peakAndRhythm(const float* power, float* dom_hz, float* rhythm);

  // Ring of the last kWin samples (high-passed axes + raw magnitude).
  float hp_[6][kWin];
  float mag_[kWin];
  int head_ = 0;
  int filled_ = 0;
  int since_hop_ = 0;

  // High-pass filter state (removes gravity and gyro bias, ~0.5 Hz corner).
  float prev_in_[6] = {0};
  float prev_out_[6] = {0};
  bool primed_ = false;

  float hann_[kWin];
  Fft fft_;
  Features f_;
};
