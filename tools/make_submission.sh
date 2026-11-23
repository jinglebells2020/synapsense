#!/usr/bin/env bash
# Collects everything for the Embedded Challenge submission into submission/SynapSense/
# and zips it as submission/SynapSense_submission.zip.
# Runs the tests and rebuilds the firmware first, so the folder matches the code.
#
#   tools/make_submission.sh
set -euo pipefail
cd "$(dirname "$0")/.."
PIO=${PIO:-$HOME/.platformio/penv/bin/pio}
UF2CONV=$HOME/.platformio/packages/framework-arduinoadafruitnrf52/tools/uf2conv/uf2conv.py
OUT=submission/SynapSense

echo "== tests"
"$PIO" test -e native | tail -1
make -s -C tools check | tail -1
echo "== firmware"
"$PIO" run -e xiao | tail -1

rm -rf submission
mkdir -p "$OUT"

# 1. Enclosure: print files, CAD source, renders, original models.
mkdir -p "$OUT/1_enclosure"
cp -R enclosure/README.md enclosure/*.py enclosure/original enclosure/out "$OUT/1_enclosure/"

# 2. One-page PDF and its LaTeX source.
mkdir -p "$OUT/2_one_pager/source"
cp docs/SynapSense_OnePager_IEEE.pdf "$OUT/2_one_pager/SynapSense_OnePager.pdf"
cp docs/ieee/synapsense_ieee.tex docs/ieee/fig_device_recording.pdf docs/ieee/make_figures.py "$OUT/2_one_pager/source/"

# 3-7. The PlatformIO project (sources only, no build output).
P="$OUT/3_platformio_project"
mkdir -p "$P/tools"
cp -R platformio.ini README.md src lib test recordings "$P/"
rm -rf "$P/recordings/raw"
cp tools/Makefile tools/*.cpp tools/*.py "$P/tools/"

# Ready-to-flash firmware.
mkdir -p "$OUT/firmware_build"
cp .pio/build/xiao/firmware.zip .pio/build/xiao/firmware.hex "$OUT/firmware_build/"
python3 "$UF2CONV" .pio/build/xiao/firmware.hex -c -f 0xADA52840 -o "$OUT/firmware_build/synapsense.uf2" >/dev/null

cp docs/SUBMISSION.md "$OUT/README.md"
cp docs/HOW_IT_WORKS.md "$OUT/HOW_IT_WORKS.md"
cp -R docs/photos "$OUT/photos"
echo "Built $(date '+%Y-%m-%d %H:%M') from commit $(git rev-parse --short HEAD)$(git diff --quiet HEAD -- . ':!submission' || echo ' (plus uncommitted changes)')." \
  > "$OUT/firmware_build/BUILD_INFO.txt"

find "$OUT" \( -name .DS_Store -o -name __pycache__ -o -name '*.pyc' \) -prune -exec rm -rf {} +
(cd submission && zip -qr SynapSense_submission.zip SynapSense)
echo "== done: $OUT ($(du -sh "$OUT" | cut -f1)), submission/SynapSense_submission.zip ($(du -h submission/SynapSense_submission.zip | cut -f1))"
