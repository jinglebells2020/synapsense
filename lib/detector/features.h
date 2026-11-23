// Turns the 52 Hz IMU stream into movement features, once per second.
//
// Every second it looks back over the last 4 s and measures, separately for
// the accelerometer and the gyroscope:
//   - how strong the motion is (RMS)
//   - its main frequency, and how rhythmic it is (FFT)
// Plain C++: the same code also runs on the Mac for tests and replays.
#pragma once
#include <stdint.h>

#include "fft.h"

struct Sample {
  float ax, ay, az;  // acceleration, g
  float gx, gy, gz;  // rotation speed, deg/s
};

struct Features {
  float acc_rms = 0;     // acceleration without gravity, RMS over 4 s, g
  float gyr_rms = 0;     // rotation speed, RMS over 4 s, deg/s
  float acc_rms_1s = 0;  // the same over the newest 1 s ("is it moving now?")
  float gyr_rms_1s = 0;
  float acc_dom_hz = 0;  // main (dominant) frequency, Hz
  float gyr_dom_hz = 0;
  float acc_rhythm = 0;  // how much of the motion is that one rhythm, 0..1
  float gyr_rhythm = 0;
};

class FeatureExtractor {
 public:
  static constexpr int kFs = 52;        // samples per second
  static constexpr int kWin = 4 * kFs;  // window: the last 4 s
  static constexpr int kHop = kFs;      // new features every 1 s

  FeatureExtractor();
  // Adds one sample. Returns true when new features are ready.
  bool push(const Sample& s);
  const Features& features() const { return features_; }
  void reset();

 private:
  void compute();
  void addPowerSpectrum(const float* signal, float* power);
  static void findRhythm(const float* power, float* dom_hz, float* rhythm);

  // The last kWin samples of each axis (3 accel + 3 gyro), after the
  // high-pass filter. A ring buffer: the oldest sample gets overwritten.
  float axes_[6][kWin];
  int next_ = 0;            // slot for the next sample
  int stored_ = 0;          // samples in the window, up to kWin
  int since_features_ = 0;  // samples since features were last computed

  // High-pass filter (0.5 Hz): removes gravity and gyro drift.
  float prev_in_[6] = {0};
  float prev_out_[6] = {0};
  bool filter_started_ = false;

  float hann_[kWin];  // window shape that fades the edges in and out before the FFT
  Fft fft_;
  Features features_;
};
