# Minimum cell size — manual diagnostics

Two standalone probes backing the phase gates in
`workplans/MIN_CELL_SIZE_TESTING_HANDOFF_2026-08-17.md`. They are diagnostics,
not regression tests, so they are not CMake targets — the automated coverage
lives in `tests/gui/test_pslgminsize.cpp` and
`tests/gui/test_meshminsizecleanup.cpp`.

Both write their reports to `tests/output/mesh_minsize/` (CLAUDE.md §4.1).

| Probe | Answers |
|---|---|
| `lfs_premise_probe.cpp` | Phase 0 — do the reported local-feature-size violations actually predict where the small cells are? |
| `conditioning_effect_probe.cpp` | Phase 4 end-to-end — is `h = 0` bit-identical to the unconditioned mesh, and what does conditioning actually do to a real model at each `h`? |

Each builds the same PSLG the mesh-generation worker builds: SWMM nodes as
tagged Steiner points, conduits as tagged constraint polylines, and the convex
hull of the node coordinates standing in for the boundary layer (`inp_pslg.h`).

## Building

`SWMMVis` must have been built once first, for `build/libtriangle_lib.a`.

```sh
R=$PWD
QTI=$HOME/Qt/6.9.3/macos/lib          # your Qt prefix

clang++ -std=c++20 -O2 -DQT_NO_DEBUG -include arm_acle.h \
  -DSWMMVIS_MINSIZE_OUT="\"$R/tests/output/mesh_minsize\"" \
  -I $R/include -I $R/vendor/triangle -I $R/tests/manual/mesh_minsize \
  -F $QTI -I $QTI/QtCore.framework/Headers \
          -I $QTI/QtGui.framework/Headers \
          -I $QTI/QtConcurrent.framework/Headers \
  $R/tests/manual/mesh_minsize/conditioning_effect_probe.cpp \
  $R/src/mesh/pslgminsize.cpp $R/src/mesh/pslgprep.cpp \
  $R/src/mesh/meshgenerator.cpp $R/src/mesh/meshminsizecleanup.cpp \
  $R/src/mesh/trirefinehook.cpp $R/src/core/editgeometry.cpp \
  $R/build/libtriangle_lib.a -Wl,-rpath,$QTI \
  -F $QTI -framework QtCore -framework QtGui -framework QtConcurrent \
  -o $R/build/_minsize_effect_probe
```

`-include arm_acle.h` works around Qt 6.9's `qyieldcpu.h` calling `__yield()`
without including the ACLE header; the normal build gets it from Qt's own
compile flags. Swap the source file for `lfs_premise_probe.cpp` (and drop
`meshminsizecleanup.cpp`) to build the Phase 0 probe.

## Running

```sh
build/_minsize_lfs_probe      <model.inp> [maxArea] [hOverride]
build/_minsize_effect_probe   <model.inp> [maxArea] [h ...]
```

With no `maxArea` both pick one that yields roughly 200 000 cells over the
domain bounding box.

## Measured 2026-08-18

Models from `../openswmm.engine.benchmarks`.

### Phase 0 — the premise HOLDS

For each model: the 100 smallest output triangles, and how far their centroids
are from the nearest violation `analyseLocalFeatureSize` reported at
`h = median cell edge`. The control is 100 evenly-sampled triangles, and it is
the number that makes the result mean anything — without it a high hit rate
could just mean the violations blanket the domain.

| model | nodes | h | violations | smallest within 1 h | control within 1 h | median d/h smallest | median d/h control |
|---|---|---|---|---|---|---|---|
| `2383_H&H_Link_Elements` | 2363 | 16.65 | 38 198 | **100/100** | 42/100 | 0.030 | 2.25 |
| `3440_H&H_Elements (1)`  | 1688 | 96.57 |  2 188 | **100/100** | 33/100 | 6.7e-7 | 5.97 |
| `2300_H&H_Elements`      | 1145 | 56.11 | 10 277 | **100/100** | 18/100 | 0.0027 | 6.80 |

The dominant cause is `ShortSegment` and `CloseFeatures`; `SmallAngle` matched
**zero** of the 300 smallest cells. Per the handoff's Phase 0 note, that means
resampling and welding — not corner trimming — are where the leverage is.

### Phase 4 — `h = 0` is bit-identical

The SHA-256 of the mesh (all vertex coordinates and markers, all triangle and
boundary-edge indices) at `h = 0` equals the unconditioned mesh's hash on all
three models. The regression guarantee holds.

### End-to-end effect — conditioning does NOT deliver a minimum cell size

`maxArea` fixed per model; `minArea` is the smallest output triangle.

| model | h | verts | minArea | cells < A_min | conditioning |
|---|---|---|---|---|---|
| `2383` | — (off) | 222 326 | 3.47e-09 | — | — |
| `2383` | 2  |   214 037 | 3.47e-09 | 79 817 | applied, 15 welds |
| `2383` | 4  |   154 712 | 6.02e-06 | 36 856 | applied, 27 welds |
| `2383` | 8  | **1 315 680** | **5.20e-11** | 2 390 951 | applied, 200 welds |
| `2383` | 16 |   200 446 | 3.47e-09 | 178 251 | ABANDONED (crossings 15→26, zero-length 2) |
| `2300` | — (off) |  82 209 | 1.34e-04 | — | — |
| `2300` | 2  |    81 314 | 2.41e-04 |  1 726 | applied, 3 welds |
| `2300` | 5  |    81 441 | 8.47e-05 |  3 969 | applied, 10 welds |
| `2300` | 10 |    80 937 | 7.03e-05 |  7 414 | applied, 12 welds |
| `3440` | any | — | 1.11e-15 | — | ABANDONED at every h |

Three things to take from that table:

1. **The smallest cell barely moves, and often gets worse.** The feature's
   stated purpose is a floor on cell size; it is not achieved on any of these
   models.
2. **`h = 8` on `2383` is a 6x vertex explosion** with a *smaller* minimum
   cell — strictly worse than doing nothing. The response is non-monotonic in
   `h`, so there is no "just pick a smaller h" rule to give the user.
3. **Welding almost never fires** — 3 to 200 welds against thousands of
   reported violations. On a SWMM model nearly every crowded vertex is a
   protected identity (a node Steiner point, or the endpoint of a tagged
   conduit), and `conditionMinSize` may not merge two identities into one
   another. The crowding that forces the small cells is therefore exactly the
   crowding it is forbidden to remove. The upstream `nodeMinSeparation` /
   `greedyMinSeparation` demotion is the pass that *can* act on these, and it
   already runs before conditioning.

Conditioning time is also worth noting: 18.4 s at `h = 0.5` and 8.8 s at
`h = 1` on the 2363-node model, dominated by the crossing broad phase at small
`wr`.

### Defects these probes found

- **Triangle aborted** on the conditioned PSLG at `h = 8` ("Topological
  inconsistency after splitting a segment"): 393 duplicate segments, 5
  collinear overlaps, 1 zero-length segment, and *no new proper crossings* — so
  the fail-safe, which compared crossings only, shipped it. Fixed by censusing
  the whole degenerate family (`PslgCensus` in `pslgminsize.cpp`).
- The vertex-edge fix-up spliced each of two nearby protected identities into
  the other's path, emitting the same segment twice and laying one leg on top
  of another. Fixed by only splitting an edge where the projection falls
  strictly inside it, plus an adjacency claim against the mirror insertion.
- A hole ring emptied by the sub-scale drop was counted a second time when the
  weld stage saw it empty, so `holesDropped` double-reported.
