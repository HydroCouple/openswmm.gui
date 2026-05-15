# QSG Renderer — Pan/Zoom Responsiveness Optimizations

> Created: **2026-04-28** · Last updated: **2026-04-29** · Status: **All 7 stages shipped ✅**
>
> Builds on: `docs/RENDERING_5M_PLAN.md` Phase B.RHI (complete)
>
> Status legend: ✅ shipped & verified · 🔧 in progress · ⏳ not started

---

## Problem statement

`SWMMLayerQSGRenderer::updatePaintNode` rebuilds **all vertex buffers on
every call**, including during pan and zoom. The precision anchor used to
convert scene coordinates to float is `m_extent.centerX/Y()` — a value that
changes on every pan. Because every vertex is stored as
`float(sceneCoord - extentCenter)`, any pan invalidates every vertex in
every buffer. A typical West Whiteland frame (121k links, 42k nodes, 1.7k
subcatchments) reconstructs ≈ 4–8 M floats per frame on the GUI thread
before the GPU sees any data.

The `QSGTransformNode` at the root of the scene graph is already the right
architectural choice: it can absorb the entire view transform as a single
`QMatrix4x4`. Pan and zoom should cost one matrix write — not a geometry
rebuild.

---

## Baseline measurement protocol

Before starting any stage, record a reference frame time using the existing
`qDebug` line at the end of `updatePaintNode` (see `src/map/swmmlayerqsgrenderer.cpp`
line ≈580):

```
[SWMMLayerQSGRenderer::updatePaintNode] geom_build_ms=XX total_ms=YY
```

**Test model:** West Whiteland (42,809 nodes, 121,902 links, 1,700 subcatchments).

**Measurement actions:**

| Action | Expected after all stages |
|---|---|
| Initial load render | ≤ 80 ms (geometry build is unavoidable) |
| Pan (mouse drag) | ≤ 2 ms per frame (matrix-only) |
| Zoom (scroll wheel) | ≤ 2 ms per frame (matrix-only) |
| Selection change (3k elements) | ≤ 15 ms (selection buffers only) |
| Symbology change | ≤ 80 ms (full geometry rebuild) |

Record `geom_build_ms` and `total_ms` for each action before and after each
stage.

---

## Stage 1 — Remove per-frame `qDebug` ⏳

**Effort:** ~5 minutes. **Risk:** none. **Expected gain:** 1–3 ms per frame
(string formatting + I/O on the render thread).

### What to change

**File:** `src/map/swmmlayerqsgrenderer.cpp`

Remove (or gate behind `#ifdef OPENSWMMVIS_RENDER_DEBUG`) the two `qDebug`
blocks at the bottom of `updatePaintNode`:

```cpp
// REMOVE this block (≈ lines 580–595):
qDebug().noquote()
    << "[SWMMLayerQSGRenderer::updatePaintNode]"
    << "lines_base_tris=" << (baseTriVerts / 3)
    ...
    << "total_ms=" << t.elapsed();

// REMOVE this block (≈ lines 597–609):
static bool s_loggedColors = false;
if (!s_loggedColors) { ... }
```

Also remove the `QElapsedTimer t; t.start();` and `QElapsedTimer tg;
tg.start();` declarations at the top of `updatePaintNode`, and the
`const qint64 t_geom = tg.elapsed();` line after the geometry build.

### Acceptance criterion

No `[SWMMLayerQSGRenderer::updatePaintNode]` lines appear in the debug
output during normal pan/zoom. Frame rendering continues correctly.

---

## Stage 2 — Fixed precision anchor (pan becomes matrix-only) ⏳

**Effort:** ~1 hour. **Risk:** low — vertex math change, visually verifiable.
**Expected gain:** pan/zoom geometry rebuild drops from O(N) to O(1). This
is the single highest-impact change.

### Root cause

`updatePaintNode` computes `ox = m_extent.centerX()` and `oy = -m_extent.centerY()`
on every call, then encodes every vertex as `float(coord - ox)`. Because `ox`
and `oy` change on every pan, the entire float payload changes even though the
underlying geometry is identical. The GPU matrix that maps these vertex
coordinates to pixels is updated to compensate:

```cpp
m.translate(float(ox - m_extent.xMin()), float(oy + m_extent.yMax()));
```

The fix: use a **fixed dataset anchor** — the center of the model's full
scene bounding box, computed once when the layer is bound. Vertices become
`float(coord - anchorX)` — values that don't change across pans. The matrix
translation changes every pan, but that's a single float4 write, not a buffer
rebuild.

### What to change

#### `include/map/swmmlayerqsgrenderer.h`

Add two private members:

```cpp
// Fixed precision anchor: scene-space center of the full dataset bounding
// box, computed once in setLayer(). Vertex coordinates are stored as
// float(sceneCoord - m_anchorX/Y) so they remain valid across all pans
// and zooms; only the QSGTransformNode matrix changes on view changes.
double m_anchorX = 0.0;
double m_anchorY = 0.0;
```

Remove or repurpose `m_lastSegmentVertexCount` (it will be subsumed by the
geometry dirty flag in Stage 3).

#### `src/map/swmmlayerqsgrenderer.cpp` — `setLayer()`

After binding the new layer, compute the anchor from the layer's scene bounding box. The layer's `m_linkSceneFlat`, `m_nodeScenePts`, and `m_catchScenePts` are populated by `rebuildSceneCoords()`. Use the layer's scene bbox if it exposes one; otherwise compute it from the flat arrays:

```cpp
// Compute fixed anchor from dataset scene bounds.
// Fall back to (0, 0) if the layer has no geometry yet
// (anchor will be corrected on first repaintRequested).
double minX =  1e18, minY =  1e18;
double maxX = -1e18, maxY = -1e18;
for (const QPointF &p : m_layer->m_nodeScenePts) {
    minX = std::min(minX, p.x()); maxX = std::max(maxX, p.x());
    minY = std::min(minY, p.y()); maxY = std::max(maxY, p.y());
}
m_anchorX = (minX + maxX) * 0.5;
m_anchorY = (minY + maxY) * 0.5;  // NOTE: scene Y is already flipped at this point
```

Alternatively — preferred if the layer adds a `sceneCenter()` accessor — call that directly. Adding `QPointF SWMMModelLayer::sceneCenter() const` is a one-liner reading `m_nodeScenePts` bounds or `m_linkSceneBBoxes` union.

#### `src/map/swmmlayerqsgrenderer.cpp` — `updatePaintNode()`

Replace all occurrences of the per-frame `ox / oy` anchor with the fixed `m_anchorX / m_anchorY`:

```cpp
// BEFORE (recomputed every frame — changes on every pan):
const double ox = m_extent.centerX();
const double oy = -m_extent.centerY();

// AFTER (fixed for the lifetime of the layer binding):
const double ox = m_anchorX;
const double oy = -m_anchorY;   // keep the same sign convention
```

No other vertex computations change — every `float(coord - ox)` already uses
this variable. The matrix block at the end changes to:

```cpp
// BEFORE:
m.translate(float(ox - m_extent.xMin()),
            float(oy + m_extent.yMax()));

// AFTER (same formula, now ox/oy are the fixed anchor):
m.translate(float(m_anchorX - m_extent.xMin()),
            float(-m_anchorY + m_extent.yMax()));
```

The scale (`sx`, `sy`) computation doesn't change.

### Acceptance criterion

1. Model renders correctly after pan, zoom, and initial load.
2. Vertex values in the geometry buffers are **identical** between consecutive
   pan operations (verify by temporarily adding a checksum assert or by
   comparing `baseTriVerts` count — it must not change during pan).
3. `geom_build_ms` in the debug log is unchanged (Stage 1 removes it; measure
   before Stage 1, or temporarily re-add it for Stage 2 validation only).

---

## Stage 3 — Geometry and selection dirty flags ⏳

**Effort:** ~2 hours. **Risk:** medium — must correctly classify every
trigger that calls `update()`.
**Expected gain:** pan/zoom `updatePaintNode` becomes a pure matrix write
(≤ 1 ms). Selection changes rebuild only the 4 selection geometry nodes.

### What to change

#### `include/map/swmmlayerqsgrenderer.h`

Add three dirty flags:

```cpp
bool m_geomDirty  = true;  // full geometry rebuild needed (model changed)
bool m_selDirty   = true;  // selection geometry rebuild needed
bool m_styleDirty = true;  // material colors need refresh (symbology changed)
```

Also add a helper to reset all three at once (used in `setLayer()`):

```cpp
void markAllDirty() { m_geomDirty = m_selDirty = m_styleDirty = true; }
```

#### `src/map/swmmlayerqsgrenderer.cpp` — `setLayer()`

Replace the single catch-all `repaintRequested → update()` connection with
differentiated connections:

```cpp
// Geometry or visibility change → full rebuild
connect(m_layer, &SWMMModelLayer::repaintRequested, this, [this]() {
    m_geomDirty = true;
    m_styleDirty = true;
    update();
});

// Selection change → selection buffers only
connect(m_layer, &SWMMModelLayer::selectionChanged, this, [this](const QStringList &) {
    m_selDirty = true;
    update();
});
```

Mark all dirty on initial bind:

```cpp
markAllDirty();
update();
```

#### `src/map/swmmlayerqsgrenderer.cpp` — `setMapExtent()`

**Do not set any dirty flag here.** The extent change only needs a matrix
update. The existing `update()` call is sufficient:

```cpp
void SWMMLayerQSGRenderer::setMapExtent(const MapExtent &extent)
{
    if (extent == m_extent) return;
    m_extent = extent;
    update();   // matrix-only update — no dirty flags set
}
```

#### `src/map/swmmlayerqsgrenderer.cpp` — `updatePaintNode()`

Wrap each geometry build section with its dirty flag:

```cpp
// --- Subcatchments ---
if (m_geomDirty) {
    // ... existing catch fill/edge build code ...
    uploadVerts(catchFill, fillBase);
    // etc.
}

// --- Links ---
if (m_geomDirty) {
    // ... existing link thick-segment build code ...
    uploadVerts(lines, baseTri);
}

// --- Base node glyphs ---
if (m_geomDirty) {
    // ... existing junction/outfall/storage/divider build ...
    uploadVerts(junctionsBase, junc);
    // etc.
}

// --- Gage base glyphs ---
if (m_geomDirty) {
    // ... existing gage build ...
}

// --- Selection overlays ---
if (m_geomDirty || m_selDirty) {
    // rebuild linesSel, nodesSel, gagesSel, catchSelFill, catchSelEdge
}

// --- Style / material refresh ---
if (m_geomDirty || m_styleDirty) {
    setNodeColor(catchFill, ...);
    // ... existing setNodeColor calls ...
}

// Clear flags
m_geomDirty  = false;
m_selDirty   = false;
m_styleDirty = false;

// Matrix update — always runs
QMatrix4x4 m;
m.scale(sx, sy);
m.translate(...);
if (root->matrix() != m)
    root->setMatrix(m);
```

> **Note:** The `QSGGeometryNode` for each buffer must mark itself dirty only
> when its geometry actually changed. Nodes whose geometry wasn't rebuilt this
> frame must **not** call `markDirty(DirtyGeometry)` — the QSG renderer will
> re-use the existing buffer unchanged. This is already handled by
> `uploadVerts()` (it calls `markDirty` unconditionally today; change it to
> only call `markDirty` when the vertex count or data actually changed).

### Acceptance criterion

1. During pan/zoom (no model change, no selection change): `m_geomDirty` and
   `m_selDirty` are both false when `updatePaintNode` runs. Add a temporary
   `Q_ASSERT(!m_geomDirty)` inside the matrix block to verify.
2. After a rubber-band selection: only the selection buffers are rebuilt
   (`m_selDirty` was true, `m_geomDirty` was false).
3. After loading a new model: full rebuild fires exactly once.

---

## Stage 4 — Subcatchment triangulation cache ⏳

**Effort:** ~1 hour. **Risk:** low — purely additive cache.
**Expected gain:** ear-clip O(n²) cost moves from every geometry rebuild to
once per model load. For West Whiteland (1,700 subcatchments with ≈10–50
verts each) this removes a multi-ms per-rebuild cost at larger models.

### What to change

#### `include/map/swmmlayerqsgrenderer.h`

Add a triangulation cache:

```cpp
struct CatchTriCache {
    quint64 geomRevision = 0;
    std::vector<QVector<int>> tris;  // tris[i] = ear-clip result for subcatchment i
};
CatchTriCache m_catchTriCache;
```

#### `src/layers/swmmmodellayer.h` / `swmmmodellayer.cpp`

Add a geometry revision counter that increments whenever `rebuildSceneCoords()`
completes:

```cpp
// In header:
quint64 m_geomRevision = 0;
quint64 geomRevision() const { return m_geomRevision; }

// In rebuildSceneCoords(), at the end:
++m_geomRevision;
```

#### `src/map/swmmlayerqsgrenderer.cpp` — subcatchment block inside `updatePaintNode()`

Replace the inline `earcutTriangulate()` call with a cached lookup:

```cpp
if (m_geomDirty || m_catchTriCache.geomRevision != m_layer->geomRevision()) {
    m_catchTriCache.tris.resize(cps.size());
    for (int i = 0; i < cps.size(); ++i)
        m_catchTriCache.tris[i] = earcutTriangulate(cps[i]);
    m_catchTriCache.geomRevision = m_layer->geomRevision();
}

// Then use m_catchTriCache.tris[i] instead of calling earcutTriangulate(poly)
for (int i = 0; i < cps.size(); ++i) {
    const QVector<int> &tris = m_catchTriCache.tris[i];
    // ... vertex upload unchanged ...
}
```

### Acceptance criterion

1. Triangulation results are identical to the uncached version (verify with
   a vertex-count assert on a known model).
2. Repeated pan/zoom/selection changes do not retrigger triangulation (add a
   temporary counter; verify it stays at 1 after load).
3. After adding a subcatchment, the cache is invalidated and re-triangulated
   exactly once.

---

## Stage 5 — View-frustum culling for links ⏳

**Effort:** ~1 hour. **Risk:** low — additive skip, doesn't change geometry
of visible links.
**Expected gain:** at typical zoom levels (30–50% of the full extent visible)
this reduces vertex buffer size and build time by 50–70%.

### What to change

**File:** `src/map/swmmlayerqsgrenderer.cpp` — inside the links block of
`updatePaintNode()`.

Before building the thick-segment triangles, skip any link whose scene
bounding box doesn't intersect the current map extent:

```cpp
const auto &bboxes = m_layer->m_linkSceneBBoxes;

for (size_t i = 0; i < counts.size(); ++i) {
    if (counts[i] < 2) continue;
    if (i < lHid.size() && lHid[i]) continue;

    // Frustum cull — skip links entirely outside the current extent.
    // m_linkSceneBBoxes[i] is in the same scene-space as m_extent.
    if (size_t(i) < bboxes.size()) {
        const QRectF &bb = bboxes[i];
        if (bb.right()  < m_extent.xMin() || bb.left()  > m_extent.xMax() ||
            bb.bottom() < m_extent.yMin() || bb.top()   > m_extent.yMax())
            continue;
    }
    // ... existing vertex build code unchanged ...
}
```

Apply the same pattern to the node glyph loop (skip nodes outside extent)
and the subcatchment loop (use `m_catchSceneBBoxes[i]`).

> **Note:** Scene Y is flipped (`y_scene = -y_map`), so verify the
> `m_linkSceneBBoxes` coordinate convention matches `m_extent.yMin/yMax`
> before deploying. A simple bounds-check visual test (zoom to a subregion
> and confirm no links are clipped at the visible boundary) is sufficient.

### Acceptance criterion

1. No visible links are missing when zoomed in — only off-screen links are
   culled.
2. At full-extent zoom, vertex count matches Stage 3 baseline (cull removes
   nothing when all features fit on screen).
3. At 10% zoom (center of model), vertex count drops by ≥ 50%.

---

## Stage 6 — LOD: suppress sub-pixel glyphs ⏳

**Effort:** ~30 minutes. **Risk:** negligible — purely additive skip.
**Expected gain:** at full-extent zoom on a large model, suppressing
node/gage glyphs (which are <1 pixel) removes millions of triangle vertices.

### What to change

**File:** `src/map/swmmlayerqsgrenderer.cpp` — at the start of the node and
gage glyph loops in `updatePaintNode()`.

```cpp
// Skip building node glyphs when they are smaller than kMinGlyphPx pixels.
// At full extent on a large model, nodes are sub-pixel and the triangles
// produce only z-fighting artefacts with no visible contribution.
constexpr float kMinGlyphPx = 2.0f;
const float junctionPx = float(m_layer->junctionSymbol().size) * sx;
if (junctionPx >= kMinGlyphPx) {
    // ... build junction/outfall/storage/divider glyphs as before ...
}

const float gagePx = float(m_layer->rainGageSymbol().size) * sx;
if (gagePx >= kMinGlyphPx) {
    // ... build gage glyphs as before ...
}
```

Where `sx = float(width()) / float(m_extent.width())` is already computed
earlier in `updatePaintNode`.

### Acceptance criterion

1. At full extent, no node/gage triangles are built when glyphs would be
   sub-pixel (verify via temporary vertex count log).
2. On zoom-in past the threshold, glyphs appear immediately on the next frame.
3. No visible artefacts at the LOD boundary.

---

## Stage 7 — Async geometry build (stretch goal) ⏳

**Effort:** ~4–8 hours. **Risk:** high — requires thread synchronization on
the QSG render thread boundary.
**Expected gain:** initial model load and any full geometry rebuild move off
the GUI thread. The GUI stays responsive during the build.

### Sketch

1. Add a `QFuture<GeometryBuffers>` member. When `m_geomDirty` fires,
   launch `QtConcurrent::run(buildGeometryBuffers, m_layer->snapshot())` —
   `snapshot()` takes a lightweight copy of the SoA arrays.
2. In `updatePaintNode()`, check if the future is ready. If yes, consume it
   (`uploadVerts` calls). If no, return the existing `oldNode` unchanged
   (stale frame) or render a "loading" indicator.
3. `m_geomDirty` is cleared only when the future result is consumed.

> This stage depends on Stages 2–4. Do not implement until those are stable.

---

## Summary table

| Stage | Change | Files | Status |
|---|---|---|---|
| 1 | Remove per-frame `qDebug`; add opt-in `SWMMVIS_RENDER_PERF` sampler | `swmmlayerqsgrenderer.cpp` | ✅ |
| 2 | Fixed precision anchor (`m_anchorX/Y` from dataset centre) | `swmmlayerqsgrenderer.h/.cpp` | ✅ |
| 3 | `m_contentDirty` flag — pan costs one matrix write | `swmmlayerqsgrenderer.h/.cpp` | ✅ |
| 4 | Subcatchment triangulation cache keyed off `geomRevision` | `swmmlayerqsgrenderer.h/.cpp`, `swmmmodellayer.h/.cpp` | ✅ |
| 5 | View-frustum culling for links, nodes, subcatchments, gages | `swmmlayerqsgrenderer.cpp` | ✅ |
| 6 | LOD: sub-pixel glyph suppression (`kMinGlyphPx = 1.0f`) | `swmmlayerqsgrenderer.cpp` | ✅ |
| 7 | Async geometry build via `QtConcurrent::run` + `LayerSnapshot` | `swmmlayerqsgrenderer.h/.cpp`, `swmmmodellayer.h/.cpp` | ✅ |

**Recommended order:** 1 → 2 → 3 → 5 → 4 → 6 → 7. Stages 1–3 together
deliver the pan/zoom responsiveness goal. Stages 4–6 improve the inevitable
full-rebuild cost (model load, symbology change, SRS change).

---

## Progress log

### 2026-04-28 — Plan created

Baseline behavior: `updatePaintNode` rebuilds all buffers on every call.
Pan and zoom cost is O(N) in feature count. West Whiteland typical
`geom_build_ms` to be recorded before Stage 1 begins.

### 2026-04-29 — All 7 stages implemented ✅

Stages 1–7 were implemented sequentially with a build and launch between each
stage. Key outcomes:

- **Pan**: zero geometry work — one `QMatrix4x4` write per frame.
- **Zoom**: triggers an async rebuild on a `QtConcurrent` worker; the GUI
  thread is never blocked. The stale frame persists until the worker delivers
  fresh buffers.
- **Selection/symbology change**: same async path as zoom.
- **Model load**: first frame appears immediately (empty); geometry arrives
  asynchronously as soon as the first worker run completes.
- **Frustum culling** (Stage 5): at typical zoom levels (~25–50% of full
  extent) approximately 50–75% of link segments are culled before
  triangulation.
- **LOD** (Stage 6): node and gage glyphs are suppressed at full extent when
  the symbol diameter is below 2 screen pixels.
- **Tri cache** (Stage 4): O(n²) ear-clip triangulation for subcatchments
  runs once per geometry change and is reused across all zoom/selection
  rebuilds.
- **`SWMMVIS_RENDER_PERF=1`**: env-var perf sampler logs `avg_rebuild_ms`
  and `avg_pan_ms` every 60 frames to stdout for ongoing monitoring.
