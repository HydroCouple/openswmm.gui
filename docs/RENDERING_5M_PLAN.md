# Scaling SWMM Network Rendering to 5M+ Objects

> Last updated: **2026-04-27** · Status: **Phase A complete ✅ · Phase B (OpenGL) reset → Phase B.RHI (Qt Quick Scene Graph) starting**
>
> Status legend: ✅ shipped & verified · 🔧 in progress · ⏳ not started

This document is the working plan for scaling `SWMMModelLayer` rendering from
~166k objects (West Whiteland: 42,809 nodes + 121,902 links + 1,700
subcatchments + 1 gage) to **5M+ objects** at interactive frame rates.

Another agent picking this up: **scan the Progress Log at the bottom first.**
Each phase section captures the design intent and the acceptance criteria; the
log says where we actually are. The plan is sequential — Phase B depends on
Phase A's data layout; Phase C polish depends on the GL pipeline from B.

---

## Goal

5M-element SWMM models must:

- Load and render the full network on initial open (no permanent "loading"
  modal — accept a brief upfront cost).
- Pan and zoom at ≥ 30 fps once the layer is built.
- Selection / hover / identify must respond within ~16 ms, regardless of
  selection size.

For comparison, the current baseline (West Whiteland, 121,902 links) is
**~400 ms per paint** at full extent on M1, dominated by `QPainter::drawLines`
over ~244k segments. Naive linear scaling would give ~16 s per paint at 5M —
unusable. The architecture has to change, not just be tuned.

## Baseline measurements (West Whiteland, M1)

Captured 2026-04-26 with timing instrumentation at three points:
`SWMMModelLayer::setSelectedElementNames`, `OpenSWMMVisMapToolSelect::selectInRect`,
and `SWMMLayerItem::paint`.

| Operation | Time | Notes |
|---|---|---|
| `selectInRect` (1k–3.6k hits) | **0 ms** | KD-tree + bbox cache already optimal |
| Pre-batching `setSelectedElementNames(3619)` | **1978 ms** | Dominated by ObjectBrowserPanel tree-sync (3619 individual `sm->select()` + `m_view->expand()` calls) |
| Post-batching `setSelectedElementNames(3619)` | _to be re-measured_ | Single `QItemSelection` apply + unique-parent expand |
| `SWMMLayerItem::paint` (full extent, 121k links) | **395–432 ms** | Per-frame, dominated by `painter->drawLines(244k segments)` |

The selection-side bottlenecks are essentially solved (see Slice 1 in the
Progress Log). What remains is the paint cost itself, which is the thing that
prevents scaling above ~500k–1M objects on the current architecture.

## Architecture decisions

### Decision 1 — Stay inside `QGraphicsView`, swap viewport to `QOpenGLWidget`, drop into native GL inside `SWMMLayerItem::paint()`.

We already have the right architecture for this on the SWMM side: a single
batched `SWMMLayerItem` (Slice R Phase 3, no per-element `QGraphicsItem`s).
That item's `paint()` is the only place that has to switch backends.
Everything else (basemaps, GIS layers, selection overlay, identify markers,
edit handles, rubber-band, hover) keeps its current `QPainter` path.

The viewport must be a `QOpenGLWidget` for `painter->beginNativePainting()`
to work, but every other layer item benefits from the GL paint engine
without touching its code.

### Decision 2 — Defer `QRhi` until OpenGL hits a wall.

`QRhi` (Qt 6.10 has it as `QRhiWidget`) translates to native Metal on Apple
Silicon — long-term that's the right answer. But:

- OpenGL 4.1 on M1 (via Apple's GL→Metal shim) is **good enough** for
  `glDrawArrays(GL_LINES, …)` over a packed VBO. The translation cost on
  this workload is small.
- `QRhi` integration with `painter->beginNativePainting()` is less
  battle-tested than GL.
- The `SWMMLayerItem::paint()` interior is contained enough that swapping
  the backend later is a 1–2 day port, not a rewrite.

So: GL first, port to QRhi when Apple actually deprecates OpenGL hard or
when cross-platform native (Metal/Vulkan) perf becomes a measurable
requirement.

### Decision 3 — Spatial index drives both the cull AND the GPU upload subrange.

Whatever index we build (uniform grid first; R-tree if profiling forces it)
must answer "give me the SoA indices of links whose scene-space bbox
intersects this rect." Both the CPU paint loop (Phase A) and the GL draw
call (Phase B) consume that result. The index is the contract between
phases.

### Decision 4 — Selection / hidden state moves off the QString hot path.

`paint()` currently calls `selectedSet.contains(name)` and
`hidden.contains(name)` per link — two QString hashes per link per paint.
At 5M that's 10M hashes per frame. Replace both with `std::vector<uint8_t>`
indexed by SoA position; populate at selection-set time / hidden-set time.

---

## Phase plan

### Phase A — Foundation (CPU-side wins; prerequisites for B)

**Goal:** make the existing CPU paint as fast as it can be, build the data
layout the GPU phase needs, and validate the cull architecture against the
West Whiteland baseline.

After Phase A ships, the same code should comfortably handle 500k–1M
objects on CPU. Past that, we hit GL.

#### A.1 — ✅ Spatial index over `m_linkSceneBBoxes` (shipped 2026-04-27)

Add a uniform grid (~50 LoC, no boost dependency) keyed by the existing
scene-space bboxes computed in `rebuildSceneCoords()`. The grid build is
O(N) and runs once per geometry/CRS change.

Paint replaces

```cpp
for (int i = 0; i < m_links.size(); ++i) {
    if (!exposed.intersects(lboxes[i])) continue;
    // … emit segments …
}
```

with

```cpp
const QVector<int> visible = m_layer->m_linkGrid.query(exposed);
for (int i : visible) {
    // … emit segments …
}
```

**Files:** [include/layers/swmmmodellayer.h](../include/layers/swmmmodellayer.h)
(grid struct), [src/layers/swmmmodellayer.cpp](../src/layers/swmmmodellayer.cpp)
(build at end of `rebuildSceneCoords`), [src/map/swmmlayeritem.cpp](../src/map/swmmlayeritem.cpp)
(paint loop).

**Acceptance:** at zoomed-in views (visible link count ~1–5% of total),
paint drops from ~400 ms to <50 ms on West Whiteland. At full extent the
time is unchanged (everything is visible).

#### A.2 — ✅ Selection / hidden state as `std::vector<uint8_t>` (shipped 2026-04-27)

Add `m_selectedFlag` and `m_hiddenFlag` parallel to the SoAs. Populated in
`setSelectedElementNames` / `setObjectVisible*` by translating names →
SoA indices via the existing `m_objectLocation` hash.

Paint replaces:

```cpp
const bool sel = selectedSet.contains(l.name);  // QString hash
if (hidden.contains(l.name)) continue;          // QString hash
```

with:

```cpp
if (m_layer->m_hiddenFlag[i]) continue;
const bool sel = m_layer->m_selectedFlag[i];
```

**Files:** [include/layers/swmmmodellayer.h](../include/layers/swmmmodellayer.h)
(flag arrays), [src/layers/swmmmodellayer.cpp](../src/layers/swmmmodellayer.cpp)
(set + maintain), [src/map/swmmlayeritem.cpp](../src/map/swmmlayeritem.cpp)
(paint).

**Acceptance:** per-paint hash work goes from `2 × N` QString hashes to zero.
On West Whiteland (244k hashes per paint), expect ~10–30 ms savings.
QString-keyed methods (`selectedElementNames` getter, `isObjectVisible`)
keep working — the flag arrays are the fast path, the QStringList /
QSet remains the canonical state for now.

#### A.3 — ✅ Pack link scene-coords into flat arrays (shipped 2026-04-27)

Replace `QVector<QVector<QPointF>> m_linkScenePts` with:

```cpp
QVector<float> m_linkSceneFlat;     // 2 floats per vertex (x, y)
QVector<int>   m_linkSceneOffset;   // [link i] -> first-vertex index in m_linkSceneFlat
QVector<int>   m_linkSceneCount;    // [link i] -> vertex count
```

Why floats not doubles: GL VBOs want floats, and link coords easily fit in
single precision once translated to scene space. Why flat: 121k little
heap allocations is catastrophic for cache locality at 5M.

Paint reads from the flat array; the segment loop becomes a tight pointer
walk. This is the layout the VBO upload in Phase B will `memcpy` directly.

**Files:** same three as A.1.

**Acceptance:** `rebuildSceneCoords` runs in similar time to today (it's
already O(N)); paint loop maintains correctness. Memory for the cache
drops noticeably (one allocation instead of N).

#### A.4 — ✅ Coalesce repaint requests (verified 2026-04-27)

The startup / load / zoom log shows `paint()` firing 5+ times in
succession from cascading `repaintRequested` signals. Add a 0-delay
`QTimer` debounce in `MapCanvas` (or wherever `repaintRequested` lands)
that collapses bursts to one paint per event-loop tick.

**Files:** wherever `repaintRequested` is wired (search:
[grep `repaintRequested`](../src/map/mapcanvas.cpp) is the first place to
look).

**Acceptance:** post-load / post-zoom paint count drops from 5+ to 1
per logical event. Total wall time post-load drops proportionally.

---

### Phase B — GL pipeline (the architectural pivot)

**Goal:** swap the canvas viewport to `QOpenGLWidget`, draw the SWMM layer
via raw `glDrawArrays(GL_LINES, …)` over a packed VBO inside
`painter->beginNativePainting()`. This is the change that crosses the
million-object threshold.

Phase A's flat link arrays + spatial index + selection-flag arrays are all
prerequisites — the VBO upload becomes a `memcpy` from already-packed
buffers, and the cull tells `glDrawArrays` which subrange to draw.

#### B.1 — ✅ Spike: `painter->beginNativePainting()` works on macOS Qt 6.10 (verified 2026-04-27)

Before committing to the rewrite, build a minimal smoke test that opens a
`QGraphicsView` with a `QOpenGLWidget` viewport, has one custom item that
calls `beginNativePainting`, draws one triangle via raw GL, and
`endNativePainting`. Verify it renders correctly on M1 + the project's
current Qt build.

**Files:** new throwaway target under `tests/gui/spike_glnativepaint.cpp`
or similar.

**Acceptance:** the triangle renders on top of QPainter content without
artifacts, on the current Qt 6.10 macOS build. If it fails, revisit
Decision 2 (consider `QRhiWidget` instead).

#### B.2 — ⏳ Add `QOpenGLWidget` viewport to `MapCanvas`

Wire the canvas's `QGraphicsView` to use a `QOpenGLWidget` viewport. Every
existing item's `paint()` now goes through the GL paint engine. Validate
that all current rendering still works (basemaps, GIS layers, selection
overlay, identify markers, rubber-band, edit handles).

**Files:** [src/map/mapcanvas.cpp](../src/map/mapcanvas.cpp).

**Acceptance:** the app behaves identically to before, no visual
regressions, no perf regressions on the existing layers. (Phase B's perf
win is in B.3, not B.2.)

#### B.3 — ⏳ VBO + GL draw inside `SWMMLayerItem::paint()`

- One VBO per layer carrying the packed scene-coord float array from A.3.
- Build VBO once at geometry-load time (or whenever `rebuildSceneCoords`
  runs). `m_needsVboUpload` flag triggers re-upload.
- Inside `paint()`: `painter->beginNativePainting()`, set up the program
  + projection matrix (derived from `painter->transform()`), bind the
  VBO, query the spatial index for visible link indices, issue
  `glDrawArrays(GL_LINES, offset, count)` per visible run (or use
  `glMultiDrawArrays` to batch). `endNativePainting()`.
- Selection rendering: second pass with a different uniform colour over
  the indices flagged in `m_selectedFlag` (Phase A.2). At 5M, this is
  another VBO chunk or a per-vertex selection attribute.

**Files:** [src/map/swmmlayeritem.cpp](../src/map/swmmlayeritem.cpp) +
[include/map/swmmlayeritem.h](../include/map/swmmlayeritem.h) (GL state),
[src/layers/swmmmodellayer.cpp](../src/layers/swmmmodellayer.cpp) (VBO
upload trigger).

**Acceptance:** paint of West Whiteland at full extent drops from ~400 ms
to <30 ms. At zoomed-in views, <5 ms. A synthetic 5M-link test fits in
~30 ms per paint at full extent.

#### B.4 — ⏳ Node + subcatchment + gage rendering through GL

Same approach for node glyphs (instanced quads with per-instance shape
selector), subcatchment polygons (triangulated → `GL_TRIANGLES` VBO),
and rain gages.

**Files:** same as B.3.

**Acceptance:** all existing node / subcatchment / gage visuals preserved,
all rendered through GL.

---

### Phase C — Polish (UX-level smoothness)

**Goal:** make 5M *feel* fast across pan, zoom, selection — not just hit a
paint-time number.

#### C.1 — ⏳ LOD / decimation when zoomed out

At full extent on a 5M model, individual links overdraw. Render every
Nth link (or pre-aggregate into clustered geometry). Switch back to full
detail on zoom-in.

**Files:** [src/layers/swmmmodellayer.cpp](../src/layers/swmmmodellayer.cpp)
(LOD selection),
[src/map/swmmlayeritem.cpp](../src/map/swmmlayeritem.cpp) (LOD-aware
draw call).

**Acceptance:** "Zoom to extent" on 5M completes in <100 ms.

#### C.2 — ⏳ Tile cache for the static layer

Pre-rasterize the SWMM layer to GL textures at the current zoom. On pan,
blit textures; only re-rasterize on zoom change. Selection / hover stay
in a separate dynamic overlay so they don't invalidate the cache.

**Files:** new `SWMMTileCache` class +
[src/map/swmmlayeritem.cpp](../src/map/swmmlayeritem.cpp).

**Acceptance:** pan at 60 fps on 5M with no per-frame VBO traversal.

#### C.3 — ⏳ Async tile / VBO build off the GUI thread

Build new-zoom tiles or re-pack VBOs on a worker thread; swap in when
ready. Pan/zoom stay smooth mid-build.

**Files:** worker class + signal wiring.

**Acceptance:** no GUI stall during tile rebuild.

---

## Risks & open questions

- **macOS GL→Metal translation behavior on `glDrawArrays(GL_LINES)` at very
  high counts** — probably fine, but B.1 spike will give hard data.
- **Selection rendering at 5M** — drawing 100k highlighted lines as a
  separate pass is itself non-trivial. May need per-vertex selection
  attribute + shader branch instead of a second draw call.
- **Identify hit-testing** at 5M — `pickAt`/`linksInRect` already use
  KD-trees + bbox cache; should still scale, but verify on synthetic 5M
  data once Phase A lands.
- **Memory** — 5M links × avg 4 vertices × 8 bytes (`QPointF`) = 160 MB
  for the cached scene-coords alone. With flat float arrays: 5M × 4 × 8
  bytes = 160 MB. Plus VBO copy on GPU. Live with it; modern machines
  have GB to spare.
- **`QRhi` migration timing** — defer until OpenGL on macOS actually
  shows pain (artifacts, deprecation warnings, Apple ships removal
  notice). Track the `painter->beginNativePainting()` API for any Qt
  changes.

---

## Progress log

Append-only. Each entry: **date — slice — outcome**. Keep entries short;
detail belongs in the relevant phase section above.

### 2026-04-26 — Pre-Phase-A selection wins (Slice 1)

Set up the timing pipeline that this whole plan depends on, and cleared the
selection-side bottlenecks so the rendering bottleneck is unambiguous.

- ✅ **`SWMMModelLayer::objectTypeFor` is now O(1)**. Replaced the four
  linear `for` scans (`m_nodes`, `m_links`, `m_catchments`, `m_gages`)
  with a single `m_objectLocation.constFind(name)` plus a `Category`
  range check. The hash was already built in `rebuildCategoryIndex()`.
  [src/layers/swmmmodellayer.cpp:1260-1273](../src/layers/swmmmodellayer.cpp#L1260-L1273).
- ✅ **Selection no longer triggers a full scene rebuild**.
  `setSelectedElementNames` previously flipped `m_needsRebuild = true`,
  forcing `depopulateScene` + `populateScene` of the entire 166k-object
  layer per selection change. `SWMMLayerItem::paint()` reads
  `m_selectedNames` live each frame, so a repaint alone is sufficient.
  Added a no-op short-circuit on equality.
  [src/layers/swmmmodellayer.cpp:878-890](../src/layers/swmmmodellayer.cpp#L878-L890).
- ✅ **`ObjectBrowserPanel` selection sync is batched**. Previously
  3619-row selection caused 3619 individual `sm->select()` +
  `m_view->expand()` calls (≈ 2 s). Now builds a single `QItemSelection`,
  applies via `ClearAndSelect | Rows` once, and expands the unique parent
  set only.
  [src/ui/panels/objectbrowserpanel.cpp:266-302](../src/ui/panels/objectbrowserpanel.cpp#L266-L302).
- ✅ **Scene-coord cache (originally targeted at paint, found to be a
  no-op for same-CRS models, kept anyway)**. Added
  `m_nodeScenePts` / `m_linkScenePts` / `m_linkSceneBBoxes` /
  `m_catchScenePts` / `m_catchSceneBBoxes` / `m_gageScenePts` parallel
  arrays + `rebuildSceneCoords` / `refreshSceneCoordsForNode` /
  `refreshSceneCoordsForLink`. Wired into `buildGeometryCache`,
  `rebuildTransform`, `previewNodeMove`, `applyNodeMove`, and the
  link-vertices edit path. Paint reads from the cache. **The cache is a
  no-op for West Whiteland (same-CRS, `m_transform` is null)**, so no
  measurable gain there — the gain materializes for cross-CRS layers,
  and the layout is the prerequisite for Phase A.3 packing.
  [src/layers/swmmmodellayer.cpp](../src/layers/swmmmodellayer.cpp),
  [src/map/swmmlayeritem.cpp](../src/map/swmmlayeritem.cpp),
  [include/layers/swmmmodellayer.h](../include/layers/swmmmodellayer.h).
- 🔧 **Timing instrumentation in place**. `setSelectedElementNames`,
  `selectInRect`, `SWMMLayerItem::paint` log to qDebug. Strip after
  Phase A.4 lands.

**Diagnostic insight from the timing run:** for West Whiteland
(EPSG:4326 model, EPSG:4326 canvas), `m_transform` is null, so the
"per-vertex transform cost" the cache was meant to eliminate was
already free. The real per-paint cost is **iterating 121k links +
building a 244k-segment `QVector<QLineF>` + `painter->drawLines`**.
This is what Phase A.1 (spatial-index cull) and Phase A.3 (flat
arrays) target.

### 2026-04-27 — Phase A.1 shipped ✅

- ✅ **Uniform-grid spatial index over scene-space link bboxes**.
  `LinkSpatialGrid` nested in `SWMMModelLayer`, built at the end of
  `rebuildSceneCoords()`. Cell size = 16× median bbox diagonal;
  capped at 1024×1024 cells. Outliers inserted into every cell they
  span (no clipping). `query(rect)` returns deduplicated SoA indices.
  [include/layers/swmmmodellayer.h](../include/layers/swmmmodellayer.h),
  [src/layers/swmmmodellayer.cpp](../src/layers/swmmmodellayer.cpp)
  (`rebuild` + `query` + grid-build call site).
- ✅ **Paint queries the grid instead of scanning the SoA**.
  [src/map/swmmlayeritem.cpp](../src/map/swmmlayeritem.cpp) — replaces
  the `for (i in m_links)` + per-link `exposed.intersects(lboxes[i])`
  with a single `m_linkGrid.query(exposed)` call. The inner loop no
  longer does any bbox testing.

**Measured on West Whiteland (M1, 121,902 links, 18×13 grid built in 2 ms):**

| View | Before A.1 | After A.1 | Notes |
|---|---|---|---|
| Initial paints (warmup) | ~400 ms | 348–406 ms | similar; first frames pay Qt/driver cache costs |
| **Steady-state full-extent** | **~400 ms** | **~127–133 ms** | **3× faster** even with no items culled — replacing 121k per-link `QRectF::intersects` calls with one grid query was the dominant saving |
| Zoomed-in views (prior run) | ~400 ms | ~140 ms | same source: per-link cull overhead gone, plus reduced N |

Honest note: the full-extent improvement was **larger than the plan
predicted**. The plan expected unchanged paint at full extent because
the grid returns all links anyway. The actual win came from
eliminating the per-link `QRectF::intersects` call (~120 ms across
121k links) — a per-call cost that's invisible at smaller N but
dominant at this scale.

A separate observation worth recording: the first ~13 paints after
load are 350–400 ms, then steady-state is 127–133 ms. The cause is
unclear (Qt scene-graph warmup, GPU atlas build, layer compositing
during early frames) — not blocking, but worth investigating later
if it shows up as a startup-latency complaint.

### 2026-04-27 — Phase A.2 shipped ✅

- ✅ **Per-SoA selection / hidden flag arrays** added:
  `m_nodeSelectedFlag` / `m_linkSelectedFlag` / `m_catchSelectedFlag` /
  `m_gageSelectedFlag` plus the four matching `*HiddenFlag`. Maintained
  by `rebuildFlagArrays()` which walks `m_selectedNames` +
  `m_hiddenObjects` and uses the new `m_nameToSoa` lookup
  (`name → (kind, soaIdx)`) to flip individual bytes. Built in
  `rebuildCategoryIndex()` alongside `m_objectLocation`.
  [include/layers/swmmmodellayer.h](../include/layers/swmmmodellayer.h)
  (fields + `rebuildFlagArrays` decl + `m_nameToSoa`),
  [src/layers/swmmmodellayer.cpp](../src/layers/swmmmodellayer.cpp)
  (`rebuildFlagArrays` implementation, `rebuildCategoryIndex` populates
  `m_nameToSoa`, all four visibility setters call `rebuildFlagArrays`,
  `setSelectedElementNames` calls it).
- ✅ **`SWMMLayerItem::paint` consumes flags** — replaced the per-link
  `selectedSet.contains(name)` and `hidden.contains(name)` QString
  hashes with `linkSel[i]` / `linkHid[i]` byte reads. Same for nodes,
  catchments, gages, and labels.
  [src/map/swmmlayeritem.cpp](../src/map/swmmlayeritem.cpp).
- ✅ **`ObjectBrowserPanel` tree-sync threshold** — for selections >
  5,000 rows, the bus listener clears the tree's selection instead of
  applying ~K single-row `QItemSelectionRange` instances. The
  canvas+attribute-panel path is unaffected. This eliminates an
  O(K²)-ish blowup in QItemSelectionModel range coalescing that made
  78k-row selections take 32 seconds.
  [src/ui/panels/objectbrowserpanel.cpp:266-302](../src/ui/panels/objectbrowserpanel.cpp#L266-L302).

**Measured on West Whiteland (M1, 121,902 links + 42,809 nodes):**

| `setSelectedElementNames` count | total_ms | flags_ms | emit_ms |
|---|---|---|---|
| 21,130 | 10 | 2 | 8 |
| 25,657 | 8 | 2 | 6 |
| 28,613 | 11 | 2 | 9 |
| 61,554 | 12 | 5 | 7 |
| 63,723 | 13 | 6 | 7 |
| 66,390 | 19 | 6 | 13 |

`flags_ms` scales linearly with K (the selection count) because it's
~165k zero-fills + K hash lookups. `emit_ms` stays flat — the bridge
fan-out (SelectionManager + Object Browser + attribute panel) no
longer does per-row work for huge selections.

**Paint:** ~250–470 ms range at full extent (selected=63k didn't
visibly change paint time vs selected=0). The dominant cost is now
unambiguously `QPainter::drawLines(~244k segments)` itself plus
the per-link segment-build loop. That's what A.3 + A.4 target.

**Caveat — UX change:** with the threshold in place, large rubber-band
selections deselect any prior tree-view selection without filling in
new tree highlights. Acceptable because at 60k+ selected the tree was
unusable anyway (every category fully highlighted = visual noise),
but worth noting in the manual.

### 2026-04-27 — Phase A.3 shipped ✅

- ✅ **Flat-array link scene-coords**. Replaced
  `QVector<QVector<QPointF>> m_linkScenePts` with three parallel
  arrays:
  - `std::vector<float>    m_linkSceneFlat`     — interleaved (x, y), Y-flipped
  - `std::vector<uint32_t> m_linkVertexOffset`  — per-link first-vertex index
  - `std::vector<uint32_t> m_linkVertexCount`   — per-link vertex count

  `rebuildSceneCoords` does a pre-pass to compute total vertex count +
  offsets, then a single `assign(totalVerts * 2, 0.0f)` covers all
  links. `refreshSceneCoordsForLink` does an in-place rewrite of the
  link's slice when the vertex count is unchanged (the typical drag
  preview), and falls back to a full `rebuildSceneCoords` only when
  the count differs (rare; only happens when an editor adds/removes a
  vertex).
  [include/layers/swmmmodellayer.h](../include/layers/swmmmodellayer.h)
  (fields),
  [src/layers/swmmmodellayer.cpp](../src/layers/swmmmodellayer.cpp)
  (rebuild + refresh).
- ✅ **`SWMMLayerItem::paint` reads the flat buffer**. Pointer
  arithmetic instead of nested QVector access; no allocations on the
  inner loop. The float buffer is now exactly the layout the GL
  pipeline (Phase B) will hand to `glBufferData`.
  [src/map/swmmlayeritem.cpp](../src/map/swmmlayeritem.cpp).

**Measured on West Whiteland (M1, 121,902 links):**

| State | A.2 floor | A.3 floor |
|---|---|---|
| Steady-state full-extent | ~250 ms | **~123–145 ms** |
| Selection apply (23k) | ~10 ms | ~7 ms |
| Selection apply (10k) | ~10 ms | ~3 ms |

Honest framing: the CPU paint win at 121k is modest. The dominant
cost remains `painter->drawLines(~244k segments)` — that's the wall
that GL (Phase B) is needed to break. **A.3's real wins are
elsewhere:**

- **Memory:** 121k × QVector overhead (~24 B) + 244k × QPointF
  (~16 B) ≈ **6.7 MB** before; flat = **3 MB** after. ~50% reduction;
  scales to ~280 MB → ~120 MB savings at 5M links.
- **Phase B prep:** `m_linkSceneFlat.data()` is a `const float *`
  ready to `memcpy` straight into a VBO. No reformat step in the
  upload path.
- **Edit-path performance:** in-place vertex rewrite avoids the
  `QVector<QPointF>` allocation churn that the previous layout did
  on every drag tick.

**Caveat:** the in-place edit path assumes vertex counts don't change
during normal use. If the user adds an interior vertex via the
geometry editor, `refreshSceneCoordsForLink` triggers a full
`rebuildSceneCoords` (which also rebuilds the spatial grid + flag
arrays). Acceptable today — vertex add/remove is a rare,
explicit-action operation, not a hot path.

### 2026-04-27 — Phase A.4 verified ✅ (no code change needed)

A pre-existing 50 ms `m_refreshTimer` in
[src/map/mapcanvas.cpp](../src/map/mapcanvas.cpp) already coalesces
multiple `repaintRequested` emissions into a single
`refreshLayerItems` call. Hypothesis-driven experiment:

- ❌ **Tried 0 ms interval (collapse within one event-loop tick)**.
  Result: per-paint cost rose ~10–15% (~470 ms vs ~400 ms) and total
  paint count was unchanged or slightly higher. Reverted.
- ✅ **Confirmed root cause of "paint storm" patterns**: most extra
  paints in the log come from continuous mouse-move / hover events
  *after* a selection completes — not from synchronous signal
  cascades during the selection itself. Each cursor tick triggers a
  viewport invalidation; at 60 Hz that's hundreds of paint
  candidates, and the 50 ms timer reduces them to ~16 per gesture.
  The timer is doing real work — removing it just lets Qt schedule
  paints faster against an unchanged dominant cost
  (`drawLines(244k segments)`).

**Findings:** the 50 ms coalescer is already optimal at the
canvas/refresh level. Further per-event paint reduction requires
either:

1. Per-paint **state-change check** so paints triggered by
   non-state-changing events (cursor hover, basemap tile arrival)
   skip the segment-build step. Modest win — drawLines is the
   dominant cost.
2. **Pixmap cache** of the rendered SWMM layer, blitted on
   non-state-change paints. Phase C work — proper tile cache.
3. **GL backend** so each paint costs <30 ms regardless of count.
   Phase B work — the right answer.

Decision: leave the 50 ms timer as-is, mark A.4 complete, move to
Phase B. The "paint storm" disappears as a UX concern once each
paint costs ~10 ms instead of ~400 ms.

---

## Phase A complete — summary

| Slice | Status | Measured impact (West Whiteland, 121k links) |
|---|---|---|
| A.1 Spatial index | ✅ | Steady-state full-extent paint **~400 ms → ~127 ms** |
| A.2 Flag arrays + tree-sync threshold | ✅ | `setSelectedElementNames(78k)` **32 s → ~13 ms** |
| A.3 Flat scene-coord arrays | ✅ | Memory ~6.7 MB → ~3 MB; modest CPU win; Phase B prep |
| A.4 Repaint coalescer | ✅ | Verified existing 50 ms timer; no further optimization warranted at CPU stage |

**Net effect of Phase A on West Whiteland:**
- Paint at full extent: ~400 ms → ~130 ms typical (3× improvement)
- Selection apply at 78k items: 32 s → ~13 ms (~2500× improvement)
- Memory footprint reduced ~50% on link scene-coord cache

Phase A also lays the data-layout foundation Phase B needs:
`m_linkSceneFlat` is the float buffer that `glBufferData` will consume
directly; `m_linkGrid.query(exposed)` is the cull primitive that will
tell `glDrawArrays` which subrange of the VBO to draw.

### Next up: Phase B — GL pipeline.

Start with **B.1 spike** (`painter->beginNativePainting()` smoke test
on macOS Qt 6.10) before committing to the rewrite. Decision tree
from Architecture Decision 2 still applies:

- B.1 spike succeeds → proceed to B.2 (`QOpenGLWidget` viewport) →
  B.3 (VBO + GL draw inside `SWMMLayerItem::paint`).
- B.1 spike fails → revisit Decision 2; consider `QRhiWidget`
  instead.

### 2026-04-27 — Phase B.1 spike succeeded ✅

[tests/gui/spike_glnativepaint.cpp](../tests/gui/spike_glnativepaint.cpp)
opens a `QGraphicsView` with a `QOpenGLWidget` viewport, drops into
raw GL inside a custom item's `paint()` via `beginNativePainting()`,
and renders a triangle through `glDrawArrays(GL_TRIANGLES, …)` over a
small VBO. Build target: `spike_glnativepaint` (registered in
[tests/gui/CMakeLists.txt](../tests/gui/CMakeLists.txt) as an
executable, **not** a CTest case — it's interactive).

**Confirmed on M1 Max + Qt 6.10.2 + macOS 26.4:**

```
[spike] GL vendor : Apple
[spike] GL renderer: Apple M1 Max
[spike] GL version : 2.1 Metal - 90.5
[spike] shader + VBO created
[spike] first paint OK — beginNativePainting + drawArrays works
```

- ✅ `beginNativePainting()` / `endNativePainting()` round-trip
- ✅ `QOpenGLWidget` viewport composes correctly with QGraphicsView
- ✅ Apple's GL→Metal translation engaged (`Metal - 90.5`)
- ✅ Shader compile + link, VBO allocate, `glDrawArrays`, `glClear`,
  `glGetError` — all functional with no GL errors

**Important correction to Architecture Decision 2** (originally said
GL 4.1 cap): Qt's default context on macOS returns **GL 2.1
(compatibility profile)**, not 4.1. Going to 4.1 Core requires
`QSurfaceFormat::setProfile(CoreProfile)` and a different shader
language version (GLSL 410 vs 120). For our `GL_LINES` /
`GL_TRIANGLES` workload — basic VBOs, attribute arrays, simple
fragment shaders — **GL 2.1 is sufficient and the most battle-tested
profile on the Apple translation shim**. Stay with the default.
Revisit if a future feature (geometry shaders, tessellation, compute)
demands a higher version.

### 2026-04-27 — Phase B (OpenGL) reset, pivot to QRhi / Qt Quick Scene Graph

After taking Phase B all the way through `painter->beginNativePainting()`,
offscreen FBO + readback (`SWMMLayerGLRenderer`), GL 2.1 → GL 4.1 Core,
stencil attachment, multiple QPainter render-hint combinations, and
several hybrid CPU+GL splits, **Qt's GL paint engine on macOS Apple
Silicon's GL→Metal translation cannot fill polygons / ellipses /
arbitrary closed shapes**. Strokes work, fills don't, regardless of
context profile, FBO format, MSAA, or QPainter render hints.

The full diagnosis is captured in
`~/.claude/.../memory/feedback_qt_gl_fill_limitation.md`. Critical
findings worth not repeating:

- A diagnostic `painter.fillRect(scene_rect, Qt::yellow)` at top of the
  GL render did not appear in the readback. So the failure is in Qt's
  GL paint engine itself, not anything project code sets up.
- Apple deprecated OpenGL in 2018; the GL→Metal translation shim
  doesn't fully implement Qt's GL paint engine fill paths and never
  will. Qt's own GL paint engine is a known weak spot — the modern Qt
  rendering path is `QRhi` / Qt Quick Scene Graph, which uses native
  Metal directly on macOS.
- Workarounds tried that didn't fix it: `setProfile(CoreProfile)` per
  Stack Overflow advice, `setSamples(0)`/`setSamples(4)`,
  `setAttachment(CombinedDepthStencil)`, `glDisable(GL_CULL_FACE)`,
  `QPainter::Antialiasing` on/off, `QPainterPath` with
  `Qt::WindingFill`, `drawPoints` with wide cosmetic pen.

**Decision: skip the rest of Phase B (OpenGL). Pivot to Phase B.RHI —
Qt Quick Scene Graph (`QSGGeometryNode`) inside a `QQuickWidget`.**

Why this is the right pivot, not just another attempt:

- QSG runs on `QRhi`, which uses **native Metal on macOS** (Vulkan on
  Linux, D3D11 on Windows) — no GL paint engine, no translation tax.
- Fills work because we control the geometry directly (triangles for
  polygons, points for nodes, lines for links) — no QPainter involved.
- Cross-platform: same code ships to Linux/Windows on their native
  GPUs.
- Qt Charts, Qt Location, custom Quick Items at scale all use this
  path — it's Qt's officially supported high-performance 2D pipeline.

**What stays from Phases A and B:**

- All Phase A data layout (spatial grid, flag arrays, flat scene-coord
  arrays, hidden/selected bitsets) — these feed any downstream
  renderer, GL or RHI. Already shipped and validated.
- Memory note on the Qt GL fill limitation — keep so no future agent
  goes down the same dead end.

**What goes:**

- `SWMMLayerGLRenderer` — OpenGL FBO + QPainter renderer. Replaced by
  a QSG-based renderer.
- `MapCanvas::paintEvent`'s GL renderer integration block.
- The `glRenderingEnabled` flag in `SWMMModelLayer` and its guards in
  `SWMMLayerItem::paint` — replaced by RHI-renderer-enabled flag.
- `tests/gui/spike_glnativepaint.cpp` — kept in git history as the
  reference spike that proved GL itself worked, even though the paint
  engine on top of it was broken for our purposes.

---

## Phase B.RHI — Qt Quick Scene Graph

**Goal:** native Metal-backed rendering of the SWMM layer (lines,
polygons, node glyphs, gages) via `QSGGeometryNode`s. Targets the same
"5M+ objects, 30 fps interactive" outcome as the original Phase B,
with no fill-rendering limitations and full cross-platform portability.

### Architecture

- `MapCanvas` keeps its existing CPU `QImage` compositor for raster
  basemaps + decorations (flicker-free pan via stale-buffer transform
  is preserved).
- A `QQuickWidget` is hosted as a child of `MapCanvas`, sized to match
  the canvas viewport, with the SWMM layer rendered inside it as a
  custom `QQuickItem` whose `updatePaintNode()` builds geometry nodes
  from the layer's data layout.
- The `QQuickWidget` is layered ABOVE the raster compositor and BELOW
  the CPU tool overlay (rubber-band, decorations).
- For each visible `SWMMModelLayer`, the `QQuickItem` exposes one
  geometry-node tree:
  - **Subcatchments** — `QSGGeometryNode` of `GL_TRIANGLES`
    (CPU-side polygon triangulation via Qt's earcut, then upload).
  - **Links** — `QSGGeometryNode` of `GL_LINES`, packed from
    `m_linkSceneFlat` (already in the right format from Phase A.3).
  - **Nodes** — point-sprite or instanced quad node (one drawcall for
    all nodes).
  - **Gages** — same as nodes.
  - **Selection highlight** — separate node trees with yellow material,
    populated from `m_*SelectedFlag` (Phase A.2).

### Slices

#### B.RHI.1 — ⏳ Spike

Smallest possible `QQuickWidget` with one `QSGGeometryNode` rendering
a colored triangle. Verifies the scene graph + Metal path works in
this Qt build before any architectural commitment. Same risk-reduction
shape as the GL B.1 spike.

**Files:** `tests/gui/spike_qsgnode.cpp` + a CMake target.

**Acceptance:** triangle renders; the log reports
`QSGRendererInterface::graphicsApi() == Metal` (or another non-OpenGL
backend on non-macOS). No GL warnings.

#### B.RHI.2 — ⏳ Replace `SWMMLayerGLRenderer` with a QSG renderer

Drop in `SWMMLayerQSGRenderer` (custom `QQuickItem` subclass). For the
first cut: render only the **link** geometry node, parity with
Phase B.3 first-cut. CPU path continues to handle subcatchments /
nodes / gages until B.RHI.3 lands.

**Files:** new `include/map/swmmlayerqsgrenderer.h` and
`src/map/swmmlayerqsgrenderer.cpp`. Wire it via a child
`QQuickWidget` in `MapCanvas`. Remove `SWMMLayerGLRenderer` references.

**Acceptance:** West Whiteland renders with links via QSG; visually
matches the CPU CPU output; per-frame line draw drops below 50 ms at
full extent.

#### B.RHI.3 — ⏳ Polygons + nodes + gages + selection coloring

Add `QSGGeometryNode`s for the rest. Polygon triangulation handled
once at geometry-cache build time and stored alongside
`m_linkSceneFlat`; selection highlights live in their own nodes with
yellow material. Selection updates re-upload only the selected-node
geometry, not the base.

**Acceptance:** all visuals from the original CPU path are preserved;
fills work (this is the whole point); selection highlights show on
all object classes; paint cost at full extent <30 ms.

#### B.RHI.4 — ⏳ Strip OpenGL renderer

Delete `SWMMLayerGLRenderer.{h,cpp}`, the CMake entries, the
`glRenderingEnabled` flag, the `MapCanvas` integration, the
`tests/gui/spike_glnativepaint.cpp` build target (keep the source as
a reference under a `_archive/` dir if desired).

---

### Architecture finding (revising B.2 / B.3) — historical record

The block below is the abandoned OpenGL-path investigation. Kept as
the diagnostic trail that produced the QRhi pivot decision.

`MapCanvas` is **not** a vanilla `QGraphicsView`. It inherits
`QWidget`, paints into a `QImage` (`m_frameBuffer`), then blits the
image to the widget surface
([src/map/mapcanvas.cpp:905-1018](../src/map/mapcanvas.cpp#L905-L1018)).
The hidden `m_overlayView` is used only for transform math. The
SWMM layer's `paint()` runs via `m_scene->render(&p, …)` where `p` is
a `QPainter` on the **QImage** — not a GL surface. So
`beginNativePainting()` cannot work there even if we swap a viewport.

**Revised plan:**

- **B.2** = make `MapCanvas` inherit `QOpenGLWidget`. The existing
  CPU compositing (raster basemaps + decorations into `m_frameBuffer`)
  stays as-is — just gets blitted onto a GL-backed widget surface
  instead of a raster one.
- **B.3** = do a separate GL render pass for the SWMM layer in
  `paintEvent` *after* the `m_frameBuffer` blit. Inside that pass,
  `painter->beginNativePainting()` works because the painter targets
  the GL surface. `SWMMLayerItem::paint()` becomes a no-op for the
  canvas-paint path (the item is still useful for hit-test
  bookkeeping if needed). The SWMM data is rendered by a new method
  on `SWMMModelLayer` (e.g. `renderGL(extent, viewportSize)`) that
  uploads `m_linkSceneFlat` as a VBO and issues
  `glDrawArrays(GL_LINES, …)`.

This is more invasive than the original "just swap a viewport"
sketch. The reason: this codebase deliberately composes via a
`QImage` for flicker-free pan, and we don't want to throw that away
for basemap layers. The hybrid (CPU compositor for raster +
decorations, GL for the heavy SWMM layer) preserves both.

### Next up: Phase B.2 — `MapCanvas` inherits `QOpenGLWidget`.
