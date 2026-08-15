@page manual_map_editing 12. Map Editing

OpenSWMM lets you edit the geometry of a loaded SWMM network directly on
the map canvas. Three tools are in the Editing toolbar: **Move Node**,
**Edit Vertex**, and the **Add …** family (Add Junction / Add Outfall /
Add Storage / Add Divider). Every geometry change goes through the
undo stack, so `Ctrl+Z` reverts the most recent edit and `Ctrl+Shift+Z`
reapplies it.

## Moving a node

1. Activate **Move Node** from the Editing toolbar.
2. Left-click and drag any node on the map.
3. Release the mouse to commit. The node's new coordinate is written to
   the engine and every attached link's endpoint follows.
4. Press **Escape** mid-drag to cancel.

## Editing link vertices

1. Activate **Edit Vertex**.
2. Click a link on the map. The tool highlights the link and draws a
   small white handle at each interior vertex (endpoints are fixed —
   they belong to the from- and to-nodes; move those with **Move Node**).
3. **Left-drag** a handle to move that vertex. Release to commit.
4. **Right-click** on an interior handle → **Delete vertex**.
5. **Right-click** on an empty stretch of the link (within 10 px) →
   **Insert vertex here** — the new vertex snaps to the closest point
   on the polyline.
6. Press **Escape** or double-click empty canvas to deselect the link.

## Adding a node

The Editing toolbar has one button per node type: **Add Junction**,
**Add Outfall**, **Add Storage**, **Add Divider**. Activate the one you
want, then left-click anywhere on the canvas to place a node. A name is
auto-generated (`J1`, `J2`, … for junctions; `O1`, `S1`, `D1` for the
other kinds) with the next unused suffix so nothing collides.

Use the property editor (*Object Browser → right-click → Properties…*,
Phase 3) to set invert elevation, max depth, and so on. The newly-placed
node starts with zero elevation / depth.

## Auto-length

A checkbox in the status bar toggles **Auto-Length**. When on, every
**Move Node** or **Edit Vertex** commit that touches a conduit
recomputes its length from the new polyline and writes it through the
engine's `CONDUITS` `length` field. When off, the length is left alone
(the default, matching the conventional SWMM GUI).

The toggle is per-project and persists across sessions in `QSettings`
under `SWMMVis/autoLength`.

## Undo / redo

- **Move Node**, **Edit Vertex**, and **Add Node** all push onto the
  canvas undo stack (`MapUndoStack`). Use the **Edit** menu, `Ctrl+Z`
  / `Ctrl+Shift+Z`, or the Undo/Redo dock to step through edits.
- Consecutive moves of the same node merge into a single undoable step
  so you don't have to undo fifty tiny drag-frames to get back to the
  original position.
- **Add-node undo** only succeeds while the added node is still the
  most recent — subsequent adds / edits can forfeit the undo. This is
  a deliberate trade-off for the engine's narrow `pop-last` API; a
  general-purpose `node_remove(idx)` that supports undoing arbitrarily
  old adds is planned for Slice F-2. In the meantime, if you need to
  back out an older add, save the project to `.inp`, remove the
  offending entry, and reload.

## Limitations in this cut

- **Delete**, **Add Conduit**, and **Add Subcatchment** are not yet
  wired — the engine's index-renumbering remove API isn't implemented,
  and the conduit / polygon tools depend on it. Tracked as
  Slice F-2 in the implementation plan.
- Editing is only valid when the engine is in the **BUILDING** or
  **OPENED** state. Once you start a simulation (`Execute`), the tools
  stop committing until the run ends and the engine re-opens the model.
- When the canvas is reprojecting the model layer on-the-fly
  (Phase 0.7 "Re-render only" CRS change), edit commits write the
  canvas coordinate rather than the layer coordinate. Switch to the
  layer's native CRS or run a full **Reproject** before editing.
