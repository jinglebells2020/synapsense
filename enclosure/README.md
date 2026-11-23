# SynapSense case

Parametric rebuild of the original SolidWorks parts in `original/` (those were mesh-derived
B-spline surfaces, so they couldn't be edited). Same idea: rounded body with swept lugs,
slide-in bottom cover, reset pusher, USB plug. Changes:

- **Strap:** 20.0 mm between the lugs for a 19 mm strap on 20 mm spring bars. Ø1.1 mm through-holes,
  bar centre 2 mm above the wrist side.
- **Fits the real hardware:** XIAO nRF52840 Sense with full-length headers pointing down, and the
  90 mAh LiPo (XIAO footprint, 4 mm) under the pin tips.
- **Pin guard plate** between the pins and the battery, so pins can't press into the LiPo pouch.
- **LED window** over the RGB/charge LEDs (the alert LED has to be visible).
- **USB wall is 1.2 mm** with a recess, so the cable plug seats fully.
- **Cover clicks shut** (detent), has a fingernail notch and "SynapSense" on the wrist side.
- **Prints without supports.** The fit check finds no overlaps between parts.

Size: 34.0 × 24.8 × 20.5 mm. It's tall because of the full-length pins. Trimming them to ~1.5 mm
(`HDR_PIN_BELOW = 1.5`, `HDR_PLASTIC = 0`) would make it ~14 mm.

## Check these before printing

The model uses estimates for the board. Measure yours and change the top of `synapsense_case.py`
if they differ:

| What | Value used | Parameter |
|---|---|---|
| PCB bottom → pin tips | 8.5 mm (2.5 strip + 6.0 pins) | `HDR_PLASTIC`, `HDR_PIN_BELOW` |
| XIAO total, top of USB-C → pin tips | **12.0 mm (measured)** | `XIAO_TOTAL` |
| Battery thickness | 4.0 mm | `BATT_T` |
| Reset button / LEDs, from board centre (USB up) | (−7.2, 8.2) / (6.8, 8.2) | `RESET_XY`, `LED_XY` |

A quick test: hold the printed body over the board and check that the LED shows through the window
and the pusher sits on the reset button.

## Print

| Part | File | Orientation | Notes |
|---|---|---|---|
| body | `out/body.stl` | open side down | PLA/PETG, 0.12–0.16 mm layers, 3+ walls. Ceiling is an 18.5 mm bridge. |
| bottom cover | `out/bottom_cover.stl` | text down | |
| pin guard | `out/pin_guard.stl` | flat | |
| reset pusher | `out/reset_pusher.stl` | foot down | |
| USB plug | `out/usb_plug.stl` | flange down | TPU is nicest, PLA works |

Spring-bar holes are small and horizontal. If the bar tips don't click in, open them with a 1.0–1.1 mm drill.

## Assemble

1. Drop the reset pusher into its slot from inside (foot down).
2. Put the XIAO in, USB-C against the thin wall. Route the battery wires up past the end of the pin rows.
3. Lay the pin guard on the pin tips, then the battery on the guard.
4. Slide the cover in from the back (the side opposite the USB) until it clicks.
5. Fit 20 mm spring bars through the strap ends and click them into the lug holes.

## Rebuild

```bash
python3 -m venv .venv && .venv/bin/pip install cadquery matplotlib
.venv/bin/python synapsense_case.py     # STEP + STL into out/, prints the fit check
.venv/bin/python render_case.py         # preview PNGs into out/
```
