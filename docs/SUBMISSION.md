# SynapSense: Embedded Challenge, Fall 2026

A wrist-worn prototype on the Seeed XIAO nRF52840 Sense. It detects the rhythmic jerking of a tonic–clonic
seizure and alerts a paired phone over Bluetooth.

**Team:** Enes Sissenbi, Ivan Ni, Danish Puri. **Educational prototype.**

- `SynapSense_OnePager.pdf`: the one-page description (deliverable 2)
- `SynapSense_PlatformIO/`: the PlatformIO project (deliverables 3–7)

The wearable itself (deliverable 1) is the physical device: XIAO and a 90 mAh LiPo in a 3D-printed case,
on a 19 mm strap with 20 mm spring bars.

## Where each deliverable is in the code

| # | Deliverable | Where (in `SynapSense_PlatformIO/`) |
|---|---|---|
| 3 | PlatformIO project | `platformio.ini`: board `seeed-xiao-afruitnrf52-nrf52840-sense`, Arduino framework |
| 4 | IMU data + stored window | `src/imu.cpp` reads the LSM6DS3TR-C through its FIFO (52 Hz, one interrupt per second). `src/main.cpp` `drainImu()` keeps the last 30 s in a ring buffer (`lib/detector/sample_ring.h`), saved as a snapshot at every alert |
| 5 | Classifier | `lib/detector/features.cpp` (filter, RMS, FFT, dominant frequency, rhythm) and `lib/detector/detector.cpp` (rules, 8-of-10 confirmation, pre-alert → alert → ended → cooldown) |
| 6 | Bluetooth to the phone | `src/ble_link.cpp`: Alert, Status, Live and Command characteristics plus the Battery Service, used with nRF Connect |
| 7 | Efficient use of the hardware | IMU FIFO + interrupt with the CPU asleep in between, the IMU's own double-tap detection, gyro in normal mode, microphone off, FFT once per second, LEDs driven by a FreeRTOS timer, slower BLE advertising after 30 s |

## Build and use

```bash
cd SynapSense_PlatformIO
pio run -t upload   # build and flash the watch
```

On the phone, in nRF Connect, connect to **SynapSense** and open service `7A3E0001-…`. Subscribe to **Alert**
and **Status**, shown as text. Write `test` to **Command** to get a test alert. The one-pager explains the LED
signals and how to cancel an alert.
