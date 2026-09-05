@page manual_selection 09 — Selection and Search

## What you'll do

Pick the objects you want to work on — junctions, conduits, subcatchments,
rain gages, GIS features, 2D mesh vertices/edges/cells — from whichever view is
most convenient, and have every other view follow. Then edit them, plot them,
trace the network from them, or delete them in bulk.

\figtodo{09_selection_overview.png, A selection made on the map mirrored in the Object Browser; the Properties panel and the Attribute Table}

## Where to find it

| Command | Menu | Ribbon | Shortcut |
|---|---|---|---|
| **Select** | **Edit → Select** | Home ▸ Select; Model ▸ Select | — |
| **Select by Polygon** | **Edit → Select by Polygon** | Home ▸ Select | — |
| **Invert Selection** | **Edit → Invert Selection** | Home ▸ Select | — |
| **Select Upstream** | **Edit → Select Upstream** | Home ▸ Select | — |
| **Select Downstream** | **Edit → Select Downstream** | Home ▸ Select | — |
| **Search** | **Edit → Search** | Home ▸ Inspect | `Ctrl+F` |
| **Copy** | **Edit → Copy** | Home ▸ Inspect | `Ctrl+C` |
| **Edit Existing** | **Edit → Edit Existing** | Model ▸ Edit | `Ctrl+E` |
| **Zoom To Selection** | **View → Zoom To Selection** | Home ▸ Navigate | `Ctrl+Shift+J` |
| **Select Mesh Vertices / Edges** | **Model → Mesh** | Mesh 2D | — |

Selection is also driven from the **Object Browser** (`Ctrl+Alt+2`), the
**Attribute Table** (`Ctrl+Alt+4`) and the layer tree.

## Step-by-step

### The selection bus

Each open project owns one `SelectionManager` — the canonical set of selected
objects for that project. Every view that cares about selection subscribes to
it and reports its own gestures back to it, so a click in one place is
reflected everywhere without per-pair sync code.

Each entry is a *(type, name)* pair, not a bare name. SWMM keeps a separate
name space per object class, so a node and a link — or a rain gage and a
subcatchment — may legitimately share an id without being confused for one
another.

The bus supports four modes:

| Mode | Effect |
|---|---|
| **Replace** | Discard the current selection and use the new set. |
| **Add** | Union the new set into the current selection. |
| **Toggle** | Flip each entry's membership. |
| **Subtract** | Remove each entry from the current selection. |

Selection is **per project**: switching MDI tabs swaps the active bus, so two
open projects carry independent selections. Selection is **not a model edit** —
changing or clearing it never marks the project dirty and never deletes
anything.

### Selecting on the map

Activate **Select** and click. The tool walks the visible layers top-down and
the first hit wins, so one click never selects two overlapping objects.

| Gesture | Result |
|---|---|
| Click an object | Replace the selection with it |
| **Shift**-click | Add it to the selection |
| **Ctrl**-click (⌘ on macOS) | Subtract it from the selection |
| Drag a box | Replace with everything the rubber band touches |
| **Shift**-drag | Add the box contents |
| **Ctrl**-drag | Subtract the box contents |
| Click empty space | Clear every SWMM and GIS layer's selection |
| **Shift**/**Ctrl** + click empty space | Leave the selection alone |
| **Esc** | Clear the selection |
| **Del** / **Backspace** | Delete the selected objects (with confirmation) |

Two pick tolerances govern the tool, both on **Tools → Preferences… →
Selection**:

- **Click tolerance** (default 16 px) is a *minimum*. The effective pick radius
  is `max(tolerance, largest rendered glyph half-bound + 4 px halo)`, so a
  click anywhere inside a visible symbol always hits regardless of the number.
- **Drag threshold** (default 15 px) is how far the cursor must travel before a
  click becomes a rubber band. Trackpad jitter on a click routinely reaches
  10–12 px; raising this forgives it.
- **Clear selection when clicking empty space** turns the click-on-nothing
  behaviour off.

The same page carries the selection pens and fills — the stroke for the link
halo, polygon outline and glyph outline, and the fill brush for polygonal and
glyph classes. A link pen width is *additive*: a width of 2 means a 2 px halo
on top of the link's own pen.

\figtodo{09_select_tool_preferences.png, Preferences → Selection with the click tolerance; drag threshold and selection pens}

### Select by Polygon

**Select by Polygon** draws a lasso instead of a box.

1. Left-click to drop polygon vertices; the growing outline is drawn on the
   overlay with a rubber-band edge that tracks the cursor.
2. **Right-click**, **double-click** or **Enter** closes the polygon and
   selects every node, link, subcatchment and rain gage whose representative
   point falls inside it.
3. **Esc** cancels the in-progress polygon.

**Shift** adds the enclosed objects to the current selection; **Ctrl** removes
them; no modifier replaces.

\figtodo{09_select_by_polygon.png, A lasso being drawn across a sewer network with the enclosed objects highlighted}

### Selecting in the Object Browser and the Attribute Table

Both docks are two-way bound to the same bus.

- **Object Browser** — the tree uses extended selection: click replaces,
  `Ctrl`-click toggles a row, `Shift`-click extends a contiguous range. A
  selection arriving from elsewhere scrolls the matching row into view and
  highlights it.
- **Attribute Table** — selecting rows pushes the objects onto the bus, and a
  selection made anywhere else highlights the matching rows. **Show selected
  only** filters the table down to the current selection; **Zoom to selected**
  frames it on the map.

Right-clicking on the map deliberately does **not** change the selection: the
context menu acts on whatever is under the cursor without disturbing what is
already highlighted.

### Search

**Edit → Search** (`Ctrl+F`) is not a dialog. It raises the **Object Browser**
dock and puts the caret in its **Filter by name…** box with the existing text
selected, so you can type straight over it.

Filtering is case-insensitive, substring-based and recursive — a category stays
visible while any object under it matches — and it is debounced, so typing into
a million-object project does not rebuild the filter on every keystroke. The
`(x)` clear button restores the full tree.

The filter **hides rows; it does not select them**. Selecting an object whose
name does not match the current filter will not clear the filter.

For finding *commands* rather than objects, use the **Command Palette**
(`Ctrl+Shift+P`) — a frameless popup with a filter line over every registered
action. See \ref manual_interface.

\figtodo{09_search_filter.png, The Object Browser filtered to names containing OUT}

### Select Upstream and Select Downstream

Both actions trace the routing graph from whatever is currently selected and
**replace** the selection with the result. The status bar reports how many
nodes, links and subcatchments were found.

Seeding rules:

- A selected **node** seeds itself.
- A selected **link** seeds both of its end nodes.
- A selected **subcatchment** seeds itself when tracing upstream; when tracing
  downstream it contributes its outlet chain, and the terminal outlet node
  becomes a node seed.
- Mesh elements and non-spatial data objects in the selection are ignored.

The traversal is a breadth-first search over the directed link graph, reversed
for upstream. A link joins the result only when **both** of its endpoints land
inside the visited node set.

Subcatchments are asymmetric, and that is physical rather than an oversight:
drainage runs subcatchment → node, so a subcatchment is always upstream of its
outlet. Tracing upstream therefore pulls in every subcatchment draining to a
visited node — and, transitively, every subcatchment draining into one of
those. Tracing downstream picks up subcatchments only along the outlet chain of
a subcatchment seed.

If nothing traceable is selected the command says so in the message log and
does nothing.

\figtodo{09_select_upstream.png, The upstream subnetwork of a selected outfall highlighted on the map}

### Invert Selection

**Invert Selection** is category-scoped when the selection is homogeneous.
"Category" here is the fine object category — Junctions, Outfalls, Storage
Units, Dividers, Conduits, Pumps, Orifices, Weirs, Outlets, Subcatchments, Rain
Gages. Inverting a selection of three junctions therefore yields every *other*
junction; a selection holding a junction and a conduit is mixed and inverts
across all eleven categories.

- An empty selection inverts to **select all**.
- 2D mesh refs and non-spatial data refs (curves, time series, …) are **passed
  through unchanged** — they neither take part in the homogeneity test nor get
  inverted.

### Zoom To Selection

`Ctrl+Shift+J` unions the bounding box of every selected object across every
visible SWMM model layer and frames it. It is a view change only.

### Selecting 2D mesh elements

The Mesh 2D ribbon tab carries two picking tools.

#### Select Mesh Vertices

| Gesture | Result |
|---|---|
| Click (12 px tolerance) | Replace |
| **Shift**-click | Add |
| **Ctrl**/⌘-click | Toggle |
| Drag a box | Replace with every vertex inside |
| **Shift**-drag | Add the box's vertices |
| **Esc** | Clear |

The tool binds to the **active** 2D mesh layer at activation; with no active
mesh it is a no-op.

#### Select Mesh Edges

The edge tool defaults to **boundary edges only** — the subset a boundary
condition can mean anything on. Press **A** while the tool is active to include
interior edges (mainly diagnostic) and **B** to go back.

One click picks the nearest edge; a drag box picks every edge whose midpoint
falls inside it.

\figtodo{09_mesh_edge_selection.png, Boundary edges of a 2D mesh selected along an outfall face}

#### Boundary path selection

Assigning a boundary condition usually means selecting a long run of boundary
edges — an outfall face, a road crest, the whole domain perimeter. Clicking
each one is tedious, so the edge tool takes a path:

1. Click a boundary edge to select it, the ordinary way.
2. **Ctrl**-click (⌘-click on macOS) another boundary edge. Every edge on the
   shortest run of boundary edges between the two — both ends included — is
   **added** to the selection.
3. Keep Ctrl-clicking. Each click continues the run from the edge the last one
   ended on, so a long perimeter takes a few clicks.

The starting edge is resolved in this order: an explicitly placed anchor; the
edge this tool last selected on its own (a plain click, or the far end of the
path it just committed — this is what makes runs chain); or the single selected
edge when the selection holds exactly one for this mesh.

With nothing to start from, the first Ctrl-click instead drops a **path
anchor** — drawn distinctly and confirmed in the status bar — and the next
Ctrl-click commits the path from it. A box select leaves no single "last edge",
so the Ctrl-click after one drops a fresh anchor rather than guessing.

"Shortest" means **geometric length**, not the fewest edges: on a mesh that is
fine in one place and coarse in another, the path follows the physically
shorter route around the boundary.

Press **Esc** once to drop a pending anchor; press it again to clear the
selection.

\videotodo{Selecting a whole 2D boundary run with Ctrl-click path picking}

### Selecting GIS features

Imported vector layers (Shapefile, GeoPackage, GeoJSON, …) join the same bus.
A picked feature is carried as a `Feature` ref keyed by the layer's stable id
and the feature id, so features from several layers coexist in one selection
without collision. Picking on the map pushes into the bus and the bus pushes
back into each layer's highlight, both directions under one guard so nothing
bounces.

A point click does not stop at the first layer for GIS: the bridge rebuilds the
refs from every GIS layer on the canvas so one layer's picks do not drop
another's.

Feature attributes are read in the **Attribute Table**; the Properties panel
does not yet carry a GIS feature adapter.

### Select by attribute from the Attribute Table

The Attribute Table's **Query** bar is the select-by-attribute route. Type a
SQL-like `WHERE` clause, choose a selection mode with the radios above it, and
press **Apply** — the rows are filtered *and* the chosen mode is applied to the
matched rows.

| Radio | Effect on the current selection |
|---|---|
| **Replace** | Replace it with the matched rows |
| **Add** | Union the matched rows into it |
| **Subtract** | Remove the matched rows from it |
| **Intersect** | Keep only rows that are in both matched and selected |
| **Invert** | Replace it with every row in this category that is *not* selected (ignores the query) |

The grammar and worked examples are in \ref manual_attribute_tables.

### Multi-select modifier summary

| Surface | Replace | Add | Subtract | Toggle |
|---|---|---|---|---|
| Map **Select** | click | Shift+click | Ctrl+click | — |
| Map **Select by Polygon** | close | Shift | Ctrl | — |
| **Object Browser** tree | click | Shift+click (range) | — | Ctrl+click |
| **Attribute Table** rows | click | Shift / Ctrl per Qt table rules | — | Ctrl+click |
| **Select Mesh Vertices** | click | Shift+click | — | Ctrl+click |
| **Select Mesh Edges** | click | Shift+click | — | *Ctrl is path picking* |

The mesh edge tool is the one place the modifier grammar differs: **Ctrl/⌘ is
path picking there, not Toggle**.

### Copy

`Ctrl+C` is registered once, on the main window, and routes to whatever has
focus:

| Focus | What lands on the clipboard |
|---|---|
| **Attribute Table** | The selected rows as **TSV** — a header line plus one line per row, in the view's current sort and column order, hidden columns skipped. With no row selected it falls back to every *visible* row (what the query and show-selected-only filters leave). |
| **Message Logs** | The selected log rows. |
| Anywhere else | The active map canvas as an **image**. |

There is no `.inp`-fragment clipboard format: copying objects on the map copies
a picture of them, not the model text.

### Deleting a selection

**Del** or **Backspace** with the Select tool active — or the map's right-click
**Delete…**, the Object Browser's **Delete…**, or the Attribute Table's
**Delete (Del)** — deletes the selected objects. All four routes go through the
same undoable command, so behaviour is identical wherever the delete starts.

What happens:

1. Every target is classified from its typed kind bits, not from its name — SWMM
   name spaces are per type, so classifying by name could delete a same-named
   object of the wrong kind.
2. Cascades are analysed: deleting a node also removes the links attached to it.
3. **Virtual and inlet junctions** are intercepted. Deleting one — or one of the
   two conduits it sits between — offers **Re-fuse Conduits** (put the split
   conduit back together, the default) or **Delete Node && Conduits** /
   **Delete Conduit**. For an inlet junction the dialog also names the
   `[INLET_USAGE]` row that will be removed.
4. A node that receives groundwater from subcatchments is called out in the
   confirmation text — the engine nulls those subcatchments' groundwater node.
5. You confirm, then everything is deleted as **one batch**: one engine
   `delete_many` call per kind, one cache rebuild, one repaint, and one entry
   on the undo stack. Selecting thousands of objects and pressing Delete is a
   single undoable step.

\figtodo{09_confirm_bulk_delete.png, The Confirm Delete prompt for a multi-object selection}

Data objects (curves, time series, transects, …) are deleted from the Object
Browser's data section. Kinds whose engine delete API is not wired show a
disabled **Delete…** with a tooltip saying so, rather than pretending.

### Edit Existing

**Edit Existing** (`Ctrl+E`, Model ▸ Edit) is a per-project toggle that opens an
explicit **edit session**. It gates the Select tool's inline geometry editing:
with the session off, double-clicking an object is an ordinary select with no
edit side-effect; with it on, a double-click drops vertex and position handles
onto the picked node, gage, link or subcatchment so you can drag them. Adding
*new* objects is not gated — those tools are available whenever a project is
open.

The toggle is disabled until a project opens, is remembered per project window,
and its state follows you across tab switches. The editing gestures themselves
are described in \ref manual_map_editing.

## Tips and gotchas

- The bus is re-entrancy guarded in both directions, so a change pushed from
  one view does not bounce back and produce double events.
- Selecting rows in the Attribute Table **replaces** the whole selection.
  Switching between two tables and selecting rows in each will not accumulate a
  mixed set.
- The Object Browser is populated when the model finishes loading, not when the
  dock is first shown. Opened before any project is active it is empty by
  design.
- On a large mesh opened progressively, the boundary is not known until the
  background load finishes; path picking reports no path until then.
- Path picking needs **boundary** edges at both ends. Ctrl-clicking an interior
  edge or empty space says so in the status bar and does nothing. If the two
  edges sit on boundary loops that are not connected — a mesh with an island,
  say — there is no path, and the anchor is kept so you can pick another target.
- **Zoom To Selection** uses only *visible* SWMM model layers. A selection whose
  objects are on a hidden layer has nothing to frame.
- The Properties panel follows the selection but shows **one** object — see
  \ref manual_object_browser. Editing many objects at once is the Attribute
  Table's job.

## Related

- \ref manual_object_browser — the Object Browser tree and the Properties panel that follow the selection
- \ref manual_attribute_tables — the query bar; the selection-mode radios and mesh element tables
- \ref manual_map_editing — the Select tool's inline edit mode and geometry editing
- \ref manual_map_navigation — zooming and panning around a selection
- \ref manual_2d_mesh — 2D mesh boundary conditions the edge selection feeds
- \ref manual_analysis_tools — flow balance and travel time; both seeded from the selection
- \ref manual_preferences — the Selection page
- \ref manual_shortcuts — the default key bindings
