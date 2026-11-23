// Fixed-size ring buffer: keeps the newest N items, overwriting the oldest.
//
// It's a template so the same code works for any item type: main.cpp uses
// SampleRing<RawSample, 1560> for the last 30 s of IMU samples and
// SampleRing<LogEntry, 16> for the last 16 events.
#pragma once
#include <stddef.h>

template <typename T, size_t N>
class SampleRing {
 public:
  void push(const T& item) {
    buf_[head_] = item;
    head_ = (head_ + 1) % N;
    if (count_ < N) count_++;
  }
  // i = 0 is the oldest item still stored, i = size() - 1 the newest.
  const T& at(size_t i) const { return buf_[(head_ + N - count_ + i) % N]; }
  size_t size() const { return count_; }
  // Copies the items, oldest first, into out (room for N items). Returns how many.
  size_t copyTo(T* out) const {
    for (size_t i = 0; i < count_; i++) out[i] = at(i);
    return count_;
  }

 private:
  T buf_[N];
  size_t head_ = 0;   // where the next item goes
  size_t count_ = 0;  // items stored, up to N
};
