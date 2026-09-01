# Minimum Cell Size Enforcement — Test Results (2026-08-18)

Answers `MIN_CELL_SIZE_TESTING_HANDOFF_2026-08-17.md`. Every phase gate was
worked; the numbers behind the tables live in
`tests/manual/mesh_minsize/README.md` and `tests/output/mesh_minsize/`.

**Headline.** The code builds and is now safe, Phase 0's premise holds
decisively, and everything the handoff asked to be tested is tested. But the
end-to-end measurement says the conditioning stage **does not deliver a
minimum cell size on real SWMM models**, and at one value of `h` it is
severely counterproductive. That is a design finding for you to decide on, not
something to tune away — see §4.

---

## 0. Build — PASS

`cmake --build build --target SWMMVis` compiles clean on macOS/arm64, Qt 6.9.3,
AppleClang. No compile errors in either new translation unit; the three
specific worries in handoff §0 (`M_PI` via `<QtMath>`, `qHash(EdgeRef)` ADL,
`QVector::assign` avoidance) were all non-issues on this toolchain.

## 1-3, 5. Unit gates — PASS

| Target | Tests | Result |
|---|---|---|
| `test_pslgminsize` | 45 | pass |
| `test_meshminsizecleanup` | 16 | pass |
| `test_meshmincelldialog` | 7 | pass |
| `test_meshstagecache` (extended) | 8 | pass |
| Whole GUI suite | 153 | pass |

Phase 1 (resampling), Phase 2 (welding properties, fail-safe, determinism,
meshability), Phase 3 (corner trim), Phase 5 (cleanup protection rules, link
condition, rollback) are all covered as the handoff specified, plus
cancellation and the `analyseLocalFeatureSize` diagnostic itself.

Two deviations from the handoff's wording, both recorded in the code:

- **Invariant (2) needed a qualification.** "No vertex within `weldRadius` of a
  non-incident segment" cannot hold when two *protected identities* start
  closer than that — neither may move, so the proximity is structural. The
  design's answer is to report it as a `CloseFeatures` residual, and the test
  now asserts exactly that. `pslgminsize.h` says so.
- **Phase 4's floor logic was not reachable from a test.** It lived as a
  closure inside `runMeshPipelineImpl`. It is now
  `MinSizePolicy::refinementAreaCap(uniformCap)`, called by the dialog, so the
  §2.4 vertex-explosion regression is a real test rather than a copy of the
  rule.

## 4. Switch string and region bounds — PASS

- `h = 0` produces a **bit-identical** mesh (SHA-256 over all coordinates,
  markers and indices) to the unconditioned build on all three real models
  tested. The regression guarantee holds.
- Minimum cell size set with `maxArea = 0` does **not** explode: the cap stays
  unconstrained, as §2.4 requires. Asserted both on the rule and on a real
  triangulation.
- A `maxArea` below `A_min` is raised to `A_min`; the mesh then tracks the
  floor, not the cap.
- **`trirefinehook.h`'s corrected claim is confirmed empirically.** With a
  numeric `a<area>` switch, `RegionMarker::maxArea` is measurably inert
  (identical vertex count with and without the region). Install a size
  function and the same region bound produces >5x the vertices. The dialog's
  clamp then bounds it. This is the first coverage of that behaviour.

## 6. Dialog — PASS, with one gap

Default-off state, dependent-widget enablement, the live derived label
(`A_min`, weld radius, and the >28° min-angle hint), and the Suggest button
are all covered in `test_meshmincelldialog`. The boundary-prep cache key is
covered in `test_meshstagecache`, including the generate-at-2 / at-5 / back-at-2
round trip and the fact that `h = 0` reproduces the pre-feature key.

**Not covered:** `collectInputs()` is private with no read-back path, so
"every new field round-trips into `PipelineInputs`" cannot be asserted without
widening the dialog's API. Flagged rather than worked around.

---

## Defects found and fixed

1. **The conditioned PSLG killed Triangle** ("Internal error in
   `segmentintersection()`: Topological inconsistency after splitting a
   segment") on a real 2363-node model at `h = 8`. The fail-safe compared
   *proper crossings only*, and every degenerate case has a zero orientation
   determinant, so it is invisible to a crossing test: the run that died
   carried 393 duplicate segments, 5 collinear overlaps and a zero-length
   segment with **no new crossings at all**. Fixed by censusing the whole
   family (`PslgCensus`), and abandoning on crossings, zero-length segments or
   collinear overlaps. Duplicates are counted and reported but deliberately do
   not abandon — 114 of them meshed fine, and abandoning on them would make
   the feature inert on any model with two near-parallel alignments.

2. **The vertex-edge fix-up created the degeneracy it was meant to remove.**
   Given two nearby protected identities it spliced each into the other's
   path, emitting the same segment twice and laying one leg on top of another.
   Fixed by splitting an edge only where the projection falls *strictly
   inside* it — a projection landing on an endpoint is a separation problem,
   not a vertex-on-segment — plus an adjacency claim that declines the
   mirror-image insertion.

3. **`holesDropped` double-counted.** A ring emptied by the sub-scale drop was
   counted again when the weld stage saw it empty, so the user-facing "the
   mesh now COVERS those regions" warning reported twice the truth.

4. Reporting: `ConditionReport` now carries `abandonReason` (the summary said
   only "crossings N -> M", which was uninformative once other causes could
   trip it) and `duplicateSegments`, and the worker warns when alignments were
   merged, because Triangle keeps one of a duplicate pair and the other loses
   its edge marker and conduit tag.

---

## Findings to decide on — the feature does not do what it says

### Phase 0 — the premise HOLDS, decisively

Of the 100 smallest cells on each of three real models, **100/100 lie within
one `h` of a reported local-feature-size violation**, against a control of
42/33/18 for evenly-sampled cells; median distance-to-violation is 3-6 orders
of magnitude smaller for the small cells than for the control. Triangle's
output cell size does track input local feature size. The dominant causes are
`ShortSegment` and `CloseFeatures`; `SmallAngle` matched **zero** of the 300
smallest cells, so per the handoff's own note, resampling and welding are the
levers and corner trimming is not.

### ...but conditioning does not act on it

| model | h | verts | minArea | conditioning |
|---|---|---|---|---|
| `2383_H&H_Link_Elements` | off | 222 326 | 3.47e-09 | — |
| | 2 | 214 037 | 3.47e-09 | applied, **15 welds** |
| | 4 | 154 712 | 6.02e-06 | applied, 27 welds |
| | 8 | **1 315 680** | **5.20e-11** | applied, 200 welds |
| | 16 | 200 446 | 3.47e-09 | ABANDONED |
| `2300_H&H_Elements` | off | 82 209 | 1.34e-04 | — |
| | 2 | 81 314 | 2.41e-04 | applied, 3 welds |
| | 5 | 81 441 | 8.47e-05 | applied, 10 welds |
| | 10 | 80 937 | 7.03e-05 | applied, 12 welds |
| `3440_H&H_Elements (1)` | any | — | 1.11e-15 | ABANDONED at every h |

Three things:

1. **The smallest cell barely moves, and usually gets worse.** A floor on cell
   size is the feature's entire purpose and it is not achieved anywhere in
   this table.
2. **`h = 8` on the 2383 model is a 6x vertex explosion with a smaller minimum
   cell** — strictly worse than doing nothing. The response is non-monotonic
   in `h`, so there is no "pick a smaller h" rule to give the user.
3. **Welding almost never fires** — 3 to 200 welds against thousands of
   reported violations.

Reason for (3), and it looks structural rather than incidental: on a SWMM
model nearly every crowded vertex is a **protected identity** — a tagged node
Steiner point, or the endpoint of a tagged conduit — and `conditionMinSize`
may never merge two identities into one another, because they are coupling
locations. The crowding that forces the small cells is precisely the crowding
it is forbidden to remove. Note that the pass which *can* act on these,
`nodeMinSeparation` / `greedyMinSeparation` node demotion, already runs
upstream in the worker.

Also worth knowing: conditioning cost 18.4 s at `h = 0.5` and 8.8 s at
`h = 1` on the 2363-node model, dominated by the crossing broad phase at small
`weldRadius`.

### Suggested next step

Before any further work on welding, decide whether the identity rule can be
relaxed — e.g. letting two identities merge when the post-generation node
mapper can still couple both to the resulting cell, which is exactly what node
demotion already relies on. Without that, conditioning can only resample
polyline interiors, and the measurements above are roughly what that is worth.

The post-mesh cleanup pass is a separate matter and behaves as designed; it is
just limited to what it is allowed to touch (48 000 - 4 250 000 sub-scale
cells were protected in these runs, all bounded by constrained or coupled
geometry).

---

## Repository hygiene — needs your action

`include/mesh/pslgminsize.h`, `src/mesh/pslgminsize.cpp`,
`include/mesh/meshminsizecleanup.h` and `src/mesh/meshminsizecleanup.cpp` are
**untracked**, while the `CMakeLists.txt` that registers them is tracked and
modified. A fresh clone would fail to configure. The three new test sources
are untracked for the same reason. Nothing has been committed.
