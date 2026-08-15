@page manual_redraw_policy 21 — Redraw Policy and Performance Tuning

## What this chapter covers

How the canvas decides when to repaint, why most user gestures are
cheap, and how to diagnose performance regressions with the
`SWMMVIS_LOG_REDRAW` environment variable.

This is mostly a developer / power-user reference; you don't need to
read it to use the GUI.

## How the canvas redraws

The canvas separates redraws into four independent **dirty channels**.
Each channel tracks its own pending state and has its own debounce
timer so a cheap update doesn't pay the cost of an expensive one.

| Channel | Cost | Debounce | Drives |
|---------|-----:|---------:|--------|
| **Raster** | high — background `MapRenderJob` recomposes basemaps + GIS rasters | 150 ms | basemap changes, raster layer add/remove/visibility/opacity, CRS change |
| **Scene** | medium — refresh QGraphicsItems for visible vector layers | 50 ms | network edits, theme changes, vector layer visibility |
| **Overlay** | low — repaint widget-coord decorations only | immediate | selection highlights, rubber-band, measure rubber-line, cursor readout |
| **Extent** | very low — recompute scale + emit signals | immediate | pan / zoom commit |

A single user gesture often touches just one channel. For example
clicking to select an object dirties only **Overlay** — the basemap
isn't touched and the scene's vector items don't repopulate.

### Coalescing and debouncing

Multiple invalidates of the same channel within its debounce window
fire **once**. Toggling a vector layer five times quickly costs one
scene refresh, not five.

The debounce is *deferred*, not delayed: the channel timer starts on
the first invalidate and fires once at the end of the window. Subsequent
invalidates within the window only re-set the pending mask.

### Suspending and resuming

For batch operations that touch many channels (loading a project with
many layers, applying a multi-step undo command), bracket the work
with `MapCanvas::suspendRefresh()` / `MapCanvas::resumeRefresh()`. All
invalidates inside the bracket are accumulated and fire **once** on
the matching `resumeRefresh()`. The pair is reference-counted so
nesting is safe.

```cpp
canvas->suspendRefresh();
for (Layer *l : layersToAdd) canvas->addLayer(l, /*pushUndo*/false);
canvas->resumeRefresh();   // single Raster + Scene fire instead of 20+
```

## Diagnosing performance with SWMMVIS_LOG_REDRAW

Set the environment variable before launching the app:

```sh
SWMMVIS_LOG_REDRAW=1 open SWMMVis.app
# or, with a debug build:
SWMMVIS_LOG_REDRAW=1 ./build/darwin-debug/SWMMVis.app/Contents/MacOS/SWMMVis
```

Every invalidate and every channel firing is logged to stderr:

```
[redraw:invalidate] Scene  reason=layer-properties-apply
[redraw:fire] Scene  reason=layer-properties-apply
[redraw:invalidate] Overlay  reason=map-tool-select
[redraw:fire] Overlay  reason=map-tool-select
[redraw:invalidate] Raster|Scene|Extent  reason=crs-reproject
[redraw:fire] Extent  reason=crs-reproject
[redraw:fire] Scene  reason=crs-reproject
[redraw:fire] Raster  reason=crs-reproject
```

What to look for:

- **Mismatched cause / channel** — e.g. a "selection-changed" reason
  triggering Raster is a regression; selection should be Overlay-only.
- **Burst of identical invalidates** that the debounce *did* coalesce
  into one fire (good); or *didn't* (a code path is calling the
  immediate Overlay/Extent path in a loop without a `suspendRefresh`).
- **Raster firings during a pan gesture** — pan should commit Extent
  on mouse-up only; intermediate moves should preview via the stale
  buffer.

The lookup is done once per process via `qEnvironmentVariableIntValue`
so the unset case has effectively zero cost.

## Tips and gotchas

- The legacy `refresh()` and `refreshLayerItems()` methods still work
  unchanged — they fire both Raster + Scene through a single 50 ms
  timer. Code is being migrated to the typed `invalidate()` entry
  point incrementally (see the *Migration backlog* in the
  implementation plan).
- The four channels are an `enum` of QFlags — combine with `|`
  (`invalidate(Raster | Scene)`) or test with `&`
  (`pending & Overlay`).
- `invalidate(NoChannel, …)` is a no-op (handled at the top of
  `invalidate`).
- Overlay and Extent fire **synchronously** inside `invalidate()` —
  there's no point debouncing immediate work. Raster and Scene use
  per-channel `QTimer`s.

## Related

- [05 — Layer Management](05_layers.md) — adding / hiding layers
  triggers Scene (vector layers) or Raster (basemaps).
- [04 — Coordinate Reference Systems](04_crs.md) — CRS change triggers
  Raster + Scene + Extent.
