"""SynapSense wrist case - parametric CadQuery model.

Rebuilt from the original SolidWorks/IGES parts in original/ (their surfaces are
mesh-derived B-splines, so they can't be edited cleanly). Same design language:
rounded body with swept lugs, slide-in bottom cover on rails, reset-button
pusher and USB plug - plus:

  * lugs for 19 mm straps on 20 mm spring bars (20.0 mm between the lugs,
    1.1 mm through-holes for the spring-bar tips)
  * stack sized for XIAO nRF52840 Sense with full-length headers pointing down
    and a 90 mAh LiPo (XIAO footprint, 4 mm thick) underneath
  * pin guard plate so the header pins can't press into the LiPo pouch
  * LED window over the RGB/charge LEDs, spiral on the lid
  * cover detent so it clicks shut; "SynapSense" on the wrist side

Frame: X along the strap, Y across the strap (USB side = +Y), Z up, z=0 = wrist.

  python synapsense_case.py          # writes out/*.step, out/*.stl, prints checks
"""
import math
import os

import cadquery as cq

# ---------------------------------------------------------------- parameters
# Electronics (measure yours: these are the numbers to check before printing)
PCB_W, PCB_L, PCB_T = 17.8, 21.0, 1.2   # XIAO board
XIAO_TOTAL = 12.0       # MEASURED: top of USB-C port -> pin tips
HDR_PLASTIC = 2.5       # black header strip
HDR_PIN_BELOW = 6.0     # pin length below the strip
XIAO_TOP = XIAO_TOTAL - HDR_PLASTIC - HDR_PIN_BELOW  # PCB bottom -> top of USB-C (3.5)
USB_OVERHANG = 0.6      # USB-C mouth sticks out past the board edge
USB_Z_ABOVE_PCB_BOT = XIAO_TOP - 3.26 / 2  # USB-C centre: half a shell below its top
RESET_XY = (-7.2, 8.2)  # reset button, relative to board centre (from the original)
RESET_TOP = 2.7         # PCB bottom -> top of reset button (estimate; pusher leaves room)
LED_XY = (6.8, 8.2)     # RGB + charge LEDs, opposite side of the USB
PUSHER_FOOT = 0.6       # pusher foot height under the lid
PUSHER_STICK_OUT = 1.6  # stem above the lid: enough travel even if the button sits low
BATT_T = 4.0            # LiPo thickness (footprint = XIAO)

# Strap
LUG_GAP = 20.0          # spring-bar length / space between lugs (19 mm strap)
BAR_HOLE_D = 1.1        # spring-bar tip hole (tips are ~0.8-1.0 mm)
BAR_X, BAR_Z = 15.0, 2.0  # spring-bar centre
LUG_TIP_R = 2.0

# Case
CLR = 0.35              # board/battery clearance per side
CX = 9.25               # cavity half-width (X)
CY0, CY1 = -10.8, 11.2  # cavity Y range (+Y end touches the USB mouth)
WALL_X = 2.2            # end walls (hold the cover rails)
WALL_Y_BACK = 1.6
WALL_Y_USB = 1.2        # thin at the USB so the plug seats fully
LID = 1.3
TOP_CLR = 0.3
R_VERT, R_TOP = 2.5, 2.0

# Bottom cover (slides in from -Y on rails in the end walls)
LIP = 0.6               # wall lip under each rail
COVER_T = 1.2
RAIL_CLR = 0.2
GROOVE_D = 0.9          # rail depth into the end walls
GUARD_T = 0.8           # pin guard plate
BATT_CLR = 0.3

# ---------------------------------------------------------------- derived stack
GROOVE_Z0 = LIP
GROOVE_Z1 = LIP + COVER_T + RAIL_CLR
COVER_TOP = LIP + COVER_T
BATT_Z0 = COVER_TOP
GUARD_Z0 = BATT_Z0 + BATT_T + BATT_CLR
PIN_TIP_Z = GUARD_Z0 + GUARD_T
PCB_Z0 = PIN_TIP_Z + HDR_PIN_BELOW + HDR_PLASTIC
CEIL = PCB_Z0 + XIAO_TOP + TOP_CLR
H = CEIL + LID
BX = CX + WALL_X
BY0, BY1 = CY0 - WALL_Y_BACK, CY1 + WALL_Y_USB
BOARD_YC = CY1 - USB_OVERHANG - 0.1 - PCB_L / 2  # board centre Y
USB_Z = PCB_Z0 + USB_Z_ABOVE_PCB_BOT


def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))


def lug(sign_x, sign_y):
    """Swept lug: quarter-ellipse from the lid edge down to a round tip around the bar.
    Built once for +X/+Y and mirrored (building the -Y one directly gave an invalid solid)."""
    y0, y1 = LUG_GAP / 2, BY1
    a = BAR_X + LUG_TIP_R - (BX - 1)
    b = H - BAR_Z
    ell = [((BX - 1) + a * math.cos(t), BAR_Z + b * math.sin(t))
           for t in [i * (math.pi / 2) / 16 for i in range(1, 17)]]
    c45 = math.sqrt(0.5)
    prof = (cq.Workplane("XZ").moveTo(BX - 1, 0).lineTo(BAR_X, 0)
            .threePointArc((BAR_X + LUG_TIP_R * c45, BAR_Z - LUG_TIP_R * c45), (BAR_X + LUG_TIP_R, BAR_Z))
            .spline(ell, includeCurrent=True).close())
    s = prof.extrude(y1 - y0)
    bb = s.val().BoundingBox()
    s = s.translate((0, y0 - bb.ymin, 0))
    try:
        s = s.edges("not |Y").fillet(0.5)
    except Exception:
        pass
    if sign_x < 0:
        s = s.mirror("YZ")
    if sign_y < 0:
        s = s.mirror("XZ")
    return s


def spiral_cut(cx, cy, z_top, depth=0.4, r0=0.8, r1=6.3, turns=2.25, width=0.7):
    n = 160
    pitch = (r1 - r0) / turns
    outer, inner = [], []
    for i in range(n + 1):
        t = i / n * turns * 2 * math.pi
        r = r0 + pitch * t / (2 * math.pi)
        c, s = math.cos(t), math.sin(t)
        outer.append((cx + (r + width / 2) * c, cy + (r + width / 2) * s))
        inner.append((cx + (r - width / 2) * c, cy + (r - width / 2) * s))
    pts = outer + inner[::-1]
    return cq.Workplane("XY").workplane(offset=z_top - depth).polyline(pts).close().extrude(depth + 1)


def make_body():
    b = (cq.Workplane("XY").box(2 * BX, BY1 - BY0, H, centered=(True, False, False)).translate((0, BY0, 0))
         .edges("|Z").fillet(R_VERT).faces(">Z").edges().fillet(R_TOP))
    assert abs(BY0 + BY1) < 1e-6, "lugs are mirrored, so the body must be symmetric in Y"
    for sx in (1, -1):
        for sy in (1, -1):
            b = b.union(lug(sx, sy))
    # Inside: open at the bottom.
    cav = box(-CX, CX, CY0, CY1, -1, CEIL).edges("|Z").fillet(0.5)
    b = b.cut(cav)
    # Cover rails in the end walls, open through the -Y wall, plus the slot for the cover's pad.
    b = b.cut(box(-(CX + GROOVE_D), CX + GROOVE_D, BY0 - 1, CY1, GROOVE_Z0, GROOVE_Z1))
    b = b.cut(box(-CX, CX, BY0 - 1, CY0 + 0.5, -1, GROOVE_Z0 + 0.01))
    # Detent dimples in the rail ceilings.
    for sx in (1, -1):
        b = b.cut(cq.Workplane("XY").circle(0.65).extrude(0.4).translate((sx * (CX + 0.45), -8.5, GROOVE_Z1)))
    # USB-C opening (plug body is ~8.4 x 2.6) with a shallow outer recess for the overmold edge.
    usb = cq.Workplane("XZ").slot2D(9.0, 3.4).extrude(-(WALL_Y_USB + 2)).translate((0, CY1 - 0.5, USB_Z))
    b = b.cut(usb)
    rec = cq.Workplane("XZ").slot2D(10.2, 4.4).extrude(-1).translate((0, BY1 - 0.4, USB_Z))
    b = b.cut(rec)
    # Lid: reset pusher slot and LED window.
    rx, ry = RESET_XY[0], BOARD_YC + RESET_XY[1]
    b = b.cut(box(rx - 0.95, rx + 0.95, ry - 1.6, ry + 1.6, CEIL - 0.5, H + 1))
    lx, ly = LED_XY[0], BOARD_YC + LED_XY[1]
    b = b.cut(cq.Workplane("XY").slot2D(4.6, 2.2, angle=90).extrude(LID + 2).translate((lx, ly, CEIL - 0.5)))
    b = b.cut(spiral_cut(0, -3.0, H))
    # Spring-bar holes, straight through both lugs.
    for sx in (1, -1):
        b = b.cut(cq.Workplane("XZ").circle(BAR_HOLE_D / 2).extrude(-40, both=True).translate((sx * BAR_X, 0, BAR_Z)))
    return b


def make_cover():
    plate = box(-(CX + GROOVE_D - 0.1), CX + GROOVE_D - 0.1, BY0, CY1 - 0.1, LIP, COVER_TOP)
    pad = box(-(CX - 0.15), CX - 0.15, BY0, CY1 - 0.1, 0, LIP + 0.01).edges("|Z").fillet(0.6)
    c = plate.union(pad)
    for sx in (1, -1):  # detent bumps (0.1 mm interference while sliding, click into the dimples)
        bump = cq.Workplane("XY").sphere(0.6).translate((sx * (CX + 0.45), -8.5, COVER_TOP + 0.3 - 0.6))
        lim = CX + GROOVE_D - 0.1  # stay inside the rail, only stick up
        c = c.union(bump.intersect(box(-lim, lim, -20, 20, COVER_TOP - 0.05, COVER_TOP + 0.3)))
    c = c.cut(box(-3, 3, BY0 - 1, BY0 + 0.8, -1, 0.5))  # fingernail notch
    try:  # name on the wrist side, readable from outside
        txt = (cq.Workplane(cq.Plane(origin=(0, -0.5, 0), xDir=(-1, 0, 0), normal=(0, 0, -1)))
               .text("SynapSense", 2.4, -0.35, combine=False, halign="center", valign="center"))
        c = c.cut(txt)
    except Exception as e:
        print("text skipped:", e)
    return c


def make_guard():
    # Covers the header pins only (y +/-9.4); the gaps at both ends leave room for the battery wires.
    return box(-(CX - CLR), CX - CLR, BOARD_YC - 9.4, BOARD_YC + 9.4, GUARD_Z0, GUARD_Z0 + GUARD_T).edges("|Z").fillet(0.5)


def make_pusher():
    rx, ry = RESET_XY[0], BOARD_YC + RESET_XY[1]
    foot = box(rx - 1.5, rx + 1.5, ry - 2.0, ry + 2.0, CEIL - PUSHER_FOOT, CEIL)
    stem = box(rx - 0.75, rx + 0.75, ry - 1.4, ry + 1.4, CEIL - 0.01, H + PUSHER_STICK_OUT).edges("|Z").fillet(0.3)
    return foot.union(stem)


def make_usb_plug():
    y_in = BY1 - WALL_Y_USB + 0.2
    plug = cq.Workplane("XZ").slot2D(8.8, 3.2).extrude(-(BY1 - y_in)).translate((0, y_in, USB_Z))
    flange = cq.Workplane("XZ").slot2D(10.6, 4.2).extrude(-0.8).translate((0, BY1 + 0.05, USB_Z))
    tab = box(-2.0, 2.0, BY1 + 0.05, BY1 + 0.85, USB_Z - 4.6, USB_Z - 1.0)
    return plug.union(flange).union(tab)


# ---- dummy electronics, only for fit checks / renders
def make_xiao():
    yc = BOARD_YC
    pcb = box(-PCB_W / 2, PCB_W / 2, yc - PCB_L / 2, yc + PCB_L / 2, PCB_Z0, PCB_Z0 + PCB_T)
    usb = box(-4.47, 4.47, yc + PCB_L / 2 + USB_OVERHANG - 7.35, yc + PCB_L / 2 + USB_OVERHANG,
              PCB_Z0 + PCB_T, PCB_Z0 + XIAO_TOP)
    rst = box(RESET_XY[0] - 1.3, RESET_XY[0] + 1.3, yc + RESET_XY[1] - 1.0, yc + RESET_XY[1] + 1.0,
              PCB_Z0 + PCB_T, PCB_Z0 + RESET_TOP)
    x = pcb.union(usb).union(rst)
    for sx in (1, -1):
        px = sx * 7.62
        x = x.union(box(px - 1.27, px + 1.27, yc - 8.89, yc + 8.89, PCB_Z0 - HDR_PLASTIC, PCB_Z0))
        for k in range(7):
            py = yc - 7.62 + 2.54 * k
            x = x.union(box(px - 0.32, px + 0.32, py - 0.32, py + 0.32, PIN_TIP_Z, PCB_Z0 - HDR_PLASTIC))
    return x


def make_battery():
    return box(-PCB_W / 2, PCB_W / 2, BOARD_YC - PCB_L / 2, BOARD_YC + PCB_L / 2, BATT_Z0, BATT_Z0 + BATT_T)


def check(parts):
    names = list(parts)
    bad = 0
    for i in range(len(names)):
        for j in range(i + 1, len(names)):
            a, b = parts[names[i]].val(), parts[names[j]].val()
            v = a.intersect(b).Volume()
            if v > 0.05:
                print(f"  OVERLAP {names[i]} x {names[j]}: {v:.2f} mm3")
                bad += 1
    print("  no overlaps" if not bad else f"  {bad} overlaps")
    return bad


if __name__ == "__main__":
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
    os.makedirs(out, exist_ok=True)
    body, cover, guard, pusher, plug = make_body(), make_cover(), make_guard(), make_pusher(), make_usb_plug()
    xiao, batt = make_xiao(), make_battery()

    bb = body.val().BoundingBox()
    print(f"body {bb.xlen:.1f} x {bb.ylen:.1f} x {bb.zlen:.1f} mm (X along strap)")
    print(f"stack: cover top {COVER_TOP:.2f}, battery {BATT_Z0:.2f}-{BATT_Z0 + BATT_T:.2f}, guard {GUARD_Z0:.2f}, "
          f"pin tips {PIN_TIP_Z:.2f}, PCB {PCB_Z0:.2f}, ceiling {CEIL:.2f}, top {H:.2f}")
    print(f"lug gap {LUG_GAP:.1f} mm, spring bar at x=+/-{BAR_X}, z={BAR_Z}, USB centre z={USB_Z:.2f}")
    gap = CEIL - PUSHER_FOOT - (PCB_Z0 + RESET_TOP)
    print(f"reset pusher: {gap:.2f} mm free above the button, {PUSHER_STICK_OUT} mm of stem to press")
    print("fit check (assembled):")
    check({"body": body, "cover": cover, "guard": guard, "pusher": pusher, "usb_plug": plug,
           "xiao": xiao, "battery": batt})

    # Print orientation: everything flat on the bed, z=0 down.
    def to_bed(w):
        b2 = w.val().BoundingBox()
        return w.translate((-(b2.xmin + b2.xmax) / 2, -(b2.ymin + b2.ymax) / 2, -b2.zmin))

    printable = {
        "body": to_bed(body),                                  # open side down, no supports
        "bottom_cover": to_bed(cover),                         # text side down
        "pin_guard": to_bed(guard),
        "reset_pusher": to_bed(pusher),                        # foot down
        "usb_plug": to_bed(plug.rotate((0, 0, 0), (1, 0, 0), -90)),  # flange down
    }
    for name, w in printable.items():
        cq.exporters.export(w, os.path.join(out, f"{name}.stl"), tolerance=0.01, angularTolerance=0.1)
        cq.exporters.export(w, os.path.join(out, f"{name}.step"))
    asm = (cq.Assembly(name="synapsense")
           .add(body, name="body", color=cq.Color(0.55, 0.7, 0.85))
           .add(cover, name="bottom_cover", color=cq.Color(0.4, 0.4, 0.45))
           .add(guard, name="pin_guard", color=cq.Color(0.9, 0.8, 0.3))
           .add(pusher, name="reset_pusher", color=cq.Color(0.9, 0.3, 0.3))
           .add(plug, name="usb_plug", color=cq.Color(0.3, 0.3, 0.3)))
    asm.save(os.path.join(out, "assembly.step"))
    print("wrote", ", ".join(sorted(os.listdir(out))))
