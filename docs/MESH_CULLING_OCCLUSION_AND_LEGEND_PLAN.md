# Mesh Culling, Occlusion & Configurable Legend Plan

> Created: **2026-06-01** · Status: **🔧 In progress — LOD overview landed**
>
> Status legend: ✅ shipped & verified · 🔧 in progress · ⏳ not started
>
> **Correction (2026-06-01):** the active mesh render path on the main canvas
> is **`SWMM2DMeshGraphicsItem::paint()`** (QPainter `QGraphicsItem` in
> `src/layers/swmm2dmeshlayer.cpp`), **not** `SWMM2DMeshQSGRenderer` — the QSG
> renderer is currently dormant (registered as a QML type only). The 5M-cell
> cost lives in that QPainter loop (`drawConvexPolygon` per triangle). Culling /
> LOD work therefore targets the QPainter path until the QSG renderer is made
> the live path. Sections below that say "QSG" should be read against the
> active QPainter path for now.
>
> **Implementation log**
> - 🔧 **Incremental vertex-elevation edits:** `applyMeshVertexZ()` called the
>   full `rebuildSceneGeometry()` (all triangles + edge-dedup set + grids +
>   overview + size-sort) for a single z change — multi-second on a large mesh,
>   and once *per selected vertex*. A z edit moves nothing in XY, so it now
>   updates only the incident triangles' z (via the vertex→triangle adjacency),
>   the node dot, and the elevation range — O(incident). Grids, bboxes,
>   size-sort and overview cells are untouched (XY-invariant); incident edge
>   slope and far-zoom overview colour are left to the next full rebuild. Falls
>   back to a full rebuild if the caches aren't 1:1.
> - 🔧 **Fix: mesh vanished at some zooms.** The size-cull overview floor was
>   all-or-nothing (drawn only when *zero* large cells qualified), so a view
>   mixing large and small cells left the small-cell areas empty; and the
>   overview was centroid-binned, so large triangles left holes. Fixed: the
>   overview is now bbox-coverage-binned (gap-free) and always drawn as a
>   continuous base, with large real cells composited on top. *Minor known
>   artifact: large-cell areas blend over the base, so they read slightly more
>   opaque; refine with a composition mode later if needed.*
> - 🔧 **E3-analog — Cached scene buffer (the pan fix):** the active canvas
>   re-ran `QGraphicsScene::render()` (which paints the whole mesh) on *every*
>   `paintEvent`, i.e. every pan/zoom frame — only the basemap was cached. Added
>   `MapCanvas::renderSceneBuffer()` + `m_sceneBuffer`/`m_sceneBufferExtent`/
>   `m_sceneBufferDirty`: the vector scene is now rasterised once per *settled*
>   view and blitted with the basemap's stale-buffer transform during gestures.
>   Re-renders only when stale and no gesture is active; a `Scene`-channel
>   invalidation (selection/symbology/geometry) flips the dirty flag. This makes
>   pan/zoom an O(1) blit regardless of triangle count — the direct fix for
>   "slow to pan." *Exposed-rect note:* the live render goes through
>   `scene->render(target, source)` with `ItemUsesExtendedStyleOption`, so the
>   mesh item's grid cull receives a tight viewport `exposedRect` and limits the
>   render to visible triangles when zoomed in.
>   - **Scope correction:** the cache is used **only during an active pan/zoom
>     gesture** (`m_isPanning || m_isZooming`). When idle the scene renders
>     live, so vertex/edge selection and the mesh profile tool stay immediately
>     responsive — an earlier always-cached version left selection stale until
>     the next tool switch, because selection repaints (`onLayerRepaintRequested`
>     → `refresh()`) don't mark the buffer dirty. Gesture-scoping avoids
>     depending on every dirty path being wired.
> - 🔧 **C3+C4 — Adaptive size-based LOD culling:** at low zoom (native
>   triangle < ~2 px) `SWMM2DMeshGraphicsItem::paint()` no longer replaces the
>   mesh with a uniform aggregation grid (which read as an artificial grid).
>   Instead it draws the *real* cells whose projected area is ≥ ~16 px², via a
>   size-sorted index (`m_trisBySizeDesc`, largest first, stop when below
>   threshold → O(kept)), so large cells render faithfully and tiny sub-pixel
>   cells are culled. The coarse overview (`rebuildOverview()`, ~15k cells,
>   mean elevation + neighbour-averaged corner heights for hillshade) is now a
>   *fallback floor*, drawn only when no real cell is large enough (e.g. a
>   uniformly fine mesh zoomed right out) so the mesh never vanishes. Wireframe
>   / node passes are skipped at LOD zoom; contour passes run over the overview.
>   Built only for meshes ≥ 200k triangles. *Visible only on meshes with
>   varying cell sizes — use `gen_synthetic_mesh.py --graded`. Needs a macOS
>   build; tune `kLodTriPx` / `kLodKeepPx2` if the result is too sparse or too
>   dense.*
>
> Scope: the **2D mesh-cell** render path (`SWMM2DMeshLayer` →
> `SWMM2DMeshQSGRenderer`), targeting interactive rendering of **5M+ triangles**.
> This is a *different subsystem* from the SWMM network plan in
> `RENDERING_5M_PLAN.md` (that one covers nodes/links/subcatchments). The two
> share the QSG pivot and the spatial-index philosophy but touch different
> renderers — keep them in sync, don't merge them.

---

## 0. Read this first — alignment with the legacy

Per `CLAUDE.md §4.01`, **alignment with the legacy is the primal objective**.
This plan is deliberately incremental on top of what already exists. Before
writing any code in a slice, re-read the corresponding legacy file and preserve
its contracts. The pieces already in the tree that this plan builds on:

| Concern | Already exists | File |
|---|---|---|
| Extent / frustum cull | Uniform CSR spatial grid, O(visible) query | `include/layers/meshspatialgrid.h`, `src/layers/meshspatialgrid.cpp` |
| Frustum margins | `cullMargin`, mesh-bbox early-out | `src/map/swmm2dmeshqsgrenderer.cpp` (~L355–402) |
| Single draw call / pass | `QSGGeometryNode` per pass, CPU color compute | `src/map/swmm2dmeshqsgrenderer.cpp` |
| Immutable topology | "vertex / triangle counts are CONSTANT" | `include/layers/swmm2dmeshlayer.h` (~L64) |
| Scene caches | `SceneTri/SceneEdge/SceneNode`, `m_triGrid/m_edgeGrid` | `include/layers/swmm2dmeshlayer.h` (~L362–406) |
| Legend chrome model | fonts, sizes, spacing, swatch, frame, bg, anchor, opacity | `include/render/legendoverlaystyle.h` |
| Per-item legend toggle | `ItemOverride{visible,userLabel}` + checkbox column | `legendoverlaystyle.h`, `src/ui/models/legendlayertreemodel.cpp` |
| Legend drag (not resize) | `mousePress/Move/Release`, free placement | `src/ui/widgets/legendoverlay.cpp` (~L457–490) |

**The headline:** topology is immutable after load, yet the renderer currently
rebuilds and re-uploads expanded (non-shared) triangle vertices on every
`m_contentDirty` frame. That single fact is the largest lever in this plan.

---

## 1. Goal & budget

At 5M cells the mesh must:

- Open and build once with a brief, bounded upfront cost (no permanent modal).
- Pan / zoom at **≥ 30 fps** (≤ 33 ms/frame) at every zoom level.
- Re-color on a timestep change in **≤ 16 ms** without re-uploading positions.
- Hold a **flat, predictable memory ceiling** regardless of pan position.

### 1.1 Why the current path will not reach this

The fill pass expands each triangle to **3 independent vertices** and
`push_back`s them every rebuild (`swmm2dmeshqsgrenderer.cpp` ~L593–658). With
`QSGGeometry::ColoredPoint2D` = 2×float + 4×uint8 = **12 bytes/vertex**:

| Layout | Per triangle | 5M triangles | Re-uploaded when |
|---|---|---|---|
| Current — expanded, non-indexed | 3 × 12 = 36 B | **180 MB** | every dirty frame |
| + CPU `SceneTri` (48 B pts + 16 B z) | 64 B | **320 MB resident** | — |
| Target — indexed, shared verts | ~0.5 vert/tri amortized | **~30 MB pos + 60 MB idx** | once (pos) / on recolor (color only) |

So ~500 MB+ of CPU/GPU churn per frame today. Naive scaling makes pan unusable.
The fix is structural (Phase M1), not a tuning pass.

---

## 2. Strategy catalogue

Two families. **Culling** removes triangles before they reach the GPU.
**Occlusion** removes triangles that something else hides. Both feed the same
goal: fewer triangles submitted per frame.

### 2.1 Culling (filter before submit)

**C1 — View-frustum / extent culling.** *(exists — upgrade)*
Already done per-triangle via `m_triGrid.query(cullRect)`. The query result
itself grows with the visible set; at high zoom that is fine, at the boundary
between zoom levels it is not. Upgrade to **chunk-level** culling (C2) so the
per-frame query cost is bounded by chunk count, not triangle count.

**C2 — Chunk-level culling.** *(new)*
Partition triangles into contiguous **render chunks** aligned to
`MeshSpatialGrid` cells. Each chunk owns a contiguous index range and a bbox.
Per frame: test chunk bboxes against the cull rect; fully-inside chunks emit
their whole index range with **zero per-triangle work**; only boundary chunks
fall back to per-triangle culling. This turns the hot path from O(visibleTris)
into O(chunks + boundaryTris).

**C3 — Sub-pixel / small-feature culling (screen-space LOD).** *(new)*
When a chunk's projected screen area drops below a threshold (≈ a few px²), the
chunk is not worth drawing as vector triangles. At full extent, 5M cells over a
~1000-px viewport means the vast majority of cells are sub-pixel. Replace those
chunks with their LOD representation (M3) or, at the extreme, a single colored
quad per chunk (its mean value). This is the difference between submitting 5M
and ~5k triangles at full extent.

**C4 — Hierarchical LOD pyramid (mesh decimation).** *(new)*
Build coarse mesh levels once at load (quadtree / edge-collapse decimation,
or value-binned aggregation per grid cell). Choose level by zoom so the
submitted triangle count stays roughly constant (~50–200k) across zoom. This is
the primary "5M without a sweat" lever. Keep level 0 = the exact legacy mesh so
fully-zoomed-in views are byte-for-byte the current output.

**C5 — Raster bake for far zoom.** *(new, optional but high-value)*
At the coarsest zooms, bake the active scalar field (elevation / result) into
mipmapped texture tiles and draw the mesh as a handful of textured quads.
1 textured quad vs millions of triangles. Switch vector↔raster at a zoom
threshold with a short cross-fade. Reuses the existing raster renderer
machinery (`palettedrasterrenderer`, `RasterColorRamp`, `colorFromRamp`).

### 2.2 Occlusion (filter what is hidden)

In a top-down 2D mesh, "occlusion" is overdraw and layer stacking — not 3D
visibility. Strategies, cheapest first:

**O1 — Inter-layer coverage-mask culling.** *(new)*
If an **opaque** layer sits above the mesh and fully covers part of the extent
(another mesh, a filled basemap, an opaque result band), the covered mesh
chunks need not be drawn. Build a coverage mask from opaque layers above the
mesh and cull chunks fully inside it. The mask plumbing already exists —
`src/render/maskclipresolver.cpp`, `include/render/maskspec.h` — so this is
mostly: compute opaque coverage → intersect with chunk bboxes → drop chunks.

**O2 — Opaque-pass depth + early-Z.** *(new)*
The fill pass, when opaque (alpha == 255), can be drawn with a depth value and
front-to-back ordering so the GPU's early-Z rejects overdrawn fragments before
shading. Cuts fragment cost where passes overlap (fill under edges/contours).
Requires a custom `QSGMaterial` with depth test enabled; **does not apply** to
the semi-transparent default fill (`kFillAlpha = 160`), so gate it on an
opaque-fill mode. Lower priority than O1.

**O3 — GPU occlusion queries / Hi-Z chunk culling.** *(advanced, optional)*
Per-chunk hardware occlusion queries: skip chunks whose bbox is provably hidden
by already-drawn opaque geometry. Only pays off with multiple overlapping
opaque layers (e.g. stacked meshes). Defer until O1/O2 are in and profiling
shows overlapping-opaque cost. Carries latency/readback complexity — treat as
research, not committed scope.

**O4 — Backface culling.** *(note only — not applicable today)*
All mesh faces point at the camera in the 2D top-down view, so there is nothing
to cull. Record it here only because it becomes relevant if the view ever tilts
to 2.5D; until then, leave `GL_CULL_FACE` off to avoid winding-order surprises.

### 2.3 Pipeline enablers (make the above affordable)

**E1 — Indexed, static-topology geometry.** *(new — the keystone)*
Upload shared-vertex positions + an index buffer **once**, since topology is
immutable. Store color as a **separate** per-vertex attribute updated only on
timestep / symbology change. Decouples position upload (once) from recolor
(cheap). Foundation for every chunk/LOD slice below.

**E2 — Quantized vertex positions.** *(new)*
Positions as 16-bit normalized offsets from the existing scene anchor
(`m_anchorX/Y`) halve position bandwidth and resident size. The anchor already
exists for float precision — extend it to a quantization origin + scale.

**E3 — Persistent buffers + dirty-subrange upload.** *(new)*
With E1, a pan changes *which* index ranges are drawn, not the buffer contents —
so pans become zero-upload. Only LOD-level switches or recolors touch GPU
memory, and recolor touches the color buffer only.

**E4 — Threaded build / double-buffer.** *(new)*
Move LOD construction and per-vertex color computation to a worker thread,
double-buffering the color/index arrays. `MeshSpatialGrid::query()` stays
render-thread-only per its documented threading contract — respect it; only the
build-side work moves off-thread.

---

## 3. Phase plan

Sequential. Each slice states **success criteria** and an explicit
**verification** step (per `CLAUDE.md §4` goal-driven execution). All test
artifacts (synthetic meshes, timing CSVs, screenshots) are written to
`docs/test_output/` so they are reviewable, **never to temp dirs**
(`CLAUDE.md §4 Transparent File IO`).

### Phase M0 — Instrumentation & a real 5M target *(prerequisite)*

- **M0.1** Add per-pass timers (fill/edge/node/contour) + counters (tris
  submitted, verts uploaded, bytes uploaded, chunks visited) to
  `SWMM2DMeshQSGRenderer::updatePaintNode`, behind a runtime flag.
- **M0.2** Generate a synthetic ~5M-cell mesh and write it to
  `docs/test_output/synthetic_5M_mesh.*` for repeatable benchmarking.
- **M0.3** Record a baseline timing table into `docs/test_output/` at three
  zooms (full extent / mid / fully-zoomed-in).
- ✅ when: a CSV baseline exists and counters print sane numbers on the legacy
  path. *Verify:* numbers match a hand count on a tiny known mesh.

### Phase M1 — Indexed static geometry (E1, E2) *(keystone)*

- **M1.1** Add a shared-vertex + index representation alongside `SceneTri`
  (don't delete `SceneTri` yet — keep the legacy path switchable for A/B).
- **M1.2** Build position + index buffers once on `rebuildSceneGeometry`;
  upload via `QSGGeometry` index API; pans reuse them.
- **M1.3** Split color into its own attribute / node; recolor path touches
  color only.
- **M1.4** (optional) Quantize positions to int16 about the anchor.
- ✅ when: visual parity with legacy at all zooms; recolor ≤ 16 ms; position
  buffer uploaded exactly once per load. *Verify:* screenshot diff vs legacy
  (pixel-identical at level 0); upload counter from M0 shows one position
  upload; timing CSV in `docs/test_output/`.

### Phase M2 — Chunked culling (C1→C2, E3)

- **M2.1** Assign each triangle a chunk id from its `MeshSpatialGrid` cell;
  store per-chunk contiguous index ranges + bbox.
- **M2.2** Replace per-triangle cull with chunk-bbox test; emit whole ranges
  for inside chunks, per-triangle only for boundary chunks.
- **M2.3** Persistent buffers — pan = re-pick index ranges, zero upload (E3).
- ✅ when: pan at full extent ≥ 30 fps; per-frame CPU cull cost scales with
  chunk count, not triangle count. *Verify:* counters show boundary-only
  per-triangle work; fps captured in `docs/test_output/`.

### Phase M3 — LOD pyramid + sub-pixel culling (C3, C4, E4)

- **M3.1** Build decimated levels once at load (level 0 = exact legacy mesh).
- **M3.2** Pick level by projected scale; per-chunk sub-pixel test drops or
  coarsens chunks below threshold.
- **M3.3** Move LOD build + color compute to a worker thread, double-buffered
  (E4); keep `query()` on the render thread.
- ✅ when: submitted triangle count stays ~constant across zoom; full-extent
  frame ≤ 33 ms at 5M. *Verify:* triangle-count-vs-zoom plot flat-ish;
  level-0 zoomed-in output still pixel-identical to legacy.

### Phase M4 — Raster bake for far zoom (C5)

- **M4.1** Bake active field to mipmapped tiles via the existing raster ramp.
- **M4.2** Vector↔raster switch at a zoom threshold with a short cross-fade.
- ✅ when: coarsest zoom draws ≤ a few hundred triangles/quads at ≥ 60 fps with
  no visible popping. *Verify:* side-by-side raster-vs-vector screenshot at the
  switch zoom shows acceptable match.

### Phase M5 — Inter-layer occlusion (O1, then O2)

- **M5.1** Compute opaque coverage from layers above the mesh; cull fully
  covered chunks (reuse `maskclipresolver` / `MaskSpec`).
- **M5.2** (gated) Opaque-fill depth + front-to-back early-Z for overlapping
  opaque passes.
- *(O3 GPU occlusion queries — research only, out of committed scope.)*
- ✅ when: stacking an opaque layer over the mesh measurably drops submitted
  mesh triangles. *Verify:* counter delta with/without the covering layer.

---

## 4. Configurable legend (GIS-style)

The user wants: **resize**, **change fonts**, and **selectively show/hide
layers in the legend without hiding them on the map**. Most of the model already
exists — this is largely UX wiring plus one real gap (resize).

### 4.1 What already exists (do not rebuild)

- **Fonts / sizing / chrome:** `LegendOverlayStyle` already exposes
  `titleFont`, `itemFont`, `layerHeaderFont` (+ colors), `rowSpacing`,
  `swatchSize`, `padding`, `opacity`, `showFrame`/`frameColor`/`frameWidth`/
  `cornerRadius`, `backgroundMode`/colors, and `Anchor`.
- **Per-item show/hide independent of map visibility:**
  `ItemOverride{visible, userLabel}` is exactly "turn a row off in the legend
  without turning the layer off in the view." The tree model already renders a
  **checkbox column** wired to `setItemVisible` (`legendlayertreemodel.cpp`
  ~L309–351, flags ~L419).
- **Placement:** the overlay is already **draggable** to free positions
  (`legendoverlay.cpp` ~L457–490).

### 4.2 Gaps to close

- **L1 — Free resize of the legend box.** *(real gap)* The overlay currently
  **auto-sizes** (`resize(contentW + 2*padding, contentH)`, ~L269) and has no
  resize grips. Add corner/edge resize handles. Decide the resize semantics
  (pick one, see §5 open question Q3): (a) box scales font + swatch together
  ("zoom" the legend), or (b) box defines a frame and content reflows /
  multi-columns. GIS tools usually offer both; start with (a) as the simpler,
  legacy-aligned option, then add multi-column reflow.
- **L2 — Surface fonts/sizes in the properties dialog with live preview.**
  `legendpropertiesdialog.cpp` (89 lines) is currently a thin tree. Add tabs
  (General / Frame / Background) editing the existing `LegendOverlayStyle`
  properties via `QFontComboBox` + size spinners; every setter already emits a
  change signal that triggers repaint, so wiring is mechanical.
- **L3 — Global font scale.** One master multiplier applied to all three fonts
  + swatch, for quick "make the whole legend bigger" — pairs naturally with the
  L1 resize handle.
- **L4 — Multi-column / wrap** for layers with many classes, so a tall legend
  doesn't overflow the canvas.
- **L5 — Persistence.** Save legend chrome + per-item overrides + box size/pos
  with the project (extend `dialoglayoutpersistence` / the existing style
  serialization).

### 4.3 Legend slices

- **LG.1** Properties dialog: General/Frame/Background tabs bound to
  `LegendOverlayStyle` with live preview. ✅ when font/size/color edits update
  the on-canvas legend immediately. *Verify:* change each property, confirm
  repaint + round-trip through save/load.
- **LG.2** Resize handles on `LegendOverlay` (semantics per Q3) + global font
  scale (L3). ✅ when dragging a grip rescales as specified and the box stays
  inside the canvas.
- **LG.3** Confirm/finish the per-item legend-visibility checkbox end-to-end
  (model already supports it) and add a "show in legend" affordance that is
  visibly distinct from map-layer visibility. ✅ when toggling a row hides it in
  the legend while the feature still draws on the map.
- **LG.4** Multi-column reflow (L4) + persistence (L5).

---

## 5. Risks & open questions

- **Q1 — LOD decimation method.** Edge-collapse (geometry-preserving) vs
  value-binned aggregation per grid cell (cheaper, field-preserving). For a
  scalar-field mesh, value-binning is likely the better fit and reuses the
  spatial grid. *Decision needed before M3.*
- **Q2 — Raster-bake fidelity (M4/C5).** A baked texture cannot show crisp
  per-edge slope styling; acceptable only at far zoom where edges are sub-pixel
  anyway. Need a zoom threshold that hides the transition.
- **Q3 — Legend resize semantics (L1).** Scale-content vs reflow-frame — affects
  the handle behavior and persistence schema. *Needs a product call.*
- **Q4 — Memory ceiling at 5M.** Even indexed + quantized, level-0 positions +
  index + per-vertex color are non-trivial. Confirm the target hardware budget;
  if tight, level 0 may need to stream by chunk rather than stay fully resident.
- **Q5 — Selection / hover at 5M.** Picking already uses
  `pickCellsInRect/Polygon` over the grid; confirm it still hits ≤ 16 ms at 5M,
  and that chunked/LOD geometry maps back to true cell ids for identify.
- **Q6 — Edge / node / contour passes.** This plan focuses on the fill pass.
  Edges (`appendThickSeg`, ~6 verts/edge) and nodes scale the same way and need
  the same indexed + chunked treatment; fold them in per phase, don't leave
  them on the legacy expand-every-frame path.

---

## 6. Suggested order of attack (TL;DR)

1. **M0** instrument + build a real 5M test mesh (can't tune what you can't measure).
2. **M1** indexed static geometry — the keystone; upload positions once, recolor cheap.
3. **M2** chunked culling + persistent buffers — pans become zero-upload.
4. **M3** LOD pyramid + sub-pixel culling — flat triangle budget across zoom.
5. **M4** raster bake for far zoom — trivial cost at full extent.
6. **M5** inter-layer occlusion — drop chunks hidden by opaque layers above.
7. **Legend LG.1–LG.4** in parallel (independent subsystem): dialog wiring →
   resize handles + font scale → per-item visibility polish → reflow + persistence.

Each step is independently shippable and visually parity-checked against the
legacy path before the next begins.
