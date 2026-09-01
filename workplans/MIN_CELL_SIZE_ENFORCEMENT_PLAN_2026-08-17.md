# Minimum Cell Size Enforcement for Constrained Mesh Generation — GUI Plan (2026-08-17)

**Status:** IMPLEMENTED 2026-08-17 (uncommitted, **UNBUILT and UNTESTED** — no Qt toolchain was available to the implementing session). Production code for all six phases is in place; tests are not written. See `MIN_CELL_SIZE_TESTING_HANDOFF_2026-08-17.md` for what to build, what to test, and the four places the implementation deliberately deviates from this plan.

**Decisions (user-approved 2026-08-17):**
- Strategy = **both** pre-Triangle PSLG conditioning *and* a post-Triangle cleanup pass.
- Permitted geometry edits = snap-round vertices to an `h_min` grid; weld near-coincident vertices/endpoints; resample polylines to a minimum segment length; trim sharp corners at constraint junctions.
- Scope = **openswmm.gui only**. No engine changes; the engine keeps consuming whatever mesh it is given.

**Problem statement.** Constraint segments (conduit alignments, aux 3D breaklines, hole rings) drive Triangle to produce cells far below any usable size. On the 2D explicit marcher, one sliver sets the CFL timestep for the whole domain, so the smallest cell — not the mean — governs runtime. Today the pipeline has no lower bound of any kind: `GenerationOptions` exposes `maxArea` and `minAngle` only.

---

## 1. What already exists

Read before adding anything — several partial levers are already in place, and the new work must compose with them rather than duplicate them.

| Existing mechanism | File | What it does | Why it is not sufficient |
|---|---|---|---|
| RDP simplification (`pslgSimplifyEps`) | `src/mesh/pslgprep.cpp` — `simplifyPolyline`, `simplifyRing` | Drops near-collinear vertices within a perpendicular tolerance | Tolerance-based, not length-based. A tight zig-zag with large perpendicular deviation keeps every vertex, however short the segments. |
| Steiner snap + dedupe (`pslgSnapEps`) | `pslgprep.cpp` — `snapAndDedupe` | Grid-snaps and merges near-coincident **untagged** Steiner points | Points only. Does not touch constraint-segment vertices, which are where the constrained short edges live. |
| Node min separation (`nodeMinSeparation`) | `pslgprep.cpp` — `greedyMinSeparation` | Demotes crowded SWMM nodes from pinned vertices to cell coupling | Nodes only. Establishes the demotion idiom the new stages reuse. |
| Ring densification (`maxBoundaryEdgeLen`) | `pslgprep.cpp` — `densifyRing` | Splits long ring edges | Enforces a **maximum**, the opposite direction. |
| Terrain boundary buffer (`terrainBoundaryBuffer`) | `meshgenerationdialog.cpp:1506-1550` (`nearConstraint`, `nearMandatoryVertex`) | Rejects DTM Steiner candidates within a buffer of a constrained segment or mandatory vertex | Filters *terrain* points only; two constraint segments passing 5 cm apart are untouched. The grid + 3×3 scan idiom here is reused verbatim in §4. |
| Refinement hook | `include/mesh/trirefinehook.h`, `src/mesh/trirefinehook.cpp` | `triunsuitable()` bridge: cancellation, progress, **and a production-unused `targetAreaAt` size function** | No `src/` code assigns `targetAreaAt`; the only production install (`meshgenerationdialog.cpp:1627-1633`) sets `isCancelled` (1628) + `onProgress` (1629), so `maxArea` still goes through the global `-a` switch. The size function is exercised only in `tests/verification/trirefinehook_check.cpp` (108, 140, 194, 256, 291, 303) — so the machinery is proven, just unused. |
| Cell area stats | `include/mesh/meshcellstats.h` | `computeCellAreaStats` → min/max/mean/median, feeds the Metadata tab | Reporting only, post hoc. |

The one-line summary: **every existing knob either removes points or caps size from above. Nothing bounds size from below.**

---

## 2. Root cause — why constraints force small cells

This section is the load-bearing part of the plan. If it is wrong, everything downstream is wasted effort, which is why Phase 0 exists to test it empirically before any geometry is modified.

Ruppert's Delaunay refinement (what Triangle's `-q` implements) has a size guarantee that runs in the *unhelpful* direction: the output edge length near a point `p` is **Θ(lfs(p))**, where `lfs(p)` — the local feature size — is the radius of the smallest disk centred at `p` that touches two non-incident input features (input vertices or input segments). Triangle will not produce cells much larger than `lfs`, and it *cannot* produce cells much smaller either — it is doing exactly what the input asked for.

So sub-`h_min` cells arise from precisely four input conditions:

1. **Short constrained segments.** A constrained edge of length `L` is never split *or merged* — Triangle preserves input segments, so it forces mesh edges of length ≈ `L`. GIS-digitised conduit alignments and 3D breaklines routinely carry vertices centimetres apart.
2. **Close non-incident features.** Two polylines passing within `d`; a polyline within `d` of the domain boundary; a mandatory Steiner vertex within `d` of a segment it does not belong to. Then `lfs ≈ d/2` and cells collapse to that scale in the gap.
3. **Small input angles.** Two segments sharing an endpoint at angle `θ` below the quality bound — two conduits meeting at a manhole, a breakline grazing the boundary. Triangle handles these without looping forever (concentric shell splitting), but the cells at the apex are *necessarily* tiny: shell radii halve toward the vertex. **No amount of snapping fixes this**, because the two segments legitimately share that vertex. This is usually the dominant contributor and the reason corner trimming is in scope.
4. **Sub-scale rings and notches.** A hole ring narrower than `h_min`, or a boundary notch of that width, has `lfs < h_min` everywhere inside it.

Consequences that shape the design:

- **Enforcing `h_min` ⇔ enforcing `lfs ≥ c·h_min` on the conditioned PSLG.** That is a statement about the *input*, which is why conditioning is the primary lever and everything else is secondary.
- **An area floor in the size function cannot enforce a minimum.** Triangle only ever *refines*; it never coarsens. A floor prevents further subdivision, it does not enlarge a cell the input demanded. §5 keeps the floor because it is cheap and stops `minAngle`+`maxArea` from subdividing near features, but it must not be sold as the fix.
- **Post-mesh cleanup cannot fix input-demanded slivers either**, because the edges involved are constrained and must be protected (§6.6). Cleanup removes what Triangle *added*, conditioning removes what the input *demanded*. Both are needed; they address disjoint populations.

---

## 3. Architecture

Two new self-contained modules plus wiring. Both are pure value-type code with no Qt-GUI dependency, headless-unit-testable, matching the `meshboundarygraph` / `pslgprep` precedent.

```
collectInputs()  [main thread]
   └─ minCellSize + MinSizePolicy read from widgets

runMeshPipeline() [worker]
   1. boundary ingestion / dissolve            (unchanged)
   2. candidate filtering + markers            (unchanged)
   3. ▶ NEW: mesh::pslg::conditionMinSize()    ← Stages 1–4, §4
   4. hole ring prep                           (unchanged; consumes conditioned rings)
   5. PSLG assembly → MeshGenerator            (+ region maxArea clamp, §5)
   6. DTM sampling / terrain Steiner filter    (unchanged; buffer defaults derive from h)
   7. Triangle generate()                      (+ targetAreaAt floor, §5)
   8. ▶ NEW: mesh::collapseSubScaleCells()     ← §6.6 cleanup, before Hilbert reorder
   9. reorderMeshHilbert → elevation fill → node mapping → write   (unchanged)
```

Placement notes that matter:

- **Conditioning runs after marker assignment**, so tagged identity (conduit id ↔ marker, node ↔ marker) is already established and the conditioner can honour it as priority.
- **Conditioning runs before hole-ring prep**, so `prepareHoleRings` simplify/validate/densify sees conditioned rings and its existing self-intersection validation acts as a second, independent check on the conditioner's output.
- **Cleanup runs before `reorderMeshHilbert`** (`generate()` at `meshgenerationdialog.cpp:1637`, reorder at 1659, with only `result.ok`/cancel checks and `meanVertexIndexSpread` between). Reorder is a pure permutation; running cleanup after it would waste the locality it just established. Elevation fill (`keyOf` hash, 1696 and 1805) and node mapping (`mapNodesToMesh`, 2072) are coordinate-keyed, so they see the cleaned geometry with no further change.
- **The coupling-map pass is *marker*-keyed, not coordinate-keyed** (`meshgenerationdialog.cpp:2047-2057`, `in.nodeMarkerToTag.value(vertices[i].marker)`) — the comment at line 1654 is imprecise on this point and should be corrected in passing. Permutation-safe, but it means cleanup must never drop or alter a marker-bearing vertex. The §6.5 protection rule (no collapse of any edge with a `marker != 0` endpoint) is exactly what guarantees this, so the two must not drift apart.

MVC (CLAUDE.md §5.1): the conditioner and cleanup are model-layer free functions. The dialog is a controller reading widgets into `PipelineInputs`; no view holds min-size state.

### New files

**`include/mesh/pslgminsize.h` + `src/mesh/pslgminsize.cpp`**

```cpp
namespace mesh::pslg {

/*! Policy for minimum-feature-size conditioning. Every field defaults from
 *  minCellSize; overrides exist for the advanced expander only. */
struct MinSizePolicy
{
    double minCellSize     = 0.0;   ///< h, HORIZONTAL map units. 0 = conditioning off.
    double gridPitch       = -1.0;  ///< <0 = h / sqrt(2)  → max displacement h/2.
    double weldRadius      = -1.0;  ///< <0 = h.
    double minSegmentLen   = -1.0;  ///< <0 = h.
    double maxDeviation    = -1.0;  ///< <0 = h/2. Cap on shape change from resampling.
    double trimAngleDeg    = 30.0;  ///< Trim constraint corners sharper than this.
    bool   trimAtTaggedNodes   = false;  ///< See §4 Stage 4 rationale.
    bool   dropSubScaleHoles   = true;
    bool   collapseSubScalePaths = true;
    int    maxIterations   = 3;     ///< Snap-round / weld fixed-point cap.
};

enum class ViolationCause { ShortSegment, CloseFeatures, SmallAngle, SubScaleRing };

struct Violation { QPointF xy; double lfs; ViolationCause cause; QString tagA, tagB; };

struct ConditionReport
{
    int    verticesMoved = 0, verticesWelded = 0, segmentsSplit = 0;
    int    pathsCollapsed = 0, holesDropped = 0, cornersTrimmed = 0;
    int    crossingsRepaired = 0, iterationsUsed = 0;
    double maxDisplacement = 0.0;    ///< map units
    double domainAreaBefore = 0.0, domainAreaAfter = 0.0;
    double predictedMinLfs = 0.0;
    bool   conditioningAbandoned = false;  ///< fail-safe tripped; inputs unchanged
    QVector<Violation> residuals;          ///< capped (default 200), for the report UI
};

/*! Analysis only — no mutation. Returns the k worst lfs violations. Phase 0. */
[[nodiscard]] QVector<Violation> analyseLocalFeatureSize(
    const QVector<QPolygonF> &domains,
    const QVector<QVector<QPointF>> &holeRings,
    const QVector<ConstraintSegment> &segs,
    const QVector<SteinerPoint> &pts,
    double h, int maxReported = 200);

/*! Conditions the PSLG in place. Returns false only when the fail-safe tripped,
 *  in which case all arguments are left exactly as passed. */
bool conditionMinSize(QVector<QPolygonF> *domains,
                      QVector<QVector<QPointF>> *holeRings,
                      QVector<ConstraintSegment> *segs,
                      QVector<SteinerPoint> *pts,
                      const MinSizePolicy &policy,
                      ConditionReport *report,
                      const std::function<bool()> &isCancelled = {});
} // namespace mesh::pslg
```

**`include/mesh/meshminsizecleanup.h` + `src/mesh/meshminsizecleanup.cpp`** — §6.6.

**New in `pslgprep.h` / `pslgprep.cpp`** (siblings of the existing simplify/densify family, so they belong there rather than in the new module):

```cpp
/*! Length-based decimation: drop intermediate vertices until each retained
 *  chord is >= minLen, but never deviate more than maxDeviation from the
 *  original path. Endpoints always kept. Inverse of densifyRing. */
[[nodiscard]] QVector<QPointF> resampleMinLength(const QVector<QPointF> &pts,
                                                double minLen, double maxDeviation);
/*! Closed-ring variant: keeps the ring closed and preserves orientation. */
[[nodiscard]] QVector<QPointF> resampleRingMinLength(const QVector<QPointF> &ring,
                                                    double minLen, double maxDeviation);
```

### Changed files

- `include/ui/dialogs/meshgenerationdialog.h` — `PipelineInputs`: `double minCellSize = 0.0;` + `mesh::pslg::MinSizePolicy minSizePolicy;`; new widget members (§6.7).
- `src/ui/dialogs/meshgenerationdialog.cpp` — `buildUi`/`seedDefaults`/`collectInputs`; conditioning call at worker step 3; region `maxArea` clamp; `hook.targetAreaAt`; cleanup call; completion summary.
- `tests/gui/CMakeLists.txt` — two new targets, mirroring the `test_pslgprep` registration block (`tests/gui/CMakeLists.txt:1632-1638`).
- `include/mesh/trirefinehook.h` — correct the stale comment about per-region area bounds (§5 item 2).
- `CHANGELOG.md` — per CLAUDE.md §5.2.

`include/mesh/meshgenerator.h` needs no change: the region clamp happens at the dialog before `addRegion`, and the size-function floor rides the existing `RefineHook`. **No `vendor/triangle/` change is needed either** — though note that file is *not* pristine (it already carries local edits: thread-local `jmp_buf`/RNG seed at `triangle.c:355-363, 672`, `triexit()` rewritten to `longjmp` at 1477-1479, and the `triangulate_safe()` wrapper at 1491). Only the *refinement hook* avoids patching it, via `EXTERNAL_TEST` (`CMakeLists.txt:107`). Anything in this plan that seems to need a Triangle edit should be re-examined instead.

**Naming:** `minCell*` identifiers are already taken on the render side for LOD gating (`swmm2dmeshlayer.h:214-222, 678-679`; `qsg2dlodpolicy.cpp:24, 122-165` — all `*MinCellAreaPx`, pixel-space). Meshing-side names must stay clearly distinct: `minCellSize` / `MinSizePolicy` / `A_min` are map-unit quantities and should never be abbreviated to a bare `minCellArea`.

---

## 4. PSLG conditioning stages

One user knob: **`h` = minimum cell size**, horizontal map units. Everything else derives from it.

| Derived | Default | Meaning |
|---|---|---|
| `pitch` | `h / √2` | Snap grid. Max vertex displacement = `pitch/√2` = **`h/2`** |
| `weldRadius` | `h` | Features closer than this merge |
| `minSegmentLen` | `h` | Shortest permitted constrained edge |
| `maxDeviation` | `h / 2` | Hard cap on shape change from resampling |
| `θ_trim` | 30° | Corner sharper than this gets clipped |
| `A_min` | `(√3/4)·h²` | Equilateral triangle of side `h` — the size-function floor (§5) |
| `β` | 0.35 | Cleanup collapses edges shorter than `β·h` (§6.6) |

### Stage 1 — Length resampling (open polylines)

`resampleMinLength` walks each conduit / aux-line path, accumulating chords: drop intermediate vertices until the accumulated chord reaches `minSegmentLen`. Before dropping a run, check the maximum perpendicular deviation of the dropped vertices from the new chord (reuse `distSqToSegment`); if it exceeds `maxDeviation`, keep the offending vertex and record a `ShortSegment` residual rather than distorting the alignment. **Endpoints are always retained** — for conduits they carry coupling identity.

Paths whose *total* length is below `minSegmentLen` cannot be breaklines at this resolution. With `collapseSubScalePaths`, each becomes a single tagged `SteinerPoint` at its midpoint; it stays in `couplingNodes` and the post-generation mapper couples it by containing cell. This is the exact demotion pattern `nodeMinSeparation` already uses, so the coupling story is unchanged and already tested.

Runs after the existing `dedupeSegPath` + `simplifyPolyline` in the conduit candidate loop (`meshgenerationdialog.cpp:594-614`, `simplifyPolyline` at 600), so RDP has already removed the easy collinear cases and resampling only sees genuine zig-zag.

### Stage 2 — Rings

`resampleRingMinLength` on domain rings and hole rings, orientation preserved. Then sub-scale ring rejection:

- Hole ring with `area < h²` or width proxy `2·area/perimeter < h` is dropped when `dropSubScaleHoles` is set. **Dropping a hole means the mesh now covers it** — a real modelling change, hence a counted, reported, policy-gated action, never silent.
- A boundary notch narrower than `h` shows up as a `SubScaleRing` residual; it is *reported*, not auto-repaired. Automatic notch removal changes the wetted domain outline in ways a user needs to see first. Deferred to a follow-up plan if Phase 0 shows notches actually matter on real projects.

Ordering: resample before `prepareHoleRings`, so the existing simplify → validate → densify → seed chain independently re-validates conditioned rings.

### Stage 3 — Snap-round and weld (the core; highest risk)

Iterated snap rounding over *all* PSLG geometry at once — domain rings, hole rings, constraint paths, mandatory Steiner points. Plain snap rounding (Hobby/Guibas–Marimont) gives vertex–vertex separation; the iterated variant (Halperin–Packer) also gives vertex–edge separation, which is what condition (2) in §2 needs.

**3a. Quantize with priority.** Map every vertex to its `pitch` cell and take the cell centre. Where several vertices share a cell, the survivor is the highest priority one and the rest move onto it:

```
tagged SWMM node  >  conduit endpoint  >  tagged vertex  >  untagged vertex
                                       (ties: earlier input order wins)
```

Same priority idiom as `snapAndDedupe` (tagged never merges away) and `greedyMinSeparation` (input order is priority order). Two *distinct* tagged nodes never merge with each other — if they collide, keep both, log a `CloseFeatures` residual, and let `nodeMinSeparation` handle the demotion as it does today.

**3b. Rebuild paths.** Collapse consecutive duplicates. A path degenerating below 2 vertices becomes a Steiner point (Stage 1's collapse rule).

**3c. Vertex–edge fixup.** For each vertex `v`, find segments within `weldRadius` that are *not* incident to `v`. Split each at its closest point to `v` and weld the new split vertex into `v`. Uniform grid of cell size `weldRadius` with a 3×3 scan — literally the `nearConstraint` structure at `meshgenerationdialog.cpp:1506-1521`, extracted into `pslgprep` so both call sites share it. This is the "snap a dangling endpoint onto the nearby boundary" behaviour, generalised: a conduit ending 3 cm from the domain edge gets welded onto it instead of forcing a 3 cm sliver.

**3d. Crossing repair — mandatory.** Welding can create a proper segment crossing, and Triangle *aborts* on a non-planar PSLG (`meshgenerator.cpp` fatal path → generic "check PSLG for degenerate geometry"). So: bucket segments on the same grid, test only same-cell pairs, and where a proper crossing exists split both segments at the intersection and weld the four ends into one vertex. Count into `crossingsRepaired`.

**3e. Fixed point.** Repeat 3a–3d until nothing changes or `maxIterations` (3). Each iteration is monotone in the number of distinct vertices, so it terminates; the cap is belt-and-braces.

**3f. Fail-safe.** If crossings remain after the cap, do **not** hand a possibly non-planar PSLG to Triangle. Set `conditioningAbandoned`, restore the caller's original geometry, log loudly, and let generation proceed unconditioned — a slow correct mesh beats a failed run.

### Stage 4 — Corner trim

Runs **after** Stage 3, because incidence is only well-defined once vertices have been welded.

Build the incidence graph over conditioned segments. At each vertex with ≥ 2 incident legs, sort legs by bearing and inspect adjacent pairs. For a pair with angle `θ < θ_trim`: move each leg's first interior vertex to distance `r = h / (2·sin(θ/2))` along its leg and insert a bridging segment between the two new points — a chord of length exactly `h`. The bridge is not a conduit, so `marker = 0` and no tag; both legs keep their own markers. Cap `r` at `0.4 ×` the shorter leg so trimming can never consume a whole segment; if the cap binds, skip the corner and log a `SmallAngle` residual.

**Tagged apexes default to no trim** (`trimAtTaggedNodes = false`). A manhole where two conduits meet at 15° is exactly where fine resolution is wanted, and the node is a coupling location whose position must not move. Trimming there would relocate the apex away from the pinned node and blunt resolution at the one place it is useful. The knob exists for users who care more about timestep than manhole detail, but the default is off, and this is a decision to confirm at review.

### Stage 5 — Report

`ConditionReport` carries counts, `maxDisplacement`, **domain area before/after** (the number a reviewer will ask about, since moving boundary vertices moves the wetted domain), `predictedMinLfs`, and up to 200 residuals with coordinates and cause so the GUI can list and zoom-to.

---

## 5. Size-function floor (secondary, cheap)

Install the currently-unused `RefineHook::targetAreaAt` alongside the existing `isCancelled`/`onProgress`:

```cpp
hook.targetAreaAt = [aMin, maxArea](double, double) {
    return maxArea > 0.0 ? std::max(aMin, maxArea) : aMin;
};
```

Three consequences to handle deliberately. The second is a genuine behaviour change and the most easily missed item in this plan.

**1. The global `-a` switch drops out.** Installing `targetAreaAt` makes `MeshGenerator` omit `a<area>` (`meshgenerator.cpp:514-518`; base string `"pzA"` at 507, `q<minAngle>` at 508-509, `u` at 524-525), so the global cap must be folded into the lambda, as above.

**2. Installing the hook silently *activates* per-region area bounds, which are inert today.** Triangle honours `regionlist[4i+3]` only when its `vararea` flag is set, and `vararea` is set **only by a bare `a` with no number** (`vendor/triangle/triangle.c:3449-3469`; the bound is tested at `triangle.c:7390`). The switch construction at `meshgenerator.cpp:515-518` is an `if / else if`: when `maxArea > 0 && !useSizeFn` it emits `a<maxArea>` and *skips* the bare-`a` branch. So **`RegionMarker::maxArea` is silently ignored in the common case today**, and installing `targetAreaAt` flips it on. That is a pre-existing latent bug (`trirefinehook.h`'s claim that per-region constraints are "unaffected and still applied by Triangle" is wrong in this configuration) and it becomes *our* regression the moment the hook ships. Therefore:

- Clamp `rm.maxArea = max(rm.maxArea, A_min)` in the dialog before `g.addRegion` — now load-bearing, not belt-and-braces, since these bounds start taking effect.
- Log the per-region bounds that become newly active on any project where subcatchment regions are used, so a mesh that suddenly gains cells has a traceable cause.
- Fix the stale `trirefinehook.h` comment in the same change.

**3. There is no existing regression net for switch strings.** `tests/gui/test_meshgenerator.cpp` does not reference `customSwitchString` or any switch text; all 11 of its slots are behavioural, driving options only through `setOptions({.maxArea, .minAngle})`. Phase 4 must therefore *add* the first switch-string assertions rather than extend existing ones.

Restating §2: this is a **no-further-refinement guard, not enforcement**. It stops the `minAngle`+`maxArea` pair from subdividing near features; it cannot enlarge a cell the input demanded.

Also worth a non-blocking dialog hint: `minAngle` is a real lever here. Sliver count near unavoidable small input angles falls steeply as the bound drops, and the header already warns that 33° costs 2–4× the vertices of 26° for no practical benefit. Warn when `minAngle > 28` is combined with a tight `h`.

---

## 6. Phases and verification gates

Each phase is independently shippable and independently revertable. Per CLAUDE.md §4.1, every fixture and artifact goes to `tests/output/mesh_minsize/`, not temp.

### Phase 0 — Diagnostics only. **This is the gate for the whole plan.**

Implement `analyseLocalFeatureSize` plus generation-log lines for predicted vs actual min cell size. No geometry changes, no UI beyond a log line.

**Verify:** on a real project with dense conduits, correlate the reported worst `lfs` violations against the smallest 100 triangles Triangle actually produced. If `lfs` does not predict the slivers, §2 is wrong and **the plan stops here** rather than proceeding to modify geometry on a bad theory. Also record the cause histogram — if `SmallAngle` dominates, Phases 1–2 will disappoint and Phase 3 should be promoted ahead of them.

### Phase 1 — Length conditioning (Stages 1–2)

**Verify:** unit tests — no output segment shorter than `minSegmentLen` unless flagged; deviation ≤ `maxDeviation`; endpoints bit-identical; sub-scale path → single Steiner point; ring orientation and closure preserved; idempotent. Regression on a real project — min cell area rises, conduit endpoint coordinates unchanged, and the coupling map is identical modulo intended demotions.

### Phase 2 — Snap-round and weld (Stage 3)

**Verify:** property test over randomly generated adversarial PSLGs (dense polylines, near-parallel pairs, near-miss endpoints, segments grazing the boundary), asserting for every case:

1. pairwise vertex separation ≥ `pitch`;
2. vertex–edge distance ≥ `pitch` or incident;
3. **zero proper crossings**;
4. `maxDisplacement ≤ h/2`;
5. tagged vertices moved ≤ `pitch/2` and never merged with each other;
6. **Triangle actually runs** on every conditioned case without the fatal-error path — the single most important assertion in this plan;
7. fail-safe: force a residual crossing and assert inputs are returned untouched with `conditioningAbandoned` set.

### Phase 3 — Corner trim (Stage 4)

**Verify:** two-segment fixtures sweeping `θ` from 5° to 40°. Assert min cell size ≥ `h` for `θ < θ_trim`; PSLG stays planar; nothing trimmed at tagged nodes under the default policy; the leg cap prevents over-trimming on short legs; conditioning is idempotent (running twice changes nothing).

### Phase 4 — Size-function floor and region clamp

Note this phase starts from **zero** switch-string coverage (§5 item 3), so its first task is building the net, not extending one.

**Verify:** new switch-string assertions in `test_meshgenerator.cpp` — `a<area>` present without the hook, absent with it, bare `a` emitted when regions exist; `h = 0` reproduces today's switch string byte-for-byte. Then behavioural: min output area ≥ `A_min` when `maxArea < A_min`; region clamp respected; and — the §5 item 2 regression — a fixture with subcatchment regions whose `maxArea` is below `A_min`, asserting the clamp prevents the newly-activated region bounds from refining below the floor.

### Phase 5 — Post-mesh cleanup

`mesh::collapseSubScaleCells(MeshResult*, double h, double beta, CleanupReport*)` — short-edge collapse with hard protections:

- Candidates: edges shorter than `β·h` (default `β = 0.35` — only genuinely degenerate edges, not merely small ones).
- **Protected, never collapsed:** any edge in `boundaryEdges` or carrying a non-zero marker; any edge with an endpoint whose `marker != 0` or `tag` is non-empty; any endpoint with a non-empty `coupledNode`; any collapse that would flip a triangle's orientation or create a non-manifold vertex.
- Collapse to the higher-priority endpoint (tagged wins, else midpoint); drop the two degenerate triangles; remap indices.
- Bounded passes (default 2), revalidating orientation and non-degeneracy after each.
- **Fail-safe:** any validity failure abandons the pass and returns the pre-pass mesh. Never hand back a broken mesh.
- Report cells removed, min area before/after, and — the actionable list — cells that could not be fixed *because they were protected*, which points straight back at the PSLG.

**Verify:** fixtures with known slivers; protected edges survive; orientation preserved; vertex/triangle indices internally consistent; coupling maps and `cellCouplings` intact; no-op when `h = 0`; injected orientation-flipping collapse returns the original mesh.

### Phase 6 — UI, docs, CHANGELOG

New "Minimum cell size" group in the dialog's Quality section: one spin box in map units using the existing unit display, an advanced expander for pitch / weld radius / trim angle / `β`, and a read-only derived line — "min triangle area ≈ `A_min`; max vertex shift ≈ `h/2`". A **Suggest** button sets `h` from the current `maxArea` (0.3 × the side of the equivalent equilateral triangle).

**Default is off (`minCellSize = 0`)**, so every existing project reproduces its current mesh bit-for-bit. That regression-safety property is the reason for the default and should survive review.

Completion summary extends the existing message with min/median cell area (via `computeCellAreaStats`) plus conditioning counts; residuals log under `lcMeshPerf`. A "Mesh conditioning report" dialog with zoom-to-residual is a follow-up, not part of this plan.

**Verify:** `h = 0` produces an identical mesh to the current build on two real projects (hash the written mesh file); tooltips and derived line update live; advanced overrides round-trip through `collectInputs`.

---

## 7. Risks and explicit tradeoffs

1. **Snap rounding is the only stage that can create new topology errors.** A surviving crossing means Triangle aborts and the entire generation fails. Mitigated by the crossing-repair fixed point, the "Triangle actually runs" property assertion, and the Stage 3f fail-safe that abandons conditioning rather than risk it.
2. **Moving boundary vertices moves the wetted domain.** A domain edge can shift by `h/2`, changing storage volume slightly. `domainAreaBefore/After` is reported for exactly this reason. Users running volume-sensitive comparisons need to see it.
3. **Coupling identity is what breaks silently.** Every stage must preserve conduit endpoints and tagged node positions. Phase 1 and Phase 5 both carry an explicit assertion comparing the coupling map with and without conditioning on a real project.
4. **Corner trimming fights the desire for resolution at manholes** — hence off by default at tagged nodes (§4 Stage 4). Confirm at review.
5. **`h` is horizontal map units.** This pipeline already juggles vertical units (`zConversionFactor`, `meshLinearUnitName`), so the dialog must label `h` with the existing horizontal unit display and the header doc must say so plainly.
6. **Scope discipline.** Sub-scale boundary *notch* repair and the residual-report dialog are deliberately excluded. They are separable, and both change what the user sees rather than what the mesher does.
7. **Latent region-area bug becomes visible.** Per §5 item 2, per-region `maxArea` is inert in today's common configuration and starts taking effect the moment the size function is installed. A project whose subcatchment regions carry small area bounds will gain cells on upgrade for reasons unrelated to `h`. The clamp handles the floor, but the *count* change is real and needs the Phase 4 fixture plus a CHANGELOG note.

---

## 8. Open questions for review

1. **Default `θ_trim` = 30°.** Reasonable against a default `minAngle` of 26°, but Phase 0's cause histogram is the real evidence. Revisit after Phase 0.
2. **Should `dropSubScaleHoles` default true?** It silently extends the mesh over small buildings/obstructions. Arguably safer defaulted false with a warning, at the cost of leaving known slivers in place.
3. **Should `h` default to off forever, or auto-derive from `maxArea` after one release?** Off is regression-safe but discoverability suffers; most users will never find the knob that fixes their timestep.
4. **Is `β = 0.35` for cleanup too conservative?** Larger `β` removes more slivers but collapses more real geometry. Phase 5 fixtures should sweep it.
5. **Should the region-area fix (§5 item 2) ship separately, ahead of this plan?** It is a one-line switch-string correction plus a test, independent of minimum-size work, and bundling a latent-bug fix into a feature makes both harder to review and to revert.

---

*Code references in this document were verified against the working tree on 2026-08-17. Four claims in the first draft were wrong and are corrected inline: `targetAreaAt` is exercised in `tests/verification/`, not wholly unused; per-region `maxArea` is **not** applied independently of the hook (§5 item 2); `test_meshgenerator.cpp` has **no** switch-string coverage; and `vendor/triangle/triangle.c` is not pristine.*
