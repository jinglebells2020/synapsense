"""Fig. 1 of the IEEE-style one-pager: (a) the printed case, (b) a real wrist
recording replayed through the firmware's detector. Times, grayscale-safe,
one column wide.

  make -C tools replay
  python docs/ieee/make_figures.py      (needs matplotlib)
"""
import os
import re
import subprocess

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
COLUMN_IN = 3.45  # IEEE two-column width

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

# The case render, cropped to its content.
img = plt.imread(os.path.join(ROOT, "docs", "onepager", "fig_case.png"))[:, :, :3]
ink = np.where(img.mean(axis=2) < 0.97)
img = img[ink[0].min():ink[0].max() + 1, ink[1].min():ink[1].max() + 1]

fig, (left, right) = plt.subplots(1, 2, figsize=(COLUMN_IN, 1.12), gridspec_kw={"width_ratios": [0.8, 2.6]})
left.imshow(img)
left.set_axis_off()

# Shade each run of PRE_ALERT / ALERT windows as one band (window i covers t-1 .. t).
for name, gray in (("PRE_ALERT", "0.86"), ("ALERT", "0.70")):
    start = None
    for ti, s in zip(t + [None], state + [None]):
        if s == name and start is None:
            start = ti - 1
        elif s != name and start is not None:
            right.axvspan(start, prev, color=gray, lw=0)
            start = None
        prev = ti
right.plot(t, gyro, color="black", lw=0.6)
right.axhline(100, color="black", lw=0.5, ls=(0, (3, 2)))
right.text(160, 165, "threshold 100$^\\circ$/s", ha="center", bbox=dict(fc="white", ec="none", pad=0.4))
right.text(115, 430, "3.5 min of wearing and\nhandling: no candidates", ha="center")
right.text(229, 640, "pre-alert\n+8 s", ha="right", va="center")
right.text(298, 640, "alert\n+18 s", ha="left", va="center")
right.spines[["top", "right"]].set_visible(False)
right.set_ylabel("gyro RMS ($^\\circ$/s)")
right.set_xlabel("time (s)", labelpad=0.5)
right.set_ylim(0, 900)
right.set_xlim(0, 335)

fig.tight_layout(pad=0.15, w_pad=0.4)
for ax, label in ((left, "(a)"), (right, "(b)")):  # sub-figure labels under each panel
    box = ax.get_position()
    fig.text(box.x0 + box.width / 2, 0.0, label, ha="center", va="bottom", fontsize=7.5)
fig.subplots_adjust(bottom=0.34)
fig.savefig(os.path.join(HERE, "fig_device_recording.pdf"))
print(f"fig_device_recording.pdf: {len(t)} windows")
