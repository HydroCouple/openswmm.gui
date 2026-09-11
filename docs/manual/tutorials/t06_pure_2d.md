@page tutorial_pure_2d T6 — A Stand-alone 2D Overland-Flow Model

## Goal

Run a model that has **no 1D network at all** — no junctions, no conduits, no
outfalls, no subcatchments. Just a triangulated surface, a rain gage, and the
explicit local-inertial marcher. You will open a 3 % tilted plane, confirm that
the Object Browser really is empty of network objects, style the mesh by bed
elevation, follow rainfall onto the cells, animate depth and velocity, pull a
time series out of a single cell, read the 2D mass balance, and then experiment
with the wetting/drying closure that this example exists to demonstrate.

The last step builds the same kind of model from scratch, starting from a
synthetic slope raster.

## Capabilities exercised

- A pure 2D project: an inline mesh with no `[JUNCTIONS]` / `[CONDUITS]`
- `[2D_OPTIONS] RAINFALL_MODE SYSTEM` — uniform rain from the gage mean
- `CELL_CLOSURE` / `FACE_RECONSTRUCTION` — the VFR wetting/drying closure
- Default `WALL` boundaries — every boundary edge, because none are listed
- 2D results styling, map animation, per-cell time series, 2D mass balance
- **Generate 2D Mesh** from a raster

## Files

| File | What it is |
|---|---|
| `examples/demo_vfr_slope/vfr_slope.oswp` | The project — open this one |
| `examples/demo_vfr_slope/vfr_slope.inp` | Inline mesh; 147 vertices; 240 triangles |
| `examples/demo_vfr_slope/vfr_slope.2d.h5` | Pre-computed 2D results |
| `examples/demo_vfr_slope/vfr_slope.out` / `.rpt` | 1D outputs — see the warning in step 5 |

\figtodo{t06_vfr_slope_overview.png, The tilted-plane mesh coloured by bed elevation with the rain gage symbol west of the domain}

## Steps

### 1. Open the model and notice what is missing

**File → Open…** → `examples/demo_vfr_slope/vfr_slope.oswp`.

Open the Object Browser (**Ctrl+Alt+2**). The Junctions, Outfalls, Conduits,
Pumps, Orifices, Weirs, Outlets, Storage and Subcatchments categories are all
empty. The only model objects are one rain gage and the mesh. The Layers panel
(**Ctrl+Alt+1**) shows a **Meshes** entry and, once results are loaded, a
**SWMM 2D Outputs** entry; the **SWMM 1D Outputs** category stays hidden
because it is empty.

The complete section list of `vfr_slope.inp` is:

```
[TITLE] [OPTIONS] [EVAPORATION] [RAINGAGES] [TIMESERIES] [SYMBOLS]
[2D_OPTIONS] [2D_VERTICES] [2D_TRIANGLES]
```

No `[COORDINATES]`, no `[SUBCATCHMENTS]`, and — importantly — no
`[2D_BOUNDARY_CONDITIONS]`.

The `[OPTIONS]` block is minimal:

```
[OPTIONS]
FLOW_UNITS           CMS
INFILTRATION         HORTON
FLOW_ROUTING         DYNWAVE
START_DATE           01/01/2026
START_TIME           00:00:00
END_DATE             01/01/2026
END_TIME             06:00:00
REPORT_STEP          0:02:00
ROUTING_STEP         2
THREADS              4
```

`FLOW_ROUTING DYNWAVE` is still set even though there is nothing to route in
1D. That is fine — the 1D solver simply has an empty network.

### 2. The mesh

The mesh is a structured 21 × 7 vertex grid: *x* from 0 to 100 m in 5 m steps,
*y* from 0 to 30 m in 5 m steps, with `Z = 0.03 x` — a 3 % ramp rising from
0.0 m at the west edge to 3.0 m at the east. 147 vertices, 240 triangles,
Manning's *n* = 0.030 everywhere.

```
[2D_VERTICES]
;;X       Y       Z
   0.000    0.000   0.0000
   5.000    0.000   0.1500
  10.000    0.000   0.3000
...

[2D_TRIANGLES]
;;V1  V2  V3   MANNINGS_N
   0    1   22   0.030
   0   22   21   0.030
   1    2   23   0.030
...
```

Because `[2D_BOUNDARY_CONDITIONS]` is absent, **every** boundary edge takes the
default `WALL` — the plane is a closed box. Rain falls on the ramp, runs
downhill to the west, and pools against the low wall at *x* = 0. Nothing ever
leaves the domain; the storm's water is either stored or evaporated.

Style the mesh to see the ramp. Double-click the mesh layer in **Layers** →
**Terrain Fill** tab:

| Control | What it does |
|---|---|
| **Show terrain fill** | turns the filled surface on |
| **Attribute:** | the cell attribute to colour by — leave it on **Bed elevation** |
| **Category palette:** | the colour ramp |
| **No-data colour:** | colour for cells without a value |
| **Azimuth (light from):** / **Altitude (sun angle):** | hillshade direction and elevation |
| **Vertical exaggeration:** / **Shadow floor:** | hillshade strength and its darkest value |

The **Elevation Bands** and **Elevation Isolines** tabs add filled bands and
contour lines with optional elevation labels; **Mesh Edges** draws the
wireframe and can widen and recolour steep edges past a slope break.

\figtodo{t06_mesh_style_terrain.png, The Terrain Fill tab of the mesh layer style dialog with hillshade settings}

### 3. Rainfall: the gage and `RAINFALL_MODE SYSTEM`

There is one gage:

```
[RAINGAGES]
;;Name   Format     Intvl   SCF   Source
GAGE1    INTENSITY  0:05    1.0   TIMESERIES RAIN

[TIMESERIES]
RAIN   0:00   20.0
RAIN   1:00   20.0
RAIN   1:01    0.0
RAIN   6:00    0.0

[SYMBOLS]
GAGE1  -10  15
```

20 mm/hr for one hour, then five hours of drainage. The gage sits at
(−10, 15) — **outside** the mesh, to the west.

`[2D_OPTIONS] RAINFALL_MODE SYSTEM` is what pushes that rain onto the cells:

| Value | What the mesh sees |
|---|---|
| `NATURAL_NEIGHBOUR` (default) | natural-neighbour (Laplace) weights inside the convex hull of the gages, inverse-distance-squared extrapolation outside; weights are precomputed once |
| `SYSTEM` | one uniform value — the arithmetic mean of every gage's current rainfall |
| `NONE` | no rain on the mesh; use this when subcatchments already capture the storm and rain-on-mesh would double-count it |

`SYSTEM` is also the automatic fallback when **no gage has a map location**, so
a gage with no `[SYMBOLS]` row silently gives you uniform rain even if you asked
for natural neighbour.

Set it in **Simulation Options → 2D Surface Routing → Processes**, in the
**Rainfall mode:** combo — the entries read **Natural neighbour (all gages)**,
**System (uniform gage mean)** and **None (no direct rainfall)**.

To see the storm itself, use **Analysis → Rainfall Visualization…**. The
dialog opens on the **Overlay** tab with a **Basis:** combo offering
**Intensity (mm/hr)**, **Depth per interval (mm)** and
**Cumulative depth (mm)**, and a statistics table underneath listing total
depth, peak intensity, peak time, interval, first and last timestamps, gaps and
point count.

\figtodo{t06_rainfall_visualization.png, The Rainfall Visualization dialog showing the one-hour 20 mm per hour block}

### 4. Run it

Press **Ctrl+R**. Because this is a 2D-only model there is no `.out` content of
interest; the interesting product is `vfr_slope.2d.h5`, written because
`[2D_OPTIONS]` sets `REPORT_2D YES` and `OUTPUT_FILE vfr_slope.2d.h5`.

While it runs, watch the **Simulation Status** panel (**Ctrl+Alt+6**). Its
columns are **Name**, **Status**, **Progress**, **Sim Start**, **Sim Current**,
**Sim End**, **Runoff Err (%)**, **Routing Err (%)**, **2D Err (%)**,
**Duration**, **Avg Timestep** and **Engine Version**. The **2D Err (%)**
column is the one to watch here — it is polled live during the run, not only at
the end. In a model with no 2D it would read `—`.

On completion the 2D results layer is attached automatically, named
**2D Results (live)** while the run is in progress and keyed by the canonical
`.h5` path so a re-run overwrites rather than stacking duplicates. The GUI
reads the engine's `DRY_DEPTH` so its wet/dry rendering threshold matches the
solver's.

\videotodo{Running the stand-alone 2D slope model and animating depth and velocity}

### 5. Two things to fix in the shipped file

Do this before you trust any numbers from this example.

**The bundled `vfr_slope.rpt` is stale.** It reports `Internal Steps 0`,
`Face-Kernel Evals 0`, all-zero 2D continuity, `Flow Routing ........... NO`
and `Total elapsed time: < 1 sec` — a degenerate run that does not match the
5.9 MB `.2d.h5` sitting next to it. Re-run the model before quoting continuity
errors or step counts.

**Four `[2D_OPTIONS]` keys are retired.** The shipped block is:

```
[2D_OPTIONS]
MAX_TIMESTEP           5.0
MIN_TIMESTEP           0.001     ;; retired
DRY_DEPTH              0.001
COUPLING_CD            0.65
LINEAR_SOLVER          GMRES     ;; retired
PRECONDITIONER         JACOBI    ;; retired
MAX_CVODE_STEPS        2000      ;; retired
RAINFALL_MODE          SYSTEM
REPORT_2D              YES
OUTPUT_FILE            vfr_slope.2d.h5
```

Each retired key produces, on load:

> `WARNING 104: [2D_OPTIONS] <key> was retired with the CVODE/ARKODE 2D
> solvers and was ignored; the explicit local-inertial marcher is the only 2D
> integrator.`

They are harmless — warn-and-ignore — but noisy. Saving the model from SWMMVis
drops them, because the Simulation Options dialog writes through the API, which
rejects retired keys outright.

**And the header comment is wrong about the default.** The file's own leading
comment says *"Under VFR (default)…"*, but the engine's defaults are
`CELL_CLOSURE FLAT` and `FACE_RECONSTRUCTION MEAN`, and the file sets neither.
As shipped, this model runs **FLAT**. Getting the VFR behaviour the example is
named for is step 8.

### 6. Animate depth and velocity

Style the 2D results layer (double-click it in **Layers**). Its style panel has
scalar-fill tabs plus **Depth Contours**, **Depth Isolines**, **Flow Velocity**,
**Mesh Edges** and **Mesh Vertices**.

Use the **Results** ribbon tab transport — **Play**, **Pause**, **Stop**,
**Skip Back**, **Skip Forward** — and the **Show Legend** toggle. Then look for:

- rain accumulating uniformly across every cell during the first hour, because
  `RAINFALL_MODE SYSTEM` gives every cell the same intensity;
- a sheet of water translating downslope to the west;
- ponding against the *x* = 0 wall, deepening while the upslope cells drain;
- velocities peaking during the storm and collapsing afterwards as the water
  surface flattens against the wall.

\figtodo{t06_depth_animation_frame.png, A mid-drainage animation frame — a wedge of water against the west wall and a thin film upslope}

### 7. A time series for one cell, and the mass balance

**Per-cell time series.** Activate the **Select 2D Cells** tool. It supports
**B** for box mode and **L** for lasso mode; **Shift** adds to the selection,
**Ctrl** toggles, a plain click replaces, and **Esc** clears. Pick a cell near
the west wall and one near the crest. The picked cells are handed to the
**Comparison Plot** dialog, seeded with those cells and their attributes, so
you get both traces in a single chart. Right-clicking a mesh **edge** plots its
flux time series; right-clicking a **vertex** plots interpolated depth and HGL
there.

Selecting a single cell also shows it in the Properties panel, titled
`2D Cell <n>`.

\figtodo{t06_cell_timeseries.png, A comparison plot with depth traces from a crest cell and a toe cell}

**Mass balance.** There is no separate mass-balance dialog:
**Analysis → Show Mass Balance** opens the **status report**, which is where
continuity lives. Its *2D Surface Routing Continuity* block accounts for
rainfall inflow, coupling exchange, outfall and boundary flux, evaporation,
infiltration, and initial and final storage. In this model boundary outflow
should be **zero** — every edge is a wall — so rainfall in must equal final
storage plus evaporation, minus infiltration.

The 2D solver statistics block underneath reports internal steps, face-kernel
evaluations, average internal step, active-cell percentages and the occupancy of
each local-timestepping tier. On a small mesh like this one the tier occupancy
tells you whether `LTS_TIERS` is buying you anything.

### 8. The point of the example: FLAT versus VFR

Set `CELL_CLOSURE` to VFR and re-run, then compare.

**Simulation Options → 2D Surface Routing → Wetting & Drying**, in the
**Cell closure** group:

| Control | Entries | Writes |
|---|---|---|
| **Cell closure:** | **Flat (η = z̄ + V/A, legacy)** / **VFR (planar-bed volume/free-surface)** | `CELL_CLOSURE` |
| **Face reconstruction:** | **Mean (upwind cell depth, legacy)** / **VFR face (edge depth + wetting gate)** | `FACE_RECONSTRUCTION` |
| **VFR min wet fraction:** | (0, 0.5] | `VFR_MIN_WET_FRAC` |

Under `FLAT` the free surface of a cell is reconstructed as
η = z̄ + V/A, which is exact only when the cell is fully wetted. On a
partially wet cell spanning a slope it **overstates** η by up to two-thirds of
the cell's relief — and that is what drives thin films to climb and strand up
the ramp during drainage.

Under `VFR` the engine uses the exact stage–storage relation of the planar bed
through the cell's three vertex elevations, regularised by
`VFR_MIN_WET_FRAC`. The water drains cleanly to the *x* = 0 wall and the
shoreline sits at the true contour. The cost is roughly three to eight times
more marcher substeps, which is why it is opt-in.

Compare the two runs by keeping both results layers on the canvas and adding
series from each in the **Comparison Plot** (\ref manual_time_series_plots).

\figtodo{t06_flat_vs_vfr.png, Depth along the ramp under FLAT and under VFR at the same drainage time}

### 9. Build one from scratch

Any pure-2D model is the same three ingredients: a mesh, a rain gage, and 2D
options. Here is the raster route.

1. **Start a project and set the CRS.** Mesh generation requires a **projected
   or local** CRS with a usable linear unit. A geographic (lat/lon) CRS is
   rejected outright:

   > *The project CRS (…) has no usable planar linear unit. 2D mesh generation
   > requires a projected or local CRS in metres or feet. Geographic (lat/lon)
   > CRSes are not supported.*

   Use **Project → Change CRS…** and pick a projected CRS, or the
   *Local projected* option when the source units are unknown.
2. **Add the raster.** **File → Import → Add Raster Data…** and pick your slope
   DEM (GeoTIFF, ASCII Grid, Erdas Imagine, SRTMHGT, netCDF, … — the offered
   filter lists only the drivers the running build actually has).
3. **Open Generate 2D Mesh** (**Mesh 2D** ribbon tab, or **Model → Generate
   Mesh**). The dialog has three tabs plus a persistent footer.

   **Sources** tab:
   - **DTM raster:** your slope raster; **DTM vertical unit:**,
     **Mesh vertical unit:** and **Z conversion (×):** reconcile the units.
   - **Boundary polygon:** a polygon layer whose features define the meshing
     boundary; interior rings are respected as holes. With **(none)** the domain
     falls back to the SWMM model bounding rectangle plus 5 %, which for a
     network-free project means you *must* supply a boundary polygon.
   - **Constraining points** and **Constraining lines** add Steiner points and
     constraint segments from checked layers.
   - The **1D geometry influence** and **1D ↔ 2D coupling** groups are all
     irrelevant to a pure-2D model — leave them unchecked.
   - **Elevation interpolation (no DTM)** — **Method:**, **NN variant:**,
     **IDW power:** — only matters when you have no raster.

   **Quality** tab: **Max triangle area:**, **Min angle:**,
   **Size gradation:**, **Max Steiner points:**, then **PSLG Optimisation**
   (**Geometry simplification ε:**, **Steiner snap radius:**),
   **Minimum Cell Size** (**Minimum cell size:**, **Trim corners sharper
   than:**) and **Terrain-Adaptive Thinning**.

   **Hydraulics** tab: **Roughness (Manning's n):** and **Initial depth:** —
   the initial per-cell values written into `[2D_TRIANGLES]`.

   **Footer:** the **Output:** radio pair chooses **External .2dm** (a
   standalone mesh file referenced by `[2D_MESH_FILE]`) or **Inline in .inp**
   (the mesh embedded in the input file, as `vfr_slope.inp` does). Then
   **Generate**.

   \figtodo{t06_generate_mesh_dialog.png, The Generate 2D Mesh dialog on the Sources tab}

4. **Generating a mesh activates the 2D module** — the same project flag the
   Simulation Options **Models / Processes → Modules → 2D Surface Routing**
   checkbox holds.
5. **Add a rain gage** (**Model → Add Rain Gauge**) and give it a time series in the
   time-series editor. If you leave it off the map, rainfall mode falls back to
   `SYSTEM` on its own.
6. **Set the 2D options.** At minimum: `RAINFALL_MODE`, `MAX_TIMESTEP`,
   `DRY_DEPTH`, `REPORT_2D` and `OUTPUT_FILE`. The **2D results file:** field
   has the placeholder `<model>.2d.h5 (automatic)`.
7. **Leave the boundaries alone** for a closed box, or select the low edge with
   **Select Mesh Edges** and give it **Normal Flow** with a non-zero slope so
   the domain drains.
8. **Save and run.**

If generation fails, the dialog reports the reason directly — see
\ref manual_troubleshooting for the full catalogue of mesh failures
(self-intersecting boundary, degenerate segments, minimum-cell-size conditioning
that would have made things worse, and so on).

## What to look for

- The Object Browser's network categories stay empty for the whole session.
  A 2D-only project is a legitimate SWMM 6 model.
- Rain arrives on every cell at the same rate — that is `SYSTEM`, not a
  coincidence.
- Boundary outflow in the report is **zero**, because every boundary edge is a
  default `WALL`.
- Under `FLAT`, thin films strand partway up the ramp during drainage. Under
  `VFR` they do not. That difference is a discretisation artefact, not physics.
- The four `WARNING 104` lines in the log are the retired CVODE keys, not a
  problem with your run.

## Variations

- **Open the west wall.** Select the run of boundary edges at *x* = 0 and set
  **Normal Flow** with a slope of, say, 0.03 to match the bed. The pond drains
  off the domain instead of filling.
- **Switch to `NATURAL_NEIGHBOUR`** and add a second gage with a different
  series. Give both `[SYMBOLS]` locations and watch the rainfall field vary
  across the ramp; drop the location from one and it silently reverts to
  `SYSTEM`.
- **Tighten `CFL_NUMBER`** from 0.7 toward 0.3 and compare run time against the
  2D continuity error in the report.
- **Raise `THETA`** from 0.8 to 1.0 (pure Bates 2010) and compare the leading
  edge of the sheet flow.
- **Lower `DRY_DEPTH`** and `H_MOVE` and watch the active-cell percentage and
  the internal step count in the report's 2D solver statistics block climb.
- **Set `VFR_MIN_WET_FRAC`** down toward its lower bound and see how much extra
  substepping the sharper shoreline costs.

## Related

- \ref manual_2d_mesh — mesh generation, editing and cell parameters in full
- \ref manual_simulation_options — the 2D Surface Routing page
- \ref manual_hydrology — rain gages and time series
- \ref manual_results — 2D results layers, styling and animation
- \ref manual_analysis_tools — rainfall visualisation and 2D cell time series
- \ref manual_running — the Simulation Status panel and the status report
- \ref tutorial_2d_inundation — from a DTM raster to an animated inundation map
- \ref tutorial_2d_boundaries — boundary types and edge conveyance in detail
- \ref manual_troubleshooting — mesh generation failures and retired options
