// LSM6DS3TR-C driver for the XIAO nRF52840 Sense (internal I2C bus Wire1).
//
// The sensor does the buffering: accel + gyro samples go into its FIFO at
// 52 Hz, and INT1 fires once a second (watermark = 52 samples). The CPU sleeps
// in between. The sensor's tap engine also raises INT1 on a double-tap.
#pragma once
#include <Arduino.h>

struct RawSample {
  int16_t g[3];  // gyro x, y, z   (FIFO order: gyro first, then accel)
  int16_t a[3];  // accel x, y, z
};

class Imu {
 public:
  static constexpr float kAccelG = 0.000244f;  // g per LSB at +/-8 g
  static constexpr float kGyroDps = 0.070f;    // deg/s per LSB at 2000 dps
  static constexpr int16_t kClip = 32000;      // |raw| above this = clipping
  static constexpr uint16_t kWatermarkSamples = 52;

  // Powers the sensor, checks WHO_AM_I and configures FIFO + tap engine.
  bool begin();
  void powerOff();
  // Reads every complete sample waiting in the FIFO (up to max). Returns count.
  int readFifo(RawSample* out, int max);
  // Returns true if a double-tap happened since the last call.
  bool doubleTapped();
  bool overrun() const { return overrun_; }
  void clearOverrun() { overrun_ = false; }
  bool intPinHigh() const { return digitalRead(PIN_LSM6DS3TR_C_INT1) == HIGH; }

 private:
  bool write(uint8_t reg, uint8_t val);
  uint8_t read(uint8_t reg);
  bool readBytes(uint8_t reg, uint8_t* buf, uint8_t len);
  bool overrun_ = false;
};
