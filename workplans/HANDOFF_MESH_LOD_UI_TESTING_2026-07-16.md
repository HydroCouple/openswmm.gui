# Mesh Tiled LOD — Stage 0 + Stage 1 shipped; STOPPED for interactive UI testing

Date: 2026-07-16 · Branch: `swmm6_gui` · Plan: `MESH_TILED_LOD_RENDERING_PLAN_2026-07-13.md`

## Why this stop point

Stage 1 is where the live mesh rendering path actually switches from the
QPainter `SWMM2DMeshGraphicsItem` to the GPU `SWMM2DMeshQSGRenderer`, and
where mesh load leaves the GUI thread. Visual parity, identify/pick, and BC
editing against the *real* app need human eyes before Stage 2 (chunked
indexed geometry) and Stage 3 (LOD pyramid) build on top. Offscreen gui
tests cover everything they can headless — but the offscreen QPA cannot
read back scene-graph pixels (same limitation as the pre-existing
offscreen-GL test failures), so QSG-vs-QPainter parity must be confirmed
interactively.

## What shipped

### Stage 0 (commit `ef7923f`)
- `mesh_perf_generator` (tests/tools/) → synthetic terrain fixtures at
  0.5M / 1M / 5M tris in `tests/perf-data/mesh/` (gitignored; README there).
- `openswmm.load.mesh` logging category.
- `test_meshperf_baseline` harness; plan §6 table filled. Verdict: 5M mesh
  sync load = **36.4 s GUI freeze**, QPainter mid/near zoom = **3.7–3.8 s
  per frame**, scene caches ≈ **1.0 GB**.

### Stage 1 (this commit)
- **P1.1 QSG mesh activation**: `SWMM2DMeshQSGRenderer` instantiated in
  `swmmlayer.qml` (bottom of the stack: mesh → 2D results → 1D network).
  MapCanvas hands it the topmost visible `SWMM2DMeshLayer` per paint,
  mirroring the 2D-results single-owner pattern (mask ⇒ CPU path; CPU 2D
  results on screen ⇒ mesh stays CPU so stacking order can't invert; 1D
  QSG kinds force-enabled while the mesh owns the QSG frame).
- `SWMM2DMeshLayer::qsgOwnsRendering()` gate — the QPainter item
  early-returns while the QSG renderer owns the layer; full QPainter path
  retained as fallback.
- **Preference** `Rendering/QsgMeshEnabled` (default ON) + checkbox in
  Preferences → GPU Rendering; **env kill-switch** `OPENSWMM_QSG_MESH=0`.
- **P1.2 async mesh load**: `SWMMVis::attachMesh2DLayersAsync` — the
  `.inp`/`.2dm` parse + `SWMM2DMeshLayer` scene-geometry build run on a
  QtConcurrent worker (the layer is built there and moved to the GUI
  thread); adoption (SRS, canvas add) + the prior-run HDF5 results attach
  happen in the completion handler. Hidden-until-adopted: a window closed
  mid-load deletes the layer instead of adding it. Busy bar + status
  message during the load; one "Opened <mesh> (…)" summary line on
  success; timings under `QT_LOGGING_RULES="openswmm.load.*=true"`.
- Tests: `test_meshasyncload` (preference default+signal, CPU-paint gate
  + pick invariance, worker-build ≡ sync-build equivalence, parity
  screenshots) + committed 192-tri fixture
  `tests/gui/data/mesh_async_fixture.inp`. QPainter screenshots land in
  `tests/output/mesh_qsg_parity/`.

## How to test the UI (the part only you can do)

Build/run as usual, then:

1. **Generate fixtures** (if absent):
   `build/darwin-debug/tests/tools/mesh_perf_generator tests/perf-data/mesh`
2. **No-freeze open**: File → Open →
   `tests/perf-data/mesh/mesh_5000000tri.inp`. The window should appear
   immediately; the busy bar runs while "Loading 2D mesh …" shows; the mesh
   pops in ~30–40 s later (Debug build) without the UI ever freezing.
   With `QT_LOGGING_RULES="openswmm.load.*=true"` expect a
   `mesh load timing (ms): read=… sceneBuild=…` line.
3. **Visual parity at three zooms** (fill, wireframe, contours, selection):
   compare QSG on vs off — toggle Preferences → Map Display → GPU
   Rendering → "Use GPU rendering for 2D terrain mesh layers", or restart
   with `OPENSWMM_QSG_MESH=0`. Same visuals expected; report any
   difference (colors, edge widths, contour placement, selection cyan).
4. **Identify/pick + BC editing**: vertex/edge/cell pick, hover, the Mesh
   Editing toolbar, per-edge BC edits — must behave identically with QSG
   on and off (they always resolve against the native arrays).
5. **Interactions**: pan/zoom on the 1M and 5M meshes with QSG on — full
   extent will still be slow-ish (~0.3 s/rebuild at 5M; the Stage 3
   pyramid is what fixes far zoom), but pans inside the view should be
   matrix-only and smooth. Layer visibility checkbox, sublayer toggles
   (fill/edges/nodes/bands/isolines), opacity sliders, style edits, and
   layer remove must all still repaint correctly.
6. **Mixed stacks**: with a 2D results layer + mesh + network visible,
   confirm stacking stays mesh (bottom) → flood map → network, and that
   masking a layer falls back to CPU without visual inversion.

## Known/expected

- `warm=0 ms` QSG numbers in the Phase 0 table are a grabWindow caching
  artifact; real per-frame costs get measured through the MapCanvas
  pipeline (`OPENSWMM_RENDER_PERF=1`) during Stage 2.
- The mesh now composites inside the QSG frame (above every
  QGraphicsScene-buffer layer, e.g. rasters) when QSG-owned — same
  accepted tradeoff the 2D results layer already made.
- 5M-tri full-extent QSG rebuild ≈ 0.3 s (target <33 ms) — that is
  Stage 2/3 work, not a Stage 1 regression.

## Next (do NOT start before UI sign-off)

Stage 2: `MeshStaticGeometryBuffers` indexed fill + per-chunk
`QSGGeometryNode`s with LRU buffer pool (256 MB pref) + remap-table id
round-trip test. Stage 3: `DTMThinner` dot-product pyramid + editing
native-topology guarantee + Terrain-toolbar "True mesh" toggle.
