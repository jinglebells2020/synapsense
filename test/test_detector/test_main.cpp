// Host unit tests for the SynapSense detector:  pio test -e native
#include <stdio.h>
#include <unity.h>

#include <vector>

#include "detector.h"
#include "features.h"
#include "sample_ring.h"
#include "synth.h"

void setUp() {}
void tearDown() {}

struct Logged {
  float t;
  DetEvent e;
};

// Runs a generator through the full pipeline (features -> detector).
static std::vector<Logged> run(Gen gen, float seconds, Detector* det_out = nullptr,
                               bool print = false) {
  Synth s;
  FeatureExtractor fx;
  Detector local;
  Detector& det = det_out ? *det_out : local;
  std::vector<Logged> log;
  const int n = int(seconds * FeatureExtractor::kFs);
  for (int i = 0; i < n; i++) {
    const float t = i * kDt;
    if (!fx.push(gen(s, t))) continue;
    const Features& f = fx.features();
    const uint32_t now_ms = uint32_t(t * 1000.0f + 0.5f);
    DetEvent e = det.update(f, now_ms);
    if (print)
      printf("t=%5.1f a=%.2f g=%6.1f fa=%.2f fg=%.2f ra=%.2f rg=%.2f c=%d %s\n", t, f.acc_rms,
             f.gyr_rms, f.acc_dom_hz, f.gyr_dom_hz, f.acc_rhythm, f.gyr_rhythm,
             det.candidateCount(), stateName(det.state()));
    if (e.type != EventType::None) log.push_back({t, e});
  }
  return log;
}

static int count(const std::vector<Logged>& log, EventType t) {
  int n = 0;
  for (auto& l : log) n += l.e.type == t;
  return n;
}

static const Logged* first(const std::vector<Logged>& log, EventType t) {
  for (auto& l : log)
    if (l.e.type == t) return &l;
  return nullptr;
}

// ---------- building blocks ----------

void test_ring_buffer_keeps_newest() {
  SampleRing<int, 4> rb;
  for (int i = 1; i <= 6; i++) rb.push(i);
  TEST_ASSERT_EQUAL(4, rb.size());
  TEST_ASSERT_EQUAL(3, rb.at(0));
  TEST_ASSERT_EQUAL(6, rb.at(3));
  int out[4];
  TEST_ASSERT_EQUAL(4, rb.copyTo(out));
  TEST_ASSERT_EQUAL(3, out[0]);
}

void test_fft_finds_sine_bin() {
  Fft fft;
  float re[Fft::kN], im[Fft::kN];
  for (int i = 0; i < Fft::kN; i++) {
    re[i] = sinf(2 * kPi * 10 * i / Fft::kN);  // exactly bin 10
    im[i] = 0;
  }
  fft.forward(re, im);
  int peak = 1;
  for (int k = 1; k < Fft::kN / 2; k++)
    if (re[k] * re[k] + im[k] * im[k] > re[peak] * re[peak] + im[peak] * im[peak]) peak = k;
  TEST_ASSERT_EQUAL(10, peak);
}

void test_features_measure_4hz_rotation() {
  Synth s;
  FeatureExtractor fx;
  bool ready = false;
  for (int i = 0; i < 6 * FeatureExtractor::kFs; i++) {
    Sample x = s.base();
    const float w = 2 * kPi * 4.0f * i * kDt;
    x.gx += 200.0f * sinf(w);
    x.ay += 1.0f * sinf(w + 0.5f);
    ready = fx.push(x) || ready;
  }
  TEST_ASSERT_TRUE(ready);
  const Features& f = fx.features();
  TEST_ASSERT_FLOAT_WITHIN(0.25f, 4.0f, f.gyr_dom_hz);
  TEST_ASSERT_FLOAT_WITHIN(0.25f, 4.0f, f.acc_dom_hz);
  TEST_ASSERT_TRUE(f.gyr_rhythm > 0.8f);
  TEST_ASSERT_FLOAT_WITHIN(20.0f, 141.0f, f.gyr_rms);  // 200/sqrt(2)
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.71f, f.acc_rms);
}

void test_features_still_is_quiet() {
  Synth s;
  FeatureExtractor fx;
  for (int i = 0; i < 6 * FeatureExtractor::kFs; i++) fx.push(s.base());
  TEST_ASSERT_TRUE(fx.features().acc_rms < 0.03f);
  TEST_ASSERT_TRUE(fx.features().gyr_rms < 5.0f);
}

// ---------- end-to-end: the seizure must be caught ----------

void test_seizure_triggers_one_alert_then_ends() {
  SeizureModel m;
  auto log = run([&](Synth& s, float t) { return m.at(s, t); }, 150.0f);
  const Logged* pre = first(log, EventType::PreAlert);
  const Logged* alert = first(log, EventType::Alert);
  const Logged* ended = first(log, EventType::Ended);
  TEST_ASSERT_NOT_NULL(pre);
  TEST_ASSERT_NOT_NULL(alert);
  TEST_ASSERT_NOT_NULL(ended);
  printf("seizure: clonic %.0f-%.0f s, pre-alert %.0f s, alert %.0f s (conf %d%%, %.1f Hz), "
         "ended %.0f s (lasted %d s)\n",
         m.clonicStart(), m.clonicEnd(), pre->t, alert->t, alert->e.confidence,
         alert->e.dom_hz, ended->t, ended->e.duration_s);
  // Pre-alert within 15 s of clonic onset, full alert 10 s later.
  TEST_ASSERT_TRUE(pre->t > m.clonicStart());
  TEST_ASSERT_TRUE(pre->t < m.clonicStart() + 15.0f);
  TEST_ASSERT_FLOAT_WITHIN(1.5f, pre->t + 10.0f, alert->t);
  TEST_ASSERT_TRUE(alert->e.confidence >= 70);
  // Exactly one alert for one seizure.
  TEST_ASSERT_EQUAL(1, count(log, EventType::Alert));
  // Ends after the clonic phase stops, not before.
  TEST_ASSERT_TRUE(ended->t > m.clonicEnd());
  TEST_ASSERT_TRUE(ended->t < m.clonicEnd() + 25.0f);
}

void test_alert_fires_even_if_motion_stops_during_pre_alert() {
  SeizureModel m;
  m.clonic_s = 14.0f;  // short convulsion: stops right after the pre-alert
  auto log = run([&](Synth& s, float t) { return m.at(s, t); }, 90.0f);
  TEST_ASSERT_NOT_NULL(first(log, EventType::PreAlert));
  TEST_ASSERT_EQUAL(1, count(log, EventType::Alert));
}

// ---------- end-to-end: everyday motion must not alert ----------

static void expect_no_pre_alert(Gen g, float seconds, const char* name) {
  auto log = run(g, seconds);
  if (!log.empty()) printf("%s: unexpected %s at %.0f s\n", name, eventName(log[0].e.type), log[0].t);
  TEST_ASSERT_EQUAL_MESSAGE(0, count(log, EventType::PreAlert), name);
}

void test_no_alert_still() { expect_no_pre_alert(still(), 600, "still"); }
void test_no_alert_walking() { expect_no_pre_alert(walking(), 600, "walking"); }
void test_no_alert_fast_walking() { expect_no_pre_alert(walking(1.1f), 600, "fast walking"); }
void test_no_alert_running() { expect_no_pre_alert(running(), 300, "running"); }
void test_no_alert_brushing() { expect_no_pre_alert(brushing(), 180, "brushing"); }
void test_no_alert_impacts() { expect_no_pre_alert(impacts(), 300, "impacts"); }
void test_no_alert_waving() { expect_no_pre_alert(waving_bursts(), 300, "waving"); }

// ---------- state machine with hand-made feature windows ----------

static Features shaking() {
  Features f;
  f.acc_rms = f.acc_rms_1s = 0.8f;
  f.gyr_rms = f.gyr_rms_1s = 200;
  f.acc_dom_hz = f.gyr_dom_hz = 4.0f;
  f.acc_rhythm = f.gyr_rhythm = 0.7f;
  return f;
}
static Features quiet() {
  Features f;
  f.acc_rms = f.acc_rms_1s = 0.01f;
  f.gyr_rms = f.gyr_rms_1s = 2;
  f.acc_dom_hz = f.gyr_dom_hz = 1.0f;
  f.acc_rhythm = f.gyr_rhythm = 0.1f;
  return f;
}

// Feeds windows until the detector reaches PRE_ALERT. Returns time in ms.
static uint32_t drive_to_pre_alert(Detector& d) {
  uint32_t t = 0;
  for (int i = 0; i < 20; i++) {
    t += 1000;
    if (d.update(shaking(), t).type == EventType::PreAlert) return t;
  }
  TEST_FAIL_MESSAGE("never reached PRE_ALERT");
  return t;
}

void test_needs_8_of_10_windows() {
  Detector d;
  uint32_t t = 0;
  for (int i = 0; i < 7; i++) TEST_ASSERT_EQUAL(EventType::None, d.update(shaking(), t += 1000).type);
  TEST_ASSERT_EQUAL(EventType::PreAlert, d.update(shaking(), t += 1000).type);
}

void test_tap_cancel_when_steady() {
  Detector d;
  uint32_t t = drive_to_pre_alert(d);
  d.update(quiet(), t += 1000);  // wearer stops and holds still
  TEST_ASSERT_EQUAL(EventType::Cancelled, d.requestCancel(CancelSource::Tap, t).type);
  TEST_ASSERT_EQUAL(DetState::Cooldown, d.state());
}

void test_tap_cancel_rejected_while_shaking() {
  Detector d;
  uint32_t t = drive_to_pre_alert(d);
  d.update(shaking(), t += 1000);
  TEST_ASSERT_EQUAL(EventType::CancelRejected, d.requestCancel(CancelSource::Tap, t).type);
  TEST_ASSERT_EQUAL(DetState::PreAlert, d.state());
  // ...and the alert still goes out.
  EventType got = EventType::None;
  for (int i = 0; i < 12 && got != EventType::Alert; i++) got = d.update(shaking(), t += 1000).type;
  TEST_ASSERT_EQUAL(EventType::Alert, got);
}

void test_tap_cannot_cancel_a_sent_alert_but_phone_can() {
  Detector d;
  uint32_t t = drive_to_pre_alert(d);
  for (int i = 0; i < 10; i++) d.update(shaking(), t += 1000);
  TEST_ASSERT_EQUAL(DetState::Alert, d.state());
  d.update(quiet(), t += 1000);
  TEST_ASSERT_EQUAL(EventType::CancelRejected, d.requestCancel(CancelSource::Tap, t).type);
  TEST_ASSERT_EQUAL(EventType::Cancelled, d.requestCancel(CancelSource::Phone, t).type);
}

void test_no_repeat_alert_during_cooldown() {
  Detector d;
  uint32_t t = drive_to_pre_alert(d);
  int alerts = 0;
  // 10 s pre-alert, then 15 quiet s to end, then shaking again inside cooldown.
  for (int i = 0; i < 10; i++) alerts += d.update(shaking(), t += 1000).type == EventType::Alert;
  for (int i = 0; i < 15; i++) d.update(quiet(), t += 1000);
  TEST_ASSERT_EQUAL(DetState::Cooldown, d.state());
  for (int i = 0; i < 40; i++) {
    EventType e = d.update(shaking(), t += 1000).type;
    alerts += e == EventType::Alert;
    TEST_ASSERT_NOT_EQUAL(EventType::PreAlert, e);
  }
  TEST_ASSERT_EQUAL(1, alerts);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ring_buffer_keeps_newest);
  RUN_TEST(test_fft_finds_sine_bin);
  RUN_TEST(test_features_measure_4hz_rotation);
  RUN_TEST(test_features_still_is_quiet);
  RUN_TEST(test_seizure_triggers_one_alert_then_ends);
  RUN_TEST(test_alert_fires_even_if_motion_stops_during_pre_alert);
  RUN_TEST(test_no_alert_still);
  RUN_TEST(test_no_alert_walking);
  RUN_TEST(test_no_alert_fast_walking);
  RUN_TEST(test_no_alert_running);
  RUN_TEST(test_no_alert_brushing);
  RUN_TEST(test_no_alert_impacts);
  RUN_TEST(test_no_alert_waving);
  RUN_TEST(test_needs_8_of_10_windows);
  RUN_TEST(test_tap_cancel_when_steady);
  RUN_TEST(test_tap_cancel_rejected_while_shaking);
  RUN_TEST(test_tap_cannot_cancel_a_sent_alert_but_phone_can);
  RUN_TEST(test_no_repeat_alert_during_cooldown);
  return UNITY_END();
}
