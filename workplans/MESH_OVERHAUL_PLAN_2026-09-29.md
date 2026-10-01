# Mesh Generation Overhaul Plan — 2026-09-29

Status: APPROVED 2026-09-29 (owner: "Proceed with implementation"). Living copy of the review at
https://claude.ai/code/artifact/ca920188-9c87-469e-8ba2-b12389151456. This file is the vetted plan
per CLAUDE.md §5.0; later sessions follow it rather than inventing a new strategy.

Progress 2026-09-30: Phases 0–6 and the Phase 6b amendment (terrain break lines + mixed cell
shape, `workplans/MESH_OVERHAUL_PHASE6B_FEATURE_CAPTURE_2026-09-30.md`) implemented in the container
build; container ctest green (22 suites). Phase 7 pending on the Mac — full GUI build, ctest,
Bellinge and urban metrics, engine round trip — per
`workplans/MESH_OVERHAUL_PHASE7_HANDOFF_2026-09-30.md` (revision 2).

Progress 2026-10-01: Phase 8 — the triangle engine
(`workplans/MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md`, decisions D10–D14) — replaces Stages 3–4
(quadtree core + CDT fringe) and the Phase 6b mixed cell shape: constrained Delaunay refinement with
a guaranteed minimum angle, quads only in aligned feature strips (quad regions, corridors, streets
from the DEM, conduit strips), coarsen default 20. Implemented in the container (22 suites green);
Phase 7 on the Mac continues with handoff revision 3 (patch 0005).

## 1. Objectives (priority order)

1. **Constrainable.** Domain rings, holes, breaklines, conduit alignments and junctions appear exactly
   in the mesh. Coupling identity is never simplified away.
2. **Refinable.** One size field h(x) governs every cell (triangle or quad): base size, distance to
   features, terrain fidelity, region overrides, corridor spacing.
3. **Smoothly graded.** Edge-length ratio between neighbouring cells ≤ 2 (typical ≤ 1.4) everywhere,
   including quad/triangle interfaces and around terrain detail.
4. **Very efficient.** Time and memory linear in output cells, not DEM pixels or input vertices;
   1 M cells ≈ 10 s single-threaded, < 200 B/cell peak; every stage streams/tiles/parallelises with the
   QtConcurrent pattern already used by the worker; cancel + progress between stages.
5. **Quad-dominant where it helps, triangles where geometry demands.** No percentage target.
6. **Terrain-aware without a point cloud.** Terrain enters as a size, not as hard Steiner vertices.
7. **Boundary vertex trimming by straightness** for non-coupling rings/lines (angle + deviation guard).
8. **Few options, each in physical units.** No Triangle switches, no Jacobian thresholds, no iteration counts.
9. **Minimal dependencies.** No Triangle. Robust predicates from one public-domain file. Nothing new in
   `vcpkg.json`.
10. **Deterministic** across platforms and thread counts.

Out of scope: burn-in raster, elevation interpolation, node mapper, INP writer/reader, results, rendering.
Kept: Mapped/swept corridor patches (`meshpatch`, `corridorsource`). Non-goals: all-quad guarantee,
anisotropic background cells, bit-identity with today's meshes (settings are migrated).

## 2. Findings (why today's pipeline misbehaves)

- **F1 abrupt transitions:** vertices come from five sources; only Triangle's refinement obeys the size
  field. The thinner emits hard Steiner clouds; gradation is off by default; Poisson/boundary-buffer
  constants are not functions of h; region/patch spacings are not seeds; Ruppert at 26° allows ~2.3×
  neighbour ratios that compound.
- **F2 quad over-parameterisation:** four mechanisms (tri-pair merge, patches, quad regions with five
  modes, quads-everywhere) → 20 controls; modes exposed although `classifyQuadRegion()` decides;
  directional spacing on general regions; strict bounds (60/120, SJ 0.866) leave triangle seams.
- **F3 thinning:** normal-dot criterion (not vertical error), error never measured against the DEM
  (accumulates across passes), batch removal of adjacent vertices, density unrelated to h(x), whole DEM
  processed regardless of mesh size.
- **F4 min-size surgery:** `pslgminsize` + `meshminsizecleanup` (2,366 lines) repair what a size floor at
  vertex placement prevents.
- **F5 dependencies:** vendored, patched Triangle (14,952 lines; licence not free for unrestricted
  redistribution) is why `allowSteiner`, `maxSteiner`, `minAngle`, `conformingDelaunay`,
  `customSwitchString` exist. `naturalnbinterpolator` also uses Triangle for its Delaunay.

## 3. Architecture

Stage 1 **Boundary preparation** (`pslgprep`): `trimByStraightness` (min-heap on turning angle,
deviation measured against ORIGINAL vertices, protected vertices never removed) → resample every
constraint at h(x) → auto snap (0.01·h_min).

Stage 2 **Size field** (`sizefield` v2, background grid): h_feature = h_base + (r−1)·d;
h_terrain = largest block scale whose DEM deviation from the corner plane ≤ tolerance (banded read,
pyramid); h_region/corridor overrides; h = clamp(min(...), h_min, h_max); gradation limiting sweep
h(p) ≤ h(q) + (r−1)·|p−q| (two-pass chamfer).

Stage 3 **Quadtree core** (`meshquadtree`, new): linear (Morton) tree per frame (CRS axes or
per-region angle), split while side > h, 2:1 balance, drop leaves cut by / within 0.5·h of a
constraint, 2:1 templates (quads, SJ ≥ ~0.7, triangle fallback), Triangles mode = alternating diagonal.

Stage 4 **Fringe conformer** (`meshcdt`, new + reuse): CDT of core-front + constraint vertices
(incremental insertion, `orient2d`/`incircle` from public-domain `predicates.c`, Sloan edge recovery,
flood-fill removal), light refinement (circumcentre when radius > h and 0.5·h clear of constraints),
`cleanupAndSmoothQuads` on fringe vertices, optional `meshquadmatch` pairing at 45–135°, SJ ≥ 0.5.

Stage 5 **Assembly**: stitch corridor/Mapped patches, tags, Hilbert (Morton order makes it ~no-op),
`MeshResult` → downstream unchanged.

### Option surface (11 controls)

| Group | Control | Unit | Default |
| --- | --- | --- | --- |
| Resolution | Cell size at features | map units | from extent |
| Resolution | Coarsen away from features up to | × | 4 |
| Resolution | Size ratio between neighbouring cells | ratio | 1.5 |
| Resolution | Minimum cell size | map units | cell size / 4 |
| Resolution | Terrain tolerance | vertical units | 0 = off |
| Shape | Cell shape | Quads where possible / Triangles / Quads on open ground, triangles at terrain features (6b) | the third |
| Shape | Grid orientation | CRS axes / angle | CRS axes |
| Shape | Quad regions layer (attrs `h`, `angle`) | layer | none |
| Shape | Corridors | existing widget | — |
| Boundaries | Trim: max turn | degrees | 0 = off |
| Boundaries | Trim: max deviation | map units | 0.1 · cell size |

## 4. Dependencies

Remove `vendor/triangle` + `trirefinehook`. Add `vendor/predicates/predicates.c` (Shewchuk, public
domain; `orient2d`/`incircle` only). GDAL/GEOS, nanoflann, HDF5, Qt unchanged. Nothing new in vcpkg.

Retired: `dtmthinner`, `pslgminsize`, `meshminsizecleanup`, `meshcrossfield`, `meshquadpoints`,
`meshquadmerge`, `meshsubmap`, Free/Submapped paths of `meshquadregion`/`meshgenerator`, Triangle
packing in `meshgenerator.cpp`. Kept: `meshpatch`, `meshquadmatch`, `meshquadcleanup`,
`meshquadquality`, `corridorsource`, `channelburn*`, `meshstagecache`, everything downstream.

## 5. Phases and gates

| Phase | Work | Gate |
| --- | --- | --- |
| 0 Baseline (1 wk) | reference models, baseline meshes, `computeGradingStats` in `meshcellstats`, container build harness | metrics on every baseline mesh |
| 1 Size field v2 (2 wk) | terrain error grid, unified sources, gradation sweep, wired to the `-u` hook as interim consumer | neighbour ratio ≤ r on baselines with thinner off |
| 2 Boundary prep (1 wk) | `trimByStraightness`, resample at h(x), auto snap | no coupling vertex lost; deviation ≤ d_max |
| 3 Quadtree core (2 wk) | tree, balance, templates, frames; emit MeshResult with fringe unmeshed | 2:1 everywhere, template SJ ≥ 0.7, 1 M cells < 5 s |
| 4 Fringe conformer (3 wk) | CDT kernel, recovery, flood fill, light refinement, smoothing, pairing, assembly; NN interpolator on the new kernel | conforming, no gaps/overlaps, lake at rest 0.0 |
| 5 Cut-over (1 wk) | deletions, coupling policy, CMake, licences, AUTHORS | builds without vendor/triangle, ctest green |
| 6 Dialog + migration (2 wk) | 11 controls, settings migration (max area → h = √(4A/√3); g → r = 1+g; min size → h_min; thinning → tolerance), recipe persistence | old projects open and generate |
| 7 Validation (1 wk) | test plan, engine runs, Bellinge, CHANGELOG; **last step: remove the Triangle licence entry from the About dialog** (`resources/about/components.json`, `resources/licenses/triangle.txt`) | acceptance table below |

## 6. Acceptance

edge-length ratio across shared edges (longest edge of each cell): core ≤ 2.0 by construction,
fringe ≤ 2.1, median ≤ r (a p95 ≤ r bound is not reachable with a dyadic tree: half the cells at
every level boundary sit at ratio 2 — revised in Phase 4) · 90 % of cells within [0.7h, 1.4h] ·
fringe triangles min angle ≥ 25°, none < 10° · core/template quads SJ ≥ 0.7, paired ≥ 0.5, all convex ·
quad share ≥ 90 % open ground, ≥ 70 % within 2h of constraints · orthogonality median ≤ 5°, max ≤ 30° ·
terrain RMS ≤ tolerance, max ≤ 3× · conformity 100 % · topology clean · byte-identical across 1/4/8
threads and platforms · 1 M-cell Bellinge ≤ 10 s single-thread, ≤ 200 B/cell · 10 M cells ≤ 60 s on
8 threads · Bellinge boundary trim ≥ 50 % vertices removed at 5°/0.1h, 0 protected lost.

Engine round trip: lake at rest max |h−h₀| = 0, volume to 1e-9, sloping storm continuity 0.000 %.

## 7. Decisions taken (2026-09-29)

D1 option surface as above · D2 axis-aligned per-region frames + corridors replace the cross field ·
D3 bit-identity is a non-goal · D4 Triangle removed, not kept as a backend · D5 (Phase 5) the Mapped
patch table and quad-region modes are gone; corridors (`CorridorSourcesWidget`) are the only way to
place structured patches, and the placement/stitch validation stays on them (Q5 closed) · D6 (Phase
4) grading gate revised to core ≤ 2 / fringe ≤ 2.1 / median ≤ r, see §6 · D7 (2026-09-30) Phase 6b
approved and built: terrain break lines as non-coupling constraints, mixed cell shape as the default,
terrain alone grades the mesh · D8 the terrain tolerance also drives break-line extraction (one
number, one meaning) · D9 grading gate for meshes with terrain lines: median 1:1, none above 3,
≤ 0.1 % of faces above 2.1 (measured 0.003 % on a 1 km² curb grid). Open: Q1 hanging nodes in the
engine, Q2 outer ring as size seed (no), Q3 orthogonality sensitivity.

## 8. Build/verify workflow for this overhaul

Sources are edited in the repo; the mesh suites are built standalone in a Linux container (Qt 6.4,
GDAL 3.8) for fast iteration (harness CMakeLists kept at `test_artifacts/mesh_overhaul/`); the
full GUI build and ctest run on the Mac via `build-gui.sh` (log in `build/build-gui.log`). All test
outputs go under `tests/output/mesh_overhaul_2026-09/` (CLAUDE.md §4.1).

## 9. Sync to the Mac

The container copy is synced by patch: `git diff 6f0544b HEAD` from the container is written to
`test_artifacts/mesh_overhaul/patches/NNNN-*.patch` and applied on the Mac with `git apply`
(`--3way` if the branch moved). Applied on the Mac: 0001 (Phases 0–1), 0002 (Phases 2–6, with the
§5 Phase 5 deletions), 0003 (pipeline test adaptation). 0004 carries Phase 6b and the review fixes;
it is based on container commit 850e70d (the two workplan documents already on the Mac). The
container harness (`CMakeLists.txt`, `syntax.sh`) is copied to `test_artifacts/mesh_overhaul/harness/`.
