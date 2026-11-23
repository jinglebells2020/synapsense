// Small in-place radix-2 complex FFT (no external DSP library, runs on the
// nRF52840's FPU and on the host for tests).
#pragma once

class Fft {
 public:
  static constexpr int kN = 256;
  Fft();
  // In-place forward transform of kN complex points.
  void forward(float* re, float* im) const;

 private:
  float cos_[kN / 2];
  float sin_[kN / 2];
};
