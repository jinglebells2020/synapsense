"""Figures for the IEEE-style one-pager:
  photo_top.jpg, photo_side.jpg  the watch on the wrist, cropped from docs/photos at full resolution
  fig_recording.pdf              a real wrist recording replayed through the firmware's detector

  make -C tools replay
  python docs/ieee/make_figures.py      (needs matplotlib and Pillow)
"""
import os
import re
import subprocess

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
COLUMN_IN = 3.45  # IEEE two-column width

# Photos: crop around the watch. Box = (left, top, right, bottom) in the 1500x2000 photos.
PHOTOS = {
    "photo_top.jpg": ("watch_on_wrist.jpg", (320, 680, 1200, 1300)),
    "photo_side.jpg": ("watch_side_usb_port.jpg", (440, 840, 1320, 1460)),
}
for name, (source, box) in PHOTOS.items():
    photo = Image.open(os.path.join(ROOT, "docs", "photos", source)).crop(box)
    photo.save(os.path.join(HERE, name), quality=92)

plt.rcParams.update({
    "font.family": "serif", "font.serif": ["Times New Roman", "Times"], "mathtext.fontset": "stix",
    "font.size": 6.5, "axes.linewidth": 0.5, "xtick.major.width": 0.5, "ytick.major.width": 0.5,
    "xtick.major.size": 2, "ytick.major.size": 2, "pdf.fonttype": 42,
})

# Detector output on the real recording: time, gyro RMS and state for every window.
rec = os.path.join(ROOT, "recordings", "2026-11-23_wear_then_30s_shake.csv")
out = subprocess.run([os.path.join(ROOT, "tools", "replay"), rec], capture_output=True, text=True, check=True).stdout
row = re.compile(r"^\s*([\d.]+)s a[\d.]+\([\d.]+\) g\s*(\d+)\(\s*\d+\).*?c\d+ (\w+)$")
t, gyro, state = [], [], []
for line in out.splitlines():
    m = row.match(line)
    if m:
        t.append(float(m[1]))
        gyro.append(int(m[2]))
        state.append(m[3])

fig, ax = plt.subplots(figsize=(COLUMN_IN, 0.9))

# Shade each run of PRE_ALERT / ALERT windows as one band (window i covers t-1 .. t).
for name, gray in (("PRE_ALERT", "0.86"), ("ALERT", "0.70")):
    start = None
    for ti, s in zip(t + [None], state + [None]):
        if s == name and start is None:
            start = ti - 1
        elif s != name and start is not None:
            ax.axvspan(start, prev, color=gray, lw=0)
            start = None
        prev = ti
ax.plot(t, gyro, color="black", lw=0.6)
ax.axhline(100, color="black", lw=0.5, ls=(0, (3, 2)))
ax.text(160, 165, "threshold 100$^\\circ$/s", ha="center", bbox=dict(fc="white", ec="none", pad=0.4))
ax.text(115, 430, "3.5 min of wearing and\nhandling: no candidates", ha="center")
ax.text(229, 640, "pre-alert\n+8 s", ha="right", va="center")
ax.text(298, 640, "alert\n+18 s", ha="left", va="center")
ax.text(6, 800, "(c)", fontsize=7.5, va="top")
ax.spines[["top", "right"]].set_visible(False)
ax.set_ylabel("gyro RMS ($^\\circ$/s)")
ax.set_xlabel("time (s)", labelpad=0.5)
ax.set_ylim(0, 900)
ax.set_xlim(0, 335)

fig.tight_layout(pad=0.15)
fig.savefig(os.path.join(HERE, "fig_recording.pdf"))
print(f"photo_top.jpg, photo_side.jpg, fig_recording.pdf: {len(t)} windows")
