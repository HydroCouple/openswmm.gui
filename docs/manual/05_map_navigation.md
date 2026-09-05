@page manual_map_navigation 05 — The Map Canvas

## What you'll do

Move around the map, measure distances and areas, read coordinates and the map
scale, probe features by hovering, act on a feature through the canvas context
menu, and get the map out of SWMMVis as an image or on paper.

## Where to find it

| Command | Menu | Ribbon tab | Shortcut |
|---------|------|------------|----------|
| **Pan** | **View** | Home | — |
| **Zoom In** | **View** | Home | `Ctrl++` (platform Zoom-In key) |
| **Zoom Out** | **View** | Home | `Ctrl+-` (platform Zoom-Out key) |
| **Zoom Extent** | **View** | Home | `Ctrl+Shift+F` |
| **Zoom To Selection** | **View** | Home | `Ctrl+Shift+J` |
| **Measure** | **View** | Home | `Ctrl+Shift+M` |
| **Map Image…** | **File** | — | — |
| **Print** | **File** | — | `Ctrl+P` |

The status bar carries the **Coordinates** readout, the **Map Scale** combo and
the **Coordinate Reference System** button (see \ref manual_crs). Right-clicking
the canvas opens a context menu that depends on what is under the cursor.

\figtodo{05_canvas_overview.png, The map canvas with a basemap; the SWMM network and the status-bar readouts}

## Step-by-step

### How the canvas is put together

The canvas composites three stacked surfaces into one picture. A background
worker renders every *raster* layer — basemaps, XYZ/WMS/WMTS tiles, GIS
rasters — into an off-screen image buffer. A transparent `QGraphicsScene`
overlay holds the *interactive vector* items (GIS vector features, 2D-mesh
graphics, annotations) and provides hit-testing. A hardware-accelerated scene
graph surface (Metal on macOS, Vulkan on Linux, Direct3D 11 on Windows) draws
the heavy content: the 2D terrain mesh at the bottom, the 2D flood map above
it, and the SWMM network on top.

What this means in practice:

- During a pan or zoom gesture the previous buffers are shown shifted and
  scaled so the map never goes blank; the sharp version arrives when the drag
  stops (and, for tiles, part-way through a long drag).
- Redraws are debounced per channel, so cheap changes (a selection highlight)
  never wait behind expensive ones (a basemap recomposite). The tuning knobs
  and how to trace a slow redraw are in \ref manual_performance.
- Hidden layers cost nothing — they are skipped by both the raster job and the
  scene pass.

### Navigating

These work whatever tool is active:

| Input | Effect |
|-------|--------|
| Scroll wheel up / down | Zoom in / out by ×1.5 about the cursor |
| Middle-button drag | Pan (the cursor becomes a closed hand) |
| Two-finger pinch | Zoom about the pinch centre (touchscreen and precision touchpad) |
| `←` `→` `↑` `↓` | Pan by 10 % of the visible width or height |
| `Shift` + arrow key | Pan by 2 % — a fine nudge |
| `+` / `=` | Zoom in ×2 |
| `-` | Zoom out ×2 |

And these are map tools — pick one and it stays active until you pick another:

| Tool | Behaviour |
|------|-----------|
| **Pan** | Left-drag moves the map; the point under the cursor follows it. Each completed drag is one undo step. |
| **Zoom In** | Left-drag a rubber-band rectangle to zoom to it; right-drag zooms out; a plain click zooms in about the click point. |
| **Zoom Out** | The same tool in its zoom-out mode. |

**Zoom Extent** fits every visible layer. **Zoom To Selection** fits the union
of the currently selected SWMM objects, padded by 25 % of the selection's span;
a single selected node or gage gets a fixed buffer instead so you don't end up
at an absurd scale. With nothing selected it does nothing. Selection itself is
covered in \ref manual_selection.

Every extent change goes on the map undo stack, so `Ctrl+Z` retraces your
navigation.

\figtodo{05_zoom_rubber_band.png, Zoom In tool dragging a rubber-band rectangle over part of the network}

### Reading position and scale

The **Coordinates** field on the status bar tracks the cursor in *canvas CRS*
units. When a terrain raster is active in the **Terrain** toolbar, its sampled
elevation is appended as a `Z:` value — converted into the model's vertical
unit (`ft` or `m`), so it is directly comparable with node invert elevations —
and is also painted next to the cursor on the canvas.

The **Map Scale** combo shows the current scale as `1:N`. It is editable: pick
one of the presets (1:100 through 1:25,000,000) or type a denominator and press
`Return` to jump to exactly that scale, keeping the current centre. The
computation is DPI-aware (correct on Retina/HiDPI screens) and CRS-aware — a
geographic CRS uses the cosine-latitude approximation, a projected CRS its own
linear unit.

A **scale bar** is drawn on the canvas itself. Its colour, line width, line
style, font, units, corner, maximum length, label decimals and compact-notation
behaviour are all set in **Tools → Preferences → Map** (see
\ref manual_preferences).

### Hover probes and tooltips

Hover over a SWMM node, link, subcatchment or rain gage in any visible model
layer and a tooltip appears after the usual delay, showing the element name in
bold, its type, the node or link sub-type, its coordinates, and the vertex
count for links and subcatchment polygons. The pick tolerance is about 12
pixels, and tooltips fire regardless of which tool is active.

With a 2D mesh loaded, the **Mesh Editing** toolbar carries its own hover probe:
it reports the barycentric bed elevation under the cursor for the active mesh
layer, sampled continuously as you move (see \ref manual_2d_mesh).

\figtodo{05_hover_tooltip.png, Hover tooltip over a conduit showing its name; type and vertex count}

### Measure

**View → Measure** (`Ctrl+Shift+M`) activates the measure tool and shows a small
floating panel over the canvas:

| Control | What it does |
|---------|--------------|
| **Mode** | `Distance` or `Area`. |
| **Units** | Distance: metres, kilometres, feet, US survey feet, miles, nautical miles, yards, international inches. Area: square metres, square kilometres, hectares, acres, square feet, square miles, square yards. |
| Total readout | The running measurement in the selected unit. |
| **Clear** | Discards the current measurement. |

Left-click to add vertices. In **Distance** mode a cumulative label is drawn at
each committed vertex; in **Area** mode the polygon is filled semi-transparently
and labelled with its area as you drag. Double-click finishes the measurement;
the next left-click starts a fresh one. `Esc` removes the last vertex while
drawing, or clears a finished result.

Measurements are CRS-aware. In a geographic CRS the points are transformed to
WGS-84 and measured with the Haversine formula (distance) or the
spherical-excess formula (area). In a projected CRS the Euclidean value is
converted with the CRS's own linear-unit factor. With a **Local** CRS the raw
Euclidean value is reported with no unit conversion.

\figtodo{05_measure_panel.png, The measure tool in Area mode with the floating Mode and Units panel}

\videotodo{Navigating a model — wheel zoom; middle-drag pan; Zoom Extent and a distance measurement}

### The canvas context menu

Right-clicking the canvas with the **Select** tool active opens a menu built
around whatever is under the cursor. Right-click never changes the selection —
the menu acts on the object you pointed at.

**Over empty background** the menu holds a single entry:

| Item | What it does |
|------|--------------|
| **Plot System Variable…** ▸ | Submenu of system-wide result variables; picking one opens a time-series plot. See \ref manual_time_series_plots. |

**Over a node, link, subcatchment or rain gage:**

| Item | What it does |
|------|--------------|
| **Zoom to Object** | Frames the object. If the object is part of a multi-object selection, frames the whole selection instead. |
| **Rainfall Visualization…** | Rain gages only — opens the rainfall dialog focused on this gage (see \ref manual_analysis_tools). |
| **Plot Time Series…** ▸ | Submenu of result variables valid for this object kind, including pollutant/species series. With two or more `.out` results layers loaded the menu gains a level: **Plot Time Series ▸ &lt;results layer&gt; ▸ &lt;variable&gt;**. |
| **Convert To** ▸ | Nodes and links only. Lists the other kinds of the same family, plus **Virtual Junction** and — when the engine supports it — **Inlet Junction**. Targets whose engine eligibility rules are not met are greyed out with the failing rule as their tooltip. |
| **Flip Direction** | Links only. Swaps the upstream and downstream nodes without moving the link. Undoable. |
| **Delete…** | Selects the object under the cursor and runs the shared confirm-and-delete flow. |

Type conversion and deletion are model edits — see \ref manual_map_editing.

\figtodo{05_canvas_context_menu.png, Canvas context menu on a conduit with the Plot Time Series and Convert To submenus}

### Selection beacon

After a selection made from somewhere non-spatial — the Object Browser or an
attribute table — the canvas flashes a locator at the selected features and
leaves a fixed-size beacon behind, so a feature stays findable at model-wide
zoom where its own highlight is sub-pixel. Any selection change from another
source clears a standing beacon.

### Export Map Image

**File → Map Image…** grabs the current canvas view and writes it to a file; the
file dialog it opens is titled *Export Map*.

The file-type list offers PNG, SVG, AutoCAD DXF, Enhanced Metafile and EPA SWMM
Map (`.map`), but **only PNG is implemented today** — choosing any of the others
produces a message saying so and writes nothing. The PNG is captured at the
canvas's on-screen size and resolution; there are no size or DPI options in this
release. Successful exports are logged to the **Message Logs** panel.

### Print

**File → Print** (`Ctrl+P`) opens the platform print dialog, then scales the
current canvas view to the printable area, preserving its aspect ratio. There is
no page-layout composer, header/footer or legend placement — what you get is the
map view as it appears on screen.

\figtodo{05_export_map_dialog.png, The Export Map file dialog with the PNG filter selected}

### The Overview Map panel

An overview-map navigator — a mini-map of the full model extent with a draggable
viewport rectangle — exists in the code base but is **not wired into the
application** in this release: it has no entry in **View → Panels** and is never
created. Use **Zoom Extent** and the map-scale combo instead.

## Tips and gotchas

- **Wheel zoom always wins.** Vertical wheel scrolling zooms no matter which
  tool is active; only horizontal (trackpad) scrolling is passed to the tool.
- **Middle-drag pan is global too** — you don't have to leave the tool you are
  using to reposition the map.
- **The arrow keys pan the canvas** whenever it has keyboard focus. No map tool
  claims them, so this never steals a keystroke from an editing tool (those use
  `Esc`, `Return`, `Delete` and letters).
- **Typing a scale only accepts a denominator.** `5000` and `1:5000` both work;
  anything else is rejected by the field's validator.
- **The scale readout is physical, not pixel-based.** Two monitors with
  different DPI show the same `1:N` for the same view.
- **Undo covers navigation.** If a stray click sent you somewhere unexpected,
  `Ctrl+Z` puts the extent back.
- **Export Map only writes PNG.** For a vector drawing of the network, export
  the layers to a GIS format instead (see \ref manual_file_formats).
- If the map redraws sluggishly on a large model, the rendering preferences and
  the redraw instrumentation in \ref manual_performance are the first place to
  look — not the map tools.

## Related

- \ref manual_interface — the window layout, ribbon and status bar
- \ref manual_crs — the CRS button, canvas CRS and on-the-fly reprojection
- \ref manual_layers — what can be put on the canvas and in what order
- \ref manual_styling — how the things on the canvas are painted
- \ref manual_selection — selecting features and tracing the network
- \ref manual_map_editing — editing the network on the canvas and annotations
- \ref manual_2d_mesh — 2D mesh display and the mesh hover probe
- \ref manual_results — animating results on the map
- \ref manual_performance — redraw policy and rendering settings
- \ref manual_shortcuts — the complete shortcut list
