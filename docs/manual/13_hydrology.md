@page manual_hydrology 13 — Hydrology: Rain Gages, Subcatchments, LID, Snow, Groundwater and RDII

## What you'll do

Define the rainfall that drives the model, build subcatchments and give them
subareas and an infiltration model, attach LID controls, snow packs, aquifers
and groundwater exchange, and set up unit hydrographs for RDII.

## Where to find it

| Object | Where |
|---|---|
| Rain gage | **Model → Add Rain Gauge** (ribbon **Model ▸ Rain Gages**) places one on the map |
| Assign rain gages | **Model → Assign Rain Gages…** (ribbon **Model ▸ Tools**) |
| Subcatchment | **Model → Add Subcatchment** (ribbon **Model ▸ Subcatchments**) |
| LID controls | **Model → Data Objects → LID Controls…** (ribbon **Model ▸ Data Objects ▸ LID Control**) |
| Aquifers | **Model → Data Objects → Aquifers…** (ribbon **Model ▸ Data Objects ▸ Aquifer**) |
| Snowpacks | **Model → Data Objects → Snowpacks…** — menu only; this one has no ribbon button |
| Unit hydrographs | **Model → Data Objects → Unit Hydrographs…** — menu only |
| Climate / snowmelt settings | **Model → Climate → …** (see \ref manual_climate) |

Every entry in **Model → Data Objects** opens the category's editor in *browse*
mode: nothing is created until you press the editor's own **New** button. The
Object Browser's category header carries an **Add New…** context-menu item that
opens the same editor already in create mode (see \ref manual_object_browser).

\figtodo{13_model_data_objects_menu.png, The Model → Data Objects submenu with the hydrology editors}

## Step-by-step

### Rain gages

A rain gage is a placed object: it has `[SYMBOLS]` map coordinates and a
`[RAINGAGES]` definition. Select one and edit it in the Properties panel or the
rain-gage attribute table — both drive the same adapter, so they stay in sync.

| Property | What it does | Writes |
|---|---|---|
| **Name** | Gage id | `[RAINGAGES]` name |
| **X Coordinate** / **Y Coordinate** | Map position in project units | `[SYMBOLS]` |
| **Rain Type** | `INTENSITY` / `VOLUME` / `CUMULATIVE` — how each series value is interpreted | `[RAINGAGES]` rain type |
| **Recording Interval (s)** | Series recording interval; edited through an H:MM interval combo | `[RAINGAGES]` interval |
| **Snow Catch Factor (SCF)** | Catch-deficiency correction for snow | `[RAINGAGES]` SCF |
| **Rainfall Scale Factor** | Multiplier applied to the gage's values | `[RAINGAGES]` |
| **Data Source** | `TIMESERIES` or `FILE` | `[RAINGAGES]` source keyword |
| **Series Name** | The `[TIMESERIES]` this gage reads (source `TIMESERIES`) | `[RAINGAGES]` |
| **Rain File (path)** | The rain file to read (source `FILE`); stored relative to the project when possible | `[RAINGAGES]` / file path table |
| **Rain File (resolved)** | Read-only — the absolute path the resolver settled on | — |
| **Rain File Column** | Column / station token inside a multi-column file | `[RAINGAGES]` |
| **Rain File Format** | *Auto-detect*; *Standard rain file*; *Multi-column* | `[RAINGAGES]` |
| **Station ID** | Station identifier inside the file | `[RAINGAGES]` |
| **Rain Units** | `IN` or `MM` for file data | `[RAINGAGES]` |
| **Current Rainfall** | Read-only — the resolved value at the current animation time | — |
| **User Flags** | Per-object user flags (see \ref manual_projects) | `[USER_FLAGS]` |

The three file formats are the only ones the engine ever assigns: the parser
records `USER_CSV` for a `path:column` token and `STAN_PRCP` for a bare path,
the resolver promotes `STAN_PRCP` to `USER_CSV` on detection, and a gage that
never had a file stays *Auto-detect*. The NWS / DSI / HLY format codes exist in
the engine but are never written, so they are deliberately not offered.

Rain gage series can be inspected with the **Rainfall Visualization** dialog —
overlay and per-gage charts plus a summary table, with an Intensity /
Depth-per-interval / Cumulative-depth basis selector. It is reached from the
Analysis menu, from the Object Browser's *Rain Gages* context menu, and from the
gage property editor's **Plot Rainfall…** button; it is documented in
\ref manual_analysis_tools.

\figtodo{13_raingage_properties.png, Rain gage properties with a file data source}

### Assign Rain Gages to Subcatchments

**Model → Assign Rain Gages** binds gages to subcatchments spatially in one
undoable step. Nothing is written until you press **Apply**; **Preview** shows
the full plan first.

| Control | What it does |
|---|---|
| **Nearest gage (Thiessen area majority)** | Assigns the gage whose Thiessen (Voronoi) cell covers the largest share of the subcatchment's *polygon* — not of a representative point |
| **Natural-neighbour interpolation (creates gages)** | Area-averages natural-neighbour weights over the gage network per subcatchment; clusters subcatchments whose weight vectors agree and materialises one generated gage plus a generated time series per cluster |
| **Weighting** | *Sibson (area stealing)* or *Laplace (Voronoi facet)* — interpolated method only |
| **Group weights within** | Percent tolerance for treating two weight vectors as the same cluster |
| **All subcatchments** / **Selected subcatchments (N)** | Scope of the assignment |
| **Preview** | Computes the plan and fills the table: *Subcatchment*; *Current gage*; *New gage*; *Detail* (area share or weight vector) |
| **Apply** | Commits the whole plan as a single undo macro |

SWMM binds exactly one gage per subcatchment, which is why an interpolated
rainfall field has to be expressed as generated gages. The dialog reports how
many generated gages were created, reused or removed, warns when gages have no
map location or share a location, and warns when snow catch factors differ
across the sources. A volume-conservation check runs before anything is written,
so a failed check aborts with the model untouched.

\figtodo{13_assign_rain_gages.png, The Assign Rain Gages dialog previewing an interpolated plan}

\videotodo{Assigning rain gages by Thiessen majority and then by natural-neighbour interpolation}

### Subcatchment properties

| Property | What it does | Writes |
|---|---|---|
| **Name** / **Tag** | Identity and free-form label | `[SUBCATCHMENTS]` / `[TAGS]` |
| **Rain Gage** | Driving gage (picker over `[RAINGAGES]`) | `[SUBCATCHMENTS]` |
| **Outlet** | Receiving node *or* receiving subcatchment | `[SUBCATCHMENTS]` |
| **Area** | Catchment area | `[SUBCATCHMENTS]` |
| **Width** | Characteristic overland-flow width | `[SUBCATCHMENTS]` |
| **Slope (%)** | Average surface slope | `[SUBCATCHMENTS]` |
| **% Imperv** | Impervious fraction | `[SUBCATCHMENTS]` |
| **N-Imperv** / **N-Perv** | Manning's n for the two subareas | `[SUBAREAS]` |
| **Dstore-Imperv** / **Dstore-Perv** | Depression storage depths | `[SUBAREAS]` |
| **% Zero Imperv** | Impervious area with no depression storage | `[SUBAREAS]` |
| **Rainfall Scale Factor** / **Snow Scale Factor** | Per-subcatchment multipliers | `[SUBCATCHMENTS]` |
| **Infil. Model** and its parameters | See below | `[INFILTRATION]` |
| **Aquifer** | Aquifer picker; clearing it removes the `[GROUNDWATER]` row | `[GROUNDWATER]` |
| **Groundwater** | Summary + **Edit…** → Groundwater Exchange dialog | `[GROUNDWATER]` / `[GWF]` |
| **Land Uses** | Summary + **Edit…** → coverage matrix | `[COVERAGES]` |
| **LID Usage** | Summary + **Edit…** → LID usage table | `[LID_USAGE]` |
| **Initial Loadings** | Summary + **Edit…** → per-pollutant initial buildup | `[LOADINGS]` |
| **User Flags** | Per-object user flags | `[USER_FLAGS]` |
| **Total Precipitation** / **Total Runoff Volume** / **Peak Runoff** | Read-only; populated after a run | — |

\figtodo{13_subcatchment_properties.png, Subcatchment property rows including the compound Edit buttons}

### Infiltration models

The **Infil. Model** row is a five-value enum, and the parameter rows that follow
it are the ones the chosen model actually uses.

| Model | Parameter rows |
|---|---|
| `Horton` | **Max. Infil. Rate**; **Min. Infil. Rate**; **Decay Constant (1/hr)**; **Drying Time (days)** |
| `ModHorton` | Same four rows as Horton |
| `GreenAmpt` | **Suction Head**; **Conductivity**; **Initial Deficit (frac.)** |
| `ModGreenAmpt` | Same three rows as Green-Ampt |
| `CurveNumber` | **Curve Number**; **Drying Time (days)** |

Modified Horton and Modified Green-Ampt share their predecessors' parameter
slots — the model code is what differs. The adapters re-read and re-write the
whole parameter group and then restore the model code, so editing one number on
a Mod-Horton subcatchment cannot silently demote it to plain Horton.

The project-wide default infiltration model is an `[OPTIONS]` key set in the
Simulation Options dialog (\ref manual_simulation_options); the object-creation
default is in **Preferences** (\ref manual_preferences).

**"Assign Infiltration to Selection…"** is a *2D-mesh* tool, not a subcatchment
tool. It writes a whole infiltration row (method plus up to five parameters plus
a destination) to the mesh cells picked on the map, either as per-cell
`[2D_INFILTRATION]` overrides or as one `[2D_INFILTRATION_DEFAULTS]` region row
when every selected cell shares a tag. Its methods are *Horton*, *Modified
Horton*, *Green-Ampt*, *Modified Green-Ampt*, *Curve Number* and *Constant*, and
its destinations are *Lost*, *Subcatchment Aquifer* and *2D Aquifer*. See
\ref manual_2d_mesh.

### The subcatchment compound editor

The **Land Uses**, **LID Usage** and **Initial Loadings** rows open one shared
dialog, on the page matching the row you clicked. Edits apply to the engine as
you make them (there is no separate Save step), and the cell summary refreshes on
close. The **Groundwater** row is the exception — it opens its own dialog (below).

- **Land Use page** — one row per defined land use with an editable
  **Coverage (%)** column and a live sum in the footer, which warns when the
  coverages do not add to 100 %. Writes `[COVERAGES]`.
- **LID Usage page** — a table of the LID controls already on this subcatchment
  (*LID Control*; *#*; *Area*; *Width*; *Init.Sat*; *%Imperv*) with **Remove
  Selected**, plus an **Add LID Usage** form: **LID Control**, **Number of
  Units**, **Area (per unit)**, **Top Width**, **Init. Saturation**, **% From
  Impervious**. Writes `[LID_USAGE]`.
- **Loadings page** — one row per pollutant with an **Initial Buildup
  (mass/area)** column. Writes `[LOADINGS]`.

\figtodo{13_subcatch_lid_usage_page.png, The LID Usage page of the subcatchment compound editor}

### LID Control editor

**Model → Data Objects → LID Controls…** opens a three-pane editor: the control
list with **New** / **Delete**, a form with **Name** and **Type**, and a live
layer-stack diagram that redraws from the current widget values and highlights
the active tab.

Types: *Bio-Retention Cell*, *Rain Garden*, *Green Roof*, *Infiltration Trench*,
*Permeable Pavement*, *Rain Barrel*, *Rooftop Disconnection*, *Vegetative Swale*.

| Tab | Fields |
|---|---|
| **Surface** | **Storage Depth**; **Roughness (n)**; **Slope (%)** |
| **Soil** | **Thickness**; **Porosity**; **Field Capacity**; **Wilting Point**; **Conductivity**; **Conductivity Slope** |
| **Storage** | **Thickness**; **Void Fraction**; **Seepage Rate** |
| **Drain** | **Coefficient**; **Exponent**; **Offset** |

All four tabs are always present; which layers a type actually has is what the
diagram shows (a rain barrel has no soil layer, a green roof has a drainage mat
rather than storage).

**Engine limitation, stated in the dialog itself:** the engine exposes LID layer
*setters* but no getters, so a control loaded from an existing `.inp` displays
defaults rather than its stored values, and editing it overwrites all four
layers. Only new or user-edited controls are written back. Treat the editor as
authoritative for controls you create in SWMMVis, and check `[LID_CONTROLS]` when
round-tripping someone else's model.

Assigning a control to a subcatchment is done on the **LID Usage** page above,
which writes `[LID_USAGE]`.

\figtodo{13_lid_control_editor.png, The LID Control editor with the layer-stack diagram}

\figtodo{13_lid_types.png, The LID type list showing all eight control types}

### Snow pack editor

**Model → Data Objects → Snowpacks…** opens a two-pane editor (list + form) over
the twenty-seven `[SNOWPACKS]` parameters, grouped exactly as the engine groups
them:

| Group | Fields |
|---|---|
| **Plowable Snow** | Min. / Max. Melt Coefficient; Base Temperature; Free Water Capacity; Initial Snow Depth; Initial Free Water; **Fraction of Impervious Area Plowable** |
| **Impervious Area** | The same six surface values, then **Depth at 100% Cover** |
| **Pervious Area** | The same six surface values, then **Depth at 100% Cover** |
| **Snow Removal** | **Depth at Which Removal Begins**; fractions transferred **Out of Watershed**, **to Impervious Area**, **to Pervious Area**, **Converted to Immediate Melt**, **Transferred to Subcatchment**; and the **Destination Subcatchment** for that last fraction |

**Not exposed in this release:** there is no per-subcatchment snow-pack
assignment row in the Properties panel or the subcatchment attribute table. Snow
packs can be defined and edited here, but binding one to a subcatchment must be
done in the `.inp`. The global snowmelt settings (dividing temperature, ATI
weight, negative melt ratio, elevation, latitude, longitude correction) and the
areal depletion curves live in the Climatology dialog — see \ref manual_climate.

\figtodo{13_snowpack_editor.png, The Snow Pack editor showing the four parameter groups}

### Aquifer editor

**Model → Data Objects → Aquifers…** opens a three-pane editor: the aquifer list
with **New** / **Delete**, a grouped form, and a live two-zone cross-section
illustration whose callouts track the form and emphasise the field that has
keyboard focus.

| Group | Fields |
|---|---|
| **Soil** | **Porosity**; **Wilting Point**; **Field Capacity**; **Unsaturated Zone Moisture** |
| **Conductivity** | **Conductivity**; **Conductivity Slope**; **Tension Slope** |
| **Evaporation** | **Upper Evap. Fraction**; **Lower Evap. Depth**; **Evap. Pattern** (optional monthly pattern, with a "…" button that opens the Pattern editor) |
| **Elevations & Losses** | **Bottom Elevation**; **Water Table Elevation**; **Lower GW Loss Rate** |

Units follow the project unit system (`ft`/`m`, `in/hr`/`mm/hr`). Soft-validation
warnings — wilting point above field capacity, field capacity above porosity,
initial moisture above porosity, water table below bottom elevation — appear
under the form and in the illustration; they do not block editing. The spin-box
ranges are the engine's hard limits.

All of this writes `[AQUIFERS]`. Assign an aquifer to a subcatchment on the
subcatchment's **Aquifer** property row; clearing the pick removes the
subcatchment from `[GROUNDWATER]`.

\figtodo{13_aquifer_editor.png, The Aquifer editor with the two-zone illustration}

### Groundwater Exchange editor

The subcatchment's **Groundwater** row (Properties panel or attribute table)
shows a summary such as `AQ1 → J12 (custom)` and an **Edit…** button. The dialog
is modeless and stays disabled until an aquifer is assigned.

| Group | Control | Writes |
|---|---|---|
| **Receiving node** | **Receiving Node** — the node that receives lateral groundwater flow | `[GROUNDWATER]` |
| | **Surface Elevation** — ground surface for the subcatchment | `[GROUNDWATER]` |
| **Standard lateral flow** | **A1 (GW coeff.)**; **B1 (GW expon.)**; **A2 (Surf. coeff.)**; **B2 (Surf. expon.)**; **A3 (interaction)**; **Threshold Twgr**; **Hstar** | `[GROUNDWATER]` |
| **Custom expressions ([GWF])** | **LATERAL** — *added to* the standard lateral flow | `[GWF]` |
| | **DEEP** — *replaces* the standard deep percolation | `[GWF]` |

The lateral-flow formula
`Q_lat = A1·(HGW − H*)^B1 − A2·(HSW − H*)^B2 + A3·HGW·HSW` is shown above the
coefficients for reference.

Both expression editors highlight syntax, offer completion (type two characters
or press **Ctrl+Space**) with a one-line description of each variable, and
validate against the engine as you type. **Apply** stays disabled while an
expression is invalid, and the offending column number is reported under the
field. An **Insert variable ▾** button lists the vocabulary. The variable and
function lists are read from the engine at construction, so the editor cannot
drift from the grammar.

`[GWF]` expression vocabulary (case-insensitive):

| Variable | Meaning |
|---|---|
| `HGW` | Water table height above the aquifer bottom |
| `HSW` | Surface-water head at the receiving node above the aquifer bottom |
| `HCB` | Channel bottom height above the aquifer bottom (Hstar) |
| `HGS` | Ground surface height above the aquifer bottom |
| `KS` | Saturated hydraulic conductivity |
| `K` | Unsaturated hydraulic conductivity |
| `THETA` | Upper-zone moisture content (fraction) |
| `PHI` | Soil porosity (fraction) |
| `FI` | Surface infiltration rate |
| `FU` | Upper-zone percolation rate |
| `A` | Subcatchment area |

Functions: `abs sgn sqrt log exp sin cos tan asin acos atan step min max cot
sinh cosh tanh coth log10 acot`. `log` is the natural logarithm; `min` and `max`
take exactly two arguments and every other function exactly one. Operators are
`+ - * / ^` with parentheses and unary minus. An empty expression is invalid —
clear the field entirely to remove the `[GWF]` row.

Every node's property sheet carries a read-only **Groundwater Sources** row
listing the subcatchments that discharge groundwater to it (`from S1, S3`); its
**Edit…** button opens this same dialog for the chosen subcatchment. Deleting
such a node warns that those subcatchments lose their receiving node.

\figtodo{13_groundwater_exchange.png, The Groundwater Exchange dialog with a validated LATERAL expression}

\videotodo{Defining an aquifer and wiring a subcatchment's groundwater exchange with a custom GWF expression}

### Unit hydrographs and RDII

**Model → Data Objects → Unit Hydrographs…** opens a non-modal three-pane
editor over `[HYDROGRAPHS]` and `[RDII_DECAY]`. It can also be opened by
double-clicking a unit hydrograph in the Object Browser, from the **Edit…**
button on a unit hydrograph's Parameters property row, and from the "…" button
next to the UH picker on a node's RDII page.

**Left pane — group list.** A **Search groups…** box filters as you type. The
toolbar has **New** (prompts for a name), **Delete** and **Rename**. Deleting a
group cascades to its `[HYDROGRAPHS]` rows, its rain-gage assignment, its
`[RDII_DECAY]` rows and every `[RDII]` node assignment that names it; the
confirmation says so.

**Middle pane — group details.** The group name, a **Rain Gage:** combo
(*(none)* clears the assignment) and a **Season:** combo (*All* plus the twelve
calendar months). Two tabs sit below:

- **RTK** — three rows (*Short-Term*, *Medium-Term*, *Long-Term*) × columns
  **R** (fraction of rainfall becoming I&I), **T** (time to peak, hours) and
  **K** (ratio of base time to peak time). An empty cell means the engine has no
  row for that (response, season) pair, which is distinct from an explicit 0.
  The table follows the **Season** selector.
- **Initial Abstraction** — two groups. *Linear IA (per season)* follows the
  season selector and has columns `Dmax`, `Drec` and `Do`. *Exponential IA decay
  (season-agnostic)* is an eleven-column table: the response label, an **Active**
  checkbox, the six decay parameters (`k_dep`, `k_0`, `k_T`, `T_ref`,
  `theta_rec`, `T_freeze`), a **Snow** checkbox and its two columns (`snow_T`,
  `snow_ddf`). Existence of the `[RDII_DECAY]` row *is* the Active flag —
  unchecking removes the row and falls back to the linear model; checking a blank
  row seeds defaults (`T_ref = 10`). The snow columns stay greyed out until both
  **Active** and **Snow** are checked.

**Right pane — UH preview.** The three RTK triangles are drawn filled with a
dashed **Composite** summation series over them, on a *Time (h)* × *Unit flow*
chart. Empty response rows drop out of both plot and legend. The chart carries
the standard interactive affordances — rubber-band zoom, wheel zoom, pan,
zoom-to-extent, a plot-style menu (per-series visibility, fill colour, outline
width, line style), axis label format and **Chart Properties…**.

A status strip along the bottom reports the number of groups defined, the
parameter-row count of the current group and how many decay rows are active.

**Assigning RDII to a node** is done on the node's **RDII** compound-editor page
(\ref manual_hydraulics): a table of *UH Group* / *Sewer Area* rows with **Remove
Selected**, and an **Add / Update RDII Assignment** form with a **UH Group Name**
picker (whose "…" button opens this editor) and a **Sewer Area** value. That page
writes `[RDII]`.

\figtodo{13_unit_hydrograph_editor.png, The Unit Hydrograph editor with the RTK tab and the preview plot}

\figtodo{13_rdii_decay_tab.png, The Initial Abstraction tab showing linear IA and the exponential decay table}

## Tips and gotchas

- **LID controls loaded from an existing model show defaults.** The engine has no
  LID getters. If you open the LID Control editor on an imported model, do not
  assume the numbers on screen are the ones in the file — and be aware that
  touching any field rewrites all four layers.
- **One gage per subcatchment.** The interpolated assignment method exists
  because SWMM cannot blend gages at run time; it pre-blends them into generated
  gages and generated series. Those generated objects are managed by the dialog —
  a later run removes the ones no longer referenced.
- **Gages with no map location are skipped** by both assignment methods. Place
  them on the map first; the dialog reports how many were excluded.
- **The Groundwater Exchange dialog is gated on the aquifer.** If the form is
  disabled, set the subcatchment's **Aquifer** row first.
- **Modified Horton / Modified Green-Ampt reuse their base model's parameter
  rows.** The row labels do not change; only the model code does.
- **Snow-pack assignment is not in the GUI.** You can author and edit
  `[SNOWPACKS]`, but the subcatchment→snowpack binding is not surfaced.
- **Season "All" versus per-month rows.** Editing the RTK table with the season
  on *All* while per-month rows exist prompts first; accepting clears the
  per-month entries.

## Related

- \ref manual_climate — temperature, evaporation, wind, snowmelt parameters and areal depletion curves
- \ref manual_hydraulics — nodes, links, RDII and dry-weather-flow assignment
- \ref manual_water_quality — pollutants, land uses, build-up/wash-off and treatment
- \ref manual_data_objects — time series, curves and patterns used by gages, aquifers and LID
- \ref manual_2d_mesh — 2D cell infiltration and the "Assign Infiltration to Selection…" dialog
- \ref manual_object_browser — where these objects appear and how the compound editors open
- \ref manual_attribute_tables — editing the same fields in bulk
- \ref manual_analysis_tools — the Rainfall Visualization dialog
- \ref manual_simulation_options — the model-wide `INFILTRATION` option
- \ref tutorial_site_drainage — a worked 1D hydrology model
