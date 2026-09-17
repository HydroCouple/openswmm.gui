@page manual_data_objects 17 — Time Series, Curves, Patterns and Calibration Data

## What you'll do

Create and edit the four families of *data objects* the rest of the model
refers to by name: **time series** (`[TIMESERIES]`), **curves** (`[CURVES]`),
**time patterns** (`[PATTERNS]`), and the observed data used for calibration
comparison plots. Each has a dedicated three-pane editor — a list of every
object of that kind, a grid, and a live chart — and every edit goes through the
project's undo stack so the Object Browser, the Properties panel and any other
open view stay in step.

\figtodo{17_data_objects_menu.png, The Model → Data Objects submenu}

## Where to find it

| Object | Menu | Object Browser |
| --- | --- | --- |
| Time series | **Model → Data Objects → Time Series…** | *Time Series* category |
| Curves | **Model → Data Objects → Curves…** | *Curves* category |
| Time patterns | **Model → Data Objects → Time Patterns…** | *Patterns* category |

The same three commands sit on the ribbon **Model** tab. None of them carry a
default shortcut. Double-clicking an item in the Object Browser opens its
editor bound to that item; right-clicking a category offers **New…**. Every
picker elsewhere in the application that takes a series, curve or pattern name
— the inflow editor, the DWF editor, storage and pump properties, evaporation,
the outfall stage series — can also open the matching editor inline and hand
back the name you created.

All three editors are **non-modal** with no OK/Cancel: edits commit live and are
undone with **Edit → Undo** (or the Undo/Redo buttons in the editor's own
toolbar). They remember their window geometry, splitter positions and plot-style
toggles between sessions.

\videotodo{Creating a rainfall time series from a CSV file then referencing it from a rain gage}

## Step-by-step

### The time series editor — `[TIMESERIES]`

**Model → Data Objects → Time Series…** opens a three-pane editor: the list of
every series in the project on the left, the point grid in the middle, and an
interactive chart on the right.

\figtodo{17_timeseries_editor.png, The Time Series editor — series list; point grid and chart}

**List pane.** A filter box narrows the list by case-insensitive substring.
**New**, **Delete** and **Rename** act on the selection; renaming is also
available in place (the registry enforces case-insensitive uniqueness).
Selecting a different series rebinds the grid, the chart and the source card.

**Creating a series.** **New** shows a create card above the splitter with a
**Name** field (validated for uniqueness — a duplicate name disables **Create**
and explains why) and a **Source** combo:

| Source | What it means |
| --- | --- |
| **Inline (table in .inp)** | Points are written into the `[TIMESERIES]` section of the `.inp` |
| **External file (FILE "path")** | The `.inp` carries a `name FILE "path"` reference and the file supplies the points |
| **Geopackage observed series** | Present in the combo but not implemented in this build — the pane shows a placeholder |

**Grid.** One row per point. When several sibling series share exactly the same
time vector the grid widens to `Time | V0 | V1 | …`, one value column per
series — the layout you get when one CSV feeds several series. When the time
vectors differ, the grid falls back to `Time | Value` for the selected series
only. Times must be strictly increasing; a mutation that would break that is
rejected and the reason appears in the status bar.

| Toolbar action | What it does |
| --- | --- |
| **Add Row** | Insert a point (**Insert** key) |
| **Delete Rows** | Delete the selected rows (**Delete** key) |
| **Copy** | Copy the selected rows as TSV (**Ctrl/Cmd+C**) |
| **Paste** | Paste rows from an Excel / CSV / TSV clipboard (**Ctrl/Cmd+V**) |
| **Undo** / **Redo** | The project undo stack |

Paste is the quickest way to get a series in from a spreadsheet: copy two
columns in Excel and paste into the grid. Rows whose time duplicates an existing
point or whose text will not parse are skipped, and the status bar reports how
many were pasted and how many were skipped. The same four commands are on the
grid's right-click menu.

**Chart.** The right pane is an interactive chart with **Select**, **Pan**,
**Zoom In**, **Zoom Out** and **Zoom to Extent** modes, plus two editing modes:

| Mode | What it does |
| --- | --- |
| **Edit Points** | Y-drag a point to change its value (X is locked so monotonicity survives); Shift-drag rubber-bands a multi-selection; dragging a selection translates every value; the right-click menu offers *Insert here*, *Delete selected* and *Clear selection* |
| **Rotate** | Rotates the selection about a pivot — set the pivot time, pivot value and angle in the numeric panel and press **Apply**; **centroid pivot** uses the selection's centroid |
| **Scale** | Scales the selection about an anchor — anchor time, anchor value, X factor and Y factor, then **Apply** |

With no selection, Rotate and Scale act on every point. The numeric panel
appears only while the corresponding mode is active. The **Snap** toggle snaps
inserted and dragged times to the reporting step.

The status bar under the chart shows the point count, the value range and the
first and last date-time, and carries transient messages such as a rejected
edit.

\figtodo{17_timeseries_chart_edit.png, Editing time-series points on the chart with a rubber-band selection}

**Source card — inline versus external file.** The card under the toolbar
switches the bound series between **Inline**, **External file** and
**Geopackage**:

| Control | What it does |
| --- | --- |
| **File** | The linked file, shown relative to the project directory where possible; the tooltip gives the resolved absolute path |
| **Browse…** | Pick a `.csv`, `.tsv`, `.tsf`, `.dat` or `.txt` file |
| **Column** | Which column of a multi-column file this series binds to; populated from the file's header row |
| **Reload** | Re-read the file from disk |
| **Detach → Inline** | Copy the currently loaded points into the project and break the file link — **one way** |

The status line under the card reports the load: *Loaded at …*, *⚠ File changed
on disk — Reload to pick up*, or *⚠ File not found*. While a series is
file-backed the grid and the chart are **read-only** and Add / Delete / Paste /
Rotate / Scale all refuse with an explanatory message — use **Detach → Inline**
first if you want to edit the numbers.

A file with no header row can only bind its first column; the combo says so and
offers `(no header — single column)`. Behind the scenes a multi-column
reference is written as a `path:column` token in the `.inp` — you never type the
colon, the editor composes and splits it.

\figtodo{17_timeseries_source_card.png, The source card linking a series to a CSV column}

**Time mode.** For inline series a **Time mode** row selects how times are
written to the `.inp`:

| Mode | Written as |
| --- | --- |
| **Absolute (dated)** | `Name  MM/DD/YYYY  HH:MM  value` |
| **Relative to simulation start** | `Name  H:MM  value` — elapsed hours from the simulation start |

A badge explains what the current choice means and warns when a series saved as
elapsed time will re-anchor if the simulation start date changes. Switching to
relative times is refused when the project has no start date or when the first
point precedes it. A file the model read with a mix of elapsed and dated rows
shows a read-only **Mixed (elapsed prefix + dated rows)** entry.

The engine reads the legacy row forms an external file may use — elapsed `H:MM`,
decimal hours, dash dates and month-name dates — and a `FILE` reference is
preserved verbatim on save, so round-tripping a deck does not rewrite it.

### The curve editor — `[CURVES]`

**Model → Data Objects → Curves…** opens the three-pane curve editor: list,
X/Y grid, and a live preview chart. The grid's column headers and the chart's
axis titles follow the curve type.

\figtodo{17_curve_editor.png, The Curve editor with a storage curve and its preview chart}

| Type | X | Y |
| --- | --- | --- |
| **Storage** | Depth | Surface Area |
| **Diversion** | Inflow | Diverted Flow |
| **Rating** | Head | Flow |
| **Shape** | Depth/Full | Width/Full |
| **Control** | Variable | Setting |
| **Tidal** | Hour | Stage |
| **Pump 1** | Volume | Flow |
| **Pump 2** | Depth | Flow (on/off) |
| **Pump 3** | Head | Flow (continuous) |
| **Pump 4** | Depth | Flow (continuous) |
| **Pump 5** | Depth | Flow (variable speed) |

The list pane has a **Filter** combo (**All types**, or one type) alongside
**New**, **Rename** and **Delete**; the right-click menu offers the same.
Deleting a curve warns that model objects referencing it will lose the
reference.

Points must be strictly ascending in X. **Add Row** and **Delete Row(s)** work
on the grid, **Copy** and **Paste** move X,Y pairs through the clipboard (a
paste reports how many rows were taken and how many lines were skipped), and
the right-click menu carries all four. Editing a Y value leaves X alone;
editing X is validated against the neighbouring points.

The chart toolbar has **Fit** (zoom to extent, **F**), **Zoom in**
(**Ctrl++**), **Zoom out** (**Ctrl+-**), **Pan** and **Edit points** (**E**),
plus **Lock X** and **Lock Y** to constrain a vertex drag to one axis.

Changing a curve's **Type** rewrites the grid headers and axis labels but keeps
the points.

\figtodo{17_curve_type_combo.png, The curve type combo with the eleven SWMM curve types}

### The time pattern editor — `[PATTERNS]`

**Model → Data Objects → Time Patterns…** opens the pattern editor: list,
factor table, and a step-line preview.

\figtodo{17_pattern_editor.png, The Time Pattern editor with an hourly pattern and its step-line preview}

| Type | Factors | Row labels |
| --- | --- | --- |
| **Monthly** | 12 | Jan … Dec |
| **Daily** | 7 | Sun … Sat |
| **Hourly** | 24 | 00:00 … 23:00, weekdays |
| **Weekend** | 24 | 00:00 … 23:00, weekends |

The factor table has one **Factor** column; row labels come from the type.
Negative factors are rejected. A **Normalize to sum =** target with a
**Normalize** button rescales every factor so the set sums to the target — the
usual case being 1.0 for a distribution, or the factor count for a set of
multipliers averaging one. The status line shows the sum, the mean and the
count.

The list pane offers a **Search patterns…** filter and **New**, **Duplicate**,
**Rename** and **Delete**; each entry shows its type after the name.
**Duplicate** clones the selected pattern under a new name — the fastest way to
build a variant.

The preview is a step line on the same interactive chart used elsewhere: zoom,
pan, zoom-to-extent, **Copy chart to clipboard (PNG)**, **Export chart…** (PNG
or SVG), and a plot-style menu for markers and step-versus-smooth. In
**Edit vertices** mode a vertical drag changes a factor and a horizontal drag
swaps two slots.

Creating a pattern shows the same create card as the other editors, with a
**Type** combo (**Monthly (12)**, **Daily (7)**, **Hourly (24)**,
**Weekend (24)**) and a uniqueness-validated **Name**.

### Calibration data and observed series

Observed measurements are compared against simulated results in the comparison
plots — see \ref manual_time_series_plots. Observed data is loaded there
directly: the comparison plot dialog's **Load Observed…** adds a CSV, TSV or
`.dat` file as an extra run source whose series are labelled by their column
headers, and the fit statistics are computed against it.

A separate **Calibration Data** dialog exists in the codebase — a table of
(object kind, object name, attribute, observed file, column) rows persisted in
the project — but it is **not reachable from any menu in this build** and
nothing currently reads the bindings it saves. Register observed data through
the comparison plot instead.

\figtodo{17_observed_comparison.png, A comparison plot with an observed series loaded alongside a simulated one}

### External file paths and project portability

Every editor that takes an external file — the time series source card, the
Simulation Options file slots, the rain gage rainfall file — uses the same path
picker. It **stores** the absolute path so the engine can always open the file,
but **displays** it relative to the project directory when the file lives at or
under it, with the resolved absolute path in the tooltip. A path that cannot be
made relative is shown absolute with a warning tooltip.

When the project is saved elsewhere, relative references are rewritten against
the new destination and absolute ones are rebased. See \ref manual_projects for
the project-portability options and \ref manual_file_formats for the file types
involved.

## Tips and gotchas

- **These editors have no OK button.** Edits are live. Use **Edit → Undo** or
  the editor's own Undo button to back one out; closing the window keeps
  everything you did.
- **A file-backed series is read-only.** If you need to change the numbers,
  press **Detach → Inline** first — it is a one-way conversion.
- **Reload after editing the CSV.** SWMMVis notices that the file's timestamp
  moved and says so, but it does not re-read it automatically.
- **Pick the column, do not type a path.** Multi-column references are composed
  as `path:column` by the editor; a hand-typed colon in a file name will not do
  what you expect.
- **Relative time series re-anchor.** A series saved as elapsed time follows the
  simulation start date. If you change the start date, dated series stay put and
  elapsed ones move.
- **Paste is the fastest import.** Both the time series grid and the curve grid
  accept a two-column clipboard from Excel; there is no separate import wizard.
- **Curve X values must ascend strictly.** A paste with a duplicate or
  out-of-order X is rejected with a message rather than silently reordered.
- **Normalize before you trust a pattern.** A monthly pattern meant as a
  distribution should sum to 1.0; one meant as multipliers should average 1.0.
  The **Normalize** button does whichever you set as the target.
- **Deleting a data object does not clean up references.** The delete
  confirmation warns you; check the objects that used it afterwards.

## Related

- \ref manual_object_browser — the Object Browser and the Properties panel that reference these objects
- \ref manual_hydrology — rain gages, unit hydrographs and the series they read
- \ref manual_hydraulics — storage, pump, rating and control curves; inflows and DWF patterns
- \ref manual_climate — evaporation, temperature and wind series
- \ref manual_water_quality — the `EXT` build-up series reference
- \ref manual_simulation_options — external file slots and the reporting step that Snap uses
- \ref manual_time_series_plots — comparison plots, observed data and fit statistics
- \ref manual_projects — project portability and relative paths
- \ref manual_file_formats — the file types the editors read
