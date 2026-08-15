# SWMMVis (OpenSWMM GUI) 6.0.0-alpha.3

**Pre-release.** Project format, preferences and UI layout may still change before 6.0.0.

SWMMVis is a ground-up Qt6/C++ replacement for the legacy Delphi `epaswmm5.exe` GUI (last shipped as `v5.2.4`), built directly on [`openswmm.engine`](https://github.com/HydroCouple/openswmm.engine).

This release covers 118 commits since the informal "Alpha 1" milestone of 2026-05-22 — the `6.0.0-alpha.2` and `6.0.0-alpha.3` cycles. No `v6.0.0-alpha.2` tag was ever cut, so alpha.2 work is released here for the first time. See [`CHANGELOG.md`](https://github.com/HydroCouple/openswmm.gui/blob/v6.0.0-alpha.3/CHANGELOG.md) for the itemized record.

**Requires** `openswmm.engine` 6.0.0-alpha.3. The cross-section shape fix and the 2D rendering change below both depend on engine changes in that release.

---

## Highlights

### Million-cell 2D meshes are now usable

The 2D mesh and results layers moved onto Qt's scene-graph renderer with a per-sync dirty-domain classifier (pans are matrix-only; time ticks skip static geometry), a hysteresis-damped Far/Mid/Near LOD policy with viewport culling, chunk-batched render culling, shared static geometry buffers rewritten in place per tick, async contouring on a worker with a generation-guarded double buffer, and an opt-in GPU fill path with a ramp LUT texture.

On top of that the mesh load became **progressive and off-thread**: the constructor produces only what is needed to draw coarse levels, so the layer joins the canvas immediately, and wireframe edges, spatial grids, vertex adjacency and BC slots finish on a worker.

On a 9.4M-triangle regional mesh, terrain appears at **~43 s instead of ~88 s**, with the rest filling in behind. A long open now narrates every stage to the Message Log instead of going silent.

Large rasters got the same treatment: display is **O(viewport) rather than O(raster size)**, via background overview-pyramid (`.ovr`) generation, a windowed reprojecting `warpToCanvas`, a fixed-grid 256 px tile cache with coarse-tile fallback, and parallel tile production.

### File I/O no longer freezes the window

Every open path — single `.inp`, `.out` results, raster and vector layers — moved off the GUI thread, with consistent status messages and opt-in load telemetry. On West Whiteland (103k nodes, Debug) the post-parse GUI freeze went from **~1877 ms to ~250 ms**.

### Mesh generation survives real regional datasets

Generation appeared to hang at ~37% on large domains. Two independent causes, both real:

- The segment spatial hash binned each constrained segment into every cell of its inflated bounding box — O((len/buffer)²) insertions for a diagonal segment. Segments are now chunked before binning, and point-in-ring tests use a y-banded crossing test instead of per-candidate polygon scans. Both validated against brute force over 50k random queries, zero mismatches.
- The grid step came from the DTM's pixel size — a DTM-CRS quantity — while every consumer measured in **mesh** CRS units. With a geographic DTM against a projected mesh, the boundary buffer came out as 0.14 mm instead of ~12 m, so 205 km of boundary exploded into ~1.5×10⁹ chunks. The same slip had silently disabled the Poisson-disk filter entirely.

On the domain that hung: chunks **1.5×10⁹ → 21,336**, the segment hash from never-returning to **66 ms**, the full pipeline to **1024 ms** — and the boundary filter finally doing its job.

Also fixed: natural-neighbour vertex elevation passed an `N` switch to Triangle that leaves the point list *uninitialised*, so **every valid seed set segfaulted** while every degenerate one returned cleanly — meaning that interpolator had never produced a value in this pipeline. And DTM thinning only ever ran a single pass regardless of the configured iteration count, its block sampler fabricated elevations for 8500 of 12,100 points outside the DEM footprint, and it read the whole clipped domain in one `RasterIO` (fatal on a multi-GB DEM; now banded).

Refinement is now **cancellable** — the Stop button was dead for the entire refinement stage — with progress reporting and a graded size function, all through Triangle's documented `triunsuitable()` extension point so `vendor/triangle/` stays untouched.

### The >65k-vertex OpenGL truncation

Qt's batch renderer addresses batched vertices with 16-bit indices, so any single geometry node above the cap wraps and draws garbage. On macOS — where OpenGL is the forced RHI — a row-ordered mesh drew only its first ~21k triangles, reading as "the mesh shows only a sliver". Metal tolerates giant nodes, which is why the offscreen test harness never reproduced it. All mesh and 2D-results uploads now split into ≤65,532-vertex chunks — the largest value under 2¹⁶−1 divisible by both 3 (bare triangles) and 6 (thick-segment quads), so a chunk boundary never splits a primitive.

Relatedly: **QML types were registered too late**, so anything constructed during application startup could create a canvas before the QML module existed — silently falling every GPU renderer back to the CPU painters for the whole session, at 10-second paints on a 9.4M mesh.

### Editing and GIS round-tripping

- **Delete** from the Object Browser and the Attribute Table, undoable, cascading links on node deletion exactly as on the map.
- **Newly added objects appear immediately** — adds updated the arrays but never realigned the cached bounding rect or the scene BSP index, so a new object outside the stale bounds was culled until a pan or zoom.
- **Loaded GIS raster and vector layers persist in the project** and reopen through the same async path.
- **Attribute Table support for external GIS feature layers**, and a **Feature layer → SWMM objects import dialog** with column mapping, three combinable link-endpoint strategies, a worker-thread dry-run preview, and the whole import as a single undo macro.
- **Polygons with holes** (PR #5) — including a guaranteed-interior hole seed, since the vertex centroid can fall outside a non-convex ring and fail to carve the hole at all. Draw/edit of holes is not yet wired up.
- **A re-runnable "Remap 1D↔2D" operation**, decoupled from mesh generation.
- **Mesh persistence across save/reopen** — four linked defects meant a freshly generated mesh could vanish and the old one reappear. In the worst case, saving an inline-mesh project stripped its `[2D_*]` sections and pointed a `[2D_MESH_FILE]` reference at the `.inp` itself: `demo_road_culvert` lost 3135 lines and ran in under a second with all-zero flows.

### Simulation options and editors

A `MINIMUM_STEP` field and a one-click **fast-run preset** (`THREADS=8`, `MINIMUM_STEP=1.0 s` — ~2.6× faster on the Bellinge coupled model with mass balance as good or better), VFR cell-closure controls, editable gage and per-subcatchment precipitation scale factors, degree-day snow columns in the RDII decay editor, a full classification editor for mesh terrain fill and elevation bands, and Simulation Options rebuilt on a list sidebar so pages scroll instead of opening taller than the screen.

---

## Breaking changes

- **Requires `openswmm.engine` 6.0.0-alpha.3.** The cross-section shape table is now keyed off the engine's `SWMM_XSECT_*` constants; against an older engine it will write wrong shapes.
- **Cross-section shape picking was wrong for every code from 8 up** — picking EGGSHAPED wrote a baskethandle, IRREGULAR wrote a vertical ellipse. Projects saved with an affected shape by an earlier build have the wrong shape stored and need checking.
- **2D depth now defaults to the smooth per-vertex fill** rather than contour bands. Bands remain available.
- **DTM thinning now performs its configured number of passes**, so the default of 3 thins harder than before; threshold and iteration defaults may want re-tuning.
- **Mesh-generation defaults retuned** — minimum angle 33° → 26°, junctions-as-Steiner-points now **off**, boundary-filter normal-dot threshold 1.0 → 0.6. Regenerating an existing mesh will produce a different (generally smaller) result.
- **Mesh LOD thresholds lowered** (edge 3.0, vertex 6.0 px/cell) and the style-dialog range tightened to 0–40 px/cell.
- **Windows builds were previously compiled without `/GR` or `/EHsc`** — an ABI mismatch against Qt6 and GDAL, which both ship built with them. Fixed, but it means prior Windows builds were unsound and any Windows-specific behaviour is worth re-testing.
- **The copyright holder on the GPL v3 notice changed** from "Caleb Buahin" to "HydroCouple". The MIT → GPL v3 relicensing itself happened earlier, on 2026-05-08, before this release span.

---

## Known gaps

- GIS-layer symbology is not persisted, and z-order among GIS layers may differ after an async reopen.
- Inline (non-external) 2D meshes still need an engine-side mesh-replace API to survive regeneration.
- 12 Windows GUI tests plus `test_ioportabilitynormalizer` are quarantined behind an exclude regex — hidden, not fixed, each tracked to re-enable.
- The 2D results renderer still uploads straight-alpha colours into a premultiplied material — the same defect fixed in the mesh renderer, not yet swept there.
- Polygon-with-holes draw/edit and round-trip remain plan-only pending `EditableVectorLayer`.

---

## Acknowledgements

Engine contributors and issue reporters whose work this release depends on are credited in [`openswmm.engine/AUTHORS.md`](https://github.com/HydroCouple/openswmm.engine/blob/v6.0.0-alpha.3/AUTHORS.md). See [`AUTHORS.md`](https://github.com/HydroCouple/openswmm.gui/blob/v6.0.0-alpha.3/AUTHORS.md) for this repository.
