# 05 — Layer Management

## What you'll do

Add basemaps and GIS data to a project, control visibility and ordering, zoom to a
single layer, and understand how the **Layers** dock follows the active project tab.

## Where to find it

- The **Layers** dock (default position: left side). Toggle it via `View → Layers`.
- Right-click any layer row for a context menu.

## Step-by-step

### Layers dock anatomy

```
┌── Layers ─────────────────────────────────┐
│ [Filter layers…                       (x)]│   ← search box
├───────────────────────────────────────────┤
│ Layer                          | Opacity  │
├───────────────────────────────────────────┤
│ ▾ SWMM Model (1)                          │   ← category header
│   ☑  📐 Project A                100%    │
│ ▾ SWMM Results (1)                        │
│   ☑  📊 Project A.out             80%    │
│ ▾ Vectors (2)                             │
│   ☑  📏 Roads.shp                100%    │
│   ☐  ⬡  Subareas.geojson         60%    │   ← unchecked = hidden
│ ▾ Basemaps (1)                            │
│   ☑  🌐 OSM Standard             100%    │
└───────────────────────────────────────────┘
```

The dock has **no toolbar of its own** — adding layers is done from the main
application toolbar's *Add* group:

| Main-toolbar action | Action |
|---------------------|--------|
| **Add SWMM Output** | File picker for `.out` files; attaches as a results layer to the active project's model. |
| **Add Vector Data** | File picker for Shapefile / GeoJSON / GeoPackage / KML / GML. |
| **Add Basemap** | Opens the WMTS / XYZ tile connection dialog (e.g. OSM, Google, Bing tiles). |
| **Add Raster Data** | File picker for GeoTIFF / IMG / NetCDF / HDF / ASC. |
| **Add WMS Data** | Opens the WMS connection dialog. |

All layer-adding goes through these toolbar actions; the dock focuses purely on
inspection, ordering, and per-layer operations via the right-click menu.

### Categories

Layers are auto-grouped into the following categories (only non-empty
categories are shown):

| Category | Includes |
|----------|----------|
| **SWMM Model** | The active project's `SWMMModelLayer` — its nodes, links, subcatchments, gages. |
| **SWMM Results** | One or more `.out` results layers attached to the model. |
| **Vectors** | GIS vector layers (shapefiles, GeoJSON, GeoPackage). |
| **Rasters** | GIS raster layers (GeoTIFFs, IMG, etc.). |
| **Basemaps** | WMS, WMTS, XYZ tile services. |
| **Tabular** | Attribute-only and time-series tabular layers. |
| **Sub-projects** | Composite layer collections. |
| **Other** | Any layer that doesn't fit a known category. |

Within a category, layers appear in **canvas-stack order** (top-of-stack first).
Each layer row shows the per-type icon from the application's resource bundle
so you can tell vector / raster / basemap apart at a glance.

### Visibility, ordering, and opacity

- Toggle the checkbox in the **Layer** column to show / hide. Hidden layers are
  skipped by both the raster render job and the scene populate pass — no cost.
- Drag a row to reorder, or use the toolbar **Up / Down** buttons. The list is
  shown **top-of-stack first** — the row at the top renders on top of the map.
- The **Opacity** column shows a percentage; double-click to edit
  (e.g. type `60` for 60% opacity).

### Right-click menu

| Item | What it does |
|------|--------------|
| **Zoom to Layer** | Same as the toolbar Zoom To button. |
| **Properties…** | Opens the layer properties dialog *(coming in Slice D-2 — currently a no-op signal)*. |
| **Move Up / Down** | Disabled at top / bottom respectively. |
| **Hide / Show Layer** | Equivalent to toggling the checkbox. |
| **Remove Layer** | Drops the layer; cannot be undone yet. |

### How the Layers dock follows the active tab

Each open project tab has its own canvas with its own layer stack. When you
switch tabs, the **Layers** dock re-binds to the focused project's canvas — the
list, visibility states, and ordering you see always belong to the **frontmost
project**. With no project open, the dock is empty and toolbar buttons are
no-ops.

### Searching the tree

Type into the **Filter layers…** box at the top of the dock to live-filter
the tree by layer name. Filtering is case-insensitive and recursive — a
category header is hidden if none of its layers match. Clear the box (or
hit the (x) clear button) to restore the full tree. The active selection
is preserved across filter changes.

### Layer Properties dialog

Right-click a layer row → **Properties…** (or double-click the row) to open
a 3-tab dialog:

| Tab | Contents |
|-----|----------|
| **General** | Editable layer name, read-only type label, CRS row with a *Change…* button (delegates to the same CRS picker as the canvas-CRS button). |
| **Rendering** | Visibility checkbox, paired opacity slider + percentage spin box (kept in sync). |
| **Metadata** | Read-only summary: layer ID (UUID), type, visibility, opacity, CRS, extent in layer coordinates, child count. |

Edits are committed to the layer when you press **OK** or **Apply** — press
**Cancel** to discard pending changes. The CRS row applies its choice to
the layer immediately on pick (consistent with the canvas-CRS button); for
permanent coordinate reprojection, use the canvas-CRS button on the status
bar (Phase 0.7 reproject prompt).

### How tab focus works

The Layers dock and status-bar widgets follow the **frontmost project
tab**, not the focused widget. Clicking on the Layers dock or any other
panel does **not** clear the tree — bindings persist through transient
focus changes. The dock only clears when the last project tab is closed.

## Tips and gotchas

- Type icons come from the application's Qt resource bundle
  (`resources/swmmvis.qrc`) — vectors get the *AddVector* icon, rasters the
  *AddRaster* icon, basemaps the *AddWMS* / *AddBasemap* icons, etc. The
  per-type mapping lives in `layertreepanel.cpp`'s
  `data(..., Qt::DecorationRole)` switch.
- **Removing a layer is not undoable yet** — the engine-state-mutating undo
  (Phase 0.7 reproject also falls in this bucket) lands once the broader
  command stack for non-edit-tool operations is in place.
- **Drag-drop reorder** is supported within a category only. Cross-category
  drag is intentionally a no-op in this slice — categories are derived from
  layer type, so dragging a vector into the Basemaps group would be
  meaningless. Cross-window drag (between two open projects) is also not
  supported.
- The category headers always show the live layer count in parentheses —
  `Vectors (3)` etc. The count updates immediately on any add / remove.

## Related

- [02 — The Interface](02_interface.md) — where the Layers dock lives.
- [04 — Coordinate Reference Systems](04_crs.md) — basemap layers depend on a
  valid canvas CRS.
