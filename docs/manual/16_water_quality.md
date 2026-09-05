@page manual_water_quality 16 — Water Quality, Reaction Systems, Water Age and Heat

## What you'll do

Define the constituents a simulation transports and the processes that change
them: pollutants and their decay, land uses with build-up / wash-off and street
sweeping, per-node treatment expressions, initial in-network concentrations, a
multispecies reaction system compiled from expressions, water-age tracking, and
heat transport with radiative and solar forcing. Every one of these writes a
section of the `.inp` file (or, for reactions, a companion `.rxn` file
registered in `[PROCESS_COMPONENTS]`), and every constituent a run carries
becomes a themeable, plottable result attribute afterwards.

\figtodo{16_quality_overview.png, The Model menu quality entries — Data Objects with Pollutants and Land Uses; plus Initial Quality; Reaction System and Heat Configuration}

## Where to find it

| What | Where |
| --- | --- |
| **Pollutants** | **Model → Data Objects → Pollutants…**; Object Browser → *Pollutants* → double-click |
| **Land Uses** (build-up / wash-off / sweeping) | **Model → Data Objects → Land Uses…**; Object Browser → *Land Uses* |
| **Coverages** and **Loadings** | Subcatchment **Properties** panel — compound cells (see \ref manual_hydrology) |
| **Treatment** | Node **Properties** panel → *Treatment* cell; Attribute Table → *Treatment* column |
| **Initial Quality…** | **Model → Initial Quality…**; per-element from the node/link **Properties** panel |
| **Reaction System…** | **Model → Reaction System…**; ribbon **Model** tab, *Reaction System* |
| **Heat Configuration…** | **Model → Heat Configuration…**; ribbon **Model** tab, *Heat* |
| **Water Age Sources…** | **Model → Water Age Sources…** |
| Transport engine, quality step, dispersion | **Model → Simulation Options…** → *Quality & Transport* (\ref manual_simulation_options) |

None of these carry a default keyboard shortcut. All of them require an open
project — with no project the command logs a warning in the **Message Logs**
panel instead of opening.

\videotodo{Adding a pollutant with a land use and build-up/wash-off then running and theming the result}

## Step-by-step

### Pollutants — `[POLLUTANTS]`

**Model → Data Objects → Pollutants…** opens a two-pane editor: the list of
pollutants on the left with **New** and **Delete**, the field form for the
selected pollutant on the right. Edits apply as you type; **Close** dismisses
the dialog (there is no Cancel — a mistake is undone with **Edit → Undo**).

\figtodo{16_pollutant_editor.png, The Pollutants editor — list pane on the left and the field form on the right}

| Control | What it does | Writes |
| --- | --- | --- |
| **Name** | Pollutant id — must be unique; a duplicate is refused with a warning | `[POLLUTANTS]` name |
| **Concentration Units** | `MG/L`, `UG/L` or `#/L` | units column |
| **Rain Concentration** | Concentration in rainfall | rain conc. column |
| **GW Concentration** | Concentration in groundwater inflow | GW conc. column |
| **I&I Concentration** | Concentration in RDII / infiltration inflow | I&I conc. column |
| **Initial Concentration** | Concentration everywhere in the network at t = 0 | initial conc. column |
| **Decay Coefficient (1/days)** | First-order decay rate | decay column |
| **Snow Only** | Build-up accumulates only while snow is present | snow-only flag |
| **Co-Pollutant** | Another pollutant this one is co-generated with; a pollutant cannot reference itself | co-pollutant column |
| **Co-Fraction** | Fraction of the co-pollutant's runoff concentration | co-fraction column |

**Units are write-once.** The engine has no "change units" call, so the
**Concentration Units** combo only takes effect for a pollutant that has not
yet been created on the engine. Changing it on an existing pollutant does not
convert the model; delete and re-add the pollutant instead.

Deleting a pollutant cascades — its build-up and wash-off rows, coverages,
loadings, treatment expressions and initial-quality rows go with it. The
confirmation prompt names the pollutant before it does.

### Land uses — `[LANDUSES]`, `[BUILDUP]`, `[WASHOFF]`

**Model → Data Objects → Land Uses…** opens the same list-plus-detail shape,
with the detail pane split into three tabs. Rows in the two function tables are
the model's pollutants and re-dimension automatically as pollutants are added,
removed or renamed while the dialog is open.

\figtodo{16_landuse_editor.png, The Land Uses editor with the General && Sweeping tab}

**General && Sweeping** tab:

| Control | What it does | Writes |
| --- | --- | --- |
| **Name** | Land-use id | `[LANDUSES]` name |
| **Sweep Interval (days)** | Days between street-sweeping events | `[LANDUSES]` sweep interval |
| **Sweep Removal (fraction)** | Fraction of the *available* build-up that a sweeping event removes | `[LANDUSES]` availability |

The per-pollutant sweeping efficiency is a separate number and lives on the
**Washoff** tab as **Sweep Effic (%)**; the dialog says so in a hint under the
form.

**Buildup** tab — one row per pollutant, writing `[BUILDUP]`:

\figtodo{16_landuse_buildup.png, The Buildup tab — one row per pollutant with function and coefficients}

| Column | What it does |
| --- | --- |
| **Pollutant** | Row label; read-only |
| **Function** | `NONE`, `POW`, `EXP`, `SAT` or `EXT` |
| **Max/C1** | Maximum build-up / first coefficient |
| **Rate/C2** | Rate constant / second coefficient |
| **Power/Sat/C3** | Power, saturation constant, or — under `EXT` — the index of a driving time series |
| **Per Unit** | Normaliser: `AREA` or `CURB` |

Under the `EXT` function the third coefficient holds a *time-series table
index*, not a number. The cell is read-only in that case and shows
`(series #N)` with a tooltip explaining why, so a numeric edit cannot corrupt
the reference.

**Washoff** tab — one row per pollutant, writing `[WASHOFF]`:

\figtodo{16_landuse_washoff.png, The Washoff tab with per-pollutant coefficients and sweeping and BMP efficiencies}

| Column | What it does |
| --- | --- |
| **Pollutant** | Row label; read-only |
| **Function** | `NONE`, `EXP`, `RC` or `EMC` |
| **Coefficient** | Wash-off coefficient |
| **Exponent** | Wash-off exponent |
| **Sweep Effic (%)** | Per-pollutant street-sweeping removal efficiency |
| **BMP Effic (%)** | Per-pollutant BMP removal efficiency |

Deleting a land use is impact-aware: the confirmation lists what else is
removed (its build-up and wash-off rows and any subcatchment coverages that
reference it) before the delete runs.

### Coverages and loadings

A land use only does anything once subcatchments assign area to it. **Coverage**
(`[COVERAGES]`) and initial surface **Loading** (`[LOADINGS]`) are compound
cells on the subcatchment in the **Properties** panel and as columns in the
subcatchment attribute table — see \ref manual_hydrology for the subcatchment
editor and \ref manual_attribute_tables for editing them in bulk.

### Treatment — `[TREATMENT]`

Treatment is a per-(node, pollutant) removal expression, edited from the node's
**Treatment** compound cell in the **Properties** panel or from the
**Treatment** column of the node attribute table. The dialog shows one row per
pollutant with an expression cell; clearing a cell removes the expression.

\figtodo{16_treatment_editor.png, The Treatment page of the node compound editor with a validated expression}

Expressions are of the form `R = …` (removal fraction) or `C = …` (outlet
concentration). The editor highlights the grammar and completes identifiers as
you type; a status banner under the table shows **● Valid expression** or the
validator's column-numbered complaint. The available variables are `C`
(concentration), `R` (removal fraction), `DT` (step, seconds), `HRT`
(hydraulic residence time), `Q` (flow), `V` (volume), `D` (depth) and `AREA`;
the functions are `exp`, `log`, `ln`, `sqrt`, `min`, `max`, `abs`, `sgn` and
`step`. The engine's own validator has the last word — an expression it
rejects is not written.

### Initial quality — `[INITIAL_QUALITY]`

**Model → Initial Quality…** opens a single add/remove table of per-element
starting concentrations, applied at the start of the run.

\figtodo{16_initial_quality.png, The Initial Quality dialog with node and link rows}

| Column | What it does |
| --- | --- |
| **Scope** | `Node` or `Link` |
| **Element** | The node or link id, picked from a combo |
| **Constituent** | Any pollutant, plus **Water age (hours)** and **Temperature (°C)** — the reserved species, offered only when their `[OPTIONS]` toggle is on |
| **Value** | Pollutant concentration in the pollutant's own units; water age in **hours** (a negative value is legal); temperature in °C |

**Add** appends a row, **Remove** deletes the selected one, and **OK** writes
the table to the engine. The same dialog opens scoped to one element from that
element's **Initial Quality** property cell — the Scope and Element columns
collapse, rows belonging to other elements are neither shown nor touched, and
**Add** creates rows pinned to that element. Accepting a whole-model edit also
reloads the **Attribute Table**, which surfaces the same rows as
per-constituent columns.

For a 2D model the engine also reads `[2D_INITIAL_QUALITY]` and
`[2D_BOUNDARY_QUALITY]`. SWMMVis has no dedicated editor for those two
sections — it round-trips them unchanged when the deck is saved, so hand-edited
values survive. See \ref manual_2d_mesh.

### The reaction system — `[REACTION_*]` and the `.rxn` file

**Model → Reaction System…** opens a non-modal, tabbed editor for a
multispecies reaction system: species you declare, coefficients they use, named
intermediate terms, and one kinetic expression per species and scope. Every
structured edit applies to the engine immediately and is validated there; a
rejected edit is rolled back and the reason appears in the status line at the
bottom of the dialog, so the model can never reach a state that will not
compile. Persistence to disk happens only on **Save to File**.

\figtodo{16_reaction_system_species.png, The Reaction System editor on the Species tab}

**Options** tab — writes `[REACTION_OPTIONS]` in the `.rxn` file:

| Control | Values | Writes |
| --- | --- | --- |
| **Solver** | `EUL`, `RK5`, `ROS2`, `BDF2` | `SOLVER` |
| **Coupling** | `NONE`, `FULL` | `COUPLING` |
| **Rate units** | `SEC`, `MIN`, `HR`, `DAY` | `RATE_UNITS` |
| **Area units** | `FT2`, `M2`, `CM2` | `AREA_UNITS` |
| **Reaction step (s, 0 = quality step)** | Sub-step for the kinetics integrator | `TIMESTEP` |
| **Absolute tolerance** | Integrator absolute tolerance | `ATOL` |
| **Relative tolerance** | Integrator relative tolerance | `RTOL` |

**Species** tab — writes `[REACTION_SPECIES]`. Each row is **Kind**, **Name**,
**Units**, **Atol**, **Rtol**; the add row at the bottom takes a kind
(`BULK` or `WALL`), a name and a units string (`MG` by default). `BULK` species
live in the water column; `WALL` species live on the wetted surface. Removing a
species that an expression still references is refused, and the status line
says so.

**Coefficients** tab — writes `[REACTION_COEFFICIENTS]`. Rows are **Kind**
(`PARAMETER` or `CONSTANT`), **Name** and **Value**. A `CONSTANT` is fixed for
the whole model; a `PARAMETER` is a per-element quantity the deck can override.
Removal is refused while an expression still references the name.

**Terms** tab — writes `[REACTION_TERMS]`. A term is a named sub-expression you
can reuse in the kinetic expressions, edited in the same expression cell as the
expressions themselves. An invalid term is refused and the previous one is kept.

\figtodo{16_reaction_expressions.png, The Expressions tab with the syntax-highlighted expression editor and the validation banner}

**Expressions** tab — writes `[REACTION_PIPES]` and `[REACTION_TANKS]`. There
are two rows per species, one for each scope:

| Column | What it does |
| --- | --- |
| **Species** | Row label; read-only |
| **Scope** | **Pipes** (flowing conduits) or **Tanks** (storage units) |
| **Form** | `None`, `RATE`, `EQUIL` or `FORMULA` |
| **Expression** | The right-hand side |

`RATE` integrates dφ/dt = *expr*; `EQUIL` solves *expr* = 0 for the species;
`FORMULA` assigns φ = *expr* directly. Choosing a form with an empty expression
cell is refused with a prompt to enter one first.

The expression cell is a completing editor whose vocabulary comes from the
engine, not from a hard-coded list, so it cannot drift from what actually
compiles. It highlights and completes:

- **species, coefficients, terms and pollutant names** from the live model
  (matched case-sensitively — the compiler matches them exactly);
- **hydraulic variables** `D` (depth, ft), `Q` (flow, cfs), `U` (velocity,
  ft/s), `RE` (Reynolds number), `US` (shear velocity, ft/s), `FF`
  (Darcy–Weisbach friction factor), `AV` (wetted surface area per volume,
  1/ft), `HRT` (hydraulic residence time, s), `DT` (reaction step, s) and
  `TEMP` (water temperature, °C — the `TEMPERATURE` option when heat transport
  is off). Variable names are matched case-insensitively;
- **functions** `EXP`, `LOG`, `LOG10`, `SQRT`, `ABS`, `SGN`, `STEP`, `SIN`,
  `COS`, `TAN` (one argument) and `MIN`, `MAX`, `POW` (two arguments).

Press **Ctrl+Space** — or just type two characters — for the completion list.
Validation runs debounced as you type and again on commit; the authoritative
verdict is the engine's own expression compiler.

**Initial Quality** tab — writes `[REACTION_QUALITY]`. The upper table sets a
`GLOBAL` initial value per species; the lower one adds `NODE` / `LINK`
overrides with **Add** and **Remove**.

**File** tab — the whole reaction system serialised as `.rxn` text. Editing the
text and leaving the tab applies it back to the engine (an unparsable text
keeps you on the tab and reports the error); **Discard text edits** re-reads
the engine instead. The structured tabs and the File tab are two views of one
state, which is what keeps them in step.

\figtodo{16_reaction_file_tab.png, The File tab showing the serialised .rxn text}

**Save to File** writes the `.rxn` file registered for this model in
`[PROCESS_COMPONENTS]`. When no reactions component is bound yet, SWMMVis
offers to register one and create the config file in a single step; the title
bar shows the bound file, or *(no file bound)*. Nothing is written to disk
until you press Save — the structured edits before that live on the engine
only.

**Sources** tab — present but disabled. `[REACTION_SOURCES]` needs an engine
phase this build does not have; the tab explains that instead of offering
controls.

### Water age — `WATER_AGE ON` and `[WATER_AGE_SOURCES]`

Water-age tracking is turned on with **WATER_AGE** on the *Quality & Transport*
page of **Model → Simulation Options…**, which writes `WATER_AGE ON` in
`[OPTIONS]`. Because age is transported by the Eulerian ARD scheme, that page
also selects the transport engine — the two keys are written together.

**Model → Water Age Sources…** sets the age that water carries as it enters the
model by each pathway.

\figtodo{16_water_age_sources.png, The Water Age Sources dialog — global ages above and per-node overrides below}

The **Global source ages** table has one row per pathway, all in hours:

| Pathway | Water it ages |
| --- | --- |
| **Rainfall / runoff** | Surface runoff generated by wash-off |
| **Dry weather flow** | `[DWF]` inflow |
| **Groundwater** | Groundwater inflow |
| **RDII** | Rainfall-derived inflow and infiltration |
| **External inflow** | `[INFLOWS]` inflow |
| **Routing interface file** | Flow read from a routing-interface file |
| **Initial network state** | Water standing in the network at t = 0 |

The **Per-node overrides** table below adds `NODE`-scoped rows with **Add** and
**Remove**. Only **Dry weather flow** and **External inflow** accept overrides —
the engine's scope rule, which the source combo enforces. `SUBCATCH` and
`EDGE_BC` scopes are not available in this build.

**Negative ages are legal.** A negative source age *extracts* age-volume,
making the receiving water read younger, clamped so that age never falls below
zero. The dialog therefore accepts negative values rather than validating them
away.

Water age can also be given as an inflow constituent on a node (choose **Water
age (hours)** in the node's *Inflows* editor); its value column is hours, never
mg/L, and the `MASS` inflow type is not offered for it.

### Heat transport — `HEAT_TRANSPORT ON` and the heat sections

Heat transport is turned on with **HEAT_TRANSPORT** on the *Quality &
Transport* page of **Model → Simulation Options…**, writing `HEAT_TRANSPORT ON`
in `[OPTIONS]`. **Model → Heat Configuration…** then configures the sources and
the flux modules. The dialog has five tabs.

\figtodo{16_heat_sources.png, The Sources tab of the Heat Configuration dialog}

**Sources** tab — writes `[HEAT_SOURCES]`. Seven rows, the same pathways as the
water-age table, each with a **Set** check box and a **Temperature** spin
between −50 °C and 100 °C (the range the engine's parser accepts). The check box
matters: a source the model never set reads the 20 °C default, and leaving the
box clear keeps it that way, so pressing **OK** on an untouched dialog cannot
invent `[HEAT_SOURCES]` rows. The **Per-node overrides** table below adds rows
for **Dry weather flow** and **External inflow** only.

**Fluxes** tab — writes `[HEAT_FLUXES]` module toggles:

| Check box | Module |
| --- | --- |
| **Surface exchange (latent + sensible)** | `SURFACE_EXCHANGE` |
| **Radiative exchange (shortwave + longwave)** | `RADIATIVE_EXCHANGE` |
| **LID layer conduction** | `LAYER_CONDUCTION` |

\figtodo{16_heat_radiative.png, The Radiative tab with the shortwave mode radio buttons and the radiative parameters}

**Radiative** tab — writes `[RADIATIVE_FLUXES]`. *Incoming shortwave* is one of
three modes:

| Mode | What it means |
| --- | --- |
| **Constant** | A fixed flux in W/m², 0–1500 |
| **Timeseries** | A named time series supplies the flux |
| **Computed (solar position + Bird clear sky)** | Computed from the site geometry on the *Solar* tab |

The *Radiative parameters* group holds seven fractions, all 0–1: **Water albedo
Rs**, **Shade factor fs**, **Sky view fsky**, **Water emissivity**,
**Land-cover emissivity**, **Brunt atmospheric coeff.** and **Longwave
reflection RL**.

**Solar** tab — writes `[SOLAR_RADIATION]`; consulted only under the
**Computed** shortwave mode. *Site*: **Latitude (°, +N)**, **Longitude (°, +E)**,
**Timezone (h from UTC)**, **Elevation (m)**. *Atmosphere (Bird clear-sky
model)*: **Aerosol depth at 380 nm**, **Aerosol depth at 500 nm**,
**Precipitable water (cm)**, **Ozone column (cm)**, **Ground albedo (land)**.
Selecting **Computed** without a latitude and longitude is refused with a
prompt to fill the Solar tab first.

\figtodo{16_heat_solar.png, The Solar tab with the site geometry and Bird atmosphere parameters}

**Cloud** tab — writes `[CLOUD_COVER]`. A **Cloud cover configured** check box
(clear it and the model is treated as clear sky), a **Fraction [0..1]**, a
**Fraction timeseries** combo, and the three attenuation coefficients
**Shortwave atten. k**, **Shortwave atten. n** and **Longwave cloud k**. One
fraction drives both the shortwave and the longwave path.

Air temperature, wind and the standalone solar-radiation series are climate
data, not heat configuration — see \ref manual_climate.

**Known gap.** The engine reports the shortwave and cloud time-series *mode*
but not the bound series *name*, so those two combos rebind rather than display
what is bound. The `(keep current series)` entry is the no-op position: leave it
selected and the existing binding is untouched.

### Species as result attributes

A finished run carries N species — every pollutant, plus the reserved
`__WATER_AGE__` and `__TEMPERATURE__` when those options were on. They appear
throughout the results side of the application as ordinary attributes:

- in the results style panels and the Style Manager as themeable attributes
  (\ref manual_styling and \ref manual_results);
- in the plot variable picker for time-series, profile and scatter plots
  (\ref manual_time_series_plots, \ref manual_profile_plots);
- in the attribute table's result columns and the statistics dashboard
  (\ref manual_tabular_results).

Water age is labelled **Water age (hours)** and temperature **Temperature
(°C)**; ordinary pollutants use their own concentration units. Saved themes and
plots reference a species by **name**, not by index, so adding or reordering
pollutants cannot silently repoint a saved theme. If a saved theme names a
species the currently open run does not carry, the theme degrades to no theme
and a message appears once per token in the **Message Logs** panel.

\figtodo{16_species_attributes.png, A results style panel listing pollutant species alongside water age and temperature}

## Tips and gotchas

- **Set the transport engine before expecting reactions, age or heat.** Water
  age, heat transport and the reaction system all ride the Eulerian ARD
  transport scheme. Choose it on the *Quality & Transport* page of
  \ref manual_simulation_options; the legacy quality routing does not carry
  them.
- **Pollutant units cannot be changed after creation.** Pick them when you add
  the pollutant.
- **The reaction editor is non-modal and applies immediately.** Structured
  edits are already on the engine before you press anything; only the `.rxn`
  file needs **Save to File**. Closing the dialog after any successful write
  marks the project dirty.
- **A reaction expression is refused, not silently truncated.** When the status
  line reports a rejection, the previous expression is still in force.
- **`EXT` build-up C3 is a series index.** Do not try to type a number over it;
  edit the referenced time series instead (\ref manual_data_objects).
- **An untouched Heat Configuration writes nothing.** The per-source **Set**
  check boxes exist precisely so that opening the dialog and pressing OK cannot
  add `[HEAT_SOURCES]` rows you did not intend.
- **Negative water ages are a feature.** Use them to represent water entering
  younger than the water it mixes into; the engine clamps the result at zero.
- **Land-use tables follow the pollutant list live.** Add a pollutant while the
  Land Uses dialog is open and its build-up and wash-off rows appear at once.
- **Deleting a pollutant or land use cascades.** Read the confirmation text; it
  lists what goes with it.

## Related

- \ref manual_hydrology — subcatchments, coverages, loadings, LID controls and groundwater
- \ref manual_hydraulics — nodes, links and the inflows that carry quality into the network
- \ref manual_climate — temperature, evaporation, wind, snowmelt and solar radiation
- \ref manual_data_objects — the time series and patterns these editors reference
- \ref manual_simulation_options — `QUALITY_SOLVER`, `QUALITY_STEP`, dispersion, `WATER_AGE`, `HEAT_TRANSPORT`
- \ref manual_2d_mesh — 2D initial and boundary quality sections
- \ref manual_object_browser — the Properties panel and the compound editors
- \ref manual_attribute_tables — editing coverages, loadings and treatment in bulk
- \ref manual_results — theming a run by species
- \ref manual_time_series_plots — plotting concentrations, age and temperature
- \ref tutorial_site_drainage — a worked build-up / wash-off model
- \ref tutorial_transport — reaction systems, water age and heat end to end
