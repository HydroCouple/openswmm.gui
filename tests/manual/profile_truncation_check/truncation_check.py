#!/usr/bin/env python3
"""Reproduction of the 2D-profile water-band truncation at the shoreline.

Transliterates MeshProfileInterp::bridgedTops (include/plot/meshprofileinterp.h)
and the run-termination geometry of paintWetBand
(src/plot/meshprofileplotwidget.cpp) into Python, then drives them with the
ideal post-CellSurfaceInterp field: a flat pool WSE over a linearly rising bed,
depth tapering continuously to zero at the true waterline.

Shows that even with a perfect tapering depth field, the painted band ends at
the LAST WET SAMPLE, not at the WSE/ground intersection - a vertical cliff of
height (WSE - ground) up to one sample step before the true shoreline.

Also demonstrates the proposed fix: a per-run boundary intercept computed by
linear crossing between the bracketing samples.

Output: truncation_check_results.txt next to this script.
"""

import math
import os

NAN = float("nan")
K_FILM = 1e-6  # MeshProfileInterp::kFilm


def bridged_tops(samples):
    """Direct transliteration of MeshProfileInterp::bridgedTops.
    samples: list of dicts {chainage, ground, depth, cellHasSurface}."""
    n = len(samples)
    top = [NAN] * n
    # 1) raw per-sample surface
    for i, s in enumerate(samples):
        if not math.isfinite(s["ground"]):
            continue
        t = s["ground"] + s["depth"]
        if t > s["ground"] + K_FILM:
            top[i] = t
    # 2) bridge dry gaps between wet runs (no-data gaps only)
    i = 0
    while i < n:
        if math.isnan(top[i]):
            i += 1
            continue
        run_end = i
        while run_end + 1 < n and not math.isnan(top[run_end + 1]):
            run_end += 1
        gap_start = run_end + 1
        nxt = gap_start
        blocked = False
        saw_no_data = False
        while nxt < n and math.isnan(top[nxt]):
            if not math.isfinite(samples[nxt]["ground"]):
                blocked = True
                break
            if not samples[nxt]["cellHasSurface"]:
                saw_no_data = True
            nxt += 1
        if not blocked and saw_no_data and nxt < n and gap_start < nxt:
            cl, wl = samples[run_end]["chainage"], top[run_end]
            cr, wr = samples[nxt]["chainage"], top[nxt]
            dc = cr - cl
            wse_up = max(wl, wr)
            for k in range(gap_start, nxt):
                if not math.isfinite(samples[k]["ground"]):
                    continue
                wse = wl + (samples[k]["chainage"] - cl) / dc * (wr - wl) if dc > 1e-12 else wl
                wse = min(wse, wse_up)
                if wse > samples[k]["ground"] + K_FILM:
                    top[k] = wse
        i = run_end + 1 if blocked else max(nxt, run_end + 1)
    return top


def wet_runs(samples, top):
    """paintWetBand's run walk: contiguous non-NaN stretches of top."""
    runs, i, n = [], 0, len(samples)
    while i < n:
        if math.isnan(top[i]):
            i += 1
            continue
        j = i
        while j < n and not math.isnan(top[j]):
            j += 1
        runs.append((i, j - 1))
        i = j
    return runs


def main():
    # Ideal post-fix field: pool WSE flat at 1.0, bed rising 0 -> 2 over 0..20.
    # CellSurfaceInterp tapers depth = max(0, WSE - ground) continuously, so the
    # true shoreline (depth -> 0) is exactly at ground == 1.0 -> chainage 10.0.
    wse, step, length = 1.0, 0.7, 20.0  # step deliberately not dividing 10.0
    ground = lambda c: c * 0.1  # noqa: E731
    samples = []
    c = 0.0
    while c <= length + 1e-9:
        g = ground(c)
        samples.append(
            {"chainage": c, "ground": g, "depth": max(0.0, wse - g), "cellHasSurface": True}
        )
        c += step

    top = bridged_tops(samples)
    runs = wet_runs(samples, top)

    lines = []
    lines.append("2D profile truncation reproduction")
    lines.append(f"WSE = {wse}, ground = 0.1*chainage, sample step = {step}")
    lines.append(f"True shoreline (WSE == ground): chainage = {wse / 0.1:.4f}")
    lines.append("")
    for a, b in runs:
        s_end = samples[b]
        cliff = top[b] - s_end["ground"]
        lines.append(f"Painted wet run: samples {a}..{b}")
        lines.append(f"  band ends at chainage {s_end['chainage']:.4f}")
        lines.append(f"  vertical cliff height (top - ground) at run end: {cliff:.4f}")
        gap = wse / 0.1 - s_end["chainage"]
        lines.append(f"  horizontal shortfall vs true shoreline: {gap:.4f} (up to one step = {step})")
        # Proposed fix: extrapolate the WET-side surface slope (last two wet
        # samples; the dry sample has no surface) and intersect it with the
        # ground segment [b, b+1], clamped to that segment.
        if b + 1 < len(samples) and math.isfinite(samples[b + 1]["ground"]):
            s_dry = samples[b + 1]
            dc_run = s_end["chainage"] - samples[b - 1]["chainage"] if b > a else 0.0
            m_top = ((top[b] - top[b - 1]) / dc_run) if dc_run > 1e-12 else 0.0
            dc_seg = s_dry["chainage"] - s_end["chainage"]
            m_grd = (s_dry["ground"] - s_end["ground"]) / dc_seg
            if m_grd - m_top > 1e-12:  # ground rises through the surface
                d = (top[b] - s_end["ground"]) / (m_grd - m_top)
                d = min(max(d, 0.0), dc_seg)  # clamp into the segment
                cx = s_end["chainage"] + d
                lines.append(f"  FIX intercept (wet-side extrapolation): chainage = {cx:.4f}"
                             f"  (error vs true = {abs(cx - wse / 0.1):.2e})")
        lines.append("")

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "truncation_check_results.txt")
    with open(out, "w") as f:
        f.write("\n".join(lines))
    print("\n".join(lines))


if __name__ == "__main__":
    main()
