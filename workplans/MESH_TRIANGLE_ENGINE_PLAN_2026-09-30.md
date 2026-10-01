# Mesh Triangle Engine Plan — 2026-09-30 (Phase 8)

Status: IMPLEMENTED 2026-10-01 in the container (Phases 8.1–8.8, §9 as built); Mac validation pending
(`MESH_OVERHAUL_PHASE7_HANDOFF_2026-09-30.md` revision 3). APPROVED 2026-09-30 by the owner's answers
below; supersedes Stages 3–4 of
`MESH_OVERHAUL_PLAN_2026-09-29.md` (quadtree core + CDT fringe) and the Phase 6b mixed cell shape
(`MESH_OVERHAUL_PHASE6B_FEATURE_CAPTURE_2026-09-30.md` §2.2). Everything else in those plans stands:
size field v2, terrain tolerance, terrain break lines, boundary trimming, corridors, no Triangle.

## 1. What the owner asked for (2026-09-30)

> "I want to have the guaranteed mesh quality like the other models and really coarse meshes. …
> The mesh looks like it is using a lot of quad meshes unnecessarily." · "I don't necessarily want to
> use the triangle library." · "I just want triangular thinning and use quads when reasonable, and
> aligned with certain features." · "I want to confirm that the smooth transitions is going to be
> implemented."

Answers to the three decision questions:

| Question | Answer |
| --- | --- |
| Which features carry aligned quads? | Quad regions I draw · line layers with a width (corridors) · streets from the DEM · 1D conduit lines |
| Pair triangles into quads elsewhere? | No — triangles only |
| The quadtree engine? | Retire it |

## 2. Diagnosis of the current engine

- Open ground is a quadtree of squares; "Triangles" halves the squares. That is why the mesh is
  quad-heavy and never looks like a Delaunay mesh.
- The fringe refiner inserts circumcentres only when they are 0.3 h clear of every constraint and
  otherwise skips them, so no minimum angle is guaranteed next to lines. Triangle's guarantee comes
  from splitting a segment a new point would encroach (Ruppert), which the fringe never does.
- The default coarsening cap is 4 × the cell size at features, so open ground can never be coarse.

## 3. Decisions

- **D10 Triangles are the engine.** The whole domain is a constrained Delaunay triangulation refined
  by Ruppert/Shewchuk rules in our own kernel (`mesh/meshcdt`): encroached subsegments are split
  (concentric shells at input corners), bad triangles get Üngör off-centres, and a new point that
  would encroach a subsegment splits the subsegment instead. Guarantee: every triangle has a minimum
  angle ≥ θ (default 30°, user setting 20–33°), except (a) in the wedge of two input segments meeting
  at < 60° (Shewchuk's exemption, needed for termination) and (b) against fixed edges (quad-strip
  boundaries, which may not be split without breaking the strip). Max angle ≤ 180° − 2θ.
- **D11 No pairing, no quadtree.** `meshquadtree` and its test are deleted; the generator no longer
  pairs or smooths triangles (smoothing would break the guarantee). `meshquadmatch` /
  `meshquadcleanup` stay in the library (tested, predating the overhaul) but are unused by the
  generator — delete in a follow-up if pairing never returns.
- **D12 Quads only in aligned feature strips**, all stitched as structured patches (boundary edges
  fixed in the CDT, interior removed, quads appended):
  1. *Quad regions* (region layer, subcatchment list): a ring that `classifyQuadRegion()` finds
     four-sided becomes a mapped (transfinite) patch at the region's `h` or the local size. Other
     rings stay constraint loops with triangles inside; the region report says why.
  2. *Corridors* (line layers with a width, bank pairs): unchanged (`CorridorSourcesWidget`).
  3. *Streets from the DEM*: two accepted terrain break lines that face each other (parallel within
     20°, steady separation, nothing in between) with lower ground between them than on both outer
     sides become a bank-pair strip; the curbs are its long sides. Option "Quads between facing break
     lines (streets, ditches)".
  4. *Conduits*: option "Conduit quad strip width" (0 = off). Each conduit gets a swept strip of that
     width, an even number of rows (the conduit runs along the middle row, its edges keep the conduit
     marker/tag), cut back one width from each end so strips never collide at junctions; the ends stay
     plain constrained conduit edges.
  A strip that fails its checks (outside the domain, crossing or within h_min of another
  constraint, overlapping another strip, containing a node) is dropped with a log line; the feature
  stays a triangle mesh. Existing corridor and channel-burn patches keep their hard validation.
- **D13 Smooth transitions.** (1) The size field is gradation-limited at the user's neighbour ratio
  and seeded at every constraint, conduit, patch boundary and region ring (unchanged). (2) Strips
  take their cell size from the size field where they lie (along = h, rows = width / h, rounded), so
  a strip and its neighbours start at the same size. (3) Refinement splits any triangle larger than
  h(x); with θ = 30° two neighbouring triangles' longest edges differ by at most 1 / sin θ = 2, and
  quality refinement grades away from anything finer (a narrow ditch strip) at its natural rate.
  Measured, not assumed: grading stats in every generator test.
- **D14 Really coarse.** Default "coarsen away from features up to" 4 → 20 (preference default;
  saved preferences keep their value). Terrain tolerance is what keeps rough ground fine.

## 4. Algorithm (mesh/meshcdt `refineQuality`)

1. Input: CDT with constraints inserted, exterior / holes / patch interiors removed. Every constrained
   subsegment remembers its origin input segment; `setFixedConstraint(a, b)` marks an input segment
   unsplittable.
2. Queues: encroached subsegments (apex of a live adjacent triangle inside the diametral circle) and
   triangles to test. Subsegments are drained before each triangle.
3. Split subsegment (a, b): at the midpoint, or — when exactly one end is an input vertex with another
   segment at an acute angle — at the power of two in [L/3, 2L/3] from that end (concentric shells).
   The vertex is inserted on the edge topologically (`splitEdge`), both halves stay constrained and
   keep the origin. Fixed subsegments are never split.
4. Bad triangle: area > (√3/4)·(1.22 h(centroid))² (size; the factor makes the MEAN edge h — at
   factor 1 it is 0.82 h, measured) or smallest angle < θ (quality), unless the
   quality failure is Shewchuk's exemption (shortest edge between two segment Steiners on different
   segments that share an input vertex, equidistant from it within 0.1 %).
5. Point: circumcentre, or the off-centre on the shortest edge's bisector with apex angle θ when that
   is closer (Üngör; Triangle's 0.475 factor). Straight walk from the triangle to the point; a
   constrained edge on the way is split (fixed → give up on this triangle). Otherwise the point's
   cavity (triangles whose circumcircle holds it, bounded by constraints) is checked: any cavity
   subsegment whose diametral circle holds the point is split instead (fixed → give up). Otherwise
   insert, re-queue the star.
6. Safety: insertion cap from the expected cell count (8 × ∫ dA / ((√3/4) h²) + 20 × inputs +
   10 000) and cancellation every 4 096 insertions; hitting the cap is logged, never silent.

## 5. Option surface (Quality tab)

| Group | Control | Change |
| --- | --- | --- |
| Resolution | Cell size at features / Coarsen up to / Size ratio / Minimum cell size / Terrain tolerance | coarsen default 20 |
| Shape | ~~Cell shape~~, ~~Grid orientation~~ | removed |
| Shape | Minimum angle | new, 30°, 20–33° (preference `MeshMinAngleDeg`) |
| Shape | Quads between facing break lines (streets, ditches) | new checkbox, on |
| Shape | Conduit quad strip width | new, map units, 0 = off |
| Shape | Region layer / Subcatchments / Corridors | tooltips: four-sided rings get quads |

`GenerationOptions`: `minAngleDeg`, `quadsBetweenBreaklines`; `trianglesOnly`, `trianglesAtTerrain`, `frameAngleDeg` removed. `ConstraintSegment::stripWidth`.
`RefineHook::elevationAt` (street trough test); `terrainBoundAt` removed. `PatchMesh::lines`
(interior polylines emitted as boundary edges — the conduit under its strip).

## 6. Acceptance (container harness, then the Mac)

- Every triangle ≥ θ except those counted as exempt (small input angle) or fixed-edge neighbours;
  none < 10° outside those; reported per test.
- Neighbour longest-edge ratio max ≤ 2.0 (θ = 30°), median ≤ 1.3.
- Coarseness: on feature-free ground cells reach ≥ 0.7 × the cap.
- Area of the output = area of the domain minus holes (1e-9 relative); every interior edge shared
  by exactly two cells; every constraint realised as a chain of edges.
- Strips: quads only inside strips; every strip boundary edge shared with one triangle.
- 1 M triangles ≤ 10 s single-thread in the container.
- Coupling: every conduit edge (strip or not) carries its marker; every node is a vertex.

## 7. Files

New/rewritten: `src/mesh/meshcdt.cpp` (+ header), `src/mesh/meshgenerator.cpp` (+ header),
`include/mesh/meshpatch.h` (`PatchLine`), `tests/gui/test_meshcdt.cpp` (quality cases),
`tests/gui/test_meshstrips.cpp` (regions, conduit and street strips). Deleted:
`include/mesh/meshquadtree.h`, `src/mesh/meshquadtree.cpp`, `tests/gui/test_meshquadtree.cpp`.
Edited: size field (drop `terrainBoundAt`), worker/dialog, preferences, the mesh tests that set the
removed options, CMake (app, tests, harness), CHANGELOG, Phase 7 handoff.

## 8. Build and sync

Container harness first (`test_artifacts/mesh_overhaul/harness/`), then patch 0005 against the Mac
state after 0004 (`test_artifacts/mesh_overhaul/patches/`). The Phase 7 harnesses
(`test_mesh_overhaul_bellinge.cpp`, `test_mesh_overhaul_engine.cpp`) are adapted to the new options
in 0005 from their latest Mac copies.

## 9. As built (2026-10-01)

Commits on the container line `phase8-on-mac` (Mac-equivalent base `macbase2` = Mac state + patch 0004):
8.1 kernel quality refinement · 8.2 generator rewrite · 8.3 step-aware terrain size, strip sizing,
street trimming · 8.4 dialog, preferences, quadtree deleted · 8.5 review fixes · 8.6 crossing repair
· 8.7 constraint joining, conduit unfolding, strip bends · 8.8 Phase 7 harnesses. Shipped as patch
0005 (`test_artifacts/mesh_overhaul/patches/`).

### 9.1 Measured (container harness, 22 suites)

| Case | Result |
| --- | --- |
| City 1 km², 625 DEM lines, h = 1 m | 36 256 cells (was 1.1 M on the quadtree), 6 918 street-strip quads, min angle 30.01°, neighbour ratio max 1.99 / P50 1.08, 0.34 s mesh |
| Street between curbs | free-triangle min angle 30.2°, ratio max 1.95 |
| Graded field (h 2 → 80) | min angle 30.2°, ratio max 1.94 / P50 1.14 |
| Open ground, coarsen to the cap | 774 cells on 1 km², ratio max 1.96 |
| Generator, 1 000 × 1 000 at h = 1 | 2.42 M cells in 4.2 s (one thread) |
| Kernel refinement | 1.6 M triangles, 0.8 M points inserted in 2.0 s |
| Bellinge network (probe, nodes + 1 015 conduits, cell 10, no DEM) | as lines: 68.6 k cells, ratio max 2.17, 11 cells < 10° (all at two pipes leaving a manhole a few degrees apart); 4 m strips: 270 k cells (29.9 k quads), 835 strips placed / 207 dropped, ratio max 2.17, SJ ≥ 0.93 |
| Size conformity (probe, graded field) | mean edge / h: P50 0.88, 85 % within [0.7, 1.4] |

Review outputs: `tests/output/mesh_triangle_engine_2026-09-30/` (`feature_capture/` renders,
`review/` probes with their before/after images of the two Bellinge strip defects and
`bellinge_probe_output.txt`).

### 9.2 Beyond §3–§4 (found on real data and in review)

- **Joining (generator, before strips).** Constraint lines that cross or come closer than the
  refinement floor (0.25 h_min) share vertices: a vertex within that distance of another moves onto it
  (pinned nodes and domain vertices never move), a vertex that close to a segment is inserted into
  it, proper crossings get one shared vertex. Bellinge has 13 pipes crossing in plan (the CDT used to
  refuse them — Phase 7 had to drop conduits) and alignments passing centimetres apart that left
  0°–3° slivers no refinement could fix. Geometry moves by less than the floor.
- **Conduit alignments unfolded (worker).** `pslg::unfoldPolyline`: a vertex list stored from the
  to-node back is reversed when that is shorter; vertices where the path turns back by more than 150°
  are dropped. Bellinge: 3 reversed lists, 3 stray vertices (one 30 m back along the pipe).
- **Strips.** Conduit strips are cut into runs at crossings and at turns sharper than 45° (the inner
  side of a swept strip folds there), each run stopping short like at a junction; the strip
  centreline is resampled so no station interval is under half the along spacing (a vertex 1.5 cm
  past the cut left a 1.5 cm row of quads). Strip edge lengths are bounded by the size field along the
  strip and by the room to the nearest other constraint; terrain lines keep 0.75 × edge length from
  strip edges; street pairs are trimmed by a quarter width at their ends; manholes on a street split
  its strip instead of dropping it.
- **Kernel.** The small-angle exemption is keyed on input pieces (a vertex lying on a segment);
  overlapping constraints keep each chain; a refinement floor `minEdge = 0.25 h_min` stops cascades at
  θ above 30° (hence the 33° UI cap).
- **Size field.** Inside the cone of a DEM step the generator keeps as an edge, the terrain term is
  ignored (the step is captured by the edge, so it needs no fine cells); steps come from
  `MeshGenerator::previewTerrainBreaklines()`.
- **Stats.** `GenerationStats` (strips placed/dropped, refinement inserts, triangles under θ that the
  exemption does not explain) is logged and carried on `PipelineResult::generationStats`.

### 9.3 Limitations

- A quad region whose ring touches the domain boundary or another region gets triangles (the strip
  checker needs h_min of room round it); the region report says so.
- Inputs at a small angle (two pipes leaving a manhole a few degrees apart) keep cells under θ there —
  inherent in any mesher; counted, never hidden.
- A DEM step cut back near a strip loses its own refinement there.
- Orthogonality gates from the quadtree era do not apply to triangles (reported only).
- Strips narrower than the cell size make the cells beside them smaller than h by design (Bellinge
  with 4 m strips at cell 10: four times the cells of conduits as lines).
- `meshquadmatch` / `meshquadcleanup` are unused by the generator (D11).

