# 2D Profile — Physically Consistent Free-Surface Extrapolation (2026-08-02)

## Reported artifact

In the 2D longitudinal profile plot: (a) the water surface **climbs up walls / steep
banks** instead of staying level, and (b) the water band **truncates abruptly** at
partially-wet and dry cells instead of tapering to a shoreline.

---

## Root cause

### The reconstruction contract

Free surface η is reconstructed per-vertex as a depth-weighted mean of the incident
**wet** cells' VFR free surfaces, and stored as a *signed depth* `sd_v = η_v − z_v`:

- engine: `openswmm.engine/src/engine/2d/mesh/VertexReconstruction.cpp:180-220`
- GUI fallback: `openswmm.gui/src/layers/swmm2dresultslayer.cpp:2202-2255`

Three vertex states are encoded:

| `sd_v`  | meaning                                                              |
|---------|----------------------------------------------------------------------|
| `> 0`   | wet — η valid, water stands above the bed                            |
| `< 0`   | dry side of a partially-wet cell — **η valid**, surface below the bed |
| `== 0`  | **no data** — no wet incident cell; η is undefined                     |

The `== 0` case is a deliberate sentinel, documented at
`openswmm.engine/include/openswmm/plugin_sdk/SimulationSnapshot.hpp:239`
("0 with no wet incident cell") and written verbatim to HDF5 `/Mesh2_node_depth`
(`Default2DOutputPlugin.cpp:449-458, 509-510`).

### Defect 1 — the no-data sentinel is read as η = z (→ climbing walls)

`depthAtCellInterp` (`swmm2dresultslayer.cpp:2049-2094`) blends `dv0/dv1/dv2`
barycentrically with no state check. For a no-data vertex, `sd = 0` arithmetically
means **η = z**, so the interpolated surface is dragged from the true water level up
to the *bed elevation of the dry corner* — the wall crest. The bed leaks into η.

`clampToDrivingHead_` (`:2012-2047`) caps the blend at `max η` over vertices with
`sd > 0`, which suppresses the ramp only when every high corner is no-data. It does
**not** help when a high corner is genuinely wet (e.g. sheet flow on a road above a
channel): the cap then equals the crest η and the full ramp up the wall face is
licensed.

### Defect 2 — the cell-mean dry gate (→ truncation)

`depthAtCellInterp:2063-2065` returns 0 for any cell whose **mean** depth is below
`dry_depth_`. `dry_depth_` auto-tunes to `max(1e-5, 0.05·peakDepth)`
(`swmmvis.cpp:5162-5176`), so shoreline cells routinely fall under it. The band is
therefore chopped at a cell edge rather than at the sub-cell waterline — even though
the signed-depth field was introduced precisely to resolve that intercept.

`MeshProfileInterp::bridgedTops` (`meshprofileinterp.h:60-115`) then papers over the
hole with a chainage-linear ramp, a third non-physical surface.

### Blast radius (verified)

The gate at `:2063` is reached **only** from the profile path
(`meshprofilesampler.cpp:108`, `meshprofileplotdialog.cpp:199`). The profile is also
the only consumer that forms `ground + dv`; every map path uses `dv` as a bare
scalar. The three Gouraud/smooth fills carry their **own** independent gate
(`swmm2dresultsqsgrenderer.cpp:1242, 1359, 1528`) and are unaffected. The CPU/QSG
isoband + isoline passes are already ungated today and are out of scope.

### Engine: no change required

`reconstructVertexRenderDepths` is correct — it produces a wet-masked η field with an
explicit no-data sentinel and a no-new-maxima clamp (`VertexReconstruction.cpp:210-213`),
using the exact VFR inverse (`vfrEtaFromMeanDepth(..., eps=0)`). The GUI fallback
`reconstructVertexSignedDepths` mirrors it exactly. **Both stay as-is.**

> Rejected alternative: re-encode no-data as NaN instead of `0.0` to remove the
> (measure-zero) ambiguity with a genuinely wet vertex at η = z exactly. Costs an
> HDF5 semantic break, a `Mesh2DH5Reader` change, and inversion of the
> `!isfinite → 0` sanitiser at `swmm2dresultslayer.cpp:2624-2625`, for no observable
> benefit. Not doing it.

---

## Fix — constant free-surface extrapolation into no-data corners

Standard wetting/drying reconstruction: **bed elevation never stands in for η.** Within
a cell, fill no-data corners by extending the surface from the valid corners with
**zero gradient in the dry direction**, then take `depth = max(0, η − z)` so the
waterline lands exactly at the bed intercept.

New helper on `SWMM2DResultsLayer`, replacing the body of `depthAtCellInterp` and
reused by `maxDepthAtSceneInterp`:

```
surfaceAtCell(idx, scenePt, sd0, sd1, sd2) -> depth
  1. barycentric weights (w,v,u) against (a,b,c)   [unchanged construction]
  2. η_k = z_k + sd_k ;  valid_k = (sd_k != 0)
  3. nValid == 0                 -> return 0        (cell is dry)
     nValid == 3                 -> η = w·η0 + v·η1 + u·η2
     nValid == 1  (valid p)      -> η = η_p                       (constant surface)
     nValid == 2  (valid p,q)    -> fill the no-data corner r with
                                      η_r = η_p + (η_q − η_p)·t,
                                      t = ((r−p)·(q−p)) / |q−p|²
                                    then blend all three
                                    [gradient parallel to pq  ==  zero transverse
                                     gradient  ==  no surface slope toward the dry side]
  4. η = min(η, max over VALID corners of η_k)      (driving-head cap, retained;
                                                     now guards only extrapolating
                                                     weights at/outside the edge)
  5. z_interp = w·z0 + v·z1 + u·z2
     return max(0, η − z_interp)
```

Step 4 changes `clampToDrivingHead_`'s admission test from `sd > 0` to `sd != 0`.
Including negative-`sd` corners cannot raise the max (their η is below their bed and
below the water), so this is a semantic clarification, not a behaviour change — but it
makes the "valid η" concept single-sourced across the function.

**The cell-mean dry gate at `:2063-2065` is removed.** Dryness is now decided by
step 3's `nValid == 0`, which is the vertex-scoped statement of the same condition:
a cell with no wet cell anywhere in its vertex stars has no valid η and paints
nothing. A cell the solver calls dry but whose corners all carry valid η now paints
the correct sliver tapering to the bed intercept — that sliver is the shoreline the
truncation was hiding.

**Bridging** (`meshprofileinterp.h`) is restricted to true no-data gaps. With a correct
η field, a shoreline is no longer a gap, so the only remaining gaps are off-mesh
(NaN ground) or all-corners-no-data runs. Concretely: bridge only when the gap is
bounded on both sides by wet runs **and** every sample in it has finite ground **and**
at least one sample in it sits in a cell with `nValid == 0` — otherwise leave it dry.
Two pools separated by a dry crest then each read flat at their own level instead of
being joined by a ramp.

### Known residual (not in scope)

Where a vertex is shared between a deep channel cell and a shallow overbank cell, the
depth-weighted mean η smears across the bed step — inherent to vertex averaging, present
in the engine field too. The step-4 cap bounds it. If it proves visible after this
change, the follow-up is a per-cell no-new-maxima clamp against the cell's own
`cellEtaFromMeanDepth(h_i, z0, z1, z2)`. **Deferred until observed** — not
speculatively implemented.

---

## Phases

### Phase 1 — Reproducing tests (red)

**Files**: `tests/gui/test_meshprofileinterp.cpp` (extend), new
`tests/gui/test_profile_wse_extrapolation.cpp`.

Synthetic 2-cell and 3-cell fixtures driving `depthAtCellInterp` directly, per
CLAUDE.md §4.1 written to `test_artifacts/` so the geometry and sampled series are
reviewable:

1. **Wall climb** — channel cell + wall cell sharing an edge; wall-crest vertex wet
   from a shallow cell above. Assert the sampled WSE across the wall cell is
   **monotone non-increasing toward the wall** and never exceeds the channel η.
   *Fails today.*
2. **Shoreline taper** — a cell with 2 wet corners and 1 no-data corner, cell mean
   below `dry_depth_`. Assert depth decreases smoothly to 0 at the interior point
   where `η = z`, and that the last wet sample is strictly inside the cell.
   *Fails today* (returns 0 for the whole cell).
3. **Truly dry** — all three corners no-data. Assert depth 0 everywhere. *Passes today,
   must keep passing.*
4. **Fully wet** — all corners wet. Assert bit-identical output to the current
   implementation (no regression on the common case).
5. **Two pools, dry crest** — assert `bridgedTops` leaves the crest dry and each pool
   flat at its own level. *Fails today* (ramps).

Gate: all five compile and 1/2/5 fail for the stated reason.

### Phase 2 — `surfaceAtCell` + gate removal (green)

**Files**: `src/layers/swmm2dresultslayer.cpp`, `include/layers/swmm2dresultslayer.h`.

Implement the algorithm above; rewire `depthAtCellInterp` and `maxDepthAtSceneInterp`
onto it; delete the cell-mean gate; retire `clampToDrivingHead_` into step 4.
`depthAtSceneInterp`, `velocityAtScene`, and every map path keep their existing gates
untouched (§3 — surgical).

Gate: Phase 1 green + full `ctest -L gui`.

### Phase 3 — Bridging restriction

**Files**: `include/plot/meshprofileinterp.h`, `include/plot/profilesection.h`
(add `Sample::cellHasSurface` set by the sampler so the painter can see `nValid == 0`
without re-entering the layer), `src/plot/meshprofilesampler.cpp`.

Gate: Phase 1 case 5 green + full `ctest -L gui`.

### Phase 4 — Verification

1. `ctest -L gui` full suite.
2. Visual smoke on a model with a steep bank/levee and a receding shoreline: scrub the
   animation, confirm the WSE is level across wall cells and tapers to the bed rather
   than stopping at a cell edge.
3. **Consistency check**: at mesh vertices, the profile WSE must equal
   `z_v + sd_v` from the layer field (the two coincide exactly at vertices by
   construction). Assert in a test rather than by eye.
4. Confirm the map Gouraud fill and contour rendering are pixel-unchanged (they do not
   route through the edited functions) — screenshot diff on one frame.
5. `CHANGELOG.md` entry at release per §5.2.

---

## Decisions locked

1. Extrapolation rule: **constant-η / zero-transverse-gradient**, per cell, at render time.
2. Dryness gate: **vertex-scoped (`nValid == 0`)**, cell-mean gate removed — profile path only.
3. Bridging: **true no-data gaps only**.
4. Engine: **unchanged** (its field is already correct; the GUI was misreading the sentinel).

---

## EXECUTION RECORD (2026-08-02)

**Status: IMPLEMENTED — all 163 tests green (117 gui + 46 unit). Uncommitted.**

Adjustments to the plan as written (structure only; algorithm/decisions unchanged):

1. **`surfaceAtCell` extracted to a header-only helper** `include/layers/cellsurfaceinterp.h`
   (`CellSurfaceInterp::depthAt`) instead of a private layer method. Reason: the real
   `SWMM2DResultsLayer` cannot be linked from a leaf test (see the
   `test_2dresults_vizfixes` note at tests/gui/CMakeLists.txt) — the helper is shared by
   the layer and `test_profile_wse_extrapolation` (same pattern as `meshprofileinterp.h`).
2. **The envelope's own cell-mean mask removed too**: `meshprofilesampler.cpp` masked
   `maxDepth` by `perCellMax[tri] >= dry` — the same Defect-2 truncation in envelope
   form. `vertMax`'s 0-sentinel + vertex-scoped dryness now covers it; the orphaned
   `SWMM2DResultsLayer::maxDepthPerCell()` was deleted (profile build also drops a full
   per-frame time sweep).
3. **Per-tick refresh carries the flag**: `Sample::cellHasSurface` is frame-dependent, so
   `MeshProfilePlotDialog::refreshCurrentDepths` / `MeshProfilePlotWidget::setCurrentDepths`
   gained an optional parallel `QVector<bool>`.

Files: include/layers/cellsurfaceinterp.h (new), src/layers/swmm2dresultslayer.cpp
(+ header: gate + `clampToDrivingHead_` deleted, `cellHasSurface(int)` added),
include/plot/profilesection.h (`Sample::cellHasSurface`), include/plot/meshprofileinterp.h
(bridge only gaps containing a no-surface sample), src/plot/meshprofilesampler.cpp,
src/plot/meshprofileplotwidget.{h,cpp}, src/ui/dialogs/meshprofileplotdialog.cpp,
tests/gui/test_profile_wse_extrapolation.cpp (new, 8 cases incl. fully-wet BIT-parity vs
legacy and exact-at-vertex consistency), tests/gui/test_meshprofileinterp.cpp (+2 pool/crest
cases), tests/gui/CMakeLists.txt (registration).

Sampled series artifacts: build/tests/gui/test_artifacts/profile_wse_extrapolation/
(wall_climb.csv shows WSE ≡ 2.0 to the y=0.4 intercept; shoreline_taper.csv the 0.5−y taper).

Remaining (visual, user-side): scrub a steep-bank model and confirm level WSE across wall
cells + shoreline taper; map fills/contours don't route through the edited functions
(verified statically — the three QSG gates at swmm2dresultsqsgrenderer.cpp:1242/1359/1528
are untouched). NOTE: tests/verification/adverse_slope_profile_check.cpp still names
`clampToDrivingHead_` in comments (standalone emulation harness; not updated — historical).

## ITERATION 2 (2026-08-02, after visual review)

User screenshots showed two residual violations: (a) WSE still climbing banks
(valley model), (b) the pool surface notching DOWN along a wall face instead of
extending flat to the sub-cell intercept (crest model).

**Root cause of both: corners with sd < 0 (valid η below their own bed) were
admitted to the blend and the cap.** A below-bed corner carries NO standing
water at its corner, yet: a HIGH one whose η exceeds the pool (η = z + sd big
because z is big) raised the driving-head cap and pulled the blend up the wall
(a); a LOW one (a thin flank film pooled at the wall base stamps η ≈ base level
onto the wall-top vertex via the depth-weighted mean) dragged the blend below
the pool near the wall (b). The plan's assertion that negative-sd corners
"cannot raise the max" was wrong when z_k is large.

**Fix (cellsurfaceinterp.h only):** a corner SUPPLIES surface only when WET
(sd > 0); sd < 0 and sd == 0 are both non-supplying. nWet == 3 keeps the
bit-identical sd-space blend; otherwise η is the weight-RENORMALIZED average of
the wet corners' η (equals the plain lerp on an all-wet edge → continuous with
fully-wet neighbours; holds level toward non-supplying corners → flat to the
wall intercept). Cap admission reverted to wet corners (original semantics).
This supersedes the nValid==1/2 constant/projected-fill branches — the
renormalized blend generalizes both.

Semantic note: at a below-bed corner inside a partially wet cell the painted
depth is now the wet corners' flooded level (max(0, η_wet − z)), not
max(0, sd_v) — vertex/field consistency holds at WET vertices.

Tests: negativeSd_isValidNotNoData REWRITTEN → belowBedCorner_doesNotDragSurfaceDown;
new highBelowBedCorner_doesNotLicenseClimb (Fig 1 pin) and
lowBelowBedCorner_doesNotNotchPool (Fig 2 pin). 163/163 green.
