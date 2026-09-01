# Handoff — Stale-Render Invalidation Fixes (2026-08-03)

**To the verifying agent:** the changes below are implemented but **not built and
not tested** — the authoring environment had no Qt/CMake toolchain. Your job is to
build, run the suite, run the manual smoke, and commit. Do not push.

Plan of record: `workplans/STALE_RENDER_INVALIDATION_PLAN_2026-08-03.md`.

---

## SCOPE — read this first

The working tree on `swmm6_gui` contains **two unrelated bodies of uncommitted
work**. Only the first is yours to verify and commit.

### Files in this handoff (commit these)

```
include/map/mapcanvas.h
include/map/swmmlayerqsgrenderer.h
include/map/swmm2dresultsqsgrenderer.h
include/map/swmm2dmeshqsgrenderer.h
src/map/mapcanvas.cpp
src/map/swmmlayerqsgrenderer.cpp
src/map/swmm2dresultsqsgrenderer.cpp
src/map/swmm2dmeshqsgrenderer.cpp
src/map/tools/maptooladdlink.cpp
src/map/tools/maptooladdsubcatchment.cpp
src/map/tools/maptoolmeshselectvertex.cpp
src/map/tools/maptoolmeshselectedge.cpp
src/map/tools/maptoolpick2dcells.cpp
src/layers/swmm2dmeshlayer.cpp
tests/gui/test_render_invalidation.cpp          (new, untracked)
tests/gui/CMakeLists.txt                        (PARTIAL — see below)
```

### Files NOT in this handoff (leave uncommitted)

A separate, in-progress 2D profile free-surface change occupies the same tree:

```
include/layers/swmm2dresultslayer.h        src/layers/swmm2dresultslayer.cpp
include/layers/cellsurfaceinterp.h         include/layers/vertexdepthreconstruct.h
include/plot/meshprofileinterp.h           include/plot/meshprofileplotwidget.h
include/plot/profilesection.h              src/plot/meshprofileplotwidget.cpp
src/plot/meshprofilesampler.cpp            src/ui/dialogs/meshprofileplotdialog.cpp
tests/gui/test_meshprofileinterp.cpp       tests/gui/test_profile_wse_extrapolation.cpp
tests/gui/test_vertexdepthreconstruct.cpp
forms/swmmvis.ui                           resources/images/thermomether.svg
examples/demo_road_culvert/*               tests/gui/data/landuse_editor/
tests/unit/data/mesh_sync_fixture.oswp
```

That work tracks `workplans/2D_PROFILE_WSE_EXTRAPOLATION_PLAN_2026-08-02.md`.
Do not commit, revert, or "tidy" any of it.

### `tests/gui/CMakeLists.txt` is shared

It has two independent hunks. **Stage only the second** (around line 1896):

```cmake
# Stale-render invalidation — vertex-Z edits must invalidate the renderer's
# geomRevision-keyed shading caches, and layer changes must advance the
# renderer content revision MapCanvas polls to regrab its framebuffer.
add_swmmvis_gui_test(test_render_invalidation
    test_render_invalidation.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_render_invalidation)
```

The hunk near line 268 (`test_profile_wse_extrapolation`,
`test_vertexdepthreconstruct`) belongs to the profile work. Use
`git add -p tests/gui/CMakeLists.txt` and accept only the later hunk.

> Note: the profile work will not build without its CMake hunk. If you need a
> green build to verify, build with the whole file and stage selectively at
> commit time — or stash the profile work first. Do not solve this by committing
> both.

---

## What changed and why

The symptom was "a newly drawn object / a new mesh selection / a vertex-elevation
edit does not render until I zoom in and out".

**Root cause.** `MapCanvas` caches the grabbed QQuickWidget framebuffer in
`m_qsgFrameCache`. That cache was keyed on extent, layer pointer and widget size —
never on content. `MapCanvas::invalidate()`, the API every map tool calls, had **no
path to `m_qsgFrameDirty`**; the only routine producer was the extent-drift check in
`refreshLayerItems()` (`mapcanvas.cpp:1240-1241`). Zooming changes the extent, which
is precisely why zooming was the workaround.

This bites hardest in 2D projects because `paintEvent` force-promotes the 1D network
onto the GPU path when a QSG-owned mesh layer is present (`mapcanvas.cpp:1612-1619`,
`m_qsg1DForced`) so the network composites above the mesh. Nodes, links,
subcatchments and the mesh selection overlay therefore all live in the one cache
`invalidate()` could not reach.

### 1. Scene channel now drives the QSG frame — `src/map/mapcanvas.cpp`

`fireSceneChannel()` sets `m_qsgFrameDirty = true` alongside `m_sceneDirty`. The
Scene channel already means "layer content changed"; when QSG owns that content the
framebuffer is part of the scene. Costs nothing when no QSG kind is owned —
`paintEvent`'s `qsgActive` guard (`:1635`) skips the grab entirely in that case.

### 2. Scene invalidations survive a gesture — `src/map/mapcanvas.cpp`

`fireSceneChannel()` cleared `m_pendingChannels &= ~Scene` **before** the
`m_isPanning || m_isZooming` early return, so an invalidation landing mid-gesture was
dropped outright. The clear now happens after the guard, and the guard re-arms
`m_sceneTimer`.

`refreshLayerItems()` was left alone deliberately: its early return self-recovers
because `endPan()` calls `refresh()`, which re-arms `m_refreshTimer`.

### 3. Content-revision guard against a stale grab — canvas + all three renderers

`SWMMLayerQSGRenderer`, `SWMM2DResultsQSGRenderer` and `SWMM2DMeshQSGRenderer` gain
`contentRevision()`, bumped by a new private `noteContentChanged()` at every site
that previously called bare `update()` in response to an **external layer signal**.
`MapCanvas::qsgContentRevision()` sums the three; `paintEvent` regrabs when the sum
differs from `m_qsgCachedContentRev`, recorded after each grab.

This makes the cache content-keyed. Without it, a renderer that learned of a change
*after* the canvas consumed and cleared `m_qsgFrameDirty` would rebuild its scene
graph in the offscreen widget while the canvas kept compositing the stale image,
with nothing to ever trigger a regrab.

Extent and layer-pointer changes deliberately do **not** bump the revision — the
canvas keys those separately, and bumping there would force a regrab on the very
paint that pushed them, every paint.

### 4. Map tools ask for the right channel — `src/map/tools/*`

`Scene | Overlay` instead of `Overlay` alone at the five **commit** sites.
Rubber-band/preview invalidations are untouched — they genuinely only need `Overlay`.

| File | Site |
|---|---|
| `maptooladdlink.cpp` | `addlink-chain` (commit) |
| `maptooladdsubcatchment.cpp` | new `addsubcatch-commit` after `cancel()` |
| `maptoolmeshselectvertex.cpp` | `mesh-vertex-select-done` |
| `maptoolmeshselectedge.cpp` | `mesh-edge-select-done` |
| `maptoolpick2dcells.cpp` | `pick2dcells-box-end`, `pick2dcells-lasso-commit` |

`maptoolpick2dcells.cpp` also had an **ordering inversion**: it invalidated *before*
`applySelection_()`, scheduling the repaint against the selection being replaced.
Both call sites now invalidate after. The empty-hit clear path gained its own
invalidate (`pick2dcells-box-clear`), which it previously relied on the misplaced
one for.

### 5. Vertex-Z edits invalidate the shading caches — `src/layers/swmm2dmeshlayer.cpp`

`applyMeshVertexZ` takes an incremental fast path that rewrites `z0/z1/z2/zAvg` —
the hillshade inputs — without going through `rebuildSceneGeometry()`, which is where
`++m_geomRevision` normally lives. `SWMM2DMeshQSGRenderer` keys its shaded-RGB
(`:754-781`), isoband (`:967-995`) and isoline (`:1160-1167`) caches on that
revision, so they reported a hit and re-uploaded pre-edit colours. Now bumped
unconditionally before the emits.

Accepted cost (this was the F5a choice): `MeshStaticGeometryBuffers::ensureBuilt` is
also keyed on `m_geomRevision`, so each Z edit forces a position-buffer rebuild even
though nothing moved in XY. If Z-dragging on large meshes turns out to stutter, the
follow-up is F5b — a separate `m_shadingRevision` for the colour caches only.

---

## Deliberate non-changes (do not "fix" these)

- **`m_zMin` / `m_zMax` stay expand-only** (`swmm2dmeshlayer.cpp:1722-1728`). The
  plan floated recomputing them so they shrink; on inspection the existing
  expand-only behaviour is a documented O(N)-avoidance that exists to keep the
  incremental path fast. Lowering the highest vertex leaves the ramp slightly
  stretched until the next full rebuild — cosmetic, and not the reported bug.
- **The `m_selectionPending` absorb** (`swmmlayerqsgrenderer.cpp:671-674`) is
  untouched. It swallows one `repaintRequested` and is currently masked by
  `geometryChanged` firing first. Fragile and order-dependent, but removing it means
  re-verifying the original "toggle the layer off/on to see a newly added junction"
  bug it was added for. Out of scope.
- **Dead signals** `nodeAdded` / `linkAdded` / `subcatchmentAdded` / `gageAdded` have
  zero `connect()` anywhere in `src/`. Pre-existing; reported, not deleted.
- **Stale comment** at `swmm2dmeshlayer.cpp:554-556` claims the QSG mesh renderer is
  inactive; `mapcanvas.cpp:1586` and the `true` default of `qsgMeshRenderEnabled()`
  say otherwise. Not chased.

---

## UNRESOLVED — the AddNode case needs your eyes

`maptooladdnode.cpp` **already** did everything right before this change: it uses
`Scene | Overlay`, and `applyNodeAdd` (`swmmmodellayer.cpp:4218-4219`) emits both
`geometryChanged` and `repaintRequested`, which reach `onLayerRepaintRequested`
(`mapcanvas.cpp:2158-2180`) and set `m_qsgFrameDirty` synchronously. By static
reading it should never have failed, yet it was reported as failing.

Two candidates were identified and **not** distinguished:

1. **Grab race** — `paintEvent` clears `m_qsgFrameDirty` after a grab that captured
   a not-yet-synced frame. Change 3 above is aimed at exactly this and may well fix
   it. Unverified.
2. **`exposedRect` culling** — see the existing comment at
   `swmmmodellayer.cpp:4213-4217`: *"a node added beyond the prior extent is culled
   from paint() by the stale exposedRect until a view change reindexes the scene."*
   Change 3 does nothing for this.

**Please determine which, during the manual smoke below.** If AddNode still fails
after these changes, it is candidate 2 and the fix belongs in
`SWMMModelLayer::applyNodeAdd`'s bounding-rect / BSP refresh, not in the canvas.
Report the finding; do not extend this commit to cover it.

---

## Verification

### 1. Build

```
cmake --build build/Darwin --parallel        # or your platform preset
```

Watch for: the three renderer headers now declare `contentRevision()` and a private
`noteContentChanged()`; `mapcanvas.h` declares `qsgContentRevision()` and
`m_qsgCachedContentRev`. Any missing-include or ordering error will surface here.

### 2. Full GUI suite

```
ctest --test-dir build/Darwin -L gui --output-on-failure
```

Must be fully green. `test_render_invalidation` is new (7 cases):

- `vertexZEdit_bumpsGeomRevision` — fails without the `swmm2dmeshlayer.cpp` change
- `vertexZEdit_bumpsOnEachEdit`
- `vertexZEdit_noOpDoesNotBump` — guards against invalidating on a no-op write
- `vertexZEdit_invalidIndexDoesNotBump`
- `meshSelection_advancesRendererContentRevision` — fails without `contentRevision()`
- `meshSelection_unchangedSetDoesNotAdvance`
- `vertexZEdit_advancesRendererContentRevision`

**Sanity-check the test is real:** revert `++m_geomRevision;` in
`applyMeshVertexZ`, confirm cases 1/2 fail, restore. A test that passes against the
unfixed code is worthless.

### 3. Manual smoke — the part the suite cannot cover

`MapCanvas` is not constructible in the offscreen harness (it needs a live
`QQuickWidget`) and its cache flags have no test seam, so the canvas-level and
tool-level behaviour is verified by hand.

Open a project **with a 2D mesh layer** (that is the configuration that force-promotes
the 1D network onto the GPU path — the bug is much weaker without one). Then, **without
touching zoom or pan at any point**:

| # | Action | Expected |
|---|---|---|
| 1 | Draw a node | appears immediately |
| 2 | Draw a link | appears immediately |
| 3 | Draw a subcatchment | appears immediately |
| 4 | Select mesh vertices | highlight appears immediately |
| 5 | Select mesh edges | highlight appears immediately |
| 6 | Box-select 2D cells | highlight appears immediately, and matches the box |
| 7 | Lasso-select 2D cells | same |
| 8 | Edit a vertex elevation | hillshade/colour updates immediately |

Then repeat 1-8 with **Preferences → Rendering → QsgMeshEnabled = false** (CPU path)
to confirm no regression there.

If #4 or #5 fails: check whether `MeshEditingToolbar::onSelectionChanged`
(`mesheditingtoolbar.cpp:965-1001`) is firing at all. It is the *single* bridge from
the selection manager to `setHighlighted*`, and if its `m_canvas`/`m_selection` is
stale after `rebindCanvas` (`swmmvis.cpp:5855-5857`) the highlight sets are never
written and no signal is emitted — a separate defect these changes do not address.

### 4. Performance check

Change 1 means a `Scene` invalidation now triggers `grabFramebuffer()` whenever any
QSG kind is owned, including for scene changes that touched only CPU-painted layers.
That grab is documented as the dominant `paintEvent` cost on large models
(`mapcanvas.cpp:1628-1632`).

On the largest model available, compare pan/zoom and animation-scrub frame times
before and after:

```
OPENSWMM_RENDER_PERF=1 ./SWMMVis        # logs repaintMs / grabMs per frame
OPENSWMM_2D_RENDER_DEBUG=1 ./SWMMVis    # logs the regrab decision + its reason
```

The debug line now prints `contentChg` alongside `dirty`/`layerChg`/`sizeChg`. If
`contentChg=1` on frames where nothing changed, a renderer is bumping its revision
from a non-external path — report it rather than papering over it.

---

## Commit

One commit. Conventional-commit style, matching recent history on `swmm6_gui`
(`feat(quality): …`, `feat(ui): …`).

```
fix(render): invalidate cached QSG framebuffer on content change

The cached QQuickWidget framebuffer was keyed on extent, layer pointer and
widget size but never on content, and MapCanvas::invalidate() had no path to
m_qsgFrameDirty. Newly drawn nodes/links/subcatchments, mesh vertex/cell/edge
selections and vertex-elevation edits therefore composited from a stale frame
until a zoom changed the extent and forced the cache to miss.

- fireSceneChannel() marks the QSG frame dirty: the Scene channel means layer
  content changed, and in a 2D project paintEvent force-promotes the 1D network
  onto the GPU path, so that content lives in the framebuffer.
- Scene invalidations arriving mid-gesture are no longer dropped; the pending
  bit is cleared after the pan/zoom guard, which re-arms the timer.
- The three QSG renderers publish a contentRevision(), bumped when an external
  layer signal marks them dirty. MapCanvas compares it against the value
  recorded at the last grab, closing the window where a change landed after the
  canvas had already consumed its own dirty flag.
- Add-link, add-subcatchment and the three mesh selection tools request
  Scene | Overlay on commit instead of Overlay alone; the 2D cell picker also
  invalidated before applying the selection rather than after.
- SWMM2DMeshLayer::applyMeshVertexZ bumps m_geomRevision so the renderer's
  geomRevision-keyed shaded-RGB, isoband and isoline caches miss; its
  incremental fast path rewrote the hillshade inputs without it.

Adds tests/gui/test_render_invalidation.cpp.
```

**Author the commit as the repository's configured user.** No `Co-Authored-By`
trailer, no `Generated with` line, no reference to any AI tool anywhere in the
message or the code comments.

Standing conventions from `CLAUDE.md` and prior handoffs: `workplans/`,
`test_artifacts/` and `Testing/` stay untracked. **Never push.**

Update `CHANGELOG.md` only at release, per CLAUDE.md §5.2 — not in this commit.
