@page manual_selection 08 — Selection

## What you'll do

Pick SWMM objects (junctions, conduits, subcatchments, gages, …) so you can
edit them, plot their results, run calibration, or operate on them in bulk.

## Where to find it

Selection is a **per-project bus** — one canonical set of selected objects
follows the focused tab. Every view that cares about selection (the map,
the attribute table, charts, the object browser when it lands) reads from
and writes to the same bus, so a click in one place is reflected
everywhere.

## How selection works

A `SelectionManager` per project owns the selection set. Each entry is an
`SWMMObjectRef` — a (type, name) pair — so a node and a link with the same
name aren't conflated.

The bus accepts four modes (the QGIS / ArcGIS Pro vocabulary):

| Mode | Effect | Typical input |
|------|--------|---------------|
| **Replace** | Drop the current selection and use the new set as-is. | Click on an object with no modifier. |
| **Add** | Union the new set with the current selection. | Shift-click or rubber-band with Shift. |
| **Toggle** | Flip each new ref's membership (selected ↔ unselected). | Ctrl-click. |
| **Subtract** | Remove each ref from the current selection. | Alt/Option-click or rubber-band with Alt. |

Whenever the set changes, every subscriber is notified via
`selectionChanged(current, added, removed)`. Subscribers can do incremental
updates instead of diffing their own copy of the selection.

## What's wired today

- **Map gestures** — click and rubber-band selection on the canvas already
  populate the map layer's selection set; the bridge in
  `SWMMVisProjectWindow::loadModel` forwards each change to the
  `SelectionManager` so any other subscribed view sees the same set.
- **SelectionManager → map layer** — programmatic changes (e.g. via a
  future select-by-attribute / select-by-location tool) push into the
  manager; the bridge routes them back to the layer so map highlights
  update.

## What's coming

- **Attribute panel sync** (Phase 6.2) — selecting a row highlights the
  corresponding object on the map and vice versa, both routed through the
  bus instead of peer-to-peer wiring.
- **Object browser sync** — the dock will follow the active selection and
  populate from it.
- **Chart sync** — clicking a series on a time-series plot will select the
  underlying object.
- **Select by Attribute** (Phase 6.5) and **Select by Location** (Phase
  6.6) — both feed into the bus with a chosen mode (Replace / Add /
  Subtract / Toggle / Intersect-as-future-extension).

## Tips and gotchas

- Selection is **per project**: switching MDI tabs swaps the active bus.
  Two projects can carry independent selections at the same time.
- Selection is **not a model edit** — clearing the selection does not
  delete objects, and changing it does not mark the project dirty.
- The bus is re-entrancy-guarded — a manager-driven update doesn't bounce
  back into the layer (and vice versa) so listeners don't see double
  events.

## Related

- [04 — Coordinate Reference Systems](04_crs.md) — selection is in object
  IDs, independent of CRS.
- [05 — Layer Management](05_layers.md) — visibility and ordering apply to
  layers, not to objects within them.
