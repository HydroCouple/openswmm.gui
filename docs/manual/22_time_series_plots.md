@page manual_time_series_plots 22 — Time-Series and Comparison Plots

## What you'll do

Plot any result variable of any object against calendar time; put several runs,
several objects and observed measurements on the same chart; style each series;
zoom, pan and read values off the chart; compute summary statistics and
goodness-of-fit metrics against a baseline; and export the numbers or the picture.

All of this happens in one window — the **Comparison Plot** dialog. Every
time-series entry point in SWMMVis feeds it.

\figtodo{22_comparison_plot_overview.png, The Comparison Plot dialog — series tree; stacked chart rows; range slider and statistics panel}

## Where to find it

| Entry point | What it adds |
|-------------|--------------|
| **Analysis → Plot Time Series** (`Ctrl+T`), ribbon **Analysis → Plots** | Opens the **Plot Variables** picker: the 14 system-wide variables plus one checkable group per selected map feature. Everything you tick is added in one go |
| Right-click a node / link / subcatchment on the **map** → **Plot Time Series** | Pops the attribute menu for that kind; the pick is added as one series |
| Right-click an object in the **Object Browser** → **Plot Time Series** | Same. With more than one `.out` loaded the entry becomes a **Plot Time Series ▸ *run*** submenu so you choose which run |
| Right-click a **2D mesh cell / edge / vertex** on the map | The mesh attribute menu — see \ref manual_analysis_tools |
| **Add from Map…** on the dialog's own toolbar | Arms a one-shot map pick so you can keep adding objects without leaving the dialog |

The dialog is shared: every entry point reuses the one open window rather than
stacking new ones. `Ctrl+T` needs an **active 1D results layer** — if none is
set it tells you to pick one in the Analysis toolbar's **1D results:** selector.

## Step-by-step

### The Plot Variables picker (`Ctrl+T`)

The picker lists:

- one checkable group per **selected** node, link or subcatchment, containing
  that kind's plottable attributes plus the run's water-quality species;
- a group of the 14 **system-wide** variables, which need no selection.

Attributes the loaded run cannot serve are greyed out. Tick as many as you like
and press **OK**; ticking more than 500 leaves raises a confirmation first,
because that many series takes a while to build and clutters the charts.

\figtodo{22_plot_variables_picker.png, The Plot Variables dialog with one group per selected object and the system group}

### What can be plotted

| Object kind | Attributes |
|-------------|------------|
| **Node** | Depth, Head, Volume, Lateral inflow, Total inflow, Overflow |
| **Link** | Flow, Depth, Velocity, Volume, Capacity |
| **Subcatchment** | Rainfall, Snow depth, Evaporation, Infiltration, Runoff |
| **System** | Rainfall, Snow depth, Evaporation (average rate), Infiltration, Runoff, Dry-weather inflow, Groundwater inflow, Lateral inflow, Flooding, Outflow, Storage, Total evaporation, Potential ET, Temperature |
| **2D mesh cell** | Depth, HGL, Velocity magnitude, Velocity X, Velocity Y, Rainfall, Rainfall volume |
| **2D mesh edge** | Flow (volumetric, m³/s), Unit-width flux (m²/s) |
| **2D mesh vertex** | Depth, HGL (interpolated) |
| **Observed file** | Whatever attribute you assign the file's columns to |

Every 1D kind additionally offers one entry per **water-quality species** the run
carries — each pollutant plus the reserved water-age and temperature columns.
Species are carried by **name**, so a picked series keeps meaning the same
constituent even if the model's pollutant order changes. Water age is reported in
hours and temperature in degrees; ordinary pollutants use the run's concentration
unit.

Units follow the run: a `.out` written in CFS/GPM/MGD labels axes in US units,
one written in CMS/LPS/MLD in SI.

Every attribute menu also carries an **All attributes** entry that adds one
series per attribute for that object in a single click.

### Dialog layout

| Region | Contents |
|--------|----------|
| Toolbar (top) | Interaction modes, Fit, exports, view toggles, the 1v1 controls |
| **Series** panel (left) | A tree grouped by run, with **Add Series…**, **Load Observed…** and **Remove** underneath |
| Charts (centre) | A vertical stack of chart **rows** — one row per distinct plot attribute; scrolls when there are many |
| Range slider | An X-range slider under the charts with a date/time read-out |
| **Statistics** panel (bottom) | One tab per chart row |

Rows are separated by splitters you can drag; the splitter sizes, the window
geometry, table header layouts and the view toggles are all remembered between
sessions (per dialog, under the application's `Dialogs/` settings).

The toolbar's **Series Panel**, **Range Slider**, **Stats Panel** and **1v1
Plots** toggles hide and show each region; **Charts Only** (`Ctrl+Shift+F`) hides
the first three at once and restores your previous widths when you toggle it back.

### The series tree

Series are grouped by **run**. The baseline run carries a `⊙` glyph and a
*(baseline)* suffix; the others use `●`. Each series row shows a colour swatch and
a label of the form *object — attribute*; a mesh series reads `Cell N` and an
observed column reads `Observed: name`. The checkbox on a row toggles that
series' visibility without removing it.

| Action | How |
|--------|-----|
| Add a series | **Add Series…** — pick a run, then type the SWMM object ID for the chosen attribute |
| Add observed data | **Load Observed…** — see below |
| Add system series | **Add System Series…** on the toolbar |
| Add by clicking the map | **Add from Map…** on the toolbar (a checkable tool; `Esc` cancels it) |
| Remove | Select rows and press **Remove**, or right-click → **Remove Series** |
| Isolate | Right-click a series → **Plot This Only** |
| Edit style | Double-click a series, or right-click → **Edit Properties…** |
| Export one series | Right-click → **Export Series Data…** |
| Drop a whole run | Right-click the run row → **Remove Run** |

The **first run added becomes the baseline.** There is no separate
"set baseline" command; to compare series that are not the baseline-vs-others
default, configure explicit pairs (below).

\figtodo{22_series_tree_context_menu.png, The series tree with a run group; a baseline marker and the series context menu}

### Loading observed data

**Load Observed…** accepts `*.csv`, `*.tsv`, `*.tsf` and `*.dat`. Column 0 is the
timestamp; every other column becomes one series. Timestamps are recognised as:

- ISO 8601 — `2026-01-01T00:15:00` or `2026-01-01 00:15:00`;
- SWMM external time series — `01/01/2026 00:15`;
- hours since the start of the run — a bare number, tried only when nothing else
  matches.

After the file is chosen you are asked **which attribute** the value columns
represent — Depth (node), Head, Total inflow, Overflow, Flow, Depth (link),
Velocity (link), Rainfall or Runoff. That choice decides which chart row the
columns land on. Column headers become the series names (`col_1`, `col_2`, … when
the file has no header row). A unit suffix in the header (`Depth_m`, `Flow_ft3s`)
is used to infer the unit system. To put one file's columns on several different
rows, load the file more than once with a different attribute each time.

\figtodo{22_load_observed.png, The observed-attribute prompt after choosing a CSV}

### Chart interaction

The toolbar's four mode buttons are mutually exclusive and apply to every row:

| Mode | Left button behaviour |
|------|-----------------------|
| **Select** | Passes through — hover shows a tooltip with the nearest sample's series name; date and value |
| **Pan** | Drag to scroll the axes |
| **Zoom In** | Click zooms 2× about the cursor; drag a rectangle zooms to it |
| **Zoom Out** | Click zooms out 2×; drag a rectangle does the inverse zoom |

Independent of the mode:

- the **wheel** always zooms about the cursor;
- **middle-button drag** pans;
- **Shift-drag** across a chart selects an X range — the statistics panel narrows
  to that window, and **Fit** re-fits Y to it. Shift-drag again outside to clear;
- clicking an axis **endpoint label** lets you type an exact minimum or maximum;
- **Fit** resets every row to its data extents;
- the X axes of all rows are **linked** — zooming one scrolls them all, and the
  range slider under the charts drives them together.

Right-clicking a chart gives **Reset Zoom**, **Fit All Rows**, **Export Row
Data…** and **Chart Properties…**.

**Show Animation Cursor** (`Ctrl+Shift+C`) draws a vertical line at the map
animation's current time on every row, so scrubbing the map moves the cursor in
the plots. It is on by default.

While a run is in flight with **Live 1D** enabled, the charts extend as the `.out`
grows — points are appended and axes stretched outward rather than the charts
being rebuilt, so your zoom and styling survive.

\figtodo{22_chart_modes_toolbar.png, The Comparison Plot toolbar with the interaction modes and view toggles}

### Series styling

Double-clicking a series opens **Edit series properties**, a property tree with a
live preview — the chart restyles as you edit, and **Cancel** puts back the
pre-edit style.

| Group | Properties |
|-------|-----------|
| Identity | **Colour**, **Opacity** (0–1), **Legend name** — blank means auto (*run — object (attribute)*) |
| Line | **Show line**, **Width**, **Dash** (Solid / Dash / Dot / DashDot / DashDotDot), **Cap style**, **Join style** |
| Markers | **Show markers**, **Shape** (Circle / Square / Triangle / Diamond / Cross / Plus), **Size**, **Fill colour**, **Border colour**, **Border width** |
| Point labels | **Show point labels**, **Font**, **Colour**, **Precision**, format mode (decimals or significant figures) or an explicit format string |
| Area fill | **Show area fill**, **Colour** |

Colours left invalid derive from the main series colour, so you only override what
you care about. New series are assigned colours from a 12-colour cycle.

\figtodo{22_series_style_editor.png, The series property editor showing line and marker groups}

### Chart properties

**Chart Properties…** (right-click a chart) opens a modeless editor that floats
above the dialog:

| Property | Effect |
|----------|--------|
| **Title text**, **Title font** | The chart's title |
| **Y auto range** | Off lets you pin the Y minimum and maximum |
| **X gridlines**, **Y gridlines**, **Grid colour** | Gridline visibility and colour |
| **Axis label font**, **Tick label font** | Axis typography |
| **Background colour**, **Plot area colour** | Chart chrome |
| **Chart theme** | The Qt Charts palette |
| **X number format**, **Y number format** | Decimals or significant figures with a digit count, or an explicit format string |
| **Statistics format** | The number format used for statistic values |

The editor charts elsewhere in SWMMVis (curve, pattern, time series, transect,
hydrograph, scatter) share this dialog through a per-chart axis-format controller,
so a format you set on one of those sticks across replots.

### Statistics panel

One tab per chart row; one table row per series.

| Column | Meaning |
|--------|---------|
| **count** | Finite samples used (NaN and infinite samples are skipped) |
| **mean**, **median**, **stddev** | Sample mean, median, sample standard deviation (N−1) |
| **min**, **max**, **sum** | Extremes and total |
| **p05 / p25 / p50 / p75 / p95** | Percentiles (p50 repeats the median for column symmetry) |
| **NSE**, **R²**, **RMSE**, **PBIAS** | Fit against the baseline — present only when a baseline run is set |

Right-click the table header to hide or show individual columns; the choice is
remembered between sessions. A Shift-drag X selection on any chart narrows every
tab's statistics to that time window; clearing the selection restores the full
series.

\figtodo{22_statistics_panel.png, The statistics panel with summary columns and fit metrics}

### Baseline vs comparison — the 1v1 scatter column

With **1v1 Plots** on, each row grows a second chart: baseline sample on X,
comparison sample on Y, with a 45° dashed identity line. Perfect agreement puts
every point on that line. The column appears only when there is something to pair
— two or more runs, or explicitly configured pairs.

Samples are paired by **nearest timestamp**, not by index: the two streams are
walked in lockstep and a pair is kept when the timestamps are within half the
smaller of the two report steps. That is what lets a 1-minute observed record be
compared against a 15-minute simulated one without resampling either.

**Configure 1v1…** opens a pair editor: choose an X series and a Y series, add the
pair, and the scatter rebuilds live. An empty pair list means automatic mode —
baseline against every other run, matched by object. Explicit pairs can compare
two series of the *same* run (two objects, say), which automatic mode cannot.

The row title carries the fit metrics:

| Metric | Definition | Reading it |
|--------|-----------|------------|
| **NSE** | Nash–Sutcliffe efficiency — 1 − Σ(sim − obs)² ⁄ Σ(obs − mean obs)² | 1 is perfect; 0 means the simulation is no better than the observed mean; negative is worse than the mean |
| **R²** | Squared Pearson correlation of the paired samples | 0 to 1; measures the strength of the linear relationship, and is blind to bias |
| **RMSE** | Root mean square error, √(Σ(sim − obs)² ⁄ n) | Same units as the plotted variable; 0 is perfect |
| **PBIAS** | Percent bias, 100 × Σ(sim − obs) ⁄ Σ(obs) | 0 is unbiased; positive means the simulation over-predicts, negative under-predicts |

The baseline is treated as *observed* for the NSE and PBIAS sign conventions.
Pairs where either sample is NaN are dropped; with fewer than two valid pairs
every metric reads as undefined.

A calibration loop therefore reads: run the model (its run becomes the baseline),
**Load Observed…** the measured record onto the same attribute row, read
NSE / R² / RMSE / PBIAS, adjust parameters, re-run, watch the metrics move.

\figtodo{22_1v1_scatter_metrics.png, A 1v1 scatter with the identity line and the fit metrics in the title}

\videotodo{Comparing a simulated hydrograph with an observed record — loading a CSV; reading the fit metrics and exporting the data}

### Exporting

| Command | Output |
|---------|--------|
| **Export PNG…** (toolbar) | Saves the chart pane as a `.png` |
| **Export Data…** (toolbar) | Every plotted series |
| **Export Row Data…** (chart right-click) | The series on that chart row |
| **Export Series Data…** (series right-click) | One series |

Both data exports offer two formats:

- **CSV** — wide format: column 0 is an ISO `yyyy-MM-dd HH:mm:ss` timestamp, one
  column per series, rows being the sorted union of all series' timestamps. Cells
  are blank where a series has no sample at that time. This is exactly what
  **Load Observed…** reads back, so an export can be re-imported.
- **SWMM `.dat`** — the engine's external time-series format
  (`MM/dd/yyyy HH:mm:ss value`) preceded by a `;name` comment. A `.dat` file
  carries one series, so exporting N series writes N files named
  `<base>_<series>.dat` beside the path you chose, and the dialog tells you which
  files it wrote. Non-finite values are dropped, because SWMM cannot parse them.

## Tips and gotchas

- **Rows are keyed by attribute, not by object.** Two runs' depth at the same
  junction land on one chart row automatically; node depth and 2D cell depth do
  *not* share a row, because they are physically different quantities that happen
  to share a unit.
- **A missing 1v1 column usually means one run.** Load a second run, or define an
  explicit pair, before expecting fit metrics.
- **R² can be excellent while NSE is terrible.** R² ignores bias and scale; a
  systematically high simulation still correlates perfectly. Read them together
  with PBIAS.
- **Observed columns are all assigned one attribute.** Load the file once per
  attribute you need.
- **The animation cursor is shared.** If the vertical line is not where you
  expect, check which results layer is driving the map animation
  (\ref manual_results).
- **Exports respect what is plotted, not what is loaded.** Hidden series are still
  part of the model; use **Export Series Data…** or **Export Row Data…** when you
  want a subset.
- Two dialogs exist in the build but have **no entry point in the current user
  interface**: a standalone variable-correlation **scatter plot** dialog and a
  **calibration data** registration dialog that would bind observed CSVs to
  objects in the project file. Use **Load Observed…** and the 1v1 column instead.

## Related

- \ref manual_results — choosing the active run; the animation cursor these plots follow
- \ref manual_running — producing and live-tailing the results plotted here
- \ref manual_profile_plots — the same result fields plotted along a path instead of against time
- \ref manual_tabular_results — the same numbers as tables
- \ref manual_analysis_tools — 2D cell time series; rainfall visualisation and the statistics dashboard
- \ref manual_object_browser — the right-click entry points
- \ref manual_data_objects — time series and calibration data in the model itself
- \ref manual_file_formats — the CSV and `.dat` formats read and written here
