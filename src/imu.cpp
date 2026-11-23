#include "imu.h"

#include <Wire.h>

namespace {
constexpr uint8_t kAddr = 0x6A;
// Registers (LSM6DS3TR-C datasheet, section 9).
constexpr uint8_t FIFO_CTRL1 = 0x06, FIFO_CTRL2 = 0x07, FIFO_CTRL3 = 0x08,
                  FIFO_CTRL4 = 0x09, FIFO_CTRL5 = 0x0A, INT1_CTRL = 0x0D,
                  WHO_AM_I = 0x0F, CTRL1_XL = 0x10, CTRL2_G = 0x11, CTRL3_C = 0x12,
                  CTRL7_G = 0x16, TAP_SRC = 0x1C, FIFO_STATUS1 = 0x3A,
                  FIFO_STATUS3 = 0x3C, FIFO_DATA_OUT_L = 0x3E, TAP_CFG = 0x58,
                  TAP_THS_6D = 0x59, INT_DUR2 = 0x5A, WAKE_UP_THS = 0x5B,
                  MD1_CFG = 0x5E;
constexpr int kWordsPerSample = 6;
constexpr int kChunkSamples = 5;  // 60 bytes per I2C read (Wire buffer is 64)
}  // namespace

bool Imu::write(uint8_t reg, uint8_t val) {
  Wire1.beginTransmission(kAddr);
  Wire1.write(reg);
  Wire1.write(val);
  return Wire1.endTransmission() == 0;
}

uint8_t Imu::read(uint8_t reg) {
  uint8_t v = 0;
  readBytes(reg, &v, 1);
  return v;
}

bool Imu::readBytes(uint8_t reg, uint8_t* buf, uint8_t len) {
  Wire1.beginTransmission(kAddr);
  Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom(kAddr, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire1.read();
  return true;
}

bool Imu::begin() {
  // The IMU is powered from a GPIO (P1.08); use high drive strength.
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
  write(CTRL3_C, 0x44);  // BDU (no torn samples) + register auto-increment

  // Accel: 416 Hz internally so the tap engine can see sharp taps, +/-8 g.
  // Gyro: 52 Hz, 2000 dps, normal (not high-performance) mode to save power.
  write(CTRL1_XL, 0x6C);
  write(CTRL2_G, 0x3C);
  write(CTRL7_G, 0x80);

  // FIFO: store both sensors at 52 Hz, continuous mode, watermark = 1 s.
  const uint16_t wtm_words = kWatermarkSamples * kWordsPerSample;
  write(FIFO_CTRL5, 0x00);  // bypass first: clears the FIFO
  write(FIFO_CTRL1, wtm_words & 0xFF);
  write(FIFO_CTRL2, (wtm_words >> 8) & 0x07);
  write(FIFO_CTRL3, 0x09);  // gyro and accel in FIFO, no decimation
  write(FIFO_CTRL4, 0x00);
  write(FIFO_CTRL5, (0x3 << 3) | 0x6);  // FIFO ODR 52 Hz, continuous mode

  // Double-tap: all axes, latched, threshold 5 x (8 g / 32) = 1.25 g.
  write(TAP_CFG, 0x8F);
  write(TAP_THS_6D, 0x05);
  write(INT_DUR2, 0x7F);     // max gap between taps ~540 ms, quiet 29 ms, shock 58 ms
  write(WAKE_UP_THS, 0x80);  // enable double-tap recognition

  // INT1 = FIFO watermark OR double-tap.
  write(INT1_CTRL, 0x08);
  write(MD1_CFG, 0x08);
  pinMode(PIN_LSM6DS3TR_C_INT1, INPUT);
  overrun_ = false;
  return true;
}

void Imu::powerOff() {
  write(CTRL1_XL, 0x00);
  write(CTRL2_G, 0x00);
  digitalWrite(PIN_LSM6DS3TR_C_POWER, LOW);
}

int Imu::readFifo(RawSample* out, int max) {
  uint8_t st[2];
  if (!readBytes(FIFO_STATUS1, st, 2)) return 0;
  uint16_t words = st[0] | ((st[1] & 0x07) << 8);
  if (st[1] & 0x40) overrun_ = true;
  if (words == 0) return 0;

  // Line up on a sample boundary: the pattern says which word comes next
  // (0 = gyro x ... 5 = accel z).
  uint8_t pat[2] = {0, 0};
  if (!readBytes(FIFO_STATUS3, pat, 2)) return 0;
  uint16_t pattern = pat[0] | ((pat[1] & 0x03) << 8);
  while (pattern != 0 && words > 0) {
    uint8_t junk[2];
    readBytes(FIFO_DATA_OUT_L, junk, 2);
    words--;
    pattern = (pattern + 1) % kWordsPerSample;
  }

  int n = words / kWordsPerSample;
  if (n > max) n = max;
  int got = 0;
  uint8_t buf[kChunkSamples * kWordsPerSample * 2];
  while (got < n) {
    const int chunk = min(kChunkSamples, n - got);
    // Burst reads of FIFO_DATA_OUT roll back from 0x3F to 0x3E automatically.
    if (!readBytes(FIFO_DATA_OUT_L, buf, chunk * kWordsPerSample * 2)) break;
    for (int s = 0; s < chunk; s++) {
      const uint8_t* p = buf + s * kWordsPerSample * 2;
      RawSample& r = out[got + s];
      for (int i = 0; i < 3; i++) r.g[i] = int16_t(p[2 * i] | (p[2 * i + 1] << 8));
      for (int i = 0; i < 3; i++) r.a[i] = int16_t(p[6 + 2 * i] | (p[6 + 2 * i + 1] << 8));
    }
    got += chunk;
  }
  return got;
}

bool Imu::doubleTapped() { return read(TAP_SRC) & 0x10; }
