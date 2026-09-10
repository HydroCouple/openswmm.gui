@page manual_map_editing 12 — Editing the Network on the Map

## What you'll do

Draw a network on the canvas: place nodes, split conduits with virtual and
inlet junctions, draw links and subcatchments, drop rain gages, add text
annotations — then move things, edit their vertices, snap them to each other,
and undo any of it. Or skip the drawing entirely and import an existing GIS
feature layer as SWMM objects.

\figtodo{12_editing_toolbar.png, The Model ribbon tab with the Edit; Nodes; Links; Subcatchments; Rain Gages and Annotation groups}

## Where to find it

Everything lives on the **Model** menu and the **Model** ribbon tab.

| Command | Menu | Ribbon group |
|---|---|---|
| **Edit Existing** (`Ctrl+E`) | **Edit → Edit Existing** | Model ▸ Edit |
| **Junction** | **Model → Add Node → Junction** | Model ▸ Nodes |
| **Virtual Junction** | **Model → Add Node → Virtual Junction** | Model ▸ Nodes |
| **Inlet Junction** | **Model → Add Node → Inlet Junction** | Model ▸ Nodes |
| **Outfall** | **Model → Add Node → Outfall** | Model ▸ Nodes |
| **Flow Divider** | **Model → Add Node → Flow Divider** | Model ▸ Nodes |
| **Storage** | **Model → Add Node → Storage** | Model ▸ Nodes |
| **Pipe** | **Model → Add Link → Pipe** | Model ▸ Links |
| **Pump** / **Orifice** / **Weir** / **Outlet** | **Model → Add Link → …** | Model ▸ Links |
| **Subcatchment** | **Model → Add Subcatchment** | Model ▸ Subcatchments |
| **Rain Gage** | **Model → Add Rain Gauge** | Model ▸ Rain Gages |
| **Add Text** | **Model → Add Text** | Model ▸ Annotation |
| **Import Feature Layer…** | **Model → Import Feature Layer…** | Model ▸ Setup |
| **Assign Rain Gages…** | **Model → Assign Rain Gages…** | Model ▸ Tools |
| **Undo** / **Redo** | **Edit → Undo / Redo** | Home ▸ History |

Two per-project switches sit in the **status bar**: the **Offset** mode
(Depth ↔ Elevation) and the **Auto-Length** toggle.

The add-object tools are enabled whenever a project is open. Editing objects
that already exist additionally requires the **Edit Existing** session.

## Step-by-step

### The edit session

**Edit Existing** (`Ctrl+E`) is a toggle, per project window, disabled until a
project opens.

- **Off** — double-clicking an object on the map is an ordinary selection with
  no side effect. You can still create new objects, and still delete a
  selection.
- **On** — double-clicking an object drops editing handles onto it, so
  positions and vertices can be dragged.

The state follows the project across tab switches. In the action catalog this
is the `RequiresEditSession` idea; the add-object actions instead carry
`RequiresProject`, which is why they never grey out while a project is open.

### Inline editing: handles on the Select tool

With an edit session open, activate **Select** and double-click an object.
Handles appear in the map overlay — not as scene items — and you stay on the
Select tool throughout.

| Object | Handles |
|---|---|
| Node | One circle-and-crosshair handle at the node; dragging it moves the node |
| Rain gage | The same single handle |
| Link | One square handle per **interior** vertex. Endpoints belong to the from- and to-nodes and are moved by moving those nodes |
| Subcatchment | A filled square at the centroid (dragging it translates the whole polygon) plus a circle at each polygon vertex |

| Gesture | Result |
|---|---|
| Drag a handle | Move that vertex or object; release commits |
| Drag on empty space inside edit mode | Rubber-band select several vertex handles |
| Drag a handle that is part of a multi-handle selection | Move the whole group by the same delta |
| **Del** / **Backspace** with handles selected | Delete those vertices |
| Right-click a vertex handle | **Delete vertex** — or **Delete N selected vertices** when several are picked |
| Right-click the line or polygon edge (within ~10 px) | **Insert vertex here**; the new vertex lands on the closest point of the geometry |
| **Esc** | Cancel an in-flight drag and leave edit mode |
| Double-click the same object again | Leave edit mode |
| Double-click empty space | Leave edit mode |

Drags are previewed live — the geometry follows the cursor before you release —
and cancelled cleanly with **Esc**.

\figtodo{12_inline_vertex_edit.png, A conduit in edit mode with interior vertex handles and two selected}

A subcatchment must keep at least three vertices. Deleting more than that is
refused with an explanation, and the single-vertex delete is disabled with a
tooltip when only three remain; where the whole polygon would be destroyed you
are offered the option of deleting the subcatchment instead.

### Snapping

Single-handle drags snap to the nearest node or link interior vertex within
15 px; the snap target is drawn while it is armed. Group drags and the
subcatchment centroid translate are excluded — their delta is relative, so
snapping one absolute position would skew the whole group. The handle's own
pre-drag position is excluded from the candidates so a vertex cannot snap to
itself.

The drawing tools use the preference-driven snap engine instead:

| Preference (**Tools → Preferences… → Canvas & CRS**) | Default |
|---|---|
| **Enable snapping** | on |
| **Tolerance** | 12 px |
| **Also snap to link and subcatchment vertices** | on — otherwise only node and gage centres |

### Moving nodes and rain gages

Move a node or a gage by double-clicking it in an edit session and dragging its
handle. Both commit through the undo stack, and consecutive drags of the same
object merge into one undoable step, so a single `Ctrl+Z` returns to the
pre-drag position rather than replaying fifty drag frames.

When **Auto-Length** is on, moving a node also recomputes the length of every
conduit attached to it, as part of the same command. Rain gages have no
attached links, so there is no auto-length leg for them.

A gage can also be repositioned by typing coordinates into its Properties rows.

### Adding a node

Activate **Junction**, **Outfall**, **Flow Divider** or **Storage** and click
the canvas. The name is auto-generated from a configurable prefix plus the
next unused number:

| Type | Default prefix |
|---|---|
| Junction | `J` |
| Outfall | `O` |
| Storage | `S` |
| Divider | `D` |
| Conduit | `C` |
| Pump | `Pu` |
| Orifice | `Or` |
| Weir | `W` |
| Outlet | `Ou` |
| Rain gage | `RG` |
| Subcatchment | `Sub` |

Prefixes are edited on **Tools → Preferences… → Naming**.

If a DTM is selected on the **Terrain** toolbar, the node's invert elevation is
filled from the raster: `invert = terrain Z × unit factor + node offset`, with
the offset (negative for "below ground") set on that toolbar and the factor
derived from the raster's vertical unit versus the model's.

\figtodo{12_add_node_terrain.png, Placing a junction with a DTM active on the Terrain toolbar}

### Placing a node on a conduit

Every node tool — **Junction**, **Storage**, **Flow Divider** — inserts the
node *into* a conduit when you click on one, instead of dropping it beside it:

1. With the tool active, hover a **conduit**: a marker shows the prospective
   break point (a solid ring with a crosshair). Clicking empty canvas places
   the node freely, as before; snapping to an existing node wins over the
   conduit under it. Pumps, weirs, orifices and outlets have no length to
   divide and are never split.
2. On the click the conduit is split at the closest point on its polyline.
   The original conduit keeps its name and upstream end; a second conduit
   named `<name>_B` carries the downstream half. Cross-section, roughness and
   barrels are copied, the break-point invert is interpolated along the
   conduit's gradient, and any interior vertices are shared out between the
   two halves.
3. A **Storage** or **Flow Divider** placed this way is converted from the
   inserted junction and receives its creation defaults (a divider carries the
   engine's "two links" warning until you draw its third link).

**Outfall** is the exception: an outfall must end the network, so clicking a
conduit with it puts a hint in the status bar (and draws the marker as a dotted
ring while hovering) and places nothing.

The insertion is one undoable step, and undoing it re-fuses the two conduits.

Use a junction rather than a **Virtual Junction** when you want a real node —
one that can take inflows, dry-weather flow or a third connecting link.
Virtual junctions are computational break points and require dynamic-wave
routing; a junction inserted this way carries no such restriction.

### Virtual junctions

A virtual junction only exists between exactly two conduits, so it cannot be
placed freely. **Virtual Junction** is a *split* tool:

1. Activate it and click a **conduit**. Clicking empty canvas puts a hint in
   the status bar and places nothing.
2. The conduit is split at the closest point on its polyline and a virtual
   junction is inserted there; a second conduit is created for the downstream
   half.

The insertion is one undoable step, and undoing it re-fuses the two conduits.
Deleting a virtual junction — or either of its conduits — offers **Re-fuse
Conduits** as the default action.

### Inlet junctions

**Inlet Junction** is the same split tool with two additions:

- the hit must be a **`STREET`** conduit. An inlet junction models a gutter
  inlet, so a pipe is not a legal host; clicking one emits a status hint
  instead of splitting. (A drop inlet on a `RECT_OPEN` or `TRAPEZOIDAL`
  channel is an inlet-*usage* case, edited from the conduit's **Inlets** row.)
- the insertion is configured **before** it happens. A modal **Inlet Junction
  Setup** dialog collects the inlet design, the capture (underdrain) node and
  the placement, because the engine requires all three for the node to
  validate. Virtual and inlet junctions are already excluded from the
  capture-node list, and so are the host conduit's own two end nodes.

Undo re-fuses the conduits and drops the `[INLET_USAGE]` row.

\figtodo{12_inlet_junction_setup.png, The Inlet Junction Setup dialog collecting a design and a capture node}

### Adding a link

Activate **Pipe**, **Pump**, **Orifice**, **Weir** or **Outlet**.

1. Click on (or near) an existing node — this anchors the **from** node. A link
   must start on a node; a click on empty space is ignored.
2. Click to add intermediate polyline vertices. The rubber band tracks the
   cursor and snaps to vertex candidates as it goes.
3. Click a second node to commit. The nearest node within the snap tolerance is
   ringed by a snap indicator so you can see what you are about to connect to.
4. **Right-click** removes the last intermediate vertex, or cancels when there
   is none. **Esc** cancels outright.

\figtodo{12_add_conduit_snap.png, Drawing a conduit with the snap indicator ringing the target node}

### Adding a subcatchment

Activate **Subcatchment** and draw a polygon:

| Gesture | Result |
|---|---|
| Left-click | Add a vertex; the rubber-band polygon updates |
| **Double-click** | Add the clicked point as the final vertex, then close the polygon and commit (minimum three vertices) |
| **Enter** / **Return** | Close and commit the vertices placed so far, without adding one |
| Right-click | Remove the last vertex, or cancel when fewer than two remain |
| **Esc** | Cancel |

The subcatchment's **Outlet** is not assigned by drawing — set it on the
Properties panel's Outlet row or the attribute table's Outlet column, both of
which are closed pickers over the project's nodes and subcatchments.

To bind subcatchments to gages spatially rather than one at a time, use
**Model → Assign Rain Gages…** — see \ref manual_hydrology.

### Adding a rain gage

Activate **Rain Gage** and click. The gage is placed with the next free `RG`
name and the creation defaults for gages. Configure the rainfall source — inline
time series, or a rain file with its format, station id, column and interval —
in the Properties panel or the Rain Gages attribute table.

### Text annotations

**Add Text** places labels on the map. They live on their own **Annotations**
layer, created the first time you use the tool.

- Click **empty map** → the **Add Text Annotation** dialog opens with the
  defaults.
- Click an **existing annotation** → the same dialog opens seeded with that
  item's current style, for an in-place edit.
- Cancel leaves the layer untouched.

The dialog has a quick **Text:** field at the top — the one attribute you always
change — over a property tree carrying everything else:

| Group | Properties |
|---|---|
| Placement | `x`, `y` (layer CRS), rotation in degrees |
| Type | font, fill colour |
| Outline | enabled, colour, width |
| Halo | enabled, colour, radius |
| Background | enabled, fill colour, outline colour, outline width, padding, corner radius |

Colour rows open a colour picker, the font row a font dialog, booleans are
checkboxes. Positions are stored in the layer's CRS and reprojected to the
canvas CRS on every redraw, so an annotation stays pinned to its map location
when the project is reprojected.

Placing an annotation is undoable. There is no drag-to-move and no delete
action for an annotation in this build — reposition one by editing its `x` / `y`
rows, and remove a newly placed one with `Ctrl+Z`.

\figtodo{12_annotation_style_dialog.png, The Add Text Annotation dialog with the halo and background groups expanded}

### Object-creation defaults

Newly drawn objects are not blank. **Tools → Preferences… → Object Defaults**
holds a full set of creation defaults for all eleven object types, in **two
parallel sets** — US customary and SI — because some values (weir discharge
coefficient, outlet coefficient) are unit-semantic. A selector at the top of the
page picks which set you are editing; it starts on the open project's unit
system, and **Apply** writes both.

| Tab | Groups |
|---|---|
| **Nodes** | Junctions (max depth, initial depth, surcharge depth, ponded area); Outfalls (type, flap gate); Storage units (max depth, functional a/b/c, seepage rate); Dividers (type) |
| **Links** | Conduits (shape, geom1–4, roughness, length, barrels, entry and exit loss, flap gate); Pumps (initially on, startup and shutoff depth); Orifices (type, diameter, discharge coefficient, flap gate, open/close rate); Weirs (type, height, length, discharge coefficient, end contractions, flap gate); Outlets (rating basis, coefficient, exponent, flap gate) |
| **Subcatchments** | Surface (area, width, slope, imperviousness, Manning's n imperv./perv., depression storage imperv./perv., zero-storage imperv.); Infiltration — Horton, Green-Ampt and curve-number parameter sets, of which only the family matching the project's infiltration model is applied |
| **Rain Gages** | Rain format, recording interval, snow catch factor |

Defaults are applied immediately after the object is created and **before**
geometry-derived values, so auto-length and terrain-derived inverts win over a
default. The conduit length default and the subcatchment area default are
skipped entirely when auto-length is on. Undoing a delete restores the deleted
object's captured properties, never these defaults.

\figtodo{12_object_defaults_page.png, The Object Defaults preferences page on the Links tab}

### Auto-Length and Auto-Area

The status bar's **Auto-Length** switch is per project, persisted in
preferences, and drives two behaviours:

- **Auto-Length** — a conduit's length is computed from its polyline whenever it
  is created, whenever a node it touches is moved, and whenever its vertices are
  edited. The length is written through the engine's conduit length field as
  part of the same undoable command.
- **Auto-Area** — a subcatchment's area is computed from its polygon on
  creation, converted from square map units to the SWMM area unit (hectares for
  SI flow units, acres for US customary).

Turn it off to keep hand-entered lengths and areas — the conventional SWMM GUI
default.

### Import Feature Layer

**Model → Import Feature Layer…** turns an already-loaded GIS vector layer into
SWMM objects. Load the layer first (**File → Import → Add Vector Data…**), then
open the dialog.

\figtodo{12_import_feature_layer.png, The Import Feature Layer dialog with the attribute mapping table and a preview}

| Group | Controls |
|---|---|
| **Source Feature Layer** | Combo of the compatible vector layers on the canvas; **Selected features only**; an info line giving the feature count and the layer's CRS, with a warning when it differs from the model CRS |
| **Target SWMM Object Type** | Junction, Outfall, Storage, Divider, Rain Gage (point layers) or Conduit, Pump, Orifice, Weir, Outlet (polyline layers) |
| **Attribute Mapping** | One row per mappable attribute of the target type — bind it to a source field or a constant. **Auto-match** binds every unmapped attribute to an identically named source column. **Preset:** saves, loads and deletes named mappings, and reports any preset column that is missing from this layer. A validation line names anything required that is still unbound |
| **Link Endpoints** (link targets only) | Resolution order: **use the mapped From Node / To Node columns**, then **snap endpoints to existing nodes within** *N* map units, then **create junctions at unresolved endpoints** with a given prefix |
| **Existing Objects** | **Skip — leave existing objects unchanged**, or **Update existing objects (matched by name)** with separate **Overwrite mapped attributes** and **Overwrite geometry** checkboxes |
| **Preview** / **Import** | **Preview** runs a dry run on a worker thread and fills a per-feature results table with what would happen and why. Any edit invalidates the plan and you must preview again before importing |

Coordinates are transformed from the source layer's CRS to the model layer's CRS
during the read, so a layer in a different projection imports in the right place
— but if either CRS is unknown the dialog says so rather than guessing. See
\ref manual_crs.

The import runs on the GUI thread with a progress dialog and lands as **one undo
macro**: a single `Ctrl+Z` reverts the whole import. It reuses the ordinary
add-object commands, so creation defaults apply and the mapped attributes are
written afterwards — mapped values always win over a default.

\videotodo{Importing a manhole shapefile as junctions and a pipe shapefile as conduits with endpoint snapping}

### Deleting

Select objects and press **Del** / **Backspace**, or use the map's right-click
**Delete…**. Cascades (a node's attached links), virtual and inlet junction
re-fusing, and the batch behaviour are described in \ref manual_selection.

### Undo and redo

Every geometry and property edit goes onto the active project's map undo stack.
**Edit → Undo** / **Redo** and the Home ▸ History ribbon buttons route to the
focused project's stack, so the pair always mirrors the visible canvas and greys
out when there is nothing to do.

What is undoable:

| Operation | Notes |
|---|---|
| Add node / link / subcatchment / gage / annotation | Including the defaults and auto-length legs applied with them |
| Move node / gage | Consecutive drags of the same object merge into one step |
| Edit / insert / delete vertices | A multi-vertex delete is one step |
| Cell and property edits | Same stack as geometry |
| Insert virtual or inlet junction | Undo re-fuses the split conduit |
| Delete (single or bulk) | The whole batch is one step |
| Bulk apply to selected rows | One step |
| Object Browser category and object reordering | |
| Feature-layer import | One macro for the entire import |

**Type conversion is not undoable** — the confirmation says so before it runs.

## Tips and gotchas

- **Edit Existing** must be on before a double-click will produce handles. If
  nothing happens on double-click, that is almost always why.
- The standalone **Move Node** and **Edit Vertex** tools that older builds put
  on an Editing toolbar are gone. Both jobs now live on the Select tool's
  inline edit mode.
- A link **must start and end on a node**. Draw the nodes first, or import them.
- A virtual junction cannot be placed on empty canvas, and an inlet junction
  cannot be placed on anything but a `STREET` conduit.
- Endpoints of a link are not editable as vertices — they follow their nodes.
- The subcatchment centroid handle translates the whole polygon; do not mistake
  it for a vertex.
- Object defaults are unit-set specific. Editing the US set does not change the
  SI set; **Apply** writes whichever values each page currently holds for both.
- Auto-Length also governs Auto-Area. There is one switch, not two.
- On very large models, prefer selecting and deleting in one batch over
  repeated single deletes — the batch path takes one engine call and one
  repaint. See \ref manual_performance.

## Related

- \ref manual_selection — the Select tool; modifiers and bulk delete
- \ref manual_object_browser — setting the attributes of what you have drawn; type conversion
- \ref manual_attribute_tables — bulk-editing many new objects at once
- \ref manual_layers — loading the vector layer the import reads
- \ref manual_crs — what happens when the source layer's CRS differs from the model's
- \ref manual_preferences — Object Defaults; Naming and snapping
- \ref manual_hydraulics — streets; inlets and cross-sections
- \ref manual_hydrology — rain gages and Assign Rain Gages…
- \ref manual_2d_mesh — editing a 2D mesh which is a separate toolbar
