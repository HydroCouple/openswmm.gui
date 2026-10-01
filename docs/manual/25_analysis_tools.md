@page manual_analysis_tools 25 — Analysis Tools: Flow Balance, Travel Time, Rainfall Visualisation, 2D Cell Time Series

## What you'll do

Create saved flow-balance and travel-time maps for completed runs; compare
upstream and downstream pathways; and use a dedicated window for
comparing every rain gage's record, per-cell time series picked straight off the
2D mesh, and the sortable statistics dashboard.

\fig{25_analysis_ribbon.png, The Analysis ribbon tab with the Report; Plots and Network Analysis groups}

## Where to find it

| Tool | Menu | Ribbon | Needs |
|------|------|--------|-------|
| **Flow Balance Downstream / Upstream** | **Analysis** | **Analysis → Network Analysis** | A completed 1D output; choose the seed node on the map or by ID |
| **Travel Time Downstream / Upstream** | **Analysis** | **Analysis → Network Analysis** | A completed 1D output; choose the seed node on the map or by ID |
| **Rainfall Visualization…** | **Analysis** | — | Rain gages in the model |
| **Pick 2D Cells** | — | The **Mesh 2D** toolbar | A 2D results layer |
| Mesh **Select Vertices** / **Select Edges** | **Model → Mesh** | **Mesh 2D** | A mesh |
| **Summarize Results** | **Analysis** | **Analysis → Report** | An active 1D results layer |

Rainfall Visualization is also reachable from the Object Browser's **Rain Gages**
context menu and from a rain gage property editor's **Plot Rainfall…** button.

## Step-by-step

### Flow balance and travel time

Select an output in **Layers**, then click **Analysis → Flow Balance Upstream/Downstream** or **Travel Time Upstream/Downstream**. The tools use the selected output, falling back to the active output. The output's context menu offers the same actions.

- If nodes are selected, the tool creates their analysis layers immediately. Otherwise, click a node on the map or enter its ID in the small picker. Comma-separated IDs create separate estimates. A link's context menu lets you choose its seed endpoint.
- The first request automatically computes duration-weighted hydraulic averages and saves a GeoPackage beside the model under `analysis/<run-id>/`. A progress dialog supports cancellation. There is no separate preparation step.
- Later requests reuse the saved averages. Repeating the same node and direction reopens the saved estimate; requesting another node solves from the averages without rescanning the output.
- **Flow balance** and **Travel time** appear as static sublayers beneath their output in Layers. Each has its own visibility, opacity and style. Hiding/removing the output hides/removes its analyses from the map. Double-click an analysis sublayer, or use **Style…**, to change link color, link width, node color and node area independently. **Result details…** shows values, coverage, partial balance quantities and CSV export.

Link widths use a common zero-based proportional scale. At a balanced split, an incoming flow of 10 divides into widths representing 6 and 4; at a confluence those widths add. Constant-flow reaches keep their width. There is no arbitrary narrowing toward a link's endpoint. Arrows indicate physical flow direction in both upstream and downstream analyses. Flow fractions or flow magnitudes give additive widths; widths themed by time or another quantity do not imply hydraulic conservation. Travel time initially uses time for color and flow fraction for width.

The analysis **Style…** dialog has **Links**, **Nodes**, and **Labels** tabs:

- **Color** uses the same classification editor as other visual layers: choose the quantity, continuous or classified colors, ramp and inversion, automatic or custom range, classification method and class count. Class breaks, colors and legend labels can be edited in the table.
- **Size** has its own quantity, value range and pixel limits. Link widths default to the proportional scale described above. Select **Custom size scale** to choose minimum/maximum widths and linear, square-root or logarithmic scaling. Custom scales no longer guarantee additive widths. A fixed maximum in proportional mode sets the flow-to-width reference without clipping larger flows. Node marker area follows the chosen scale; pixel limits specify diameter. **Uniform** uses the maximum size.
- **Labels** can be enabled independently for links and nodes. Choose flow magnitude, fraction, expected travel time, local delay, coverage, or object ID. Add the ID to numeric labels, show fractions as percentages or ratios, set decimal places, and choose font, text size/color and halo. Values are in m³/s or minutes. Partial expected times include coverage; unavailable values are never labeled as zero. Fractions above 100% remain possible in circulating networks.

Edits preview on the map. **OK** retains the theme; **Cancel** restores the previous style. Flow Balance and Travel Time keep separate themes without recomputing their shared analysis.

Every rerun receives a new identity. Existing analysis sublayers remain attached to the original run, shown under a saved-output parent when needed. The next run prepares its own averages. Previous raw results and reports are retained when a run already has saved analysis, unless retention was disabled in the saved project. Projects preserve their analysis layers and styles, and reopen them even when the original output is unavailable. Save As carries managed analysis folders; explicitly external packages remain referenced.

These estimates use duration-weighted trapezoidal averages from the first through last saved report and do not follow animation time. Effective transport follows mean net flow; gross magnitude and reversal diagnostics are stored separately. Passage ratios may exceed one in circulating networks. Travel time uses conduit length divided by mean absolute velocity and storage volume divided by outgoing flow plus known sinks. Non-conduit controls have zero modeled delay. Missing or partially known times remain flagged with coverage. The sampled hydraulic balance is partial, not the simulation's exact continuity report. Imported outputs retain their model-pairing provenance. A one-report output is a snapshot without a duration-based balance.

\figtodo{25_flow_balance_result.png, Flow-balance analysis beneath a completed output with proportional flow widths on the map}

\figtodo{25_travel_time_result.png, Travel-time analysis with time represented by color and flow fraction by width}

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

\fig{25_rainfall_visualization.png, The Rainfall Visualization window on the Overlay tab with the gage summary table}

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
bar, two-way selection sync with
the map, **Zoom to Selected**, and CSV export. It is covered in full in
\ref manual_tabular_results.

\figtodo{25_statistics_dashboard_histogram.png, The Statistics Dashboard with result tables and query controls}

## Tips and gotchas

- **Saved analyses use the run's report interval**, not the animation cursor.
  They summarize sampled hydraulics and do not replace the engine's continuity
  report (\ref manual_running).
- **Choose the run before the seed.** Saved analyses remain attached to their
  original run after a rerun. Confirm the parent output in Layers.
- **Read coverage and reversal diagnostics.** Partial or unavailable travel times
  must not be interpreted as zero. Circulating networks can have passage ratios
  greater than one.
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
