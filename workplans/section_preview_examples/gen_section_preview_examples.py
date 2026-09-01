#!/usr/bin/env python3
"""Generate example SVG mockups for the section/profile preview panels.

These are review mockups for SECTION_PREVIEW_PANELS_WORKPLAN.md. The final
implementation renders with QPainter, sampling swmm_xsect_width_of_depth();
here the same sampling idea is emulated in Python so the geometry is honest.

Run:  python3 gen_section_preview_examples.py   (writes *.svg next to itself)
"""

import math
import os

OUT = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------- palette ---
INK      = "#2c3e50"   # primary linework
DIM      = "#1a6fb5"   # dimension lines + text
DIM_LT   = "#7fa8cc"   # extension lines
PIPE     = "#d9e9f7"   # pipe interior fill
WALL     = "#b7c4cf"   # pipe wall fill
CONC     = "#e9e6e1"   # structure concrete
SOIL     = "#c9b99a"   # native soil fill
GRND     = "#8a7357"   # ground hatch stroke
WATER    = "#a8cdec"
GREEN    = "#7fae6e"
MUTED    = "#6b7a88"   # secondary text
BG       = "#fdfdfc"

FONT = 'font-family="Helvetica,Arial,sans-serif"'


def text(x, y, s, size=11, color=INK, anchor="middle", weight="normal", angle=None):
    tr = f' transform="rotate({angle} {x} {y})"' if angle is not None else ""
    return (f'<text x="{x:.1f}" y="{y:.1f}" {FONT} font-size="{size}" '
            f'fill="{color}" text-anchor="{anchor}" font-weight="{weight}"{tr}>{s}</text>')


def line(x1, y1, x2, y2, color=INK, w=1.2, dash=None):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    return (f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" '
            f'stroke="{color}" stroke-width="{w}"{d}/>')


def arrow(x, y, angle, color=DIM, size=7):
    """Filled arrowhead pointing along `angle` (deg, 0 = +x)."""
    a = math.radians(angle)
    pts = []
    for dx, dy in [(0, 0), (-size, size * 0.35), (-size, -size * 0.35)]:
        px = x + dx * math.cos(a) - dy * math.sin(a)
        py = y + dx * math.sin(a) + dy * math.cos(a)
        pts.append(f"{px:.1f},{py:.1f}")
    return f'<polygon points="{" ".join(pts)}" fill="{color}"/>'


def vdim(x, y1, y2, label, side=1, ext_from=None, size=10):
    """Vertical dimension line at x between y1<y2; label rotated, ext lines optional."""
    s = []
    if ext_from is not None:
        for yy in (y1, y2):
            s.append(line(ext_from, yy, x + 4 * side, yy, DIM_LT, 0.7))
    s.append(line(x, y1, x, y2, DIM, 1.0))
    s.append(arrow(x, y1, -90))
    s.append(arrow(x, y2, 90))
    s.append(text(x - 4 * side if side > 0 else x + 10, (y1 + y2) / 2, label,
                  size, DIM, "middle", angle=-90))
    return "\n".join(s)


def hdim(y, x1, x2, label, ext_from=None, above=True, size=10):
    s = []
    if ext_from is not None:
        for xx in (x1, x2):
            s.append(line(xx, ext_from, xx, y + (4 if above else -4), DIM_LT, 0.7))
    s.append(line(x1, y, x2, y, DIM, 1.0))
    s.append(arrow(x1, y, 180))
    s.append(arrow(x2, y, 0))
    s.append(text((x1 + x2) / 2, y - 5 if above else y + 13, label, size, DIM))
    return "\n".join(s)


def leader(x1, y1, x2, y2, label, size=10, color=DIM, anchor=None):
    """Leader line from feature (x1,y1) with elbow to label at (x2,y2)."""
    a = anchor or ("start" if x2 >= x1 else "end")
    elbow = x2 + (-6 if a == "start" else 6)
    s = [line(x1, y1, elbow, y2, color, 0.8),
         line(elbow, y2, x2, y2, color, 0.8),
         f'<circle cx="{x1:.1f}" cy="{y1:.1f}" r="2" fill="{color}"/>',
         text(x2 + (3 if a == "start" else -3), y2 + 3.5, label, size, color, a)]
    return "\n".join(s)


def hatch_ground(x1, x2, y, spacing=9, ln=8, color=GRND):
    s = [line(x1, y, x2, y, color, 1.4)]
    xx = x1 + 3
    while xx < x2:
        s.append(line(xx, y, xx - ln * 0.6, y + ln, color, 0.8))
        xx += spacing
    return "\n".join(s)


def svg(w, h, body, title):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" '
            f'viewBox="0 0 {w} {h}">\n'
            f'<rect width="{w}" height="{h}" fill="{BG}" stroke="#d8dde2"/>\n'
            f'{body}\n'
            f'<text x="10" y="{h-8}" {FONT} font-size="9" fill="{MUTED}">{title}</text>\n'
            f'</svg>\n')


def polypts(pts):
    return " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)


# ------------------------------------------------------- shape half-widths ---
def egg_halfwidth(y, H):
    """Standard egg (H:B = 3:2), y measured up from invert, three-arc construction.
    Mirrors what swmm_xsect_width_of_depth() returns for EGG."""
    r = H / 3.0
    if y <= 0 or y >= H:
        return 0.0
    if y < 0.2 * r:
        return math.sqrt(max(0.0, (0.5 * r) ** 2 - (y - 0.5 * r) ** 2))
    if y <= 2 * r:
        return math.sqrt(9 * r * r - (y - 2 * r) ** 2) - 2 * r
    return math.sqrt(max(0.0, r * r - (y - 2 * r) ** 2))


def circ_halfwidth(y, D):
    r = D / 2.0
    return math.sqrt(max(0.0, r * r - (y - r) ** 2))


def outline_from_halfwidth(fn, H, cx, y_invert, scale, n=72):
    """Closed outline polygon by sampling half-width over depth (engine-style)."""
    right = []
    for i in range(n + 1):
        y = H * i / n
        hw = fn(y)
        right.append((cx + hw * scale, y_invert - y * scale))
    left = [(2 * cx - x, y) for (x, y) in reversed(right)]
    return right + left


# ============================================================ Example 1 =====
def ex1_conduit_xsection():
    W, H = 400, 320
    b = []
    b.append(text(14, 22, "C-12  —  Cross-Section", 12.5, INK, "start", "bold"))
    b.append(text(14, 38, "CIRCULAR", 10, MUTED, "start"))
    # toggle mock (cross-section | profile)
    b.append(f'<rect x="270" y="12" width="58" height="20" rx="4" fill="{DIM}"/>')
    b.append(f'<rect x="330" y="12" width="58" height="20" rx="4" fill="none" stroke="#c3ccd4"/>')
    b.append(text(299, 26, "Section", 9.5, "#ffffff"))
    b.append(text(359, 26, "Profile", 9.5, MUTED))

    cx, cy = 175, 180          # pipe centre
    D = 1.2                    # m
    scale = 95                 # px per m
    r = D / 2 * scale
    wall = 10
    b.append(f'<circle cx="{cx}" cy="{cy}" r="{r+wall}" fill="{WALL}" stroke="{INK}" stroke-width="1.4"/>')
    b.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{PIPE}" stroke="{INK}" stroke-width="1.4"/>')
    # centrelines
    b.append(line(cx - r - 18, cy, cx + r + 18, cy, MUTED, 0.7, "8 3 2 3"))
    b.append(line(cx, cy - r - 18, cx, cy + r + 18, MUTED, 0.7, "8 3 2 3"))
    # diameter dimension (vertical, right side)
    b.append(vdim(cx + r + wall + 34, cy - r, cy + r, "D = 1.200 m", side=-1,
                  ext_from=cx + r * 0.05))
    # invert + crown leaders
    b.append(leader(cx, cy + r, cx - 95, cy + r + 34, "Invert  El. 101.25 m", anchor="start"))
    b.append(leader(cx, cy - r, cx - 95, cy - r - 30, "Crown  El. 102.45 m", anchor="start"))
    # footer: engine-computed properties
    b.append(line(14, 278, 386, 278, "#d8dde2", 1))
    b.append(text(14, 294, "A 1.131 m²      Rh 0.300 m      Wmax 1.200 m      Barrels 1",
                  9.5, MUTED, "start"))
    return svg(W, H, "\n".join(b),
               "Mockup 1 — property-grid preview panel: conduit, Section view")


# ============================================================ Example 2 =====
def ex2_conduit_profile():
    W, H = 470, 334
    b = []
    b.append(text(14, 22, "C-12  —  Profile", 12.5, INK, "start", "bold"))
    b.append(text(14, 38, "J-04  →  J-05", 10, MUTED, "start"))
    b.append(f'<rect x="340" y="12" width="58" height="20" rx="4" fill="none" stroke="#c3ccd4"/>')
    b.append(f'<rect x="400" y="12" width="58" height="20" rx="4" fill="{DIM}"/>')
    b.append(text(369, 26, "Section", 9.5, MUTED))
    b.append(text(429, 26, "Profile", 9.5, "#ffffff"))

    # geometry (px)
    mw = 34                      # manhole width
    ux, dx_ = 70, 400            # manhole centrelines
    rimU, rimD = 92, 118         # rim y
    invU, invD = 218, 248        # node invert y
    D = 26                       # pipe depth in px (~0.6m)
    off = 12                     # upstream inlet offset px
    pipeUy = invU - off          # pipe invert at upstream (offset above node invert)
    pipeDy = invD                # at downstream

    # ground + structures
    b.append(hatch_ground(18, ux - mw / 2, rimU))
    b.append(hatch_ground(ux + mw / 2, dx_ - mw / 2,  (rimU + rimD) / 2))
    b.append(hatch_ground(dx_ + mw / 2, 452, rimD))
    for cxm, rim, inv in ((ux, rimU, invU), (dx_, rimD, invD)):
        b.append(f'<rect x="{cxm-mw/2}" y="{rim}" width="{mw}" height="{inv-rim+8}" '
                 f'fill="{CONC}" stroke="{INK}" stroke-width="1.3"/>')
    # pipe polygon (crown + invert lines)
    p = [(ux + mw / 2, pipeUy - D), (dx_ - mw / 2, pipeDy - D),
         (dx_ - mw / 2, pipeDy), (ux + mw / 2, pipeUy)]
    b.append(f'<polygon points="{polypts(p)}" fill="{PIPE}" stroke="{INK}" stroke-width="1.3"/>')
    # labels
    b.append(text(ux, rimU - 26, "J-04", 10, INK, "middle", "bold"))
    b.append(text(dx_, rimD - 26, "J-05", 10, INK, "middle", "bold"))
    b.append(leader(ux + 2, rimU, ux + 46, rimU - 14, "Rim 104.60"))
    b.append(leader(dx_ - 2, rimD, dx_ - 60, rimD - 16, "Rim 103.90", anchor="end"))
    b.append(leader(ux + mw / 2, pipeUy, ux + 74, pipeUy + 40, "Inv 101.55"))
    b.append(leader(dx_ - mw / 2, pipeDy, dx_ - 80, pipeDy + 26, "Inv 101.25", anchor="end"))
    b.append(leader(ux + mw / 2 + 60, pipeUy - D + 4, ux + 130, pipeUy - D - 22, "Crown 102.15"))
    # upstream inlet offset dimension
    b.append(vdim(ux - mw / 2 - 16, pipeUy, invU, "offset 0.30", side=1,
                  ext_from=ux + mw / 2))
    # node invert leader
    b.append(leader(ux, invU + 6, ux - 8, invU + 34, "Node inv 101.25", anchor="start"))
    # length / slope along pipe
    ang = math.degrees(math.atan2(pipeDy - pipeUy, dx_ - ux))
    b.append(text((ux + dx_) / 2, (pipeUy + pipeDy) / 2 - D - 10,
                  "L = 120.0 m      S = 0.25 %", 10, DIM, "middle", angle=ang))
    b.append(line(14, 292, 456, 292, "#d8dde2", 1))
    b.append(text(14, 306, "CIRCULAR  D 0.600 m      n 0.013      Qfull 0.42 m³/s",
                  9.5, MUTED, "start"))
    return svg(W, H, "\n".join(b),
               "Mockup 2 — property-grid preview panel: conduit, Profile view")


# ============================================================ Example 3 =====
def ex3_node_profile():
    W, H = 480, 340
    b = []
    b.append(text(14, 22, "J-04  —  Node Profile", 12.5, INK, "start", "bold"))
    b.append(text(14, 38, "JUNCTION · 3 connecting links", 10, MUTED, "start"))

    cx = 200
    mw = 56
    rim, inv = 84, 268
    wall = 7
    # ground
    b.append(hatch_ground(18, cx - mw / 2 - wall, rim))
    b.append(hatch_ground(cx + mw / 2 + wall, 380, rim))
    # manhole: walls + chamber
    b.append(f'<rect x="{cx-mw/2-wall}" y="{rim}" width="{mw+2*wall}" height="{inv-rim+wall}" '
             f'fill="{CONC}" stroke="{INK}" stroke-width="1.3"/>')
    b.append(f'<rect x="{cx-mw/2}" y="{rim+4}" width="{mw}" height="{inv-rim-4}" '
             f'fill="{PIPE}" stroke="{INK}" stroke-width="1.1"/>')
    # connecting pipes: (side, invert-y, depth-px, id, invert-label)
    pipes = [(-1, 268, 24, "C-03 in", "Inv 101.25"),
             (-1, 196, 18, "C-08 in", "Inv 102.15"),
             (+1, 262, 26, "C-12 out", "Inv 101.32")]
    for side, py, d, pid, ilab in pipes:
        x0 = cx + side * (mw / 2 + wall)
        x1 = x0 + side * 74
        b.append(f'<rect x="{min(x0,x1):.1f}" y="{py-d}" width="{abs(x1-x0):.1f}" height="{d}" '
                 f'fill="{PIPE}" stroke="{INK}" stroke-width="1.2"/>')
        lx = x1 + side * 2
        anc = "end" if side < 0 else "start"
        b.append(text(lx, py - d - 6, pid, 9.5, INK, anc, "bold"))
        b.append(text(lx, py + 12, ilab, 9, DIM, anc))
        # crown tick
        b.append(line(x0, py - d, x0 + side * 8, py - d, DIM, 0.8))
    # rim / invert leaders
    b.append(leader(cx - mw / 2 - wall, rim, cx - 132, rim - 16, "Rim  El. 104.60 m", anchor="start"))
    b.append(leader(cx, inv, cx + 34, inv + 30, "Invert  El. 101.25 m"))
    # max depth dimension
    b.append(vdim(cx + mw / 2 + wall + 96, rim, inv, "Max depth 3.35 m", side=-1,
                  ext_from=cx + mw / 2 + wall))
    # surcharge depth marker
    b.append(line(cx - mw / 2, 128, cx + mw / 2, 128, DIM, 0.9, "5 3"))
    b.append(text(cx, 122, "Psurch +1.0", 8.5, DIM))

    # plan-view inset
    ix, iy, ir = 408, 96, 40
    b.append(f'<circle cx="{ix}" cy="{iy}" r="{ir+9}" fill="#f3f6f9" stroke="#c3ccd4"/>')
    b.append(f'<circle cx="{ix}" cy="{iy}" r="9" fill="{CONC}" stroke="{INK}"/>')
    for ang, pid, inbound in ((205, "C-03", True), (120, "C-08", True), (350, "C-12", False)):
        a = math.radians(ang)
        x2, y2 = ix + ir * math.cos(a), iy - ir * math.sin(a)
        b.append(line(ix + 9 * math.cos(a), iy - 9 * math.sin(a), x2, y2, INK, 1.6))
        if inbound:
            b.append(arrow(ix + 16 * math.cos(a), iy - 16 * math.sin(a),
                           math.degrees(math.atan2(math.sin(a), -math.cos(a))) + 180, INK, 6))
        else:
            b.append(arrow(x2, y2, -ang, INK, 6))
        lx, ly = ix + (ir + 14) * math.cos(a), iy - (ir + 14) * math.sin(a)
        b.append(text(lx, ly + 3, pid, 8.5, MUTED))
    b.append(text(ix, iy + ir + 24, "plan", 8.5, MUTED))
    b.append(line(ix, iy - ir - 2, ix, iy - ir - 14, INK, 1.0))
    b.append(arrow(ix, iy - ir - 14, -90, INK, 5))
    b.append(text(ix + 7, iy - ir - 8, "N", 8, INK, "start"))
    return svg(W, H, "\n".join(b),
               "Mockup 3 — property-grid preview panel: junction with connecting pipes + plan inset")


# ============================================================ Example 4 =====
def ex4_xsect_editor_egg():
    W, H = 510, 400
    b = []
    b.append(text(14, 22, "Cross-Section Editor — live preview", 12.5, INK, "start", "bold"))
    b.append(text(14, 38, "EGG-SHAPED", 10, MUTED, "start"))

    Hm = 1.5                       # max depth m
    scale = 170
    cx, y_inv = 205, 330
    r = Hm / 3
    outline = outline_from_halfwidth(lambda y: egg_halfwidth(y, Hm), Hm, cx, y_inv,
                                     scale, n=160)
    b.append(f'<polygon points="{polypts(outline)}" fill="{PIPE}" stroke="{INK}" stroke-width="2.2"/>')
    # sampled depth ticks (hint that outline comes from engine sampling)
    for frac in (0.25, 0.5, 0.75):
        y = y_inv - frac * Hm * scale
        hw = egg_halfwidth(frac * Hm, Hm) * scale
        b.append(line(cx - hw, y, cx + hw, y, MUTED, 0.6, "3 4"))
    # dims
    b.append(vdim(cx + 118, y_inv - Hm * scale, y_inv, "Max depth 1.500 m", side=-1,
                  ext_from=cx + 20))
    ymax = y_inv - 2 * r * scale   # widest at y = 2r
    b.append(hdim(y_inv - Hm * scale - 26, cx - r * scale, cx + r * scale,
                  "Wmax = 1.000 m", ext_from=ymax, above=True))
    b.append(leader(cx, y_inv, cx - 76, y_inv + 22, "invert", anchor="start"))
    # engine-property readout
    b.append(f'<rect x="362" y="196" width="134" height="112" rx="5" fill="#f3f6f9" stroke="#c3ccd4"/>')
    b.append(text(370, 214, "swmm_xsect_*", 9, MUTED, "start"))
    for i, (k, v) in enumerate([("A full", "1.149 m²"), ("Rh full", "0.289 m"),
                                ("W max", "1.000 m"), ("y crit @1 m³/s", "0.87 m"),
                                ("Open?", "no")]):
        b.append(text(370, 234 + i * 15, k, 9.5, INK, "start"))
        b.append(text(488, 234 + i * 15, v, 9.5, DIM, "end"))
    b.append(text(14, 372, "Outline = polygon of swmm_xsect_width_of_depth() samples · dashed ticks at ¼, ½, ¾ depth",
                  9, MUTED, "start"))
    return svg(W, H, "\n".join(b),
               "Mockup 4 — cross-section editor preview pane (replaces static SVG icons)")


# ============================================================ Example 5 =====
def ex5_lid_bioretention():
    W, H = 530, 420
    b = []
    b.append(text(14, 22, "LID Editor — Bio-Retention Cell", 12.5, INK, "start", "bold"))
    b.append(text(14, 38, "layer diagram tracks the active tab and values", 10, MUTED, "start"))

    x0, x1 = 60, 330
    y = 78
    layers = [("Surface", 46, "#dff0d8", "Storage depth 150 mm · n 0.10"),
              ("Soil", 92, "#d8c49a", "Thickness 500 mm · por 0.45 · K 25 mm/h"),
              ("Storage", 66, "#cfd6dd", "Thickness 300 mm · void 0.60"),
              ("Native soil", 44, SOIL, "Seepage 5 mm/h")]
    ys = {}
    yy = y
    for name, hgt, fill, _ in layers:
        ys[name] = (yy, yy + hgt)
        b.append(f'<rect x="{x0}" y="{yy}" width="{x1-x0}" height="{hgt}" '
                 f'fill="{fill}" stroke="{INK}" stroke-width="1.1"/>')
        yy += hgt

    # vegetation on surface
    for vx in range(x0 + 12, x1 - 6, 24):
        t, btm = ys["Surface"]
        b.append(line(vx, t, vx, t - 12, GREEN, 1.6))
        b.append(line(vx, t - 6, vx - 5, t - 12, GREEN, 1.2))
        b.append(line(vx, t - 6, vx + 5, t - 12, GREEN, 1.2))
    # ponded water line in surface layer
    t, btm = ys["Surface"]
    b.append(line(x0, t + 14, x1, t + 14, DIM, 0.9, "5 3"))
    b.append(text(x1 - 6, t + 11, "berm", 8.5, DIM, "end"))
    # soil speckle
    t, btm = ys["Soil"]
    for i in range(40):
        px = x0 + 8 + (i * 53) % (x1 - x0 - 16)
        py = t + 8 + (i * 29) % (btm - t - 14)
        b.append(f'<circle cx="{px}" cy="{py}" r="1.1" fill="#9a835a"/>')
    # gravel in storage
    t, btm = ys["Storage"]
    for i in range(26):
        px = x0 + 12 + (i * 41) % (x1 - x0 - 24)
        py = t + 10 + (i * 17) % (btm - t - 18)
        b.append(f'<circle cx="{px}" cy="{py}" r="4" fill="none" stroke="#8b97a3" stroke-width="1"/>')
    # underdrain
    dcy = (t + btm) / 2 + 8
    b.append(f'<circle cx="{(x0+x1)/2}" cy="{dcy}" r="9" fill="{BG}" stroke="{INK}" stroke-width="1.3"/>')
    b.append(leader((x0 + x1) / 2 + 6, dcy + 6, x1 + 24, dcy + 30,
                    "Underdrain · offset 75 mm"))
    # inflow / outflow arrows
    st, sb = ys["Surface"]
    b.append(line(x0 - 28, st - 14, x0 - 6, st + 8, DIM, 1.6))
    b.append(arrow(x0 - 6, st + 8, 45, DIM, 8))
    b.append(text(x0 - 46, st - 20, "runoff in", 9, DIM, "start"))
    # thickness dimensions, right side
    for name in ("Surface", "Soil", "Storage"):
        t_, b_ = ys[name]
        lab = {"Surface": "150 mm", "Soil": "500 mm", "Storage": "300 mm"}[name]
        b.append(vdim(x1 + 20, t_, b_, lab, side=-1, ext_from=x1))
    # layer name labels, left
    for name, _, _, params in layers:
        t_, b_ = ys[name]
        b.append(text(x0 - 10, (t_ + b_) / 2 + 3, name, 10, INK, "end", "bold"))
    # active-tab highlight (Soil)
    t_, b_ = ys["Soil"]
    b.append(f'<rect x="{x0-2}" y="{t_-2}" width="{x1-x0+4}" height="{b_-t_+4}" '
             f'fill="none" stroke="{DIM}" stroke-width="2" stroke-dasharray="6 3"/>')
    b.append(text(x1 + 96, (t_ + b_) / 2 - 18, "active tab:", 9, MUTED))
    b.append(text(x1 + 96, (t_ + b_) / 2 - 5, "Soil", 10, DIM, "middle", "bold"))
    # params footer
    b.append(line(14, 386, 516, 386, "#d8dde2", 1))
    b.append(text(14, 400, " · ".join(p for _, _, _, p in layers[:3]), 8.6, MUTED, "start"))
    return svg(W, H, "\n".join(b),
               "Mockup 5 — LID editor layer diagram (per-type layer stack, active tab highlighted)")


# ============================================================ Example 6 =====
def ex6_icon_strip():
    W, H = 470, 152
    b = [text(14, 22, "Procedural shape icons (replace static SVGs)", 12.5, INK, "start", "bold")]
    cell = 86
    shapes = [("CIRCULAR", lambda y: circ_halfwidth(y, 1.0), 1.0),
              ("EGG", lambda y: egg_halfwidth(y, 1.0), 1.0),
              ("RECT_CLOSED", lambda y: 0.35 if 0 < y < 1 else 0.0, 1.0),
              ("TRIANGULAR", lambda y: 0.5 * y if y < 1 else 0.0, 1.0),
              ("TRAPEZOIDAL", lambda y: 0.22 + 0.3 * y if y < 0.8 else 0.0, 0.8)]
    for i, (name, fn, hm) in enumerate(shapes):
        cx = 24 + cell * i + cell / 2
        pts = outline_from_halfwidth(fn, hm, cx, 102, 60 / max(hm, 1.0), n=48)
        b.append(f'<polygon points="{polypts(pts)}" fill="{PIPE}" stroke="{INK}" stroke-width="1.3"/>')
        b.append(text(cx, 116, name, 8, MUTED))
    return svg(W, H, "\n".join(b),
               "Mockup 6 — icon tiles rendered from the same width-of-depth sampler")


if __name__ == "__main__":
    for fn, name in [(ex1_conduit_xsection, "1_conduit_xsection_panel"),
                     (ex2_conduit_profile, "2_conduit_profile_panel"),
                     (ex3_node_profile, "3_node_profile_panel"),
                     (ex4_xsect_editor_egg, "4_xsect_editor_preview"),
                     (ex5_lid_bioretention, "5_lid_bioretention_diagram"),
                     (ex6_icon_strip, "6_procedural_icons")]:
        path = os.path.join(OUT, name + ".svg")
        with open(path, "w") as f:
            f.write(fn())
        print("wrote", path)
