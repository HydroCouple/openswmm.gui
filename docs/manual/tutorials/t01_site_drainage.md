@page tutorial_site_drainage T1 — Site Drainage with Water Quality

## Goal

Take the classic EPA site-drainage deck from a cold start to a full 1D result
set: open it, give it a coordinate system and a basemap, walk the model with
the Object Browser and the attribute tables, review the storm and the
build-up/wash-off setup, run it, then read the report, animate the map, and
plot depths, flows, profiles and TSS concentrations. Finish by changing the
routing method, the infiltration model, and adding an LID control, comparing
each variant against the base run.

\videotodo{Opening the site drainage model — setting a CRS — running it and animating the result}

## Capabilities exercised

Project open · project CRS · XYZ basemap · Object Browser and Properties ·
attribute tables · rain gages and time series · pollutants, land uses,
build-up/wash-off and coverages · Simulation Options · running · report
viewer and continuity · results styling and animation · time-series plots ·
profile plots · tabular results and CSV export · comparison plots · LID
controls.

## Files

| File | What it is |
| ---- | ---------- |
| `examples/site_drainage/site_drainage_model.inp` | The model — the only file shipped with this example |

There is **no checked-in `.rpt` or `.out`** for this example, so every number
below is one you will produce yourself. Running the model writes
`site_drainage_model.rpt` and `site_drainage_model.out` beside the `.inp`.

What is in the deck:

| Object type | Count | Names |
| ----------- | ----- | ----- |
| Rain gages | 1 | `RainGage` |
| Subcatchments | 7 | `S1`–`S7` (28.92 acres total) |
| Junctions | 11 | `J1`–`J11` |
| Outfalls | 1 | `O1` (`FREE`) |
| Conduits | 11 | `C1`–`C11` |
| Pollutants | 1 | `TSS` (`MG/L`) |
| Land uses | 4 | `Residential_1`, `Residential_2`, `Commercial`, `Undeveloped` |

`[OPTIONS]` sets `FLOW_UNITS CFS`, `INFILTRATION HORTON`,
`FLOW_ROUTING DYNWAVE`, `LINK_OFFSETS DEPTH`, and runs from
`01/01/1998 00:00` to `01/02/1998 06:00` — 30 hours — with a 5-minute
report step, a 1-minute wet step and a 5-second routing step. `[MAP]` declares
`Units Feet` but no coordinate reference system, which is the first thing you
will fix.

The conveyance is a mix of grass swales and culverts, and the `[TAGS]` section
says which is which — `C1`, `C4`–`C6`, `C8`–`C10` are tagged `Swale`, `C3`,
`C7` and `C11` `Culvert`, and `C2` `Gutter`. That tag is a useful thing to
style and label by later.

## Steps

### 1. Open the model

**File → Open…** (`Ctrl+O`) opens the dialog titled **Open SWMM Model or
Project**; pick `examples/site_drainage/site_drainage_model.inp`. The `.inp`
opens directly — this example has no `.oswp` project wrapper, so SWMMVis
builds the project from the input file alone and will offer to create the
`.oswp` when you save.

The network draws on the map canvas, and the **Layers**, **Object Browser**,
**Properties** and **Message Logs** docks populate. Press `Ctrl+Shift+F`
(**Zoom Extent**) if the view does not already frame the site.

\figtodo{t01_open_model.png, The site drainage model on the map canvas immediately after opening}

### 2. Give the project a CRS and add a basemap

The deck carries plan coordinates in feet with no declared CRS, so nothing
will line up with web imagery until you tell SWMMVis what those coordinates
mean.

1. **Model → Set Project CRS…** opens **Select Coordinate Reference System**.
   Type a name or an EPSG code in **Search:**, narrow with **Authority:** and
   **Type:**, and pick the projected system the site was surveyed in. If you
   do not know it, the **Local (no transform)** group at the top of the tree
   offers **Local — feet (no transform)**, which keeps the drawing honest
   without pretending to georeference it.
2. If you pick a real CRS while coordinates are already stored, the
   **Change Coordinate Reference System** dialog asks *How should the change
   be applied?* — **Reproject stored coordinates** permanently transforms
   every node, vertex and polygon; **Re-render only (display in new CRS)**
   keeps the numbers and reprojects on the fly. For a deck whose coordinates
   are already correct in the target system, choose re-render.
3. **File → Import → Add Basemap** opens **Add Basemap**. On the
   **XYZ Tiles** tab the built-in providers are **OpenStreetMap**,
   **CartoDB Positron**, **CartoDB Dark Matter**, **Stadia Alidade Smooth**
   and **ESRI World Imagery**. Positron is the least distracting under a
   network; Imagery is the one to use when you want to see the parking lots
   the impervious fractions describe.

\figtodo{t01_crs_dialog.png, Select Coordinate Reference System with the Local group expanded}

\figtodo{t01_basemap.png, The network over a CartoDB Positron basemap}

A basemap only lands in the right place if the project CRS is right. If the
network flies off to the Gulf of Guinea, the CRS is wrong — see
\ref manual_crs.

### 3. Walk the model

Three panels answer three different questions.

| Panel | Shortcut | Answers |
| ----- | -------- | ------- |
| **Object Browser** | `Ctrl+Alt+2` | *What objects exist?* — every category, every name |
| **Properties** | `Ctrl+Alt+3` | *What is this one object?* — one selected object, editable |
| **Attribute Table** | `Ctrl+Alt+4` | *How do they compare?* — one row per object, sortable and queryable |

Expand **Subcatchments** in the Object Browser and click `S5`. The Properties
dock retitles itself **Properties — S5** and shows the area, imperviousness,
width, slope and outlet. `S5` is the 87.7 % impervious catchment draining to
`J10`; `S7` next to it is 0 % impervious and drains to the same node — a
useful pair when you look at runoff coefficients later.

Right-click any object in the Object Browser for **Plot Time Series…**,
**Zoom to Object**, **Sort Category A→Z**, **Reset Category to Default
Order** and **Delete…**. The same **Plot Time Series…** and **Zoom to
Object** entries are on the map canvas right-click menu, along with
**Convert To ▸** and **Delete…**.

Open the **Attribute Table** dock and choose the conduits category in
**Category:**. Sort by length, or type a query in **Query:** and press
**Apply** to isolate the culverts. **Export CSV…** writes the table out.

\figtodo{t01_object_browser.png, The Object Browser with subcatchment S5 selected and the Properties dock beside it}

\figtodo{t01_attribute_table.png, The conduit attribute table sorted by length}

Select a conduit and look at the **Section View** dock (`Ctrl+Alt+9`): the
**Section** button draws the cross-section — a 3 ft deep trapezoid for the
swales, a 4.75 ft circle for `C11` — and **Profile** draws the longitudinal
section between its end nodes with `Rim`, `Inv` and `Crown` annotations. The
**V:H** combo (**Auto**, **1:1**, **2:1**, **5:1**, **10:1**, **20:1**,
**50:1**) sets the vertical exaggeration.

### 4. Inspect the rain gage and its time series

Select `RainGage` in the Object Browser. The Properties dock shows
**Rain Type**, **Recording Interval (s)**, **Snow Catch Factor (SCF)**,
**Rainfall Scale Factor**, **Data Source** and **Series Name** among others.
This gage is `VOLUME` format reading the time series named `2-yr`.

Click the **…** button beside **Series Name** to open the **Time Series
Editor** on that series, or use the **Plot Rainfall…** button in the
Properties dock to open **Rainfall Visualization**, whose **Overlay** and
**Per Gage** tabs summarise every gage with columns for total depth, peak
intensity, peak time, interval and gaps.

The `2-yr` series is 239 six-minute values totalling **2.83 in**, with the
peak interval — 0.389 in, about 3.9 in/hr — at **12:00** on 1 January 1998.
Everything before hour 11 is drizzle; the storm is a single sharp burst in the
middle of the day.

\figtodo{t01_rainfall_visualization.png, Rainfall Visualization showing the 2-yr design storm}

### 5. Review pollutants, land uses and coverages

**Model → Data Objects → Pollutants…** opens the **Pollutants** dialog. `TSS`
is the only constituent: concentration units `MG/L`, zero rain/groundwater/RDII
concentrations, zero decay.

**Model → Data Objects → Land Uses…** opens **Land Uses**, whose tabs are
**General & Sweeping**, **Buildup** and **Washoff**. No sweeping is configured
in this deck (interval and removal are zero).

The **Buildup** grid has columns **Pollutant**, **Function**, **Max/C1**,
**Rate/C2**, **Power/Sat/C3** and **Per Unit**; the **Washoff** grid has
**Pollutant**, **Function**, **Coefficient**, **Exponent**,
**Sweep Effic (%)** and **BMP Effic (%)**. In this model:

| Land use | Buildup | Washoff |
| -------- | ------- | ------- |
| `Residential_1` | `EXP` 0.11 / 0.5 per `CURB` | `EXP` 2 / 1.8 |
| `Residential_2` | `EXP` 0.13 / 0.5 per `CURB` | `EXP` 4 / 2.2 |
| `Commercial` | `EXP` 0.15 / 0.2 per `CURB` | `EXP` 4 / 2.2 |
| `Undeveloped` | `NONE` | `RC` 500 / 2 |

Coverages are a *subcatchment* property, not a land-use one. Select `S4` and
click the **Land Uses** row in the Properties dock: the compound editor
**Land Use Coverage — S4** opens with columns **Land Use** and
**Coverage (%)**. `S4` is 9 % `Residential_1`, 30 % `Residential_2` and 26 %
`Commercial`; the dialog's footer warns when coverages do not sum to 100 %,
which for `S4` they deliberately do not. `S7` has no coverage rows at all, so
it washes nothing off — the paired-catchment contrast that makes the TSS plots
in step 10 legible.

\figtodo{t01_landuse_editor.png, The Land Uses dialog on the Buildup tab}

\figtodo{t01_coverages.png, The Land Use Coverage compound editor for subcatchment S4}

### 6. Check Simulation Options

**Model → Simulation Options…** opens **Simulation Options**, a dialog with a
category list down the left side rather than tabs: **Title / Notes**,
**Models / Processes**, **Dates & Times**, **Routing & Hydraulics**,
**Quality & Transport**, **System / Performance**, **Spatial & CRS**,
**Mesh**, **2D Surface Routing** and **Files / Output / Plugins**.

For this run, confirm three things:

| Page | Control | Value for this model |
| ---- | ------- | -------------------- |
| **Models / Processes** | **Infiltration model:** | **Horton** |
| **Models / Processes** | **Flow routing:** | **Dynamic Wave** |
| **Models / Processes** | **Active processes** | **Rainfall / runoff** and **Water quality** both ticked |
| **Dates & Times** | **Start:** / **End:** | 01/01/1998 00:00 → 01/02/1998 06:00 |
| **Dates & Times** | **Reporting step:** | 00:05:00 |

Note that flow units are *not* on this dialog. `FLOW_UNITS` is changed on the
**Flow Units:** combo in the status bar; **Preferences → Simulation Defaults**
only sets what **File → New** starts a blank project with. An existing model
keeps whatever its `.inp` declares until you change the combo — `CFS` here. See
\ref manual_simulation_options for the complete page-by-page reference.

\figtodo{t01_simulation_options.png, Simulation Options on the Models / Processes page}

### 7. Run

**Analysis → Execute** (`Ctrl+R`) runs the model. SWMMVis auto-saves any
pending edits first — **Run** always uses the `.inp` on disk — and logs
*Auto-saved before running.* if it had to.

The **Simulation Status** dock (`Ctrl+Alt+6`) shows the running job and its
progress; **Message Logs** (`Ctrl+Alt+7`) collects engine warnings. **Pause**
and **Stop** (`Ctrl+.`) act on the selected job. When the run finishes the
results attach to the project as a results layer in the **Layers** panel.

\figtodo{t01_run_status.png, The Simulation Status dock during a run}

### 8. Read the report and the mass balance

**Analysis → Report** opens the **Report Viewer**, whose title bar carries the
report path. The left pane is a section navigator built from the `.rpt`'s own
headings — `Report Header & Notes`, the continuity blocks, every summary
table, and `Warnings & Errors (n)`. **Filter sections…** narrows the list;
the right pane holds the whole raw report text with a **Search:** box
(`Enter` = next, `Shift+Enter` = previous) and a **Regex** checkbox.

**Analysis → Show Mass Balance** opens the same viewer — there is no separate
mass-balance dialog; the ledger is the engine's own text. Jump to these
sections:

| Section | What to check |
| ------- | ------------- |
| `Runoff Quantity Continuity` | `Total Precipitation` against the 2.83 in you measured in step 4; `Continuity Error (%)` near zero |
| `Flow Routing Continuity` | `Wet Weather Inflow` in, `External Outflow` at `O1` out, `Flooding Loss`, and `Continuity Error (%)` |
| `Quality Routing Continuity` | the TSS mass ledger — wash-off in, outfall load out |
| `Node Flooding Summary` | which junctions overflow and for how long |
| `Link Flow Summary` | peak flow magnitude — `Max/Full Flow` and `Max/Full Depth` per conduit |

The viewer shows a banner when the continuity error exceeds 10 %. Anything
above about 1 % on a deck this small means the routing step is too coarse for
`DYNWAVE`; drop `Routing step:` on the **Dates & Times** page and re-run.

**Analysis → Summarize Results** opens the **Statistics Dashboard** instead:
tabs **Nodes** (**Node**, **Max depth**, **Max head**, **Max overflow**,
**Volume**), **Links** (**Link**, **Max flow**, **Max depth**, **Max
velocity**, **Max capacity**) and **Subcatchments** (**Subcatchment**,
**Peak runoff**, **Total runoff**, **Total infil**, **Total evap**), each
sortable, filterable with the **Query:** box, and exportable with
**Export CSV…**. A histogram of the selected column sits below the table.

\figtodo{t01_report_viewer.png, The Report Viewer with the section navigator open on Flow Routing Continuity}

\figtodo{t01_statistics_dashboard.png, The Statistics Dashboard on the Links tab}

### 9. Colour the map by result and animate it

The **Results** ribbon tab drives playback.

1. **Set Style** opens the results layer's style editor. Use the
   **Classification** block to pick the **Attribute:** to colour by —
   link flow or node depth — a **Colour ramp:**, a **Method:**
   (**Equal interval**, **Quantile**, **Natural breaks (Jenks)**,
   **Standard deviation**, **Logarithmic**, **Exponential** or **Manual**)
   and a **Range:** (**Fixed over run**, **Per-frame auto-stretch** or
   **Fixed (user range)**). Use **Fixed over run** while animating —
   per-frame stretching makes a trickle look like a flood.
2. **Show Legend** puts the ramp on the canvas.
3. In the **Timeline** group, drag the slider or type into the time box
   (`MM/dd/yyyy hh:mm`), set **Speed:** (0.25× … 8×) and leave **Cycle**
   ticked to loop.
4. **Play**, **Pause**, **Stop**, **Skip Back** and **Skip Forward** run the
   animation. Scrub to 12:00 on 1 January to catch the storm peak.

\figtodo{t01_results_style.png, The results style editor classifying link flow with a fixed range}

\figtodo{t01_animation_peak.png, The map at the storm peak with links coloured by flow and the legend showing}

### 10. Plot time series

Select `J11` (the junction just upstream of the outfall) and `C11` on the map,
then **Analysis → Plot Time Series** (`Ctrl+T`). The **Plot Variables** picker
lists a checkable group per selected object plus a **System Variables** group;
tick **Depth (node) (ft)** for `J11` and **Flow (ft³/s)** for `C11` and press
OK. The series land in the **Comparison Plot** window.

Faster route: right-click the object on the map or in the Object Browser and
use the **Plot Time Series…** submenu, which lists the attributes directly —
for a node **Depth (node) (ft)**, **Head (ft)**, **Volume (node) (ft³)**,
**Lateral inflow (ft³/s)**, **Total inflow (ft³/s)**, **Overflow (ft³/s)**;
for a link **Flow (ft³/s)**, **Depth (link) (ft)**, **Velocity (link) (ft/s)**,
**Volume (link) (ft³)** and **Capacity**; then a separator and one entry per
run species — **TSS (MG/L)** here — and **All attributes**.

That last group is how you get the pollutant plots: right-click `C11` and pick
**TSS (MG/L)** to see the concentration hydrograph leaving the site, then add
`C1` (draining the wholly residential `S1`) and a link fed by `S7` (no
coverages, therefore no wash-off) to the same plot. The first flush shows up
as a concentration spike ahead of the flow peak.

Comparison Plot toolbar essentials: **Fit**, **Show Animation Cursor**
(`Ctrl+Shift+C`) to tie the plot cursor to the map animation,
**Add System Series…**, **Add from Map…**, **Stats Panel**,
**Export PNG…** and **Export Data…**. **Load Observed…** reads a CSV/TSV of
measured values for calibration.

\figtodo{t01_plot_variables.png, The Plot Variables picker with a node and a link expanded}

\figtodo{t01_comparison_plot.png, Depth at J11 and flow in C11 in the Comparison Plot}

\figtodo{t01_tss_plot.png, TSS concentration at three conduits showing the first flush}

### 11. Plot a profile from the head of the system to the outfall

**Analysis → Plot Profile** (`Ctrl+Shift+T`) arms the profile picker. The
status bar reads *Click the profile start node or link — then Ctrl/⌘+click
the end.* Click `J3`, the most upstream junction on the main line, then
`Ctrl`-click (`⌘`-click on macOS) the outfall `O1`. The route runs
`J3 → C3 → J4 → C4 → J5 → C5 → J6 → C7 → J8 → C8 → J9 → C9 → J10 → C10 →
J11 → C11 → O1`.

When more than one route connects the endpoints, **Select Profile Path**
opens listing the candidates with **Length**, **Conduits**, **Other** and
**Drop** columns — pick one and press OK.

The **Profile Plot** window draws ground, inverts, crowns, the animated HGL
and the maximum-HGL envelope. Its header reads
`Path: n nodes, m links · Length: …`. **Display Options…** opens
**Profile Display Options**, whose **Display** tab toggles **Current HGL
line**, **Current HGL fill**, **Current EGL**, **Max HGL band**,
**Max HGL line** and **Max EGL line**, plus node/link labelling and legend
and timestamp styling. Turn on **Show Animation Cursor** in the Comparison
Plot and press **Play** on the Results tab to watch the HGL and the plots
move together.

\figtodo{t01_profile_plot.png, Profile plot from J3 to the outfall with the maximum HGL envelope}

### 12. Tabular view and export

**Analysis → Tabular View** (`Ctrl+Shift+A`) raises the **Attribute Table**
dock and puts the keyboard focus in it — it is not a separate window. Set
**Category:** to **Conduits** and scroll to the right-hand end of the table:
after the editable model attributes comes the read-only dynamics block the run
produced — **Max Flow (Sim.)**, **Max Velocity**, **Max/Full Depth**, **Total
Flow Volume** and **Time Surcharged (hr)**. The query bar, **Show selected
only**, `Ctrl+C` and **Export CSV…** all work on those columns, so "every
conduit whose `"Max/Full Depth"` > 0.9, as a CSV" is one query and one button.
For a period-by-period series instead of a per-object summary, use the
comparison plot's **Export Data…** (step 10).

\figtodo{t01_tabular_results.png, The Attribute Table dock showing the conduit dynamics columns after a run}

## What to look for

- **Continuity.** Both continuity errors in the report should be a small
  fraction of a percent. They are the first thing to check on every run.
- **Where the water goes.** The three culverts (`C3`, `C7`, `C11`) are the
  capacity constrictions; `Max/Full Depth` in the Link Flow Summary tells you
  whether any of them runs full at the peak, and the Node Flooding Summary
  tells you whether that backed water up to the surface.
- **The 12:00 spike.** Everything interesting happens between 11:30 and 13:00.
  Set the animation window narrow and scrub through that hour.
- **First flush.** Because build-up is `EXP` per unit curb length and wash-off
  is `EXP` on runoff rate, TSS concentration peaks *before* flow does. The
  `Undeveloped` land use uses an `RC` (rating-curve) wash-off with no
  build-up, so catchments carrying it behave differently — and `S7`, which
  carries no land use at all, contributes flow but no load.
- **Swales versus pipes.** The `Swale`-tagged conduits have Manning's *n* of
  0.05 against 0.016 in the culverts. Style the network by the `Tag` field to
  see the two systems at a glance (\ref manual_styling).

## Variations

Each variation is a separate run. Save the base run's `.out` under a distinct
name first (or copy the whole example folder) so you can load both into one
plot: in the **Comparison Plot** every results layer contributes its own run,
and **Profile Display Options → Sources** has **Add output file…** to bring a
second `.out` into the profile.

### Change the routing method

**Simulation Options → Models / Processes → Flow routing:** offers **Steady**,
**Kinematic Wave**, **Dynamic Wave** and **Finite Volume**.

- **Kinematic Wave** ignores backwater and cannot surcharge. On this model the
  culverts stop constraining the system, peaks arrive earlier and flooding
  disappears. Kinematic wave also ignores the `LINK_OFFSETS DEPTH` outlet
  offset on `C2`.
- **Finite Volume** switches to the shock-capturing solver and reveals the
  **Finite volume solver** and **Finite volume performance** groups on the
  **Routing & Hydraulics** page: **Cell length:**, **Min cells per conduit:**,
  **CFL number:**, **Riemann solver:** (**HLLC** / **HLL**),
  **Spatial order:** (**1st order** / **2nd order (MUSCL-Hancock)**),
  **Slope limiter:** (**Minmod** / **van Leer** / **Superbee**),
  **Time integration:** (**Euler** / **RK2**), **Pressure closure:**
  (**SLOT (Preissmann)** / **TPA (two-component pressure)**) and a
  **Backend:** selector. Start with the defaults and only reach for
  second-order plus a limiter once a first-order run is stable.

Plot flow in `C11` from all three runs in one Comparison Plot: the dynamic and
finite-volume hydrographs should agree closely, the kinematic one should be
earlier and sharper.

\figtodo{t01_routing_comparison.png, Flow in C11 under dynamic wave — kinematic wave and finite volume}

### Change the infiltration model

**Simulation Options → Models / Processes → Infiltration model:** offers
**Horton**, **Modified Horton**, **Green-Ampt**, **Modified Green-Ampt** and
**Curve Number**. The five `[INFILTRATION]` parameters mean different things
under each model, so switching the combo is only half the job — the parameters
on each subcatchment have to be re-entered for the new model. Do it once on
`S3` (the least impervious catchment, where infiltration matters most) and
watch its runoff coefficient in the Subcatchment Runoff Summary move.

### Add an LID control

1. **Model → Data Objects → LID Controls…** opens **LID Controls**. Press
   **New**, name it, and pick a **Type**: **Bio-Retention Cell**,
   **Rain Garden**, **Green Roof**, **Infiltration Trench**,
   **Permeable Pavement**, **Rain Barrel**, **Rooftop Disconnection** or
   **Vegetative Swale**. Fill the layer tabs that apply — **Surface**,
   **Soil**, **Storage**, **Drain**.
2. Select `S5` (87.7 % impervious, the worst offender) and click the
   **LID Usage** row in the Properties dock. The compound editor
   **LID Usage — S5** opens with a table of **LID Control**, **#**, **Area**,
   **Width**, **Init.Sat** and **%Imperv**, and an **Add LID Usage** group
   with **LID Control**, **Number of Units**, **Area (per unit)**,
   **Top Width**, **Init. Saturation** and **% From Impervious**. Fill it in
   and press **Add**.
3. Re-run and compare peak runoff from `S5` and flow in `C10` against the base
   run.

\figtodo{t01_lid_editor.png, The LID Controls dialog with a bio-retention cell defined}

\figtodo{t01_lid_usage.png, The LID Usage compound editor on subcatchment S5}

The LID editor cannot read existing layer values back from the engine, so
treat what you type as the record of truth and check the written `[LID_USAGE]`
rows in the saved `.inp` if a result surprises you.

## Related

- \ref manual_projects — opening `.inp` files and saving `.oswp` projects
- \ref manual_crs — coordinate reference systems and the reproject/re-render choice
- \ref manual_layers — basemaps and other data sources
- \ref manual_object_browser — the Object Browser, Properties and Section View
- \ref manual_attribute_tables — attribute tables and the query box
- \ref manual_hydrology — rain gages, subcatchments, infiltration and LID controls
- \ref manual_water_quality — pollutants, land uses and build-up/wash-off
- \ref manual_simulation_options — the Simulation Options dialog page by page
- \ref manual_running — running, logs, reports and continuity
- \ref manual_results — results layers, styling and animation
- \ref manual_time_series_plots — the Comparison Plot and observed data
- \ref manual_profile_plots — profile plots along a 1D path
- \ref manual_tabular_results — tabular results and export
- \ref manual_analysis_tools — the Statistics Dashboard and rainfall visualisation
- \ref tutorial_street_inlets — the next tutorial
