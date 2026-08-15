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

## 2D mesh edges: selecting a whole boundary run

The **Select Mesh Edges** tool on the mesh toolbar picks edges of the
active 2D mesh — one click for the nearest edge, or drag a box for every
edge whose midpoint falls inside it. Press **A** while the tool is active
to include interior edges, **B** to go back to boundary edges only.

Assigning a boundary condition usually means selecting a long run of
boundary edges — an outfall face, a road crest, the whole domain
perimeter. Clicking each one is tedious, so the tool takes a path:

1. Click a boundary edge to select it, the ordinary way.
2. **Ctrl-click** (⌘-click on macOS) another boundary edge. Every edge on
   the shortest run of boundary edges between the two — both ends
   included — is **added** to the selection and highlighted.
3. Keep Ctrl-clicking. Each click continues the run from the edge the
   last one ended on, so a long perimeter takes a few clicks.

If nothing is selected when you Ctrl-click, that first click has no
starting edge to work from, so it instead drops a **path anchor** —
drawn in magenta, confirmed in the status bar — and the next Ctrl-click
commits the path from it. Selecting an edge first and Ctrl-clicking is
the same thing in one step less.

A **box select** leaves no single "last edge", so the Ctrl-click after
one drops a fresh anchor rather than guessing where the run should
start.

"Shortest" means **geometric length**, not the fewest edges — on a mesh
that is fine in one place and coarse in another, the path follows the
physically shorter route around the boundary.

Press **Esc** once to drop a pending anchor; press it again to clear the
selection.

- **Attribute table ↔ map** — selecting rows in the Attribute Table
  highlights the objects on the map and vice versa, both routed through
  the bus. This covers the 2D mesh element tables too — see
  [09 — 2D Mesh Attribute Tables](09_mesh_attribute_tables.md).

## What's coming

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
- In the mesh edge tool, **Ctrl/⌘ is path picking, not Toggle** — it is
  the one place the modifier grammar above differs. Shift-click still
  adds and a plain click still replaces.
- Path picking needs **boundary** edges at both ends: Ctrl-clicking an
  interior edge or empty space says so in the status bar and does
  nothing. If the two edges sit on boundary loops that aren't connected
  to each other (a mesh with an island, say), there is no path — the
  anchor is kept so you can pick a different target.
- On a large mesh opened progressively, the boundary is not known until
  the background load finishes; path picking reports no path until then.

## Related

- [04 — Coordinate Reference Systems](04_crs.md) — selection is in object
  IDs, independent of CRS.
- [05 — Layer Management](05_layers.md) — visibility and ordering apply to
  layers, not to objects within them.
- [09 — 2D Mesh Attribute Tables](09_mesh_attribute_tables.md) — mesh
  vertices, edges and cells as selectable, editable tables.
