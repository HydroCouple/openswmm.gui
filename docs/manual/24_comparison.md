@page manual_comparison 24 · Comparison Plot Dialog

The **Comparison Plot Dialog** unifies the GUI's time-series surface. Series from any number of runs — open project windows, standalone `.out` files, observed CSV/TSV measurements, and 2D mesh cells — share a single multi-pane plotting window with calendar X axes, per-series styling, and an optional baseline-vs-comparison scatter column.

## Opening the dialog

Right-click a node, link, or subcatchment in the Object Browser or on the canvas and pick **Plot Time Series…** (the old single-series dialog is now a backwards-compatible shim for this surface). Or open it directly through any of the other entry points described in **§24.4 Entry points** below.

## Layout

A horizontal splitter divides the dialog into two regions.

**Left — Series panel.** A tree-of-series grouped by run. The baseline run carries an `⊙` glyph and a `(baseline)` suffix; other runs use a plain `●`. Each series shows a 12 px colour swatch, the object identifier, and the attribute. Buttons under the tree:

- **Add Series…** opens a small popup with one menu entry per loaded run; selecting one prompts for a SWMM object ID and adds it as a series.
- **Load Observed…** opens a file picker for CSV / TSV / DAT data, an attribute picker (which chart row the file's columns belong on), and parses the file into a new "Observed" run with one series per column.
- **Remove** drops the selected series.

**Right — Charts area.** A scrolling vertical stack of chart rows. One row per distinct `PlotAttribute` present in the series list (Depth, Flow, |V|, etc.). Each row holds two charts:

| Column | Content |
|--------|---------|
| 0 | Time-series chart — calendar X axis (auto-formatting from days down to minutes), Y axis labelled in the run's unit system (US ft·ft³·ft/s or SI m·m³·m/s), all series for that attribute rendered together. |
| 1 | 1v1 scatter — `X = baseline sample`, `Y = comparison sample`, paired by timestamp. A 45° dashed identity line marks perfect agreement. Title carries `NSE / R² / RMSE / PBIAS`. |

The scatter column collapses to zero width when only one run is loaded.

## Editing series style

Double-click any series in the tree to open the style editor. Live preview restyles the chart as you edit. Cancel reverts to the pre-edit style. Available controls:

- Colour swatch (opens the system colour picker).
- Line: visible toggle / width / dash pattern.
- Marker: visible toggle / shape (circle / square / triangle / diamond / cross / plus) / size.
- Opacity (0 – 100 %).
- Legend name override (blank = auto-generate from `run — object — attribute`).

## Calibration workflow (observed-vs-baseline)

The combination of `Load Observed…` + the 1v1 scatter column gives a calibration mini-flow:

1. Open your project; run the simulation. The first run becomes the baseline automatically.
2. `Load Observed…` → pick the CSV with measured `J1` depths → attribute picker `Depth (node)`. Each column lands as one series on the **Depth (node)** chart row.
3. The 1v1 scatter column appears with `NSE / R² / RMSE / PBIAS` in the row title computed against the baseline run. Tweak model parameters, re-run, and watch the metrics update.

## Entry points

| Surface | How |
|---------|-----|
| Object Browser right-click → **Plot Time Series…** | Adds one series for the picked object; reuses an open dialog if present. |
| Canvas right-click on a SWMM object → **Plot Time Series…** | Same as Object Browser. |
| **Tools → Pick 2D Cells…** (also on the Analysis toolbar) | Activates the box / lasso cell-picker for 2D mesh layers — see **§43c · 2D mesh cell time series**. |

## Performance notes

- Bulk fetches use `swmm_output_get_*_series` for 1D objects (one engine call per series).
- 2D mesh-cell series read directly from the engine's CF/UGRID HDF5 via `Mesh2DH5Reader` — depth and HGL are cheap (single hyperslab); velocity requires the closed-form RT0 reconstruction described in **§43b**.
- Picking more than 500 mesh cells × attributes triggers a confirmation dialog.

## Persistence

Series styling is persisted per `(run, object, attribute)` in `QSettings` so per-series colours, dash patterns, and marker shapes survive across sessions. The dialog geometry follows the standard project-window pattern.
