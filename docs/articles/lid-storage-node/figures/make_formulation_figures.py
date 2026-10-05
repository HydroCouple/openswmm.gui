"""Reproducible formulation diagrams and an analytical orifice animation.

Diagrams are explanatory. The resaturation animation uses native engine samples.
Uses standard-library SVG, rsvg-convert, and ImageMagick. No AI image model.
"""
from pathlib import Path
import html
import csv
import json
import hashlib
import math
import subprocess
import shutil

ASSETS = Path(__file__).resolve().parents[1] / 'assets'
FRAMES = Path('/tmp/lid_formulation_frames')
COL = dict(ink='#123047', muted='#557080', teal='#008f83', blue='#188fb6',
           amber='#db8b24', red='#b84e5b', gray='#dce6eb', paper='#f7fafb')


def color(c):
    return COL.get(c, c)


def text(x, y, value, size=22, fill='ink', weight='normal', anchor='start'):
    # Arial lacks U+2098 on macOS. Native SVG subscripts keep equations legible.
    encoded = html.escape(str(value))
    for glyph, letter in [('ₘ', 'm'), ('ₛ', 's')]:
        encoded = encoded.replace(glyph, f'<tspan baseline-shift="sub" font-size="75%">{letter}</tspan>')
    return (f'<text x="{x}" y="{y}" font-size="{size}" fill="{color(fill)}" '
            f'font-weight="{weight}" text-anchor="{anchor}">{encoded}</text>')


def rect(x, y, w, h, fill='white', rx=0, stroke='none', opacity=1):
    return (f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" '
            f'fill="{color(fill)}" stroke="{color(stroke)}" opacity="{opacity}"/>')


def line(x1, y1, x2, y2, fill='gray', width=2, dash='', arrow=False):
    return (f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" '
            f'stroke="{color(fill)}" stroke-width="{width}"'
            + (f' stroke-dasharray="{dash}"' if dash else '')
            + (' marker-end="url(#arrow)"' if arrow else '') + '/>')


def circle(x, y, r, fill='blue', stroke='none'):
    return f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color(fill)}" stroke="{color(stroke)}"/>'


def document(parts, height=820):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="{height}" '
            f'viewBox="0 0 1200 {height}" font-family="Arial, sans-serif">'
            '<defs><marker id="arrow" viewBox="0 0 10 10" refX="8" refY="5" '
            'markerWidth="5" markerHeight="5" orient="auto-start-reverse">'
            '<path d="M0 0 L10 5 L0 10Z" fill="context-stroke"/></marker></defs>'
            + rect(0, 0, 1200, height, 'paper') + ''.join(parts) + '</svg>')


def save(name, parts, height=820, directory=ASSETS):
    directory.mkdir(parents=True, exist_ok=True)
    source = directory / (name + '.svg')
    source.write_text(document(parts, height))
    target = directory / (name + '.png')
    subprocess.run(['rsvg-convert', '-o', str(target), str(source)], check=True)
    return target


def water_stores():
    p = [text(36, 48, 'One layered facility. Two coupled water stores.', 32, weight='bold'),
         text(36, 84, 'Retained moisture occupies pores; mobile water fills the remaining connected space.', 23, 'muted'),
         rect(28, 112, 614, 558, rx=14), rect(668, 112, 504, 558, rx=14),
         text(50, 148, 'Layer geometry and flow pathways', 24, weight='bold')]
    x, y, w = 179, 220, 275
    p += [rect(x, y, w, 63, '#eef4e5'), rect(x, y+63, w, 126, '#e2d5bd'),
          rect(x, y+189, w, 126, '#dce6eb')]
    for i in range(1, 5):
        p += [line(x, y+63+i*25.2, x+w, y+63+i*25.2, 'muted', 1, '4 4')]
    # Illustrative snapshot: media at field capacity, aggregate retained theta=0.
    # h=0.70 ft is wholly in the aggregate; V_m = .4*1000*.7 = 280 ft3.
    head_y = y + 315 - .70*126
    p += [rect(x, head_y, w, .70*126, 'blue', opacity=.65)]
    for row in range(5):
        for col in range(8):
            p += [circle(x+17+col*34, y+75+row*25.2, 4.2, 'teal')]
    p += [text(56, 247, 'SURFACE', 19, weight='bold'), text(56, 273, '6 in · φ = 0.90', 16, 'muted'),
          text(56, 325, 'MEDIA', 19, weight='bold'), text(56, 351, '12 in · φ = 0.45', 16, 'muted'),
          text(56, 445, 'AGGREGATE', 18, weight='bold'), text(56, 471, '12 in · φ = 0.40', 16, 'muted'),
          line(x, head_y, 618, head_y, 'blue', 2, '7 5'),
          text(475, head_y-12, 'h = 0.70 ft', 20, 'blue', 'bold'),
          line(220, 181, 220, 214, 'blue', 4, arrow=True), text(177, 177, 'Runoff', 20, 'blue'),
          line(389, 214, 389, 181, 'amber', 4, arrow=True), text(361, 177, 'Evaporation', 20, 'amber'),
          line(454, y+63, 620, y+63, 'muted', 3), text(475, y+44, 'Overflow crest', 19, 'muted'),
          text(475, y+89, 'z = 2.00 ft', 18, 'muted'),
          line(316, y+147, 316, y+208, 'teal', 4, arrow=True),
          text(338, y+176, 'Drainage', 18, 'teal'),
          line(454, y+285, 618, y+285, 'blue', 4, arrow=True),
          circle(548, y+285, 12, 'white', 'blue'), line(540, y+293, 556, y+277, 'blue', 3),
          text(475, y+324, 'Layer port / valve', 18, 'blue'),
          line(x, y+315, x+w, y+315, 'ink', 4),
          text(316, y+350, 'Closed bottom in these examples', 19, 'muted', anchor='middle'),
          text(50, 612, 'Dots: retained moisture · Blue: mobile water', 20, 'muted'),
          text(50, 643, 'Five media cells resolve the moisture profile.', 20, 'muted')]
    for top, label, formula, details, value, c in [
        (148, 'RETAINED WATER', 'Vᵣ = Σ θᵢ Gᵢ', 'Media θ = 0.20; aggregate θ = 0', '200 ft³', 'teal'),
        (314, 'MOBILE WATER', 'Vₘ(h) = Σ (φᵢ − θᵢ) Gᵢ sᵢ(h)', 'sᵢ(h): fraction of a cell below h', '280 ft³', 'blue'),
        (480, 'TOTAL WATER INVENTORY', 'V = Vᵣ + Vₘ', 'Footprint A = 1,000 ft²; invert z = 0', '480 ft³', 'ink')]:
        p += [text(696, top, label, 20, c, 'bold'), text(696, top+39, formula, 27, c),
              text(696, top+74, details, 20, 'muted'), text(696, top+115, value, 31, c, 'bold')]
    p += [rect(28, 692, 1144, 98, '#eaf1f4', 12),
          text(50, 730, 'Internal drainage moves water between stores with equal debits and credits.', 25, weight='bold'),
          text(50, 766, 'Gᵢ: cell geometric volume · θᵢ: retained water fraction · φᵢ: available void fraction', 22, 'muted')]
    return p


def hydraulic_frame(frame):
    # Four conceptual states; the animation is not a replay of the control rules.
    phase, offset = divmod(frame, 16)
    if phase == 0:
        hr, u, title = .4, 1, 'Forward release'
    elif phase == 1:
        hr, u, title = .4+1.2*min(offset/8, 1), 1, 'Receiving stage rises'
    elif phase == 2:
        hr, u, title = 1.6-1.2*max((offset-7)/8, 0), 0, 'Valve isolates the connection'
    else:
        hr, u, title = .4, 1, 'Reopen after stage falls'
    hb, delta = 1.2, 1.2-hr
    cd, area, g = .6, math.pi*.1**2/4, 32.174
    q = cd*area*math.copysign(math.sqrt(2*g*abs(delta)), delta) if u else 0
    c = 'blue' if q >= 0 else 'amber'
    p = [text(36, 48, 'Head determines direction. Control determines connection.', 31, weight='bold'),
         text(36, 84, 'Analytical illustration of a fully submerged orifice; no flap gate.', 23, 'muted'),
         rect(28, 112, 655, 485, rx=14), rect(710, 112, 462, 485, rx=14),
         text(52, 150, title, 25, weight='bold')]
    bottom, scale = 527, 145
    for name, x, width, head in [('LID B', 129, 230, hb), ('Boundary R', 464, 145, hr)]:
        if name == 'LID B':
            p += [rect(x, bottom-2.5*scale, width, .5*scale, '#eef4e5'),
                  rect(x, bottom-2*scale, width, scale, '#e2d5bd'),
                  rect(x, bottom-scale, width, scale, '#dce6eb')]
        else:
            p += [rect(x, bottom-2.5*scale, width, 2.5*scale, '#eaf1f4')]
        p += [rect(x, bottom-head*scale, width, head*scale, 'blue', opacity=.5),
              line(x, bottom-head*scale, x+width, bottom-head*scale, c, 3),
              text(x+width/2, bottom-head*scale-17, f'H = {head:.2f} ft', 21, c, 'bold', 'middle'),
              line(x, bottom, x+width, bottom, 'ink', 3),
              text(x+width/2, 568, name, 23, 'ink', 'bold', 'middle')]
    py, left, right = 506, 359, 464
    p += [line(left, py, right, py, 'gray', 6), circle(412, py, 13, 'white', c)]
    if not u:
        p += [line(401, py-11, 423, py+11, 'red', 4), line(401, py+11, 423, py-11, 'red', 4)]
    elif abs(q) > 1e-7:
        direction = 1 if q > 0 else -1
        for j in range(3):
            fraction = (frame/8+j/3) % 1
            xx = left+(fraction if direction > 0 else 1-fraction)*(right-left)
            p += [f'<path d="M{xx-6*direction},{py-6} L{xx},{py} L{xx-6*direction},{py+6}" '
                  f'fill="none" stroke="{color(c)}" stroke-width="3"/>']
    p += [text(734, 151, 'Open-valve response', 24, weight='bold')]
    px, py, pw, ph = 771, 208, 348, 277
    def xx(d): return px+(d+1.2)/2.4*pw
    def yy(v): return py+ph/2-v/.05*ph/2
    p += [line(px, yy(0), px+pw, yy(0), 'muted'), line(xx(0), py, xx(0), py+ph, 'muted')]
    for tick in [-1, 0, 1]:
        p += [text(xx(tick), py+ph+26, f'{tick:+.1f}' if tick else '0', 17, 'muted', anchor='middle')]
    for tick in [-.04, 0, .04]:
        p += [text(px-12, yy(tick)+6, f'{tick:.2f}', 17, 'muted', anchor='end')]
    pts = []
    for i in range(161):
        d = -1.2+i*2.4/160
        v = cd*area*math.copysign(math.sqrt(2*g*abs(d)), d)
        pts.append(f'{xx(d):.2f},{yy(v):.2f}')
    p += [f'<polyline points="{" ".join(pts)}" fill="none" stroke="{color("blue")}" stroke-width="4"/>',
          line(px, yy(0), px+pw, yy(0), 'red', 2, '7 5'),
          circle(xx(delta), yy(q), 9, c if u else 'red', 'white'),
          text(771, 186, 'Q (cfs)', 18, 'muted'), text(945, 553, 'ΔH = H_B − H_R (ft)', 22, 'muted', anchor='middle'),
          text(734, 582, 'Dashed red: closed valve, Q = 0', 20, 'red')]
    p += [rect(28, 618, 1144, 166, '#eaf1f4', 12),
          text(53, 657, 'Q = C_d Aₒ(u) sign(ΔH) √(2g |ΔH|)', 28, weight='bold'),
          text(53, 697, f'Opening u = {u}     ΔH = {delta:+.2f} ft     Ideal Q = {q:+.4f} cfs', 25, c, 'bold'),
          text(53, 734, 'C_d = 0.6 · diameter = 0.10 ft · heads share the same datum', 21, 'muted'),
          text(53, 765, 'Arrows indicate direction. Full routing also limits flow by wetting and available water.', 21, 'muted')]
    return p


def treatment():
    p = [text(36, 48, 'Treatment follows pollutant mass and exposure time.', 32, weight='bold'),
         text(36, 84, 'Retained cells carry their own mass; mobile water uses one mixed reactor per LID node.', 23, 'muted'),
         rect(28, 112, 1144, 259, rx=14)]
    for x, w, label, eq, note, c in [
        (52, 300, 'RETAINED CELL', 'Cᵣ = Mᵣ / Vᵣ', 'Cell-specific decay kᵣ', 'teal'),
        (441, 329, 'SHARED MOBILE REACTOR', 'Cₘ = Mₘ / Vₘ', 'Volume-weighted layer decay', 'blue'),
        (860, 287, 'PHYSICAL LAYER EXIT', 'C_exit = (1 − r) C', 'Optional expression follows', 'amber')]:
        p += [rect(x, 139, w, 156, '#eaf1f4', 10), text(x+16, 169, label, 17, c, 'bold'),
              text(x+16, 216, eq, 28, c), text(x+16, 259, note, 18, 'muted')]
    p += [line(358, 199, 432, 199, 'teal', 4, arrow=True),
          line(432, 247, 358, 247, 'blue', 4, arrow=True),
          line(777, 220, 850, 220, 'blue', 4, arrow=True),
          text(52, 341, 'Each transfer: source loses ΔV × C_source; receiver gains the treated remainder.', 23, weight='bold'),
          rect(28, 393, 728, 397, rx=14), rect(780, 393, 392, 397, rx=14),
          text(52, 431, 'Closed-parcel illustration: k = 2/day', 25, weight='bold')]
    px, py, pw, ph = 107, 480, 597, 235
    def xx(t): return px+t/24*pw
    def yy(v): return py+(1-v)*ph
    for v in [0, .5, 1]:
        p += [line(px, yy(v), px+pw, yy(v), 'gray', 1), text(px-14, yy(v)+6, f'{v:.1f}', 18, 'muted', anchor='end')]
    for t in [0, 6, 12, 18, 24]:
        p += [text(xx(t), py+ph+28, str(t), 18, 'muted', anchor='middle')]
    p += [line(px, py, px, py+ph, 'muted'), line(px, py+ph, px+pw, py+ph, 'muted'),
          text(107, 461, 'Fraction of starting mass', 19, 'muted'), text(405, 776, 'Time held (h)', 21, 'muted', anchor='middle')]
    for reacted, c in [(False, 'teal'), (True, 'amber')]:
        points = []
        for j in range(121):
            t = j/5
            v = math.exp(-2*t/24)
            points.append(f'{xx(t):.2f},{yy(1-v if reacted else v):.2f}')
        p += [f'<polyline points="{" ".join(points)}" fill="none" stroke="{color(c)}" stroke-width="4"/>']
    p += [line(xx(6), py, xx(6), py+ph, 'muted', 1, '5 5'), circle(xx(6), yy(1-math.exp(-.5)), 7, 'amber'),
          text(xx(6)+14, yy(1-math.exp(-.5))+29, '39.3% reacted at 6 h', 19, 'amber', 'bold'),
          text(686, yy(math.exp(-2)) - 14, 'Remaining', 18, 'teal', 'bold', 'end'),
          text(686, yy(1-math.exp(-2)) - 14, 'Reacted', 18, 'amber', 'bold', 'end'),
          text(805, 432, 'FIRST-ORDER REACTION', 19, 'teal', 'bold'),
          text(805, 475, 'M(t) = M₀ exp(−kt)', 26, 'teal'),
          text(805, 514, 'Hold longer → more reaction', 21, 'muted'),
          text(805, 541, 'Half-life = 8.3 h', 20, 'teal', 'bold'),
          text(805, 576, 'SATURATED MOBILE WATER', 18, 'blue', 'bold'),
          text(805, 610, 'kₘ = k_bg + Σ kᵢ Vₘ,ᵢ / Vₘ', 22, 'blue'),
          text(805, 638, 'Ponded surface water adds volume', 19, 'muted'),
          text(805, 666, 'without the porous-layer decay rate.', 19, 'muted'),
          text(805, 715, 'Exit removal acts once per authored', 19, 'muted'),
          text(805, 743, 'layer; internal cell interfaces do not', 19, 'muted'),
          text(805, 771, 'repeat its percentage removal.', 19, 'muted')]
    return p


def richards():
    p = [text(36, 48, 'Retain the water balance. Simplify the soil-pressure solve.', 31, weight='bold'),
         text(36, 84, 'A conditional approximation for gravity-dominated engineered media—not every LID.', 23, 'muted'),
         rect(28, 112, 554, 475, rx=14), rect(608, 112, 564, 475, rx=14),
         text(52, 151, 'Richards-equation soil domain', 25, weight='bold'),
         text(632, 151, 'Current storage-node approximation', 25, weight='bold')]
    p += [rect(150, 189, 299, 243, '#e2d5bd'), rect(737, 189, 299, 243, '#e2d5bd')]
    for y in [227, 267, 307, 347, 387]:
        p += [line(170, y+10, 431, y-10, 'amber', 2, '6 4')]
    for x, y in [(210, 217), (371, 314), (250, 383)]:
        p += [line(x, y, x, y+33, 'teal', 4, arrow=True)]
    p += [line(244, 301, 309, 277, 'amber', 4, arrow=True),
          line(366, 367, 342, 323, 'amber', 4, arrow=True),
          text(52, 471, 'Gravity plus matric-pressure gradients', 22, 'muted'),
          text(52, 508, '∂θ/∂t = ∇·[K(θ) ∇(ψ + z)] − S', 26, 'ink'),
          text(52, 549, 'Requires retention and conductivity relations.', 20, 'muted')]
    for i in range(1, 5):
        p += [line(737, 189+i*39, 1036, 189+i*39, 'muted', 1, '5 4')]
    for y in [204, 247, 290]:
        p += [line(887, y, 887, y+30, 'teal', 4, arrow=True)]
    p += [rect(737, 352, 299, 80, 'blue', opacity=.6),
          line(718, 352, 1138, 352, 'blue', 2, '6 4'),
          text(1046, 340, 'h', 23, 'blue', 'bold'),
          line(1036, 406, 1134, 406, 'blue', 4, arrow=True),
          text(1046, 441, 'Network', 18, 'blue'),
          text(632, 471, 'Downward K(θ) flux; shared mobile head', 22, 'muted'),
          text(632, 508, 'Gᵢ dθᵢ/dt = Q_in,ᵢ − Q_out,ᵢ − Eᵢ', 25),
          text(632, 549, 'Green–Ampt entry; field-capacity drainage cutoff.', 20, 'muted'),
          rect(28, 611, 554, 179, '#eaf1f4', 12), rect(608, 611, 564, 179, '#eaf1f4', 12),
          text(52, 650, 'Useful when', 25, 'teal', 'bold'),
          text(52, 691, 'Vertical gravity drainage dominates in coarse,', 22, 'muted'),
          text(52, 722, 'relatively uniform media, and the objective is', 22, 'muted'),
          text(52, 753, 'network detention, discharge and control.', 22, 'muted'),
          text(632, 650, 'A richer model may be needed when', 25, 'amber', 'bold'),
          text(632, 691, 'Capillary barriers or upward supply matter,', 22, 'muted'),
          text(632, 722, 'or detailed moisture, root uptake and preferential', 22, 'muted'),
          text(632, 753, 'pathways determine performance. Check field data.', 22, 'muted')]
    return p


def interlayer_flux():
    p = [text(36, 48, 'Intercell flux: donor conductivity or matric-head gradient?', 30, weight='bold'),
         text(36, 84, 'Downward-positive coordinate ξ. Conceptual cells; illustrative parameter calculation.', 22, 'muted'),
         rect(28, 112, 554, 472, rx=14), rect(608, 112, 564, 472, rx=14),
         text(52, 153, 'Current storage-node kernel', 26, 'teal', 'bold'),
         text(632, 153, 'Tu, Wadzuk & Traver (2020)', 26, 'amber', 'bold')]
    for x in [52, 632]:
        p += [rect(x, 183, 210, 99, '#e2d5bd', 8), rect(x, 331, 210, 99, '#e2d5bd', 8),
              text(x+15, 218, 'Donor cell i', 22, weight='bold'),
              text(x+15, 254, 'Moisture θᵢ', 22),
              text(x+15, 367, 'Receiver i + 1', 22, weight='bold'),
              text(x+15, 403, 'Moisture θᵢ₊₁', 22)]
    p += [line(157, 285, 157, 324, 'teal', 6, arrow=True),
          text(287, 228, 'qᵢ = Kₛ exp[−m (φ − θ)]', 21, 'teal', 'bold'),
          text(287, 269, 'Above field capacity;', 20, 'muted'),
          text(287, 299, 'otherwise zero.', 20, 'muted'),
          text(287, 365, 'Receiver limits volume,', 20, 'muted'),
          text(287, 395, 'not the trial conductivity.', 20, 'muted'),
          text(52, 471, 'No adjacent-cell matric-head calculation.', 22, 'muted'),
          text(52, 508, '10 × exp[−10 (0.45 − 0.30)] = 2.23 mm/h', 21, 'teal', 'bold'),
          text(52, 550, 'Ks = 10 mm/h; θFC = 0.20; slope m = 10', 21, 'muted'),
          line(732, 287, 732, 326, 'teal', 5, arrow=True),
          line(770, 325, 770, 286, 'amber', 5, arrow=True),
          text(867, 223, 'q = K(θ) − D(θ) ∂θ/∂ξ', 24, 'amber', 'bold'),
          text(867, 263, 'D = K dψ/dθ', 23, 'amber'),
          text(867, 304, 'Matric gradients can', 20, 'muted'),
          text(867, 334, 'augment, oppose or', 20, 'muted'),
          text(867, 364, 'reverse gravity flow.', 20, 'muted'),
          text(632, 471, 'van Genuchten retention / conductivity', 22, 'muted'),
          text(632, 508, 'Different textures: match matric head', 22, 'muted'),
          text(632, 550, 'Implemented with SWMM pumps and controls', 21, 'muted'),
          rect(28, 607, 1144, 183, '#eaf1f4', 12),
          text(52, 646, 'Surface → media: modified Green–Ampt entry', 26, 'teal', 'bold'),
          text(52, 687, 'f_cap = Ks [1 + (suction + ponded head) Δθ / F]     (existing front, F > 0)', 25),
          text(52, 726, 'ΔV = min(potential volume, donor water, receiving space); δt ≤ 1 s.', 23, 'muted'),
          text(52, 763, 'Accepted entry advances F. A submerged receiver routes to shared mobile storage.', 21, 'muted')]
    return p



def resaturation_frame(frame):
    root = ASSETS.parent / 'results'
    meta = json.loads((root/'resaturation_cycle.json').read_text())
    assert hashlib.sha256((root/'resaturation_cycle.csv').read_bytes()).hexdigest() == meta['csv_sha256']
    rows = [{k:float(v) for k,v in r.items()} for r in csv.DictReader((root/'resaturation_cycle.csv').open())]
    index = round(frame/63*(len(rows)-1)); r = rows[index]; sec = r['seconds']
    stage = min(2, sec/10) if sec <= 100 else max(0, 2-(sec-100)/10)
    phase = 'Reverse inflow wets media' if sec < 100 else ('Boundary falls; mobile water drains' if sec < 180 else ('Second surface pulse infiltrates' if sec < 241 else 'Recession after the second event'))
    p = [text(36,48,'Backwater, recession, second storm: one continuous history.',30,weight='bold'),
         text(36,84,'Native six-minute test · 10 ft² facility · clean reverse water · 0.1-s routing',22,'muted'),
         rect(28,112,514,635,rx=14),rect(566,112,606,635,rx=14),
         text(52,150,f'{sec/60:.2f} min: {phase}',19,'teal','bold')]
    x, y, w, scale = 100, 195, 150, 150
    bottom = y+300
    p += [rect(x,y,w,75,'#eef4e5'),rect(x,y+75,w,150,'#e2d5bd'),rect(x,y+225,w,75,'#b7c5cc'),
          text(266,y+38,'Surface',20),text(266,y+126,'Media',20),text(266,y+254,'Aggregate',20),
          rect(x,bottom-min(2,r['head'])*scale,w,min(2,r['head'])*scale,'blue',opacity=.55),
          rect(x,y,w,300,'none',stroke='ink'),
          line(x-24,y+75,x+w+13,y+75,'amber',2,'6 4'),
          text(52,526,'Blue: connected water below mobile table',18,'blue'),
          text(52,562,f"Media retained θ = {r['media_theta']:.3f}",22),
          text(52,597,f"Upper-zone deficit Δθ = {r['IMD']:.3f}",22,'amber','bold'),
          text(52,633,f"Surface front history F = {r['F_ft']:.4f} ft",20,'teal'),
          text(52,669,'Backflow does not increment F.',21,'muted'),
          text(52,707,'Full resaturation clears the old front.',21,'muted')]
    # Physical bottom port; flow sign is from the native engine, not stage alone.
    p += [rect(431,y,65,300,'#edf5f8'),rect(431,bottom-stage*scale,65,stage*scale,'blue',opacity=.45),
          text(464,181,'O',21,'blue','bold',anchor='middle'),
          text(431,521,f'{stage:.2f} ft',18,'blue')]
    sign = 1 if r['flow_cfs']>=0 else -1
    c = 'blue' if sign>0 else 'amber'
    if abs(r['flow_cfs']) > .00001:
        start,end = (250,423) if sign>0 else (423,250)
        p += [line(start,bottom-12,end,bottom-12,c,4,arrow=True)]
        dot = 250+(frame*.27%1)*173
        if sign<0: dot=673-dot
        p += [circle(dot,bottom-12,4,c)]
    p += [text(342,465,f"D: {r['flow_cfs']:+.3f} cfs",17,c,'bold',anchor='middle')]
    px,pw=639,490
    def xx(s):return px+s/360*pw
    for gy,gh,key,maximum,label,c in [(183,202,'head',2,'Mobile water-table depth (ft)','blue'),(490,170,'IMD',.4,'Green–Ampt upper-zone deficit','amber')]:
        def yy(v):return gy+gh*(1-v/maximum)
        p += [text(591,gy-20,label,23,weight='bold'),rect(xx(180),gy,xx(241)-xx(180),gh,'teal',opacity=.06)]
        for v in [0,maximum/2,maximum]:
            p += [line(px,yy(v),px+pw,yy(v),'gray',1),text(px-14,yy(v)+5,f'{v:g}',17,'muted',anchor='end')]
        for minute in range(7):
            p += [text(xx(minute*60),gy+gh+27,str(minute),17,'muted',anchor='middle')]
        all_points=' '.join(f'{xx(q["seconds"]):.2f},{yy(q[key]):.2f}' for q in rows)
        past_points=' '.join(f'{xx(q["seconds"]):.2f},{yy(q[key]):.2f}' for q in rows[:index+1])
        p += [f'<polyline points="{all_points}" fill="none" stroke="#dce6eb" stroke-width="2"/>',
              f'<polyline points="{past_points}" fill="none" stroke="{color(c)}" stroke-width="4"/>',
              line(xx(sec),gy,xx(sec),gy+gh,'ink',1,'4 4'),circle(xx(sec),yy(r[key]),5,c)]
        if key=='head':
            p += [line(px,yy(1.5),px+pw,yy(1.5),'amber',2,'6 4'),text(px+pw-5,yy(1.5)-9,'Media top = 1.5 ft',17,'amber',anchor='end')]
    p += [text(884,722,'Elapsed time (min); shaded band = second inflow',19,'muted',anchor='middle'),
          text(36,785,'Finite-zone reconciliation follows physical wetness; it does not resolve upward capillary flow.',22,'muted')]
    return p

def main():
    save('water-stores', water_stores())
    save('interlayer-flux', interlayer_flux())
    save('treatment-formulation', treatment())
    save('richards-approximation', richards())
    save('hydraulic-control-static', hydraulic_frame(16+15))
    FRAMES.mkdir(parents=True, exist_ok=True)
    frames = [save(f'frame-{i:03d}', hydraulic_frame(i), directory=FRAMES) for i in range(64)]
    subprocess.run(['magick', '-delay', '16', *map(str, frames), '-loop', '0',
                    '-layers', 'Optimize', str(ASSETS/'hydraulic-control.gif')], check=True)
    save('resaturation-static', resaturation_frame(37))
    cycle_frames = [save(f'cycle-{i:03d}', resaturation_frame(i), directory=FRAMES) for i in range(64)]
    subprocess.run(['magick', '-delay', '16', *map(str, cycle_frames), '-loop', '0', '-layers', 'Optimize', str(ASSETS/'resaturation.gif')], check=True)
    manual=ASSETS.parents[2]/'manual/images'
    for stem in ['water-stores','interlayer-flux','richards-approximation','treatment-formulation','hydraulic-control-static','resaturation-static']:
        shutil.copy2(ASSETS/(stem+'.png'),manual/('t10_'+stem+'.png'))
    shutil.copy2(ASSETS/'hydraulic-control.gif',manual/'t10_hydraulic-control.gif')
    shutil.copy2(ASSETS/'resaturation.gif',manual/'t10_resaturation.gif')
    assert math.isclose(.20*1000, 200)
    assert math.isclose(.40*1000*.70, 280)
    assert math.isclose(100*(1-math.exp(-2*6/24)), 39.3469340287, abs_tol=1e-8)
    print('Created six formulation figures and two 10.24-second GIFs with static alternatives.')


if __name__ == '__main__':
    main()
