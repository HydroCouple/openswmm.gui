@page manual_attribute_tables 11 — Attribute Tables and the Attribute Calculator

## What you'll do

Put every object of one kind in a spreadsheet: sort it, query it, edit cells in
place, apply one value to many rows at once, drive the selection from a
`WHERE` clause, and copy or export what you see. The same dock lists 2D mesh
vertices, edges and cells, imported GIS feature layers, and loaded CSV/TSV
tables.

\figtodo{11_attribute_table_dock.png, The Attribute Table dock showing the Conduits category with the query bar and selection radios}

## Where to find it

| Route | Notes |
|---|---|
| **Analysis → Tabular View** (`Ctrl+Shift+A`) | Raises the dock and focuses it |
| **View → Panels → Attribute Table** (`Ctrl+Alt+4`) | Toggles the dock |
| Layer tree → right-click a layer → **Open Attribute Table** | Jumps straight to that layer's table |

The dock binds to the active project tab and re-binds on every tab switch.

## Step-by-step

### Choosing what the table lists

The **Category:** combo at the top is the source picker. It lists, in order:

1. the eleven SWMM object categories — `Junctions`, `Outfalls`,
   `Storage Units`, `Dividers`, `Conduits`, `Pumps`, `Orifices`, `Weirs`,
   `Outlets`, `Subcatchments`, `Rain Gages`;
2. after a separator, any loaded delimited-text layer as
   `▾ Table: <name> (<rows>)`;
3. any imported GIS feature layer as `◆ Features: <name> (<features>)`;
4. three entries per loaded 2D mesh —
   `△ Mesh <name> — Vertices (n)`, `— Edges (n)`, `— Cells (n)`.

Mesh entries appear immediately on a progressively loaded mesh but stay greyed
until the background load finishes, because the edge pairing and boundary flags
are part of that load. They enable themselves when it lands; their tooltip says
so meanwhile.

### The toolbar

| Control | What it does |
|---|---|
| **Show selected only** | Filter the table down to the current selection |
| **Zoom to selected** | Frame the selected rows' objects on the map |
| **Copy (Ctrl+C)** | Put the selection on the clipboard as TSV |
| **Export CSV…** | Write what is visible to a `.csv` or `.tsv` file |

`Ctrl+C` is registered once on the main window and routed to whichever panel
has focus, so the toolbar action deliberately carries no shortcut of its own.

### Columns

Each SWMM category has a fixed column schema, laid out left to right in four
blocks so the table never shifts under you:

| Block | Contents |
|---|---|
| **Inputs** | Name, object type, tag, coordinates or endpoints, and every editable engine attribute for that kind |
| **Initial quality** | One editable column per constituent — `Init. <pollutant>`, plus `Init. Water Age (hr)` and `Init. Temperature (°C)` while those options are on. Blank means "no override": the global initial concentration applies, and clearing a cell removes the element's `[INITIAL_QUALITY]` row |
| **User flags** | One editable column per defined user flag, with the flag's description as the header tooltip |
| **Dynamics** | Read-only post-run statistics (see below) |

Examples of the input block:

| Category | Input columns include |
|---|---|
| **Junctions** | Invert Elevation, Maximum Depth, Initial Depth, Surcharge Depth, Ponded Area, plus the compound cells External Inflows, Dry Weather Flow, RDII, Pollutant Treatment, Groundwater Sources |
| **Outfalls** | Invert Elevation, outfall type, gated, and the stage data the type needs — fixed stage, tidal curve or stage time series |
| **Storage Units** | Maximum Depth, storage shape, functional coefficient/exponent/constant, three shape parameters (headers are generic; the per-shape meaning is the cell tooltip), seepage rate and the Green-Ampt exfiltration triple |
| **Dividers** | The junction set plus divider type |
| **Conduits** | From/To Node, Vertex Count, Length, roughness, offsets, initial and maximum flow, entry/exit/average loss, seepage rate, barrels, culvert code, Cross-Section and Inlets compound cells |
| **Subcatchments** | Vertex Count, Area, Width, Slope, % Impervious, % Zero-Impervious, Manning's n (imperv./perv.), depression storage (imperv./perv.), rain gage, outlet, infiltration model and its parameters, plus the Land Use, LID Usage, Initial Loadings and Groundwater compound cells |
| **Rain Gages** | Rain format, recording interval, snow catch factor, data source, Station ID, Rain File (path) and Rain File Column |

Column headers carry the unit resolved from the project's flow-unit system, so
switching between US customary and SI relabels them without touching the data.
Link offset columns follow the `LINK_OFFSETS` mode — **Inlet/Outlet Offset** in
depth mode, **Upstream/Downstream Elevation** in elevation mode. See
\ref manual_object_browser.

Column widths are remembered per category (and per mesh element kind) across
sessions, and every column is at least as wide as its header.

#### Dynamics columns

The right-hand block reads the project's **active 1D results layer** — the run
chosen in the Analysis toolbar's Results combo — not the editing engine. It
re-reads when that selection changes, so re-running into the same `.out`
refreshes in place. With no results loaded the cells read zero.

| Category | Dynamics columns |
|---|---|
| Node kinds | Max Depth (Sim.), Max Overflow, Vol. Flooded, Time Flooded (hr) |
| Link kinds | Max Flow (Sim.), Max Velocity, Max/Full Depth, Total Flow Volume, Time Surcharged (hr) |
| Pumps | The link block plus Pump Cycles, Pump On Time (hr), Volume Pumped |
| Subcatchments | Total Precipitation, Total Runoff Volume, Peak Runoff Rate |
| Rain Gages | none — the engine keeps no statistics for gages |

These are computed from the `.out`, which is written at **report-step**
resolution: maxima are the largest *reported* value and volume and duration
integrals use the report step as `dt`. Expect small disagreements with the
`.rpt` summary tables when `REPORT_STEP` is much coarser than `ROUTING_STEP`.
**Pump Cycles** is the worst case — a pump that switches off and back on
between two report times is invisible, so the count is a lower bound. Shorten
`REPORT_STEP` for an exact one.

\figtodo{11_dynamics_columns.png, The right-hand dynamics block of the Conduits table after a run}

### Sorting

Click a column header to sort; click again to reverse. Sorting is numeric for
numeric columns, so the smallest value really is the first row. Sorting is a
view operation — the model's row order (which matches the Object Browser) is
untouched, and the copy/export honour the sort you see.

### The query bar

Type a SQL-like `WHERE` clause and press **Apply** to filter the rows. The
status label to the right reports `N of M matched`; **Clear** removes the
filter.

```
"Max depth" > 5
Name LIKE 'J%'
Type IN ('Junction','Outfall')
"Invert elev" > 100 AND Name LIKE '%-OUT'
Area < 0.5
Boundary = 'Yes' AND Length > 12
```

| Element | Rules |
|---|---|
| Column names | Quote with `"double quotes"` or `[brackets]` when they contain spaces. Lookup is case-insensitive |
| Numbers | `100`, `3.14`, `-2` |
| Strings | Single-quoted — `'Junction'` |
| Comparison | `=` `!=` `<` `<=` `>` `>=` |
| `LIKE` | Case-insensitive pattern match on a string column. `%` matches any sequence including none; `_` matches exactly one character |
| `IN` | Match any of a list — `Type IN ('Junction','Outfall')` |
| Combining | `AND`, `OR`, `NOT`, grouped with `( )` |

A malformed clause is reported in the status label with the offending column,
and the filter is left off.

The query bar works on every source, including the read-only ones — the
predicate evaluator runs over rows, not over the engine.

### Selection

The table is two-way bound to the project's selection bus: picking rows selects
the objects everywhere, and a selection made on the map or in the Object
Browser highlights the matching rows.

Double-clicking a **row number** in the left-hand strip zooms the map to that
element (plus the rest of the selection). It sits on the vertical header, so
the double-click-to-edit affordance on cells is untouched.

The **Selection:** radios decide what **Apply** does with the rows matched by
the query:

| Radio | Effect |
|---|---|
| **Replace** | Replace the current selection with the matched rows |
| **Add** | Union the matched rows into it |
| **Subtract** | Remove the matched rows from it |
| **Intersect** | Keep only rows that are in both matched and selected |
| **Invert** | Replace the selection with every row in this category that is *not* selected — ignores the query |

The selection ops always run against the full population of the category; the
"show selected only" filter does not narrow them.

\figtodo{11_query_and_selection.png, A WHERE clause matching 47 of 1205 rows with the Replace radio armed}

### Editing cells

Double-click a cell to edit it. The editor is chosen from the column's
metadata:

| Column kind | Editor |
|---|---|
| Numeric | Double spin box with the column's range and decimals |
| Integer | Spin box |
| Enumeration | Combo of the valid values |
| Text | Line edit — Name and Tag |
| Interval | Editable combo of the legacy `H:MM` presets; the value round-trips to engine seconds |
| File path | Line edit plus a **…** file dialog carrying the column's name filter — the rain gage's Rain File |
| File column | Editable combo whose options are the headers read from that row's resolved data file |
| Compound | The cell shows a summary and a button that opens the node, link or subcatchment compound dialog at the right page |
| Reference | Closed picker combo plus **…** — mesh coupled node, boundary time series, rating curve |
| Read-only | Not editable — derived values and the dynamics block |

Every commit goes onto the canvas undo stack, so `Ctrl+Z` round-trips a cell
edit, and it emits an *object edited* signal that refreshes the Properties
panel and the Section View for the same object. The reverse holds too.

Two edits are deliberately atomic: setting a rain gage's **Rain File** path also
commits the column selector and the file-format flip that a column implies, as
one undo macro — otherwise undo would restore the path and keep a stale column.

### Applying one value to many rows

Select several rows, right-click an editable cell, and pick one of:

- **Apply this "…" value to N selected rows** — copies the clicked cell's value;
- **Apply "…" value to N selected rows…** — prompts for a value with the input
  widget the column's editor kind implies.

The whole batch is one undo step. Rows whose cell is not editable are skipped —
so bulk-applying a boundary Stage touches only the rows already set to a
Specified Stage type. Picker columns offer only the copy-the-clicked-cell
flavour, since their value has to come from the dropdown; that also makes
"couple these twelve vertices to J1" a single gesture.

\figtodo{11_apply_value_to_rows.png, The right-click menu offering to apply one roughness value to twelve selected conduits}

### Change Type and Delete

The right-click menu on a SWMM row also carries:

- **Change Type…** — convert the row's node or link to another SWMM type. Runs
  the same confirm → convert → summary flow as the map's **Convert To ▸**; see
  \ref manual_object_browser. Not undoable.
- **Delete (Del)** / **Delete N selected (Del)** — delete the selected objects
  with confirmation, through the same undoable batch path the map and the
  Object Browser use. Disabled for non-deletable categories.
- **Zoom to selected**.

The Delete and Backspace keys do the same thing as the menu item.

### Copy and export

**Copy** puts the selection on the clipboard as **TSV**: a header line plus one
line per selected row, in the view's current sort and column order, hidden
columns skipped. With nothing selected it falls back to every *visible* row —
what the query and show-selected-only filters leave.

**Export CSV…** writes what is visible to a file. The extension picks the
separator: `.tsv` writes tab-separated with no quoting convention, anything else
writes RFC-4180-style CSV — values containing a comma, a quote or a newline are
wrapped in quotes and internal quotes are doubled. The header row uses the
proxy's column headers, so a re-sorted or re-ordered view exports the way you
see it, and the "show selected only" filter is honoured.

### The attribute calculator

A field calculator — an expression editor writing a computed value into an
existing column, a new `[USER_FLAGS]` field, or a preview-only column, scoped to
all rows / selected rows / rows matching a `WHERE` clause — is **designed but
not implemented** in this build. There is no **Field Calculator…** button on the
toolbar or in the column-header menu.

Until it ships, the two facilities that cover most of its ground are:

- the **query bar plus selection radios**, for picking the rows an expression
  would have targeted;
- **Apply … to N selected rows**, for writing one value across them as a single
  undo step.

For values that must be *computed*, export the category to CSV, compute
elsewhere, and bring the result back as a user flag or by editing the `.inp`.

### 2D mesh tables

Selecting one of the `△ Mesh …` entries lists that mesh's elements. The model is
fully virtual — rows read straight out of the mesh on every call and write
through the same undoable commands the mesh-editing toolbar pushes — so a
multi-million-cell mesh costs only a one-time edge index.

| Table | Read-only columns | Editable columns |
|---|---|---|
| **Vertices** | Index, X, Y, Marker | Elevation, Tag, Coupled Node, Coupling Cd, Coupling Area |
| **Edges** | Edge (`triangle:edge`), Boundary, Length (map units) | Conveyance; and on boundary edges BC Type, Stage, Bed Slope, Flow, Time Series, Rating Curve, Group |
| **Cells** | Index, Area (map units²), Centroid X, Centroid Y | Tag, plus one column per per-cell parameter |

Coordinates are **not** editable here — move a vertex on the map. The same goes
for the other derived columns (edge length, cell area, centroid): they are
computed from the geometry, so an edit would have nowhere to write.

The per-cell parameter columns come from a registry, so a new parameter appears
here the moment it is added. Today they are Manning's n and Initial Depth
(`[2D_TRIANGLES]`), the per-cell infiltration set — Infiltration Method plus its
positional parameters (`[2D_INFILTRATION]`) — and a greyed-out 2D groundwater
block (Saturated Conductivity, Aquifer Thickness, Porosity, initial unsaturated
and saturated depths) that is listed so the roadmap is visible but refuses
edits until the engine supports it.

A cell showing **—** does not apply to that row:

- boundary-condition columns on an **interior** edge;
- the BC parameters the row's current type does not read;
- Coupling Cd and Coupling Area on an **uncoupled** vertex — they appear the
  moment you give the vertex a coupled node;
- infiltration parameters the resolved method does not use;
- parameters still waiting on engine support.

**Conveyance** is the exception among the edge columns: it applies to interior
edges too, and writing it mirrors the value onto the other half of the edge,
because the engine requires both halves to agree.

#### One boundary condition, one parameter

Set **BC Type** first; the column it reads becomes editable and the rest show
**—**:

| BC Type | Live parameter |
|---|---|
| Wall | *(none)* |
| Normal Flow | Bed Slope |
| Specified Stage (constant) | Stage |
| Specified Stage (time series) | Time Series |
| Specified Flow (constant) | Flow |
| Specified Flow (time series) | Time Series |
| Rating Curve | Rating Curve |

**Group** is independent of the type and stays available on any boundary edge.
A value typed under a previous type is kept but hidden, so flipping back brings
it into view — and only the live parameter is ever written to the `.inp`.

#### References point at objects that exist

Three columns name something else in the model, and each is a **closed picker** —
a dropdown of what is actually defined plus a **…** button onto that family's
editor:

| Column | Offers |
|---|---|
| Vertices → **Coupled Node** | every SWMM node; blank removes the coupling |
| Edges → **Time Series** | the project's time series |
| Edges → **Rating Curve** | the project's curves |

Open a mesh without its model and these fall back to plain text entry — there is
nothing to pick from, and an empty dropdown would be a dead cell.

#### One row per edge

The mesh stores each edge against a triangle corner, so an interior edge is held
twice, once from each side. The Edges table lists it **once**, under the lower
of the two slots. Selecting that edge on the map from either triangle highlights
the same single row.

#### Finding bad cells

Tiny cells are what make a 2D run slow — a handful of sub-square-metre cells
forced in around a coupling point can dominate the timestep. The usual hunt:

1. Open the **Cells** table and click the **Area** header to sort ascending.
2. Or query for them directly — `Area < 0.5` — and read the match count in the
   status label.
3. Set the selection radios to **Replace**, press **Apply**, then **Zoom to
   selected** to see where they cluster.

This is the same measurement the layer-properties Metadata tab summarises as
min / max / mean / median, so the smallest row here is exactly the minimum
reported there.

\figtodo{11_mesh_cells_smallest_first.png, The mesh Cells table sorted ascending by area with the smallest cells selected}

Mesh elements cannot be added or deleted from the table: **Delete** is inactive
for these sources and there is no **Change Type**.

#### Assigning infiltration to a selection

Infiltration is not one parameter — it is a method, up to five positional
values and a destination, and the values only mean anything together. So it has
its own form rather than the toolbar's one-parameter cell editor: pick cells on
the map with the 2D cell picker, then open **Assign Infiltration to
Selection…**.

Two write targets:

| Target | Writes | When it is offered |
|---|---|---|
| **Per-cell overrides** | one `[2D_INFILTRATION]` row per cell | always |
| **Region tag** | one `[2D_INFILTRATION_DEFAULTS]` row | only when every selected cell shares one non-empty tag |

The region route is the in-map way to edit a *default* instead of minting
overrides, so a later region-level edit still reaches the cells. Undo restores
the cells' original **provenance** — undoing an assignment made over an
inheriting cell restores inheritance rather than a materialised copy carrying
identical numbers.

Parameter fields are masked by the chosen method, and destinations the engine
does not accept in this release are shown disabled — the same two rules the
attribute table and the region-defaults table follow.

\figtodo{11_assign_infiltration_dialog.png, The Assign Infiltration to Selection dialog with Green-Ampt parameters and the region-tag option}

### GIS feature layers

An imported vector layer's table lists its OGR features, keyed by feature id.
It is **read-only** — there are no engine setters behind a shapefile — but
sorting, the query bar, copy, export and two-way selection all work.

To turn features into SWMM objects, use **Model → Import Feature Layer…**; see
\ref manual_map_editing.

### Delimited-text (tabular) layers

A CSV or TSV added with **File → Import → Add Delimited Data…** appears as
`▾ Table: <name>`. It is read-only by design — tabular layers are observation
data, and engine setters do not apply. The query bar still filters it, and copy
and export behave as elsewhere. Selection ops are no-ops for this source: the
layer carries no SWMM object references.

These layers are how observed series reach the calibration and comparison
plots — see \ref manual_time_series_plots.

\videotodo{Querying conduits by slope; selecting the matches and bulk-applying a roughness value}

## Tips and gotchas

- Selecting rows **replaces** the whole selection. Switching between two tables
  and selecting rows in each will not accumulate a mixed set.
- **Invert** ignores the query on purpose — it inverts the selection within the
  category, which is rarely what a filtered view would give you.
- Edge length and cell area are in **map units** (the project CRS), not the
  vertical unit used by Elevation. **Coupling Area** follows the mesh's own
  units — its header says `m²` when the mesh file is tagged SI and the project
  length unit squared otherwise.
- Dynamics values are report-step aggregates. Do not chase small differences
  against the `.rpt`.
- A blank initial-quality cell is not zero — it means "no override", and the
  global initial concentration applies.
- Export writes the *visible* rows. Turn off **Show selected only** and clear
  the query first if you want the whole category.
- On very large projects the table refreshes several times while a project
  opens; column layout and selection survive it.

## Related

- \ref manual_selection — the selection bus and the modifier grammar
- \ref manual_object_browser — the single-object Properties panel and the compound dialogs the cells open
- \ref manual_2d_mesh — mesh generation; cell parameters and boundary conditions
- \ref manual_map_editing — importing GIS features as SWMM objects
- \ref manual_layers — adding delimited text; vector and mesh layers
- \ref manual_tabular_results — tabular *results* which are a different dock
- \ref manual_time_series_plots — plotting observed data from tabular layers
- \ref manual_performance — behaviour on very large models
