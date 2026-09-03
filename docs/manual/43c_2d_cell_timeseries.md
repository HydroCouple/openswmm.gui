@page manual_2d_cell_timeseries 43c · 2D Mesh Cell Time Series

The 2D overland-flow results layer (§43) shows mesh state at a single instant — depth heatmap and velocity arrows for the current time index. **§43c** adds the complementary view: per-cell *time-series* — depth, HGL/WSE, velocity magnitude, and velocity components plotted over the whole simulation for one or many selected cells.

The feature is delivered as Slice CF.3 in the GUI implementation plan and lives on top of the Comparison Plot Dialog (§24).

## Selecting cells on the map

Open **Tools → Pick 2D Cells…** (also on the Analysis toolbar). The action is checkable — toggling it on activates the cell-picker tool; toggling it off reverts to the Select tool. Two interaction modes:

| Mode | Activation | Behaviour |
|------|-----------|-----------|
| Box (default) | Left-click drag | Rubber-band rectangle; release picks every triangle whose centroid falls inside. |
| Lasso | Press `L` while active | Left-click anchors polygon vertices, right-click undoes last vertex, double-click closes, Escape cancels. |

Press `B` while active to swap back to Box mode. A single click without a drag picks the cell under the cursor.

Picked cells get a 2 px gold outline on the canvas. The outline width stays constant in pixels regardless of zoom level so it remains visible at any scale.

## Attribute picker

Right-clicking the selection (or a single cell when nothing is selected) pops the same attribute context menu the 1D Select tool uses for nodes and links: **Depth**, **HGL**, **|V|**, **Vx**, **Vy**, **Rainfall** (intensity, mm/hr), **Rainfall volume** (cumulative m³ applied to the cell), and a trailing **All attributes** entry. Entries the layer's source can't serve are greyed out with a tooltip — velocity when the file lacks edge-flux data, rainfall when it predates the `Mesh2_face_rainfall` / `Mesh2_face_rain_cum` datasets.

Mesh edges (**Edge flow**, **Edge flux**) and mesh vertices (**Depth**, **HGL**, interpolated) get the same menu style from their own select tools.

A selection of `N` cells × `M` chosen attributes produces `N × M` series in `M` chart rows. Selections producing more than 500 series trigger a confirmation prompt — RT0 velocity reconstruction is per-cell-per-tick and big selections can take seconds on large meshes.

## How the data is computed

| Attribute | Source |
|-----------|--------|
| **Depth** | Hyperslab read of `/Mesh2_face_depth[:, triIdx]` from the HDF5 source. Cheap. |
| **HGL** | `depth + z_bed`, where `z_bed[triIdx]` is the mean of the three triangle vertex elevations from `/Mesh2_node_z`. Cached on the adapter on first use. |
| **\|V\|, Vx, Vy** | Closed-form 2 × 2 Raviart–Thomas (RT0) least-squares solve over the three signed edge-normal fluxes (`/Mesh2_edge_flux`) and the time-invariant edge geometry (`/Mesh2_edge_length`, `/Mesh2_edge_nx`, `/Mesh2_edge_ny`). Per-edge speed is clamped to ±10 m/s before assembly to suppress wet/dry-front spikes (same convention as the §43 velocity arrows). |

| **Rainfall** | Hyperslab read of `/Mesh2_face_rainfall[:, triIdx]` (engine m/s, spatially interpolated per cell from the rain gages) × 3.6e6 → mm/hr. |
| **Rainfall volume** | Hyperslab read of `/Mesh2_face_rain_cum[:, triIdx]` — cumulative m³ the engine booked onto the cell; summed over all cells it equals the 2D mass balance's `rainfall_in`. |

Dry cells (`depth < dry_depth`) render the value as `NaN` — appears as gaps in the chart.

## Multi-run comparison

A perturbed second run of the same model can be opened in a parallel project window. Box-pick the same cells in the second window and the Comparison Plot Dialog's 1v1 scatter column lights up automatically — `X = baseline run depth at triangle T`, `Y = comparison run depth at the same triangle`, paired by timestamp. The scatter row title carries `NSE / R² / RMSE / PBIAS` (§24) so you can quickly quantify scenario differences.

Note that triangle indices are mesh-specific — comparing two runs with different meshes (regenerated triangulation, refined region) will pair cells only where indices coincide. For mesh-independent comparison, use coupling-node series from the 1D side (§24 entry points).

## Live mode

While a simulation is running, cell picks land on the existing live-tick stream. New series start empty and grow tick-by-tick as the engine emits `twoDDepthsAvailable` / `twoDFluxAvailable` / `twoDRainfallAvailable` (the last carries per-cell rainfall intensity + cumulative volume from `swmm_2d_get_rainfall_bulk` / `swmm_2d_get_rain_volume_bulk`, so the Rainfall entries are enabled live, not only after the HDF5 swap-in). The animation cursor (when enabled) sweeps across all chart rows in lockstep with the canvas heatmap.

## Limitations

- 1D node `Depth` and 2D cell `Mesh2DDepth` are *different* `PlotAttribute` values. They share units but represent physically different quantities (free-surface depth above a junction invert vs. cell-averaged surface water column over a triangle). Mixing them on one chart row is intentionally not supported.
- Velocity reconstruction requires CF.2-era engine output (edge-flux dataset + edge geometry). Older `.h5` files still plot depth and HGL but show velocity attributes greyed out.
- Per-tick velocity reconstruction in live mode is gated by selection size — interactive performance starts to degrade above ~500 simultaneously-plotted cells on commodity hardware.
