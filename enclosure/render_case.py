"""Preview renders for the SynapSense case: python render_case.py [outdir]"""
import os
import sys

import cadquery as cq

import synapsense_case as sc
from render import render_views

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
os.makedirs(out, exist_ok=True)

body, cover, guard, pusher, plug = sc.make_body(), sc.make_cover(), sc.make_guard(), sc.make_pusher(), sc.make_usb_plug()
xiao, batt = sc.make_xiao(), sc.make_battery()
C = {"body": "#8fb3d9", "cover": "#6b6b78", "guard": "#e3c74d", "pusher": "#d9534f", "plug": "#444444",
     "xiao": "#2e7d4f", "batt": "#b0b0b0"}

render_views([(body.val(), C["body"])], os.path.join(out, "preview_body.png"), "body (open side = wrist, prints this way up)")

asm = [(body.val(), C["body"]), (cover.val(), C["cover"]), (pusher.val(), C["pusher"]), (plug.val(), C["plug"])]
render_views(asm, os.path.join(out, "preview_assembled.png"), "assembled",
             views=[("top iso", 35, -60), ("wrist side", -40, -60), ("USB side", 10, 60), ("strap end", 5, 0)])

# Cutaways through the middle: keep one half of everything.
def half(w, keep):
    return w.intersect(keep).val()

for name, keep, views in (
        ("section_x0", sc.box(-30, 0, -30, 30, -5, 40), [("cut at x=0", 0, 0)]),
        ("section_y", sc.box(-30, 30, -30, sc.BOARD_YC, -5, 40), [("cut through the board", 0, 90)])):
    parts = [(half(w, keep), c) for w, c in ((body, C["body"]), (cover, C["cover"]), (guard, C["guard"]),
                                              (pusher, C["pusher"]), (plug, C["plug"]), (xiao, C["xiao"]),
                                              (batt, C["batt"]))]
    parts = [(s, c) for s, c in parts if s.Volume() > 0.01]
    render_views(parts, os.path.join(out, f"preview_{name}.png"), name, views=views)

# Exploded.
dz = {"cover": -10, "batt": -5, "guard": 0, "xiao": 6, "body": 16, "pusher": 26, "plug": 16}
ex = [(w.translate((0, 18 if k == "plug" else 0, dz[k])).val(), C[k]) for k, w in
      (("cover", cover), ("batt", batt), ("guard", guard), ("xiao", xiao), ("body", body), ("pusher", pusher),
       ("plug", plug))]
render_views(ex, os.path.join(out, "preview_exploded.png"), "exploded: cover, battery, pin guard, XIAO, body, pusher, USB plug",
             views=[("exploded", 15, -55)])
print("renders in", out)
