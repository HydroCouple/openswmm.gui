# Rendering Performance — Next Steps (Plan for Review)

> Created: **2026-05-28** · Status: **H0 + F1 + F2 shipped 2026-05-28 ✅ · H1–H6 pending measurement**
>
> Predecessors: `RENDERING_5M_PLAN.md` (Phase A + B.RHI), `QSG_PAN_ZOOM_OPTIMIZATIONS.md` (7 stages shipped 2026-04-29).
>
> Reported symptom: even with QSG/OpenGL enabled, pan/zoom/repaint on real-world models still feel sluggish.

This plan does **not** assume any of the strategies below should be implemented. Each is a hypothesis paired with a verification step. The goal of the review is to pick which 1–3 hypotheses to chase first, in what order, and what we explicitly defer.

---

## 0. State of the world (verified, not assumed)

- `PreferencesManager::qsgRenderEnabled()` defaults to **true** ([preferencesmanager.cpp:489-493](../src/core/preferencesmanager.cpp#L489-L493)). `MapCanvas::syncQsgRenderKindsFromPreferences()` then sets the mask to `QsgNodes | QsgLinks | QsgCatch | QsgGages` ([mapcanvas.cpp:802-819](../src/map/mapcanvas.cpp#L802-L819)). So the QSG path is live for users with default settings.
- The field default in the header is `m_qsgKinds = QsgNone` ([swmmmodellayer.h:1612](../include/layers/swmmmodellayer.h#L1612)) — that is the pre-sync state, not the runtime state.
- `SWMMLayerQSGRenderer::updatePaintNode` has a working pan-only fast path ([swmmlayerqsgrenderer.cpp:349-377](../src/map/swmmlayerqsgrenderer.cpp#L349-L377)) and a wide cull margin (50% of viewport).
- Subcatchment triangulation is cached by `geomRevision` ([swmmlayerqsgrenderer.cpp:540-546](../src/map/swmmlayerqsgrenderer.cpp#L540-L546)), so earcut does not re-run per frame.
- Old CPU path in `SWMMLayerItem::paint()` is still compiled and still draws **labels and any kinds not owned by QSG** every frame ([swmmlayeritem.cpp:835-918](../src/map/swmmlayeritem.cpp#L835-L918) is the label loop).

### 0.1 Basemap / raster path — already correct, do not change

The basemap blit in `MapCanvas::paintEvent()` already implements the QGIS-style stale-buffer transform ([mapcanvas.cpp:1237-1287](../src/map/mapcanvas.cpp#L1237-L1287)):

- `m_mapBuffer` is the last fully-rendered raster (basemap + raster layers), produced asynchronously by `MapRenderJob`.
- `m_mapBufferExtent` records the extent that buffer covers.
- During pan/zoom, `paintEvent` computes the destination pixel rect that maps `m_mapBufferExtent` into the *current* `m_extent` and issues a single `p.drawImage(dstRect, m_mapBuffer)`. That's one fast blit — no per-pixel reverse-projection, no tile reload, no scene re-render.
- `endPan()` and zoom-release trigger `refresh()`, which kicks off a new `MapRenderJob`; when it completes, `m_mapBuffer` and `m_mapBufferExtent` are swapped in and the buffer aligns exactly again.

**Accepted trade-off:** during a drag, the leading edge of the viewport shows a thin background-coloured strip where the stale buffer doesn't cover. This matches QGIS, ArcGIS Pro, and every other interactive GIS — users read it as "the map is loading there" and it disappears as soon as the drag ends. **This behaviour is intentional and must be preserved by any change in this plan.**

This pattern is the model for what the SWMM QSG layer should also do during pan (see H0 below).

### 0.2 Verification before any work

Run the app with the user's actual model, set `SWMMVIS_RENDER_PERF=1`, log one full pan + one zoom + one selection. Numbers go in the table at §6. Without those numbers, every strategy below is guesswork.

---

## 1. Hypotheses ranked by suspected impact

The QSG renderer already has the structural wins (single batched node, vertex buffers, matrix-only pan). If it still feels slow, the cost is most likely one of these — listed in the order I'd investigate.

### H0 — QSG framebuffer is re-grabbed every pan frame (likely the actual bottleneck) ✅ shipped 2026-05-28

**Where:** [mapcanvas.cpp:1321-1366](../src/map/mapcanvas.cpp#L1321-L1366).

**Current behaviour:** `MapCanvas::paintEvent()` drives the QSG widget manually:

```cpp
m_qsgWidget->repaint();                    // synchronous: runs updatePaintNode + GPU render
m_qsgFrameCache = m_qsgWidget->grabFramebuffer();  // GPU → CPU readback
```

There IS a cache (`m_qsgFrameCache`) keyed on `(layer, extent, size)` — but the invalidation key includes `m_extent`. **Any pan changes `m_extent`, so every pan frame is a cache miss**, which means every pan frame pays for a synchronous QSG repaint *and* a `grabFramebuffer()` GPU-to-CPU readback at device-pixel resolution.

The comment at line 1332-1335 already calls this out: *"grabFramebuffer() is the single most expensive thing in paintEvent on large models — running it on every paint when it's drawing nothing was the silent killer of pan/zoom responsiveness even with the QSG path nominally off."* That fix avoided the readback when QSG owns nothing; it did **not** avoid the per-frame readback during pan when QSG owns everything.

This is the same problem the basemap path solved with the stale-buffer transform (§0.1) — and the same fix applies.

**Strategy:** apply the basemap pattern to the QSG framebuffer.

  1. Store the extent the cached framebuffer was rendered at (`m_qsgFrameCacheExtent`), analogous to `m_mapBufferExtent`.
  2. During pan/zoom, **do not** repaint the QSG widget or call `grabFramebuffer()`. Instead, compute the dst rect that maps `m_qsgFrameCacheExtent` into the current `m_extent` and issue one `p.drawImage(dstRect, m_qsgFrameCache)`. Identical formula to mapcanvas.cpp:1261-1279.
  3. Only call `repaint() + grabFramebuffer()` on:
     - `endPan()` / zoom-release (handled by the existing `refresh()` path),
     - selection change (a fresh `m_qsgFrameDirty = true` source),
     - layer/symbology change.
  4. Accept the trade-off: leading-edge background strip during drag, identical to the basemap. Matches QGIS behaviour and the user has explicitly approved this trade-off for the raster path.

**Why this is likely THE win:** the QSG renderer's `updatePaintNode` pan-only path is already matrix-only and fast. What's not fast is `grabFramebuffer()` reading an 8M+ pixel framebuffer back to CPU memory on every mouse-move event. The stale-buffer blit replaces that with a single `drawImage` of an already-resident `QImage`.

**Verification step:** with `SWMMVIS_RENDER_PERF=1`, also instrument the line that calls `m_qsgWidget->repaint()` and `grabFramebuffer()`. If the sum dominates `total_ms` during pan, this is confirmed as the bottleneck.

**Risks:**
  - The framebuffer was rendered at one device pixel ratio / canvas size; if the canvas resizes during pan (it shouldn't, but Hi-DPI screen moves can), we get a stretched blit for one frame. Resize already invalidates via `m_qsgCachedSize`; preserve that.
  - Selection changes must still trigger a fresh repaint+grab. The existing `m_qsgFrameDirty` flag handles this; just ensure pan does **not** set it.
  - During a drag that crosses a selection click, ordering matters: selection-set must invalidate *before* the next paint, or the user sees the old selection drag away with the basemap. Verify by clicking + dragging in one gesture.

**Reversibility:** local change in `mapcanvas.cpp` paintEvent and the cache state. Behind a preference toggle (`Rendering/QsgStaleBlitDuringPan`, default on) if there's any doubt during review.

**As implemented 2026-05-28** (no preference toggle — change is straightforward enough to ship unconditionally):

  - `MapCanvas::paintEvent` ([mapcanvas.cpp:1303-1391](../src/map/mapcanvas.cpp#L1303-L1391)) — dropped `extentChanged` from the regrab predicate. When the cache is non-null and the layer/size are unchanged, the paint composites `m_qsgFrameCache` via the same dst-rect math the basemap blit uses (`m_qsgCachedExtent` → `m_extent`). One `drawImage` call, no GPU readback.
  - `MapCanvas::refreshLayerItems` ([mapcanvas.cpp:1063-1078](../src/map/mapcanvas.cpp#L1063-L1078)) — when the 50 ms debounce timer fires and the viewport has drifted from the cached extent, sets `m_qsgFrameDirty = true`. That single line is the end-of-gesture trigger: `endPan()` and `zoomAroundCursor()` both call `refresh()` which routes through this timer, so the next paint after a gesture grabs a fresh frame. Basemap-tile-arrived refreshes don't drift the extent and correctly skip the regrab.
  - No new fields needed — the existing `m_qsgCachedExtent` already records "the extent the cached framebuffer was rendered at" and serves as the source for the dst-rect transform.

---

## Bugs discovered + fixed during H0 verification (2026-05-28)

When H0 first shipped, two visual regressions surfaced immediately. Both are documented here so they can be re-fixed quickly if anything regresses them. They are **not** new hypotheses; they were emergent bugs that H0 simply made more visible (because the QSG path now actually paints something instead of being permanently hidden behind a stale-but-correctly-aligned blit).

### Bug F1 — Anti-aliasing not reaching the QSG framebuffer ✅ fixed 2026-05-28

**Symptom:** with MSAA configured (`samples=6` in [main.cpp:83-87](../src/main.cpp#L83-L87) and `samples=4` in the `QQuickWidget`'s own `QSurfaceFormat`), conduit lines, node glyph outlines, and subcatchment edges in the QSG render still came out aliased on Retina. The CPU `SWMMLayerItem::paint()` path looked correctly antialiased; only the QSG output was jaggy.

**Root cause:** the `QQuickWidget` was sized at *logical* resolution (`m_qsgWidget->resize(size())` — the canvas's logical pixel size, e.g. 800×600). With `WA_DontShowOnScreen` set on the widget, the `setScreen()` call in `MapCanvas::showEvent` ([mapcanvas.cpp:1703-1725](../src/map/mapcanvas.cpp#L1703-L1725)) does **not** propagate the screen's device-pixel ratio to the widget's `effectiveDevicePixelRatio()`, so the FBO is created at 800×600 actual pixels. MSAA samples are taken correctly at that resolution — but `paintEvent` then does `p.drawImage(0, 0, m_qsgFrameCache)` into `m_frameBuffer`, which is at DPR=2 (1600×1200 actual, 800×600 logical to QPainter). With the grabbed image's `devicePixelRatio` defaulting to 1, drawImage interprets the 800×600 image as logical-space, draws it at 800×600 logical = 1600×1200 device — pixel-doubling every MSAA-resolved pixel into a 2×2 hard block. Every smoothing the MSAA produced gets eaten by the upscale.

This was specific to the fresh-grab path (`drawImage(0, 0, img)`). The stale-buffer transform path (`drawImage(dstRect, img)`) is unaffected because dstRect is an explicit destination size — the source image is scaled into it regardless of DPR.

**Fix** ([mapcanvas.cpp:1364-1395](../src/map/mapcanvas.cpp#L1364-L1395)) — in the regrab branch of `paintEvent`:

```cpp
const qreal qsgDpr = devicePixelRatioF();
const QSize wantedDev(qRound(width()  * qsgDpr),
                      qRound(height() * qsgDpr));
if (m_qsgWidget->size() != wantedDev)
    m_qsgWidget->resize(wantedDev);

m_qsgWidget->repaint();
m_qsgFrameCache = m_qsgWidget->grabFramebuffer();
m_qsgFrameCache.setDevicePixelRatio(qsgDpr);
```

Two parts must both be present:

1. **Resize the widget to device-pixel size** so MSAA samples are taken at 1:1 with on-screen pixels (not pre-upscale logical pixels).
2. **Tag the grabbed image with the same DPR** so QPainter's `drawImage(0, 0, ...)` interprets the image's logical extent correctly. Without the tag, the larger image overflows the canvas.

The QSG renderer's pixel-based sizing stays consistent because `sx_r = width()/extent.width()` doubles when the widget doubles, and `invView = 1/sx_r` halves; half-width-in-scene-units × scene-to-pixel scale ends up at the same on-screen pixel width.

**Regression watch:**
- If `MapCanvas::paintEvent` is ever refactored and someone "simplifies" `resize(wantedDev)` back to `resize(size())`, AA breaks. The comment block in the code (`Render at DEVICE-pixel resolution so MSAA samples are taken at 1:1...`) is there to prevent that.
- If `setDevicePixelRatio(qsgDpr)` is removed from the grab line, lines look right but everything else in the canvas shifts because the image overflows.
- This fix is independent of the global `QSurfaceFormat::setDefaultFormat` `samples` setting and the `QQuickWidget::setFormat` call in MapCanvas's constructor. **All three are required**: the format requests MSAA, the resize provides the resolution to take samples at, the DPR tag prevents pixel-doubling.

### Bug F2 — Conduits drawing on top of node glyphs (Z-order) ✅ fixed 2026-05-28

**Symptom:** at full extent on the West Whiteland model, conduit lines were drawn over the centres of junction circles, making nodes hard to see — opposite of standard GIS rendering where points sit on top of lines.

**Root cause:** the QSG child-node list in `SWMMLayerQSGRenderer::updatePaintNode` had the four node buckets appended **before** the line buckets:

```cpp
// Buggy order:
for (auto *n : {catchFill, catchSelFill, catchEdge, catchSelEdge,
                junctionsBase, outfallsBase, storageBase, dividersBase,
                gagesBase, lines, linesSel, nodesSel, gagesSel})
    root->appendChildNode(n);
```

QSG paints children in tree order: earlier siblings draw first (back), later siblings draw on top (front). The list above puts links *after* nodes, so links paint *over* nodes.

**Fix** ([swmmlayerqsgrenderer.cpp:568-587](../src/map/swmmlayerqsgrenderer.cpp#L568-L587)) — reordered to put lines below nodes, with selection overlays on top of everything:

```cpp
for (auto *n : {catchFill, catchSelFill, catchEdge, catchSelEdge,
                lines, linesSel,
                junctionsBase, outfallsBase, storageBase, dividersBase,
                gagesBase,
                nodesSel, gagesSel})
    root->appendChildNode(n);
```

The matching `firstChild() / nextSibling()` walk in the existing-root reuse branch ([swmmlayerqsgrenderer.cpp:572-587](../src/map/swmmlayerqsgrenderer.cpp#L572-L587)) was updated in lockstep — these two orders **must stay in sync**, otherwise variable bindings on reuse silently point at the wrong nodes (lines get the node geometry uploaded into them and vice versa, with no compile-time check).

**Regression watch:**
- If anyone adds a new child node, it has to be added in both places at the same index. The else-branch reads siblings positionally — there's no name-keyed lookup.
- If selection overlays are split into per-kind buckets (e.g. as part of the H2 fix), keep them after all base buckets in the order — selection always paints on top.
- If catchments are ever split into multiple fill / outline layers, keep them at the head of the list — subcatchments are the visual background.

---

### H1 — Zoom triggers full content rebuild (inside `updatePaintNode`)

**Where:** [swmmlayerqsgrenderer.cpp:356-360](../src/map/swmmlayerqsgrenderer.cpp#L356-L360).
**Current behaviour:** any change in extent width/height (i.e. any wheel tick) sets `m_contentDirty = true`. That re-tessellates all links into thick triangles, re-emits all node glyphs, re-emits subcatchment fills (triangles cached, but per-vertex emit still runs).
**Why this is expensive:** `appendThickSegment` runs 6 vertices × N segments × every zoom step. For West Whiteland (244k segments) that's ~1.5M float writes per zoom tick on the GUI thread.
**Why the code does it:** the precision anchor (`m_anchorX/Y`) is fixed at content-build time, and the thick-segment half-width is `1.0f * invView` — both look like they depend on zoom, but inspect carefully:
  - The anchor is a translation; it's absorbed by the `QSGTransformNode` matrix and does **not** need to change with zoom.
  - `invView` *does* change with zoom — line widths in scene units must scale to stay 1 px. But this can be done as a uniform / matrix scale on a unit-width geometry, not by re-tessellating.
**Strategy:** emit links once as unit-width triangle strips (or `GL_LINES` if 1 px is acceptable) and let the transform node carry zoom. Keep `m_contentDirty` set only on real geometry/symbology change.
**Verification step:** with the perf sampler on, hold a steady zoom-in drag and check whether `geom_build_ms` fires on every wheel tick. If it does, this is the bug.
**Risk:** line width on `QSGGeometry::DrawTriangles` doesn't behave like `GL_LINE_WIDTH` — the visual line width in pixels will change with zoom. If we want constant-pixel link width while keeping the triangle-strip approach, we need either a vertex shader with view-space offset (custom `QSGMaterial`), or accept that 1-px-wide lines look identical in `GL_LINES` mode and switch to it.

### H2 — Any selection click forces full rebuild

**Where:** [swmmlayerqsgrenderer.cpp:320-330](../src/map/swmmlayerqsgrenderer.cpp#L320-L330) and `§QSG-3` comment at line 417.
**Current behaviour:** `selectionChanged` sets both `m_selDirty` and `m_contentDirty`, because node colour is baked per-vertex into the base buckets. A single click that selects one junction re-tessellates everything.
**Strategy:** restore a true selection-only path by separating selection from base.
  - Option A: keep base buckets at base colour only; emit a separate `selectedNodes` vertex-coloured bucket with one glyph per selected node. Selection change updates only that bucket.
  - Option B: per-vertex `aSelected` attribute (1 byte) + a custom material that branches on it. More flexible but requires a custom `QSGMaterialShader`.
**Verification step:** with perf sampler on, click-select 1 node and observe `geom_build_ms`. If > 5 ms it's worth fixing.
**Risk:** the §QSG-3 comment notes that an earlier separate-overlay attempt had a bug where "geometry uploads silently didn't paint despite correct vertex/material/parent state" on macOS Metal. That bug needs root-causing before Option A is safe — likely related to material/node ordering or a missing `markDirty(DirtyGeometry|DirtyMaterial)` call. The vertex-coloured workaround dodges it; we should diagnose, not re-hit.

### H3 — Labels are still CPU-painted every frame

**Where:** [swmmlayeritem.cpp:835-918](../src/map/swmmlayeritem.cpp#L835-L918).
**Current behaviour:** node labels go through `QPainter::drawText` 3–5 times per label (halo offsets + fill). At full extent on a 42k-node model, this is the visible label set times 3–5 every frame — even when QSG owns the network geometry.
**Strategy (in increasing scope):**
  1. **LOD gate** — only draw labels when zoomed in past a threshold where they don't overlap (already partially done? confirm during audit).
  2. **Rasterise once, cache per text** — build a label atlas (`QImage` keyed by text content + font); blit each label as a `drawPixmap` call. One blit per label, no shaping per frame.
  3. **Move labels into the QSG path** — `QSGSimpleTextureNode` or a label-atlas texture sampled by per-quad UVs. Removes labels from the CPU paint event entirely.
**Verification step:** with the user's model, toggle labels off ("show node labels" preference) and re-measure. If pan/zoom suddenly feel fine, labels are the dominant cost.
**Risk:** label collision detection (the existing layout that hides overlapping labels) must still run when the visible set changes. Keep that logic; just don't re-rasterise the glyphs.

### H4 — Vector scene (`QGraphicsScene::render()`) repaints fully every frame

**Where:** [mapcanvas.cpp:1289-1302](../src/map/mapcanvas.cpp#L1289-L1302).

**Current behaviour:** between the basemap blit (Layer 1) and the QSG composite (Layer 2b), `m_scene->render(&p, target, source)` rasterises the whole `QGraphicsScene` into the canvas painter every paint. This is *not* the basemap path (that's separate, see §0.1) and *not* the SWMM network (that's QSG). It's everything else in the scene: identify markers, edit handles, profile overlays, rubber-band, hover, snapping indicators, scale bar items if any, vector GIS layer items, raster tile items that aren't part of `m_mapBuffer`.

**Why this matters during pan:** `QGraphicsScene::render()` walks the scene's BSP index, calls `paint()` on every visible item, and the painter has antialiasing on (per `OpenSWMMVisGraphicsView` render hints). For a scene with 10s–100s of overlay items it's small; for hundreds-to-thousands of vector-layer items (a loaded GIS shapefile) it's a real cost on every pan frame.

**Strategy (tiered):**
  1. **Quick check first.** Count `m_scene->items().count()` in the user's slow case. If < 100 and antialiased rendering takes < 2 ms, skip this — H0/H1/H3 will dominate.
  2. **If there are GIS overlay items:** split the scene into two render passes — a "static" pass (vector layers that don't change per-frame) cached to a `QImage` with stale-buffer transform (same pattern as basemap), and a "dynamic" pass (identify markers, rubber-band, hover) drawn live every frame.
  3. **Per-item invalidation.** Use `scene()->changed(QList<QRectF>)` to mark the static cache dirty only when an item in it actually moved/repainted.

**Verification step:** in `paintEvent`, wrap the `m_scene->render` call with `QElapsedTimer` and log per paint. Categorise items by type if the time is non-trivial.

**Risks:**
  - Animated/interactive overlays (rubber-band, hover ring) must stay in the live pass or they freeze. The split must be deliberate.
  - Cache invalidation on layer style change must include the static cache. Easy to forget.
  - Don't apply the stale-blit-during-pan trick to the dynamic pass — the user's hover/identify ring would lag the cursor.

### H5 — Other layers (mesh, GIS) double up

**Where:** [swmm2dmeshqsgrenderer.cpp](../src/map/swmm2dmeshqsgrenderer.cpp) and any vector/raster layer items in the QGraphicsScene.
**Current behaviour:** mesh QSG renderer runs independently; if the user has 2D results visible at the same time as the SWMM network, both QSG trees rebuild on their own triggers. Vector and raster layer items in the scene still paint via `QPainter`.
**Strategy:** only chase this if H1–H4 have been ruled out. Quantify first: how many non-SWMM items are in the scene during the user's slow case? If basemap is a single raster tile item, this isn't the issue.
**Verification step:** add a one-shot log of `scene()->items().count()` and a breakdown by type at the moment slowness is reported.

### H6 — Initial load time vs. interactive frame time

**Diagnosis split:** the user said "rendering is slow" but didn't separate "first frame after open" from "every pan after that". Earcut triangulation of 1.7k catchments and the first `appendThickSegment` pass over 121k links happen exactly once per model load — if that's what feels slow, the fix is "load in a worker" not "make rendering faster".
**Verification step:** ask the user to characterise — is the slowness on first open, on every interaction, or both?
**Strategy if first-open is the problem:** move geometry build off the GUI thread. The data structures it reads (`m_linkSceneFlat`, `m_catchScenePts`) are stable after load, so a worker can produce the QSGGeometry::Point2D vectors and hand them back; only the upload-to-GPU step needs to be on the render thread.

---

## 2. What is explicitly **out of scope** for this round

- **Any change to the basemap / raster `m_mapBuffer` path.** The QGIS-style stale-buffer transform at [mapcanvas.cpp:1237-1287](../src/map/mapcanvas.cpp#L1237-L1287) is correct and accepted as-is, including the trade-off of the leading-edge background strip during drag. Touching this is forbidden.
- Switching from QSG to `QRhiWidget` / native Metal — `RENDERING_5M_PLAN.md` Decision 2 still applies; defer until OpenGL hits a wall.
- Custom shaders / per-vertex selection attribute — only if H2 Option A turns out to be unreliable.
- Replacing the QGraphicsView+QSG hybrid wholesale — too disruptive given how many overlays depend on QGraphicsView.
- Any change to the spatial index — the existing cull is doing its job (verified by the wide-margin pan path).

---

## 3. Decision points needing user input before code changes

1. **What model triggers the slowness?** West Whiteland (42k/121k/1.7k) or something larger? Baseline numbers depend on which model we measure.
2. **First-open slow, interaction slow, or both?** (Determines whether to chase H6 vs. H1–H5.)
3. **Are labels on during the slow case?** (If yes, H3 jumps to first.)
4. **Is the 2D mesh result layer visible during the slow case?** (If yes, H5 moves up.)
5. **Acceptable visual regression for H1 fix?** Switching links to `GL_LINES` gives constant 1-px width regardless of zoom — cleaner than triangle strips that get pixel-thin when zoomed out, but it's a look change.

---

## 4. Proposed sequence (pending answers above)

Assuming default answers (single user model, both first-open and interaction feel slow, labels usually on, mesh not always on):

1. **Measure first** — `SWMMVIS_RENDER_PERF=1`, fill in §6 table for the user's actual model. **Do not skip this.** Also time the `m_qsgWidget->repaint() + grabFramebuffer()` block separately ([mapcanvas.cpp:1360-1361](../src/map/mapcanvas.cpp#L1360-L1361)) — this is the single most-suspect line.
2. **H0** — stale-buffer transform for the QSG framebuffer cache during pan. Expected to be the biggest single win, mirrors the proven basemap pattern, low blast radius.
3. **H1 then H2** if zoom and selection are still hot frames after H0.
4. **H3** if the steady-state pan is still slow after H0–H2.
5. **H6** if first-open is independently a complaint.
6. **H4/H5** only if the above don't get us under target.

Target frame budget for "smooth" pan/zoom is 16 ms total. Anything under 8 ms is overkill for this domain; spend the savings on label quality and selection responsiveness instead.

---

## 5. Risks and reversibility

- All proposed changes are local to `swmmlayerqsgrenderer.cpp` and `swmmlayeritem.cpp` (label changes). No data-model changes. Reversible by `git revert` per slice.
- H2 Option A re-enters territory where a prior attempt failed silently on macOS Metal. If we go there, first **reproduce** the silent-upload bug on current Qt (6.10) — it may have been a Qt 6.6/6.7 issue already fixed upstream.
- H1's `GL_LINES` switch is a visible change; ship behind a preference for one release if there's any doubt.

---

## 6. Baseline measurement table (to fill in before implementation)

Model: _________________ (nodes / links / catchments / gages)
Qt version: _____ · macOS version: _____ · GPU: _____

| Action | `geom_build_ms` | `total_ms` | Notes |
|---|---|---|---|
| First paint after load | | | |
| Pan (mouse drag, no zoom) | | | of which `grabFramebuffer()` = ___ ms |
| Zoom in (wheel tick) | | | |
| Zoom out (wheel tick) | | | |
| Click-select 1 node | | | |
| Rubber-band select ~1k | | | |
| Toggle labels off, then pan | | | |
| Hide 2D mesh layer, then pan | | | |
| `m_scene->render()` time (Layer 2) during pan | | | from `mapcanvas.cpp:1301` |

Once filled in, the hypothesis ranking above gets reordered to match reality.

---

## 7. Open questions for the reviewer

- Is the existing `SWMMVIS_RENDER_PERF` sampler enough, or should we add per-bucket timing (catch / link / node / gage / labels separately) before doing anything else?
- Is there appetite to remove the legacy `swmmlayerglrenderer.cpp` once H1–H3 land, or keep it as a fallback?
- Should label rendering be its own QSG node-list (cleanest), or stay on the CPU `SWMMLayerItem::paint()` overlay with caching (cheapest change)?
