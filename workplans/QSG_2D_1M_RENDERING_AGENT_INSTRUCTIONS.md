# QSG 2D Rendering: 1M Cell Execution Instructions

Created: 2026-07-05

## Mission

Make the 2D mesh/results QSG path responsive for models up to roughly 1 million
cells. Treat this as a rendering architecture task, not a micro-optimization
task.

The target behavior is:

- Pan and zoom remain interactive after the first render.
- A pan never rebuilds or reuploads mesh/result content.
- A zoom rebuilds content only when it crosses a level-of-detail threshold.
- Selection/highlight changes update only small overlay buffers.
- Time-step changes update scalar/result data without rebuilding static mesh
  topology.
- Full-extent rendering avoids drawing visually useless million-scale wireframe,
  vertex-marker, label, and dense-vector detail.

Ignore older rendering workplans while executing this brief. Use the live code
as the source of truth.

## Primary Files

Start by reading these files:

- `src/map/swmm2dmeshqsgrenderer.cpp`
- `include/map/swmm2dmeshqsgrenderer.h`
- `src/map/swmm2dresultsqsgrenderer.cpp`
- `include/map/swmm2dresultsqsgrenderer.h`
- `src/map/mapcanvas.cpp`
- `src/layers/swmm2dmeshlayer.cpp`
- `include/layers/swmm2dmeshlayer.h`
- `src/layers/swmm2dresultslayer.cpp`
- `include/layers/swmm2dresultslayer.h`
- `include/layers/meshspatialgrid.h`
- `src/layers/meshspatialgrid.cpp`

Important current hotspots:

- `SWMM2DMeshQSGRenderer::setMapExtent()` marks content dirty on every zoom.
- `SWMM2DResultsQSGRenderer::setMapExtent()` marks content dirty on every zoom.
- Both QSG renderers still build large `std::vector<QSGGeometry::...>` payloads
  inside `updatePaintNode()`.
- Result mesh-edge rendering scans all edges in one pass.
- Mesh/result vertex markers repeat shared vertices and should be LOD-gated.
- Highlight changes are coupled to full content rebuilds in the results path.

## Execution Rules

- Keep rendering decisions out of `updatePaintNode()` where possible. Extract
  pure helper classes/functions and unit-test them.
- Do not make pan/zoom performance depend on `QGraphicsScene::render()` behavior.
- Do not add new per-cell `QObject`, `QGraphicsItem`, or `QSGNode` objects.
- Do not make dense labels, mesh vertices, wireframe, or velocity vectors visible
  at full extent.
- Keep visual parity at near zoom. LOD may simplify only when the original detail
  is subpixel or visually noisy.
- Keep the CPU/QPainter path working while the QSG path is being changed.
- Preserve existing user-facing sublayer visibility and opacity behavior.

## Phase 1: Instrumentation And Dirty Reasons

Add a runtime-gated perf logger controlled by:

```text
OPENSWMM_RENDER_PERF=1
```

Record per QSG sync:

- renderer name: `mesh2d` or `results2d`
- dirty reason bitset: `pan`, `zoom`, `time`, `style`, `selection`, `geometry`,
  `layer`, `visibility`
- visible cells, visible edges, visible vertices
- built vertices by pass
- uploaded bytes by pass
- QSG repaint time if measurable
- framebuffer grab time in `MapCanvas`

Implementation guidance:

- Add a tiny helper struct, for example `Qsg2DRenderStats`, under `include/map/`
  or `include/render/`.
- Keep the helper independent of Qt Quick so it can be unit-tested.
- Logging must be silent unless `OPENSWMM_RENDER_PERF=1` is set.

Required tests:

- Add a unit test target, for example `test_qsg2d_renderstats`, that verifies:
  - dirty reason formatting is stable
  - byte counters sum correctly
  - disabled logging has no formatted output path

Run:

```sh
cmake --build build/darwin-debug --target test_qsg2d_renderstats -j2
ctest --test-dir build/darwin-debug -R '^test_qsg2d_renderstats$' --output-on-failure
```

Acceptance:

- Existing UI behavior is unchanged.
- With `OPENSWMM_RENDER_PERF=1`, a pan/zoom/selection/time-step sequence produces
  enough data to identify whether the renderer rebuilt or only transformed.

## Phase 2: Dirty Domain Separation

Split each 2D renderer into separate dirty domains:

- `Geometry`: mesh topology or scene coordinates changed.
- `Style`: color ramp, opacity, line width, marker size, contour settings.
- `Data`: result time/depth/velocity values changed.
- `Selection`: highlighted cells/edges/vertices changed.
- `Lod`: zoom crossed a display-detail threshold.
- `Transform`: extent changed but content does not need rebuild.

Expected behavior:

- Pan sets only `Transform`.
- Zoom sets `Transform`; it sets `Lod` only if the computed LOD bucket changes.
- Selection/highlight sets only `Selection`.
- Time-step change sets `Data`, and possibly contour/vector dirty bits, but not
  static geometry.
- Style change sets `Style` for affected passes only.

Implementation guidance:

- Introduce a pure helper, for example `Qsg2DDirtyState`.
- The helper receives events and current LOD bucket, then returns dirty bits.
- Renderers call this helper rather than directly toggling one `m_contentDirty`.
- Preserve a temporary full-rebuild fallback while each pass is migrated.

Required tests:

- Add `test_qsg2d_dirtystate`.
- Cover these transitions:
  - pan after clean frame -> `Transform` only
  - zoom within same LOD -> `Transform` only
  - zoom across LOD -> `Transform | Lod`
  - highlighted cells changed -> `Selection` only
  - time changed -> `Data` only plus the result-specific pass bits
  - geometry revision changed -> `Geometry`
  - style changed -> `Style`

Run:

```sh
cmake --build build/darwin-debug --target test_qsg2d_dirtystate -j2
ctest --test-dir build/darwin-debug -R '^test_qsg2d_dirtystate$' --output-on-failure
```

Acceptance:

- Pan no longer causes a full 2D mesh/results content rebuild.
- Selection no longer rebuilds base cell fill, base edges, contours, or vectors.

## Phase 3: LOD Policy

Create a deterministic LOD policy from current map extent, viewport size, and
mesh density.

At minimum define these buckets:

- `Far`: aggregate/raster-style fill only; no dense wireframe, vertices, labels,
  or dense vectors.
- `Mid`: exact or chunked fill; optional contours/vectors with caps; sparse
  edges only when useful.
- `Near`: exact cells, exact selected overlays, optional wireframe and vertices.

Policy inputs:

- viewport logical size
- map extent
- approximate cell count or visible cell count
- projected average cell area
- sublayer visibility

Implementation guidance:

- Add a pure helper, for example `Qsg2DLodPolicy`.
- Return both the bucket and per-pass visibility decisions:
  - draw fill
  - draw edges
  - draw vertex markers
  - draw contours
  - draw contour labels
  - draw velocity vectors
  - draw selected overlays
- Always draw selected overlays when selected items are visible, even if the base
  dense pass is hidden.

Required tests:

- Add `test_qsg2d_lodpolicy`.
- Cover:
  - 1M cells at full extent -> `Far`, edges off, vertices off, labels off
  - 1M cells near zoom -> `Near`, exact edges allowed
  - selected cells in `Far` -> selected overlay remains enabled
  - LOD bucket is stable under tiny zoom jitter
  - changing viewport size can change bucket predictably

Run:

```sh
cmake --build build/darwin-debug --target test_qsg2d_lodpolicy -j2
ctest --test-dir build/darwin-debug -R '^test_qsg2d_lodpolicy$' --output-on-failure
```

Acceptance:

- Zoom no longer dirties content unless the LOD bucket changes.
- Full extent does not submit dense wireframe or dense vertex markers for large
  meshes.

## Phase 4: Chunk Index

Create a chunk index for mesh/result rendering. This is separate from
`MeshSpatialGrid`; it is for rendering batch decisions, not point picking.

Each chunk should store:

- bbox
- cell id range or cell id list
- edge id range or edge id list
- approximate cell count
- optional aggregate values for far LOD

Implementation guidance:

- Add a pure class such as `MeshRenderChunkIndex`.
- Build chunks from triangle bboxes and edge bboxes.
- Keep chunk data stable across pan/zoom.
- A chunk query should return:
  - fully visible chunks
  - boundary chunks
  - optional aggregate chunks for far LOD
- For now, boundary chunks may fall back to existing per-triangle/per-edge cull.

Required tests:

- Add `test_meshrenderchunkindex`.
- Cover:
  - empty mesh
  - single chunk round trip
  - no duplicate cell ids from overlapping chunk cells
  - query fully inside vs boundary chunk classification
  - chunk bbox is a superset of member cell bboxes
  - rebuild replaces old data
  - deterministic chunk ids for deterministic input

Also keep running:

```sh
ctest --test-dir build/darwin-debug -R '^test_meshspatialgrid$' --output-on-failure
```

Acceptance:

- Render culling can be expressed in chunks.
- Existing picking via `MeshSpatialGrid` is unchanged.

## Phase 5: Static Geometry Buffers

Stop expanding static cell positions every QSG rebuild.

Target structure:

- static position array, anchor-relative
- static triangle index array
- optional static edge endpoint/index array
- dynamic color/scalar array
- dynamic overlay arrays for selected cells/edges/vertices

Implementation guidance:

- Add a pure builder such as `MeshStaticGeometryBuffers`.
- Build from the layer scene caches after `rebuildSceneGeometry()`.
- Keep the existing expanded `QSGGeometry::ColoredPoint2D` path as a fallback
  until the persistent-buffer path is complete.
- Do not attempt shader color mapping in this phase unless needed.

Required tests:

- Add `test_meshstaticgeometrybuffers`.
- Cover:
  - two triangles sharing an edge produce shared vertex positions
  - triangle indices reference valid vertices
  - anchor-relative positions are stable across pan and zoom
  - geometry revision change rebuilds buffers
  - style/data/selection events do not rebuild static buffers

Also run:

```sh
ctest --test-dir build/darwin-debug -R '^(test_meshedgecount|test_meshspatialgrid)$' --output-on-failure
```

Acceptance:

- Static positions/indices are created once per geometry revision.
- Pan/zoom does not rebuild static mesh buffers.

## Phase 6: Pass Migration

Migrate passes one at a time. Do not rewrite every pass at once.

Recommended order:

1. Base cell fill.
2. Highlight/selection overlay.
3. Mesh edges.
4. Vertex markers.
5. Velocity vectors.
6. Contours and contour labels.

For each pass:

- Add stats before changing behavior.
- Add/adjust unit tests for pure pass-decision logic.
- Keep a fallback path until visual parity is confirmed.
- Verify the dirty domain for that pass is correct.

Pass-specific instructions:

- Base fill:
  - At `Far`, use chunk aggregate or raster-like fill.
  - At `Near`, use exact cell geometry.
  - Time-step changes should update data/color, not positions.
- Highlight overlay:
  - Always exact for selected cells.
  - Rebuild only selected overlay geometry.
- Mesh edges:
  - Disable dense wireframe at `Far`.
  - Use chunk/visible-edge culling at `Mid` and `Near`.
  - Avoid round caps/joins except for visibly thick/selected lines.
- Vertex markers:
  - Disable at `Far` and usually `Mid`.
  - Render selected vertices even when dense markers are disabled.
- Velocity vectors:
  - Use screen-space sampling with a maximum glyph count.
  - Never one glyph per cell for 1M-cell views unless explicitly in `Near` and
    below a safe cap.
- Contours:
  - Recompute asynchronously when possible.
  - Labels should be disabled at `Far` and capped at `Mid`.

Required tests:

- Extend `test_qsg2d_lodpolicy` for per-pass decisions.
- Add focused tests for any extracted pass helper.
- Existing tests to run after each migrated pass:

```sh
ctest --test-dir build/darwin-debug -R '^(test_2d_sublayers|test_meshedgecount|test_meshprofileinterp|test_meshspatialgrid)$' --output-on-failure
```

Acceptance:

- Each migrated pass reports fewer built/uploaded vertices at full extent.
- Near zoom remains visually equivalent to the current path.

## Phase 7: Async Heavy Work

Move expensive non-render-thread work off the QSG update path:

- contour generation
- contour label placement
- velocity sampling over large extents
- far-LOD aggregate generation
- color/scalar chunk updates if CPU-side coloring remains

Implementation guidance:

- Use cancellable workers.
- Double-buffer results.
- Render the previous completed buffers while a new worker result is pending.
- Do not access QSG nodes, QSG textures, or QQuickWindow objects from workers.

Required tests:

- Add pure tests for cancellation-safe result replacement if a helper is
  extracted.
- Add a GUI/integration test only if the worker can be tested headlessly without
  relying on a real render loop.

Acceptance:

- Changing time/style while a previous contour/vector job is pending does not
  apply stale output.
- The GUI stays responsive during heavy derived-geometry rebuilds.

## Phase 8: Optional Shader Color Mapping

Do this only after the static buffer and dirty-domain work is stable.

Goal:

- Upload scalar values and a ramp texture/uniform.
- Let the GPU map scalar to color.
- Style/ramp changes update tiny GPU state instead of recoloring 1M cells on CPU.

Required tests:

- Unit-test ramp normalization and clamp behavior in a pure helper.
- Add screenshot/manual verification for shader parity because the shader itself
  is not meaningfully unit-testable.

Acceptance:

- Time-step changes and ramp changes avoid full position/index upload.
- Color parity with CPU ramp is visually acceptable.

## Build And Test Gates

Minimum build gate for every phase:

```sh
cmake --build build/darwin-debug --target SWMMVis -j2
```

Core regression suite for this work:

```sh
ctest --test-dir build/darwin-debug -R '^(test_meshspatialgrid|test_meshedgecount|test_2d_sublayers|test_meshprofileinterp|test_mesh2dh5reader)$' --output-on-failure
```

When adding new pure helpers, add tests to `tests/unit/CMakeLists.txt` if they
do not need widgets. Add to `tests/gui/CMakeLists.txt` only when they need Qt
Gui/Widgets or existing GUI test scaffolding.

Expected new tests:

- `test_qsg2d_renderstats`
- `test_qsg2d_dirtystate`
- `test_qsg2d_lodpolicy`
- `test_meshrenderchunkindex`
- `test_meshstaticgeometrybuffers`

Manual verification after major phases:

1. Launch the app with a normal model and a large synthetic/real 2D mesh.
2. Enable `OPENSWMM_RENDER_PERF=1`.
3. Test pan, wheel zoom, time-step scrub, style edit, cell selection, and profile
   overlay.
4. Confirm logs show:
   - pan -> transform only
   - zoom within same LOD -> transform only
   - selection -> overlay only
   - time step -> data/result passes only
   - geometry edit/load -> static geometry rebuild

## Performance Targets

Use these as acceptance goals, not hard-coded test assertions:

- Pan/zoom after first render: at least 30 fps.
- Full-extent idle render after warm cache: under 33 ms.
- Selection/highlight change: under 16 ms.
- Time-step scrub: initially under 50 ms, eventually under 16-33 ms.
- Static geometry rebuild: only on geometry revision/layer change.
- Memory usage: stable during pan/zoom.

## Stop Conditions

Pause and report before continuing if:

- A phase requires deleting the CPU/QPainter fallback.
- A change requires making every cell/object a QObject, QGraphicsItem, or QSGNode.
- A pass cannot be made testable without extracting pure logic.
- Full-extent rendering still submits dense edges or dense vertex markers after
  LOD gating.
- Pan still reports geometry/data/style dirty after Phase 2.
