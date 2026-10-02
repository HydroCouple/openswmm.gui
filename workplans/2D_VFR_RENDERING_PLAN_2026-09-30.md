# VFR plan for consistent 2D profiles and depth contours

Date: 2026-09-30
Status: Implemented; continuous display correction passed nine regression suites and visual fixture review
Scope: 2D results reconstruction, profiles, depth contours, wet boundaries, and maximum envelopes in `openswmm.gui`, with a narrow shared geometry and output contract in `openswmm.engine`.

The implemented solution reconstructs each cell's water level from its stored water volume and mesh terrain, projects levels across connected wet edges for a continuous display, finds the projected wet boundary analytically, and gives profiles and contours the same reconstruction. The testing correction below supersedes the original independent-cell display proposal. Later investigation and proposal sections are retained as historical context.

Here VFR means **volume/free-surface relationship**. “Exact interface” means exact, within numerical tolerance, for the chosen piecewise planar terrain and reconstructed surface at an available output time. Cell averages cannot identify the unique real surface, unresolved terrain, or motion between saved timesteps. Water advancing up a bed slope can also be physically valid: backwater, pond filling, inertia, and wave run-up must not be suppressed by a blanket rule that water cannot rise uphill.

The initial investigation did not change production code. It used a freshly compiled diagnostic to exercise the then-current production helpers. The user's specific result file, location, and timestep remain unidentified; those findings establish possible mechanisms, not attribution of that particular observation. Rendering changes were subsequently authorized and implemented; solver code remains unchanged.

## Animation correction (2026-09-30, verified and launched)

The strict current-frame async contour cache introduced an unintended visual transition: while marching was pending the map drew flat cell colours and removed isolines. The worker now produces bands and isolines together from one immutable scalar snapshot. The renderer retains a complete frame, including compact depth/velocity attributes for optional direct fills and arrows, while a single background job computes the replacement. Rapid ticks coalesce; completed intermediate frames can display so continuous playback cannot starve. Source/geometry changes and changed contour levels synchronously bootstrap a complete frame and reject incompatible or older pending results. Panning/zooming can rebuild from the retained full-mesh frame. The displayed frame may lag the animation cursor when rendering cannot keep up; no interpolation or solver data changes are introduced.

Automatic range setup uses each cell's temporal peak mean depth to bound the VFR corner depths before playback. This bound can exceed the largest actual smoothed depth; the tradeoff avoids reconstructing every full surface at load time and prevents the colour range expanding on a first visit to a sloping wet frame. Live data can still extend the known range. Fixed statistical classifications use one peak mean-depth sample per cell across the available history; explicit per-frame classification retains current-frame samples. The band legend uses the same reference samples.

Regression coverage: paired band/line/direct-fill/velocity frame publication, producer faster than worker, same-time replacement, dry/re-wet, pending style/source replacement, bounded scalar ranges, stable fixed classifications, and exact shoreline preservation. Artifacts: `tests/verification/animation_frame_artifacts/`. Nine regression suites passed: cellwatergeometry, meshprofile_shoreline_render, contourjob, vertexdepthreconstruct, map_pooling_extrapolation, profile_wse_extrapolation, meshprofileinterp, 2dresults_vizfixes, and live2d_coalesce. A second run of the renderer suite with forced background marching passed all 21 QtTest cases (zero skips). The app uses the isolated `build/animation-verification` directory because other tasks were building in `build` concurrently. The full build exited successfully. Strict/deep bundle signature verification passed, and the isolated app launched as PID 73913 at 19:18:13 EDT with existing instances preserved. See `signature.log` and `launch.log` in the artifact directory. Changes remain uncommitted for user testing.

## Testing correction: continuous display surface

The user tested the first implementation and requested a smooth water surface rather than independent flat cell patches. This supersedes the initial cell-constant display recommendation below. Retain the exact VFR inversion as the source of each cell's water level, then project those levels to continuous, area-supported vertex values within connected wet fans. A connection requires a shared edge with positive wet overlap from both cells; point contact, dry cells, non-manifold edges, and closed bed crests do not connect fans. Signed values at high terrain corners participate in the same projection, avoiding the former wet-only interpolation and maximum-stage extrapolation.

Maps, point queries and exact profile intervals consume that one piecewise-linear surface. Profile water lines join continuously where their endpoint values agree. The smoothing is a display projection and does not preserve each cell's displayed volume exactly; stored simulation volumes remain unchanged. The temporal envelope takes maxima after reconstructing actual frames, retaining separate values for disconnected fans and keeping the replaceable latest frame outside its frozen cache. Its interpolation is an upper envelope of corner extrema, not a claim of an exact pointwise temporal maximum within every triangle.

Verify a sloping surface across mixed triangle/quad boundaries, identical profile and contour traces, a constant lake across wet and dry terrain corners, dry crests and point-touching pools, datum invariance, same-time replacement frames, and the existing shoreline/dry-gap regressions. Render a production profile fixture that would visibly reveal flat plateaus or jumps.

Validation completed: all nine rebuilt regression suites passed, as did a forced asynchronous contour run. The production profile fixture shows a continuous sloping surface without cell-height jumps; the dry-gap and contour fixtures retain their clipped shorelines. See [implementation and verification notes](../tests/verification/vfr_smooth_surface_artifacts/implementation-notes.txt), [test log](../tests/verification/vfr_smooth_surface_artifacts/tests.log), and [profile image](../tests/verification/vfr_smooth_surface_artifacts/profile-continuous-surface.png).

### Testing correction: pale vertical fill seams

Subsequent testing reported vertical white streaks. A controlled profile fixture reproduced pale vertical seams when identical continuous geometry was split into cell intervals: independently antialiased fills blend their shared edges against the background. This affected current water, the maximum envelope, and soil fills. Draw each pass as one compound winding path, keeping separately closed subpaths for clipped intervals and genuine gaps. This cancels internal shared edges before rasterization without expanding wet geometry, overlapping translucent polygons, or disabling shoreline antialiasing.

The new regression compares one interval against thirteen intervals for all three fills, opaque and translucent colors, and display scales 1, 1.5, and 2. All eighteen variants failed before the change and pass afterward; existing shoreline and dry-gap tests also pass. See [before](../tests/verification/vfr_seam_artifacts/before/profile-cell-seams.png), [after](../tests/verification/vfr_seam_artifacts/after/profile-cell-seams.png), and [test results](../tests/verification/vfr_seam_artifacts/after/tests.log).

## 1 Evidence from the current implementation

| Finding | Current implementation | Consequence |
| --- | --- | --- |
| Exact planar storage inversion already exists | `include/layers/vertexdepthreconstruct.h`, `cellEtaFromMeanDepth` and `quadEtaFromMeanDepth`; engine `src/engine/2d/mesh/VfrClosure.hpp` and `QuadVfr.hpp` | Reuse and unify this mathematics rather than introducing another VFR variant. |
| Cell surfaces are averaged into one value per mesh vertex | GUI `reconstructVertexSignedDepths`; engine `reconstructVertexRenderDepths` | Neighboring pools can influence each other through a shared vertex. The displayed volume is not constrained to equal each cell's volume. |
| Profiles and contours do not represent the same surface in general | `CellSurfaceInterp::depthAt` renormalizes wet-corner weights; `extrapolateDryCorners` assigns the maximum wet-corner stage to a dry corner, followed by linear contour interpolation | The constant-pool regression works, but different wet-corner stages create a contour surface that rises toward the dry corner. |
| Missing surface can become displayed water | `MeshProfileInterp::bridgedTops` bridges a gap when it contains at least one `cellHasSurface == false` sample | A no-surface or solver-dry interval can become a wet connection in the profile. |
| Shoreline location depends on sampled points | `MeshProfileSampler::buildMeshProfile` uses regular samples and refines one crossing when successive containing triangles differ; `shorelineIntercept` extrapolates the last wet samples | Multiple thin cells, dry crests, mesh holes, or narrow wet intervals can be missed between samples. |
| Engine and fallback reconstruction differ on mixed meshes | GUI weights triangle contributions by `h` and quad contributions by `0.75h`; engine uses `h` for both | Presence of `/Mesh2_node_depth` can change the displayed result. This difference is established by code inspection; its impact on a particular mixed mesh still needs measurement. |
| Fallback accumulation uses absolute elevations in float | GUI `vsum`, `wsum`, and `w * float(eta)` | Small water depths change with elevation datum. Engine accumulation uses double. |
| Maximum envelopes follow a separate path | `maxDepthPerVertex` always uses fallback reconstruction, then `maxDepthAtSceneInterp` interpolates vertex maxima | It can disagree with source-provided animation fields and combine maxima from different times into an artificial surface. |
| Solver head is available but not equivalent to the render surface | `/Mesh2_face_head`, `EngineMesh2DSource::pushHeads`, regularized VFR in the engine | A head value must not be treated as an exact volume-conserving geometric surface without checking its closure and regularization. |

### Numerical reproduction

The diagnostic is [vfr_rendering_diagnostic.cpp](../tests/verification/vfr_rendering_diagnostic.cpp), with [recorded results](../tests/verification/vfr_rendering_artifacts/diagnostic.txt). It includes the real reconstruction, profile, and marching-triangle headers.

For a triangle at `(0,0), (1,0), (0,1)`, let bed elevations be `(0,0,4)` and the incoming wet-corner stages be `(1,2)`, with no surface at the high corner. Current extrapolation produces signed depths `(1,2,-2)`. On the line `x=(1-y)/2`, the profile stage is 1.5 m, while the map stage is `1.5 + 0.5y` wherever wet.

| Position y | Bed elevation m | Profile depth m | Contour field depth m | Contour field stage m |
| --- | --- | --- | --- | --- |
| 0.20 | 0.80 | 0.70 | 0.80 | 1.60 |
| 0.30 | 1.20 | 0.30 | 0.45 | 1.65 |
| 0.375 | 1.50 | 0 | 0.1875 | 1.6875 |
| 0.40 | 1.60 | 0 | 0.10 | 1.70 |

The profile shoreline is at `y=0.375`; the map zero contour crosses this line at `y=3/7`, approximately 0.428571. These are competing reconstructions of the supplied corner data, not an assertion that either is the physical truth for a cell whose volume has not been specified. They prove that the two consumers disagree and that the map adds an upslope stage gradient.

Two further diagnostic observations:

- A three-sample profile with depths `(1,0,1)` and no valid surface at the middle sample paints 1 m of water at that middle sample.
- A flat cell containing 0.001 m mean depth reconstructs approximately 0.00100000005 m at datum zero, 0.00102538615 m at datum 1,000 m, and 0.00287964917 m at datum 100,000 m. The last datum is a numerical stress case, not a claim about ordinary terrain elevations.

The six existing executables `test_contourjob`, `test_meshprofileinterp`, `test_meshprofile_shoreline_render`, `test_profile_wse_extrapolation`, `test_vertexdepthreconstruct`, and `test_map_pooling_extrapolation` all passed. See [baseline log](../tests/verification/vfr_rendering_baseline_2026-09-30.txt). These were the existing build's binaries; only the new diagnostic was freshly compiled against the current headers. No full rebuild or interactive visual verification is claimed.

## 2 Relationship to earlier plans

Continue the useful requirements in [the profile surface plan](2D_PROFILE_WSE_EXTRAPOLATION_PLAN_2026-08-02.md), [the map pooling plan](2D_MAP_POOLING_EXTRAPOLATION_PLAN_2026-08-04.md), [the shoreline handoff](HANDOFF_PROFILE_SHORELINE_INTERCEPT_2026-08-23.md), and [the mixed mesh plan](TRI_QUAD_MESHING_PLAN_2026-09-06.md): retain subcell boundaries, consistent terrain, support for triangles and quads, no propagation across arbitrary dry regions, and no solver changes disguised as rendering fixes.

This proposal explicitly revisits two earlier choices. It does not silently implement their replacement:

1. **The one-cell dry halo.** Earlier pooling behavior intentionally borrows a neighbor's water into a solver-dry cell. That can remove a visual truncation, but it assigns positive displayed volume to a zero-volume cell. Recommend retiring it from the authoritative depth display. A cell with positive stored volume still gets its exact partial wet area, even when that area is small. If neighbor extension remains useful for diagnosis, identify it as an inferred overlay and exclude it from reported depths and inundation statistics.
2. **Painter-side shoreline extrapolation.** The handoff previously selected this approach. Recommend replacing it with exact intersections generated from the shared cell surface, because the two nearest wet samples cannot establish all terrain crossings. Keep the painter responsible for drawing the supplied segments, not reconstructing new water.

The present request is to develop a plan. These are proposed design changes for review, and all previous production behavior remains in place at this stage. No new end-user preference is needed merely to test the candidate implementation; use an internal comparison switch during development.

## 3 Mathematical reconstruction

### Recover the surface from cell storage

For cell C with area A, stored volume V, and terrain `z(x,y)`, solve for a horizontal geometric surface eta:

```text
V = integral over C of max(eta - z(x,y), 0) dA
hbar = V / A
```

Use the engine's exact VFR with regularization epsilon zero for geometric rendering. For a quad, solve once for the whole cell using the area-weighted sum of its two storage triangles. Both triangles share that eta. Never apply the quad mean depth independently to both triangles, change the diagonal for visual convenience, or replace the terrain with a bilinear quad patch.

For reference, the current exact triangle storage relation, with `z1 <= z2 <= z3` and `zbar=(z1+z2+z3)/3`, is:

```text
eta <= z1:       hbar = 0
z1 < eta <= z2:  hbar = (eta-z1)^3 / [3 (z2-z1) (z3-z1)]
z2 < eta < z3:   hbar = eta-zbar + (z3-eta)^3 / [3 (z3-z1) (z3-z2)]
eta >= z3:      hbar = eta-zbar
```

Use explicit limits for coincident elevations and flat terrain. Inversion must be bracketed and monotone; verify a volume residual, not just a change in eta. Perform calculations relative to a nearby bed datum in double precision, then store relative signed depths as float only at a GPU boundary where its error is acceptable.

### Locate wet boundaries and depth contours on edges

Parameterize an edge from A to B as `p(t)=A+t(B-A)`, `0 <= t <= 1`. Let `qA=etaA-zA` and `qB=etaB-zB` be **unclamped** signed depths belonging to the same cell surface. For contour depth k:

```text
t = (qA-k) / (qA-qB)
p_interface = A + t (B-A)
```

The physical wet boundary uses `k=0`. A visible depth threshold uses `k=h_display`; it is a different boundary and must be named accordingly. For constant eta and a rising bed the shoreline reduces to `t=(eta-zA)/(zB-zA)`.

Only emit an intersection when endpoint signs relative to k bracket it. Handle both-wet, both-dry, endpoint equality, the entire edge lying on the contour, nearly parallel surfaces, zero-length edges, and missing values explicitly. Clamp t only for roundoff near a valid interval; do not manufacture an intersection by clamping an out-of-range solution. Reversing the edge must yield the same physical point.

Clip each storage triangle against `q >= 0` to obtain its wet polygon. Clip the same signed field against contour levels for bands. Computing `max(q,0)` before interpolation would move the shoreline toward dry vertices and recreate the original problem.

For diagnostics, the mean edge depth is the integral of `max(q(t),0)` over `t in [0,1]`. With a horizontal surface and `zA < eta < zB`, it equals `(eta-zA)^2/[2(zB-zA)]`. This useful flux-related quantity is not the local depth at the edge midpoint and must not replace the geometric contour field. The distinction is also made by the modified face reconstruction in [Begnudelli and Sanders 2007, equation 14](https://www.researchgate.net/publication/235731558_Conservative_Wetting_and_Drying_Methodology_for_Quadrilateral_Grid_Finite-Volume_Models).

### Preserve real gradients without forcing continuity

Start with a horizontal surface per cell as the conservative reference implementation. It is exact for a lake at rest initialized with the same terrain storage relation. Different cell volumes can legitimately produce different stages on the two sides of a shared edge; retain both traces instead of averaging them or selecting the larger one. A visible finite-volume step is preferable to a fictitious water connection.

If smooth interior gradients are necessary, add a separately validated reconstruction:

```text
eta(x,y) = alpha + gx (x-xc) + gy (y-yc)
```

Obtain limited gradients from valid, hydraulically connected neighbors or compatible saved gradient fields; preserve jumps and barriers. Holding the gradient fixed, solve for alpha so the clipped depth integrates to V. This can reuse triangle VFR by replacing each bed elevation with `z-g dot (x-xc)`; quads retain their original storage diagonal. Reduce the gradient and fall back to horizontal VFR if volume, bounds, or topology requirements cannot be met. This extension is unnecessary for the first correctness milestone and must not restore unrestricted vertex averaging.

## 4 Source data and semantic contract

The authoritative input for the proposed geometric display is the cell's volume, or `Mesh2_face_depth` interpreted as `V/A`, plus the exact result mesh. Carry these concepts separately:

| Quantity | Meaning and use |
| --- | --- |
| Stored volume or mean storage depth | Conserved input to the geometric reconstruction. |
| Geometric stage | Exact VFR inversion for the supplied volume and terrain. Drives depth geometry. |
| Solver head | Solver state using FLAT or regularized VFR closure. Retain for hydraulic diagnostics; do not silently replace it. |
| Signed local depth | Geometric stage minus terrain, valid even when negative. Drives clipping. |
| Wet area fraction | Derived geometry, not a substitute for volume. |
| Data validity | Explicit missing, invalid, dry, or valid state. Zero is a legitimate depth and must not encode missing data. |

Under regularized VFR, solver head and geometric stage can differ in the low-volume tail. Under FLAT closure, they can differ throughout a partially wet cell. The renderer cannot simultaneously preserve that solver head, exact terrain clipping, and stored volume in those cases. Recommend volume-preserving geometry as the depth display, with solver head available as a clearly labeled diagnostic. Changing solver defaults or re-running models is a separate decision.

Audit existing live and HDF5 adapters for depth meaning, units, face IDs, mesh connectivity, terrain elevations, result precision, and frame synchronization. Use source mesh geometry for both ground and water in result profiles. Detect a changed project mesh rather than combining current project terrain with old results. Convert length, area, and volume consistently to SI for reconstruction; convert presentation values afterward.

Add backward-compatible metadata only where absent: closure, VFR regularization, solver dry threshold, storage semantics, reconstruction version, mesh identity, and units. Existing depth datasets suffice for the initial geometric path. Prefer optional per-cell double or datum-relative geometric stage over large edge-intersection datasets if profiling later warrants an engine-provided cache. Any cached stage must satisfy the same volume residual and mesh/version contract.

Legacy node-depth files remain readable but their averaged vertex fields must not override available cell storage in the new authoritative path. Node-only sources lacking cell storage can offer an explicitly approximate legacy view; they cannot claim volume conservation or exact cell interfaces.

## 5 Shared geometry and ownership

Introduce a small, testable reconstruction component consumed by `SWMM2DResultsLayer`, contour jobs, profiles, point queries, and exports. Suggested responsibilities are:

- Static geometry: stable cell and edge IDs, storage triangles, projected coordinates, terrain planes, areas, adjacency, barriers, and quad diagonal.
- Frame data: per-cell volume and validity, reconstructed stage or plane, wet classification, and provenance.
- Operations: evaluate local signed depth, return wet intervals on an edge, clip a triangle to a band, intersect a profile segment, and integrate displayed volume.

Use one source of VFR arithmetic. Prefer a dependency-light shared header in the engine's public geometry surface with GUI compatibility tests; avoid adding a GUI dependency on private engine solver types. An interim adapter may mirror it only with randomized parity tests and a documented removal path.

Represent a physical mesh edge once, with separately owned left and right cell traces. Its canonical orientation supplies consistent coordinates and tolerances. A cache key includes edge ID, owning side, contour level, mesh generation, frame revision, and reconstruction policy. Deduplicate intersections only when the two surface traces actually agree. Equal coordinates alone do not authorize joining contours across a barrier or hydraulic jump.

Vertex contact alone must not merge distinct wet regions. Connectivity requires a compatible wet interval of positive length on the shared edge and no blocking structure. The diagonal inside a quad is a terrain break, not an independent hydraulic boundary; the two halves use the same cell surface. True walls or discontinuous beds need their own topology and crest semantics; if unavailable in an old result, report that limitation rather than infer a connection from node proximity.

## 6 Profile implementation

Replace sample-driven discovery with intersections against the result mesh:

1. Query candidate cells for every segment of the user's polyline using the existing spatial index or an edge traversal.
2. Enumerate all intersections with physical cell boundaries and internal storage diagonals. Sort by chainage and resolve vertex hits, paths lying on an edge, repeated points, mesh holes, and nonconvex quad pieces deterministically.
3. Within each resulting terrain interval, evaluate the shared surface and solve every `q=0` or display-threshold crossing analytically.
4. Emit explicit wet segments and dry or missing gaps. Preserve left and right values at a discontinuity; do not connect them with a diagonal ramp across a dry interval.
5. Paint the supplied polygons and lines directly. Remove automatic dry-gap bridging and sampled shoreline extrapolation from this new path.

The existing regular sample spacing can remain for cursors, tables, or optional resampling. It must not control shoreline topology. The 2,000-sample cap cannot discard mandatory boundary events: stream or draw the exact segments and decimate only interiors whose omission stays within a visual error budget. When a path lies on a hydraulic boundary, expose both sides or apply a documented deterministic side selection.

Cache path/mesh intersections independently of timesteps. Per-frame updates evaluate water on those intervals and insert the moving shoreline endpoints. Ground, water, cursor depth, exported profile values, and maximum envelope must share the same units and geometry.

Main files: `include/plot/profilesection.h`, `src/plot/meshprofilesampler.cpp`, `src/ui/dialogs/meshprofileplotdialog.cpp`, `src/plot/meshprofileplotwidget.cpp`, and the compatibility boundary in `include/plot/meshprofileinterp.h`.

## 7 Depth contour and rendering implementation

Route `SceneTri` scalar data and `ContourJobInput` through the shared per-cell surface. The marching algorithms already accept per-triangle scalar values; retain them where their degeneracy behavior passes the new geometry tests. Separate physical wet clipping from palette classification so a custom lowest band cannot make dry terrain wet.

CPU bands, asynchronous QSG bands, isolines, smooth fills, picking, and depth queries must evaluate the same q field. Clip smooth-fill geometry or use an equivalent scalar-aware shader clip; interpolating vertex alpha cannot locate an exact wet boundary. Indexed smooth-fill buffers keyed only by global mesh vertex cannot represent distinct cell traces. Use cell-corner vertices or explicit clipped pieces; do not use a last-writer-wins assignment or max reduction.

Maintain cell ownership through contour chaining and band triangulation. Prevent joining isolines across a dry gap, mesh hole, barrier, or differing edge traces. Suppress duplicate segments on shared continuous edges and the internal quad diagonal without hiding true discontinuities. At coarse zoom, retain wet topology and avoid drawing complete wet triangles merely because one corner is wet.

Frame snapshots must include mesh generation, source identity, time index, same-time revision, and display settings. Cancel or discard stale contour jobs when any change. This includes live frames where depth, head, and node fields arrive separately at the same time.

Main files: `src/layers/swmm2dresultslayer.cpp`, `include/layers/swmm2dresultslayer.h`, `src/render/contourjob.cpp`, `include/render/contourjob.h`, `src/map/swmm2dresultsqsgrenderer.cpp`, and `include/contour/marchingtriangles.h`.

## 8 Maximum envelopes and animation

Label an envelope as the maximum over the available frames or solver-recorded extrema, never as an instantaneous water surface. Sparse output cannot recover unsaved peaks or the shoreline's exact travel between frames.

For the initial horizontal-per-cell reconstruction on fixed terrain, stage is monotone in volume. Therefore reconstructing each cell from its maximum stored volume gives exactly the pointwise maximum of that cell's reconstructed depths over the same frames. This is a safe shortcut unavailable to the present shared-vertex blend. Use solver whole-run maximum depth only after verifying that it represents the same storage quantity and mesh; identify that time coverage separately from loaded-frame maxima.

For any later sloping-plane reconstruction, evaluate `max_t depth(x,t)` from the actual frame reconstructions. Do not assume interpolation of vertex maxima equals the maximum of interpolated depths. A profile envelope may need extra breakpoints where different frame surfaces cross. Cache this work incrementally; invalidate on source changes, altered historic frames, terrain edits, reconstruction changes, or history replacement.

Separate the envelope's ever-wet state from the current frame's validity. Scrubbing the animation must not change the already computed historical envelope merely because `cellHasSurface` changed this frame. Default animation should show recorded states. If temporal interpolation is offered later, interpolate conserved storage and reconstruct again; do not interpolate clipped polygons or invent unobserved maxima.

## 9 Implementation sequence and completion gates

| Phase | Deliverable | Completion gate |
| --- | --- | --- |
| 0 Reproduce and classify | Capture model/frame/path and raw versus displayed values; keep the synthetic cases from this investigation | Determine whether the reported case is reconstruction, sampling, source/mesh mismatch, asynchronous state, or solver behavior; retain a small reproducible fixture |
| 1 Define the contract | Shared exact VFR, explicit validity, canonical geometry, separate solver head, source metadata audit | Triangle/quad round trips, independent clipped-volume integration, datum invariance, units and engine/GUI parity pass |
| 2 Build the reference geometry | Horizontal cell surfaces, edge intervals, wet polygons, deterministic side ownership | Volume, shoreline, topology, and edge-reversal invariants pass without neighbor halos |
| 3 Integrate profiles | Exact path traversal and wet segments using reference geometry | Narrow cells and dry gaps survive coarse sampling; profile depths and boundaries equal direct reconstruction |
| 4 Integrate all map consumers | Contour bands/isolines, CPU/QSG fills, picking, exports, async revisions | Profiles and maps agree numerically; CPU/GPU images have matching wet boundaries; no stale-frame reappearance |
| 5 Unify time and legacy behavior | Cell-based envelopes, live/HDF5 parity, legacy fallback, source provenance | Scrubbing, same-time replacement, history thinning, missing datasets, and export/reopen remain consistent |
| 6 Validate and roll out | Regression fixtures, screenshots, performance records, documentation | All mandatory gates below pass; retire conflicting heuristics only after comparisons are reviewable |
| Optional later work | Limited sloping planes or richer subgrid terrain | Repeats all volume and topology gates; demonstrable benefit over the horizontal reference |

Do not change solver closure defaults as part of phases 1–6. If phase 0 shows water is actually stored above the expected physical shoreline, run a separate solver investigation using FLAT versus VFR and MEAN versus VFR_FACE on reproducible fixtures. Rendering cannot correct an erroneous volume distribution without changing the meaning of the results.

## 10 Verification and acceptance criteria

Use analytically defined volumes and an independent polygon clipping/integration oracle, so tests do not merely compare a VFR function against its own inverse. Keep artifacts in `tests/verification` or a documented test artifact directory.

| Test family | Required cases and assertions |
| --- | --- |
| Exact storage | Flat, one-wet-corner, two-wet-corner, fully wet; repeated elevations; very small positive volume; unequal-area and nonplanar quads; all supported diagonal choices |
| Wet edges | Wet/dry, dry/wet, exact endpoint hits, fully coincident contour, nearly flat edge, reversed vertex order, several depth levels, no out-of-range crossings |
| Equilibrium | Analytic lake at rest on an adverse plane; shore crossing cells; uniform stage preserved where compatible with the exact closure |
| Connectivity | Two ponds separated by a dry crest; zero-volume neighbor below the pond stage; cells meeting only at a vertex; barriers and bed steps; no invented connecting wedge |
| Existing defects | Two wet corners with different stages; `(1,0,1)` no-surface profile; mixed triangle/quad source parity; high datum and translation invariance |
| Profiles | Multiple cells between old sample stations; narrow pond; mesh hole; repeated path vertex; along-edge path; reversed path; quad diagonal; coarse/fine station spacing |
| Rendering | Numerical depth and shoreline parity across profile, contour, point query, CPU and QSG; indexed and expanded fills; all palette lower limits; zoom and LOD changes |
| Time | Wet/dry transitions, out-of-order background completion, same-time field arrival, modified latest frame, envelope peaks at different times, history reset, sparse output |
| Source compatibility | SI and US units; high projected XY; live versus saved/reopened; node data present/absent; old negative signed depths; nonfinite values; missing fields; terrain mismatch |
| Physical transients | Thacker oscillation/run-up, dry and wet dam breaks, bumps and hydraulic jumps; preserve legitimate adverse-bed advance and discontinuities |

Initial numerical targets, to be confirmed against scale sweeps rather than weakened to make failures pass:

- With local coordinates and double input, normalized edge-intersection error at most `1e-10` and stage error at most `1e-10 * max(1 m, local relief, local depth)` on ordinary nondegenerate fixtures.
- Independently integrated cell volume error at most `1e-10 * max(V, A * 1e-6 m)` for those double fixtures. For float result files, propagate storage quantization into an explicit separate tolerance.
- No positive-area wet polygon in a cell with exactly zero storage. For a configured display threshold, report or track the small volume omitted from visibility; do not confuse that omission with a failed reconstruction.
- Exact equality of shared reconstruction results at the same position, side, frame, and policy before GPU conversion. Screen shoreline disagreement no larger than one physical pixel from rasterization/antialiasing, with world-space tests passing independently.
- A test initialized to a compatible uniform stage gains no upslope stage gradient through reconstruction. Do not apply this assertion to a real transient with a nonuniform stage.
- Profile topology and shoreline positions are invariant to station spacing, path reversal, and vertex numbering within their geometric tolerances.

Use the existing examples `swashes_lake_at_rest_immersed`, `swashes_thacker_radial_2d`, Ritter/Stoker dam breaks, and the bump cases after checking their actual solver options and output freshness. `demo_vfr_slope` is useful for FLAT/VFR comparison, but its tutorial documents stale outputs and missing VFR selection in the shipped setup; do not accept it as a reference without regenerating and recording its settings.

## 11 Performance and operational constraints

Precompute cell geometry and quad splits once. Reconstruct each cell once per changed frame, with dry and fully wet fast paths. Cache profile crossings once per mesh/path revision. Reuse buffers and compute visible contour pieces off the UI thread. Do not store every edge intersection for every time and every contour level.

Measure 20,000, 160,000, and 1,000,000-cell cases, plus a large mixed mesh, at fixed viewports and contour counts. Record frame reconstruction time, contour completion latency, interaction responsiveness, peak memory, and geometry size against the existing renderer. A double stage plus one validity byte costs about 9 MB per million cells before alignment; budget extra arrays explicitly instead of retaining redundant full-mesh copies.

Proposed rollout budget: at most 20% increase in median reconstruction-plus-contour work on the same benchmark, no unbounded queue during playback, and bounded memory proportional to cells, visible contour geometry, and path intersections. Treat these as acceptance targets requiring measurement, not achieved performance claims. If clipping costs exceed the budget, optimize geometry caching and draw batching while preserving the reference geometry; do not return to endpoint alpha interpolation or dry-gap bridging.

Keep an internal comparison mode until the reference fixtures and the user's affected scene are reviewable. Remove the old authoritative path only after legacy result handling, envelopes, and exports are migrated. Preserve diagnostic access to raw cell storage and solver head to make future rendering discrepancies explainable.

## 12 Research basis and limits

The following primary sources inform the proposal; the repository evidence above supplies the implementation-specific findings.

- [Begnudelli and Sanders 2007, Conservative Wetting and Drying Methodology for Quadrilateral Grid Finite-Volume Models](https://www.researchgate.net/publication/235731558_Conservative_Wetting_and_Drying_Methodology_for_Quadrilateral_Grid_Finite-Volume_Models), author-uploaded paper, DOI `10.1061/(ASCE)0733-9429(2007)133:3(312)`. Supports distinguishing storage from local depth, storage geometry for nonplanar quads, and modified face reconstruction. Its reported runtime overhead is not a performance forecast for this GUI.
- [Kim and colleagues 2014, Mesh type tradeoffs in 2D hydrodynamic modeling of flooding with a Godunov-based flow solver](https://escholarship.org/content/qt22x190wg/qt22x190wg_noSplash_75e50d58526af2eeb22c46139752476d.pdf), Appendix A. Supports the mixed triangle/quad terrain model and VFR context. Its simplified piecewise linear storage approximation is not proposed as a replacement for the existing exact geometric inversion.
- [Bollermann, Chen, Kurganov and Noelle, A well-balanced reconstruction for wetting/drying fronts](https://arxiv.org/abs/1412.3580). Supports the need to preserve equilibrium and positivity at partially wet fronts. The paper's reconstruction is presented in one dimension; it is supporting reasoning, not a ready-made 2D renderer.

Subgrid DEM storage tables can improve interfaces where actual terrain is poorly represented by the mesh, but require corresponding solver geometry and output contracts. They are a later extension. Three-dimensional VOF/PLIC interface tracking is unnecessary for this depth-integrated rendering problem. Edge intersections are analytically computable once the cell surface is defined; the essential task is to define that surface consistently and preserve its ownership and volume throughout the display pipeline.


## 13 Follow-up: profile shoreline and saturated thematic ranges (2026-10-01)

User evidence: a bank gap in the 1D road-culvert inundation overlay, apparent
pooling near dry neighbors in mesh profiles, and holes above a depth contour
maximum. This follow-up changes display geometry only; solver storage is unchanged.

- Replace the 1D overlay's regular wet-sample runs with cached exact results
  triangle intervals. Use the result bed and signed water surface, map geometric
  distance to authored link chainage, and clip against both result bed and drawn
  ground. Split at ground/axis knots and fill a compound path to avoid seams.
- Retain the opposite triangle for an exactly boundary-aligned mesh profile.
  A dry primary owner no longer hides water on the other side; resolve that
  fallback again on each animation frame. Never extend this rule into cell
  interiors or across dry barriers. The maximum envelope includes both sides.
- Extend the first/last contour bands to cover finite out-of-range values.
  Keep dry-depth visibility separate from color minimum; preserve interior
  crossings by clipping original scalars rather than clamping vertex values.
  CPU and QSG/async paths use the same rule, including flat bands. The shared
  marcher also fixes generic mesh/terrain contour fills. Existing feature,
  node/link, continuous-ramp and raster defaults already saturate; explicit
  NoData and user-selected masking/sentinel styles keep their separate meaning.
- Road-culvert file evidence at 2026-01-01 17:45: reconstructed VFR stages agree
  with saved cell heads. Crest cells actually contain approximately 1–4 mm of
  water. Do not remove these values or force a neighboring zero-volume cell wet
  just to make a profile appear level.
- Verification artifacts: `tests/verification/shoreline_range_artifacts/`.
  Analytical tests cover shoreline intersections, terrain kinks, no-data/dry
  gaps, lake-at-rest stages, reversed boundary traces and wet-side switches,
  saturated fill area/color, dry cutoff independence and unchanged class edges.
  Final build/test/launch status is recorded in that directory's verification.txt.

Shared workspace: preserve unrelated Phase 35 section-series work and existing
profile crown/bounds edits. Build in the isolated animation-verification folder.

## 14 Proposed follow-up: dry cells, residual films and partially wet cells (2026-10-01)

Status: implemented. Six targeted suites passed, including a repeated settings transaction check. Application build and strict/deep signature verification passed; the isolated testing app launched on 2026-10-01.

### Confirmed implementation findings

- `CellWaterGeometry::reconstruct` labels exactly zero depth Dry and every
  finite positive depth Wet. `cellHasSurface` inherits that distinction.
- Exact mesh-profile bands and the 1D surface overlay clip at zero local depth.
  A positive residual can therefore leave a water line even when its fill is
  thinner than a pixel. Its stationarity alone does not prove it is artificial.
- Map fills instead apply the layer's dry-depth cutoff to local reconstructed
  depth. The current rules differ between maps and profiles.
- Post-run loading can lower the layer cutoff using a peak-depth heuristic.
  Physical/model thresholds and presentation thresholds need distinct storage
  and provenance; palette selection must not decide whether a cell is wet.
- The earlier road-culvert diagnostic found actual saved crest depths of about
  1–4 mm. Its input DRY_DEPTH is 1 mm, so applying that threshold alone cannot
  correctly hide every reported crest film. Hiding greater depths requires an
  explicit display setting, or a separate solver investigation if those depths
  should not exist.

### Proposed classification and drawing policy

1. Audit source semantics before classification: compare the affected cells'
   depth, head, volume where available, terrain, source units and model dry
   tolerance over time. Establish whether a supplied depth means volume over
   whole-cell area or over wetted area; do not interchange them. Keep a solver
   inactive flag distinct from geometric water presence. Missing data remains
   an explicit unknown state, not an inferred dry cell.
2. Keep exact VFR geometry and raw values intact. Derive a separate per-frame
   display classification from the unsmoothed cell surface: Dry (no water),
   ThinFilm (positive water but every local depth is below the selected film
   threshold), PartiallyWet (the water plane intersects the bed within the
   cell and exceeds the threshold somewhere), and Wet (fully inundated and
   exceeding the threshold somewhere). Boundary equalities use a small,
   documented numerical tolerance; no blanket millimetre allowance is implied.
   Prefer authoritative source wet/dry information when its meaning matches
   the display contract. The conservative geometric fallback preserves a small
   deeper pool even when whole-cell average depth is tiny.
3. In the default clean display, Dry and ThinFilm draw terrain only: no water
   fill, water-surface stroke or contribution to neighbouring smooth surfaces.
   Apply this mask BEFORE surface blending, so a suppressed film cannot raise
   neighbouring shorelines or regain water from a neighbour. Preserve raw
   storage, reported head and diagnostic access. An optional Show thin films
   control can expose those values when needed.
4. PartiallyWet cells retain their exact signed surface and analytical zero-
   depth shoreline. They must not be dropped merely because a vertex, cell
   centre or whole-cell mean is dry. The film threshold decides whether a cell
   contributes visible water; it must not simply replace the zero-depth bank
   intersection and recreate the previously reported bank gap. Do not flatten
   genuine hydraulic discontinuities or fill zero-volume neighbouring cells.
5. Centralize the policy for 1D overlays, 2D profiles, CPU/QSG maps, contour jobs,
   displayed query status and maximum envelopes. Palette endpoints still
   saturate independently. Classification for historical envelopes must use
   their own time coverage, never the current frame's dry mask. Persist the
   display threshold, rebuild profiles/caches when it changes, and include the
   policy revision in asynchronous frame keys so stale water cannot reappear.
6. Start with a clearly identified model-based threshold; permit an explicit
   higher display threshold for residual films. Do not silently raise it or
   adapt it to a changing color maximum. For the demonstrated 1–4 mm crest
   water, 5 mm is an optional comparison setting, not a claim that the solver
   considered those cells dry. A deterministic per-frame rule is the first
   implementation. Add temporal hysteresis only if transition tests show a
   need, with reproducible random-seek behavior and reset rules.

### Acceptance checks

- A zero-volume cell and a flat sub-threshold film draw no water line or fill,
  including next to deep water and after asynchronous frame replacement.
- A partially wet sloping triangle/quad with low whole-cell average but a
  deeper local pool retains its correct zero-depth shoreline; lake-at-rest
  water reaches the terrain boundary without the old bank gap.
- A valid isolated pond remains visible; zero velocity or unchanged values
  alone never trigger suppression.
- Map/profile agreement, live/saved-source parity and SI/US conversions hold.
  Forward play, reverse play, direct frame selection and reopening show the
  same dry mask. Maximum envelopes remain independent of current dry state.
- Raising/lowering a contour color maximum changes colors only. No raw values,
  exported simulation quantities, cell volumes or mass balances are altered.
- Recheck the road-culvert example with the model threshold and an explicitly
  selected film threshold, retaining before/after images and raw-value tables.

The need to preserve partially wet fronts and still-water equilibrium is also
supported by Bollermann et al., A well-balanced reconstruction for wetting/drying
fronts, https://arxiv.org/abs/1412.3580 (a 1D numerical-method reference, not a
ready-made 2D display algorithm).

### Implementation and verification record

- Added a separate display classification (Invalid, Dry, ThinFilm, PartiallyWet,
  Wet). A relative float-rounding tolerance is used at the film threshold.
  The exact raw VFR surface and source values remain intact.
- Surface smoothing skips hidden films before connecting wet neighbours.
  Maps and profiles now clip retained surfaces at zero local depth. Palette
  preview/legend bounds use zero independently of the film setting.
- Results styling has Water visibility controls: Use model dry depth,
  Thin-film depth (metres), and Show thin films. Model inheritance and explicit
  overrides round-trip in the project sidecar's waterDisplayPolicy object.
  Cancel restores the original policy, and accepted changes support Undo/Redo;
  the integration suite passed again after adding those transaction checks.
- Policy changes rebuild current geometry and historical envelopes, refresh
  both profile dialogs, and advance the QSG contour epoch. Old-policy jobs
  cannot replace the new display. Velocity snapshots exclude hidden cells.
- Removed peak-depth-based threshold changes on opening/finishing a run.
  Missing/invalid values retain Invalid status and cannot reuse stale frames.
- Source audit: InertialKernels.hpp etaDepthScalar/etaDepthQuadScalar compute
  depth = max(V,0)/whole-cell area. SWMMEngine writes state.depth to the output
  snapshot. The SurfaceStateData.hpp comment saying V/A_wet is inconsistent
  with the actual closure; this GUI task did not change that engine header.
- Final targeted tests: cell-water geometry, contour jobs, marching triangles,
  mesh-profile shoreline drawing, 1D profile clipping, and 2D results integration.
  Forced asynchronous contours were enabled for the final integration run.
- At road-culvert frame 70, an explicitly selected 5 mm threshold hides 40 film
  cells and preserves 40 partially wet cells. Live and HDF5 classifications
  agree for all 2,000 cells; raw depth arrays remain identical.
- Reviewed profile and contour before/after images in
  tests/verification/dry_film_artifacts/. A separate testing .oswp uses 5 mm
  and absolute references to the original model/results. The original project
  sidecar, input model and results file were not edited by this follow-up.
- The final SWMMVis build completed successfully and passed strict/deep
  signature verification. The isolated animation-verification app launched
  as PID 47443, confirmed running at 06:22 EDT on 2026-10-01. Command-line
  project arguments do not open the project in this app; automatic opening
  of the comparison copy was not verified because multiple same-name app
  instances made the UI target ambiguous. The comparison file remains ready
  to open. Changes remain uncommitted for testing.

## 15. Flowing-slope stacks follow-up (2026-10-01)

The user confirmed the reported profile is from the Bellinge model. The screenshot
shows a descending bed with separate low-corner water wedges and vertical drops
at cell boundaries. The current Bellinge HDF5 output is actively being written;
a copied snapshot was not readable as a consistent HDF5 file. The exact saved
section was not identified, so verification uses a matching sloping-flow fixture
and the existing road-culvert output without modifying the user's running model.

The horizontal VFR reference can cause this pattern even with positive volume in
every cell: a downstream cell's reconstructed level falls below its incoming
edge. The old shared-edge gate then disconnects its water from the upstream cell.
Changing that gate alone would average wet and negative signed depths, potentially
removing a real shallow flow instead of representing it.

Implement the bounded inclined-surface extension described in section 3:

- Retain raw horizontal VFR surfaces and original source volumes. Identify
  through-flow cells from finite incoming and outgoing edge fluxes. Build
  stencils only through shared edges with reciprocal, opposite, nonzero fluxes
  and visible water on both cells. Two edge hops supply a usable surface stencil
  at triangulated boundaries; point contacts and zero-flux crests do not connect.
- Fit a distance-weighted centroid surface gradient and limit its prediction in
  every neighbor direction to that neighbor's stage difference. Resolve only the
  supported direction for collinear stencils. Flat lakes and local extrema retain
  flat gradients; unsupported cells retain their horizontal VFR reference.
- Hold the gradient fixed and invert the original triangle/quad volume relation
  on the effective bed `z - gradient dot (point - centroid)`. Update centroid
  stages and repeat up to 24 times, with a relative stage-change stopping test.
  Reduce the slope if it would exceed the original frame-wide VFR depth bound,
  preserving the precomputed contour range without clipping away volume.
  This removes the bias introduced by initially pooling moving water at low
  corners. Each intermediate cell plane integrates to the source volume. A
  constant-depth planar sheet and a constant-stage lake are fixed points.
- Classify the inclined surface before shared-corner projection, so a shallow
  moving sheet still honors the chosen film visibility threshold. Retain the
  existing signed, area-weighted shared-corner projection for visual continuity.
  Require positive overlap of the two wet edge intervals; positive depths at
  opposite, disjoint ends of an edge are insufficient.
- Use the same reconstruction for current maps, profile samples, queries and
  each historical frame before accumulating maximum corners. Missing, invalid,
  zero or inconsistent flux falls back to the previous VFR reconstruction; do
  not reuse an earlier frame's moving surface. No solver data is rewritten.

Limits: cell means and fluxes do not uniquely determine the physical surface.
This remains a display reconstruction. The inclined per-cell closure preserves
volume, but the subsequent continuous corner projection is not independently
volume-conserving. A bounded iteration count is not a convergence guarantee for
arbitrary transients. Results without compatible edge fluxes retain the previous
behavior, including possible cell steps. Real fronts and disconnected pools must
not be joined just to remove visible gaps.

Verification artifacts: `tests/verification/vfr_slope_steps/`. New checks cover
uniform shallow flow over steep triangle and mixed-mesh strips, forward/reverse
flow, elevation datum shifts, exact clipped volume for random inclined surfaces,
still-water banks, dry cells, missing/inconsistent flux, disjoint wet edge traces,
map/profile/max agreement, SI/US scaling, frame seeking/replacement, film controls
and live-source parity.

Final verification (2026-10-01): all seven selected suites passed, with no
skips: `test_cellwatergeometry` (18 checks), `test_2dresults_vizfixes` (28),
`test_meshprofile_shoreline_render` (23), `test_profileplot_axis_edges` (6),
`test_marchingtriangles` (29), `test_simrunner_failures` (38), and
`test_contourjob`. Integration ran with asynchronous contours enabled and the
road-culvert saved/live fixture available. The 45,000-triangle planar-sheet
diagnostic reconstructed and projected in 118 ms, with no dry corners and a
maximum depth error of 5.4e-15 m. These are controlled fixtures, not a reproduction
of the exact Bellinge section in the screenshot. The before/after profile images
were inspected and show the artificial wedges replaced by a continuous shallow
surface. Logs and images are in the artifact directory above.

Built the app at `build/animation-verification/vfr-flow-testing/SWMMVis.app` to
avoid replacing the running older testing bundle. The final relink reused this
new bundle's freshly deployed dependencies, removed development-only executable
search paths, and passed strict/deep signing verification plus the dependency
audit (152 binaries, zero failures). Restored the normal macdeployqt CMake setting
afterward. Launched the final app at 17:16 local time (PID 41015); changes remain
uncommitted.

## 16. Follow-up: Bellinge startup failure while another app holds its output

The reported 2026-10-01 17:03:47 `plugin prepare() failed` error was traced to
the HDF5 output writer. Two app instances were open; the older testing app
(PID 91780) retained a reader on the same Bellinge `.2d.h5` file. A read-only
exclusive-lock probe confirmed the conflict. The run log also showed overlapping
attempts while an earlier simulation was active. After the failed attempt the
HDF5 file was zero bytes: `H5Fcreate(TRUNC)` can truncate an existing output before
discovering the lock conflict. No model or engine changes were needed.

Added a SimulationRunner preflight after path validation and before any run-log,
report or output writes. It opens an existing 2D output without truncation and
probes exclusive access (flock on Unix; exclusive opening on Windows). On conflict
it names the file and explains that another simulation/results view, including
another app instance, must release it. Access errors are also reported directly.
Legacy 5.x runs are excluded because they do not use this output. The engine
still owns the run-time lock; this is an early check of existing conflicts, not
an atomic cross-process reservation against simultaneous future starts.

Three new regression cases reproduced the original generic prepare failure
before the fix: a reader, a writer, and an empty locked output. All now refuse
before changing any model/report/output/run-log bytes and successfully run after
the lock is released. Native verification was on macOS; Windows handling has not
been exercised on Windows. Evidence is in `startup-tests-before.log` and
`final-tests-with-startup-guard.log` in the artifact directory.

After launch, a read-only check at 17:17 showed only the new app instance and its
active Bellinge writer. The real model's run log reached `step loop` at 17:16:44
and its HDF5 file was growing again (about 9 MB at first check). This confirms
startup recovery, not completion of the full simulation or visual verification
of the user's exact profile. No original project/output was edited by these
diagnostics; only the user's active run regenerated the results.

## 17. Bellinge retest: local pooling still dominates (strategy revision)

The user reported fine speckled/striped patches and water that still looks stuck
and disconnected, then suggested that the local VFR interpretation neglects the
neighborhood. The actual partial Bellinge output is now readable after the user
stopped the run. Preserve this reproducer in
`tests/verification/vfr_slope_steps/bellinge-partial-2026-10-01.h5` (12 frames,
46,734 triangular cells). The diagnostic `bellinge-check.cpp` reads those cell
means, mesh elevations and edge fluxes and runs the production geometry helpers.
It uses the model's 0.0001 m dry/film threshold. Results are in
`bellinge-check-before.csv` and `bellinge-time-variation.txt`.

At the last saved frame, 45,939 cells have incoming and outgoing flow, and all
67,599 opposite-flow interior edge pairs satisfy the reciprocal-flux tolerance.
Missing or inconsistent flux is therefore not the explanation in this run.
Only 13,431 cells get a materially changed plane from the inclined reconstruction;
39,448 remain partially wet in the final display, with 28,569 unequal shared-edge
traces between visible cells. These counts do not establish that every break is
unphysical; they establish that the idealized planar-sheet regression did not
generalize to this irregular model. No positive cell mean remained constant to
1e-7 m across frames 3–11, so the apparent stationary patches are not explained
by unchanged stored depths alone. Frame-cache/rasterization behavior still needs
separate visual comparison; the fine pixel stripes have not been attributed to
the geometry conclusively.

The failure mechanism to address is the reconstruction order. The limited plane
fit starts from independent horizontal VFR pools; local extrema can suppress its
gradient, then the wet-overlap gate prevents the final projection from connecting
the resulting pockets. Increasing smoothing strength or iterations is not an
adequate correctness criterion.

Revised strategy for a candidate implementation:

1. Build hydraulic neighborhoods from shared edges, stored water and valid flow
   evidence, with existing dry regions, crest/barrier exclusions, disconnected
   vertex fans and mixed-cell storage diagonals respected. A horizontal VFR wet
   mask must not be the sole gate when a compatible saved flux crosses the edge.
2. Fit shared water-surface values over the connected neighborhood using original
   cell volumes and terrain together. The unknowns are the shared surface, not
   a collection of isolated flat pool levels to be smoothed afterward. Use exact
   clipped storage as a fit constraint/check and regularize unsupported spatial
   variation. Retain independent traces for genuine barriers or unsupported
   connections. Do not force all positive-volume cells fully wet or interpolate
   through zero-volume cells merely to eliminate visible patches.
3. Preserve constant lake stage and uniform moving sheets as exact reproduction
   cases. Classify film visibility after reconstruction and intersect the shared
   signed field with terrain for shorelines. VFR remains useful for storage and
   shoreline validation; it should not dictate an independent horizontal surface
   in every moving cell.
4. Document the unavoidable display tradeoff: a continuous piecewise-linear
   vertex field generally cannot exactly match every arbitrary cell volume on an
   overdetermined mesh. Measure volume residuals explicitly and bound them; retain
   original solver values for reporting. Do not claim an exact physical interface
   from cell averages alone.
5. Use the same frame-revision-keyed field for maps, profiles, point queries and
   maximum histories. Compare CPU and GPU images of this saved Bellinge fixture,
   including terrain/basemap isolation, to distinguish geometric pockets from
   a separate rasterization defect behind the fine stripes.

Acceptance requires the actual Bellinge reproducer as well as lake-at-rest,
partly wet banks, uniform flow on irregular slopes, dry barriers, mixed meshes,
dry/re-wet animation, reversed seeking and bounded-volume checks. The revised neighborhood fit was subsequently authorized and implemented as
described below. The actual project overlay and exact original saved section
remain user acceptance checks; isolated depth-only images do not establish the
cause of every stripe in the original screenshot.


### Implementation and verification, 2026-10-01

`include/layers/neighborhoodwaterfit.h`, consumed by the existing shared geometry
helper, replaces the independent inclined-plane inversion. Reciprocal nonzero
edge flux can connect positive-water cells even when their local horizontal VFR
wet intervals do not overlap. Existing positive wet overlap connects still-water
edges. Zero-volume/invalid cells, hidden raw films, non-manifold edges and
unsupported crests remain excluded; merely touching at a vertex does not create
an edge connection. No neighbourhood connection can be inferred from missing
flux: the previous shoreline-aware projection remains the fallback in that case.

Each connected vertex fan owns one signed-depth unknown. Exact clipped triangle
storage and its analytic wet-area derivatives drive a matrix-free Gauss–Newton
fit; quads use their original storage halves and area weights. A weak prior
blends the projected pond stage and moving depth references according to their
storage residuals on incident cells. This blend is continuous rather than an
entire-component binary mode switch. Identical lake stages and uniform moving
sheets remain exact reproduction cases on irregular terrain.

The fit includes a component-volume constraint in its linear solve and merit
line search. Final monotone depth scaling closes remaining nonlinear volume
error per component, capped at the original frame's VFR peak depth. An additive
stage closure was rejected because it overfilled shallow neighbours. The fitted
shared field goes directly to visibility classification, map contours/fills,
profiles, point queries and maximum history. It is not averaged again or filtered
across animation frames. Original solver arrays and numeric reporting remain
unchanged.

Acceptance evidence is in `tests/verification/neighborhood_surface/`. The copied
46,734-cell/12-frame Bellinge output is read-only. `bellinge-final.csv` measures
all frames with the production helper. At frame 11, unequal finite shared-edge
traces fall from 28,569 to zero; partially wet cells fall from 39,448 to 20,892.
The underlying reconstructed volume agrees with stored volume within 9e-12
relative across this fixture (visibility may omit remaining thin films).
The final frame's area-weighted cell-mean absolute RMS residual is 0.0446 m;
95% of cells have absolute error at most 0.0885 m; the worst is 0.797 m. The
area-weighted relative RMS is 0.719 and maximum relative error is 4.11, so this
must not be described as exact per-cell volume conservation or a uniquely
physical interface. Across later frames the largest absolute cell residual is
1.31 m. The global depth cap, exact lake/sheet cases and component volume are
hard guarantees; individual Bellinge cell-volume residuals are measured
approximation limits, not hard per-cell tolerances. Optimized reconstruction
costs about 0.19–0.25 seconds per 46,734-cell frame on this machine; a first
maximum-history request also reconstructs the previous frames.

Regression cases verify analytic derivatives against independent clipped polygon
integration, moving sheets on irregular triangle/quad meshes, exact independent
component volumes with dry gaps on both mesh types, preserved lakes, datum
invariance, film suppression and bounded depths. The Bellinge layer regression
checks live/saved equality, reversed seeking, unchanged raw arrays, all visible
shared-edge traces, and profile/query equality. CPU maps and native OpenGL Qt
Quick maps were captured at frames 3, 8 and 11 with smooth and flat bands;
full-mesh captures exercise multiple geometry upload chunks. The isolated
water-layer images show consistent footprints and no fine striped raster
pattern. Terrain and basemap are excluded from these captures, so an overlay-
specific cause in the user's complete project is still not conclusively ruled
out. No new GPU defect was reproduced or claimed fixed by this neighborhood
change.

All seven focused suites pass after rebuilding their targets: 21 geometry
checks, 29 results-layer checks (including the Bellinge fixture), 23 shoreline
rendering checks, 6 profile-axis checks, 29 marching-triangle checks, 38 startup
checks, and the contour worker suite. The separate native GPU regression passes.
Logs and images are retained with the diagnostic. The testing app was rebuilt
at `build/animation-verification/vfr-flow-testing/SWMMVis.app`, relinked against
its deployed runtime, signed, and audited (152 binaries, zero failures). Normal
macdeployqt configuration was restored. No original project or simulation output
was modified, and this work is uncommitted.

Launched the verified bundle on 2026-10-01 at 21:06 local time; read-only process
inspection confirmed PID 31552 running that exact testing executable. Launch
required normal macOS access outside the restricted sandbox.
