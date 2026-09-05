@page manual_simulation_options 18 — Simulation Options

## What you'll do

Set everything the engine needs to run the active model: the process models it
uses, the simulation window and time steps, the routing solver and its
tolerances, the transport engine, threading, the project CRS, which 2D mesh is
active, the 2D marcher, and the secondary files, report contents and plugins.
Almost every control on this dialog maps to one `[OPTIONS]` key (or one
`[2D_OPTIONS]`, `[REPORT]`, `[FILES]`, `[EVENTS]` or `[PLUGINS]` entry) in the
`.inp` file.

\figtodo{18_options_dialog.png, The Simulation Options dialog with the category sidebar and the Models / Processes page}

## Where to find it

**Model → Simulation Options…**, or *Simulation Options* on the ribbon **Model**
tab. No default shortcut. The dialog needs an open project.

The layout is a category list down the left and one page per category on the
right; each page scrolls independently. The categories, in order:

1. **Title / Notes**
2. **Models / Processes**
3. **Dates & Times**
4. **Routing & Hydraulics**
5. **Quality & Transport**
6. **System / Performance**
7. **Spatial & CRS**
8. **Mesh**
9. **2D Surface Routing** — present only in a build compiled with the 2D module
10. **Files / Output / Plugins**

The dialog re-reads every key from the active project's engine each time it
opens, so the controls always reflect the current model rather than a snapshot.
Options the running engine does not support are disabled with a tooltip saying
why — a legacy SWMM 5.x engine, or a 6.x build that predates a given solver.

\videotodo{Setting up a dynamic-wave run — dates; time steps; solver tolerances and threads}

## Step-by-step

### Title / Notes

A rich-text editor for the model's `[TITLE]` section, with a small toolbar:
**Bold** (Ctrl+B), **Italic** (Ctrl+I), **Underline** (Ctrl+U), **Bulleted
list** and **Numbered list**. When the dialog was opened from a project window
the formatted text is kept in the project's `.oswp` sidecar; the plain text
always round-trips through the engine into `[TITLE]`.

\figtodo{18_title_notes.png, The Title / Notes page with its formatting toolbar}

### Models / Processes

\figtodo{18_models_processes.png, The Models / Processes page — process models; modules and active processes}

**Process models**

| Control | Options | Writes |
| --- | --- | --- |
| **Infiltration model** | Horton / Modified Horton / Green-Ampt / Modified Green-Ampt / Curve Number | `INFILTRATION` |
| **Flow routing** | Steady / Kinematic Wave / Dynamic Wave / Finite Volume | `FLOW_ROUTING` |

Choosing **Finite Volume** enables the two finite-volume groups on the
*Routing & Hydraulics* page. The FV entry is disabled on an engine that does not
carry the FV solver.

**Modules**

| Check box | What it does |
| --- | --- |
| **1D Hydraulics (always on)** | Always checked and disabled — the SWMM core cannot be turned off |
| **2D Surface Routing** | A project-level flag stored with the project. When on, the *Mesh* and *2D Surface Routing* pages become interactive; when off the 2D sidebar row is greyed out |

The engine itself activates its 2D solver from the *presence* of mesh sections
in the deck, not from this flag; a model that already carries `[2D_OPTIONS]`,
`[2D_VERTICES]`, `[2D_TRIANGLES]` or `[2D_MESH_FILE]` opens with the box
already checked. In a build without the 2D module the flag still toggles the
Mesh page — mesh generation works regardless — but the coupled run does not.

**Options / flags**

| Check box | Writes |
| --- | --- |
| **Allow ponding at nodes (ALLOW_PONDING)** | `ALLOW_PONDING` |

**Active processes** — six check boxes whose sense is inverted relative to the
`.inp`: **checked means the process runs**, and unchecking writes the legacy
`IGNORE_*` key as `YES`.

| Check box | Writes when unchecked |
| --- | --- |
| **Rainfall / runoff** | `IGNORE_RAINFALL YES` |
| **Snowmelt** | `IGNORE_SNOWMELT YES` |
| **Groundwater** | `IGNORE_GROUNDWATER YES` |
| **RDII** | `IGNORE_RDII YES` |
| **Water quality** | `IGNORE_QUALITY YES` |
| **Flow routing** | `IGNORE_ROUTING YES` |

### Dates & Times

\figtodo{18_dates_times.png, The Dates & Times page with the simulation window; time steps and the Events table}

**Simulation window**

| Control | Writes |
| --- | --- |
| **Start** | `START_DATE` + `START_TIME` |
| **End** | `END_DATE` + `END_TIME` |
| **Duration** | Read-only `End − Start`, shown as `Dd HH:MM:SS` |
| **Report start** | `REPORT_START_DATE` + `REPORT_START_TIME` — clamped so it can never precede **Start** |

Date-times are entered with a calendar popup in `yyyy-MM-dd HH:mm:ss`; the
engine always sees the legacy `MM/DD/YYYY` + `HH:MM:SS` form.

**Time steps** — all but the routing step are entered as a timespan:

| Control | Writes |
| --- | --- |
| **Reporting step** | `REPORT_STEP` |
| **Dry-weather step** | `DRY_STEP` |
| **Wet-weather step** | `WET_STEP` |
| **Control rule step** | `RULE_STEP` — 0 evaluates rules every routing step |
| **Routing step** | `ROUTING_STEP`, in decimal seconds (a plain text box, 0.001–3600) |

**Skip steady state**

| Control | Writes |
| --- | --- |
| **Skip steady-periods (SKIP_STEADY_STATE)** | `SKIP_STEADY_STATE` |
| **Lateral flow tol** | `LAT_FLOW_TOL` — shown as a percentage, stored as a fraction |
| **System flow tol** | `SYS_FLOW_TOL` — same conversion |

Both tolerances grey out while **Skip steady-periods** is off; the values are
kept, so turning it back on restores them.

**Sweep / antecedent**

| Control | Writes |
| --- | --- |
| **Start sweeping on** | `SWEEP_START` — `MM/DD`, no year |
| **End sweeping on** | `SWEEP_END` |
| **Antecedent dry days** | `DRY_DAYS` |

**Events ([EVENTS])** — an optional list of routing-active windows. With
**Skip steady-periods** on, the engine routes full hydraulics only inside these
windows and steps quickly through the dry periods between them.

| Action | How |
| --- | --- |
| **Add row** | Pre-filled with the simulation Start and End so you only edit the deltas |
| **Remove selected** | Select one or more rows by their row headers and press it |
| Edit a time | Click the cell and type, use the spin buttons, or drop the calendar popup — `MM/DD/YYYY HH:MM` |

Validation: **Start ≥ End** on any row **blocks** Apply and OK until it is
fixed. Overlapping rows and rows outside the simulation window raise a
non-blocking warning you can override.

### Routing & Hydraulics

\figtodo{18_routing_hydraulics.png, The Routing & Hydraulics page — surcharge handling and the solver group}

**Surcharge handling**

| Control | Options / meaning | Writes |
| --- | --- | --- |
| **Method** | EXTRAN (legacy) / SLOT (Preissmann) / DYNAMIC_SLOT / TPA (two-component pressure, experimental) | `SURCHARGE_METHOD` |
| **DPS celerity** | Target wave celerity, m/s — DYNAMIC_SLOT only | `DPS_CELERITY` |
| **DPS alpha** | Alpha exponent, ≥ 2 — DYNAMIC_SLOT only | `DPS_ALPHA` |
| **DPS decay** | Decay time, s — DYNAMIC_SLOT only | `DPS_DECAY_TIME` |
| **TPA celerity** | Acoustic celerity *a* in project length units per second — TPA only | `TPA_CELERITY` |

The three DPS rows enable only under DYNAMIC_SLOT; the TPA row only under TPA.

**Solver**

| Control | Options / meaning | Writes |
| --- | --- | --- |
| **Node continuity** | Explicit (legacy) or Semi-implicit — better on low-slope networks | `NODE_CONTINUITY` |
| **Anderson acceleration (ANDERSON_ACCEL)** | Accelerates the iterative solve | `ANDERSON_ACCEL` |
| **Max trials** | Iteration cap per routing step | `MAX_TRIALS` |
| **Head tolerance** | Convergence tolerance on node head | `HEAD_TOLERANCE` |
| **Lengthening step** | Conduit lengthening time step, s | `LENGTHENING_STEP` |
| **Variable step factor** | Courant safety fraction; 0 disables | `VARIABLE_STEP` |
| **Minimum step** | Smallest routing step the adaptive solver may take, s | `MINIMUM_STEP` |

**Minimum step** matters most in 1D–2D coupled runs: the coupling drives the 1D
step toward this floor, and raising it (1.0–1.5 s) recovers most of the runtime.
The *System / Performance* page has a one-click preset that does exactly that.

**Finite volume solver** — enabled only while **Flow routing** is Finite Volume.
The engine accepts these keys as inert under any other router, so a greyed-out
group never invalidates the project and your FV configuration is not lost when
you switch routers mid-session.

| Control | Options / meaning | Writes |
| --- | --- | --- |
| **Cell length** | Target cell length in project length units; the special value reads **whole conduit** (one cell per conduit) | `FV_CELL_LENGTH` |
| **Min cells per conduit** | Floor on cells per conduit | `FV_MIN_CELLS` |
| **CFL number** | Courant number the explicit substep targets | `FV_CFL` |
| **Riemann solver** | HLLC or HLL | `FV_RIEMANN` |
| **Spatial order** | 1st order, or 2nd order (MUSCL-Hancock) | `FV_ORDER` |
| **Slope limiter** | Minmod / van Leer / Superbee — enabled only at 2nd order | `FV_LIMITER` |
| **Time integration** | Euler or RK2 | `FV_TIME_INTEGRATION` |
| **Slot celerity** | Preissmann-slot pressure-wave celerity | `FV_SLOT_CELERITY` |
| **Pressure closure** | SLOT (Preissmann) or TPA — TPA carries a signed pressure head so sub-atmospheric full-pipe flow is representable | `FV_PRESSURE_CLOSURE` |
| **Implicit pressurized head solve (experimental)** | Solves surcharged-cell heads implicitly; CPU backend only and local time stepping stands down while it engages | `FV_PRESSURIZED_IMPLICIT` |
| **Structure coupling** | Re-evaluate weir / orifice / pump flows **Every substep** or **Every routing step** | `FV_STRUCTURE_COUPLING` |

Deliberately not on this page: `FV_SCALAR_SCHEME` (its only consumer is the
Eulerian ARD transport engine, so it lives on *Quality & Transport*) and
`FV_DISPERSION`, which is inert on every path — the engine warns at open when a
deck sets it. Decks that set the retired `FV_NODE_COUPLING`, `FV_NODE_DT`,
`FV_NODE_PICARD` or `VIRTUAL_JUNCTION_MOMENTUM FULL` still open; the engine
warns once and uses the built-in behaviour.

\figtodo{18_fv_groups.png, The finite-volume solver and performance groups enabled under FV routing}

**Finite volume performance** — also FV-only:

| Control | Options / meaning | Writes |
| --- | --- | --- |
| **Backend** | Auto / CPU (serial) / OpenMP / CUDA / HIP / SYCL | `FV_BACKEND` |
| **Min parallel cells** | Cell count below which the solver stays serial | `FV_MIN_PARALLEL_CELLS` |
| **Compact dry-cell storage (FV_COMPACTION)** | Skip fully dry reaches in the substep loop | `FV_COMPACTION` |
| **Local time stepping (FV_LTS)** | Advance slow cells with larger substeps grouped in tiers | `FV_LTS` |
| **LTS max tiers** | Cap on the tier spread; enabled only while LTS is on | `FV_LTS_MAX_TIERS` |
| **CFL census interval** | Substeps between full Courant re-surveys; 1 = every substep | `FV_CFL_CENSUS_INTERVAL` |

**Unsteady friction** — its own group, because both the dynamic-wave and the
finite-volume solvers consume it. It is enabled only under `DYNWAVE` or `FV`
routing, and only on an engine that carries the keys.

| Control | Options / meaning | Writes |
| --- | --- | --- |
| **Method** | None, or Vitkovsky (instantaneous-acceleration model) | `UNSTEADY_FRICTION` |
| **Coefficient k3** | Brunone-type coefficient; enabled only when a method is selected | `UF_K3` |

**Conduit / channel**

| Control | Options | Writes |
| --- | --- | --- |
| **Force-main equation** | Hazen-Williams (H-W) or Darcy-Weisbach (D-W) | `FORCE_MAIN_EQUATION` |
| **Normal-flow criterion** | Slope / Froude / Both / Neither | `NORMAL_FLOW_LIMITED` |
| **Inertial damping** | None / Partial / Full — dynamic-wave routing only | `INERTIAL_DAMPING` |
| **Min surface area** | Lower clamp on nodal surface area | `MIN_SURFAREA` |
| **Min conduit slope** | Lower clamp on conduit slope, percent | `MIN_SLOPE` |

### Quality & Transport

\figtodo{18_quality_transport.png, The Quality & Transport page with the solver selection and the reserved-species group}

**Water quality solver**

| Control | Options | Writes |
| --- | --- | --- |
| **Solver** | Legacy (complete mix) / Eulerian ARD (advection–reaction–dispersion) / Lagrangian (LARD) | `QUALITY_SOLVER` |
| **Outfall backflow** | Hold last concentration (legacy) or Fresh (zero concentration and age) | `OUTFALL_BACKFLOW_QUALITY` |

Under **Hold last** an outfall re-injects its held state, and under water age
that water keeps ageing with the clock, so a permanently supplying outfall grows
old without bound. **Fresh** makes a supplying outfall behave like an
EPANET-style reservoir.

**Eulerian ARD** — enabled only while that solver is selected:

| Control | Options | Writes |
| --- | --- | --- |
| **Scalar scheme** | MUSCL / Upwind / QUICKEST-ULTIMATE | `FV_SCALAR_SCHEME` |

Dispersion and the transport-mesh spacing for the ARD engine are configured in
its component file (`model.ard`: `[TRANSPORT_OPTIONS]`,
`[CONDUIT_DISPERSION]`), bound on the *Files / Output / Plugins* page. A bound
component file's `SCALAR_SCHEME` overrides the combo above.

**Lagrangian (LARD)** — enabled only while that solver is selected:

| Control | Meaning | Writes |
| --- | --- | --- |
| **Quality step** | Transport substep in seconds; 0 follows the routing step | `QUALITY_STEP` |
| **Max segments per link** | Segment slab capacity per link | `MAX_SEGMENTS_PER_LINK` |
| **Dispersion** | Off, or RWPT (random-walk particle tracking) | `DISPERSION` |
| **RWPT seed** | Deterministic seed — the same seed reproduces a run bit-for-bit at any thread count; enabled only under RWPT | `RWPT_SEED` |

**Reserved species**

| Control | Meaning | Writes |
| --- | --- | --- |
| **Track water age** | Adds the reserved `__WATER_AGE__` species, reported in hours | `WATER_AGE` |
| **Simulate heat transport** | Adds the reserved `__TEMPERATURE__` species, reported in °C | `HEAT_TRANSPORT` |
| **Edit Source Ages...** | Opens the Water Age Sources editor — writes straight to the engine with its own OK/Cancel | `[WATER_AGE_SOURCES]` |
| **Edit Initial Quality...** | Opens the per-element Initial Quality editor — likewise | `[INITIAL_QUALITY]` |

`QUALITY_SOLVER` and `WATER_AGE` are written together on save: a deck carrying
`WATER_AGE ON` without its transport-engine line reopens with age tracking
silently off. See \ref manual_water_quality for the editors these two buttons
open.

### System / Performance

\figtodo{18_performance.png, The System / Performance page with the thread spin and the effective-thread summary}

| Control | Meaning | Writes |
| --- | --- | --- |
| **Worker threads** | OpenMP team size for the 1D and 2D solvers. **0 = auto**, and the spin's special value reads *auto* | `THREADS` |

Under the spin is a live **Effective threads** line reporting what the engine
would actually use for the general, dynamic-wave and 2D teams at the requested
count — read from the engine, so it never re-implements its heuristics. Asking
for more threads than the machine has logical processors is allowed but flagged
with a warning glyph, as is exceeding the performance-core count on a machine
that has efficiency cores. The tooltip summarises the machine's logical CPUs,
performance cores, any `OMP_NUM_THREADS` / affinity limit, and whether a 2D
Kokkos backend has already fixed its thread count for this process.

**Apply fast preset** sets **Worker threads** to the machine's performance-core
count and **Minimum step** (on *Routing & Hydraulics*) to 1.0 s — the
conservative speed recipe for 1D–2D coupled runs, roughly 2.6× faster with mass
balance as good as or better than the default. It only fills in the controls;
you still press Apply or OK to commit.

The `IGNORE_*` flags are on *Models / Processes*, not here — they are about
*which* models run, not how fast.

### Spatial & CRS

\figtodo{18_spatial_crs.png, The Spatial & CRS page showing the layer CRS and the model extent}

| Control | What it shows | Writes |
| --- | --- | --- |
| **Layer CRS** | Read-only authority code of the layer's stored CRS, e.g. `EPSG:6595`, or *(local)* / *(none)* | — |
| **Change…** | Opens the CRS picker; applies the choice to the layer and to `[OPTIONS] CRS` | `CRS` |
| **Detect from coordinates** | Inspects the model extent and suggests `EPSG:4326` when every coordinate falls inside geographic bounds; non-destructive | — |
| **Model extent** | Read-only `X: [min, max]   Y: [min, max]` in the stored CRS | — |

Changing the CRS here updates the *stored* CRS — it does **not** transform
coordinates. To permanently reproject a model, use the CRS button in the status
bar (see \ref manual_crs).

**Known gap in this build.** The **Change…** and **Detect from coordinates**
buttons are drawn but not wired — their signal connections sit after the page
builder returns, so clicking them does nothing. Set the project CRS from the
status-bar CRS control until this is fixed.

### Mesh

\figtodo{18_mesh_page.png, The Mesh page listing candidate .2dm files next to the project}

This page picks which 2D mesh configuration the engine reads. It lists every
`*.2dm` file sitting next to the project `.inp`, plus a synthetic
**(Inline mesh — embedded in project .inp)** row when the deck itself carries
`[2D_VERTICES]` and `[2D_TRIANGLES]`. Above the list is the search directory;
below it is the active reference read from `[2D_MESH_FILE]`.

| Button | What it does |
| --- | --- |
| **Set Active** | Patches `[2D_MESH_FILE]` to point at the selected configuration (or removes the reference when the inline row is chosen) |
| **Remove** | Deletes the selected `.2dm` from disk |
| **Import…** | Browses for a `.2dm` anywhere on disk, copies it into the project folder and loads it as the active mesh; needs a project window |
| **Refresh** | Re-scans the directory and re-reads the reference |

New meshes are created with **Generate Mesh** — see \ref manual_2d_mesh.

### 2D Surface Routing

This page exists only in a build compiled with the 2D module, and the sidebar
row is selectable only while **2D Surface Routing** is checked on *Models /
Processes*. Every key here is stored in `[2D_OPTIONS]`. Where the project has no
value for a key, the page shows the corresponding **2D Defaults** from
\ref manual_preferences — the same value **File → New** would synthesise.

\figtodo{18_2d_options.png, The 2D Surface Routing page with the explicit marcher and coupling groups}

**Time stepping**

| Control | Meaning | Writes |
| --- | --- | --- |
| **Max timestep** | Upper bound on the marcher's CFL substeps and on the 1D↔2D co-advance batch | `MAX_TIMESTEP` |

**Explicit marcher** — the local-inertial marcher is the only 2D integrator in
this engine, so there is no solver selector and these settings are always live.

| Control | Meaning | Writes |
| --- | --- | --- |
| **Momentum θ** | θ-weighting of the face discharge in the momentum update; 1.0 is the classic Bates scheme, below 1 damps thin-film checkerboarding | `THETA` |
| **CFL number** | Courant safety factor α on each cell's stable step | `CFL_NUMBER` |
| **LTS tiers** | Local-timestepping tiers; cells march at power-of-two multiples of the finest step. 1 = a global timestep | `LTS_TIERS` |
| **Movement threshold** | Cells shallower than this stay in the lazy source-only set — rain over thin films costs nothing until water must move | `H_MOVE` |
| **Max Froude number** | Froude cap on face discharge — the supercritical guard | `FROUDE_MAX` |
| **Convective momentum flux (ADVECTION)** | Include the convective momentum flux at interior faces (Stelling–Duinmeijer staggered upwind) | `ADVECTION` |

**Performance**

| Control | Options | Writes |
| --- | --- | --- |
| **Backend** | Auto / CPU (built-in marcher) / OpenMP (Kokkos) / CUDA / HIP / SYCL | `BACKEND` |

Auto prefers an installed GPU plugin above the device mesh-size floor, then the
OpenMP plugin above its own floor, else the built-in CPU marcher. A named
backend loads that plugin outright; if it is missing or has no usable device the
run falls back to CPU with a notice. The `OPENSWMM_2D_BACKEND` environment
variable overrides this setting.

**Mesh**

| Control | Meaning | Writes |
| --- | --- | --- |
| **Dry depth threshold** | Depth below which a cell is treated as dry, m | `DRY_DEPTH` |
| **Limiter epsilon** | Limiter regularisation | `LIMITER_EPSILON` |
| **Flux head epsilon** | Head-difference regularisation for the diffusive-wave flux, m | `FLUX_DH_EPS` |

**Cell closure (wetting / drying)**

| Control | Options | Writes |
| --- | --- | --- |
| **Cell closure** | Flat (η = z̄ + V/A, legacy) or VFR (planar-bed volume/free-surface) | `CELL_CLOSURE` |
| **Face reconstruction** | Mean (upwind cell depth, legacy) or VFR face (edge depth + wetting gate) | `FACE_RECONSTRUCTION` |
| **VFR min wet fraction** | Wetted-area-fraction floor that regularises the VFR closure as a cell dries; used only under VFR | `VFR_MIN_WET_FRAC` |

Flat overstates the surface on sloped and stepped cells, which lets water climb
uphill and strand on slopes; VFR uses the exact planar-bed relation so a lake at
rest stays at rest. The VFR face reconstruction blocks flow across an edge whose
bed is above the water and pairs naturally with the VFR cell closure. Both work
on every backend.

**1D ↔ 2D coupling**

| Control | Meaning | Writes |
| --- | --- | --- |
| **Coupling Cd** | Discharge coefficient at coupling points | `COUPLING_CD` |
| **Exchange interval** | How often exchange volumes are settled; the special value reads **Every routing step** (0) and the batching interval is clamped to [routing step, 60 s] | `COUPLING_SYNC` |
| **Derive exchange areas automatically (COUPLING_AREA AUTO)** | Overrides every coupling point's exchange area with 1.25 × the largest connected conduit area, clamped 0.05–2 m²; explicit `AREA` values in the input are replaced while this is on | `COUPLING_AREA` |

Coupling every routing step gives the tightest feedback and is what
fill-and-spill ponds behind weirs and culverts need; a batching interval is much
faster on large meshes but its one-span feedback delay can ring on rapidly
filling ponds.

**Rainfall**

| Control | Options | Writes |
| --- | --- | --- |
| **Rainfall mode** | Natural neighbour (all gages) / System (uniform gage mean) / None (no direct rainfall) | `RAINFALL_MODE` |

Natural neighbour spatially interpolates every located gage onto each cell
(inverse-distance outside the gage hull). See \ref manual_2d_mesh for assigning
gages to cells.

**Output**

| Control | Meaning | Writes |
| --- | --- | --- |
| **Write 2D results to output (REPORT_2D)** | Emit 2D results at all | `REPORT_2D` |
| **2D results file** | HDF5 file receiving 2D results; relative paths resolve against the input file's folder. Blank uses `<model>.2d.h5` next to the input | `OUTPUT_FILE` |

**Retired keys.** `MIN_TIMESTEP`, `REL_TOLERANCE`, `ABS_TOLERANCE`,
`MAX_CVODE_STEPS`, `MAX_KRYLOV_DIM`, `LINEAR_SOLVER`, `PRECONDITIONER`,
`JACOBIAN`, `ATOL_AREA_REF`, `COUPLING_INTERVAL`, `COUPLING_WINDOW`,
`ACTIVE_SET`, `ACTIVE_SET_HALO` and `MOMENTUM` configured the deleted
CVODE/ARKODE 2D stack. A deck that still carries them opens with a warning and
the keys are ignored; nothing on this page writes them. Older manuals and
tutorials that mention a CVODE solver, linear solver or preconditioner for the
2D module are describing a solver that no longer exists.

### Files / Output / Plugins

This page has three nested sub-tabs so the unrelated concerns do not compete for
vertical space.

\figtodo{18_files_subtab.png, The Files sub-tab with the secondary file references and the scheduled hot-start saves}

**Files sub-tab.** *Secondary file references (.inp [FILES] section)* — each row
is a path picker showing the path relative to the project directory, with a
browse button and, where applicable, a mode combo:

| Row | Mode combo | Writes |
| --- | --- | --- |
| **Rainfall** | (off) / USE / SAVE | `[FILES] RAINFALL` |
| **Runoff** | (off) / USE / SAVE | `[FILES] RUNOFF` |
| **RDII** | (off) / USE / SAVE | `[FILES] RDII` |
| **Inflows (USE only)** | — | `[FILES] USE INFLOWS` |
| **Outflows (SAVE only)** | — | `[FILES] SAVE OUTFLOWS` |
| **Hot-start file (USE)** | — | `[FILES] USE HOTSTART` |

Leave a row blank to omit it from `[FILES]`.

*Scheduled hot-start saves (.inp [FILES] SAVE HOTSTART)* — an uncapped table,
one row per save, with **Path** and **Datetime** columns and **Add…**,
**Browse…**, **Remove**, **Move up** and **Move down**. Leave the Datetime cell
as *(end of run)* to write at the end of the simulation; otherwise the engine
writes when the simulation clock crosses that time. Both cells are always-open
editors, so no click-to-edit is needed.

\figtodo{18_output_subtab.png, The Output sub-tab with writer combos and the report contents group}

**Output sub-tab.** *Writer / Container* selects which plugin drives each of the
three writer roles:

| Control | What it does |
| --- | --- |
| **Input writer** | Plugin that writes the model file; empty id = the built-in `.inp` writer |
| **Output writer** | Plugin that writes results; empty id = the built-in `.out` writer |
| **Report writer** | Plugin that writes the report; empty id = the built-in `.rpt` writer |
| **Single container (write input, output, and report to one file)** | Enabled only when the chosen input-writer plugin advertises all three roles for one extension (a GeoPackage, for instance); checking it locks the other two combos to that plugin |

Picking a non-default entry adds the matching `[PLUGINS]` row on Apply.

*Report contents ([REPORT])* — what the engine prints to the `.rpt` text file.
This is independent of the **Reporting step** on *Dates & Times*, which controls
how often rows are written to the binary output.

| Check box | Writes |
| --- | --- |
| **Disable all reporting (DISABLED)** | `RPT_DISABLED` — greys out the five flags and the three selectors below, because none of them matter then |
| **Echo input summary (INPUT)** | `RPT_INPUT` |
| **Continuity errors (CONTINUITY)** | `RPT_CONTINUITY` — on by default |
| **Flow statistics (FLOWSTATS)** | `RPT_FLOWSTATS` — on by default |
| **Control rule actions (CONTROLS)** | `RPT_CONTROLS` |
| **Time-averaged results (AVERAGES)** | `RPT_AVERAGES` |
| **Report signed piezometric heads (sub-atmospheric)** | `REPORT_SIGNED_HEADS` — an `[OPTIONS]` key that shapes the `.out`, not the `.rpt`, so it stays live even when reporting is disabled |

Three element selectors — **Subcatchments**, **Nodes** and **Links** — each a
**None** / **All** / **Selected:** radio group plus a name list that is enabled
only under **Selected:**. The list accepts comma- or space-separated names and
round-trips comma-separated; an empty list collapses to `NONE`. They write
`RPT_SUBCATCHMENTS`, `RPT_NODES` and `RPT_LINKS`.

*Report file* and *Results output file* override the per-project `.rpt` and
`.out` paths. Leaving either blank derives the path from the input file (same
folder, matching extension). The **Browse…** filters follow the extension the
selected writer plugin advertises.

\figtodo{18_plugins_subtab.png, The Plugins sub-tab editing the model's PLUGINS section}

**Plugins sub-tab.** A two-column table over the model's `[PLUGINS]` section:

| Column | Contents |
| --- | --- |
| Plugin | A plugin id from the discovery registry, an `id:version` pin, or a shared-library path |
| Arguments | Free-form whitespace-tokenised arguments passed to the plugin's initialisation |

**Add** appends a blank row; **Remove** drops the current one. A row naming a
plugin that is statically linked into this build is annotated as such. The first
input-capable row is also what **File → Save As** uses when you pick a
non-`.inp` extension. See \ref manual_plugins.

### Apply, OK and Cancel

- **Apply** validates, writes only the keys that changed, then re-reads them so
  the dialog shows whatever the engine actually accepted. The dialog stays open.
- **OK** does the same and closes.
- **Cancel** discards pending edits; the engine is untouched.

Validation runs before either write:

- **Blocking** — an `[EVENTS]` row with Start ≥ End, a plugin row with an empty
  plugin id, or a hot-start save row with an empty path. Apply and OK refuse
  until the highlighted row is fixed.
- **Non-blocking** — overlapping or out-of-window events, a `[REPORT]` selector
  set to **Selected** with an empty list (it will be written as `NONE`), or a
  `.rpt` / `.out` override whose parent directory does not exist. These appear
  as a Yes/No prompt so you can override deliberately.

An Apply or OK that wrote at least one key marks the project dirty.

## Tips and gotchas

- **Only changed keys are written.** Apply without touching anything writes
  nothing and does not dirty the project.
- **The dialog re-reads after every Apply.** If the engine clamped or normalised
  a value, the control shows the clamped value — that is the engine's answer,
  not a display bug.
- **Active processes are inverted.** A *checked* box means the process runs; the
  `.inp` still carries the legacy `IGNORE_*` key.
- **Percent versus fraction.** The lateral and system flow tolerances are typed
  and read as percentages; the engine stores fractions and the dialog converts.
- **FV keys survive a router change.** They are only written under
  `FLOW_ROUTING FV`, but the dialog keeps them, so switching to dynamic wave and
  back does not lose your setup.
- **The 2D page is defaults-aware.** Keys the project never set show your **2D
  Defaults** from Preferences, not the engine's built-ins.
- **Water age needs its transport engine.** Ticking **Track water age** without
  selecting a transport solver that carries it produces a run with no age.
- **The Spatial page's buttons are inert in this build.** Use the status-bar CRS
  control instead.
- **The 2D CVODE knobs are gone.** If you are following an older document that
  tells you to tune a linear solver or preconditioner for the 2D module, ignore
  it — those keys are retired and only produce a warning.
- **`Ctrl+,` is Preferences, not this dialog.** Application-wide settings live in
  \ref manual_preferences; everything here belongs to the model.

## Related

- \ref manual_projects — projects, the `.oswp` sidecar and portability
- \ref manual_preferences — application settings and the 2D defaults this page falls back to
- \ref manual_crs — coordinate reference systems and reprojection
- \ref manual_hydrology — the process models this page selects
- \ref manual_hydraulics — the routing model and the objects it solves
- \ref manual_water_quality — pollutants, reaction systems, water age and heat
- \ref manual_2d_mesh — generating the mesh this page activates
- \ref manual_running — running a simulation and reading its status and report
- \ref manual_results — the output file the report options shape
- \ref manual_plugins — engine and GUI plugins
- \ref manual_performance — threads, backends and large-model tuning
- \ref manual_file_formats — the files referenced on the Files sub-tab
