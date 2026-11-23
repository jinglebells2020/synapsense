# SynapSense

Wrist-worn tonic–clonic seizure detection prototype on the Seeed XIAO nRF52840 Sense.
Embedded Challenge, Fall 2026. **Educational prototype, not a medical device.**

## What it does

The watch watches wrist motion with its built-in accelerometer + gyroscope. When it sees
**strong, regular 2–8 Hz shaking with wrist rotation for about 8–10 seconds** (the clonic
jerking of a convulsive seizure), it:

1. **Pre-alert** – LED blinks amber, the phone gets a warning. The wearer has 10 s to cancel by
   holding the wrist still and **double-tapping** the watch.
2. **Alert** – if nobody cancels: LED blinks red fast, the phone gets
   `ALERT #n: possible tonic-clonic seizure. conf %, duration, frequency`. Updates every 30 s.
3. **Ended** – after 15 s without rhythmic shaking: `ENDED #n: shaking lasted Ns`.
   Then a 60 s cooldown, so one seizure gives one alert.

It does **not** detect absence, focal-aware or other low-motion seizures, and it can be fooled
by vigorous rhythmic activity (that is what the pre-alert cancel is for).

## How it works

| Stage | Where |
|---|---|
| IMU sampled at 52 Hz into the sensor's own FIFO; INT1 wakes the CPU once a second; CPU sleeps in between | `src/imu.cpp` |
| Last 30 s of raw samples kept in a RAM ring buffer; frozen into a snapshot at each alert | `src/main.cpp` |
| Every 1 s, over the last 4 s: RMS, peak-to-peak, jerk, dominant frequency and rhythm (FFT) for accel and gyro | `lib/detector/features.cpp` |
| Candidate = strong + rhythmic 2–8 Hz in both accel and gyro, and still moving in the newest second | `lib/detector/detector.cpp` |
| Confirm: 8 of the last 10 windows → pre-alert → alert → end → cooldown | `lib/detector/detector.cpp` |
| BLE GATT service with text notifications + Battery Service | `src/ble_link.cpp` |
| LED patterns on a software timer | `src/leds.cpp` |

Thresholds live in `DetectorConfig` in `lib/detector/detector.h`.

Safety rules built in:
- A double-tap only cancels during the **pre-alert** and only when the wrist is **steady**.
  Clonic jerks can look like taps, and a seizure must never cancel its own alert.
- No cancel → the alert goes out anyway (the wearer may be unable to respond).
- Device faults (IMU missing or stalled, FIFO overrun, clipping, low battery) go on the
  Status channel, never as seizure alerts.

## LED

| LED | Meaning |
|---|---|
| 3 green blinks | booted OK |
| blue blip every 5 s | on, waiting for a phone |
| green blip every 5 s | on, phone connected |
| amber blinking | pre-alert: double-tap (wrist still) to cancel |
| fast red blinking | alert |
| red blip every 3 s | an alert happened and was not acknowledged (double-tap or `ack`) |
| long green | cancelled / acknowledged |
| 3 quick red | cancel ignored (wrist still moving) |
| purple double blink | sensor fault |

## Phone (nRF Connect)

1. Open nRF Connect → Scanner → connect to **SynapSense**.
2. Open the service `7A3E0001-…`. Tap the triple-arrow (subscribe) on **Alert**, **Status**
   and, for tuning, **Live features**. Switch the value display to text (UTF-8).
3. Write a text command to **Command**: `test` (sends a test alert), `cancel`, `ack`, `status`.

## Build, flash, test

```bash
pio run -e xiao -t upload        # build + flash (export LANG=en_US.UTF-8 if nrfutil complains)
pio test -e native               # detector unit tests on the Mac (synthetic seizure, walking, ...)
python tools/monitor.py          # live features + events over USB
python tools/monitor.py --raw --seconds 60 --out recordings/brushing.csv   # record for tuning
make -C tools replay && tools/replay recordings/brushing.csv      # same detector, on the Mac
make -C tools check              # real recording: wearing it → no alert, 48 s shake → 1 alert
```

Real-wrist result (2026-11-23, `recordings/2026-11-23_wear_then_30s_shake.csv`): 3.5 min of
strapping it on and moving around gave zero candidate windows. A 48 s forearm shake (4–6 Hz,
1–2.5 g, 400–850 °/s) gave a pre-alert after 8 s, the alert 10 s later, one alert in total, and
"ended, lasted 48 s". The board and the Mac replay produced identical events.

USB serial commands (115200 baud): `status stream feat test cancel ack dump log`.
`dump` prints the 30 s snapshot saved at the last alert.
