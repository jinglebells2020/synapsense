# How SynapSense works: a code walkthrough

For the team (Enes, Ivan, Danish). Read it top to bottom, about 15 minutes. File paths are inside the
PlatformIO project.

## One second in the life of the watch

```
 sensor collects 52 samples (1 s)  ──INT1──►  main loop wakes up
                                                │
                     drainImu() reads them ◄────┘
                       │            │
          30 s ring buffer     FeatureExtractor  (looks at the last 4 s)
          (kept for "dump")          │
                                Detector  (is this a seizure?)
                                     │
                         LED  +  Bluetooth  +  USB serial
                                     │
                              loop goes back to sleep
```

The CPU works for a short moment each second and sleeps the rest of the time.

## The files, in reading order

| File | What it does | Difficulty |
|---|---|---|
| `src/main.cpp` | Ties everything together: `setup()`, `loop()`, events → LED and phone | Medium |
| `src/imu.h`, `src/imu.cpp` | Talks to the motion sensor (LSM6DS3TR-C) | Medium–hard |
| `lib/detector/sample_ring.h` | Ring buffer: keeps the newest N items | Easy |
| `lib/detector/features.h/.cpp` | Turns samples into numbers (strength, frequency, rhythm) | Medium |
| `lib/detector/fft.h/.cpp` | The FFT (standard textbook algorithm) | Hard, but you can treat it as a black box |
| `lib/detector/detector.h/.cpp` | Decides: rules + the alert state machine | Medium |
| `src/leds.h/.cpp` | LED blink patterns | Easy |
| `src/ble_link.h/.cpp` | Bluetooth service for the phone | Easy–medium (mostly library calls) |
| `src/commands.h` | Turns text like `cancel` into a command | Easy |
| `test/test_detector/` | Tests that run on the computer | Easy to read |

`.h` files list what exists. `.cpp` files have the code that does it.

## 1. Reading the sensor: `imu.cpp`

The motion sensor sits on its own I2C bus (`Wire1`, address `0x6A`). We set it up by writing numbers into its
registers. `begin()` does this once:

- **Power:** the sensor is powered from a GPIO pin. We turn it on and check its ID (`WHO_AM_I` must be `0x6A`).
- **Accelerometer:** ±8 g. It runs at 416 Hz inside the sensor, only so the double-tap detector can see sharp taps.
- **Gyroscope:** ±2000 °/s at 52 Hz, in normal (power-saving) mode.
- **FIFO:** the sensor's own memory. It stores accel + gyro samples at 52 Hz. When 52 samples (1 s) are waiting,
  it raises its **INT1** pin. That pin is our interrupt.
- **Double-tap:** the sensor detects double-taps itself and also raises INT1.

`readFifo()` empties the FIFO:
1. Ask how many 16-bit values are waiting.
2. Make sure we start at the beginning of a sample (each sample = 6 values: gyro x, y, z, then accel x, y, z).
3. Read 5 samples per I2C transfer (the Wire buffer holds 64 bytes) and turn each pair of bytes into a number.

The raw numbers are "counts". `kAccelG` and `kGyroDps` turn them into g and °/s.

## 2. Storing the data window: `main.cpp` + `sample_ring.h`

Every sample goes into `recent`, a **ring buffer** of 30 s (1560 samples). A ring buffer is a fixed array.
When it's full, the newest sample overwrites the oldest. So it always holds the last 30 s, and memory use never grows.

When an alert fires, the 30 s are copied into `snapshot`. The serial command `dump` prints them. That's the
"data window stored on the controller" from the brief.

## 3. Turning samples into numbers: `features.cpp`

`push()` takes one sample at a time. Every 52 samples (1 s) it looks at the **last 4 s** (208 samples) and
computes the features. The 4-second windows overlap, so we get a fresh answer every second.

Step by step:

1. **High-pass filter.** Removes anything constant: gravity (always about 1 g) and gyro drift. Only the motion is left.
2. **RMS** ("how strong"): the square root of the average squared value, over all 3 axes. Still wrist ≈ 0.01 g;
   a seizure-like shake ≈ 1–2.5 g and 400–850 °/s.
3. **RMS of the newest second.** Is it moving *right now*, not just somewhere in the last 4 s?
4. **FFT** ("which frequencies"): splits the motion into frequencies, from 0 to 26 Hz in bars about 0.2 Hz wide.
   Before the FFT we fade the window's edges in and out (a **Hann window**) to keep the bars clean. We do this for
   x, y and z and add the three results, so it doesn't matter how the watch sits on the wrist.
5. **Dominant frequency:** the tallest bar between 0.6 and 15 Hz.
6. **Rhythm:** what share of all the motion sits in that tallest bar (± 0.4 Hz) or at twice its frequency.
   Jerky shaking isn't a perfect sine wave, so part of it shows up at double the frequency.
   Regular shaking scores about 0.5–0.9; mixed, random movement scores lower. (Slow everyday movements can
   also look regular, but they fail the 2–8 Hz frequency test.)

All of this is done separately for the accelerometer and the gyroscope.

## 4. Deciding: `detector.cpp`

**A window is a "candidate"** when all of these are true:
- acceleration RMS ≥ 0.3 g and rotation RMS ≥ 100 °/s (strong)
- the newest second alone reaches 70% of those (still moving now)
- the dominant frequency is 2–8 Hz, for both sensors (seizure range; walking arm swing is about 1 Hz)
- rhythm ≥ 0.3, for both sensors (regular, not random)

The detector remembers the last 10 answers in a plain array (`was_candidate_`, newest first).

**The state machine:**

```
IDLE ──8 of last 10 are candidates──► PRE_ALERT ──10 s, no cancel──► ALERT ──15 s without shaking──► COOLDOWN
                                          │                            │                                  │
                                   cancel (tap or phone)         cancel (phone only)            60 s later
                                          └──────────────► COOLDOWN ◄──┘                                  ▼
                                                                                                        IDLE
```

- **PRE_ALERT:** amber LED and a phone warning. The wearer has 10 s to cancel.
- **No cancel means alert.** The person may be unconscious, so silence never stops the alert.
- **ALERT:** red LED and the phone alert. An update goes out every 30 s while the shaking continues.
- **Ended:** after 15 s without shaking. This uses looser thresholds (**hysteresis**), so a short pause doesn't end
  the event too early.
- **COOLDOWN:** 60 s without new alerts, so one seizure gives one alert.

**Cancel rules (safety):** a double-tap counts only during the pre-alert and only when the wrist is steady.
Violent jerks can look like taps, and a seizure must never cancel its own alert. A cancel from the phone always counts.

**Confidence** (shown in the alert) = 60% × (candidates out of 10) + 40% × (how clean the rhythm was).

## 5. Telling people: `leds.cpp`, `ble_link.cpp`, `commands.h`

**LEDs.** Each pattern is a list of steps (colour, milliseconds). A FreeRTOS **software timer** shows a step and
sets itself to fire when the step ends. So the CPU only wakes when the LED changes. There's a looping
*background* pattern (for example, a blue blip every 5 s) and *one-shot* patterns (for example, the long green
"cancelled") that return to the background when done.

**Bluetooth.** The watch is a BLE device called `SynapSense`. It offers one custom **service** with four
**characteristics** (think of them as text boxes):
- **Alert:** pre-alerts, alerts, updates, "ended", "cancelled"
- **Status:** state, battery, activity, faults
- **Live:** the features every second (for tuning)
- **Command:** the phone writes `cancel`, `ack`, `test` or `status` here

"Notify" means the watch pushes new text to the phone as soon as it changes. If an alert happens while no phone
is listening, it's sent again when a phone subscribes.

**Commands.** `parseCommand()` turns text into a command. The phone and the USB serial port use the same words.

## 6. The main loop: `main.cpp`

`setup()` runs once: serial, microphone off, LEDs, Bluetooth, sensor, interrupt, battery reading.

`loop()` runs forever:
1. **Sleep** until the sensor interrupt or Bluetooth wakes us, for at most 1 s. The interrupt handler only "gives" a
   **semaphore** (a wake-up token); the loop "takes" it. While waiting, FreeRTOS puts the chip to sleep.
2. Read the sensor (`drainImu()`). If no data arrives for 3 s, restart the sensor (fault: `imu-stall`).
3. Run any phone or serial command.
4. Read the battery once a minute; send a status every 30 s or when a fault appears.
5. Update the LED.
6. If INT1 is still high (more data waiting), go round again without sleeping.

**Faults** (sensor missing or stalled, FIFO overrun, clipping, low battery) go to the Status channel only,
never as seizure alerts.

## 7. How we know it works

- `pio test -e native` runs 18 tests on the computer. They feed **fake** motion through the same code:
  a seizure, walking, running, tooth brushing, clapping, waving, and the cancel rules.
- `tools/replay` runs a **real recording** from the watch through the same code on the computer.
  `make -C tools check` checks that wearing it gives no alert and the 48 s shake gives exactly one.
- The board and the replay give the same events, so the computer tests reflect what the watch does.

## Words you might be asked about

| Word | Meaning here |
|---|---|
| I2C | Two-wire bus the CPU uses to talk to the sensor |
| Register | A numbered setting inside the sensor (we write numbers to configure it) |
| FIFO | First-in-first-out memory inside the sensor; holds 1 s of samples for us |
| Interrupt / ISR | A pin signal that makes the CPU run a tiny function (the ISR) right away |
| Semaphore | A wake-up token between the ISR and the main loop |
| Tickless idle | FreeRTOS sleeps the chip when no task has work to do |
| RMS | Root mean square: the "typical size" of a signal |
| High-pass filter | Removes slow / constant parts (gravity, drift) |
| FFT | Fast Fourier Transform: splits a signal into its frequencies |
| Hann window | Fades the edges of the 4 s window before the FFT |
| Harmonic | A frequency at 2×, 3×… a base frequency |
| Ring buffer | Fixed-size buffer that overwrites its oldest item |
| State machine | A set of states with rules for moving between them |
| Hysteresis | Easier to stay in a state than to enter it (avoids flickering) |
| BLE service / characteristic | A group of values on a Bluetooth device / one value in it |
| Notify | The device pushes a new value to the phone without being asked |

## Questions you might get, with short answers

- **Why 52 Hz?** Seizure jerks are 2–8 Hz. To see 8 Hz you need more than 16 samples per second. 52 Hz is a sensor
  rate comfortably above that, and low rates save power.
- **Why a 4 s window every 1 s?** 4 s holds 8–32 jerks, enough for a clear frequency. Updating every second
  keeps the reaction fast.
- **Why both accelerometer and gyroscope?** Seizure jerks shake *and* rotate the wrist. Tooth brushing mostly shakes,
  walking mostly swings slowly. Requiring both cuts false alarms.
- **How do you avoid false alarms?** Several features must agree. It needs 8 of 10 seconds. The wearer can
  cancel a pre-alert.
- **What if the person can't cancel?** Silence means alert. Cancelling needs a deliberate action.
- **Why can't a tap cancel an alert that was already sent?** Jerks can look like taps. A seizure must never cancel
  its own alert. Only the phone can cancel then.
- **How is it power-efficient?** The sensor buffers and interrupts once a second; the CPU sleeps in between.
  The sensor detects taps in hardware. The gyro runs in power-saving mode, the microphone is off, LED and battery
  work runs on timers, and Bluetooth advertising slows down.
- **What's stored on the board?** The last 30 s of raw samples, always. A copy is frozen at each alert.
- **What if the phone isn't connected during an alert?** The LED still shows it, and the alert is sent when the
  phone reconnects and subscribes.
- **What can't it detect?** Seizures without strong shaking: absence, focal-aware, atonic. It also gives no
  warning before the shaking starts.
- **How did you test without real seizures?** Synthetic seizure signals in the tests, plus a real recording of a
  simulated convulsion (forearm shaking) and of normal wear.
