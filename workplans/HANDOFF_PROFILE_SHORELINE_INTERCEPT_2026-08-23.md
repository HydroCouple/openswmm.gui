# HANDOFF — 2D Profile Shoreline Intercept (Premature Truncation Fix) — 2026-08-23

**Status: IMPLEMENTED, NOT COMPILED.** The implementing session had no Qt
toolchain; your job is to compile, run the tests, fix anything that surfaces,
and do the visual verification. All edits are uncommitted.

## Context

Follow-up to `2D_PROFILE_WSE_EXTRAPOLATION_PLAN_2026-08-02.md`. That plan fixed
the depth *field* (`CellSurfaceInterp::depthAt` tapers depth to the sub-cell
waterline), but the *painter* still ended each wet run at the last sample that
happened to be wet — up to one resample step (~half a cell) short of the true
WSE/ground intersection, closing with a vertical cliff of height (WSE − ground).
That is the "water level truncated prematurely before intersecting the
interpolated ground" artifact.

Reproduction (transliterated `bridgedTops` + `paintWetBand` geometry driven with
a perfectly tapering depth field — proves the truncation is painter-side):
`tests/manual/profile_truncation_check/truncation_check.py` (run with python3;
writes `truncation_check_results.txt` next to itself).

## What was changed

1. **`include/plot/meshprofileinterp.h`** — new header-only helper
   `MeshProfileInterp::shorelineIntercept(s, top, runFirst, runLast, trailing,
   &chainage, &elev)`. Extrapolates the surface from the run's two
   boundary-most wet samples (flat for a single-sample run) and intersects it
   with the ground segment toward the adjacent dry sample. Returns an intercept
   only when ground rises through the surface within that segment (`t ∈ (0,1]`).
   No intercept at: data edge, off-mesh (NaN-ground) neighbour, or a dry
   neighbour whose ground stays below the surface (no-data termination — the
   hard edge there is intentional, e.g. the split flank of an unbridged pool
   over a low bench).

2. **`src/plot/meshprofileplotwidget.cpp`** — `paintWetBand` (anonymous
   namespace, ~line 749) now prepends/appends the leading/trailing intercept
   point to `topPoly` before the fill/stroke. The intercept elevation lies on
   the drawn ground segment by construction, so the fill's closing edges hug
   the ground line and the band tapers to a point. Both the current-depth pass
   and the max-envelope pass go through `paintWetBand`, so both get the taper.
   The animation path is covered for free: `setCurrentDepths` mutates
   `depthNow` and the painter recomputes `bridgedTops` + intercepts each paint.

3. **`tests/gui/test_meshprofileinterp.cpp`** — six new slots:
   `trailing_intercept_lands_on_ground`, `leading_intercept_lands_on_ground`,
   `sloped_surface_intercept_extrapolates_wet_side`,
   `no_intercept_when_ground_stays_below_surface`,
   `no_intercept_at_offmesh_or_data_edge`, `single_sample_run_extends_flat`.
   Expected values were validated numerically against a transliteration of the
   helper (all six exact). No CMake changes needed — the test target exists.

## Your tasks

### 1. Compile

```bash
cd openswmm.gui
cmake --preset=Darwin          # or Linux/Windows per platform
cmake --build --preset=Darwin
```

(The existing `build/` used Ninja + Qt 6.9.3 at `~/Qt/6.9.3/macos`.)

Likely friction points if it doesn't compile cleanly:
- `QPolygonF::prepend` — QPolygonF derives from QList<QPointF> in Qt 6, so
  `prepend` exists; if the build disagrees, use `topPoly.insert(0, ...)`.
- Include order / `std::isnan` etc. — `meshprofileinterp.h` already includes
  `<cmath>`; the new helper uses nothing new.

### 2. Unit tests

```bash
cd build && ctest -L gui --output-on-failure
```

Required green: `test_meshprofileinterp` (all pre-existing + 6 new slots),
`test_profile_wse_extrapolation`, and the full gui label (163+ tests were green
before this change; nothing outside the three touched files should move).

If a NEW test fails, first re-derive the expected number by hand from the
helper's contract (t = (top_i − g_i) / ((g_k − g_i) − mTop·dSeg)) before
touching the implementation — the expectations were machine-checked, so a
mismatch most likely means the C++ diverged from the contract (typo), not that
the numbers are wrong.

If a PRE-EXISTING test fails, suspect the `paintWetBand` edit: the intercept
must only ever ADD points at run ends, never change run detection or drop
samples. Diff against the description in §What-was-changed item 2.

### 3. Visual verification (needs a human or screenshot loop)

Open a model with a receding shoreline / steep bank (the plan used a
steep-bank valley model), draw a 2D profile across the waterline, and confirm:
- the water band and WSE line taper to a point exactly on the ground line
  instead of ending with a vertical cliff before it;
- scrubbing the animation keeps the taper tracking the moving shoreline;
- the max-depth envelope also tapers to the ground;
- zooming in at the shoreline: the fill's closing edge lies ON the ground line
  (the intercept is collinear with the ground segment by construction);
- two pools split by a dry crest (if such a model is at hand) each taper on
  the crest flanks where ground rises through their surface, and pools over a
  low no-data bench keep their hard edge (intended — see helper doc).

### 4. Wrap-up

- Re-run `python3 tests/manual/profile_truncation_check/truncation_check.py`
  and keep the results file current (it documents the pre-fix geometry).
- Do NOT update CHANGELOG.md now — per CLAUDE.md §5.2 it is updated at release.
- Report: build result, ctest summary, any deviations you had to make (with
  reasons), and screenshots of the shoreline before/after if available.

## Decisions locked (do not reopen without user sign-off)

1. Intercept computed **painter-side** at paint time (not sampler-side
   bisection of `depthAtCellInterp`) — no API changes, per-tick correct.
2. **Wet-side extrapolation** of the surface slope; the dry sample never
   supplies a surface endpoint.
3. Intercept only when ground **rises through** the surface inside the
   boundary segment; all other run ends keep today's hard edge.
