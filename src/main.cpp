// SynapSense firmware - wrist-worn tonic-clonic seizure detection prototype.
// Educational prototype. Not a medical device.
//
// Data flow:
//   LSM6DS3TR-C FIFO --(INT1, 1/s)--> readFifo --> 30 s ring buffer
//                                              \-> FeatureExtractor (4 s window, 1 s hop)
//                                                   \-> Detector state machine
//                                                        \-> LED + BLE notifications
//
// USB serial commands (115200): help, status, stream, feat, test, cancel, ack, dump, log
#include <Arduino.h>

#include "ble_link.h"
#include "detector.h"
#include "features.h"
#include "imu.h"
#include "leds.h"
#include "sample_ring.h"

namespace {
constexpr int kRingSeconds = 30;
constexpr uint32_t kStallMs = 3000;        // no IMU data this long = fault
constexpr uint32_t kBatteryEveryMs = 60000;
constexpr uint32_t kStatusEveryMs = 30000;

enum Fault : uint8_t {
  FAULT_IMU_MISSING = 1,
  FAULT_IMU_STALL = 2,
  FAULT_FIFO_OVERRUN = 4,
  FAULT_CLIPPING = 8,
  FAULT_LOW_BATTERY = 16,
};

Imu imu;
FeatureExtractor fx;
Detector det;
SampleRing<RawSample, FeatureExtractor::kFs * kRingSeconds> ring;  // live 30 s window

// Copy of the ring frozen at the moment of the last alert.
RawSample snapshot[FeatureExtractor::kFs * kRingSeconds];
size_t snapshot_len = 0;
uint32_t snapshot_ms = 0;

struct LogEntry {
  uint32_t t_ms;
  DetEvent e;
};
SampleRing<LogEntry, 16> event_log;

SemaphoreHandle_t wake_sem;
uint8_t faults = 0;
uint8_t fault_shown = 0;
uint16_t alert_count = 0;
bool event_unacked = false;
bool stream_raw = false;
bool stream_feat = false;
uint32_t last_data_ms = 0;
uint32_t last_battery_ms = 0;
uint32_t last_status_ms = 0;
float battery_v = 0;
uint8_t battery_pct = 0;
Features last_features;
Activity last_activity = Activity::Still;

void wakeFromIsr() {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(wake_sem, &woken);
  portYIELD_FROM_ISR(woken);
}
void wakeFromTask() { xSemaphoreGive(wake_sem); }

// ---------- battery ----------

uint8_t lipoPercent(float v) {
  static const float table[][2] = {{4.20f, 100}, {4.10f, 90}, {4.00f, 78}, {3.90f, 62}, {3.80f, 45},
                                   {3.70f, 28},  {3.60f, 12}, {3.50f, 4},  {3.30f, 0}};
  if (v >= table[0][0]) return 100;
  for (unsigned i = 1; i < sizeof(table) / sizeof(table[0]); i++) {
    if (v >= table[i][0]) {
      const float f = (v - table[i][0]) / (table[i - 1][0] - table[i][0]);
      return uint8_t(table[i][1] + f * (table[i - 1][1] - table[i][1]));
    }
  }
  return 0;
}

void readBattery() {
  // Divider 1M / 510k on P0.31. VBAT_ENABLE (P0.14) stays LOW permanently:
  // driving it HIGH would put battery voltage on the ADC pin.
  analogReference(AR_INTERNAL_2_4);
  analogReadResolution(12);
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(PIN_VBAT);
  analogReference(AR_DEFAULT);
  battery_v = (sum / 8.0f) / 4096.0f * 2.4f * (1510.0f / 510.0f);
  battery_pct = lipoPercent(battery_v);
  if (battery_v > 2.5f && battery_v < 3.45f) faults |= FAULT_LOW_BATTERY;
  else faults &= ~FAULT_LOW_BATTERY;
  ble::setBattery(battery_pct);
}

// ---------- text helpers ----------

void faultText(char* out, size_t n) {
  if (!faults) {
    snprintf(out, n, "none");
    return;
  }
  snprintf(out, n, "%s%s%s%s%s", faults & FAULT_IMU_MISSING ? "imu-missing " : "",
           faults & FAULT_IMU_STALL ? "imu-stall " : "", faults & FAULT_FIFO_OVERRUN ? "overrun " : "",
           faults & FAULT_CLIPPING ? "clipping " : "", faults & FAULT_LOW_BATTERY ? "low-battery " : "");
}

void publishStatus() {
  char f[64], s[128];
  faultText(f, sizeof(f));
  snprintf(s, sizeof(s), "%s | bat %u%% %.2fV | %s | faults %s| up %lus", stateName(det.state()),
           battery_pct, battery_v, activityName(last_activity), f, millis() / 1000);
  ble::sendStatus(s);
  if (Serial) Serial.printf("STATUS %s\n", s);
  last_status_ms = millis();
}

// ---------- LED base pattern follows the state ----------

void updateLedBase() {
  if (det.state() == DetState::Alert) led::setBase(led::kAlert);
  else if (det.state() == DetState::PreAlert) led::setBase(led::kPreAlert);
  else if (faults & (FAULT_IMU_MISSING | FAULT_IMU_STALL)) led::setBase(led::kFault);
  else if (event_unacked) led::setBase(led::kHeartbeatEvent);
  else if (ble::connected()) led::setBase(led::kHeartbeatConn);
  else led::setBase(led::kHeartbeatAdv);
}

// ---------- detector events -> LED / BLE / serial ----------

void handleEvent(const DetEvent& e, const char* source) {
  if (e.type == EventType::None) return;
  const uint32_t now = millis();
  char msg[128];
  switch (e.type) {
    case EventType::PreAlert:
      snprintf(msg, sizeof(msg), "PRE-ALERT: rhythmic shaking %us, conf %u%%. Double-tap watch to cancel",
               e.duration_s, e.confidence);
      led::play(led::kPreAlert);
      ble::advertiseFast();
      break;
    case EventType::Alert:
      alert_count++;
      event_unacked = true;
      snprintf(msg, sizeof(msg), "ALERT #%u: possible tonic-clonic seizure. conf %u%%, %us, %.1f Hz, t+%lus",
               alert_count, e.confidence, e.duration_s, e.dom_hz, now / 1000);
      snapshot_len = ring.copyTo(snapshot);
      snapshot_ms = now;
      led::play(led::kAlert);
      ble::advertiseFast();
      break;
    case EventType::Update:
      snprintf(msg, sizeof(msg), "ONGOING #%u: %us so far, conf %u%%", alert_count, e.duration_s, e.confidence);
      break;
    case EventType::Ended:
      snprintf(msg, sizeof(msg), "ENDED #%u: shaking lasted %us. Check on the wearer.", alert_count,
               e.duration_s);
      break;
    case EventType::Cancelled:
      snprintf(msg, sizeof(msg), "CANCELLED by wearer (%s)", source);
      event_unacked = false;
      led::play(led::kCancelled);
      break;
    case EventType::CancelRejected:
      // Not an alert: tell the wearer locally, and note it on the status channel.
      led::play(led::kRejected);
      ble::sendStatus("Cancel ignored: hold the wrist still, then double-tap (pre-alert only)");
      if (Serial) Serial.println("EVENT CANCEL_REJECTED");
      return;
    default:
      return;
  }
  event_log.push({now, e});
  ble::sendAlert(msg);
  if (Serial) Serial.printf("EVENT %s\n", msg);
  updateLedBase();
}

void onWindow(const Features& f, uint32_t now) {
  last_features = f;
  last_activity = det.classify(f);
  handleEvent(det.update(f, now), "");
  updateLedBase();

  char live[128];
  snprintf(live, sizeof(live), "a%.2fg g%.0fdps fa%.1f fg%.1f r%.2f/%.2f c%u/10 %s", f.acc_rms, f.gyr_rms,
           f.acc_dom_hz, f.gyr_dom_hz, f.acc_rhythm, f.gyr_rhythm, det.candidateCount(),
           stateName(det.state()));
  ble::sendLive(live);
  if (stream_feat && Serial) Serial.printf("FEAT %s\n", live);
}

void onCancel(CancelSource src) {
  const uint32_t now = millis();
  const DetState st = det.state();
  if (st == DetState::PreAlert || st == DetState::Alert) {
    handleEvent(det.requestCancel(src, now), src == CancelSource::Tap ? "double-tap" : "phone");
  } else if (event_unacked) {
    // Outside an event, a cancel/tap acknowledges the last alert.
    event_unacked = false;
    led::play(led::kCancelled);
    updateLedBase();
  }
}

void sendTest() {
  ble::sendAlert("TEST ALERT: SynapSense link check, no event");
  led::play(led::kTest);
  if (Serial) Serial.println("EVENT TEST ALERT");
}

// ---------- IMU ----------

void startImu() {
  if (imu.begin()) {
    faults &= ~(FAULT_IMU_MISSING | FAULT_IMU_STALL);
  } else {
    faults |= FAULT_IMU_MISSING;
  }
  fx.reset();
  last_data_ms = millis();
}

void drainImu() {
  static RawSample batch[64];
  int n;
  while ((n = imu.readFifo(batch, 64)) > 0) {
    const uint32_t now = millis();
    last_data_ms = now;
    for (int i = 0; i < n; i++) {
      const RawSample& r = batch[i];
      ring.push(r);
      for (int k = 0; k < 3; k++)
        if (abs(r.a[k]) > Imu::kClip) faults |= FAULT_CLIPPING;
      Sample s{r.a[0] * Imu::kAccelG, r.a[1] * Imu::kAccelG, r.a[2] * Imu::kAccelG,
               r.g[0] * Imu::kGyroDps, r.g[1] * Imu::kGyroDps, r.g[2] * Imu::kGyroDps};
      if (stream_raw && Serial)
        Serial.printf("%lu,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f\n", now, s.ax, s.ay, s.az, s.gx, s.gy, s.gz);
      if (fx.push(s)) onWindow(fx.features(), now);
    }
  }
  if (imu.overrun()) {
    faults |= FAULT_FIFO_OVERRUN;
    imu.clearOverrun();
  }
  if (imu.doubleTapped()) onCancel(CancelSource::Tap);
}

// ---------- USB serial commands ----------

void dumpSnapshot() {
  if (!snapshot_len) {
    Serial.println("no alert snapshot yet");
    return;
  }
  Serial.printf("# snapshot of %u samples before alert at t+%lus (52 Hz)\n", snapshot_len, snapshot_ms / 1000);
  Serial.println("i,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps");
  for (size_t i = 0; i < snapshot_len; i++) {
    const RawSample& r = snapshot[i];
    Serial.printf("%u,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f\n", i, r.a[0] * Imu::kAccelG, r.a[1] * Imu::kAccelG,
                  r.a[2] * Imu::kAccelG, r.g[0] * Imu::kGyroDps, r.g[1] * Imu::kGyroDps, r.g[2] * Imu::kGyroDps);
  }
}

void handleSerial() {
  static char line[32];
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') {
      if (len < sizeof(line) - 1) line[len++] = c;
      continue;
    }
    line[len] = 0;
    len = 0;
    if (!line[0]) continue;
    // "stream on|off" / "feat on|off" (a bare word toggles).
    if (!strncmp(line, "stream", 6)) {
      stream_raw = !strcmp(line, "stream on") ? true : !strcmp(line, "stream off") ? false : !stream_raw;
      Serial.printf("# raw stream %s (ms,ax,ay,az [g],gx,gy,gz [dps])\n", stream_raw ? "on" : "off");
    } else if (!strncmp(line, "feat", 4)) {
      stream_feat = !strcmp(line, "feat on") ? true : !strcmp(line, "feat off") ? false : !stream_feat;
      Serial.printf("# features %s\n", stream_feat ? "on" : "off");
    } else if (!strcmp(line, "status")) {
      readBattery();
      publishStatus();
    } else if (!strcmp(line, "test")) {
      sendTest();
    } else if (!strcmp(line, "cancel")) {
      onCancel(CancelSource::Phone);
    } else if (!strcmp(line, "ack")) {
      event_unacked = false;
      updateLedBase();
    } else if (!strcmp(line, "dump")) {
      dumpSnapshot();
    } else if (!strcmp(line, "log")) {
      for (size_t i = 0; i < event_log.size(); i++) {
        const LogEntry& l = event_log.at(i);
        Serial.printf("t+%lus %s conf %u%% %us\n", l.t_ms / 1000, eventName(l.e.type), l.e.confidence,
                      l.e.duration_s);
      }
    } else {
      Serial.println("commands: status stream feat test cancel ack dump log");
    }
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  wake_sem = xSemaphoreCreateBinary();

  // Microphone is not used: keep it unpowered.
  pinMode(PIN_PDM_PWR, OUTPUT);
  digitalWrite(PIN_PDM_PWR, LOW);
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);

  led::begin();
  ble::begin(wakeFromTask);
  startImu();
  attachInterrupt(PIN_LSM6DS3TR_C_INT1, wakeFromIsr, RISING);

  readBattery();
  last_battery_ms = millis();
  led::play(faults & FAULT_IMU_MISSING ? led::kFault : led::kBoot);
  updateLedBase();
  publishStatus();
}

void loop() {
  // Sleep until the IMU (or BLE) wakes us, at most 1 s.
  xSemaphoreTake(wake_sem, pdMS_TO_TICKS(1000));

  if (!(faults & FAULT_IMU_MISSING)) drainImu();

  if (millis() - last_data_ms > kStallMs) {
    faults |= FAULT_IMU_STALL;
    startImu();  // try to recover
  }

  switch (ble::takeCommand()) {
    case BleCommand::Cancel: onCancel(CancelSource::Phone); break;
    case BleCommand::Ack: event_unacked = false; break;
    case BleCommand::Test: sendTest(); break;
    case BleCommand::Status: publishStatus(); break;
    case BleCommand::None: break;
  }
  if (Serial) {
    handleSerial();
  } else {
    // USB host gone (unplugged or port closed): stop streaming so nothing piles up.
    stream_raw = stream_feat = false;
  }

  // Read the clock after the work above (it may have updated the timestamps).
  const uint32_t now = millis();
  if (now - last_battery_ms >= kBatteryEveryMs) {
    last_battery_ms = now;
    readBattery();
  }
  // Report faults as soon as they appear, otherwise every 30 s.
  if (faults != fault_shown || now - last_status_ms >= kStatusEveryMs) {
    publishStatus();
    faults &= ~(FAULT_FIFO_OVERRUN | FAULT_CLIPPING);  // transient: report once
    fault_shown = faults;
  }
  updateLedBase();

  // INT1 is level-style: if it is still high, go round again without sleeping.
  if (imu.intPinHigh()) xSemaphoreGive(wake_sem);
}
