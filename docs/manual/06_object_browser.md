@page manual_object_browser 06 — Object Browser and Property View

## What you'll do

Navigate every SWMM object in the active project by type, click to
select on the map, and see the picked object's properties in the
Attribute Panel.

## Where to find it

- **Object Browser** dock (default position: tabbed with the Layers
  dock on the left side).
- **Attribute Panel** dock (default position: right side).

## Step-by-step

### Object Browser anatomy

```
┌── Object Browser ────────────────────────────┐
│ [Filter by name…                          (x)]│
├──────────────────────────────────────────────┤
│ ▾ Rain Gages (3)                             │
│   ◇ RG1                                       │
│   ◇ RG2                                       │
│ ▾ Subcatchments (198)                         │
│   ◇ S1                                        │
│   ◇ S2                                        │
│   ◇ …                                         │
│ ▾ Junctions (1167)                            │
│   ◇ J1                                        │
│   ◇ …                                         │
│ ▾ Outfalls (32)                               │
│ ▾ Conduits (1281)                             │
│ ▾ Pumps (4)                                   │
└──────────────────────────────────────────────┘
```

The tree mirrors the legacy SWMM 5 browser's per-type collapsible
groups. Empty groups are hidden. Each header shows the live count.

### Selecting objects

Three places drive the same shared selection bus:

1. **Click a row** in the Object Browser → selects the object
   (single-click = Replace; Ctrl/Cmd-click = Add; Shift-click extends
   contiguous range).
2. **Click an object on the map** with the Select tool → the matching
   row in the Object Browser scrolls into view and highlights.
3. **(Coming with Phase 6)** Select-by-Attribute and Select-by-Location
   tools push into the same bus.

Whatever the source, the selection is single-source-of-truth per
project. Switching MDI tabs swaps the active selection — each project
has its own.

### Filtering

Type into the **Filter by name…** box at the top to live-filter the
tree. Filtering is case-insensitive and substring-based; entire groups
hide when no contained object matches. Clear the box (or hit (x)) to
restore the full tree.

### Attribute Panel — read-only properties view

When you select an object, the **Attribute Panel** on the right
populates with that object's metadata: element type, name,
coordinates, sub-type (e.g. "Junction", "Conduit"). For multi-select,
it shows the first selected object's attributes today.

The panel is **read-only** in this slice. Editable per-type property
dialogs (with full slopes / lengths / inverts / curves / pumps / weir
geometries / control rules / etc.) ship with Phase 3 — selecting an
object today gives you immediate visibility into its identity but not
yet its full attribute surface.

## Tips and gotchas

- The Object Browser is **populated when the model finishes loading**,
  not when the dock is first shown. If you open the dock before any
  project is active it's empty by design — open or focus a project tab
  to populate it.
- The **search box only filters visible rows**, it doesn't change the
  selection. Selecting an object whose name doesn't match the current
  filter does not auto-clear the filter.
- For selections of more than one object, the Attribute Panel shows
  only the first object's properties. A future slice will add a "next
  / previous" stepper for multi-selection inspection.
- The browser, the map, and the panel are bound to the **active
  project**. Switching tabs swaps all three; clicks in dock widgets
  don't lose the binding (the canvas / browser / attribute view
  persist through transient focus changes).

## Related

- [05 — Layer Management](05_layers.md) — the Layers dock; categories
  there are *layer types*, not *object types*.
- [08 — Selection](08_selection.md) — the SelectionManager bus that
  drives this loop.
