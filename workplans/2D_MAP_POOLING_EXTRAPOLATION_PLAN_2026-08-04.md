# 2D Map — Pooling Extrapolation into Adjacent Dry Cells (2026-08-04)

## Reported artifact

In the depth **contour/map** visualization, a wet cell does not extend its free
surface into an adjacent dry cell on an adverse (rising) bed. The inundation
stops at the cell edge instead of tapering to the sub-cell waterline, so the
pooling wedge against a bank/levee is missing. Profiles read as truncated at the
same boundaries.

---

## Root cause — one field, three readers, two of them wrong

`SceneTri::dv0/dv1/dv2` is a **signed** depth `sd_v = η_v − z_v` whose value
**exactly 0 is a NO-DATA sentinel** (`swmm2dresultslayer.h:638-654`,
`vertexdepthreconstruct.h:10-27`). Only the profile path knows that.

Measured on a two-cell strip (flat reach z=0 holding a pool at η=5; adjacent
bank cell rising z=0→8 that the solver marks dry; true waterline at x=16.25),
driving the **real** headers:

```
   x   ground   PROFILE   MAP-fill   MAP-band   truth
  10.0    0.00     5.000      5.000      5.000   5.000   <- cell edge
  11.0    0.80     4.200      0.000      4.500   4.200
  14.0    3.20     1.800      0.000      3.000   1.800
  16.0    4.80     0.200      0.000      2.000   0.200
  17.0    5.60     0.000      0.000      1.500   0.000   <- bed above pool
  20.0    8.00     0.000      0.000      0.000   0.000
```

- **PROFILE** (`CellSurfaceInterp::depthAt`) is exact. It extrapolates in η space
  and lands the waterline on the bed intercept. **No defect here.**
- **MAP-fill** — the three QSG fill passes and the CPU painter twin gate on the
  **cell-mean** depth: `if (t.depth < dryDepth) continue;`
  (`swmm2dresultsqsgrenderer.cpp:1242, 1359, 1528`; `swmm2dresultslayer.cpp:592`).
  A solver-dry cell emits no geometry at all → hard truncation at the cell edge.
  **This is the reported artifact.**
- **MAP-band** — the marching-triangles bands and isolines are ungated but read
  the 0 sentinel as a literal depth of 0 and interpolate it linearly. The
  waterline is dragged to the dry *vertex*: 2.0 m of water painted at x=16 where
  truth is 0.2 m, and 1.5 m at x=17 where the **bed is above the pool**. The band
  path is therefore both over-extended and quantitatively wrong today.

### The scalar that is linear on a triangle is η, not clamped depth

Bed `z` is linear on a triangle and the extrapolated `η` is constant in the dry
direction, so `sd = η − z` is **linear** — the marching algorithms' native
assumption. Feeding them the sentinel breaks that; feeding them the extrapolated
(possibly negative) signed depth restores it exactly. In the case above,
interpolating `(5, 5, −3)` crosses 0 at x = 16.25 — identical to the profile's
constant-η surface, analytically, not approximately.

---

## Fix — extrapolate the sentinel corners once, per triangle

Because every consumer reads the per-triangle copies `t.dv0/dv1/dv2` (fanned out
at `swmm2dresultslayer.cpp:2477-2483`), the whole correction is **one pass at the
end of `applyCurrentDepths_()`**:

```
for each triangle i:
  nWet = count(sd_k > 0)
  if nWet == 0 or nWet == 3: leave unchanged      // fully dry / fully wet: bit-identical
  maxEta = max over WET corners of (z_k + sd_k)   // driving head
  for each non-wet corner k:  sd_k := maxEta − z_k
```

Properties, all of them load-bearing:

1. **One-cell halo, self-limiting.** `dv` is per-triangle, so a cell extrapolates
   only from its own wet corners. Water cannot propagate cell-to-cell into
   ground the solver never wetted (the flood-fill alternative was rejected).
2. **Bounded by the driving head.** Every extrapolated corner sits exactly on the
   pool surface `maxEta`; nothing can rise above the water that supplies it.
3. **Bands and isolines become exact for free** — no extractor changes. The
   lowest band clips at `levels.front() == dryDepth`, i.e. the true intercept.
4. **Fully-wet and fully-dry cells are byte-unchanged**, so the common case
   carries zero regression risk and zero cost.
5. **No new storage.** No extra `SceneTri` fields (12 B/tri × 1M cells avoided),
   no extra allocation; one O(nTri) pass per tick alongside the existing one.
6. **`cellHasSurface()` is unchanged** — it tests `dv != 0`, and only cells that
   already had a wet corner are touched.

### Effect on the profile path

`CellSurfaceInterp::depthAt` admits a corner only when `sd > 0`, so an
extrapolated corner **below** the pool (`maxEta − z_k < 0`, the canonical bank
case) is non-supplying exactly as the sentinel was → profile output unchanged.

The one case that does change: a no-data corner whose **bed is below** the pool
(`maxEta − z_k > 0`). It now supplies η = `maxEta`, which is the driving head and
the cap, so the result stays within the physical band — and it moves the profile
*toward* the map rather than away. This is the intended unification: map and
profile must show one surface. Pinned by a test rather than left implicit.

### Fill passes — widen the gate, don't replace it

`if (t.depth < dryDepth) continue;` becomes

```
if (t.depth < dryDepth && !(dv0 >= dryDepth || dv1 >= dryDepth || dv2 >= dryDepth))
    continue;
```

Strictly additive: every cell that painted before still paints. Gouraud fills
interpolate vertex colours and cannot clip a triangle at the waterline, so
sub-`dryDepth` values must render fully transparent and the shoreline there is a
fade, not a cut. **The exact shoreline is the contour-band path's job** — which
is what actually provides the depth fill since the ramp heatmap was retired
(`swmm2dresultslayer.cpp:229-232`).

### Indexed smooth fill — shared-vertex collision

`swmm2dresultsqsgrenderer.cpp:1530` writes `t.dv0` into a **shared** vertex slot,
so incident cells collide (last-writer-wins). Benign today because all incident
cells agree on a vertex's sentinel; with per-corner extrapolation they disagree.
Resolve with a **max-reduce** over incident cells — the deepest claim wins, which
is the same driving-head logic one level up.

---

## Phases

### Phase 1 — Reproducing test (red)

`tests/gui/test_map_pooling_extrapolation.cpp`, leaf-testable against the real
headers (`vertexdepthreconstruct.h` + `cellsurfaceinterp.h`), fixture = the
two-cell strip above. Series written to `test_artifacts/` per CLAUDE.md §4.1.

1. **Band waterline** — linear interpolation of the extrapolated corner values
   crosses `dryDepth` at the bed intercept (x = 16.25), not at the crest vertex.
   *Fails today* (crosses at x = 20).
2. **No water above the pool** — every sampled point with `ground > η` yields a
   value `< dryDepth`. *Fails today* (1.5 m at x = 17).
3. **Fill gate** — the dry bank cell passes the widened gate; a cell with no wet
   corner still does not. *Fails today.*
4. **Fully-wet cells byte-identical**; **fully-dry cells byte-identical**.
5. **Profile unchanged on the canonical bank** — `CellSurfaceInterp::depthAt`
   returns bitwise the same values before and after extrapolation.
6. **Driving-head bound** — no extrapolated corner η exceeds `maxEta`.

### Phase 2 — `applyCurrentDepths_` extrapolation (green)

Helper `extrapolateDryCorners` added to `include/layers/vertexdepthreconstruct.h`
(header-only, leaf-testable — same pattern as the rest of that file); called once
at the end of `applyCurrentDepths_()`. Gate: Phase 1 green.

### Phase 3 — Fill gates + indexed max-reduce

`swmm2dresultsqsgrenderer.cpp:1242, 1359, 1528` (+ max-reduce at :1530) and the
CPU twin `swmm2dresultslayer.cpp:592`.

### Phase 4 — Verification

1. `ctest -L gui` full suite.
2. Standalone probe refreshed under `tests/verification/` so the numbers above
   stay reproducible with a bare compiler.
3. Visual: scrub a model with a levee/bank and confirm the wedge tapers into the
   dry cell and stops at the bed intercept, with band and profile agreeing.
4. `CHANGELOG.md` entry at release per §5.2.

---

## Decisions locked (user, 2026-08-04)

1. **Always on** — this is a bug fix, not a preference; no new toggle or sidecar
   state. All map paths adopt the η-space surface the profile already uses.
2. **One-cell halo** — extrapolate only into cells sharing a wet vertex. Rejected:
   flood-fill across consecutive dry cells (can invent water the solver never
   produced, needs per-frame connectivity traversal).
3. Exact sub-cell shoreline is delivered by the marching band/isoline path; the
   Gouraud fills get consistency and a soft edge, not a hard cut.

---

## EXECUTION RECORD (2026-08-04)

**Status: IMPLEMENTED — 170/170 tests green (124 gui + 46 unit). Uncommitted.**

Two corrections to the plan as written; algorithm and decisions unchanged.

1. **`swmm2dresultslayer.cpp:592` is not a fill gate.** It selects which cells
   receive depth *labels*. Labelling a solver-dry cell would be wrong, so it
   keeps the cell-mean test. The CPU painter has no depth-fill gate to widen —
   its bands *are* the fill and were already ungated, so it inherits the fix
   through `dv` with no edit at all.

2. **The indexed max-reduce was rejected, not implemented.** Both indexed
   passes (`swmm2dresultsqsgrenderer.cpp:1359, 1528`) write into buffers keyed
   by SHARED vertex, where per-corner surfaces cannot be represented. A
   max-reduce is not merely lossy but actively harmful: at a ridge vertex shared
   by a deep pool and a thin film on the far side it stamps the deep pool's
   driving head onto the film's triangle — the exact artifact class iteration 2
   of the profile plan fought. Both keep the strict cell-mean gate, with the
   reason recorded inline. Only the *expanded* per-vertex fill got the widened
   gate; the pooling wedge itself is delivered by the marching band/isoline
   passes, which carry per-triangle values and are immune.

**Verification.** `tests/verification/pooling_extrapolation_check.cpp` — unlike
`adverse_slope_profile_check.cpp` it *includes* the production header rather
than copying it (`vertexdepthreconstruct.h` needs only the standard library), so
the recorded numbers cannot drift:

```
    x   ground   BEFORE     AFTER    truth
  16.0    4.80     2.000     0.200   0.200
  17.0    5.60     1.500     0.000   0.000   <- bed above the pool
  painted waterline:  BEFORE x = 20.000   AFTER x = 16.250   true x = 16.25
```

Files: `include/layers/vertexdepthreconstruct.h` (+`extrapolateDryCorners`),
`src/layers/swmm2dresultslayer.cpp` (call site in `applyCurrentDepths_`),
`include/layers/swmm2dresultslayer.h` (`SceneTri::dv*` contract),
`src/map/swmm2dresultsqsgrenderer.cpp` (`cellPaints` + three gate comments),
`tests/gui/test_map_pooling_extrapolation.cpp` (new, 7 cases),
`tests/gui/CMakeLists.txt`, `tests/verification/pooling_extrapolation_check.cpp`.

**Remaining (visual, user-side):** scrub a levee/bank model and confirm the band
wedge tapers into the dry cell and stops at the bed intercept. The smooth-fill
sublayer still truncates at the cell edge when the indexed path is active — by
design, per correction 2.
