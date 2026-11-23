// Fixed-size ring buffer. Keeps the newest N items; oldest are overwritten.
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
  // i = 0 is the oldest item still stored, i = size()-1 the newest.
  const T& at(size_t i) const { return buf_[(head_ + N - count_ + i) % N]; }
  size_t size() const { return count_; }
  static constexpr size_t capacity() { return N; }
  bool full() const { return count_ == N; }
  void clear() { head_ = count_ = 0; }
  // Copies items oldest-first into out (must hold capacity() items). Returns count.
  size_t copyTo(T* out) const {
    for (size_t i = 0; i < count_; i++) out[i] = at(i);
    return count_;
  }

 private:
  T buf_[N];
  size_t head_ = 0;
  size_t count_ = 0;
};
