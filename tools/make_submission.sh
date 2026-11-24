#!/usr/bin/env bash
# Builds the submission: the one-page PDF, the PlatformIO project and a short README,
# in submission/SynapSense/ and zipped as submission/SynapSense_submission.zip.
# Runs the tests (kept in the repo, not submitted) and builds the firmware first,
# so the folder matches working code.
#
#   tools/make_submission.sh
set -euo pipefail
cd "$(dirname "$0")/.."
PIO=${PIO:-$HOME/.platformio/penv/bin/pio}
OUT=submission/SynapSense

echo "== tests"
"$PIO" test -e native | tail -1
make -s -C tools check | tail -1
echo "== firmware"
"$PIO" run -e xiao | tail -1

rm -rf submission
mkdir -p "$OUT/SynapSense_PlatformIO"
cp docs/SUBMISSION.md "$OUT/README.md"
cp docs/SynapSense_OnePager_IEEE.pdf "$OUT/SynapSense_OnePager.pdf"
cp -R src lib "$OUT/SynapSense_PlatformIO/"
# platformio.ini without the computer-only test setup (the tests stay in the repo).
awk '/^; Host build/{exit} !/^test_ignore/' platformio.ini | sed -e :a -e '/^\n*$/{$d;N;ba' -e '}' \
  > "$OUT/SynapSense_PlatformIO/platformio.ini"

find "$OUT" -name .DS_Store -delete
(cd submission && zip -qr SynapSense_submission.zip SynapSense)
echo "== done: $OUT ($(du -sh "$OUT" | cut -f1)), submission/SynapSense_submission.zip ($(du -h submission/SynapSense_submission.zip | cut -f1))"
