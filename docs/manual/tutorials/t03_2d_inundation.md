@page tutorial_2d_inundation T3 — From a DTM to an Animated 2D Inundation Map

## Goal

Take the Snoopy Lagoon demo — a parabolic bowl that fills under a short storm
and drains through a single coupled junction — and turn it into an animated
inundation map. You will open the project, look at the terrain and the mesh,
style both, load or produce 2D results, animate depth, pull a time series out
of one cell, cut a profile across the bowl, and read the 2D side of the mass
balance. Along the way you will see how the mesh would be built from scratch
with the mesh generator.

\videotodo{Opening the Snoopy Lagoon project — styling the mesh — animating 2D depth across the bowl}

## Capabilities exercised

`.oswp` projects · raster layers · the 2D mesh layer and its style panel ·
mesh attribute tables · the mesh generator · Simulation Options →
2D Surface Routing · `Add 2D Results…` · results playback · the 2D results
style panel · 2D cell time series · 2D mesh profiles · the 2D continuity
ledger.

## Files

| File | What it is |
| ---- | ---------- |
| `examples/demo_snoopy_lagoon/snoopy.oswp` | The SWMMVis project — **open this one** |
| `examples/demo_snoopy_lagoon/snoopy.inp` | The SWMM input file |
| `examples/demo_snoopy_lagoon/snoopy.2dm` | The external mesh — 81 vertices, 128 triangles |
| `examples/demo_snoopy_lagoon/snoopy_dtm.npz` | A synthetic DTM grid as a NumPy archive |
| `examples/demo_snoopy_lagoon/snoopy.2d.h5` | Checked-in 2D results — 474 steps over 4 hours, 128 cells |
| `examples/demo_snoopy_lagoon/snoopy.rpt` | Checked-in report from a **1D-only** run |
| `examples/demo_snoopy_lagoon/gen_demo.py` | The generator that produced the geometry |

The terrain is a parabolic bowl 100 m in radius, deepest at the centre at
z = 97.0 m and rising to z = 100.0 m at the rim. Junction `J1` sits at the
centre with an invert of 96.5 m and a max depth of 0.6 m; a chain of four
0.6 m circular conduits `C1`–`C4` runs 130 m east to the free outfall `OUT1`
at 95.5 m. Flow units are `CMS`, map units are metres, routing is `DYNWAVE`,
and the run is four hours with a 30-second report step.

Two facts about this example matter before you start.

**The mesh is attached through the project, not the `.inp`.** The checked-in
`snoopy.inp` contains **no `[2D_*]` sections and no `[2D_MESH_FILE]`
reference**. What ties `snoopy.2dm` to the model is the `meshLayers` entry in
`snoopy.oswp`. If you open the `.inp` on its own you get a bare 1D network;
if you open the `.oswp` you get the mesh layer as well. That is also why the
checked-in `snoopy.rpt` is an all-zero **1D-only** report — with no rain gage
attached to any subcatchment (there are none) and no 2D mesh reachable from
the `.inp`, the engine had nothing to route.

**The DTM is a `.npz`, not a raster SWMMVis can open.** `snoopy_dtm.npz` is a
NumPy archive holding `x` (341 values), `y` (241 values) and a
241 × 341 float32 `z` grid spanning x = −120…220 m, y = −120…120 m at 1 m
spacing, with elevations from 97.0 to 100.0 m. **Add Raster Data** goes
through GDAL, whose filter list covers GeoTIFF, ESRI ASCII grid, Erdas
Imagine, SRTM, USGS DEM, ENVI, NetCDF, HDF5 and friends — but not `.npz`. To
follow the raster steps below, convert it once, for example with `rasterio` or
`gdal_translate` after writing an intermediate ASCII grid, and keep the result
as `snoopy_dtm.tif`. (`gen_demo.py` was originally written to emit a GeoTIFF;
the checked-in artefact is the `.npz`.)

## Steps

### 1. Open the project

**File → Open…** (`Ctrl+O`) → **Open SWMM Model or Project** →
`examples/demo_snoopy_lagoon/snoopy.oswp`.

The 1D network — five nodes in a line running east from the bowl centre —
draws on the canvas, and the **Layers** panel gains a mesh layer sourced from
`snoopy.2dm`. Because a mesh layer is on the canvas, the contextual
**Mesh 2D** ribbon tab appears, with the groups **Mesh**, **Cell Data**,
**Groundwater (2D)**, **Vertices**, **Edges**, **2D Results**, **Profile**
and **Coupling**.

\figtodo{t03_project_open.png, The Snoopy Lagoon project with the mesh layer and the 1D network}

### 2. Add the DTM as a raster layer and style it

Once you have a GDAL-readable copy of the terrain (see above):

1. **File → Import → Add Raster Data** (ribbon **Home → Import → Raster
   Data**) opens **Add Raster Layer**. The first filter entry is
   **All supported (…)**; **GeoTIFF / COG (\*.tif \*.tiff)** is the one you
   want here.
2. Right-click the new layer in the **Layers** panel → **Properties…** to open
   *<name> — Layer Properties*, and go to the **Symbology** tab. Its **Layer
   type** on the **Information** tab reads **Raster / DEM**.
3. The raster symbology panel opens on the **Renderer:** the DTM was given at
   load time — **Singleband pseudocolor** (the alternatives are **Paletted /
   unique values** for categorical grids and, for 8-bit RGB imagery,
   **Multiband colour**). **Source** has **Render band:** and **NoData
   value:**; the **Classification** block is the same one the 2D layers use —
   **Continuous / Classified**, **Colour ramp** with **Invert**, **Method:**
   and **Classes:**, **Custom range**, a per-class table whose colours and
   labels you can edit, and **Auto-classify from data** — followed by **Clip
   out-of-range values**; **Hillshade relief** has **Enable relief shading**,
   **Azimuth:**, **Altitude:**, **Z factor:** and **Strength:**.

The stretch already spans the band's own minimum and maximum (97–100 m here),
so the 3 m bowl renders across the full ramp without any manual range. Pick a
terrain-friendly ramp such as **terrain**, or switch to **Classified** with
**Quantile** and press **Auto-classify from data** to get evenly populated
elevation bands, then enable relief shading with a **Z factor:** of 5 or more.
A 3 m bowl over 200 m is a very gentle dish; without exaggeration it looks flat.

\figtodo{t03_raster_style.png, The raster symbology editor with an auto-stretched ramp and hillshade enabled}

The **Terrain** ribbon tab appears once a raster is loaded, with an
**Active Terrain** group (a combo listing `(none)` and every raster), a
**Vertical Units** group (**DEM:** in **m (metres)** or **ft (feet)**, plus a
`×` factor) and an **Invert Offsets** group.

### 3. Inspect and style the mesh layer

Select the mesh layer and open its **Symbology** tab (or use the **Layer
Styling** dock, `Ctrl+Alt+8`). The mesh style panel has seven tabs:

| Tab | What it draws |
| --- | ------------- |
| **Terrain Fill** | flat-shaded cells coloured by a cell attribute |
| **Elevation Bands** | filled elevation bands, optionally interpolated with marching triangles |
| **Elevation Isolines** | contour lines with **Line count:**, **Line colour:**, **Line width:** and **Show elevation labels** |
| **Mesh Edges** | the wireframe — **Colour:**, **Width:**, **Stroke style:**, **Show edges at:** (px/cell) and a **Slope emphasis** group that widens steep edges |
| **Mesh Vertices** | vertex markers — **Colour:**, **Marker size:**, outline and **Show vertices at:** |
| **Boundary Conditions** | per-BC-type colour and width; types absent from the mesh are marked *(none in mesh)* |
| **Coupled Nodes** | the mesh vertices coupled to 1D nodes — **Colour:** and **Marker size:** |

On **Terrain Fill**, set **Attribute:** to **Bed elevation** and let the
**Classification** block do the work: **Display:** (**Continuous (smooth
ramp)** or **Classified (discrete bands)**), **Colour ramp:** with **Invert**,
**Method:** (**Equal interval**, **Quantile**, **Natural breaks (Jenks)**,
**Standard deviation**, **Logarithmic**, **Exponential** or **Manual**),
**Classes:** and **Range:**. Press **Auto-classify from data**. The
**Hillshade (sun lighting)** group beneath it has **Azimuth (light from):**,
**Altitude (sun angle):**, **Vertical exaggeration:** and **Shadow floor:** —
again, exaggerate.

The **Attribute:** combo also lists every cell parameter: **Manning's n**,
**Initial Depth**, **Infiltration Method** and the infiltration parameters.
Colour by **Manning's n** to confirm the whole bowl is 0.035.

Turn on **Coupled Nodes** to see the single coupled vertex at the bowl centre.

\figtodo{t03_mesh_style_panel.png, The mesh style panel on the Terrain Fill tab}

\figtodo{t03_mesh_elevation.png, The mesh coloured by bed elevation with hillshade and the wireframe visible}

### 4. Read the mesh attribute tables

Open the **Attribute Table** dock (`Ctrl+Alt+4`). Its **Category:** combo now
carries three mesh entries in the form *△ Mesh &lt;name&gt; — Vertices (81)*,
*△ Mesh &lt;name&gt; — Edges (n)* and *△ Mesh &lt;name&gt; — Cells (128)*. A
layer-tree right-click on the mesh has **Open Attribute Table**, which jumps
straight to Vertices.

| Table | Columns |
| ----- | ------- |
| **Vertices** | `Index`, `X`, `Y`, `Marker`, `Elevation`, `Tag`, `Coupled Node`, `Coupling Cd`, `Coupling Area` |
| **Edges** | `Edge`, `Boundary`, `Length (map units)`, `Conveyance`, `Stage`, `Bed Slope`, `Flow`, `Time Series`, `Rating Curve`, `Group` |
| **Cells** | `Index`, `Area (map units²)`, `Centroid X`, `Centroid Y`, `Tag`, then one column per cell parameter — `Manning's n`, `Initial Depth`, `Infiltration Method` and the rest |

Sort **Vertices** by `Elevation` and the deepest row — index 0 at (0, 0),
z = 97.0 m — is the one carrying `Coupled Node` = `J1`, `Coupling Cd` = 0.65
and `Coupling Area` = 1.0. The vertices sit on five rings at radii 0, 25, 50,
75 and 100 m.

The toolbar has **Show selected only**, **Zoom to selected**, **Copy
(Ctrl+C)**, **Export CSV…**, a **Selection:** mode group (**Replace**,
**Add**, **Subtract**, **Intersect**, **Invert**) and a **Query:** box with
**Apply** and **Clear**. The right-click menu on mesh rows can push one value
to every selected row — *Apply "&lt;col&gt;" value to N selected rows…*

\figtodo{t03_mesh_attribute_table.png, The mesh Vertices table sorted by elevation with the coupled vertex highlighted}

### 5. How you would build this mesh from scratch

You do not need to do this to finish the tutorial — the mesh already exists —
but it is worth walking the dialog once with the DTM loaded.

**Model → Generate Mesh…** (ribbon **Model → Mesh 2D → Generate Mesh**, or
**Mesh 2D → Mesh**) opens **Generate 2D Mesh**, with tabs **Sources**,
**Quality** and **Hydraulics** over a permanent footer.

On **Sources**:

| Control | Note |
| ------- | ---- |
| **DTM raster:** | `(none — use junction rim elevations)` or any loaded raster — pick the converted DTM |
| **DTM vertical unit:** | read-only, derived from the raster |
| **Mesh vertical unit:** | **Match flow units (auto-convert)**, **Metres (m)** or **Feet (ft)** |
| **Z conversion (×):** | a manual scale on top of that |
| **Domain:** | read-only — always the model extent |
| **Boundary polygon:** | `(none)`, **Use SWMM subcatchment polygons**, or any loaded polygon layer |
| **Constraining points / lines** | checkable lists of point and line layers, each with a **use Z** option |
| **Junctions / outfalls / storage → Steiner vertices** | forces a mesh vertex at each node — this is what put vertex 0 exactly on `J1` |
| **Conduits → constraint segments**, **Subcatchments → triangle regions** | the 1D-geometry influence group |
| **Map model nodes to the mesh after generation** | the 1D ↔ 2D coupling checkbox |
| **Method:** | **Inverse distance weighting (IDW)** or **Natural neighbour**, used only when there is no DTM |

There is **no "draw a boundary polygon" tool and no "bowl" preset** — the
domain is always the model extent, and the only way to restrict it is to load
a polygon layer and choose it in **Boundary polygon:**. For this demo the
bowl's own rim is the natural boundary, so digitise a 100 m circle as a
shapefile or GeoPackage polygon, load it with **File → Import → Add Vector
Data**, and select it there.

**Quality** carries **Max triangle area:**, **Min angle:**, **Size
gradation:**, **Max Steiner points:** and the **Minimum Cell Size** group
(with a **Suggest** button); **Hydraulics** sets the **Initial cell values**
(**Roughness (Manning's n):** = 0.035 here, **Initial depth:**) and a
region-defaults table.

The footer chooses **Output:** — **External .2dm** (what this example uses) or
**Inline in .inp** — with a **Mesh file:** path, then **Generate**.

\figtodo{t03_mesh_generation_sources.png, The Generate 2D Mesh dialog on the Sources tab with a DTM selected}

After generation, the **Mesh 2D → Coupling** group's **Auto-couple** button
couples mesh vertices to coincident SWMM nodes, and **Remap 1D↔2D** clears
and rebuilds every coupling — vertex-coupling nodes that sit on a vertex and
cell-coupling the rest. To set the coupling by hand, activate the vertex tool
in **Mesh 2D → Vertices**, click the vertex, and fill the **Coupled SWMM
node** combo, the **Cd:** spin (0.001–1.0, default 0.65) and the **Area:**
spin — they write the `[2D_VERTEX_NODE_MAP]` `CD` and `AREA` columns and only
appear once the vertex is coupled.

### 6. 2D options in Simulation Options

**Model → Simulation Options…**. On **Models / Processes**, the **Modules**
group has a **2D Surface Routing** checkbox; the **2D Surface Routing**
category in the left-hand list is greyed until it is ticked.

On the **Mesh** page you will see the search directory, the list of `.2dm`
files found beside the project, and **Set Active**, **Remove**, **Import…**
and **Refresh**. Because `snoopy.inp` has no mesh reference, the summary line
reads *Active mesh reference: &lt;none — generate a 2D mesh first&gt;*.
Selecting `snoopy.2dm` and pressing **Set Active** writes the
`[2D_MESH_FILE]` block into the `.inp`, which is what makes the mesh visible
to the engine as well as to the canvas.

The **2D Surface Routing** page maps one-to-one onto `[2D_OPTIONS]`:

| Group | Control | `[2D_OPTIONS]` key |
| ----- | ------- | ------------------ |
| Time stepping | **Max timestep:** (s) | `MAX_TIMESTEP` |
| Explicit marcher | **Momentum θ:** | `THETA` |
| | **CFL number:** | `CFL_NUMBER` |
| | **LTS tiers:** | `LTS_TIERS` |
| | **Movement threshold:** (m) | `H_MOVE` |
| | **Max Froude number:** | `FROUDE_MAX` |
| | **Convective momentum flux (ADVECTION)** | `ADVECTION` |
| Performance | **Backend:** — **Auto / CPU (built-in marcher) / OpenMP (Kokkos) / CUDA / HIP / SYCL** | `BACKEND` |
| Mesh | **Dry depth threshold:** (m) | `DRY_DEPTH` |
| | **Limiter epsilon:** | `LIMITER_EPSILON` |
| | **Flux head epsilon:** (m) | `FLUX_DH_EPS` |
| Cell closure | **Cell closure:** — **Flat (η = z̄ + V/A, legacy)** or **VFR (planar-bed volume/free-surface)** | `CELL_CLOSURE` |
| | **Face reconstruction:** — **Mean (upwind cell depth, legacy)** or **VFR face (edge depth + wetting gate)** | `FACE_RECONSTRUCTION` |
| | **VFR min wet fraction:** | `VFR_MIN_WET_FRAC` |
| 1D ↔ 2D coupling | **Coupling Cd:** | `COUPLING_CD` |
| | **Exchange interval:** (s — **Every routing step** at 0) | `COUPLING_SYNC` |
| | **Derive exchange areas automatically (COUPLING_AREA AUTO)** | `COUPLING_AREA` |
| Rainfall | **Rainfall mode:** — **Natural neighbour (all gages)**, **System (uniform gage mean)** or **None (no direct rainfall)** | `RAINFALL_MODE` |
| Output | **Write 2D results to output (REPORT_2D)** | `REPORT_2D` |
| | **2D results file:** with **Browse…** | `OUTPUT_FILE` |

**Rainfall mode:** is the control that matters most for this demo. `snoopy.inp`
has **no subcatchments** — `GAGE1` feeds nothing in 1D. The bowl fills only
because rain falls directly on the mesh, which is what `RAINFALL_MODE` decides.
With **None (no direct rainfall)** nothing happens at all.

\figtodo{t03_sim_options_2d.png, The 2D Surface Routing page of Simulation Options}

### 7. Run, or load the checked-in results

**Analysis → Execute** (`Ctrl+R`) runs the model. If 2D Surface Routing is
enabled but no mesh resolves from the `.inp`, SWMMVis stops with the
**2D mesh not found** dialog — *The simulation will run as 1D-only … Continue
with a 1D-only run?* — which is precisely the state a freshly opened
`snoopy.inp` is in until you press **Set Active** on the Mesh page. When 2D is
enabled and a mesh does resolve but `[2D_OPTIONS]` has no `OUTPUT_FILE`,
SWMMVis defaults it to `<model>.2d.h5` so the run leaves something scrubbable
on disk.

To skip the run entirely, load the results that ship with the example:
**File → Import → Add 2D Results…** (ribbon **Home → Import → 2D Results**)
opens the **Add 2D Results** dialog with the filter
`OpenSWMM 2D Results (*.h5)`. Pick `snoopy.2d.h5`. A 2D results layer joins
the **Layers** panel, and the **Mesh 2D** tab's **2D Results** group becomes
usable.

The checked-in `snoopy.2d.h5` holds **474 time steps over the full four
hours** on **128 cells**.

\figtodo{t03_add_2d_results.png, The Add 2D Results file dialog}

### 8. Animate the inundation

Everything below is on the **Results** ribbon tab.

1. **Set Style** opens the 2D results style panel. Its tabs are **Cell Depth
   Fill**, **Smooth Depth Fill**, **Depth Contours**, **Depth Isolines**,
   **Flow Velocity**, **Mesh Edges** and **Mesh Vertices**.
2. Tick **Show cell depth fill** (or **Show smooth depth fill** for a
   marching-triangle interpolation instead of flat cells). The **Attribute:**
   combo on both offers exactly **Depth** and **Elevation** — there is no
   water-surface option in the fill styler; water surface appears only as the
   **Water surface** series in the 2D profile plot.
3. Set the **Classification** **Range:** deliberately. **Fixed over run**
   locks the ramp to the whole run's extremes, **Per-frame auto-stretch**
   rescales every frame, and **Fixed (user range)** lets you type **Min:** and
   **Max:**. On this dataset the maximum depth anywhere at any time is
   **0.018 m** — 18 mm — so an automatic 0–1 m ramp shows nothing. Use a fixed
   user range of 0 to 0.02 m.
4. The classification's automatic range runs from the run's dry depth up to
   its maximum depth; the dry depth itself is shown read-only on the layer's
   **Metadata** tab as **Dry depth**, beside **Vertices**, **Cells
   (triangles)**, **Time steps**, **Time range** and **Velocity flux**.
5. **Show Legend** puts the ramp on the canvas.
6. In the **Timeline** group, scrub with the slider, set **Window:** (minutes)
   for the look-back band, read or type the time in the `MM/dd/yyyy hh:mm`
   box, choose a **Speed:** (0.25× … 8×) and leave **Cycle** ticked.
7. **Play**. The **Display** group's **Live 2D** and **Live 1D** checkboxes
   control whether a *running* simulation streams into the canvas; for a
   loaded `.h5` they are irrelevant.

The bowl wets from the rim inward as the 30-minute pulse falls, reaches its
maximum around **00:08**, and drains through `J1` for the rest of the run;
at the peak **127 of the 128 cells** carry more than a millimetre of water.

On the **Flow Velocity** tab, tick **Show velocity vectors** to overlay
arrows, sized by **Length scaling:** (**Linear**, **Square root** or
**Logarithmic**), **Scale:** (px per m/s), **Min length:**, **Max length:**,
**Head size:** and **Shaft width:**, coloured by magnitude or a single colour,
and thinned with **Spacing:** and **Dry depth cutoff:** — the last is the only
dry-depth control in the styling UI, and it suppresses arrows below the depth
you set. On an 18 mm dataset set it to a millimetre or less or you will see no
arrows at all.

\figtodo{t03_results_style_panel.png, The 2D results style panel on the Cell Depth Fill tab}

\figtodo{t03_animation_peak.png, The bowl at maximum inundation with the depth ramp and legend}

\figtodo{t03_velocity_vectors.png, Velocity vectors over the draining bowl}

### 9. A time series from one cell

Activate the cell picker in **Mesh 2D → 2D Results** (an icon-only tool). Its
tooltip explains the modifiers: *Single-click selects and highlights a cell
(Shift = add, Ctrl = toggle); drag a box or press L to lasso multiple.
Right-click a selection to plot its depth / HGL / velocity time series. Esc
clears.* The toolbar readout beside it reads **Cell: (none)**, then
**Cell #N** or **Cells: N selected**.

Click the cell at the bowl centre and right-click it. The attribute menu
offers **Depth (2D cell) (m)**, **HGL (2D cell) (m)**, **|V| (2D cell)
(m/s)**, **Vx (2D cell) (m/s)**, **Vy (2D cell) (m/s)**, **Rainfall (2D
cell)**, **Rainfall volume (2D cell) (m³)**, and finally **All attributes**.
Entries the results file cannot supply are disabled with a tooltip.

The series opens in the **Comparison Plot**, the same window the 1D time
series use — there is no separate cell-time-series window. Pick a rim cell and
a centre cell and plot both depths together: the rim wets first and dries
first, the centre lags and holds water longest.

\figtodo{t03_cell_timeseries.png, Depth time series for a rim cell and a centre cell in the Comparison Plot}

### 10. A profile across the bowl

**Analysis → Plot 2D Profile** arms the polyline tool: *Click to add vertices
— double-click or Enter to finish — right-click to undo — Esc to cancel.* Draw
a line straight across the bowl through the centre, west to east.

The **2D Mesh Profile** window draws the terrain plus the active 2D results
layer's animated water depth and its maximum-depth envelope. The legend rows
are **Ground**, **Depth (current)**, **Water surface** and **Max depth**; the
axes are **Distance** and **Elevation**. Its toolbar carries **Select**,
**Zoom In**, **Zoom Out**, **Fit**, **Pan**, **Cell boundaries** (dots where
the line crosses mesh-cell edges), **Move marker on map** (drag the position
arrow along the profile on the canvas) and **Display Options…**, which opens
**Profile Display Options** with toggles for the depth fill, the water-surface
line, the maximum envelope fill and line, cell boundaries, pens and brushes,
legend and timestamp.

Press **Play** on the Results tab and watch the water surface rise and fall
along the section.

The **Mesh 2D → Profile** tool is the *terrain-only* twin of this: it plots
bed elevation along a polyline without the results overlay. **Model → Terrain
Profile** does the same for a raster, in the **Terrain Profile** window.

\figtodo{t03_2d_profile.png, A 2D mesh profile across the bowl with the ground line and water surface}

### 11. The 2D mass-balance ledger

**Analysis → Show Mass Balance** — like **Analysis → Report** — opens the
**Report Viewer** on the run's `.rpt`. There is no separate mass-balance
dialog; the ledger is the engine's own text, and the viewer's left pane
navigates to it.

A 2D run adds these blocks to the ordinary 1D ones:

| Section | Rows |
| ------- | ---- |
| `2D Surface Routing Continuity` | `Initial Stored Volume`, `Rainfall Inflow`, `1D -> 2D Spill Inflow`, `Outfall Inflow`, `Boundary Inflow`, `2D -> 1D Drain Outflow`, `Outfall Withdrawal`, `Boundary Outflow`, `Evaporation Loss`, `Infiltration Loss`, `Final Stored Volume`, `Continuity Error (%)` |
| `2D Solver Statistics` | `Internal Steps`, `Face-Kernel Evals`, `Avg Internal Step (s)`, `Last Internal Step (s)`, active-cell percentages and per-tier LTS occupancy |
| `1D <-> 2D Exchange Reconcil.` | `1D -> 2D Spill`, `2D -> 1D Drain`, `Net 1D -> 2D`, and `Flow Continuity w/ Exchange Internal (%)` |

For this model the interesting rows are `Rainfall Inflow` — every drop that
enters the system, since there are no subcatchments — and
`2D -> 1D Drain Outflow`, the water leaving the bowl through the coupled
vertex into `J1`. In the checked-in `snoopy.2d.h5` the coupling flux at the
centre cell peaks at about **−0.0024 m³/s** (negative meaning 2D draining into
1D), which is what that ledger row integrates.

The checked-in `snoopy.rpt` has **none of these blocks** — it is a 1D-only run
with every row zero, `Rainfall/Runoff ........ NO`, and a 0.000 % continuity
error. `snoopy_fresh.rpt` beside it is the same run reported in SI units, with
one extra line: `WARNING 02: maximum depth increased for Node J1.` The Report
Viewer shows a banner when continuity exceeds 10 %.

\figtodo{t03_mass_balance_2d.png, The Report Viewer on the 2D Surface Routing Continuity block}

## What to look for

- **Fixed classification ranges.** With an 18 mm maximum, a per-frame stretch
  turns numerical noise into a flood. Fix the range once and leave it.
- **Where the water is deepest.** The bowl centre holds water longest because
  it is the only outlet, and the outlet is a 0.6 m pipe through a junction
  whose max depth is 0.6 m.
- **The coupled vertex.** Turn on the **Coupled Nodes** tab in the mesh style
  panel: exactly one vertex should be marked. Every drop that leaves the
  surface goes through it.
- **The 2D continuity error.** On a coupled run it should be small. A large
  one usually means the CFL number or the maximum time step is too generous
  for the cell size.
- **The `.2d.h5` is the record.** The 1D `.out` knows nothing about surface
  depth; if you close the project and reopen it, **Add 2D Results…** is how
  you get the animation back.

## Variations

### Rainfall mode

Cycle **Simulation Options → 2D Surface Routing → Rainfall mode:** through its
three values and re-run:

- **Natural neighbour (all gages)** interpolates every gage across the mesh.
  With one gage this is uniform, so it matches the next option.
- **System (uniform gage mean)** applies the mean of all gages to every cell.
- **None (no direct rainfall)** removes the only forcing this model has —
  the bowl stays dry and the 2D ledger's `Rainfall Inflow` goes to zero. Use
  it to confirm that the storm really is arriving through the mesh and not
  through a subcatchment.

### Cell size

Regenerate the mesh with a tighter **Max triangle area:** or a smaller
**Minimum cell size:** on the **Quality** tab of **Generate 2D Mesh**. Finer
cells resolve the rim's wetting front better and raise the peak depth, at a
quadratic cost in cells and a linear one in time steps. Compare the same cell
location's depth series across the two runs in one Comparison Plot.

### CFL number

**Simulation Options → 2D Surface Routing → CFL number:** governs the explicit
marcher's stability. Lower it (0.5, 0.3) and the run slows but the 2D
continuity error should shrink; raise it and watch the error grow and
eventually the depth field go ragged. `2D Solver Statistics` in the report
tells you what it cost — `Internal Steps` and `Avg Internal Step (s)`.

Related knobs on the same page: **Max timestep:**, **Movement threshold:**
(`H_MOVE`), **Max Froude number:** and **Dry depth threshold:**. The
**Cell closure** group's **VFR (planar-bed volume/free-surface)** option is
the one to try when a shallow, gently sloping surface like this bowl wets and
dries unrealistically at the margins.

## Related

- \ref manual_2d_mesh — mesh generation, mesh editing, cell parameters and coupling
- \ref manual_layers — raster and vector data sources
- \ref manual_styling — colour ramps and the classification editor
- \ref manual_attribute_tables — attribute tables including the mesh tables
- \ref manual_simulation_options — every page of the Simulation Options dialog
- \ref manual_running — running and the Report Viewer
- \ref manual_results — results layers and map animation
- \ref manual_profile_plots — 2D mesh profiles and terrain profiles
- \ref manual_analysis_tools — 2D cell time series
- \ref tutorial_1d2d_coupling — the next tutorial
- \ref tutorial_pure_2d — a stand-alone 2D model with system rainfall
