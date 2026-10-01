# Terrain-adaptive meshing: implementation and validation

Implements the approved `TERRAIN_ADAPTIVE_MESH_REVIEW_PLAN_2026-10-01.md` and the request to expose appropriate defaults and remember the mesh dialog's options. The supplied Bellinge files were read as references; experimental meshes and hydraulic runs are under `tests/output/terrain_adaptive_mesh_2026-10/`.

## Delivered behavior

The default path starts coarse and refines actual triangle-to-DEM elevation error. It no longer derives adaptive spacing from the legacy hierarchy's first failed bilinear block. Flat ground and constant slopes can reach the maximum requested size. Maximum-size-only fields now work even without feature seeds.

`TerrainErrorField` reads 256-pixel tiles into a bounded cache. Sixteen-pixel leaf blocks store affine planes, conservative residual bounds, valid-sample counts and missing-data counts; parent bounds are composed conservatively. A passing bound skips the enclosed source data. A failing bound descends to original samples before deciding to insert a point. Nonlinear reprojection uses bounds over transformed samples, rather than assuming transformed raster corners bound the footprint. The selected SRTM raster contains 14,989,100 valid reference samples and needs approximately 6.3 MB of summaries.

The existing constrained Delaunay topology and robust predicates remain. The refiner inserts at a triangle's worst measured residual and rechecks changed triangles after splits and flips. A deterministic 32-bucket quality queue has at most one pending entry per triangle. Vertex elevations are cached and carried into mesh assembly, including structured-patch interiors. Included geometric constraints and coupling identities remain when optional proximity-based fine spacing is disabled.

Final verification checks the actual generated elevations and geometry. A quad is checked using the consumer's 0–2 diagonal. Exceptions report counts and example coordinates. The report distinguishes conflicts that disappear when prescribed model elevations are removed from the check. Missing DEM samples are reported separately; they do not become certified terrain after interpolation fills their elevations.

The breakline detector keeps its existing curvature, suppression, hysteresis and tracing behavior. Its whole-window byte mask now uses a bounded cache with sparse-file spill, preserving global connectivity across cache blocks. Adaptive mode streams this pass without allocating the legacy terrain-size grid. The mask budget excludes output feature chains and the connectivity work stack. No artificial mesh tile boundaries are introduced.

## GUI and persistence

| Control | New-project default |
| --- | --- |
| Terrain refinement | Adaptive elevation error |
| Terrain tolerance | Automatic: max(0.1 m, three whole-unit elevation increments), converted to output vertical units |
| Capture terrain breaklines | On |
| Extra refinement around model features | Off; included geometry and coupling remain |
| Minimum angle | 30° |
| Prioritize worst triangle angles | On |
| Coarsening multiplier | 20 |
| Cell budget | 20,000,000 |
| Terrain cache | 64 MiB per cache |

Legacy block sizing and terrain-off modes remain available. Explicit saved choices override defaults. Existing preferences for the other scalar defaults still seed a new recipe.

Closing the dialog remembers controls, stable layer selections, auxiliary-source inclusion/Z choices, corridor drafts, burn settings, region hydraulics and infiltration defaults. Reopening restores them. Project save/reload round-trips the versioned `meshGenerationOptions` recipe through `.oswp`. Displayed distances use canonical storage so changing SI/US display units does not reinterpret their saved values. Source units, manual Z factors and terrain-mode choices are restored in dependency order. The last run's cell count, resolved tolerance and unresolved/unknown counts are shown on reopen.

User documentation is updated in `docs/manual/19_2d_mesh.md`. A rendered dialog check is saved at `tests/output/terrain_adaptive_mesh_2026-10/dialog/mesh-quality-options.png`.

## Bellinge comparison

Reference: `/Users/calebbuahin/Downloads/bellinge_2d/BellingeSWMM_v021_nopervious.inp`, its selected `output_SRTMGL1.tif`, and the boundary extracted from its saved mesh. The saved mesh has 249,616 triangles, but its generation recipe is unavailable; it is **not** the matched comparison baseline.

The comparison below uses the same domain, included model nodes, 10 m reference spacing, ×20 coarsening, 2.5 m minimum request and 30° minimum angle. Conduits, quad strips and prescribed rim elevations are disabled in this comparison to isolate terrain and proximity-spacing effects. All final cells were checked exhaustively against original DEM samples.

| Tolerance | Legacy cells / max error | Adaptive with proximity spacing | Adaptive with geometry constraints only |
| --- | --- | --- | --- |
| 1 m | 115,569 / 1.938 m | 47,733 / 1.000 m | 29,551 / 1.000 m |
| 2 m | 47,045 / 2.545 m | 33,510 / 1.995 m | 13,264 / 1.998 m |
| 3 m | 35,836 / 4.020 m | 30,361 / 2.999 m | 9,009 / 2.992 m |
| 5 m | 30,557 / 4.638 m | 29,411 / 4.934 m | 7,908 / 4.989 m |

Every adaptive comparison meets its requested sample tolerance. Legacy mode has 79, 16, 8 and 0 exceeding cells respectively. At 3 m, geometry-only proximity settings reduce count by 74.9% relative to the regenerated legacy case. The same included node constraints remain.

Legacy runs took 0.33–0.52 seconds; adaptive runs took 2.59–2.77 seconds. About 2.5 seconds of adaptive time is the full-raster reference indexing/reprojection pass. These are different accuracy contracts, so the small-case result is a reduction in cells and improved error control, not a speedup over the legacy surrogate. Persistent reference-index caching is a possible subsequent optimization.

With model conduits and prescribed rim elevations also included, adaptive generation produced **55,343 cells**, preserved **all 1,020 in-domain node coupling identities**, and passed positive-area/edge-incidence checks. **208 cells** exceed the 3 m DEM tolerance; all 208 pass when the prescribed elevation overrides are removed from the check. Their combined surface error reaches 5.779 m. The GUI reports these conflicts rather than silently adjusting the authored elevations or claiming the final surface meets tolerance.

Review artifacts:

- `tests/output/terrain_adaptive_mesh_2026-10/bellinge/adaptive_comparison.csv`
- `tests/output/terrain_adaptive_mesh_2026-10/bellinge/terrain-comparison.png` and `.svg`
- The corresponding `.2dm` files, including `adaptive_model_constraints.2dm`.
- `tests/output/terrain_adaptive_bellinge_constraints_verified.log`

## Large-mesh measurements

Machine: Apple M1 Max, 10 CPU cores, 32 GiB RAM; macOS 26.7; Release build, Qt 6.9.3. Each benchmark runs in its own process. The topology algorithm is serial. Timings are observations, not deadlines; other builds were active during parts of the measurement period.

The full worker benchmark reads a tilted planar DEM, builds the reference hierarchy, streams breakline extraction, generates cells, assigns heights, reorders, and validates terrain accuracy. It uses a 16 MiB terrain-cache setting. It excludes fixture creation, final file export, layer construction/rendering, and coexistence with an already displayed mesh.

| Output cells | DEM samples | Full worker time, two runs | Peak process RSS, two runs |
| --- | --- | --- | --- |
| 1,084,156 | 4,016,016 | 5.40–5.69 s | 0.562–0.568 GB |
| 5,417,934 | 20,043,529 | 20.60–26.69 s | 1.85–1.91 GB |
| 10,835,772 | 40,056,241 | 39.33–40.82 s | 3.00–3.03 GB |

No terrain-driven insertions, unmet terrain tolerance or missing-data cells occur on these planar controls. The requested tolerance is 0.01 m. Repeated output cell counts match. The 5-to-10-million-cell time and memory scaling meets the proposed targets on this workload.

The isolated triangulation kernel produced 1,076,681 cells in 0.720 s, 5,150,062 in 3.602 s, and 10,302,126 in 6.889 s. Peak kernel-process RSS was 79 MB, 425 MB and 772 MB respectively. This is approximately 75 bytes per cell at the largest size; the full application's richer mesh objects require more memory.

Logs: `tests/output/terrain_core_scale_*.log`, `terrain_pipeline_scale_*.log`, and `terrain_pipeline_repeat_*.log`. Opt-in test controls are `SWMMVIS_TERRAIN_SCALE_CELLS` and `SWMMVIS_TERRAIN_PIPELINE_OUTPUT`.

## Validation and limits

All nine focused suites pass: CDT, generator, both size-field suites, terrain reference, breaklines, feature capture, dialog persistence and worker pipeline. The final run is recorded in `tests/output/terrain_adaptive_acceptance_tests.log`. New checks cover tilted/noisy planes, saddles, an interior mound, missing samples, rotated pixels, reprojection, cache-block connectivity parity, cancellation, resource limits and adaptive heights in quad interiors. A partial-coverage test verifies that finite elevation filling and prescribed feature elevations outside the DEM still leave cells marked as unverified. The project round-trip test also checks burn and infiltration choices. The mask parity test compares a 1 MiB spill cache with the in-memory result on a 3.16-million-pixel raster.

A legacy noise-fragment test was independently reproduced against unmodified HEAD: libc++ generates a 16-pixel fragment for its seeded normal distribution, while the test required fewer than 16. Its bound now includes 16; the production noise detector is unchanged. The old maximum-size-only test expectation was updated to the corrected behavior.

A 10-second, closed-basin lake-at-rest test at an 80 m water surface was run with the installed engine on the legacy and adaptive Bellinge comparison meshes. Both report zero exported water-level drift, zero maximum speed and zero storage change. Inputs, reports, HDF5 data and `lake-at-rest-results.json` are under `tests/output/terrain_adaptive_mesh_2026-10/hydraulics/`.

These checks do **not** establish equivalence for Bellinge rainfall/inundation predictions. Long rainfall, wet/dry connectivity, channel conveyance and accepted flood-level/extent tolerances still need a model-specific comparison. The accuracy contract is over original valid DEM samples, not unknown continuous terrain between them. The large stress case is planar; a highly detailed 10-million-cell city has not been timed. Full application rendering/export memory is not covered by worker RSS. The existing Hilbert reorder remains a whole-array operation and does not yet poll cancellation internally.

The application Release target links and packages successfully; the completed build is recorded in `tests/output/terrain_adaptive_integrated_build.log`. The macOS deployment step reports unavailable optional SQL-driver libraries before the existing packaging cleanup removes those unused drivers and retains SQLite. Terrain/GUI tests use the configured Qt/GDAL dependencies and pass. This meshing change does not modify SQL packaging.
