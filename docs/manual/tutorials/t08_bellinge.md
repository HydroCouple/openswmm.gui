@page tutorial_bellinge T8 — Bellinge: A Real-World Catchment with an External Mesh; DEM; Long Rain Records and Basemaps

## Goal

Everything until now has been a toy. Bellinge is a real municipal drainage
model of a suburb of Odense, Denmark: 713 subcatchments, 995 junctions, 1015
conduits, pumps, orifices, weirs, outlets, control rules, dry-weather flow, and
a 155 688-triangle overland-flow mesh in an external `.2dm` file. This tutorial
is about **working at that size** — opening it without waiting, keeping the
Layers panel legible, selecting sensibly, running with threads, animating a big
results set, and comparing two runs.

## Capabilities exercised

- Opening a bundled example from the Welcome page (copy-on-open)
- `[2D_MESH_FILE]` — an external mesh with 78 285 vertices and 155 688 triangles
- Adding a GeoTIFF DEM and a web basemap; reconciling two coordinate systems
- Layers panel management, selection on a large network, the Object Browser filter
- `THREADS`, `NODE_CONTINUITY SEMI_IMPLICIT`, `ANDERSON_ACCEL`
- The control rules editor, rainfall visualisation, profile plots, flow balance
- The comparison plot across two runs

## Files

`examples/bellinge_2d/`:

| File | Purpose |
|---|---|
| `BellingeSWMM_v021_nopervious.oswp` | GUI project — styling, legend, mesh display state |
| `BellingeSWMM_v021_nopervious.inp` | 1D network plus `[2D_OPTIONS]` and `[2D_MESH_FILE]` (17 704 lines) |
| `BellingeSWMM_v021_nopervious.2dm` | external overland-flow mesh (7.5 MB) |
| `rg_bellinge_Jun2010_Aug2021.dat` | two-gage rain record (26.6 MB; 749 401 rows) |
| `output_SRTMGL1.tif` (+ `.aux.xml`) | SRTM DEM — **not** referenced by the project |
| `example.json` | Welcome-page metadata |

The example ships **clean**: no `.out`, no `.rpt`, no `.2d.h5`. Every number
you see will be one you produced.

The dataset derives from the open Bellinge dataset published by the University
of Southern Denmark and VCS Denmark.

\figtodo{t08_bellinge_overview.png, The Bellinge network over a basemap with the 2D mesh visible}

## Steps

### 1. Open it from the Welcome page

The Welcome page lists every bundled example as a command link. Bellinge's
reads **Bellinge 2D Urban Flood Model** with the description *Coupled 1D
drainage network and 2D overland-flow mesh for the Bellinge catchment (Odense,
Denmark), with an 11-year rain-gage record and an SRTM terrain raster (add it
via Layers if desired).* (Examples with no metadata fall back to
*Copy to a folder you choose, then open*.)

Click it. SWMMVis **never opens an example in place** — simulation results land
next to the `.inp`, which would pollute the seeded baseline. Instead it asks
**Choose a folder to copy "Bellinge 2D Urban Flood Model" into**, starting at
your Documents folder, copies the whole folder, and opens the copy.

If a copy already exists at that path you get **Example copy already exists** —
*"…" already exists.* / *Open the existing copy, or replace it with a fresh copy
of the example?* — with **Open Existing** (the default), **Replace** and
**Cancel**.

The project opens through `BellingeSWMM_v021_nopervious.oswp` because the
Welcome page prefers a `.oswp` over an `.inp` when both are present.

If the Welcome page is not showing, **View → Show Welcome** brings it back; the
*show welcome on startup* checkbox controls whether it appears automatically.

\figtodo{t08_welcome_examples.png, The Welcome page with the bundled examples list}

### 2. Watch the load

A model this size loads in stages, and the **Message Logs** dock
(**Ctrl+Alt+7**) narrates each one. The lines to expect:

```
Scanning …inp for a 2D mesh …
2D mesh parsed: 78,285 vertices, 155,688 triangles (N s) — building scene
  geometry, spatial index and LOD pyramid …
2D mesh visible (coarse) — finishing wireframe and spatial index in the
  background …
2D mesh fully ready: wireframe, spatial index and editing structures built (N s).
```

The mesh becomes **visible coarse first** and finishes its wireframe, spatial
index and editing structures on a worker thread, so the canvas is usable before
the mesh is fully ready. A load that stops at *Loading the 2D mesh failed: …*
is a real failure; one that reaches *2D mesh parsed* is progressing.

Two useful facts about performance work already done here: applying the `.oswp`
sidecar used to be quadratic in object count and took over 22 seconds on a model
of this class — it is now around 250 ms; and the mesh reader's coupled-vertex
tag lookup is now a hash rather than a linear scan over 78 285 vertices, so
Bellinge's 977 vertex couplings and 43 triangle couplings resolve immediately.

The saved mesh display state — active flag, edge and node visibility, the four
hillshade parameters and the seven contour parameters — is restored from the
`.oswp` after the asynchronous mesh load joins the canvas, so the layer comes
back looking the way you left it.

### 3. Add the DEM and a basemap, and sort out the CRS

The project is in **EPSG:25832** (ETRS89 / UTM 32N); the `.2dm` header says so
explicitly:

```
;; UNITS: metre
;; SOURCE_CRS: EPSG:25832
```

and the `.oswp` canvas extent is in UTM metres.

The bundled DEM is **not**. `output_SRTMGL1.tif` is 5635 × 2660 pixels of
16-bit signed elevation in **EPSG:4326** at 1 arc-second (0.000277778°), with
NoData −32768 and an approximate range of −7 to 100 m. Its footprint —
roughly 9.59–11.16 °E, 54.94–55.68 °N — is far larger than the catchment.

1. **File → Import → Add Raster Data…** and pick `output_SRTMGL1.tif`. It
   appears under **Raster Layers** in the Layers panel and is reprojected on the
   fly for display.
2. **File → Import → Add Basemap…** opens the **Add Basemap** dialog. Six tabs:
   **XYZ Tiles**, **WMS / WMTS**, **WCS**, **ArcGIS REST**, **Local File**,
   **WFS**. The XYZ tab ships five built-in, non-deletable providers:

   | Name | Zoom |
   |---|---|
   | **OpenStreetMap** | 0–19 |
   | **CartoDB Positron** | 0–20 |
   | **CartoDB Dark Matter** | 0–20 |
   | **Stadia Alidade Smooth** | 0–20 |
   | **ESRI World Imagery** | 0–23 |

   Pick **CartoDB Positron** for a quiet backdrop under a dense network, or
   **ESRI World Imagery** to see roofs and roads under the mesh. **Test
   Connection** checks the URL before you commit. Use the **Local File** tab if
   you would rather have the SRTM GeoTIFF itself as the backdrop — that tab
   takes a local image plus a world file and a CRS assignment.
3. **Check the CRS.** If the project CRS is wrong, **Change Coordinate
   Reference System** offers two very different remedies under
   *How should the change be applied?*:

   - **Reproject stored coordinates** — permanently transforms every node, link
     vertex and subcatchment. *Use when the new CRS is the correct one for the
     data.*
   - **Re-render only (display in new CRS)** — keeps stored coordinates and
     reprojects on the fly. *Use when the source CRS is correct.*

   For Bellinge the stored coordinates are already correct, so you should never
   need the first option. Reprojecting a 995-node, 1015-link, 713-polygon model
   by accident is not something you want to discover after saving.

\figtodo{t08_add_basemap_xyz.png, The Add Basemap dialog on the XYZ Tiles tab with the built-in providers}

\videotodo{Adding an XYZ basemap and the SRTM DEM to the Bellinge project and checking the CRS}

### 4. Manage the Layers panel

With a basemap, a DEM, a mesh, a model and (soon) two results layers on the
canvas, the Layers panel earns its keep. Layers are grouped into eight
categories, each hidden when empty:

**SWMM**, **Meshes**, **SWMM 1D Outputs**, **SWMM 2D Outputs**,
**Feature Layers**, **Raster Layers**, **Basemaps**, **Tables**.

Right-click a category to move it up or down; right-click a layer for its
context menu:

| Entry | Notes |
|---|---|
| **Zoom to Layer** | |
| **Open Attribute Table** | vector, model, tabular and results layers |
| **Plot Time Series…** | results layers |
| **Set as Active Results Layer** | checkable; 1D and 2D results |
| **Move Up** / **Move Down** | enabled per canvas position |
| **Remove Layer** | |
| **Hide Layer** / **Show Layer** | label flips with the current state |
| **Properties…** | |
| **Styles → Edit Symbology…** | |

Several entries in that menu are visible but **disabled placeholders** today:
*Show in Overview*, *Show Feature Count*, *Move to Top*, *Move to Bottom*,
*Rename Layer*, *Duplicate Layer*, *Filter…*, *Set Layer Scale Visibility…*,
*Copy Style* and *Paste Style*. Do not plan a workflow around them.

The `.oswp` stores per-kind renderers for **Conduits, Dividers, Junctions,
Orifices, Outfalls, Outlets, Pumps, RainGages, Storage, Subcatchments** and
**Weirs**, so the network comes up with its symbology already set. Note that
the shipped project stores **no basemap and no results layers** — those are
yours to add, and they will be saved with the project when you next save it.

\figtodo{t08_layers_panel_large.png, The Layers panel with all eight categories populated}

### 5. Performance settings

The canvas splits repaints into four independent **dirty channels** — Raster
(150 ms debounce), Scene (50 ms), Overlay and Extent (both immediate) — so a
selection click never recomposes the basemap. \ref manual_performance covers
the whole picture; the settings that matter most on a model this size are:

| Where | Control | Effect |
|---|---|---|
| **Preferences → Rendering → GPU Rendering** | **Use GPU rendering for SWMM layers (recommended)** | keep on |
| | **Use GPU rendering for 2D terrain mesh layers (recommended)** | keep on for a 155k-triangle mesh |
| **Preferences → Rendering → Label Rendering** | **Label zoom-out threshold (m11)** | raise it to stop 995 junction labels rendering when zoomed out |
| **Simulation Options → System / Performance** | **Worker threads:** | see step 8 |

If a redraw feels wrong, launch with `SWMMVIS_LOG_REDRAW=1` and watch which
channel a gesture dirties. A selection change that fires **Raster** is a bug.

One caveat to know before you rely on it: saving an external `.2dm` back out
does **not** produce a byte-identical file between runs, because the
vertex-node map is written in hash order. Expect spurious diffs if the mesh is
under version control.

### 6. Selection on a big network

Clicking 995 junctions is not a plan. Three tools scale:

**Find by name.** **Edit → Search** (**Ctrl+F**) raises the **Object Browser**
dock and focuses its filter box (*Filter by name…*), with the text selected so
typing replaces it. There is no separate Find dialog.

**Trace the network.** With a node, link or subcatchment selected, use
**Edit → Upstream** or **Edit → Downstream**. Seeds are the selected nodes plus
both endpoints of selected links, and subcatchments that drain into the
network. The result replaces the selection and logs:

```
Selected upstream subnetwork: N node(s), M link(s), K subcatchment(s).
```

Pick one of the nine outfalls and trace upstream to isolate a whole tributary
sewershed in one click.

**Rubber-band and polygon.** The **Select** tool takes Shift to add and Ctrl to
subtract; **Select by Polygon** finalises with **Return** or a double-click and
cancels with **Esc**, with Shift for union and Ctrl for subtract.

Then **Zoom To Selection** (**Ctrl+Shift+J**) and open the attribute table
(**Ctrl+Alt+4**) to work on the subset. **Delete** or **Backspace** in the
table deletes the selected rows' objects through the undo stack.

\figtodo{t08_select_upstream.png, An upstream trace from an outfall highlighting a whole sewershed}

### 7. The control rules

`[CONTROLS]` holds **five rules over 28 rows** — pump controls keyed off node
depths and `TIMEOPEN`/`TIMECLOSED`. Open **Model → Data Objects → Control
Rule** to reach the **Control Rules Editor**, a non-modal two-pane editor:

- Left: a search box (*Search rules…*), the rule list with ✓ / ⚠ status
  glyphs, and **Add**, **Delete**, **Rename…**.
- Right: the rule name, a **Validate now** button, a validation banner reading
  **● Valid**, **⚠ Line \<n\>: \<message\>** or **(no rule selected)**, and a
  free-text editor for the raw `RULE … IF … THEN … ELSE … PRIORITY` body.

The first rule reads:

```
RULE VACIADO_G71F68Y_ON
IF NODE G71F06R DEPTH < 0.36
AND NODE G72K020 DEPTH < 0.40
AND NODE G71F090 DEPTH < 0.791
AND NODE G71F68Y DEPTH > 1.03
AND PUMP G71F68Yp1 TIMECLOSED > 00:02
THEN PUMP G71F68Yp1 STATUS = ON
```

The rule body editor has a completer: **Ctrl+Space** forces it open, and while
the popup is showing it consumes **Enter**, **Return**, **Esc**, **Tab** and
**Backtab**.

`[REPORT] CONTROLS YES` is set, so the status report lists every control action
taken during the run — the fastest way to confirm a rule fired.

\figtodo{t08_control_rules_editor.png, The Control Rules Editor with one of the five pump rules selected}

### 8. Run it

Look at the run settings before pressing anything.

`[OPTIONS]`, in part:

```
FLOW_ROUTING         DYNWAVE
ROUTING_STEP         0:00:10
MINIMUM_STEP         0:00:02
VARIABLE_STEP        0.65
MAX_TRIALS           20
HEAD_TOLERANCE       0.0015
THREADS              8
NODE_CONTINUITY      SEMI_IMPLICIT
ANDERSON_ACCEL       YES
START_DATE           06/29/2012   START_TIME  00:01:00
END_DATE             06/30/2012   END_TIME    23:59:00
REPORT_STEP          0:15:00
```

`[2D_OPTIONS]`, in full:

```
MAX_TIMESTEP           60
DRY_DEPTH              0.0001
LIMITER_EPSILON        1e-06
FLUX_DH_EPS            0.001
COUPLING_CD            0.65
COUPLING_SYNC          0
RAINFALL_MODE          NATURAL_NEIGHBOUR
REPORT_2D              YES
CELL_CLOSURE           VFR
FACE_RECONSTRUCTION    VFR_FACE
VFR_MIN_WET_FRAC       0.0001
INTEGRATOR             EXPLICIT
THETA                  0.6
CFL_NUMBER             0.7
H_MOVE                 0.0001
LTS_TIERS              4
FROUDE_MAX             1.5
COUPLING_AREA          DEFAULT
OUTPUT_FILE            BellingeSWMM_v021_nopervious.2d.h5
```

Note that **the simulated window is about two days**, 29–30 June 2012, even
though the rain file spans more than a decade. That is deliberate: it keeps the
example runnable. Widen it on the **Dates & Times** page if you want a longer
event.

**Threads.** `THREADS 8` is set in the file. The GUI's control is
**Simulation Options → System / Performance → Worker threads:** — a spin box
with **auto** as its special value, a live *"/ N logical"* suffix that turns
into *"/ N logical — oversubscribed"* if you exceed the machine, and an
"effective threads" label computed by the engine itself rather than by the GUI.
The **threadLimitsSummary** line beneath it reports logical processors,
performance cores (*efficiency cores slow the solvers and are avoided in auto
mode*), whether the engine was built without OpenMP, the OpenMP process limit,
and whether the 2D Kokkos backend is already running with a fixed thread count.

There is also an **Apply fast preset** button, which sets `THREADS` to the
machine's fast-preset value and `MINIMUM_STEP` to 1.0 s. Its tooltip cites
roughly a 2.6× speed-up on this very benchmark with mass balance as good or
better — and warns that pushing `MINIMUM_STEP` to 2.0 s gives about 4× but
degrades continuity.

Press **Ctrl+R**. The **Simulation Status** panel shows **Name**, **Status**,
**Progress**, **Sim Start**, **Sim Current**, **Sim End**,
**Runoff Err (%)**, **Routing Err (%)**, **2D Err (%)**, **Duration**,
**Avg Timestep** and **Engine Version**. Continuity errors are polled live, not
just at the end, so a run drifting toward a bad mass balance is visible early.
Rows are keyed by project window **and engine version**, so running the same
model under two engine versions produces two rows.

Run times depend entirely on your machine and thread count, so measure yours —
the example ships with no report and this manual will not invent a number.

On completion the `.out` is loaded automatically, becomes the active 1D results
layer, is bound to the animation controller, and has its `.rpt` path recorded so
the report viewer can find it. The 2D `.h5` is attached as a
**2D Results (live)** layer keyed by canonical path, so re-running overwrites
rather than stacking duplicates. Cancelling a run **still** loads the partial
output — the engine flushes it either way.

\figtodo{t08_simulation_status.png, The Simulation Status panel mid-run with live continuity errors}

### 9. Style and animate a big results set

Style the 1D results layer by a dynamic attribute (flow, depth, velocity,
capacity) with a graduated renderer, and the 2D results layer by depth with a
colour ramp and a dry-depth cut-off matching the engine's `DRY_DEPTH 0.0001`.
The GUI reads that value from the engine, so the wet/dry edge in the animation
matches the solver's.

Then use the **Results** ribbon transport. On a mesh this size:

- Keep the mesh **wireframe off** while animating; the terrain fill and the
  results fill are what you want to see.
- The 2D renderer switches to **asynchronous contour recomputation** above
  50 000 cells, so contours lag the frame slightly rather than blocking it.
- The renderer applies a **Far / Mid / Near** level-of-detail policy with
  hysteresis, so zooming out drops sub-pixel cells rather than drawing them.
  Selection overlays are always drawn regardless of the bucket.
- Turn labels off (or raise the label threshold in Preferences) before playing.

\figtodo{t08_2d_animation_frame.png, A 2D inundation frame over the Bellinge street network with a basemap underneath}

### 10. A profile through a pumped branch

Six pumps run in this model. Pick one, trace **upstream** from its discharge
node to find the branch, then use **Analysis → Profile** (**Ctrl+Shift+T**) and
the profile-selection tool to lay a path from a headwater junction, through the
wet well, across the pump, to the receiving node.

The profile plot shows the invert, the crowns, the HGL at the animation time and
the depth envelope. **Home** fits the view. Playing the animation while the
profile is open shows the wet-well drawdown and the pump's on/off cycling
directly against the rule thresholds you read in step 7.

Profile paths are not currently persisted in the project — the engine reads
`[PROFILE]` and discards it, and the `.oswp` has no key for profiles — so
re-lay the path after reopening.

\figtodo{t08_profile_pumped_branch.png, A profile plot through a pumped branch with the HGL at an animation time}

### 11. Flow balance at an outfall

Select one of the nine outfalls and use **Analysis → Flow Balance Upstream**.
The result is an information box:

```
Flow Balance — Upstream

Subnetwork: N node(s), M boundary link(s)

Inflow:  …
Outflow: …
Net:     …

(final time-step flows, in project flow units)
```

Read the parenthetical. This is the **final reported time step**, not a volume
integral — useful for checking that a sewershed's boundary links balance at the
end of a run, not for computing event volumes. For volumes, use the status
report's continuity blocks or the tabular results export.

### 12. Visualise the two rain gages

The rain record is the single largest file in the example: 26.6 MB, 749 401
rows, seven whitespace-separated columns in SWMM's standard rain-file layout —
`station year month day hour minute value`. Two stations, `rg5425` and
`rg5427`.

```
[RAINGAGES]
;;Name    Format   Intvl    SCF      Source
rg5425    VOLUME   0:01     1.00     FILE "rg_bellinge_Jun2010_Aug2021.dat" rg5425 MM * 30
rg5427    VOLUME   0:01     1.00     FILE "rg_bellinge_Jun2010_Aug2021.dat" rg5427 MM * 30
```

Reading that source line token by token: the file path, the **station id**
matched against column 1, `MM` as the depth unit, `*` meaning *no start date*,
and a trailing **`30`** which is the optional **rainfall scale factor** the
engine applies to every value. It is not a comment and it is not cosmetic — the
gage record is multiplied by 30.

Two more things the filename does not tell you: the record actually begins in
**January 2009** for `rg5427` and **June 2009** for `rg5425`, not June 2010; and
neither gage has a `[SYMBOLS]` map location in the shipped file, which matters
for `RAINFALL_MODE NATURAL_NEIGHBOUR` — with no gage locations the engine falls
back to `SYSTEM` (a uniform mean) on its own.

Open **Analysis → Rainfall Visualization…**. The dialog opens on the
**Overlay** tab, with a **Per Gage** tab that stacks one chart per gage on a
synchronised time axis. The **Basis:** combo switches between
**Intensity (mm/hr)**, **Depth per interval (mm)** and
**Cumulative depth (mm)**. Underneath is a twelve-column table: **Gage**,
**Source**, **Total depth**, **Peak intensity**, **Peak time**, **Interval**,
**First**, **Last**, **Gaps**, **Longest gap**, **Points**, **Status**.

Only the focused (or first) gage is checked by default — the visibility
checkbox lives in the **Gage** column, so tick the second one to overlay both.
The **Gaps** and **Longest gap** columns are the reason to run this on a
decade-long record before trusting it.

\figtodo{t08_rainfall_two_gages.png, The Rainfall Visualization dialog overlaying both Bellinge gages}

### 13. Compare two runs

A good pair for this model is **`ANDERSON_ACCEL YES` versus `NO`** — Anderson
acceleration on the semi-implicit node-continuity solve.

1. Run once as shipped. The `.out` stays on the canvas.
2. **File → Save As…** a second copy of the project, set `ANDERSON_ACCEL` to
   `NO` on the **Routing & Hydraulics** page, and run that copy.
3. Load the second `.out` (**File → Import → Add SWMM Output…**) onto the same
   canvas.

Open the **Comparison Plot**. Each distinct `.out` becomes one **run source**,
de-duplicated by results-file path, and one can be marked the baseline. Add the
same series from both runs — **Add Series…** opens the **Plot Variables**
picker; **Add from Map…** lets you click objects on the map to add them;
**Add System Series…** covers rainfall, runoff, flooding and the other
system-wide variables. **Configure 1v1 Comparisons** pairs series explicitly
with **Add**, **vs**, **Remove Selected** and **Reset to Auto**. The animation
cursor toggle is **Ctrl+Shift+C** and **Charts Only** is **Ctrl+Shift+F**.

Compare on three axes: the hydrographs at a surcharged junction, the routing
continuity error in each report, and the wall-clock duration in the Simulation
Status panel. The same recipe works for `NODE_CONTINUITY SEMI_IMPLICIT` versus
the default, for `THREADS` values, and for the fast preset.

\figtodo{t08_comparison_two_runs.png, A comparison plot of the same junction under two runs with the animation cursor showing}

## What to look for

- The mesh appears **coarse first**, then sharpens. That is by design, not a
  glitch.
- The Simulation Status panel shows a row for the model **before** you run
  anything, with status *Idle* and the simulation dates already filled in.
- Continuity errors move during the run. If **Routing Err (%)** climbs steadily
  from early on, stop and look at `MINIMUM_STEP` and `MAX_TRIALS` rather than
  waiting for the end.
- `[REPORT] CONTROLS YES` means every control action appears in the report.
  Cross-check them against the five rules.
- The 2D layer's **2D Err (%)** column shows a real number here. In a 1D-only
  model it reads `—`.
- Saved mesh display state comes back on reopen; profile paths do not.

## Variations

- **Widen the simulation window** to cover a larger storm from the gage record
  and re-run. Watch the `.out` file size and the animation frame count.
- **Swap the rainfall mode.** Give both gages `[SYMBOLS]` locations and switch
  from the effective `SYSTEM` fallback to true `NATURAL_NEIGHBOUR` weighting.
- **Turn the fast preset on** and compare its continuity against the shipped
  settings.
- **Try `CELL_CLOSURE FLAT`** against the shipped `VFR` and see how much run
  time the closure costs on a real mesh — and whether the inundation extent
  changes where it matters.
- **Use the SRTM DEM as a basemap** via **Add Basemap → Local File** and compare
  its coarse 1-arc-second relief against the meshed terrain.
- **Trace and export.** Select a sewershed upstream of one outfall, open the
  attribute table, and **Export CSV…** the subset.

## Related

- \ref manual_projects — projects, recents, portability and copy-on-open
- \ref manual_layers — every data source, including basemaps and rasters
- \ref manual_crs — coordinate reference systems and reprojection
- \ref manual_selection — selection, search and upstream/downstream tracing
- \ref manual_hydraulics — pumps, weirs, orifices, outlets and control rules
- \ref manual_simulation_options — threads, routing options and the 2D page
- \ref manual_running — the Simulation Status panel, logs and the report
- \ref manual_results — results layers, styling and animation
- \ref manual_profile_plots — profile plots along 1D paths
- \ref manual_analysis_tools — flow balance and rainfall visualisation
- \ref manual_performance — redraw channels, LOD and large-model tuning
- \ref tutorial_2d_boundaries — boundary conditions and 1D–2D coupling in detail
