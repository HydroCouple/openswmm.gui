@page manual_attribute_tables 11 — Attribute Tables and the Attribute Calculator

## What you'll do

Put every object of one kind in a spreadsheet: sort it, query it, edit cells in
place, apply one value to many rows at once, drive the selection from a
`WHERE` clause, and copy or export what you see. The same dock lists 2D mesh
vertices, edges and cells, imported GIS feature layers, and loaded CSV/TSV
tables.

\fig{11_attribute_table_dock.png, The Attribute Table dock showing the Conduits category with the query bar and selection radios}

## Where to find it

| Route | Notes |
|---|---|
| **Analysis → Tabular View** (`Ctrl+Shift+A`) | Raises the dock and focuses it |
| **View → Panels → Attribute Table** (`Ctrl+Alt+4`) | Toggles the dock |
| Layer tree → right-click a layer → **Open Attribute Table** | Jumps straight to that layer's table |
| Object Browser → right-click a *Pollutants*, *Land Uses*, *Aquifers*, *Streets* or *Inlets* category → **Open in Attribute Table** | Jumps to that data-object table |

The dock binds to the active project tab and re-binds on every tab switch.

## Step-by-step

### Choosing what the table lists

The **Category:** combo at the top is the source picker. It lists, in order:

1. the eleven SWMM object categories — `Junctions`, `Outfalls`,
   `Storage Units`, `Dividers`, `Conduits`, `Pumps`, `Orifices`, `Weirs`,
   `Outlets`, `Subcatchments`, `Rain Gages`;
2. after a separator, the data objects that have simple per-object fields —
   `◇ Data: Pollutants`, `Land Uses`, `Aquifers`, `Streets`, `Inlets`
   (each listed only when the model has at least one);
3. after a separator, any loaded delimited-text layer as
   `▾ Table: <name> (<rows>)`;
4. any imported GIS feature layer as `◆ Features: <name> (<features>)`;
5. three entries per loaded 2D mesh —
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

`Ctrl+C` always goes to the panel that has keyboard focus, so the toolbar
button deliberately carries no shortcut of its own.

The footer under the table reads `N of M selected`, as in GIS attribute
tables. `M` is every row of the current source, whatever the query shows, and
`N` counts its selected rows, including selected rows the query filter hides.
A loaded CSV/TSV table has no map selection, so its count is the rows
highlighted in the table.

### Columns

Each SWMM category has a fixed column schema, laid out left to right in four
blocks so the table never shifts under you:

| Block | Contents |
|---|---|
| **Inputs** | Name, object type, tag, coordinates or endpoints, and every editable engine attribute for that kind |
| **Initial quality** | One editable column per constituent — `Init. <pollutant>`, plus `Init. Water Age (hr)` and `Init. Temperature (°C)` while those options are on. NULL means "no override": the global initial concentration applies, and clearing a cell removes the element's `[INITIAL_QUALITY]` row |
| **User flags** | One editable column per defined user flag, with the flag's description as the header tooltip |
| **Dynamics** | Read-only post-run statistics (see below) |

Examples of the input block:

| Category | Input columns include |
|---|---|
| **Junctions** | Invert Elevation, Maximum Depth, Initial Depth, Surcharge Depth, Ponded Area, plus the compound cells External Inflows, Dry Weather Flow, RDII, Pollutant Treatment, Groundwater Sources |
| **Outfalls** | Invert Elevation, outfall type, gated, and the stage data the type needs — fixed stage, tidal curve or stage time series |
| **Storage Units** | Maximum Depth, storage shape, functional coefficient/exponent/constant, three shape parameters (headers are generic; the per-shape meaning is the cell tooltip), seepage rate and the Green-Ampt exfiltration triple |
| **Dividers** | The junction set plus divider type |
| **Conduits** | From/To Node, Vertex Count, Length, roughness, offsets, initial and maximum flow, entry/exit/average loss, seepage rate, cross-section shape, Geom 1–4, barrels, culvert code, Cross-Section and Inlets compound cells |
| **Subcatchments** | Vertex Count, Area, Width, Slope, % Impervious, % Zero-Impervious, Manning's n (imperv./perv.), depression storage (imperv./perv.), rain gage, outlet, Snow Pack, infiltration model and its parameters, plus the Land Use, LID Usage, Initial Loadings and Groundwater compound cells |
| **Rain Gages** | Rain format, recording interval, snow catch factor, data source, Station ID, Rain File (path) and Rain File Column |

Column headers carry the unit resolved from the project's flow-unit system, so
switching between US customary and SI relabels them without touching the data.
Link offset columns follow the `LINK_OFFSETS` mode — **Inlet/Outlet Offset** in
depth mode, **Upstream/Downstream Elevation** in elevation mode. See
\ref manual_object_browser.

For conduits, pumps, orifices, weirs and outlets, click **From Node** (upstream)
or **To Node** (downstream) to change the connection. Type the beginning of a
node ID and choose a matching suggestion, or enter an existing ID in full.
The same searchable picker appears in the Properties panel. Changes update the
map immediately and support Undo/Redo; link bends, length and offsets are retained.
Unknown IDs and connections back to the opposite endpoint are rejected.
Virtual and inlet junctions keep their fixed conduit connections.

With several conduits selected, choose **Cross-section Shape** in Properties,
then edit **Geom 1–4** or **Barrels**. Each committed field updates the selection
immediately as one undo step. Dimensions become editable together after the
selected conduits share a shape. Undo restores each conduit's original section,
including its other dimensions and named transect or street reference. The table's
bulk row action uses the same checked writes. Unused numeric geometry slots retain
their existing pre-fill behavior.

Choose **Transect Profile** or **Street Profile** to assign the same named section
to the selected conduits immediately. The corresponding Shape updates with the
profile. These choices appear disabled in the inline Shape list because they
need a name. Custom shape-curve replacement uses its existing dedicated editor;
a loaded custom profile's Shape and profile assignments are read-only in bulk.
When changing a transect or street section to a numeric shape, its physical full
depth becomes the starting depth in project units.

Regulator selections support immediate orifice/weir type and cross-section
shape edits. Orifices allow Circular or Rectangular Closed; weir types set the
matching rectangular, triangular or trapezoidal section. Shape changes preserve
other authored dimensions and seed newly required zero dimensions to 1 project
length unit (or side slopes of 1 for an otherwise degenerate trapezoid). Choose
a common shape before prescribing dimensions across mixed sections. Each
accepted field is one Undo step, including the individual regulator section palette.

For outlets, assigning a named rating curve enters Tabular mode while retaining
each outlet’s Head/Depth basis. Tabular type choices require a compatible bound
curve. Clearing the curve restores the corresponding Functional mode; coefficient
and exponent editing requires Functional mode. Undo retains each outlet’s complete
rating record, including inactive members. Saved INP files contain the active rating form.

For several outfalls, choose **Boundary Type** to set Free, Normal or Fixed.
A newly Fixed boundary starts at that outfall's invert elevation; an already
Fixed boundary retains its stage. Then edit **Fixed Stage Elevation** to give
the selection a common stage. Choose **Tidal Curve** or **Stage Time Series**
to prescribe a named boundary directly. These two types appear disabled in the
type list because they require a name. To remove a named boundary, choose
Free, Normal or Fixed. Flap gates and routing remain independent.

For several storage units, choose **Storage Shape**, then edit its applicable
dimensions or functional coefficients. Choose **Storage Curve** to assign a
Tabular shape; clearing an assigned curve returns to Functional. Geometric
shapes retain valid dimensions, or start each dimension at 1 if the old set is
invalid for the new shape. Dimensions require a common shape, and coefficients
apply to Functional storage. Each field commits immediately as one Undo step,
restoring the previous active geometry or named curve when undone. The table
uses the same checked writes, including its selected-row action, and its
dropdown choices commit before focus leaves the cell.

For several dividers, choose **Divider Type** to set Cutoff, Overflow or Weir.
**Diversion Curve** assigns a named Tabular curve. Edit the applicable cutoff or
minimum flow, weir height and coefficient without changing each divider's own
diversion link. A new Weir starts missing height/coefficient values at 1. The
diversion link is shown read-only in bulk because it belongs to that node.

Subcatchment selections support common **Rain Gage**, **Outlet**, **Snow Pack**
and **Aquifer** assignments. Outlet choices distinguish **Node:** from
**Subcatchment:**. A name shared by both types is rejected because SWMM files
cannot retain the distinction; rename one object before assigning that outlet.
Cycles are rejected for the whole batch. Empty snowpack/aquifer choices clear those optional assignments.
Infiltration fields apply only to their method family. Switching between ordinary
and modified variants retains the parameters; changing families starts with
editable defaults, and Undo restores the prior method and values. Defaults are
Horton rates 3/0.5 in/hr, decay 4/hr and drying 7 days; Green-Ampt suction 3 in,
conductivity 0.5 in/hr and deficit 0.3; Curve Number 75 and drying 7 days.
Rainfall lengths and rates use the equivalent values in SI projects.

For rain gages, choosing **Series Name** or **Rain File (path)** changes the
active source immediately. A direct **Data Source** choice requires that source
to be configured already. Path edits reconcile each gage's column selector;
the bulk column list shows common headers. File format, column and station
settings follow the applicable file grammar. Each source/file edit is one Undo
step that restores the related fields together. Choosing a file commits when
the file picker returns; typing a path commits on Enter or leaving the field.
The table can copy a configured path or column to its selected rows. Saving
retains the active source even when another series remains stored on the gage.

Node selections also expose **External Inflows**, **Dry Weather Flow**, **RDII**
and **Pollutant Treatment**. Open the row's editor to change individual fields
across the captured nodes, including nodes of different subtypes. Inflow and DWF
rows match by constituent; treatment rows match by pollutant. Each committed
cell has one Undo step and preserves the other members and unrelated rows on
each node. The editor lists its target names and keeps that scope when the map
selection changes. The table's **Edit … for selected nodes** action uses the same editor.

Editing an unassigned inflow/DWF row creates it with baseline/average 0,
multiplier/scale 1 and empty references, using FLOW type for FLOW and CONCEN for
other constituents. Clearing an already empty binding leaves the row unassigned.
An RDII group choice creates missing assignments with area 0; an area edit needs
an assignment on every target. An empty treatment expression clears only that
pollutant. **Remove selected row assignment** removes that keyed row across the
scope. Duplicate rows or multiple RDII assignments on a node must be resolved
individually first. **Choose or edit referenced data** opens the full existing
data-object picker; assignment choices still commit immediately. **Close**
dismisses the editor, and committed changes remain available through Undo.

Subcatchment selections expose **Land Use Coverage** and **Initial Loadings**
through the same checked editor in Properties and the table. Each row matches
by land-use or pollutant name, and a completed numeric cell edits only that row
across the listed subcatchments. Other rows and unselected objects keep their
values. Enter or leaving the cell commits immediately with one Undo step;
Escape discards an unfinished entry. The scope stays fixed if selection changes.
Coverage accepts 0–100 percent per land use. Totals above 100% are reported,
without rescaling other land uses. Initial loadings use the project's mass per
area units and accept finite nonnegative values. Zero clears the effective
assignment. The current loading editor lists ordinary pollutants; reaction
species remain outside this editor. **Close** dismisses it without another
confirmation step.

**LID Usage** edits placements across the captured subcatchments by control name.
A control placed more than once on any target is disabled in bulk; select that
subcatchment alone to edit its ordered occurrences. Entering a positive **Area**
creates missing placements with one unit and zero/empty defaults. Other fields
require a placement on every target. Each completed cell preserves the other
placements, routing settings and report-file path anchors. **Remove selected
assignment** removes only the displayed control or individual occurrence.

Conduits expose **Inlet Usage**; inlet junctions expose **Inlet Assignment**.
Edit the design, capture node, count, clogging, flow limit, depression or placement
across the listed hosts. **Add missing inlet assignments** opens two choices:
completing both design and capture node immediately creates missing rows with
one inlet, no clogging/limit/depression and automatic placement. Existing rows
retain their settings. Removing an assignment preserves inlet-junction roles
and conduit connections. Mixed Junctions selections offer an explicit inlet-only
scope and show how many ordinary junctions are excluded.

Both editors commit choices immediately and numbers/text on Enter or leaving
the cell. Escape discards an unfinished entry; **Close** dismisses the editor.
Each change is one Undo step, including after closing. Invalid references or
incompatible inlet designs reject the entire captured batch. Properties and
the Attribute Table share these writes, and map connectors refresh immediately.

Node selections also expose the **2D Coupling** fields. Change the structure,
coupleable switch or one optional parameter across the selected nodes. Switching
structure clears that node's previous structure overrides; choosing **Use class
default** removes its authored coupling row. Blank parameters restore inheritance,
while an explicit coefficient preset such as **DEFAULT** remains authored.
Compatible choices are intersected for mixed node classes. The native rules
allow explicitly enabling a virtual/inlet junction whose class default is off.
Each completed field is one Undo step and preserves the other parameters.
Dimensions retain project units; tailwater relaxation uses seconds. Existing
mesh-map area/coefficient overrides retain their precedence. Geometry mapping
remains separate: use **Remap 1D–2D** when the mapping itself needs to change.

Storage selections expose **LID Control**, **LID Initial Saturation (%)** and
**LID Bottom Ownership**. Assign a control first; newly assigned nodes start
with zero saturation and current bottom behavior. Existing assignments retain
their saturation and bottom mode. A control change updates that node's stack
depth and anchored outlet elevations. Saturation and ownership edits retain
those geometries. Clearing a control removes its outlet anchors and resets
saturation/ownership, retaining the current depth and hydraulic offsets.
Undo restores the complete assignment, original depth, anchors and offsets.
Nodes sharing the same control retain their own settings.

Bottom ownership changes preserve the bed contacts. **Mesh aquifer** requires
an enabled mesh aquifer and an authored, enabled bed with positive contact area;
configure contact geometry through **Bottom Connection** first. Authoring this
ownership does not qualify a connection for execution. Mesh/contact or control
stack changes invalidate old dependent edits. Rejected batches change no targets.
The same checked writes serve individual Properties fields and table edits;
choices commit on activation and numeric/text fields on Enter or focus-out.

**Groundwater** also opens a shared field editor for the captured subcatchments.
Choose a receiving node or edit one flow-law parameter or LATERAL/DEEP expression;
the other fields retain each target's own values. Each completed field commits
immediately with one Undo step, and **Close** dismisses the editor. Lengths and
coefficients retain project groundwater input units. Dsw zero uses the receiving
node's live depth; Egwt `*` follows its invert, while `-99` is a literal elevation.
Empty expression text clears only that expression. Native expression validation
and completion use the engine's groundwater vocabulary.

Every target needs an assigned aquifer. Choose a receiver first if a record has
none; other exchange edits require a receiver so the records can be saved.
Aquifer assignment remains in the existing Properties picker. Aquifer bindings
and optional Ebot/Wgw/Umc initial-condition overrides are preserved. The latter
and runtime groundwater state are not edited by this page. A removed target or
reference invalidates the captured context; reopen the editor after an aquifer
definition rename. Subcatchment and receiving-node renames follow their objects.

**Initial Quality**, network **Heat** fields and per-object **User Flags** also
support immediate bulk edits. Mixed spatial selections offer **Model objects —
common properties** for compatible fields, including flags shared by nodes,
links, subcatchments and rain gages. Other counted scopes remain available.
Choices commit when selected; numbers and text commit on Enter or leaving the
cell. Each field creates one Undo step and refreshes the other views.

Initial Quality includes pollutants, configured reaction species, and Water Age
or Temperature while their process option is enabled. Blank removes the selected
override; zero remains explicit. File-supplied initial values retain their source
owner and are read-only here. Heat overrides retain global/tag inheritance when
cleared. Their labels show SI units in both US and SI projects; conduit bed fields
are unavailable on other link types.

Boolean flags distinguish **Unset**, **NO** and **YES**. Numeric zero and authored
empty string values remain distinct from missing assignments. Blank text or
**Clear selected override** removes an assignment. Owned **Initial Quality** and
**User Flags** editors use the same immediate field operations; **Close** dismisses
them and Undo remains available afterward. Flag definitions stay in their global
editor. Reopen a captured selection after changing definitions or process options.

Drag the divider between column headers to change a width, including the last
column. Double-click a divider to fit the column to its contents. Column widths
are remembered per category (and per mesh element kind) across sessions, and
every column is at least as wide as its header.

#### Dynamics columns

The right-hand block reads the project's **active 1D results layer** — the run
chosen in the Analysis toolbar's Results combo — not the editing engine. It
re-reads when that selection changes, so re-running into the same `.out`
refreshes in place. With no results available the cells show NULL. A measured zero remains zero.

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

\fig{11_dynamics_columns.png, The right-hand dynamics block of the Conduits table after a run}

### Sorting

Click a column header to sort; click again to reverse. Sorting is numeric for
numeric columns, so the smallest value really is the first row. Sorting is a
view operation — the model's row order (which matches the Object Browser) is
untouched, and the copy/export honour the sort you see.

Missing or inapplicable values display as muted **NULL** and sort last in both
directions. Hover over one for its reason. Zero, NO/false and applicable
inherited defaults remain real values. Copy and CSV/TSV export write NULL as an
empty field; literal text such as `NULL` in an imported file remains text.

Use `couplingCd IS NULL` to find uncoupled vertices, `couplingCd >= 0.65` to
compare assigned coefficients, or `couplingArea BETWEEN 2 AND 10`. Reference
fields also support queries, for example `[Coupled Node] IS NULL`. Numeric
comparisons exclude NULL rows; use `IS NULL` or `IS NOT NULL` explicitly.


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
| Numbers | `100`, `3.14`, `-2`, `+.5`, `1e-3` |
| Strings | Single-quoted — `'Junction'`; double embedded quotes: `'O''Brien'` |
| Comparison | `=` `!=` `<>` `<` `<=` `>` `>=` |
| `LIKE` | Case-insensitive pattern match on a string column. `%` matches any sequence including none; `_` matches exactly one character |
| `IN` | Match any of a list — `Type IN ('Junction','Outfall')` |
| Negation | `NOT (condition)`, `Name NOT LIKE 'OUT%'`, `Name NOT IN ('J1', 'J2')` |
| Ranges | `Area BETWEEN 0.5 AND 2` includes both endpoints; `Area NOT BETWEEN 0.5 AND 2` excludes the range. `Length BETWEEN 10 AND 100` works as typed; when you quote the full header with its unit, use brackets — `[Length (ft)] BETWEEN 10 AND 100` |
| Missing values | `Field IS NULL`, `Field IS NOT NULL`; an empty string is a value, not NULL |
| Booleans | `TRUE`, `FALSE`; enumerations use the displayed labels, for example `'Yes'` |
| Combining | `AND`, `OR`, `NOT`, grouped with `( )`; precedence is comparison, `NOT`, `AND`, then `OR` |

A malformed clause is reported in the status label with the error position,
and the new clause is not applied. Any previous valid filter and selection
remain in place. Correct the clause and press **Apply**, or use **Clear** to
remove the previous filter.

\fig{11_query_error.png, Name LIKE without a string is rejected while the previous three matching conduits remain visible}

Unknown columns and compound editor cells are reported as errors, preserving
any previous filter and selection. Query scalar fields using a column header,
label or schema key. Numbers use the table's displayed units. String equality
is case-sensitive; `LIKE` is case-insensitive. NULL comparisons remain unknown
even under `NOT`, so use `IS NULL` to find missing values.

Suggestions appear as you type column names and operators. Use **Ctrl+Space**
to open suggestions at the cursor, arrow keys to choose, and **Enter** to
insert. Completion replaces the current token and preserves the rest of the
query. Fields with spaces are quoted automatically. Suggestions update when
the table source or schema changes. An optional leading `WHERE` is accepted;
this bar takes a predicate, not a full `SELECT` statement.

The query bar works on every source, including the read-only ones — the
predicate evaluator runs over rows, not over the engine. SWMM objects, mesh
vertices/edges/cells, data objects and GIS features support query selection.
CSV/TSV sources support filtering and export; applying a query there preserves
the current map selection.

#### Build an exclusion query with suggestions

1. Open **Analysis → Tabular View** and choose **Conduits** in **Category:**.
   Check the unit in the **Length** header; query numbers use that unit.
2. Click the query bar and begin typing `Na`. Choose **Name** from the
   suggestions with the arrow keys and press **Enter** to insert it. You can
   also press **Ctrl+Space** at the cursor to request suggestions.
3. Complete the clause below. It excludes names beginning with `TEMP` and
   includes lengths from 10 through 100, including both endpoints:

   ```sql
   Name NOT LIKE 'TEMP%' AND Length BETWEEN 10 AND 100
   ```

4. Choose **Replace** in the **Selection:** controls and press **Apply**.
   The table shows matching rows, the status reports the match count, and the
   corresponding conduits are selected. Click **Length** to sort the matches.
5. Use **Add**, **Subtract** or **Intersect** before applying another query to
   combine its matches with the current selection. **Clear** restores all table
   rows and keeps the selection.

In the bundled **Site Drainage** example, the clause above matches C11 and C7.
Their lengths are 89 and 95 ft. The ascending arrow in **Length (ft)** shows
the sort direction, and the highlighted rows show the selected matches.

\fig{11_query_exclusion_sorted.png, NOT LIKE and BETWEEN select two of eleven conduits; Length is sorted ascending in feet}

**Enter** accepts a highlighted suggestion while the suggestion list is open;
with the list closed, **Enter** applies the query. To change part of an existing
clause, place the cursor in that field or keyword and request suggestions;
accepting one preserves the text after the token.

\fig{11_query_completion.png, Suggestions after typing Len include the quoted Length header; use arrow keys and Enter to insert a field}

Opening suggestions does not apply the unfinished clause: the previous matches
stay visible until you press **Apply** or apply a complete query with **Enter**.

To exclude several names, use `Name NOT IN ('C1', 'C2')`. To negate a group,
use `NOT (Name LIKE 'TEMP%' OR Length < 10)`. For a field that can be missing,
use `Field IS NULL` or `Field IS NOT NULL`, replacing `Field` with a scalar
column from the chosen source. `NOT (Field = 5)` does not include missing
values; use `Field IS NULL OR Field != 5` when that is the intended selection.

#### Query another source

Choose the desired source in **Category:** before building the clause.
Suggestions then show that source's fields. Use the headers in the current
table; a field from the previous source may not exist in the new one.

| Source | Applying a valid query |
|---|---|
| SWMM object categories | Filters rows and updates object selection using the chosen selection mode |
| Data-object categories | Filters rows and selects the matching data objects |
| Mesh vertices, edges or cells | Filters rows and selects the matching elements of that mesh |
| GIS feature layers | Filters rows and selects features in that layer; field names containing spaces are quoted by completion |
| CSV/TSV tables | Filters rows for inspection, copying and export; the map selection stays in place |

Only sources offered by the picker have attribute queries. Raster pixels and
rendering-only sublayers do not have a row table here. If **Apply** reports a
syntax or field error, correct the reported position or choose a field from
suggestions and apply again. The last valid filter and selection stay in place
until the corrected clause succeeds or you press **Clear**.

### Selection

The table shares the project's selection both ways: picking rows selects
the objects everywhere, and a selection made on the map or in the Object
Browser highlights the matching rows.

Double-clicking a **row number** in the left-hand strip zooms the map to that
element (plus the rest of the selection). It sits on the vertical header, so
the double-click-to-edit affordance on cells is untouched.

The **Selection:** radios decide what **Apply** does with the rows matched by
the query. The sequence is: type the query, choose **Replace**, **Add**,
**Subtract** or **Intersect**, then press **Apply**. Typing a query leaves the
map selection unchanged; **Apply** filters the rows and then applies the chosen
radio to the matched rows:

| Radio | Effect |
|---|---|
| **Replace** | Replace the current selection with the matched rows |
| **Add** | Union the matched rows into it |
| **Subtract** | Remove the matched rows from it |
| **Intersect** | Keep only rows that are in both matched and selected |
| **Invert** | Replace the selection with every row in this category that is *not* selected — ignores the query |

**Add** and **Subtract** act on the existing map selection: Add gives the
union of the prior selection and the matched rows, Subtract removes the matched
rows from it. With nothing selected, Add gives the same result as Replace and
Subtract removes nothing. Select the objects on the map (or with a first Replace query), then
run the second query with Add or Subtract. The selection ops always run
against the full population of the category; the "show selected only" filter
does not narrow them.

\figtodo{11_query_between_add.png, A BETWEEN query on Conduits with Add selected — matched rows highlighted on the map alongside the previous selection}

\fig{11_query_and_selection.png, Name LIKE 'C1%' matches C1; C10 and C11 in the eleven-conduit drainage example; Replace selects the three matches}

To reproduce this example, open a copy of the T1 drainage model, choose
**Conduits**, select **Replace**, enter
\htmlonly<code>Name LIKE 'C1%'</code>\endhtmlonly and press **Apply**.
The status reads **3 of 11 matched** and those three rows become selected.
The percent sign includes the unsuffixed C1 as well as C10 and C11. Use
**Clear** to remove the table filter; clearing the filter does not clear the
selection.

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
edit, and it refreshes the Properties panel and the Section View for the same
object. The reverse holds too.

Two edits are deliberately atomic: setting a rain gage's **Rain File** path also
commits the column selector and the file-format flip that a column implies, as
one undo step — otherwise undo would restore the path and keep a stale column.

### Pasting cells and node coupling settings

In a SWMM object table, select the first target cell and press **Ctrl/Cmd+V**, or
right-click and choose **Paste**. Paste values without a header row.

- One value fills that column for all selected rows; with one row selected it
  edits one cell. Boolean columns, including **2D Coupleable**, use **YES/NO**
  choices and accept pasted Yes/No text without regard to case (or 1/0).
- A tab-separated spreadsheet block fills visible columns from the current
  column in their displayed order. Hidden columns are skipped. When the selected
  row count matches the block height, values follow those rows in visible order;
  otherwise the block starts at the current row. Sorting and filtering are respected.
- Read-only cells, unsupported dedicated editors, malformed blocks and invalid
  enumeration/numeric input report an error. Node-specific validation may reject
  individual cells; the message identifies the first refusal. Accepted cells
  remain one **Undo** step. Paste inside an open text editor follows normal text editing.

The **2D Coupling** group in junction, divider and storage tables contains
eligibility, structure, diameter, perimeter, opening/chamber areas, lid fraction,
crest, law, coefficient preset and four coefficient overrides. Outfalls expose
eligibility, structure, spread radius and tailwater relaxation. Numeric override
cells display `NULL` when no override is authored; clear the editor to restore
inheritance. Enum overrides have **(inherit)**. Changing structure or disabling coupling clears its parameters,
so paste the structure first and geometry second. Undo restores the whole prior
node configuration. Eligibility does not create or relocate a spatial mesh map.
See @ref manual_node_coupling_settings for defaults, units, applicability and the
meshing/remapping workflow.

### Applying one value to many rows

Select several rows, right-click an editable cell, and pick one of:

- **Apply this "…" value to N selected rows** — copies the clicked cell's value;
- **Apply "…" value to N selected rows…** — prompts for a value with the input
  widget the column's editor kind implies.

The whole batch is one undo step, and none when no row changes (every selected
row already held the value). Rows whose cell is not editable are skipped —
so bulk-applying a boundary Stage touches only the rows already set to a
Specified Stage type. On a picker column (Coupled Node, Time Series, Rating
Curve) the prompting flavour opens the same picker as the cell, so "couple
these twelve vertices to J1" is a single gesture.

**Properties** also supports editing several selected objects together. Mesh
vertices, edges and cells offer their existing field editors. Network nodes,
links, subcatchments and rain gages offer shared numeric attributes, tags and
supported choices or recording intervals. Differing values show **Mixed**.
Choose a value, or enter text or a number and press **Enter** or leave the field.
Each committed field updates all selected objects immediately as one undo step.
**Escape** discards unfinished input. Structured records use their dedicated editors.

For mixed node or link subtypes, Properties offers their common properties and
counted subtype groups. A shared Tag can update selected conduits and pumps
together; choose **Conduits** to edit their Manning’s n. For selections containing
different object families or meshes, choose a counted group above the Properties
tree. A field applicable to only some objects in the active group is read-only. For mesh boundary conditions, choose the offered
exterior-edge scope if the selection also contains interior edges. See
*Editing values* in Chapter 19.

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
column headers as you see them, so a re-sorted or re-ordered view exports the
way you see it, and the "show selected only" filter is honoured.

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

Selecting one of the `△ Mesh …` entries lists that mesh's elements;
**Mesh 2D → Edit in Table** opens the entry for the element kind selected on
the map. Rows are read straight from the mesh and written through the same
undoable edits as **Properties** uses for one clicked element, so even a
multi-million-cell mesh opens quickly.

| Table | Read-only columns | Editable columns |
|---|---|---|
| **Vertices** | Index, X, Y, Marker | Elevation, Tag, Coupled Node, Coupling Cd, Coupling Area |
| **Edges** | Edge (`cell:edge`), Boundary, Length (map units), Segment | Conveyance; and on boundary edges BC Type, Wall drag Cw, Stage, Friction Slope, Flow, Time Series, Rating Curve, Group |
| **Cells** | Index, corner count and vertex IDs, area, centroid, ownership and infiltration source/destination | Tag, plus one column per per-cell parameter |

Coordinates are **not** editable here — move a vertex on the map. The same goes
for the other derived columns (edge length, cell area, centroid): they are
computed from the geometry, so an edit would have nowhere to write.

The per-cell parameter columns follow the mesh's parameter list, so a new
parameter appears here the moment it is added. Today they are Manning's n and Initial Depth
(`[2D_TRIANGLES]`), the per-cell infiltration set — Infiltration Method plus its
positional parameters (`[2D_INFILTRATION]`) — and a greyed-out 2D groundwater
block (Saturated Conductivity, Aquifer Thickness, Porosity, initial unsaturated
and saturated depths). For an active model mesh, right-click a cell and choose **Edit groundwater parameters…**.
The assignment preview starts with the invoking cells and shows their current and proposed values.
Use **Edit surface infiltration…** for the full method, parameters, destination and write scope.
A right-click inside a selection targets the selected rows. A right-click outside it targets the clicked row.
In the Properties dock, expand a group to inspect or edit the picked element. Choose **Edit…** beside **Infiltration** or **Groundwater** to open its assignment form.

A cell showing **NULL** has no applicable or available scalar value:

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

In either **Properties** or the **Attribute Table**, click the **BC Type** value on an exterior edge
and choose a type from the list. Its parameter becomes editable immediately. Click that value to
enter the boundary data and press Enter to apply it.

| BC Type | Live parameter |
|---|---|
| Wall | Wall drag Cw (zero for free slip) |
| Normal Depth | Friction Slope (signed: positive drains, negative feeds the domain, zero acts as a wall) |
| Specified Stage (constant) | Stage |
| Specified Stage (time series) | Time Series |
| Specified Flow (constant) | Flow |
| Specified Flow (time series) | Time Series |
| Rating Curve | Rating Curve |

Right-click an edge and choose **Edit surface boundary condition…** to edit its type and parameter. The dialog counts eligible boundary edges. Each committed field updates those edges immediately as one undo step, preserving the group and conveyance on each edge. **Close** dismisses the editor; use **Undo** to revert a committed change. In Properties, choose **Edit…** beside **Boundary condition** for a picked boundary edge, including the fourth edge of a quad.

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
its own form rather than one column at a time: pick **Infiltration** in the
**Mesh 2D** tab's **Process** picker, select cells on the map with **Cells**,
then click **Infiltration to Selection** (**Model → Mesh ▸ Assign Infiltration
to Selection…**; enabled only while cells are selected).

Two write targets:

| Target | Writes | When it is offered |
|---|---|---|
| **Per-cell overrides** | one `[2D_INFILTRATION]` row per cell | always |
| **Region tag** | one `[2D_INFILTRATION_DEFAULTS]` row | only when every selected cell shares one non-empty tag |

The region route is the in-map way to edit a *default* instead of minting
overrides, so a later region-level edit still reaches the cells. Undo restores
the cells' original **provenance** — undoing an assignment made over an
inheriting cell restores inheritance rather than leaving a per-cell override
carrying identical numbers.

Parameter fields are masked by the chosen method, and destinations the engine
does not accept in this release are shown disabled — the same two rules the
attribute table and the region-defaults table follow.

\figtodo{11_assign_infiltration_dialog.png, The Assign Infiltration to Selection dialog with Green-Ampt parameters and the region-tag option}

### Data-object tables

Pollutants, land uses, aquifers, streets and inlets each get a table with one
row per object, so you can set a value on many of them at once — for example,
the decay coefficient of every pollutant, or the road roughness of every street
section — with **Applying one value to many rows** above.

- The table edits the same objects as the dedicated editor dialogs. An edit in
  either one shows up in the other straight away, and each cell edit is one
  **Edit → Undo** step.
- The **Name** column is read-only. Rename an object in its editor or the
  Object Browser, because a rename also updates every object that refers to it.
- Reference columns (a pollutant's *Co-Pollutant*, an aquifer's *Upper Evap.
  Pattern*, an inlet's *Capture Curve*) are drop-downs listing the objects that
  exist, plus `(none)`.
- On an inlet row, fields the inlet's type does not use show `NULL` and cannot be
  edited (for example, curb fields on a GRATE inlet).
- Right-click a row → **Edit "<name>" in &lt;Editor&gt;…** opens the full editor on
  that object.
- Selecting rows selects those objects, so the Properties panel follows along.
  These objects have no map location, so **Zoom to selected**, **Change Type**
  and **Delete** do not apply here.

Curves, time series, patterns, unit hydrographs, transects, control rules,
snowpacks and LID controls are lists of values rather than single records, so
they are edited only in their own editors (\ref manual_data_objects).

### GIS feature layers

An imported vector layer's table lists its OGR features, keyed by feature id.
It is **read-only** — a shapefile is not part of the model, so there is
nothing to write to — but sorting, the query bar, copy, export and two-way
selection all work.

To turn features into SWMM objects, use **Model → Import Feature Layer…**; see
\ref manual_map_editing.

### Delimited-text (tabular) layers

A CSV or TSV added with **File → Import → Add Delimited Data…** appears as
`▾ Table: <name>`. It is read-only by design — tabular layers are observation
data, not model objects. The query bar still filters it, and copy
and export behave as elsewhere. Selection ops are no-ops for this source: the
layer carries no SWMM object references.

These layers are how observed series reach the calibration and comparison
plots — see \ref manual_time_series_plots.


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
- A NULL initial-quality cell is not zero — it means "no override", and the
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
