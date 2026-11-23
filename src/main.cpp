// SynapSense firmware: wrist-worn tonic-clonic seizure detection prototype.
// Educational prototype, not a medical device.
//
//   IMU FIFO --INT1, 1/s--> drainImu() --> 30 s ring buffer (copied at each alert)
//                                      \-> FeatureExtractor (4 s window, every 1 s)
//                                           \-> Detector --> LED + BLE + USB serial
//
// USB serial (115200): status, test, cancel, ack, stream [on|off], feat [on|off], dump, log
#include <Arduino.h>

#include "ble_link.h"
#include "commands.h"
#include "detector.h"
#include "features.h"
#include "imu.h"
#include "leds.h"
#include "sample_ring.h"

namespace {
constexpr int kRecentSamples = 30 * FeatureExtractor::kFs;  // 30 s
constexpr uint32_t kImuStallMs = 3000;
constexpr uint32_t kBatteryEveryMs = 60000;
constexpr uint32_t kStatusEveryMs = 30000;

enum Fault : uint8_t {
  FAULT_IMU_MISSING = 1,
  FAULT_IMU_STALL = 2,
  FAULT_FIFO_OVERRUN = 4,  // reported once, then cleared
  FAULT_CLIPPING = 8,      // reported once, then cleared
  FAULT_LOW_BATTERY = 16,
};

Imu imu;
FeatureExtractor extractor;
Detector detector;
SampleRing<RawSample, kRecentSamples> recent;  // the last 30 s, always running
RawSample snapshot[kRecentSamples];            // copy of `recent` taken at the last alert
size_t snapshot_len = 0;
uint32_t snapshot_ms = 0;

struct LogEntry {
  uint32_t t_ms;
  DetEvent event;
};
SampleRing<LogEntry, 16> event_log;

SemaphoreHandle_t wake;  // given by the IMU interrupt and the BLE callbacks
uint8_t faults = 0;
uint8_t reported_faults = 0;
uint16_t alert_count = 0;
bool alert_unacknowledged = false;
bool stream_raw = false;
bool stream_features = false;
uint32_t last_imu_data_ms = 0;
uint32_t last_battery_ms = 0;
uint32_t last_status_ms = 0;
float battery_v = 0;
uint8_t battery_pct = 0;
Activity activity = Activity::Still;

void wakeFromIsr() {
  BaseType_t higher_priority_woken = pdFALSE;
  xSemaphoreGiveFromISR(wake, &higher_priority_woken);
  portYIELD_FROM_ISR(higher_priority_woken);
}
void wakeFromTask() { xSemaphoreGive(wake); }

Sample toSample(const RawSample& r) {
  return {r.a[0] * Imu::kAccelG, r.a[1] * Imu::kAccelG, r.a[2] * Imu::kAccelG,
          r.g[0] * Imu::kGyroDps, r.g[1] * Imu::kGyroDps, r.g[2] * Imu::kGyroDps};
}

// ---------- battery and status ----------

// Rough LiPo charge from its resting voltage.
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
  // 1M / 510k divider into P0.31. VBAT_ENABLE (P0.14) stays LOW all the time:
  // driving it HIGH would put the full battery voltage on the ADC pin.
  analogReference(AR_INTERNAL_2_4);
  analogReadResolution(12);
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(PIN_VBAT);
  analogReference(AR_DEFAULT);
  battery_v = (sum / 8.0f) / 4096.0f * 2.4f * (1510.0f / 510.0f);
  battery_pct = lipoPercent(battery_v);
  const bool low = battery_v > 2.5f && battery_v < 3.45f;  // below 2.5 V = no battery
  faults = low ? (faults | FAULT_LOW_BATTERY) : (faults & ~FAULT_LOW_BATTERY);
  ble::setBattery(battery_pct);
}

void publishStatus() {
  static const struct {
    uint8_t bit;
    const char* name;
  } kFaultNames[] = {{FAULT_IMU_MISSING, "imu-missing"}, {FAULT_IMU_STALL, "imu-stall"},
                     {FAULT_FIFO_OVERRUN, "overrun"},    {FAULT_CLIPPING, "clipping"},
                     {FAULT_LOW_BATTERY, "low-battery"}};
  char fault_text[64] = "none";
  size_t len = 0;
  for (const auto& f : kFaultNames)
    if ((faults & f.bit) && len < sizeof(fault_text))
      len += snprintf(fault_text + len, sizeof(fault_text) - len, "%s ", f.name);

  char status[128];
  snprintf(status, sizeof(status), "%s | bat %u%% %.2fV | %s | faults %s| up %lus", stateName(detector.state()),
           battery_pct, battery_v, activityName(activity), fault_text, millis() / 1000);
  ble::sendStatus(status);
  if (Serial) Serial.printf("STATUS %s\n", status);
  last_status_ms = millis();
}

// The background LED pattern shows the most important thing going on.
void updateLed() {
  const DetState state = detector.state();
  if (state == DetState::Alert) led::setBackground(led::kAlert);
  else if (state == DetState::PreAlert) led::setBackground(led::kPreAlert);
  else if (faults & (FAULT_IMU_MISSING | FAULT_IMU_STALL)) led::setBackground(led::kFault);
  else if (alert_unacknowledged) led::setBackground(led::kAlertUnacknowledged);
  else if (ble::connected()) led::setBackground(led::kPhoneConnected);
  else led::setBackground(led::kWaitingForPhone);
}

// ---------- detector events -> LED, phone, serial ----------

void handleEvent(const DetEvent& e, const char* cancelled_by) {
  if (e.type == EventType::None) return;
  if (e.type == EventType::CancelRejected) {
    // Not an alert: tell the wearer with the LED and note it on the status channel.
    led::play(led::kRejected);
    ble::sendStatus("Cancel ignored: hold the wrist still, then double-tap (pre-alert only)");
    if (Serial) Serial.println("EVENT CANCEL_REJECTED");
    return;
  }

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
      alert_unacknowledged = true;
      snprintf(msg, sizeof(msg), "ALERT #%u: possible tonic-clonic seizure. conf %u%%, %us, %.1f Hz, t+%lus",
               alert_count, e.confidence, e.duration_s, e.dom_hz, now / 1000);
      snapshot_len = recent.copyTo(snapshot);
      snapshot_ms = now;
      led::play(led::kAlert);
      ble::advertiseFast();
      break;
    case EventType::Update:
      snprintf(msg, sizeof(msg), "ONGOING #%u: %us so far, conf %u%%", alert_count, e.duration_s, e.confidence);
      break;
    case EventType::Ended:
      snprintf(msg, sizeof(msg), "ENDED #%u: shaking lasted %us. Check on the wearer.", alert_count, e.duration_s);
      break;
    case EventType::Cancelled:
      snprintf(msg, sizeof(msg), "CANCELLED by wearer (%s)", cancelled_by);
      alert_unacknowledged = false;
      led::play(led::kCancelled);
      break;
    default:
      return;
  }
  event_log.push({now, e});
  ble::sendAlert(msg);
  if (Serial) Serial.printf("EVENT %s\n", msg);
  updateLed();
}

void onFeatures(const Features& f, uint32_t now) {
  activity = detector.classify(f);
  handleEvent(detector.update(f, now), "");
  updateLed();

  char live[128];
  snprintf(live, sizeof(live), "a%.2fg g%.0fdps fa%.1f fg%.1f r%.2f/%.2f c%u/10 %s", f.acc_rms, f.gyr_rms,
           f.acc_dom_hz, f.gyr_dom_hz, f.acc_rhythm, f.gyr_rhythm, detector.candidateCount(),
           stateName(detector.state()));
  ble::sendLive(live);
  if (stream_features && Serial) Serial.printf("FEAT %s\n", live);
}

void cancel(CancelSource source) {
  const DetState state = detector.state();
  if (state == DetState::PreAlert || state == DetState::Alert) {
    handleEvent(detector.requestCancel(source, millis()), source == CancelSource::Tap ? "double-tap" : "phone");
  } else if (alert_unacknowledged) {
    // Outside an event, a cancel acknowledges the last alert.
    alert_unacknowledged = false;
    led::play(led::kCancelled);
    updateLed();
  }
}

void runCommand(Command command) {
  switch (command) {
    case Command::Cancel:
      cancel(CancelSource::Phone);
      break;
    case Command::Ack:
      alert_unacknowledged = false;
      updateLed();
      break;
    case Command::Test:
      ble::sendAlert("TEST ALERT: SynapSense link check, no event");
      led::play(led::kTest);
      if (Serial) Serial.println("EVENT TEST ALERT");
      break;
    case Command::Status:
      readBattery();
      publishStatus();
      break;
    case Command::None:
      break;
  }
}

// ---------- IMU ----------

void startImu() {
  if (imu.begin()) faults &= ~(FAULT_IMU_MISSING | FAULT_IMU_STALL);
  else faults |= FAULT_IMU_MISSING;
  extractor.reset();
  last_imu_data_ms = millis();
}

void drainImu() {
  static RawSample batch[64];
  int n;
  while ((n = imu.readFifo(batch, 64)) > 0) {
    const uint32_t now = millis();
    last_imu_data_ms = now;
    for (int i = 0; i < n; i++) {
      const RawSample& raw = batch[i];
      recent.push(raw);
      for (int axis = 0; axis < 3; axis++)
        if (abs(raw.a[axis]) > Imu::kClip) faults |= FAULT_CLIPPING;
      const Sample s = toSample(raw);
      if (stream_raw && Serial)
        Serial.printf("%lu,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f\n", now, s.ax, s.ay, s.az, s.gx, s.gy, s.gz);
      if (extractor.push(s)) onFeatures(extractor.features(), now);
    }
  }
  if (imu.takeOverrun()) faults |= FAULT_FIFO_OVERRUN;
  if (imu.doubleTapped()) cancel(CancelSource::Tap);
}

// ---------- USB serial ----------

void printSnapshot() {
  if (!snapshot_len) {
    Serial.println("no alert snapshot yet");
    return;
  }
  Serial.printf("# snapshot of %u samples before alert at t+%lus (52 Hz)\n", snapshot_len, snapshot_ms / 1000);
  Serial.println("i,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps");
  for (size_t i = 0; i < snapshot_len; i++) {
    const Sample s = toSample(snapshot[i]);
    Serial.printf("%u,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f\n", i, s.ax, s.ay, s.az, s.gx, s.gy, s.gz);
  }
}

void printLog() {
  for (size_t i = 0; i < event_log.size(); i++) {
    const LogEntry& entry = event_log.at(i);
    Serial.printf("t+%lus %s conf %u%% %us\n", entry.t_ms / 1000, eventName(entry.event.type),
                  entry.event.confidence, entry.event.duration_s);
  }
}

// "word on" / "word off" set the switch; the bare word toggles it.
bool onOff(const char* line, bool current) {
  const char* arg = strchr(line, ' ');
  if (arg && !strcmp(arg + 1, "on")) return true;
  if (arg && !strcmp(arg + 1, "off")) return false;
  return !current;
}

void handleSerial() {
  static char line[32];
  static uint8_t len = 0;
  while (Serial.available()) {
    const char c = Serial.read();
    if (c != '\n' && c != '\r') {
      if (len < sizeof(line) - 1) line[len++] = c;
      continue;
    }
    line[len] = 0;
    len = 0;
    if (!line[0]) continue;

    if (!strncmp(line, "stream", 6)) {
      stream_raw = onOff(line, stream_raw);
      Serial.printf("# raw stream %s (ms,ax,ay,az [g],gx,gy,gz [dps])\n", stream_raw ? "on" : "off");
    } else if (!strncmp(line, "feat", 4)) {
      stream_features = onOff(line, stream_features);
      Serial.printf("# features %s\n", stream_features ? "on" : "off");
    } else if (!strcmp(line, "dump")) {
      printSnapshot();
    } else if (!strcmp(line, "log")) {
      printLog();
    } else {
      const Command command = parseCommand(line);
      if (command == Command::None) Serial.println("commands: status test cancel ack stream feat dump log");
      runCommand(command);
    }
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  wake = xSemaphoreCreateBinary();

  pinMode(PIN_PDM_PWR, OUTPUT);  // microphone unused: keep it off
  digitalWrite(PIN_PDM_PWR, LOW);
  pinMode(VBAT_ENABLE, OUTPUT);  // see readBattery()
  digitalWrite(VBAT_ENABLE, LOW);

  led::begin();
  ble::begin(wakeFromTask);
  startImu();
  attachInterrupt(PIN_LSM6DS3TR_C_INT1, wakeFromIsr, RISING);

  readBattery();
  last_battery_ms = millis();
  led::play(faults & FAULT_IMU_MISSING ? led::kFault : led::kBoot);
  updateLed();
  publishStatus();
}

void loop() {
  xSemaphoreTake(wake, pdMS_TO_TICKS(1000));  // sleep until the IMU or BLE wakes us (max 1 s)

  if (!(faults & FAULT_IMU_MISSING)) drainImu();
  if (millis() - last_imu_data_ms > kImuStallMs) {  // no data for 3 s: restart the IMU
    faults |= FAULT_IMU_STALL;
    startImu();
  }

  runCommand(ble::takeCommand());
  if (Serial) handleSerial();
  else stream_raw = stream_features = false;  // USB gone: stop streaming

  // Read the clock after the work above: it may have updated the timestamps.
  const uint32_t now = millis();
  if (now - last_battery_ms >= kBatteryEveryMs) {
    last_battery_ms = now;
    readBattery();
  }
  if (faults != reported_faults || now - last_status_ms >= kStatusEveryMs) {
    publishStatus();
    faults &= ~(FAULT_FIFO_OVERRUN | FAULT_CLIPPING);
    reported_faults = faults;
  }
  updateLed();

  // INT1 stays high while data is waiting: if so, go round again without sleeping.
  if (imu.interruptPending()) xSemaphoreGive(wake);
}
