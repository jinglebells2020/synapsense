// Turns the 52 Hz IMU stream into movement features, once per second.
//
// Every second it looks back over the last 4 s and measures, separately for
// the accelerometer and the gyroscope, how strong the motion is (RMS), its
// dominant frequency and how rhythmic it is (FFT). Plain C++: also runs on
// the Mac for tests and replays.
#pragma once
#include <stdint.h>

#include "fft.h"

struct Sample {
  float ax, ay, az;  // acceleration, g
  float gx, gy, gz;  // angular rate, deg/s
};

struct Features {
  float acc_rms = 0;     // gravity-free acceleration, RMS over 4 s, g
  float gyr_rms = 0;     // angular rate, RMS over 4 s, deg/s
  float acc_rms_1s = 0;  // the same over the newest 1 s ("is it moving now?")
  float gyr_rms_1s = 0;
  float acc_p2p = 0;     // peak-to-peak of the raw acceleration magnitude, g
  float jerk = 0;        // mean change in acceleration per second, g/s
  float acc_dom_hz = 0;  // dominant frequency, Hz
  float gyr_dom_hz = 0;
  float acc_rhythm = 0;  // share of the power in that one rhythm, 0..1
  float gyr_rhythm = 0;
};

class FeatureExtractor {
 public:
  static constexpr int kFs = 52;        // samples per second
  static constexpr int kWin = 4 * kFs;  // 4 s window
  static constexpr int kHop = kFs;      // new features every 1 s

  FeatureExtractor();
  // Adds one sample. Returns true when new features are ready.
  bool push(const Sample& s);
  const Features& features() const { return features_; }
  void reset();

 private:
  void compute();
  void powerSpectrum(const float* x, const float* y, const float* z, float* power);
  static void findRhythm(const float* power, float* dom_hz, float* rhythm);

  // The last kWin samples (oldest overwritten): 3 accel + 3 gyro axes after
  // the high-pass filter, and the raw acceleration magnitude.
  float axes_[6][kWin];
  float accel_mag_[kWin];
  int next_ = 0;            // slot for the next sample
  int stored_ = 0;          // samples in the window, up to kWin
  int since_features_ = 0;  // samples since features were last computed

  // High-pass filter (0.5 Hz): removes gravity and gyro bias.
  float prev_in_[6] = {0};
  float prev_out_[6] = {0};
  bool filter_started_ = false;

  float hann_[kWin];
  Fft fft_;
  Features features_;
};
