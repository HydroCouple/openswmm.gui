# Terrain adaptive meshing review plan

Status: APPROVED and implemented 2026-10-01. Owner: “Proceed with implementation; make sure GUI surfaces options for configuring and they default to appropriate values; reload previous options on reopen of the mesh dialog.” See `TERRAIN_ADAPTIVE_MESH_IMPLEMENTATION_2026-10-01.md` for the delivered behavior, Bellinge comparisons, 1/5/10-million-cell measurements, and remaining model-specific hydraulic validation.

The objective is to use substantially fewer triangles on planar ground while preserving terrain changes that control runoff and inundation, and to support 5–10 million output cells efficiently. The recommended direction is to retain the existing constrained Delaunay engine, start with a coarse mesh, and refine using error measured against the actual terrain. Surface normals help identify features; they do not provide the final acceptance criterion.

This proposal builds on `MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md`. It preserves triangles on open ground, quads only in approved feature regions and strips, constrained geometry, coupling identity, and robust geometric predicates. The proposed terrain hierarchy is a search structure, not a return to the retired quadtree mesh generator. Existing approved plans remain in force until this proposal is approved.

## 1 Current implementation

The old terrain node thinner and its normal dot product threshold have been retired. The current worker does not generate a dense terrain point cloud and then remove nodes.

| Stage | Current criterion | Relevant source |
| --- | --- | --- |
| Terrain sizing | Test blocks of 2, 4, 8, … pixels against the bilinear surface through their four corner samples. A block passes when sampled vertical deviation is within the terrain tolerance. All children must have passed before testing their parent. | `src/mesh/terrainsizefield.cpp`, `blockPasses` and `processBand` |
| Conservative expansion | Take the smallest resolved size in each output cell, then the minimum over its 3 by 3 neighborhood. | `TerrainSizeField::dilateMinimum` |
| Terrain breaklines | Threshold the dominant magnitude of raster second differences, suppress nonmaxima, connect responses, and trace lines. This is a curvature-like detector, not a normal dot product test. | `src/mesh/terrainbreaklines.cpp` |
| Combined sizing | Minimum of feature-distance sizing, terrain sizing, region sizing, and the maximum size; clamp to the minimum size and propagate gradual transitions. | `src/mesh/sizefield.cpp`, `buildSizeGrid` |
| Triangulation | Insert vertices when a triangle exceeds its permitted area at its centroid or fails the minimum-angle test. Split encroached constraint segments first. | `src/mesh/meshcdt.cpp`, `refineQuality` |
| Elevations | Fill final vertex elevations after generation. There is no production loop that measures final triangle-to-DEM error and repairs it. | `src/ui/dialogs/meshgenerationdialog.cpp`, `runMeshPipelineImpl` |

More precisely, the generator supplies `1.22 * h` to the refiner, whose area test is `A > sqrt(3)/4 * (1.22*h)^2`. The factor calibrates typical mean edge length; it is not a terrain-error bound. The default minimum angle is 30 degrees, with exceptions and a refinement floor around difficult constraints and fixed strip edges. The current refinement queue is FIFO.

The current default coarsening factor is 20; saved settings retain their earlier values. Terrain tolerance defaults to zero, which disables terrain sizing and automatic terrain breakline extraction. These are source defaults, not verified settings for the user's saved mesh.

## 2 Evidence from the selected Bellinge model

Primary reference: `/Users/calebbuahin/Downloads/bellinge_2d/BellingeSWMM_v021_nopervious.inp`.

Read-only inspection of its adjacent `.oswp` and `.2dm` files found:

- 125,449 vertices and 249,616 triangles; no quad section.
- Mesh coordinates are EPSG:25832 and lengths are metres.
- Mean edge length per triangle: P5 8.972 m, P25 13.992 m, median 16.466 m, P75 18.594 m, P95 21.513 m.
- Sum of triangle areas: 27,723,053.49 m². This is an area sum, not a topology validation.
- The project selects `output_SRTMGL1.tif` as terrain, with vertical units of metres.
- The project sidecar does not record the generation recipe. Its original tolerance, coarsening factor, minimum angle, constraints, and executable revision cannot be reconstructed from these files alone.

The relatively narrow distribution of cell sizes motivates the investigation but does not establish its cause. No regeneration, terrain-error measurement, hydraulic run, or timing benchmark was performed during this planning review.

Use this exact project and its associated geometry as the primary baseline. Write future experiments under `tests/output/terrain_adaptive_mesh_2026-10/`; keep the supplied project as a reference. The existing Bellinge acceptance harness uses a repository fixture and must be parameterized for this selected project before its results can be treated as comparisons.

## 3 Why flat areas can remain dense

The following are confirmed code behaviors. Their contribution to the selected Bellinge mesh must be measured separately.

1. **Coarsening is bypassed without size sources.** `SizeField::build` rejects a field with no feature seeds, terrain callback, or regions, even when `maxSize` is valid. The worker falls back to the uniform area derived from the near-feature size. A feature-free domain with terrain tolerance off therefore remains uniformly fine despite the coarsening factor. The outer domain ring deliberately does not seed the size field.
2. **Every eligible feature seeds fine cells.** Conduit and auxiliary lines, valid hole rings, tagged points, patch boundaries, and region rings can impose density independent of terrain. Narrow strips also require fine neighboring triangles. Dense feature networks may never allow the maximum size to be reached.
3. **Corner noise and first failure can lock in fine sizes.** A local failed block prevents every ancestor from being considered, even if a larger plane could represent those samples within tolerance. Min aggregation and neighborhood dilation expand the affected area, followed by another expansion for grading. Perfect planes pass; the concern is noisy or quantized nearly planar ground and isolated details.
4. **Processing limits also impose geometric limits.** The largest block is normally 1,024 pixels across, and the raster band memory budget can reduce it. Even a perfect plane cannot produce a terrain size larger than the available test scale. Memory allocation should not determine geometric fidelity.
5. **The error surrogate does not match a triangle.** A bilinear patch can represent a saddle exactly, although two planar triangles over that patch cannot. Conversely, a block crossing a ridge may demand fine sizes on both otherwise planar sides. The current surrogate can both overrefine and underresolve.
6. **Breakline coverage is assumed too broadly.** Inside a distance-based cone around previewed terrain lines, the size field suppresses the terrain term. This does not prove that all nearby terrain is represented, and later strip placement can trim those lines again.
7. **Quality refinement adds cells and never removes them.** FIFO processing can create a different and sometimes larger mesh than processing the worst quality failures first. No later coarsening step reclaims overrefinement.

The size field is also sampled on a bounded background grid and then at triangle centroids. This is not sufficient to certify a narrow terrain feature inside a large triangle.

## 4 Recommended acceptance criterion

For a triangle `T`, define `E(T)` as the maximum absolute difference between the source terrain elevation and the triangle's interpolated elevation over valid reference DEM samples covered by `T`. Require `E(T) <= epsilon_z`, alongside feature, size, topology, and quality requirements.

The initial contract is explicit: a bound over original valid DEM samples, supplemented by samples along protected terrain features. It is not a claim about unknown ground between measurements. If a bound over the continuous bilinearly interpolated raster is later required, add triangle–raster-cell intersection checks and the relevant interior/edge extrema; do not present a centroid or sparse sample check as that stronger guarantee.

For quads, use the same interpolation convention as the mesh/engine consumer rather than silently treating a quad as a bilinear surface. Certify the final geometry and elevations, including changes introduced by quality refinement and elevation overrides.

**Normals remain useful.** A large `1 - dot(n1,n2)` identifies a change in surface direction without computing an angle. Use this to prioritize ridge, bank, crest, and slope-break detection at physical spatial scales. A constant steep plane deserves coarse triangles just like a horizontal plane. A fixed angular threshold alone cannot bound elevation error: small angular changes over a long distance can produce a large height discrepancy, and pixel-scale noise can produce large angular changes over a tiny distance.

Use separate feature requirements for designated channels, levees, walls, crests, banks, and coupling geometry. A shallow narrow channel can be hydraulically important even when its depth is below the global elevation tolerance. Do not smooth or remove such a feature merely because the terrain error passes. For true elevation discontinuities, a single constrained edge with shared vertex elevations cannot represent two different heights at the same location; use resolved crest/toe geometry or an existing hydraulic barrier representation.

Minimum cell size, fixed strips, prescribed elevations, and the requested error tolerance can conflict. Return the unmet locations and reasons; never silently label such a mesh as meeting tolerance. Report terrain approximation error and intentional model elevation offsets separately, together with their combined effect on the exported surface.

## 5 Proposed algorithm

### Build a terrain hierarchy with bounded memory

Read the DEM in tiles with halos, skipping tiles outside the active domain where possible. In local coordinates, store an affine plane, a conservative residual bound, valid-data coverage, and feature candidates for each terrain block. Merge summaries upward without imposing a maximum geometric scale based on raster band height. A failed small block must not permanently prevent a valid larger approximation.

For a candidate triangle plane `P_T`, a block with plane `P_B` and residual bound `E_B` has a safe upper error bound of `E_B + max(abs(P_B - P_T))` over the block. Since the plane difference is affine, its absolute maximum over a rectangular block occurs at a corner. If this upper bound passes, skip that block's samples. Otherwise descend, and check original samples at leaves. A conservative bound that fails is an instruction to inspect more closely, not proof that more mesh cells are needed.

Those bounds must use the same horizontal and vertical coordinate system. With nonlinear reprojection, use conservative bounds over transformed sample coordinates or descend to individual samples; transforming only four raster corners does not establish the affine bound. For partial triangle/block overlap, bounding the whole block is safe but may be loose. Sample ownership on shared edges, NoData exclusion, and floating-point allowance must be explicit.

Start with a single-threaded reference implementation and exhaustive verification on small rasters. Profile hierarchy depth, bound tightness, tile cache behavior, and rescans before selecting the final summary layout. Preserve NoData coverage explicitly and retain the source DEM as the authority. Avoid silently filtering it to make the mesh smaller.

### Generate coarse geometry and refine actual errors

1. Prepare and preserve the domain, holes, coupling features, region boundaries, and approved strips. Distinguish “must appear as an edge” from “must impose the global fine spacing,” using existing feature roles and explicit local spacing.
2. Build an initial coarse constrained Delaunay mesh governed by the maximum permitted size, required feature resolution, and the existing quality rules. A valid maximum-size-only field must work.
3. Query each triangle against the terrain hierarchy. Queue triangles with verified elevation errors; use a location of maximum residual as a candidate insertion point.
4. Resolve constraint encroachment and quality failures through the existing robust kernel. If a terrain candidate cannot be inserted directly, refine its local neighborhood and remeasure. Do not discard its terrain obligation when a segment or quality insertion substitutes for it.
5. Invalidate error results for every changed triangle, including edge flips and the full affected insertion cavity. Recompute only those regions. Track topology generations so stale queue entries and certificates cannot be reused.
6. Stop only when all applicable constraints pass, or explicitly report the remaining conflicts and resource limit. Run an independent final validation pass before export.

This avoids building a dense mesh simply to delete most of it. A general edge-collapse/QEM simplifier is not proposed for the first implementation: it adds a second topology-changing system, and its cost function is not by itself a maximum vertical-error or drainage-preservation guarantee. Reconsider targeted local coarsening only if measurements show meaningful residual overrefinement after the new insertion strategy.

### Make quality refinement economical

Retain the 30-degree default for the initial comparison. Compare FIFO with deterministic bucket queues that prioritize the worst angle failures, plus separate terrain-error and size work. Keep encroached segments first. Use bounded queue membership and generation counters to avoid repeated stale work.

Benchmark 25, 28, and 30 degrees as a quality-versus-count study; any default change requires the resulting hydraulic and mesh-quality evidence. Do not obtain fewer triangles simply by relaxing quality without reporting it.

## 6 Performance design for 5 to 10 million cells

Let `P` be relevant DEM samples and `N` output cells. Reading previously unexamined terrain costs at least O(P); total generation cannot honestly be promised as O(N) independent of the DEM. Aim for one streaming preprocessing pass, compact summaries, and local output-sensitive refinement. Record sample visits and revisits so repeated full-raster scans cannot hide behind an acceptable small-case time. Worst-case refinement and hierarchy traversal are not claimed to be strictly linear.

- Keep hot triangle, adjacency, vertex, error, and queue state in compact indexed arrays. Retain double precision for coordinates and robust predicates; use 32-bit local indices where counts permit and 64-bit allocation arithmetic.
- Reserve from measured estimates, release stage buffers promptly, reuse local workspaces, and avoid whole-mesh copies during assembly, elevation sampling, and Hilbert reordering. The current reordering allocates replacement cell and vertex arrays and performs a comparison sort; measure its contribution and evaluate deterministic radix ordering if it matters.
- Bound terrain tile caches independently of `N`. Replace the whole-window breakline mask with tiled detection and deterministic cross-tile connectivity. The current extractor allocates about one byte per DEM pixel and skips extraction above 512 Mi pixels; this is a material large-domain limitation.
- Retain proven spatial ordering and walking point location, and instrument full-scan fallbacks. Avoid scanning every polygon for every size query; index region bounds when measurements justify it.
- Parallelize independent raster processing, error evaluation, elevation sampling, and validation first, with per-worker raster/transform state. Keep topology modification deterministic initially. Only add parallel insertion of conflict-free cavities if profiling shows it is necessary; fixed independently meshed tiles would introduce artificial seams and extra triangles.
- Check cancellation and progress throughout long raster, feature, and refinement operations. Serialize commits in a reproducible order so worker scheduling cannot change the mesh.

Proposed benchmark targets, to be confirmed on a named reference machine in Phase 0:

| Measure | Proposed target |
| --- | --- |
| Core meshing at 5 M and 10 M output cells | 30 s and 60 s stretch targets, measured separately from DEM I/O and export |
| Scaling from 5 M to 10 M | At most 2.5 times wall time and 2.2 times mesh-dependent peak memory on comparable cases |
| Meshing working memory | Target at most 200 bytes per output cell, plus a separately reported terrain cache budget; approximately 2 GB plus cache at 10 M |
| Full application memory | Record process peak RSS including output objects, old/new mesh coexistence, and render/upload preparation; the core budget is not a claim about total application memory |
| Responsiveness | Cancellation acknowledged within 1 s during compute work, with blocking I/O reported separately |

The prior plan reports 2.42 M cells in 4.2 s in a container microbenchmark. That is useful evidence for retaining the kernel, not validation of 10 M cells, the selected Bellinge input, or the full terrain pipeline. Use isolated benchmark processes because cumulative process peak RSS can misattribute memory to later cases.

## 7 Implementation phases and review gates

| Phase | Work | Required evidence |
| --- | --- | --- |
| 0 Diagnose | Parameterize the Bellinge harness; record generation inputs; add insertion reasons, size-source diagnostics, stage timing and RSS. | Reproduce the density and show which requirements cause it. Compare saved mesh separately from regenerated baseline. |
| 1 Remove avoidable density | Fix maximum-size-only generation; separate geometric constraints from refinement requests; test bucketed quality scheduling. | Planar controls reach the coarse cap without losing constraints or angle quality. Quantify each change independently. |
| 2 Establish terrain error | Add the terrain hierarchy and an independent triangle-to-DEM checker. Test the current mesh first. | Agreement with exhaustive reference measurements on small rasters, including worst-error location. |
| 3 Refine by error | Integrate coarse-first terrain refinement, feature protection, changed-cavity invalidation, and explicit infeasibility reports. | Fewer cells at matched terrain error and feature requirements; final errors remain valid after all topology/elevation changes. |
| 4 Scale | Optimize measured bottlenecks, tile breaklines, parallelize independent stages, and bound allocation peaks. | Repeated 1 M, 5 M, and 10 M runs with timing, memory, scaling, determinism, and cancellation results. |
| 5 Validate hydraulics and expose controls | Validate Bellinge, update settings and documentation, and present comparison maps. | Terrain, topology, coupling, lake-at-rest, and representative rainfall comparisons pass their agreed gates. |

Likely implementation areas: `terrainsizefield`, `terrainbreaklines`, `sizefield`, `meshcdt`, `meshgenerator`, `meshgenerationdialog`, `meshreorder`, and their existing tests. Introduce a terrain-error evaluator as a separate testable component. Keep the external mesh format and engine interfaces stable. Version cached generation artifacts and persisted recipes when semantics change; preserve existing saved values and offer an explicit transition rather than silently reinterpreting settings.

## 8 Validation matrix

Use flat and tilted planes, bounded noisy planes, gentle domes, saddles, a planar area beside a ridge, narrow ditches, paired banks, shallow depressions and spill points, levee crests, steps, NoData boundaries, tile seams, rotated/non-square pixels, and translated large coordinates. Include dense constraints, acute intersections, holes, fixed strips, and prescribed rim elevations. Compare the same terrain at different raster resolutions to detect pixel-dependent refinement.

Proposed acceptance checks:

- Exact flat and tilted planes produce no terrain-driven insertions beyond the matched geometry/quality baseline. Their counts should be essentially equal under the same projected domain and constraints.
- The maximum final error at every valid tested reference sample meets the selected tolerance, apart from explicitly reported incompatible constraints. Report RMS, P95, P99, maximum, coverage, and the locations of exceptions.
- Required edges, protected points, coupling IDs, crest/toe locations, and channel connectivity survive. Check channel cross sections and depression spill elevations; terrain RMS alone is insufficient.
- Validate domain coverage, no gaps/overlaps, no inverted cells, valid edge incidence, minimum angles with enumerated exemptions, and measured neighbor size transitions.
- Demonstrate a material count reduction on selected planar Bellinge regions at matched requirements. A provisional goal is at least 50 percent fewer triangles there; do not apply this blindly to the whole city or trade away terrain fidelity to pass it.
- Run repeated 1, 4, and 8 worker configurations with deterministic output checks. Include independent large-domain cases with large `P` and large `N`; a small DEM over a huge synthetic mesh does not validate terrain scalability.
- Compare lake-at-rest stability, mass balance, wet/dry connectivity, water levels, inundation extent, and channel conveyance against an adequately resolved reference. Define hydraulic tolerances before accepting the implementation; a coarser terrain mesh alone does not establish simulation accuracy.

The selected terrain is named SRTM. Existing repository validation notes describe the Bellinge SRTM fixture as metre-quantized and use a 3 m tolerance. Verify this selected raster's metadata and values before assuming it is identical. Initially compare 1, 2, 3, and 5 m tolerances, plus a tolerance-off diagnostic. These are experiment settings, not recommended accuracy levels. Use finer synthetic/reference terrain for sub-metre feature tests; features absent from the source DEM require additional data or authored constraints.

Present the Bellinge result as a table and matched maps of triangle count, local cell size, actual terrain error, preserved drainage features, generation time, and peak memory. Let the owner select an operating tolerance from that evidence.

## 9 Decisions proposed for approval

Approve the phased direction: retain constrained Delaunay meshing; fix unintended density first; use actual elevation error as the acceptance measure; use normals to guide feature detection; preserve designated hydraulic features; and validate at 5 M and 10 M cells before declaring scalability complete.

No terrain tolerance or angle-default change is proposed for immediate adoption. The selected Bellinge model is the benchmark. The first implementation milestone should return a measured diagnosis and coarse-cap corrections before introducing the new terrain refinement path.

## 10 Supporting references

The recommendations above are engineering proposals, not performance guarantees from these sources.

- Garland and Heckbert, [Fast Polygonal Approximation of Terrains and Height Fields](https://www.cs.cmu.edu/afs/cs/Web/People/garland/scape/scape.pdf), 1995: error-driven greedy insertion and local updating provide a foundation for coarse-first terrain approximation. Their rendering-oriented evaluation does not establish hydraulic fidelity.
- Shewchuk, [Selected Implementation Issues](https://www.cs.cmu.edu/~quake/tripaper/triangle4.html), 1996: refinement order influences cell count; angle-bucket queues can approach heap ordering quality without its full ordering cost. The reported reduction in an example is not a forecast for this project. This supports an algorithm experiment, not reintroducing the Triangle library.
- Spielman, Teng, and Üngör, [Parallel Delaunay Refinement Algorithms and Analyses](https://arxiv.org/abs/cs/0207063), 2002: independent insertion sets provide a principled route to parallel refinement if the measured bottleneck warrants it.
