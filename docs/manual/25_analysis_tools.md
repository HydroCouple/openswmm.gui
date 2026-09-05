@page manual_analysis_tools 25 — Analysis Tools: Flow Balance, Travel Time, Rainfall Visualisation, 2D Cell Time Series

## What you'll do

Use the smaller, single-purpose analysis tools: quick flow and travel-time
summaries over an upstream or downstream subnetwork, a dedicated window for
comparing every rain gage's record, per-cell time series picked straight off the
2D mesh, and the statistics dashboard with its frequency histogram.

\figtodo{25_analysis_ribbon.png, The Analysis ribbon tab with the Report; Plots and Network Analysis groups}

## Where to find it

| Tool | Menu | Ribbon | Needs |
|------|------|--------|-------|
| **Flow Balance Downstream / Upstream** | **Analysis** | **Analysis → Network Analysis** | A selection **and** results |
| **Travel Time Downstream / Upstream** | **Analysis** | **Analysis → Network Analysis** | A selection **and** results |
| **Rainfall Visualization…** | **Analysis** | — | Rain gages in the model |
| **Pick 2D Cells** | — | The **Mesh 2D** toolbar | A 2D results layer |
| Mesh **Select Vertices** / **Select Edges** | **Model → Mesh** | **Mesh 2D** | A mesh |
| **Summarize Results** | **Analysis** | **Analysis → Report** | An active 1D results layer |

Rainfall Visualization is also reachable from the Object Browser's **Rain Gages**
context menu and from a rain gage property editor's **Plot Rainfall…** button.

## Step-by-step

### Flow Balance — upstream and downstream

**Flow Balance Downstream** and **Flow Balance Upstream** answer "how much water
crosses the boundary of everything downstream (or upstream) of what I have
selected?"

| Step | What happens |
|------|--------------|
| Inputs | The current selection — selected **nodes** seed directly; a selected **link** seeds both of its end nodes. The **active 1D results layer** supplies the flows |
| Computed | A breadth-first walk over the routing graph from the seeds, following link direction (downstream) or reversed direction (upstream), collects the subnetwork. Links with both ends inside are *interior*; links with exactly one end inside are *boundary* links |
| Reported | Node count, boundary-link count, and the summed **Inflow**, **Outflow** and **Net** across the boundary, in the project's flow units |

The numbers are read from the **final time step** of the results file — this is a
snapshot balance at the end of the run, not an integral over the simulation. The
message box says so.

With nothing selected the tool asks you to select a node or a link; with no
results it asks you to run a simulation or load a `.out`.

\figtodo{25_flow_balance_result.png, The Flow Balance summary reporting subnetwork size and boundary inflow-outflow}

### Travel Time — upstream and downstream

**Travel Time Downstream** / **Upstream** build the same subnetwork from the same
seeds, then sum *length ÷ velocity* over its **interior** links at the final time
step, skipping links with no length or with a velocity at or below zero.

The result reports the number of flowing conduits counted and the **total in-pipe
travel time in minutes**. Links that are not flowing at that instant are excluded
entirely, so the count tells you how much of the subnetwork the number actually
covers.

Both tools present their answer in a message box; they do not colour the map or
write a table. To *see* the subnetwork instead, use **Select Upstream** /
**Select Downstream** (\ref manual_selection), which selects every node, link and
subcatchment in the trace and reports the counts in the message log.

\figtodo{25_travel_time_result.png, The Travel Time summary for a downstream subnetwork}

### Rainfall Visualization

**Analysis → Rainfall Visualization…** opens a single modeless window comparing
every rain gage in the project — inline `[TIMESERIES]` gages, standard SWMM rain
files, and multi-column CSV/TSF files alike. Values are resolved through the
engine, so what you see is exactly what a run would use.

| Region | Contents |
|--------|----------|
| Toolbar | **Select**, **Pan**, **Zoom In**, **Zoom Out**, **Fit**, **Refresh**, and the **Basis** selector |
| **Overlay** tab | Every visible gage on one chart with a legend |
| **Per Gage** tab | One stacked chart per gage on a synchronised time axis, sharing the available height |
| Summary table | One row per gage, with the per-gage visibility checkboxes |

The **Basis** selector converts each gage's record so the gages can be compared
on equal terms:

| Basis | Units |
|-------|-------|
| **Intensity** | mm/hr or in/hr |
| **Depth per interval** | mm or in |
| **Cumulative depth** | mm or in — with this basis the Overlay tab *is* the cumulative-curve comparison |

The summary table columns are **Gage**, **Source**, **Total depth**, **Peak
intensity**, **Peak time**, **Interval**, **First**, **Last**, **Gaps**,
**Longest gap**, **Points** and **Status**. **Source** names where the data came
from — `TIMESERIES "name"`, `FILE (rain file, station …)` or `FILE (CSV/TSF,
column …)`. **Status** reads `OK`, `no data in window`, or `file failed to load
(0 entries)`, so a gage whose rain file is missing or misconfigured shows up here
rather than silently plotting nothing.

Only the focused gage — or the first one — is plotted when the window opens; tick
the others in the table to add them. Opening the window from a specific gage's
context menu or property editor focuses that gage.

\figtodo{25_rainfall_visualization.png, The Rainfall Visualization window on the Overlay tab with the gage summary table}

\videotodo{Comparing rain gages — switching to cumulative depth and spotting a gage with a broken rain file}

### 2D cell, edge and vertex time series

Everything the 2D mesh records can be plotted per element, straight from the map.
All three routes feed the comparison plot (\ref manual_time_series_plots).

#### Picking cells

The **Mesh 2D** toolbar's cell-picking tool is checkable — turning it on activates
the picker, turning it off returns to the select tool. It works on the **active 2D
results layer**.

| Interaction | Effect |
|-------------|--------|
| Click a cell | Selects it; `Shift` adds, `Ctrl`/`⌘` toggles |
| Drag | Box selection (the default mode) |
| Press `L` | Switch to lasso mode; press `B` to go back to box mode |
| `Esc` | Clears the selection |
| Right-click | Pops the attribute menu for the selection — or for the cell under the cursor when nothing is selected |

Selected cells are outlined on the canvas at a constant pixel width, so they stay
visible at any zoom.

#### The attribute menu

The same menu style the 1D select tool uses:

| Kind | Attributes |
|------|-----------|
| **Cell** | Depth, HGL, \|V\|, Vx, Vy, Rainfall (intensity), Rainfall volume (cumulative), plus **All attributes** |
| **Edge** | Edge flow (volumetric), Edge flux (unit-width), plus **All attributes** |
| **Vertex** | Depth, HGL (both interpolated from the incident cells), plus **All attributes** |

Entries this run cannot serve are **greyed out** with the tooltip *Not present in
this run's 2D results — re-run with the current engine*: velocity needs edge-flux
data, and the rainfall entries need the per-cell rainfall datasets. Depth and HGL
are always available.

Edges are picked with the mesh **Select Edges** tool and vertices with **Select
Vertices**; right-clicking either pops the same picker (a vertex's menu also shows
its elevation as a disabled header line).

A selection of *N* cells × *M* attributes produces *N × M* series across *M* chart
rows. More than 500 series raises a confirmation first.

\figtodo{25_pick_2d_cells_menu.png, A lasso selection of mesh cells with the attribute context menu open}

#### Multi-run and live behaviour

- **Multi-run.** Open a second run and pick the same cells; the comparison plot's
  1v1 column pairs them and reports NSE / R² / RMSE / PBIAS. Triangle indices are
  mesh-specific, so this only pairs correctly when both runs share a mesh — a
  regenerated or refined triangulation renumbers cells. For mesh-independent
  comparison, compare the 1D coupling nodes instead.
- **Live.** While a simulation is running, cell picks attach to the live tick
  stream: new series start empty and grow as the engine emits depth, flux and
  rainfall packets, so the rainfall entries are enabled during the run and not
  only after the HDF5 file is swapped in. The animation cursor sweeps every chart
  row in step with the map.
- 1D node **Depth** and 2D cell **Depth** are deliberately different quantities and
  never share a chart row.

### Statistics dashboard

**Analysis → Summarize Results** opens the statistics dashboard over the active 1D
results layer: sortable **Nodes**, **Links** and **Subcatchments** tables, a query
bar, a frequency histogram for the selected column, two-way selection sync with
the map, **Zoom to Selected**, and CSV export. It is covered in full in
\ref manual_tabular_results.

\figtodo{25_statistics_dashboard_histogram.png, The statistics dashboard histogram for a selected column}

## Tips and gotchas

- **Flow Balance and Travel Time are final-time-step snapshots.** They are quick
  sanity checks, not volume balances over the run — for that, read the continuity
  sections of the report (\ref manual_running).
- **Travel Time skips still water.** A conduit with zero velocity at the final
  step contributes nothing; compare the reported conduit count against the size of
  the subnetwork before trusting the total.
- **Both tools seed from the selection**, and a selected link seeds *both* of its
  ends — select the node, not the pipe, when you want a one-sided trace.
- **Rainfall Visualization is a singleton.** Opening it again from a different
  entry point raises the same window and re-focuses it on the gage you picked.
- **A gage with `file failed to load`** will contribute no rainfall to a run.
  Check the **Source** column for the path or station the model asked for.
- **Greyed-out mesh attributes mean an older results file**, not a broken model.
  Re-run with the current engine to get velocity or rainfall datasets.
- **Cell indices are mesh identities.** Regenerating a mesh invalidates any saved
  reference to "cell 4 271".
- Two related **model-side** summaries are not analysis tools but are easy to
  confuse with them: the node property browser and attribute table show which
  subcatchments discharge groundwater to a node (see \ref manual_hydrology), and
  the virtual-junction delete and re-fuse prompts summarise the lateral sources
  that would be dropped (see \ref manual_hydraulics).

## Related

- \ref manual_selection — Select Upstream / Downstream; which is how you *see* the subnetwork these tools measure
- \ref manual_time_series_plots — the comparison plot every mesh pick feeds
- \ref manual_tabular_results — the statistics dashboard and the attribute table's result columns
- \ref manual_results — choosing the active 1D and 2D results layers
- \ref manual_running — the report's continuity ledgers; and live results during a run
- \ref manual_2d_mesh — the mesh; its selection tools and cell attributes
- \ref manual_hydrology — rain gages and the time series the rainfall window reads
