# Stale Render — Invalidation Plan (2026-08-03)

## Symptoms

1. A newly drawn **node / link / subcatchment** does not appear until the user zooms in/out.
2. A new **mesh vertex / cell / edge selection** does not highlight until zoom in/out.
3. Changing a **mesh vertex elevation** does not re-apply hillshade/elevation shading.

Symptoms 1 and 2 are the same defect. Symptom 3 is separate.

---

## Shared root cause (symptoms 1 & 2)

`MapCanvas` is a `QWidget` that composites three pipelines into one framebuffer
(`mapcanvas.cpp:1367`). Two of them are cached:

| Cache | Validity test | Set dirty by |
|---|---|---|
| `m_sceneBuffer` (QGraphicsScene raster) | `mapcanvas.cpp:1496-1502` — `!m_sceneDirty && m_sceneBufferExtent == m_extent && …` | `QGraphicsScene::changed`, `fireSceneChannel()`, `onLayerRepaintRequested()` |
| `m_qsgFrameCache` (`grabFramebuffer()` of the QQuickWidget) | `mapcanvas.cpp:1650-1667` — `m_qsgFrameDirty \|\| layerChanged \|\| sizeChanged \|\| cache.isNull()` | **only 7 sites** |

**Both caches are extent-keyed.** That is precisely why zooming is the workaround:
a zoom changes `m_extent`, which fails `m_sceneBufferExtent == m_extent` (`:1500`)
and trips `if (m_extent != m_qsgCachedExtent) m_qsgFrameDirty = true;`
(`mapcanvas.cpp:1240-1241`, inside `refreshLayerItems()`).

**The defect:** `MapCanvas::invalidate(DirtyChannels)` — the API every map tool uses —
has **no channel that reaches `m_qsgFrameDirty`**. Neither `invalidate()`
(`:1039`), `fireSceneChannel()` (`:1146`), nor `fireRasterChannel()` (`:1127`)
ever touches it. Channels are `Raster|Scene|Overlay|Extent` (`mapcanvas.h:246-253`);
`Overlay` alone does nothing but `update()` (`:1076-1083`), which re-composites
**both stale caches**.

### Why this bites mesh projects hardest

`qsgMeshRenderEnabled()` defaults to **`true`** (`preferencesmanager.cpp:544-548`).
When a QSG-owned mesh layer is present, `mapcanvas.cpp:1612-1619` **force-promotes the
1D network onto the GPU path** (`m_qsg1DForced`), because a CPU-painted network in the
scene buffer would composite *under* the mesh:

```cpp
if ((own2D || ownMesh) && firstSwmm
    && firstSwmm->qsgRenderKinds() == SWMMModelLayer::QsgNone) {
    firstSwmm->setQsgRenderKinds(... QsgNodes | QsgLinks | QsgCatch | QsgGages);
    m_qsg1DForced = true;
}
```

So in exactly the projects where the user is doing mesh work, nodes/links/subcatchments
*and* the mesh selection overlay all live in `m_qsgFrameCache` — the cache `invalidate()`
cannot reach. One mechanism, all three of the user's drawing/selection symptoms.

### Confirmed contributing defects

**(a) `Overlay`-only invalidations on content changes.** These tools mutate content but
request a channel that only calls `update()`:

| Site | Requests | Should be |
|---|---|---|
| `maptooladdlink.cpp:360-362` | `Overlay` | `Scene \| Overlay` |
| `maptooladdsubcatchment.cpp:186-187` (via `cancel()`) | `Overlay` | `Scene \| Overlay` |
| `maptoolmeshselectvertex.cpp:197` | `Overlay` | `Scene \| Overlay` |
| `maptoolmeshselectedge.cpp:200` | `Overlay` | `Scene \| Overlay` |
| `maptoolpick2dcells.cpp:281, 318` | `Overlay` | `Scene \| Overlay` |

For contrast, `maptooladdnode.cpp:139-140`, `maptooladdgage.cpp:69-70` and
`featurelayerimporter.cpp:481` already use `Scene | Overlay`, and every 1D selection
commit in `maptoolselect.cpp` (`:459, 589, 633, 1484, 1546, 1588, 1668`) does too.

**(b) Ordering inversion in the cell picker.** `maptoolpick2dcells.cpp` invalidates at
`:281` *before* `applySelection_()` at `:289` — and again at `:318` before `:321`.
The repaint is scheduled against the pre-selection state.

**(c) Scene-channel invalidations are silently *dropped* during a gesture.**
`fireSceneChannel()` clears the pending bit **before** the early return:

```cpp
// mapcanvas.cpp:1149
m_pendingChannels &= ~Scene;
...
// mapcanvas.cpp:1153-1154
if (m_isPanning || m_isZooming)
    return;     // gesture in progress — wait for endPan() to retrigger
```

The bit is already gone, so nothing retriggers it. Identical shape in
`refreshLayerItems()` (`:1227-1228`). Any invalidation landing mid-gesture is lost
permanently, not deferred.

**(d) The QSG renderer swallows one `repaintRequested`.**
`swmmlayerqsgrenderer.cpp:671-674` returns early whenever `m_selectionPending` is set,
and every add tool calls `setSelectedElements()` immediately after the undo push.
Currently papered over because `geometryChanged` (`:688-698`) fires first and sets
`m_contentDirty` — the codebase's own comment at `:678-687` records this as the cause of
a previous *"have to toggle the layer off/on to see a newly added junction"* bug. It is
order-dependent and one refactor away from regressing.

### Not yet confirmed — the AddNode case

`AddNode` already does everything right: `Scene | Overlay`, and `applyNodeAdd`
(`swmmmodellayer.cpp:4218-4219`) emits both `geometryChanged` and `repaintRequested`,
which reaches `onLayerRepaintRequested` (`:2158-2180`) and sets `m_qsgFrameDirty`
synchronously. It should work — so something else is holding it.

Leading hypothesis is a **grab race**: `invalidate(Overlay)` calls `update()`
immediately, so `paintEvent` can run *before* the QQuickItem has synced its new
geometry (`QQuickItem::update()` schedules an async sync). `paintEvent` grabs the
still-old frame and then **clears `m_qsgFrameDirty` at `:1721`** — the flag is consumed
by a grab that captured stale content, and nothing re-arms it until the next extent
change.

Second candidate, from the codebase's own comment at `swmmmodellayer.cpp:4213-4217`:
*"a node added beyond the prior extent is culled from paint() by the stale exposedRect
until a view change reindexes the scene."*

**I am not going to guess between these.** Phase 0 instruments and decides.

---

## Root cause (symptom 3) — vertex elevation shading

`SWMM2DMeshLayer::applyMeshVertexZ` (`swmm2dmeshlayer.cpp:1689-1741`) takes an
incremental fast path: it updates `m_sceneNodes[i].z` and each incident triangle's
`z0/z1/z2/zAvg` (`:1709-1721`), then emits `attributeChanged` + `repaintRequested`
(`:1740-1741`). **It never increments `m_geomRevision`** — the bump lives only in the
full `rebuildSceneGeometry()` paths (`:1021, 1081, 1111, 1163`), which the fast path
was written to avoid.

The QSG renderer caches shaded per-triangle RGB keyed on that revision
(`swmm2dmeshqsgrenderer.cpp:754-781`):

```cpp
const quint64 curRev = m_layer->geomRevision() ^ scheme.revision();
const bool fillCacheHit = m_fillCacheValid && m_fillCacheRev == curRev && …
```

Revision unchanged → cache hit → `emitShadedTri` (`:787-830`) reuses the pre-edit
packed colours. `repaintRequested` still fires, but `Qsg2DDirtyState::resolve`
(`qsg2ddirtystate.h:142-145`) sees no geometry/selection/time diff and downgrades it to
`Style`, which re-uploads from the stale `m_cachedFillRgb`.

The isoband (`:967-995`) and isoline (`:1160-1167`) caches share the same broken key.
The CPU path is unaffected — `swmm2dmeshlayer.cpp:290-323` recomputes shading every
paint with no cache.

**Secondary:** `applyMeshVertexZ:1727-1728` only *expands* `m_zMin/m_zMax`, never
shrinks. Lowering the current highest vertex leaves the colour ramp stretched over a
range no vertex occupies.

---

## Fix design

### F1 — Give the Scene channel authority over the GPU frame *(covers 1 & 2)*

The `Scene` channel already means "layer content changed". When QSG owns that content,
the GPU frame is part of the scene. One line in `fireSceneChannel()`:

```cpp
m_sceneDirty    = true;
m_qsgFrameDirty = true;   // NEW — the QSG overlay is scene content too
```

This makes every existing `Scene`-channel call site correct at once, rather than
auditing ~15 tool call sites and hoping none is missed later. Cost: a `grabFramebuffer()`
on scene invalidations that touched only CPU-painted layers. That grab is already
described as the dominant `paintEvent` cost (`mapcanvas.cpp:1628-1632`), so it is gated
behind the existing `qsgActive` check at `:1634-1637` — no cost when no QSG kind is owned.

### F2 — Upgrade the five `Overlay`-only sites to `Scene | Overlay`

Table (a) above. Mechanical, matches the existing convention in `maptoolselect.cpp`.

### F3 — Fix the invalidate-before-mutate inversion

Move `maptoolpick2dcells.cpp:281` after `:289`, and `:318` after `:321`.

### F4 — Stop dropping Scene invalidations during a gesture

In both `fireSceneChannel()` (`:1149`) and `refreshLayerItems()` (`:1227`), clear the
pending bit **after** the `m_isPanning || m_isZooming` guard, so a mid-gesture
invalidation survives to be re-fired by `endPan()`.

### F5 — Vertex-Z shading *(covers 3)*

Two options; **decision needed** (see below).

- **F5a (one line):** `++m_geomRevision;` in `applyMeshVertexZ`. Correct and minimal,
  but also invalidates `MeshStaticGeometryBuffers::ensureBuilt`
  (`meshstaticgeometrybuffers.h:109-111`), forcing an O(N) *position* rebuild per Z edit
  even though positions did not move.
- **F5b:** add a separate `m_shadingRevision`, bumped by Z-only edits, folded into the
  fill / isoband / isoline cache keys only. Positions stay keyed on `m_geomRevision`.
  More code; no wasted rebuild. Worth it only if Z editing on large meshes is a common
  interactive loop.

Plus: make `m_zMin/m_zMax` recompute (shrink as well as grow) when the edited vertex was
the prior extremum.

### Deliberately NOT doing

- **The `m_selectionPending` swallow (d)** — currently masked by emission order. Removing
  it means re-verifying the original "toggle layer off/on" bug it was added for. Out of
  scope unless Phase 0 shows it is live. Flagged, not touched (CLAUDE.md §3).
- **Dead signals** `nodeAdded` / `linkAdded` / `subcatchmentAdded` / `gageAdded`
  (`maptooladdnode.cpp:137` et al.) have zero `connect()` anywhere in `src/`. Pre-existing
  dead code — reported, not deleted.
- **Stale comment** `swmm2dmeshlayer.cpp:554-556` claims the QSG mesh renderer is
  inactive; `mapcanvas.cpp:1586` + the `true` default contradict it. Reported, not chased.

---

## Phases

### Phase 0 — Instrument and confirm (no behaviour change)

Temporary `qCDebug` in `MapCanvas::invalidate`, `fireSceneChannel`,
`onLayerRepaintRequested`, and the `paintEvent` QSG gate (`:1650-1667`) logging
channel bits, `m_qsgFrameDirty`, `m_sceneDirty`, and whether the grab ran — written to
`test_artifacts/render-invalidation-trace.log` per CLAUDE.md §4.1.

Reproduce all three symptoms on a mesh project and record, per symptom, **which gate
held**. Success criterion: for each of the three, a named line number that returned
early or a cache that reported valid.

Specifically decide:
- Is the AddNode failure the grab race (`m_qsgFrameDirty` cleared at `:1721` after a
  stale grab) or the `exposedRect` culling at `swmmmodellayer.cpp:4213-4217`?
- Does `MeshEditingToolbar::onSelectionChanged` (`mesheditingtoolbar.cpp:965-1001`) —
  the *single* bridge from the selection manager to `setHighlighted*` — actually fire?
  If `m_canvas`/`m_selection` is stale after `rebindCanvas` (`swmmvis.cpp:5855-5857`),
  the highlight sets are never written and no signal is emitted at all, which would make
  F1/F2 necessary but not sufficient for symptom 2.

**Gate:** three confirmed root causes on paper before any fix lands.

### Phase 1 — Regression tests (red)

`tests/gui/test_render_invalidation.cpp` (new). Headless `MapCanvas` + a `SWMMModelLayer`
+ a `SWMM2DMeshLayer`, asserting on **state, not pixels**:

1. `invalidate(Scene)` with a QSG-active layer ⇒ `m_qsgFrameDirty == true`. *Fails today.*
2. AddLink / AddSubcatchment / mesh-vertex / mesh-edge / cell-pick tool commit ⇒
   `m_sceneDirty && m_qsgFrameDirty`. *Fails today.*
3. `invalidate(Scene)` during `beginPan()`…`endPan()` ⇒ the rebuild runs after `endPan()`.
   *Fails today.*
4. Cell-pick commit ⇒ the layer's highlighted set is non-empty **before** the invalidate.
   *Fails today.*
5. `applyMeshVertexZ` ⇒ the QSG fill-cache key changes. *Fails today.*
6. A `Scene` invalidation with **no** QSG-owned kind ⇒ `m_qsgFrameDirty` stays false
   (guards against F1 re-introducing the grab cost).

Needs a test seam for the private flags — friend test class, matching whatever
`tests/gui/` already does for canvas internals.

**Gate:** 1-5 fail for the stated reason; 6 passes.

### Phase 2 — F1 + F4 (canvas)

`src/map/mapcanvas.cpp`. **Gate:** Phase 1 items 1, 3, 6 green + full `ctest -L gui`.

### Phase 3 — F2 + F3 (tools)

`maptooladdlink.cpp`, `maptooladdsubcatchment.cpp`, `maptoolmeshselectvertex.cpp`,
`maptoolmeshselectedge.cpp`, `maptoolpick2dcells.cpp`.
**Gate:** Phase 1 items 2, 4 green + full suite.

### Phase 4 — Phase 0's AddNode finding

Scope written after Phase 0. If the grab race: give each QSG renderer a
`contentRevision()` bumped in `updatePaintNode` after a rebuild, and have `paintEvent`
retain `m_qsgFrameDirty` until the grabbed frame's revision matches — i.e. **never clear
the flag on a grab that captured stale content**. If instead it is `exposedRect`
culling, the fix is in `SWMMModelLayer::applyNodeAdd`'s bounding-rect/BSP refresh.

**Gate:** a test that adds a node and asserts the composited frame changed.

### Phase 5 — F5 (mesh Z)

`src/layers/swmm2dmeshlayer.cpp` (+ `swmm2dmeshqsgrenderer.cpp` if F5b).
**Gate:** Phase 1 item 5 green + full suite.

### Phase 6 — Verification

1. Full `ctest -L gui`.
2. Manual smoke on a mesh project, **without touching the zoom**: draw a node, a link, a
   subcatchment; select vertices, cells, edges; edit a vertex Z. All must appear on the
   first frame.
3. Repeat with `Rendering/QsgMeshEnabled = false` (CPU path) — confirm no regression.
4. Frame-time check on a large model before/after, to confirm F1 did not reintroduce
   per-invalidation `grabFramebuffer()` cost.
5. `CHANGELOG.md` entry at release per §5.2.

---

## Decision needed before Phase 5

**F5a vs F5b** for the vertex-Z shading fix — one-line revision bump that also forces an
unnecessary position-buffer rebuild, versus a separate shading revision. My
recommendation is **F5a** unless dragging Z across a large mesh is a normal interactive
workflow for you, in which case F5b earns its complexity.
