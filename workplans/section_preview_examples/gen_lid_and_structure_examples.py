#!/usr/bin/env python3
"""Round-2 review mockups for Slice SP.

  7  LID bio-retention, enriched: material textures, planting, ponded water,
     perforated underdrain, infiltration arrows.
  8  LID small multiples — rain barrel / green roof / permeable pavement /
     vegetative swale, showing that each TYPE now reads distinctly.
  9  Orifice drawn as an opening in the structure wall between its two nodes.
 10  Pump with wet-well, start/stop levels and the shut-off band.

7 and 8 mirror what src/ui/sectionview/lidlayerdiagram.cpp now emits.
9 and 10 are PROPOSALS — not implemented — for review before building.

Run:  python3 gen_lid_and_structure_examples.py
"""

import math
import os

OUT = os.path.dirname(os.path.abspath(__file__))

INK   = "#2c3e50"
DIM   = "#1a6fb5"
DIMLT = "#7fa8cc"
PIPE  = "#d9e9f7"
CONC  = "#e0ddd8"
SOIL  = "#c9b99a"

MEDIA = "#cdb68d"
GRAVEL= "#cfd6dd"
GREEN = "#6f9c5e"
LEAF  = "#7fae6e"
WATER = "#a8cdec"
MUTED = "#6b7a88"
BG    = "#fdfdfc"
STEEL = "#b9c2cb"

FONT = 'font-family="Helvetica,Arial,sans-serif"'


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def text(x, y, s, size=11, color=INK, anchor="middle", weight="normal", angle=None):
    tr = f' transform="rotate({angle} {x} {y})"' if angle is not None else ""
    return (f'<text x="{x:.1f}" y="{y:.1f}" {FONT} font-size="{size}" '
            f'fill="{color}" text-anchor="{anchor}" font-weight="{weight}"{tr}>{esc(s)}</text>')


def line(x1, y1, x2, y2, color=INK, w=1.2, dash=None, cap=None):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    c = f' stroke-linecap="{cap}"' if cap else ""
    return (f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" '
            f'stroke="{color}" stroke-width="{w}"{d}{c}/>')


def rect(x, y, w, h, fill, stroke=INK, sw=1.2, dash=None):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    return (f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" '
            f'fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}/>')


def circle(cx, cy, r, fill="none", stroke=INK, sw=1.2):
    return (f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r:.1f}" fill="{fill}" '
            f'stroke="{stroke}" stroke-width="{sw}"/>')


def poly(pts, fill, stroke=INK, sw=1.2):
    p = " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)
    return f'<polygon points="{p}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>'


def polyline(pts, stroke=INK, sw=1.2, dash=None):
    p = " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)
    d = f' stroke-dasharray="{dash}"' if dash else ""
    return f'<polyline points="{p}" fill="none" stroke="{stroke}" stroke-width="{sw}"{d}/>'


def arrow(x, y, angle, color=DIM, size=7):
    a = math.radians(angle)
    pts = []
    for dx, dy in [(0, 0), (-size, size * 0.35), (-size, -size * 0.35)]:
        pts.append((x + dx * math.cos(a) - dy * math.sin(a),
                    y + dx * math.sin(a) + dy * math.cos(a)))
    return poly(pts, color, color, 0)


def farrow(x1, y1, x2, y2, color=DIM, w=1.6, label=None, lx=None, ly=None):
    s = [line(x1, y1, x2, y2, color, w, cap="round"),
         arrow(x2, y2, math.degrees(math.atan2(y2 - y1, x2 - x1)), color)]
    if label:
        s.append(text(lx if lx is not None else x1,
                      ly if ly is not None else y1 - 5, label, 9, color))
    return "\n".join(s)


def vdim(x, y1, y2, label, ext_from=None, size=9.5):
    s = []
    if ext_from is not None:
        for yy in (y1, y2):
            s.append(line(ext_from, yy, x, yy, DIMLT, 0.7))
    s += [line(x, y1, x, y2, DIM, 1.0), arrow(x, y1, -90), arrow(x, y2, 90),
          text(x - 4, (y1 + y2) / 2, label, size, DIM, "middle", angle=-90)]
    return "\n".join(s)


def leader(x1, y1, x2, y2, label, size=9.5, color=DIM):
    a = "start" if x2 >= x1 else "end"
    elbow = x2 + (-6 if a == "start" else 6)
    return "\n".join([
        line(x1, y1, elbow, y2, color, 0.8),
        line(elbow, y2, x2, y2, color, 0.8),
        circle(x1, y1, 1.8, color, color, 0),
        text(x2 + (3 if a == "start" else -3), y2 + 3.4, label, size, color, a)])


def svg(w, h, body, title):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" '
            f'viewBox="0 0 {w} {h}">\n'
            f'<rect width="{w}" height="{h}" fill="{BG}" stroke="#d8dde2"/>\n{body}\n'
            f'<text x="10" y="{h-8}" {FONT} font-size="9" fill="{MUTED}">{esc(title)}</text>\n</svg>\n')


# ----------------------------------------------------------------- textures --
def _jit(k):
    h = (k * 2654435761) & 0xFFFFFFFF
    h ^= h >> 15
    h = (h * 2246822519) & 0xFFFFFFFF
    h ^= h >> 13
    return ((h & 0xFFFF) / 32767.5) - 1.0


_TEXCLIP = [0]


def tex(x, y, w, h, kind, color=INK, clip_id=None):
    """Mirrors paintTexture() in sectiondiagram.cpp, including its clip."""
    s = []
    op = 0.55
    if kind in ("stipple", "sand"):
        step = 5 if kind == "sand" else 8
        r = 0.6 if kind == "sand" else 0.9
        k = 0
        yy = y + 2
        while yy < y + h:
            xx = x + 2
            while xx < x + w:
                s.append(f'<circle cx="{xx + _jit(k)*step*0.35:.1f}" '
                         f'cy="{yy + _jit(k+7919)*step*0.35:.1f}" r="{r}" '
                         f'fill="{color}" fill-opacity="{op}"/>')
                xx += step; k += 1
            yy += step
    elif kind == "gravel":
        step, k = 13, 0
        yy = y + 5
        while yy < y + h:
            xx = x + 5
            while xx < x + w:
                rr = 2.6 + _jit(k) * 0.9
                s.append(f'<circle cx="{xx + _jit(k+104729)*3:.1f}" '
                         f'cy="{yy + _jit(k+15485863)*3:.1f}" r="{rr:.1f}" '
                         f'fill="none" stroke="{color}" stroke-opacity="{op}" stroke-width="0.9"/>')
                xx += step; k += 1
            yy += step
    elif kind == "aggregate":
        step, k = 11, 0
        yy = y + 4
        while yy < y + h:
            xx = x + 4
            while xx < x + w:
                rr = 2.2 + _jit(k) * 0.8
                cx0, cy0 = xx + _jit(k+31)*2.5, yy + _jit(k+97)*2.5
                pts = []
                for v in range(3):
                    a = v * 2 * math.pi / 3 + _jit(k + v*13) * 0.6
                    pts.append((cx0 + rr*math.cos(a), cy0 + rr*math.sin(a)))
                pp = " ".join(f"{px:.1f},{py:.1f}" for px, py in pts)
                s.append(f'<polygon points="{pp}" fill="none" stroke="{color}" '
                         f'stroke-opacity="{op}" stroke-width="0.9"/>')
                xx += step; k += 1
            yy += step
    elif kind == "hatch":
        xx = x - h
        while xx < x + w:
            s.append(f'<line x1="{xx:.1f}" y1="{y+h:.1f}" x2="{xx+h:.1f}" y2="{y:.1f}" '
                     f'stroke="{color}" stroke-opacity="{op}" stroke-width="0.8"/>')
            xx += 8
    elif kind == "lattice":
        xx = x
        while xx < x + w:
            s.append(f'<line x1="{xx:.1f}" y1="{y:.1f}" x2="{xx:.1f}" y2="{y+h:.1f}" '
                     f'stroke="{color}" stroke-opacity="{op}" stroke-width="0.8"/>')
            xx += 7
        yy = y
        while yy < y + h:
            s.append(f'<line x1="{x:.1f}" y1="{yy:.1f}" x2="{x+w:.1f}" y2="{yy:.1f}" '
                     f'stroke="{color}" stroke-opacity="{op}" stroke-width="0.8"/>')
            yy += 7
    elif kind == "brick":
        bw, bh = 26, max(6, h * 0.5)
        row, yy = 0, y
        while yy < y + h:
            s.append(f'<line x1="{x:.1f}" y1="{yy:.1f}" x2="{x+w:.1f}" y2="{yy:.1f}" '
                     f'stroke="{color}" stroke-opacity="{op}" stroke-width="1"/>')
            off = bw * 0.5 if row % 2 else 0
            xx = x + off
            while xx < x + w:
                s.append(f'<line x1="{xx:.1f}" y1="{yy:.1f}" x2="{xx:.1f}" '
                         f'y2="{min(yy+bh, y+h):.1f}" stroke="{color}" '
                         f'stroke-opacity="{op}" stroke-width="1"/>')
                xx += bw
            yy += bh; row += 1
    # paintTexture() clips the pattern to the polygon; without the same clip the
    # mockup would show grain spilling outside its layer, which the real
    # renderer never does.
    _TEXCLIP[0] += 1
    cid = f"texclip{_TEXCLIP[0]}"
    return (f'<clipPath id="{cid}"><rect x="{x:.1f}" y="{y:.1f}" '
            f'width="{w:.1f}" height="{h:.1f}"/></clipPath>'
            f'<g clip-path="url(#{cid})">' + "\n".join(s) + "</g>")


def plant(x, ybase, h, grass=False, k=0):
    s = []
    if grass:
        for i in (-1, 0, 1):
            lean = i * 0.30 + _jit(k + i) * 0.12
            hh = h * (1.0 if i == 0 else 0.72)
            s.append(f'<path d="M {x:.1f} {ybase:.1f} Q {x+lean*hh*0.4:.1f} '
                     f'{ybase-hh*0.6:.1f} {x+lean*hh:.1f} {ybase-hh:.1f}" fill="none" '
                     f'stroke="{GREEN}" stroke-width="1.3" stroke-linecap="round"/>')
        return "\n".join(s)
    tx = x + _jit(k) * h * 0.08
    ty = ybase - h
    s.append(line(x, ybase, tx, ty, GREEN, 1.5, cap="round"))
    for f in (0.45, 0.75):
        ax, ay = x + (tx - x) * f, ybase + (ty - ybase) * f
        L = h * 0.30
        s.append(line(ax, ay, ax - L, ay - L * 0.75, LEAF, 1.3, cap="round"))
        s.append(line(ax, ay, ax + L, ay - L * 0.75, LEAF, 1.3, cap="round"))
    return "\n".join(s)


# ============================================================ Example 7 =====
def ex7_bioretention_rich():
    W, H = 620, 430
    b = [text(14, 22, "LID Editor — Bio-Retention Cell", 12.5, INK, "start", "bold"),
         text(14, 38, "materials, planting, ponding, underdrain", 10, MUTED, "start")]

    x0, x1 = 120, 380
    w = x1 - x0
    layers = [("Surface", 58, "#e8f2e2", None,        "berm 0.15 m · n 0.10 · slope 1.0 %"),
              ("Soil",    96, MEDIA,     "stipple",   "thickness 0.50 m · porosity 0.45 · K 25"),
              ("Storage", 70, GRAVEL,    "gravel",    "thickness 0.30 m · void 0.60 · seepage 5")]
    y = 92
    ys = {}
    for name, hgt, fill, t, _ in layers:
        ys[name] = (y, y + hgt)
        b.append(rect(x0, y, w, hgt, fill))
        if t:
            b.append(tex(x0, y, w, hgt, t))
        y += hgt
    stack_bottom = y
    nat_h = 62
    b.append(rect(x0, stack_bottom, w, nat_h, SOIL))
    b.append(tex(x0, stack_bottom, w, nat_h, "hatch"))

    # ponded water in the surface layer
    st, sb = ys["Surface"]
    pond = sb - (sb - st) * 0.45
    b.append(rect(x0, pond, w, sb - pond, WATER, WATER, 0))
    b.append(line(x0, st, x1, st, DIM, 1.0, "5 3"))
    b.append(text(x1 - 4, st - 4, "berm", 8.5, DIM, "end"))

    # planting
    for i in range(7):
        px = x0 + w * (i + 0.5) / 7 + _jit(i * 31) * 2
        b.append(plant(px, st, 34, grass=False, k=i * 101))

    # inset labels on plates
    for name, _, _, _, _ in layers:
        t_, b_ = ys[name]
        cy = (t_ + b_) / 2
        b.append(f'<rect x="{(x0+x1)/2-30}" y="{cy-8}" width="60" height="16" rx="3" '
                 f'fill="{BG}" fill-opacity="0.78"/>')
        b.append(text((x0 + x1) / 2, cy + 4, name, 10, INK))
    b.append(f'<rect x="{(x0+x1)/2-36}" y="{stack_bottom+nat_h/2-8}" width="72" height="16" '
             f'rx="3" fill="{BG}" fill-opacity="0.78"/>')
    b.append(text((x0 + x1) / 2, stack_bottom + nat_h / 2 + 4, "Native soil", 10, INK))

    # underdrain, perforated
    drain_y = stack_bottom - 22
    b.append(circle((x0 + x1) / 2, drain_y, 9, PIPE, INK, 1.3))
    for i in range(8):
        a = math.pi * (0.15 + i * 0.10)
        dx, dy = math.cos(a), -math.sin(a)
        b.append(line((x0+x1)/2 + dx*9, drain_y + dy*9,
                      (x0+x1)/2 + dx*12, drain_y + dy*12, INK, 1.0))
    b.append(leader((x0 + x1) / 2 + 9, drain_y, x1 + 34, drain_y + 20,
                    "underdrain · offset 0.08 m"))
    b.append(vdim(x0 + 34, drain_y, stack_bottom, "offset", ext_from=x0 + 60))

    # thickness dims
    for name in ("Surface", "Soil", "Storage"):
        t_, b_ = ys[name]
        lab = {"Surface": "0.15 m", "Soil": "0.50 m", "Storage": "0.30 m"}[name]
        b.append(vdim(x1 + 22, t_, b_, lab, ext_from=x1))

    # flow arrows
    b.append(farrow(x0 - 46, st - 26, x0 - 4, st - 2, DIM, 1.6,
                    "runoff in", x0 - 46, st - 32))
    for fx in (0.28, 0.62):
        xx = x0 + w * fx
        b.append(farrow(xx, stack_bottom + 10, xx, stack_bottom + nat_h - 8, MUTED, 1.4))
    b.append(leader(x0 + w * 0.62, stack_bottom + nat_h - 8, x1 + 34,
                    stack_bottom + nat_h - 2, "infiltration"))

    b.append(line(14, 398, 606, 398, "#d8dde2", 1))
    b.append(text(14, 412, "3 layer(s) + underdrain · thicknesses in m", 9.5, MUTED, "start"))
    return svg(W, H, "\n".join(b),
               "Mockup 7 — enriched LID layer diagram (implemented)")


# ============================================================ Example 8 =====
def ex8_lid_small_multiples():
    W, H = 800, 320
    b = [text(14, 22, "LID types now read distinctly", 12.5, INK, "start", "bold"),
         text(14, 38, "same renderer, per-type materials + illustration", 10, MUTED, "start")]

    cw = 178
    top, bot = 62, 236

    def cell(ox, title, draw):
        s = [text(ox + cw / 2, 274, title, 10, INK, "middle", "bold")]
        s.append(draw(ox))
        return "\n".join(s)

    def rain_barrel(ox):
        s = []
        x0, x1 = ox + 46, ox + cw - 46
        wall = 7
        s.append(rect(x0 - wall, top + 14, (x1 - x0) + 2 * wall, bot - top - 14, STEEL))
        s.append(rect(x0, top + 22, x1 - x0, bot - top - 24, GRAVEL))
        s.append(tex(x0, top + 22, x1 - x0, bot - top - 24, "gravel"))
        s.append(line(x0 - wall, top + 14, x1 + wall, top + 14, INK, 1.6))
        s.append(text(x1 + wall + 2, top + 12, "lid", 8.5, MUTED, "start"))
        # downspout
        dx = x0 + (x1 - x0) * 0.22
        s.append(line(dx, top - 22, dx, top + 12, STEEL, 4))
        s.append(farrow(dx, top - 14, dx, top + 8, DIM, 1.4))
        s.append(text(dx + 6, top - 18, "roof leader", 8.5, DIM, "start"))
        return "\n".join(s)

    def green_roof(ox):
        s = []
        x0, x1 = ox + 34, ox + cw - 34
        w = x1 - x0
        y = top + 20
        for hgt, fill, t in ((30, "#e8f2e2", None), (46, MEDIA, "stipple"),
                             (26, "#dfe4e9", "lattice")):
            s.append(rect(x0, y, w, hgt, fill))
            if t:
                s.append(tex(x0, y, w, hgt, t))
            y += hgt
        s.append(rect(x0 - 10, y, w + 20, 20, CONC))   # roof deck
        s.append(text((x0 + x1) / 2, y + 14, "deck", 8.5, INK))
        for sx in (-1, 1):
            px = x0 - 10 if sx < 0 else x1
            s.append(rect(px, top + 4, 10, y - top - 4, CONC))
        for i in range(12):
            s.append(plant(x0 + w * (i + 0.5) / 12, top + 20, 14, grass=True, k=i * 7))
        return "\n".join(s)

    def perm_pavement(ox):
        s = []
        x0, x1 = ox + 30, ox + cw - 30
        w = x1 - x0
        y = top + 26
        for hgt, fill, t in ((26, "#dcdcd8", "brick"), (34, MEDIA, "stipple"),
                             (56, GRAVEL, "gravel")):
            s.append(rect(x0, y, w, hgt, fill))
            s.append(tex(x0, y, w, hgt, t))
            y += hgt
        s.append(rect(x0, y, w, 30, SOIL))
        s.append(tex(x0, y, w, 30, "hatch"))
        s.append(line(x0 - 6, top + 26, x1 + 6, top + 26, INK, 1.8))
        s.append(farrow((x0 + x1) / 2, top + 2, (x0 + x1) / 2, top + 22, DIM, 1.4))
        s.append(text(ox + cw/2, top - 4, "rain on pavement", 8.5, DIM))
        return "\n".join(s)

    def veg_swale(ox):
        s = []
        x0, x1 = ox + 20, ox + cw - 20
        lip, inv = top + 40, top + 108
        s.append(rect(x0, lip, x1 - x0, bot - lip, SOIL))
        s.append(tex(x0, lip, x1 - x0, bot - lip, "hatch"))
        chan = [(x0 + 8, lip), (x0 + 52, inv), (x1 - 52, inv), (x1 - 8, lip)]
        s.append(poly(chan + [(x1 - 8, lip + 4), (x0 + 8, lip + 4)], WATER, "none", 0))
        s.append(polyline(chan, GREEN, 2.0))
        for i in range(11):
            t = (i + 0.5) / 11
            px = x0 + 8 + (x1 - x0 - 16) * t
            if t < 0.3:
                py = lip + (inv - lip) * (t / 0.3)
            elif t > 0.7:
                py = inv + (lip - inv) * ((t - 0.7) / 0.3)
            else:
                py = inv
            s.append(plant(px, py, 15, grass=True, k=i * 13))
        s.append(leader((x0 + x1) / 2, inv, x0 + 26, inv + 30, "swale invert", 8.5))
        return "\n".join(s)

    for i, (title, fn) in enumerate([("Rain Barrel", rain_barrel),
                                     ("Green Roof", green_roof),
                                     ("Permeable Pavement", perm_pavement),
                                     ("Vegetative Swale", veg_swale)]):
        b.append(cell(14 + i * (cw + 8), title, fn))
    return svg(W, H, "\n".join(b),
               "Mockup 8 — per-type LID illustration (implemented)")


# ============================================================ Example 9 =====
def ex9_orifice():
    W, H = 640, 360
    b = [text(14, 22, "ORIFICE  OR-1  —  Profile  (PROPOSAL)", 12.5, INK, "start", "bold"),
         text(14, 38, "an orifice is an opening in a wall, not a pipe", 10, MUTED, "start")]

    # two structures either side of a shared wall
    lx, rx = 190, 430
    rimL, rimR = 86, 96
    invL, invR = 268, 268
    wall_t = 26
    wx = (lx + rx) / 2 - wall_t / 2

    # ground
    for x0, x1, yy in ((30, lx - 30, rimL), (rx + 30, 610, rimR)):
        b.append(line(x0, yy, x1, yy, "#8a7357", 1.4))
        xx = x0 + 3
        while xx < x1:
            b.append(line(xx, yy, xx - 5, yy + 8, "#8a7357", 0.8))
            xx += 9

    # chambers: air above, water below the head line — a chamber flooded to the
    # rim would misrepresent the very thing the drawing exists to show.
    usw, dsw = 150, 214
    b.append(rect(lx - 30, rimL, (wx - lx + 30), invL - rimL, BG))
    b.append(rect(lx - 30, usw, (wx - lx + 30), invL - usw, WATER, INK, 1.0))
    b.append(rect(wx + wall_t, rimR, (rx + 30) - (wx + wall_t), invR - rimR, BG))
    b.append(rect(wx + wall_t, dsw, (rx + 30) - (wx + wall_t), invR - dsw, WATER, INK, 1.0))
    # shared wall with the opening
    op_top, op_bot = 196, 232
    b.append(rect(wx, rimL - 6, wall_t, op_top - (rimL - 6), CONC))
    b.append(rect(wx, op_bot, wall_t, invL + 8 - op_bot, CONC))
    # the hole itself, emphasised
    b.append(rect(wx - 1, op_top, wall_t + 2, op_bot - op_top, WATER, DIM, 1.6))
    b.append(text(wx + wall_t / 2, op_top - 8, "opening", 9, DIM))

    # water levels
    b.append(line(lx - 30, usw, wx, usw, DIM, 1.2, "6 3"))
    b.append(text(lx - 26, usw - 5, "u/s head", 9, DIM, "start"))
    b.append(line(wx + wall_t, dsw, rx + 30, dsw, DIM, 1.2, "6 3"))
    b.append(text(rx + 26, dsw - 5, "d/s head", 9, DIM, "end"))
    # flow through the opening — the reason an orifice is not a pipe
    b.append(farrow(wx - 26, (op_top + op_bot) / 2, wx + wall_t + 26,
                    (op_top + op_bot) / 2, DIM, 1.8))

    # dimensions
    b.append(vdim(wx - 40, op_top, op_bot, "Height 0.60", ext_from=wx))
    b.append(leader(wx + wall_t, op_bot, rx + 30, op_bot + 44,
                    "Crest El. 101.32 m = invert + offset"))
    b.append(leader(wx, op_top, lx - 26, op_top - 40, "Crown El. 101.92 m"))

    # node labels
    b.append(text(lx - 30, rimL - 12, "J-07", 10, INK, "start", "bold"))
    b.append(text(rx + 30, rimR - 12, "J-08", 10, INK, "end", "bold"))
    b.append(leader(lx - 30, invL, lx - 40, invL + 30, "Inv 100.72"))
    b.append(leader(rx + 30, invR, rx + 44, invR + 26, "Inv 100.72"))

    b.append(line(14, 320, 626, 320, "#d8dde2", 1))
    b.append(text(14, 334, "SIDE orifice · RECT_CLOSED 0.60 × 0.45 m · Cd 0.65 · "
                           "flap gate NO", 9.5, MUTED, "start"))
    return svg(W, H, "\n".join(b),
               "Mockup 9 — PROPOSAL: orifice as an opening in the structure wall")


# =========================================================== Example 10 =====
def ex10_pump():
    W, H = 660, 400
    b = [text(14, 22, "PUMP  P-3  —  Profile  (PROPOSAL)", 12.5, INK, "start", "bold"),
         text(14, 38, "wet well with start / stop control levels", 10, MUTED, "start")]

    # wet well
    wx0, wx1 = 90, 250
    rim, inv = 92, 300
    wall = 10
    b.append(rect(wx0 - wall, rim, (wx1 - wx0) + 2 * wall, inv - rim + wall, CONC))
    b.append(rect(wx0, rim + 4, wx1 - wx0, inv - rim - 4, PIPE))

    # ground
    for x0, x1 in ((22, wx0 - wall), (wx1 + wall, 250)):
        if x1 > x0:
            b.append(line(x0, rim, x1, rim, "#8a7357", 1.4))
            xx = x0 + 3
            while xx < x1:
                b.append(line(xx, rim, xx - 5, rim + 8, "#8a7357", 0.8))
                xx += 9

    # control levels — the whole point of the drawing
    start_y, stop_y, shut_y = 176, 250, 274
    b.append(rect(wx0, start_y, wx1 - wx0, stop_y - start_y, WATER, "none", 0))
    b.append(f'<rect x="{wx0}" y="{stop_y}" width="{wx1-wx0}" height="{shut_y-stop_y}" '
             f'fill="{WATER}" fill-opacity="0.45"/>')

    for yy, lab, col, dash in ((start_y, "START  depth 2.20 m", DIM, None),
                               (stop_y,  "STOP   depth 0.80 m", DIM, "6 3"),
                               (shut_y,  "SHUT-OFF depth 0.45 m", MUTED, "3 3")):
        b.append(line(wx0 - 14, yy, wx1 + 14, yy, col, 1.4, dash))
        b.append(text(wx1 + 20, yy + 3.5, lab, 9.5, col, "start"))

    b.append(vdim(wx0 - 34, start_y, inv, "start 2.20", ext_from=wx0))
    b.append(vdim(wx0 - 62, stop_y, inv, "stop 0.80", ext_from=wx0))

    # inflow + pump + rising main
    b.append(rect(18, 196, wx0 - wall - 18, 26, PIPE))
    b.append(text(20, 190, "C-21 in", 9, INK, "start", "bold"))

    pcx, pcy = (wx0 + wx1) / 2, 292
    b.append(circle(pcx, pcy, 15, STEEL, INK, 1.4))
    b.append(text(pcx, pcy + 4, "P", 10, INK, "middle", "bold"))
    b.append(polyline([(pcx, pcy - 15), (pcx, 138), (wx1 + 100, 138)], INK, 3.0))
    b.append(arrow(wx1 + 100, 138, 0, INK, 8))
    b.append(text(wx1 + 104, 134, "rising main → J-12", 9, INK, "start"))

    b.append(leader(wx0 - wall, rim, wx0 + 6, rim - 24, "Rim El. 104.60"))
    b.append(leader(pcx, inv, pcx + 40, inv + 20, "Well invert 100.10"))

    # pump curve inset
    ix, iy, iw, ih = 460, 226, 176, 104
    b.append(rect(ix, iy, iw, ih, "#f3f6f9", "#c3ccd4", 1))
    b.append(text(ix + 6, iy + 14, "Pump curve (TYPE3)", 8.5, MUTED, "start"))
    pts = [(ix + 12 + i * (iw - 24) / 5,
            iy + ih - 14 - (ih - 30) * (1 - (i / 5) ** 1.7))
           for i in range(6)]
    b.append(polyline(pts, DIM, 1.8))
    b.append(text(ix + iw / 2, iy + ih - 3, "head vs flow", 8, MUTED))

    b.append(line(14, 360, 646, 360, "#d8dde2", 1))
    b.append(text(14, 374, "PUMP1 · startup 2.20 m · shutoff 0.45 m · "
                           "initial status ON", 9.5, MUTED, "start"))
    return svg(W, H, "\n".join(b),
               "Mockup 10 — PROPOSAL: pump wet well with start / stop levels")


if __name__ == "__main__":
    for fn, name in [(ex7_bioretention_rich, "7_lid_bioretention_rich"),
                     (ex8_lid_small_multiples, "8_lid_type_multiples"),
                     (ex9_orifice, "9_orifice_opening_proposal"),
                     (ex10_pump, "10_pump_levels_proposal")]:
        path = os.path.join(OUT, name + ".svg")
        with open(path, "w") as f:
            f.write(fn())
        print("wrote", path)


# =========================================================== Example 11 =====
NICE_VE = [1, 1.5, 2, 2.5, 3, 4, 5, 7.5, 10, 15, 20, 25, 30, 40, 50, 75, 100]


def _snap_ve(ve):
    if ve <= NICE_VE[0]:
        return NICE_VE[0]
    return max(n for n in NICE_VE if n <= ve)


def _fit(bw, bh, fw, fh, ve_req=0.0, ve_max=10.0, target=6.0):
    """Port of the exaggeration fit in paintSectionDiagram()."""
    sx, sy = fw / bw, fh / bh
    fill = sy / sx
    ve = ve_req
    if not ve > 0:
        if target > 0:
            ve = max(1.0, (bw / bh) / target)
            if ve_max > 0:
                ve = min(ve, ve_max)
            ve = _snap_ve(ve)
        else:
            ve = fill          # legacy: fill the pane
    if fill >= ve:
        sy = sx * ve
    else:
        sx = sy / ve
    return sx, sy, ve


def ex11_slope_fidelity():
    """Same 120 m reach at 0.25 %, in two pane heights, old fit vs new."""
    W, H = 940, 560
    b = [text(14, 22, "Profile slope fidelity", 12.5, INK, "start", "bold"),
         text(14, 38, "identical pipe (120 m at 0.25 %) — the old fit changes "
                      "the apparent gradient with the pane; the new one does not",
              10, MUTED, "start")]

    # Model band: 0 = lowest invert, 4.5 = rim.
    L, Hm, slope = 120.0, 4.5, 0.0025
    inv_dn = 0.30
    inv_up = inv_dn + L * slope        # 0.60
    pipe_h, rim = 0.60, 4.30

    cols = [("Before — fill both axes", dict(target=0.0, ve_max=0.0)),
            ("After — automatic", dict())]
    rows = [("short dock", 150), ("tall dock", 250)]

    pane_w = 430
    for r, (rlabel, pane_h) in enumerate(rows):
        for c, (clabel, kw) in enumerate(cols):
            ox = 14 + c * (pane_w + 16)
            oy = 62 + (0 if r == 0 else 190)
            b.append(rect(ox, oy, pane_w, pane_h, "#ffffff", "#dde3e8", 1))

            fw, fh = pane_w - 46, pane_h - 44
            sx, sy, ve = _fit(L, Hm, fw, fh, **kw)

            # centre the drawing in the pane, as the painter does
            cx0 = ox + (pane_w - L * sx) / 2.0
            base = oy + 30 + (fh + Hm * sy) / 2.0

            def px(x, y):
                return (cx0 + x * sx, base - y * sy)

            mw = L * 0.028
            for xc, inv in ((0.0, inv_up), (L, inv_dn)):
                x0, y0 = px(xc - mw, rim)
                x1, y1 = px(xc + mw, inv - 0.12)
                b.append(rect(min(x0, x1), min(y0, y1),
                              abs(x1 - x0), abs(y1 - y0), CONC))

            b.append(poly([px(mw, inv_up + pipe_h), px(L - mw, inv_dn + pipe_h),
                           px(L - mw, inv_dn), px(mw, inv_up)], PIPE))

            drawn = ((inv_up - inv_dn) * sy) / ((L - 2 * mw) * sx)
            hot = "#b5651d" if drawn > 0.012 else DIM
            b.append(text(ox + 10, oy + 17, "%s · %s" % (clabel, rlabel),
                          9.5, INK, "start", "bold"))
            b.append(text(ox + pane_w - 10, oy + 17,
                          "V:H %g:1   reads %.2f %%" % (ve, drawn * 100),
                          9.5, hot, "end"))

    b.append(line(14, H - 54, W - 14, H - 54, "#d8dde2", 1))
    b.append(text(14, H - 36,
                  "True gradient 0.25 %. Filling both axes ties the exaggeration "
                  "to the pane, so the same pipe reads steeper in a taller dock.",
                  9.5, MUTED, "start"))
    b.append(text(14, H - 20,
                  "The new fit derives the ratio from the model's own proportions "
                  "(a 6:1 drawn aspect), caps at 10:1, and states the "
                  "ratio on the drawing. 1:1 is selectable for the true picture.",
                  9.5, MUTED, "start"))
    return svg(W, H, "\n".join(b),
               "Mockup 11 — profile slope fidelity (implemented)")


if True:
    path = os.path.join(OUT, "11_profile_slope_fidelity.svg")
    with open(path, "w") as f:
        f.write(ex11_slope_fidelity())
    print("wrote", path)
