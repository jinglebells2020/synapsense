"""Figures for the one-page PDF.

  python make_figures.py   (needs matplotlib + cadquery; run tools/Makefile 'replay' first)
"""
import os
import re
import subprocess
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

# ---- 1. Real-wrist recording replayed through the firmware's detector
rec = os.path.join(ROOT, "recordings", "2026-11-23_wear_then_30s_shake.csv")
txt = subprocess.run([os.path.join(ROOT, "tools", "replay"), rec], capture_output=True, text=True).stdout
row = re.compile(r"^\s*([\d.]+)s a[\d.]+\([\d.]+\) g\s*(\d+)\(\s*\d+\).*?c(\d+) (\w+)$")
t, g, c, st = [], [], [], []
for line in txt.splitlines():
    m = row.match(line)
    if m:
        t.append(float(m[1]))
        g.append(int(m[2]))
        c.append(int(m[3]))
        st.append(m[4])

plt.rcParams.update({"font.family": "Helvetica", "font.size": 6.5, "axes.linewidth": 0.5})
fig, (a1, a2) = plt.subplots(2, 1, figsize=(3.55, 1.95), sharex=True, gridspec_kw={"height_ratios": [1.25, 1]})
for ax in (a1, a2):
    for state, color in (("PRE_ALERT", "#f2b134"), ("ALERT", "#e05252")):
        on = [s == state for s in st]
        for i in range(len(t)):
            if on[i]:
                ax.axvspan(t[i] - 1, t[i], color=color, alpha=0.28, lw=0)
    ax.spines[["top", "right"]].set_visible(False)
    ax.tick_params(width=0.5, length=2)
a1.plot(t, g, color="#2f5f8f", lw=0.8)
a1.axhline(100, color="#555", lw=0.5, ls="--")
a1.text(150, 125, "trigger 100 °/s", color="#555", fontsize=5.5, ha="center")
a1.set_ylabel("wrist rotation\nRMS (°/s)")
a1.set_ylim(0, 900)
a1.annotate("3.5 min of wearing and\nhandling: no candidates", xy=(110, 300), fontsize=5.8, ha="center")
a1.text(272, 790, "48 s\nshake", fontsize=5.8, ha="center", va="top")
a2.step(t, c, where="post", color="#2f5f8f", lw=0.8)
a2.axhline(8, color="#555", lw=0.5, ls="--")
a2.text(150, 8.5, "confirm: 8 of 10", color="#555", fontsize=5.5, ha="center")
a2.set_ylabel("candidate\nwindows")
a2.set_ylim(0, 11)
a2.set_xlabel("time (s)")
a2.set_xlim(0, 310)
a2.annotate("pre-alert\n+8 s", xy=(242, 4), xytext=(212, 4), fontsize=5.5, ha="center", va="center", color="#9a6a00", arrowprops=dict(arrowstyle="->", lw=0.5, color="#9a6a00"))
a2.text(266, 4.5, "alert +18 s\nconf 94%", fontsize=5.5, ha="center", va="center", color="#a02020")
fig.tight_layout(pad=0.3, h_pad=0.2)
fig.savefig(os.path.join(HERE, "fig_recording.png"), dpi=300)
print("fig_recording.png:", len(t), "windows")

# ---- 2. Case render
sys.path.insert(0, os.path.join(ROOT, "enclosure"))
import synapsense_case as sc  # noqa: E402
from render import render_views  # noqa: E402

parts = [(sc.make_body().val(), "#8fb3d9"), (sc.make_cover().val(), "#6b6b78"),
         (sc.make_pusher().val(), "#d9534f"), (sc.make_usb_plug().val(), "#444444")]
case_png = os.path.join(HERE, "fig_case.png")
render_views(parts, case_png, views=[("", 28, -58)])
from PIL import Image, ImageChops  # noqa: E402
im = Image.open(case_png).convert("RGB")
box = ImageChops.difference(im, Image.new("RGB", im.size, "white")).getbbox()
im.crop((box[0] - 6, box[1] - 6, box[2] + 6, box[3] + 6)).save(case_png)
print("fig_case.png")
