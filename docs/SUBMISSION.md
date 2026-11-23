# SynapSense: Embedded Challenge, Fall 2026

A wrist-worn prototype that detects the rhythmic jerking of a tonic–clonic seizure and alerts a paired
phone over Bluetooth. Built on the Seeed XIAO nRF52840 Sense.

**Team:** Enes Sissenbi, Ivan Ni, Danish Puri

**Educational prototype.**

## What's in this folder

| Folder | Contents |
|---|---|
| `1_enclosure/` | 3D-printed case: STL files to print (`out/`), STEP files, CAD source, renders, print + assembly guide |
| `2_one_pager/` | `SynapSense_OnePager.pdf` (IEEE style) and its LaTeX source |
| `3_platformio_project/` | The PlatformIO project: firmware, detector library, tests, tools, a real wrist recording |
| `firmware_build/` | Ready-to-flash firmware: `synapsense.uf2` (drag and drop), `firmware.zip` (DFU), `firmware.hex` |

## Where each deliverable is

| # | Deliverable | Where |
|---|---|---|
| 1 | Wearable with 3D-printed enclosure and strap (10) | `1_enclosure/`. Print `out/*.stl`. Holds the XIAO and a 90 mAh LiPo. Lugs are 20 mm apart for **20 mm spring bars and a 19 mm strap** (the brief suggests 22 mm straps). |
| 2 | One-page PDF (15) | `2_one_pager/SynapSense_OnePager.pdf` |
| 3 | PlatformIO project (5) | `3_platformio_project/platformio.ini`. Board `seeed-xiao-afruitnrf52-nrf52840-sense`, Arduino framework (Adafruit nRF52 core). |
| 4 | Read the IMU and store the data window in real time (20) | `src/imu.cpp`: LSM6DS3TR-C driver; accel + gyro go into the sensor's FIFO at 52 Hz and a watermark interrupt fires once per second. `src/main.cpp` `drainImu()`: burst-reads the FIFO into a 30 s ring buffer (`lib/detector/sample_ring.h`), frozen as a snapshot at every alert (serial `dump`). |
| 5 | Classifier / signal processing (20) | `lib/detector/features.cpp`: high-pass filter, RMS, 256-point FFT, dominant frequency and rhythm, every 1 s over 4 s. `lib/detector/detector.cpp`: candidate rule, 8-of-10 confirmation, pre-alert → alert → ended → cooldown, safe cancel rules. |
| 6 | Bluetooth notifications to a phone (20) | `src/ble_link.cpp`: custom service with Alert, Status, Live and Command text characteristics, plus the Battery Service. Works with nRF Connect (steps below). |
| 7 | Efficient use of the hardware (10) | See the list below. |

All paths in rows 3–7 are inside `3_platformio_project/`.

### Efficiency (deliverable 7)

- **Sensor FIFO + interrupt:** the IMU buffers 1 s of samples and wakes the CPU once per second. In between, the
  CPU sleeps (FreeRTOS tickless idle, waiting on a semaphore).
- **Hardware double-tap:** the IMU's own tap engine detects the cancel gesture (accel runs at 416 Hz inside the
  sensor; only 52 Hz goes into the FIFO).
- **Low-power settings:** gyro in normal (not high-performance) mode; microphone unpowered.
- **Cheap FFT:** two real axes share one complex FFT, so three axes cost two FFTs.
- **Timers, not polling:** LED patterns run on a FreeRTOS software timer that fires only when the pattern changes.
  The battery is read once a minute.
- **Bluetooth:** advertising runs every 100 ms for 30 s, then about once a second. Alerts missed while
  disconnected are delivered when the phone subscribes.
- **GPIO:** the IMU is powered from a GPIO pin set to high drive. The battery divider pin stays LOW, so the
  battery voltage can't reach the ADC pin.

## Quick start

**Flash** (no tools needed): double-press the XIAO's reset button. A USB drive appears. Drag
`firmware_build/synapsense.uf2` onto it. The board restarts with 3 green blinks.

Or from source: `cd 3_platformio_project && pio run -e xiao -t upload`

**Connect a phone:** in nRF Connect, connect to **SynapSense**. Open service `7A3E0001-…`. Subscribe to
**Alert** and **Status** and show them as text. Write `test` to **Command** to get a test alert.

**Try it:** wear it snug. Twist your forearm fast (about 4 times a second) without stopping. After about 8 s the
LED blinks amber (pre-alert). 10 s later it blinks red and the phone gets
`ALERT #1: possible tonic-clonic seizure`. To cancel a pre-alert: stop, hold still, then double-tap the watch.

The one-pager has the full LED table.

## Build and test from source

```bash
cd 3_platformio_project
pio test -e native          # 18 detector unit tests on the computer
make -C tools check         # real wrist recording: no alert while worn, 1 alert for the 48 s shake
pio run -e xiao             # build the firmware
python tools/monitor.py     # live features and events over USB (needs pyserial)
```

## Results

- 18 unit tests pass: synthetic seizures, walking, running, tooth brushing, clapping, waving, cancel rules.
- Real wrist recording (`recordings/`): 3.5 min of wearing and handling gave no candidate windows. A 48 s forearm
  shake gave a pre-alert after 8 s and one alert (confidence 94%) after 18 s. The board and the replay on a
  computer gave identical events.

## Hardware

Seeed XIAO nRF52840 Sense, 3.7 V 90 mAh LiPo (soldered to BAT+/BAT−), printed parts from `1_enclosure/`,
19 mm strap with 20 mm spring bars.
