#include "fft.h"

#include <math.h>

Fft::Fft() {
  const float kTwoPi = 6.28318530718f;
  for (int i = 0; i < kN / 2; i++) {
    cos_[i] = cosf(kTwoPi * i / kN);
    sin_[i] = -sinf(kTwoPi * i / kN);
  }
}

void Fft::forward(float* re, float* im) const {
  // Bit-reversal permutation.
  for (int i = 1, j = 0; i < kN; i++) {
    int bit = kN >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      float t = re[i]; re[i] = re[j]; re[j] = t;
      t = im[i]; im[i] = im[j]; im[j] = t;
    }
  }
  // Butterflies.
  for (int len = 2; len <= kN; len <<= 1) {
    const int half = len >> 1;
    const int step = kN / len;
    for (int i = 0; i < kN; i += len) {
      for (int k = 0; k < half; k++) {
        const float wr = cos_[k * step];
        const float wi = sin_[k * step];
        const int a = i + k, b = a + half;
        const float tr = re[b] * wr - im[b] * wi;
        const float ti = re[b] * wi + im[b] * wr;
        re[b] = re[a] - tr;
        im[b] = im[a] - ti;
        re[a] += tr;
        im[a] += ti;
      }
    }
  }
}
