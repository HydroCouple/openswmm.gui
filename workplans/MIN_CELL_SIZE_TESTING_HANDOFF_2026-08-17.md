# Minimum Cell Size Enforcement — Testing Handoff (2026-08-17)

**To the agent picking this up.** The production code for
`MIN_CELL_SIZE_ENFORCEMENT_PLAN_2026-08-17.md` is written and wired, but it has
**never been compiled and never been run**. The implementing session had no Qt
toolchain. Treat every claim below as unverified until you have a green build.

Read `include/mesh/pslgminsize.h` first — its header comment explains why the
feature has to change input geometry at all, and what it guarantees. The plan
document explains the reasoning; this document is the task list.

---

## 0. Do this first: build it

```
cmake --preset <your usual preset>
cmake --build --preset <your usual preset> --target SWMMVis
```

New translation units: `src/mesh/pslgminsize.cpp`, `src/mesh/meshminsizecleanup.cpp`.
Both are already registered in `CMakeLists.txt` (headers ~line 527, sources ~line 1152).

Expect to fix compile errors. A prior review pass caught and fixed nine real
defects, but a review is not a compiler. Pay particular attention to:

- `M_PI` in `pslgminsize.cpp` — `<QtMath>` was added for MSVC; confirm it is enough.
- `QSet<QPair<qint64,qint64>>` and the hand-rolled `qHash(EdgeRef)` in the
  anonymous namespace of `pslgminsize.cpp` (ADL resolution).
- `QVector::assign` was deliberately avoided in favour of `resize`/`fill` for
  older-Qt compatibility; check nothing else needs the same treatment.

**Do not "simplify" the fail-safe paths to make something compile.** Every
`restore(); return false;` in `conditionMinSize` and every rollback in
`collapseSubScaleCells` exists to keep a bad PSLG or a broken mesh from
reaching Triangle or the engine.

---

## 1. What was actually built

| Piece | Where |
|---|---|
| Length resampling (min segment length, deviation-capped) | `pslgprep.{h,cpp}` — `resampleMinLength`, `resampleRingMinLength`, plus `polylineLength`, `ringSignedArea` |
| PSLG conditioning: weld, vertex-edge fix-up, crossing repair, corner trim, sub-scale collapse | `mesh/pslgminsize.{h,cpp}` — `conditionMinSize` |
| Local-feature-size diagnostics | same — `analyseLocalFeatureSize` |
| Post-mesh sliver collapse | `mesh/meshminsizecleanup.{h,cpp}` — `collapseSubScaleCells` |
| Refinement floor + region clamp | `meshgenerationdialog.cpp`, near `g.addRegion` and the `RefineHook` block |
| Cache-key fix | `meshstagecache.{h,cpp}` — `boundaryKey` gained `minCellSize` |
| UI | `meshgenerationdialog.cpp` — "Minimum Cell Size" group on the Quality tab |
| Stale-comment fix | `trirefinehook.h` — per-region area bounds |

Pipeline order (worker, `runMeshPipeline`): boundary ingest → candidate
filtering + markers → **conditionMinSize** → hole-ring prep → PSLG assembly
(+ region clamp) → DTM sampling → Triangle (+ size-function floor) →
**collapseSubScaleCells** → Hilbert reorder → elevation fill → node mapping.

---

## 2. Deviations from the plan — read before testing

Four places where the implementation intentionally differs. If a test fails
against the plan's wording, check here before "fixing" the code.

**2.1 Welding replaces snap-rounding.** The plan specified quantizing vertices
to an `h/√2` grid. The implementation uses greedy priority-ordered welding
(the `greedyMinSeparation` idiom already in `pslgprep.h`) instead. Same
separation guarantee (survivors ≥ `weldRadius` apart), but grid quantization
moves *every* vertex including tagged SWMM nodes, whereas welding leaves every
representative — and therefore every tagged node — exactly where it was.
Consequences for tests: **max displacement is `weldRadius` (= h), not h/2**,
and **tagged node displacement is 0**. There is no `gridPitch` field.

**2.2 Corner trimming only fires at degree-2 vertices.** With three or more
incident legs the apex survives whatever pair you blunt, so trimming would not
achieve anything; those are counted in `cornersSkipped` and reported as
`SmallAngle` residuals instead. Covers the two real cases: a hairpin inside one
polyline, and two constraints meeting end-to-end.

**2.3 Cleanup protects the endpoints of every constrained edge**, not just
marker-bearing ones as the plan said. Protecting only the edge still lets a
vertex move via some *other* incident edge, dragging the domain outline with
it. This makes "`boundaryEdges` is preserved exactly" a real invariant the pass
asserts on — and it is deliberately conservative, so expect cleanup to decline
more collapses than the plan implies.

**2.4 The refinement floor is nearly inert, by necessity.** `RefineHook::targetAreaAt`
returns the *maximum* permitted area (`trirefinehook.cpp:101` splits anything
larger), so "don't go below X" is not expressible. All the floor does is raise a
too-small uniform `maxArea` up to `A_min`; with no cap set it returns 0
(unconstrained). An earlier draft returned the floor unconditionally, which
ordered the entire domain refined to the minimum size — a vertex-count
explosion. **Add a regression test for that specific case** (min cell size set,
max area 0).

---

## 3. Test targets to add

Neither test file exists. Register them in `tests/gui/CMakeLists.txt` mirroring
the `test_pslgprep` block at lines 1632-1638:

```cmake
# Minimum feature size conditioning: resampling, welding, vertex-edge fix-up,
# crossing repair, corner trimming, and the fail-safe.
find_package(Qt6 REQUIRED COMPONENTS Concurrent)
add_swmmvis_gui_test(test_pslgminsize
    test_pslgminsize.cpp
    ${CMAKE_SOURCE_DIR}/src/mesh/pslgminsize.cpp
    ${CMAKE_SOURCE_DIR}/src/mesh/pslgprep.cpp
    ${CMAKE_SOURCE_DIR}/src/core/editgeometry.cpp
    ${CMAKE_SOURCE_DIR}/include/mesh/pslgminsize.h
)
target_link_libraries(test_pslgminsize PRIVATE Qt6::Concurrent)

# Post-mesh sliver collapse: protection rules, link condition, rollback.
add_swmmvis_gui_test(test_meshminsizecleanup
    test_meshminsizecleanup.cpp
    ${CMAKE_SOURCE_DIR}/src/mesh/meshminsizecleanup.cpp
    ${CMAKE_SOURCE_DIR}/include/mesh/meshminsizecleanup.h
)
```

Per CLAUDE.md §4.1, any fixture or artifact a test writes goes to
`tests/output/mesh_minsize/`, never a temp directory.

---

## 4. Phase gates

Work these in order. **Phase 0 is a genuine stop/go gate** — if it fails, the
whole approach is wrong and the right move is to say so, not to proceed.

### Phase 0 — Does local feature size actually predict the slivers?

The entire design rests on the claim in `pslgminsize.h`: Triangle's output cell
size tracks the input's local feature size, so small cells are caused by input
geometry rather than by refinement.

Test it empirically on a real model with dense conduits:

1. Generate a mesh with minimum cell size **off**.
2. Call `analyseLocalFeatureSize(...)` on the same PSLG with `h` set to roughly
   the mesh's median cell edge length.
3. Take the 100 smallest output triangles (`mesh::computeCellAreaStats`, or sort
   `triangleArea`) and check how many lie within a few `h` of a reported violation.

**Pass:** most small cells coincide with reported violations, and the cause
histogram is informative. **Fail:** they do not correlate — stop, report it,
and do not ship conditioning. Also record which `ViolationCause` dominates: if
it is `SmallAngle`, then resampling and welding will disappoint and corner
trimming is doing the real work.

### Phase 1 — `resampleMinLength` / `resampleRingMinLength` (unit)

- No output segment shorter than `minLen`, **except** where `flaggedOut` was
  incremented. Assert the exception is always accounted for.
- No output vertex further than `maxDeviation` from the original path. The tail
  handling was a bug here (fixed by looping the deviation check); build a tight
  zig-zag whose whole extent is below `minLen` and confirm.
- First and last points bit-identical to the input.
- Ring variant: closure and orientation preserved (`ringSignedArea` keeps its
  sign), and **the seam edge `last → first` obeys `minLen`** — that edge is
  invisible to the open-sequence pass and needed its own fix-up. Test it directly.
- Idempotence: resampling twice equals resampling once.
- `minLen <= 0` or `size <= 2` returns the input unchanged.

### Phase 2 — `conditionMinSize` welding (property-based)

Generate adversarial PSLGs — dense polylines, near-parallel pairs, endpoints
just short of a boundary, segments grazing rings — and for every case assert:

1. survivors pairwise ≥ `weldRadius` apart, except pairs of distinct tagged
   vertices (which are reported as `CloseFeatures` residuals instead);
2. no vertex within `weldRadius` of a non-incident segment;
3. `report.crossingsAfter <= report.crossingsBefore` whenever the call returned true;
4. `report.maxDisplacement <= weldRadius` (see §2.1 — not h/2);
5. tagged vertices (`marker != 0`) did not move **at all**, and no two distinct
   tagged vertices merged;
6. **the conditioned PSLG survives `MeshGenerator::generate()`** without hitting
   the fatal-error path. This is the single most valuable assertion here — a
   non-planar PSLG aborts Triangle and fails the whole generation.

Fail-safe: construct a case that trips it, and assert all four output vectors
are byte-identical to the inputs and `conditioningAbandoned` is set.

Determinism: same input, same policy, same output — twice.

### Phase 3 — Corner trim

- Two segments meeting end-to-end, sweeping θ from 5° to 40°: below
  `trimAngleDeg` a bridge appears and the apex is replaced; above, nothing changes.
- Hairpin inside a single polyline: handled as one path edit, no bridge segment.
- Degree-3 vertex: nothing trimmed, `cornersSkipped` incremented (§2.2).
- Tagged apex with `trimAtTaggedNodes = false` (the default): not trimmed, and
  the node's coordinate is untouched.
- Short-leg cap: when `r` would exceed 0.4 × the shorter leg, skip and report.
- Idempotence: conditioning twice trims nothing the second time.

### Phase 4 — Switch string and region bounds

`test_meshgenerator.cpp` has **no** switch-string coverage today, so this is new
ground. `GenerationOptions::customSwitchString` is not a read-back path, so you
will likely need to either expose the constructed string for testing or assert
behaviourally.

- With `h = 0` the mesh must be **identical** to the current build's output on at
  least two real projects. Hash the written mesh file. This is the regression
  guarantee that justifies shipping.
- Minimum cell size set + `maxArea = 0` → vertex count stays in the same
  ballpark as unconditioned. This is the §2.4 explosion regression; it is the
  most important test in this phase.
- Minimum cell size set + `maxArea` smaller than `A_min` → no cell below `A_min`.
- A model with subcatchment regions carrying small area bounds: confirm the
  clamp fires (the log line reports the count) and that cell count is sane.
  Note per §2.4 and `trirefinehook.h` that region bounds were previously inert
  and now activate — a cell-count change here is expected, not a bug, but it
  must be *bounded* by the clamp.

### Phase 5 — `collapseSubScaleCells`

- A fixture with a known interior sliver: collapsed; vertex and triangle indices
  stay consistent; every triangle keeps its orientation sign.
- Sliver bounded by a constrained edge: **not** collapsed, counted in
  `skippedProtected`, centroid appears in `unfixable`.
- `boundaryEdges` count and contents preserved exactly (§2.3).
- `cellCouplings` preserved; no coupled vertex removed; `tri` indices remapped.
- Any vertex with `marker != 0`, non-empty `tag`, or non-empty `coupledNode`
  survives.
- Link-condition case: a configuration where collapse would create a
  non-manifold vertex must be declined.
- Rollback: force a validity failure (e.g. hand-build a mesh where a collapse
  inverts a triangle) and assert the mesh is byte-identical to the input and
  the function returns false.
- `minCellSize = 0` is a no-op.

### Phase 6 — Dialog and end-to-end

- Default state: minimum cell size off, dependent widgets disabled.
- The derived label updates live and reports `A_min` and the weld radius; the
  min-angle hint appears above 28°.
- **Suggest** button computes from max area.
- `collectInputs` round-trips every new field into `PipelineInputs`.
- Cache correctness: generate at `h = 2`, then at `h = 5`, then back at `h = 2`.
  The boundary-prep cache is now keyed on `minCellSize`; confirm the third run
  hits cache and produces the same mesh as the first. A stale hit here would
  silently mesh the wrong geometry, so this is worth doing by hand as well.
- Cache-hit path sets `ringsReadOnly`; confirm rings are not modified on a hit
  and the mesh matches the cache-miss run.
- Cancel during conditioning leaves no partial state.

---

## 5. Known gaps and open items

Not bugs to fix silently — decisions to raise.

1. **Corner-trim outputs are not re-welded.** Trimming runs last and its new
   vertices are not re-checked for separation; a second weld pass would undo the
   trim. New edges are ≥ 0.6 × `weldRadius` by construction, not ≥ `weldRadius`.
2. **`terrainBoundaryBuffer` was left alone.** It defaults to 0.5 × terrain
   spacing. With a minimum cell size set, a buffer below `h/2` lets DTM points
   sit closer to constraints than `h`. Deriving it from `h` was skipped as
   out-of-scope (CLAUDE.md §3); consider it after Phase 0 data.
3. **Sub-scale domain *notches* are reported, not repaired** — deliberate, per
   the plan. Only hole rings are dropped.
4. **Dropped hole rings are emptied, not erased**, to keep `holeRings`
   index-parallel with `bprep.holeSeeds`/`holeValid`. They then fail
   `prepareHoleRing`'s validity check and are counted in the existing
   "skipped invalid hole ring(s)" warning as well as in `holesDropped`. Expect
   double reporting in the log.
5. **The 2D engine has no matching guard** — GUI-only was the agreed scope. The
   engine still accepts whatever mesh it is handed.
6. **No residual-report UI.** Violations go to `lcMeshPerf` only. A
   zoom-to-residual dialog was explicitly deferred.
7. **Crossing detection is budget-limited** (`kMaxSegChunks`, `kMaxPairTests` in
   `pslgminsize.cpp`). Exceeding either trips the fail-safe rather than shipping
   an unverified PSLG. If real models hit the budget, that is a tuning question —
   report the numbers rather than raising the constants blindly.

---

## 6. If you have to report a failure

Say which phase gate failed and what the numbers were. Phase 0 failing is a
legitimate and useful outcome — it would mean the local-feature-size premise
does not hold for these models, and the honest response is to stop and say so
rather than to tune constants until the tests pass.
