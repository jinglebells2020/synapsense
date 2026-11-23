// LSM6DS3TR-C motion sensor on the XIAO nRF52840 Sense (internal I2C bus Wire1).
//
// The sensor buffers accel + gyro samples in its FIFO at 52 Hz and raises INT1
// once a second (52 samples waiting), so the CPU can sleep in between. Its tap
// engine also raises INT1 on a double-tap.
#pragma once
#include <Arduino.h>

struct RawSample {
  int16_t g[3];  // gyro x, y, z (the FIFO stores gyro first, then accel)
  int16_t a[3];  // accel x, y, z
};

class Imu {
 public:
  static constexpr float kAccelG = 0.000244f;  // g per count at +/-8 g
  static constexpr float kGyroDps = 0.070f;    // deg/s per count at 2000 dps
  static constexpr int16_t kClip = 32000;      // |count| above this = clipping
  static constexpr uint16_t kSamplesPerInterrupt = 52;

  bool begin();                           // power up, check the chip ID, set up FIFO + tap
  int readFifo(RawSample* out, int max);  // complete samples waiting; returns how many
  bool doubleTapped();                    // double-tap since the last call?
  bool takeOverrun();                     // FIFO overflowed since the last call?
  bool interruptPending() const { return digitalRead(PIN_LSM6DS3TR_C_INT1) == HIGH; }

 private:
  bool write(uint8_t reg, uint8_t value);
  uint8_t read(uint8_t reg);
  bool readBytes(uint8_t reg, uint8_t* buf, uint8_t len);
  bool overrun_ = false;
};
