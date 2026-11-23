#include "imu.h"

#include <Wire.h>

namespace {
constexpr uint8_t kAddress = 0x6A;
// Registers (LSM6DS3TR-C datasheet, section 9).
constexpr uint8_t FIFO_CTRL1 = 0x06, FIFO_CTRL2 = 0x07, FIFO_CTRL3 = 0x08, FIFO_CTRL4 = 0x09,
                  FIFO_CTRL5 = 0x0A, INT1_CTRL = 0x0D, WHO_AM_I = 0x0F, CTRL1_XL = 0x10,
                  CTRL2_G = 0x11, CTRL3_C = 0x12, CTRL7_G = 0x16, TAP_SRC = 0x1C,
                  FIFO_STATUS1 = 0x3A, FIFO_STATUS3 = 0x3C, FIFO_DATA_OUT_L = 0x3E,
                  TAP_CFG = 0x58, TAP_THS_6D = 0x59, INT_DUR2 = 0x5A, WAKE_UP_THS = 0x5B,
                  MD1_CFG = 0x5E;
constexpr int kWordsPerSample = 6;  // gx gy gz ax ay az, 16 bits each
constexpr int kBytesPerSample = 2 * kWordsPerSample;
constexpr int kSamplesPerRead = 5;  // 60 bytes per I2C read (Wire's buffer is 64)
}  // namespace

bool Imu::write(uint8_t reg, uint8_t value) {
  Wire1.beginTransmission(kAddress);
  Wire1.write(reg);
  Wire1.write(value);
  return Wire1.endTransmission() == 0;
}

uint8_t Imu::read(uint8_t reg) {
  uint8_t value = 0;
  readBytes(reg, &value, 1);
  return value;
}

bool Imu::readBytes(uint8_t reg, uint8_t* buf, uint8_t len) {
  Wire1.beginTransmission(kAddress);
  Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom(kAddress, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire1.read();
  return true;
}

bool Imu::begin() {
  // The sensor is powered from a GPIO pin (P1.08): give it high drive strength.
  pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
  const uint32_t pin = g_ADigitalPinMap[PIN_LSM6DS3TR_C_POWER];
  NRF_GPIO_Type* port = pin >= 32 ? NRF_P1 : NRF_P0;
  port->PIN_CNF[pin & 0x1F] = (port->PIN_CNF[pin & 0x1F] & ~GPIO_PIN_CNF_DRIVE_Msk) |
                              (GPIO_PIN_CNF_DRIVE_H0H1 << GPIO_PIN_CNF_DRIVE_Pos);
  digitalWrite(PIN_LSM6DS3TR_C_POWER, HIGH);
  delay(20);

  Wire1.begin();
  Wire1.setClock(400000);
  if (read(WHO_AM_I) != 0x6A) return false;

  write(CTRL3_C, 0x01);  // software reset
  delay(10);
  write(CTRL3_C, 0x44);  // block data update (no half-old samples) + address auto-increment

  write(CTRL1_XL, 0x6C);  // accel 416 Hz (fast enough for the tap engine), +/-8 g
  write(CTRL2_G, 0x3C);   // gyro 52 Hz, 2000 dps
  write(CTRL7_G, 0x80);   // gyro normal mode instead of high-performance: less power

  // FIFO: both sensors at 52 Hz, continuous, interrupt when 1 s of samples is waiting.
  const uint16_t threshold_words = kSamplesPerInterrupt * kWordsPerSample;
  write(FIFO_CTRL5, 0x00);  // bypass mode: empties the FIFO
  write(FIFO_CTRL1, threshold_words & 0xFF);
  write(FIFO_CTRL2, (threshold_words >> 8) & 0x07);
  write(FIFO_CTRL3, 0x09);  // gyro and accel into the FIFO, no decimation
  write(FIFO_CTRL4, 0x00);
  write(FIFO_CTRL5, (0x3 << 3) | 0x6);  // FIFO rate 52 Hz, continuous mode

  // Double-tap on any axis, latched until TAP_SRC is read.
  write(TAP_CFG, 0x8F);
  write(TAP_THS_6D, 0x05);   // threshold 5 x (8 g / 32) = 1.25 g
  write(INT_DUR2, 0x7F);     // taps up to ~540 ms apart
  write(WAKE_UP_THS, 0x80);  // recognise double taps (not just single)

  write(INT1_CTRL, 0x08);  // INT1 = FIFO threshold...
  write(MD1_CFG, 0x08);    // ...or double-tap
  pinMode(PIN_LSM6DS3TR_C_INT1, INPUT);
  overrun_ = false;
  return true;
}

int Imu::readFifo(RawSample* out, int max) {
  uint8_t status[2];
  if (!readBytes(FIFO_STATUS1, status, 2)) return 0;
  uint16_t words = status[0] | ((status[1] & 0x07) << 8);  // unread 16-bit words
  if (status[1] & 0x40) overrun_ = true;
  if (words == 0) return 0;

  // Skip to a sample boundary. The pattern register says which word comes
  // next (0 = gyro x ... 5 = accel z).
  uint8_t pattern[2] = {0, 0};
  if (!readBytes(FIFO_STATUS3, pattern, 2)) return 0;
  uint16_t next_word = pattern[0] | ((pattern[1] & 0x03) << 8);
  while (next_word != 0 && words > 0) {
    uint8_t skipped[2];
    readBytes(FIFO_DATA_OUT_L, skipped, 2);
    words--;
    next_word = (next_word + 1) % kWordsPerSample;
  }

  const int wanted = min(int(words / kWordsPerSample), max);
  uint8_t buf[kSamplesPerRead * kBytesPerSample];
  int done = 0;
  while (done < wanted) {
    const int count = min(kSamplesPerRead, wanted - done);
    // A burst read of FIFO_DATA_OUT wraps from 0x3F back to 0x3E by itself.
    if (!readBytes(FIFO_DATA_OUT_L, buf, count * kBytesPerSample)) break;
    for (int s = 0; s < count; s++) {
      const uint8_t* p = buf + s * kBytesPerSample;
      for (int i = 0; i < 3; i++) {
        out[done + s].g[i] = int16_t(p[2 * i] | (p[2 * i + 1] << 8));
        out[done + s].a[i] = int16_t(p[6 + 2 * i] | (p[7 + 2 * i] << 8));
      }
    }
    done += count;
  }
  return done;
}

bool Imu::doubleTapped() { return read(TAP_SRC) & 0x10; }

bool Imu::takeOverrun() {
  const bool was = overrun_;
  overrun_ = false;
  return was;
}
