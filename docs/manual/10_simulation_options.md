# 10 — Running Simulations: Simulation Options

## What you'll do

Edit the SWMM engine's `[OPTIONS]` for the active project — process
models, ignore-flags, simulation dates / times, and time steps.

## Where to find it

`Tools → Simulation Options…` while a SWMM project is the active tab.
Disabled when no project is open.

## Step-by-step

The dialog is a tabbed editor (a 7-tab layout is planned; the first cut
ships Tabs 1–2 and the rest land in subsequent slices).

### Tab 1 — Models / Processes

| Control | Engine option | Notes |
|---------|---------------|-------|
| **Infiltration model** | `INFILTRATION` | Horton / ModHorton / Green-Ampt / ModGreenAmpt / CurveNumber. |
| **Flow routing** | `FLOW_ROUTING`  | Steady / Kinwave / Dynwave. |
| **Allow ponding** | `ALLOW_PONDING` | When checked, surcharged junctions accumulate ponded volume. |
| **Skip steady-periods** | `SKIP_STEADY_STATE` | Speeds up simulations dominated by long dry periods. |
| **Ignore Rainfall** | `IGNORE_RAINFALL` | Skip rainfall ingest entirely. |
| **Ignore Snowmelt** | `IGNORE_SNOWMELT` | Skip the snow module. |
| **Ignore Groundwater** | `IGNORE_GROUNDWATER` | Skip the groundwater module. |
| **Ignore RDII** | `IGNORE_RDII` | Skip rainfall-derived inflow / infiltration. |
| **Ignore water quality** | `IGNORE_QUALITY` | Skip the water-quality engine. |
| **Ignore routing** | `IGNORE_ROUTING` | Skip flow routing entirely (hydrology only). |

### Tab 2 — Dates & Times

| Control | Engine option(s) | Notes |
|---------|------------------|-------|
| **Start** | `START_DATE` + `START_TIME` | Simulation start in MM/DD/YYYY + HH:MM:SS. |
| **End** | `END_DATE` + `END_TIME` | Simulation end. |
| **Report start** | `REPORT_START_DATE` + `REPORT_START_TIME` | When to begin writing results. |
| **Reporting step** | `REPORT_STEP` | Seconds between rows in the output file. |
| **Dry-weather step** | `DRY_STEP` | Hydrologic step during dry periods. |
| **Wet-weather step** | `WET_STEP` | Hydrologic step during wet periods. |
| **Routing step** | `ROUTING_STEP` | Hydraulic routing step (seconds, decimals allowed). |
| **Antecedent dry days** | `DRY_DAYS` | Days of dry weather assumed before `START_DATE`. |

#### Events ([EVENTS] section)

The **Events** group at the bottom of the Dates tab edits the SWMM
`[EVENTS]` block — an optional list of routing-active time windows.
When **Skip steady-periods** is on the engine routes full dynamic-wave
hydraulics only inside these windows and steps quickly through the dry
periods between them.

The table has two columns, **Start** and **End**. Each cell uses an
in-place date-time picker with a calendar popup (`MM/DD/YYYY HH:MM` —
the same resolution the legacy SWMM 5 dialog uses).

| Action | How |
|--------|-----|
| **Add row** | Click **Add row**. The new row is pre-filled with the simulation Start and End so you only edit the deltas. |
| **Remove row** | Select one or more rows (click row headers, shift- or ctrl-click for multi-select) and click **Remove selected**. |
| **Edit a time** | Click the cell and use the spin buttons, type directly, or drop the calendar popup. |

Validation rules:

- **Start < End** is required per row. Rows that violate this are
  highlighted pink; Apply / OK are blocked until they are fixed.
- **Overlapping rows** and rows **outside the simulation window**
  trigger a non-blocking warning on Apply / OK; you can choose to
  proceed if intentional.

The engine round-trips every row through `swmm_events_*`; on Save As
the `[EVENTS]` block is regenerated in the output `.inp` in legacy
SWMM 5.2 column order.

### Tab 3 — Routing & Hydraulics

Three groups: **Surcharge handling**, **Solver**, and **Conduit / channel**.

**Surcharge handling**

| Control | Engine option | Notes |
|---------|---------------|-------|
| **Method** | `SURCHARGE_METHOD` | EXTRAN (legacy) / SLOT (Preissmann) / DYNAMIC_SLOT. The next three rows enable only when DYNAMIC_SLOT is selected. |
| **DPS celerity** | `DPS_CELERITY` | Target wave celerity for DYNAMIC_SLOT (m/s). |
| **DPS alpha** | `DPS_ALPHA` | Alpha exponent (≥ 2). |
| **DPS decay** | `DPS_DECAY_TIME` | Decay time (s). |

**Solver**

| Control | Engine option | Notes |
|---------|---------------|-------|
| **Node continuity** | `NODE_CONTINUITY` | EXPLICIT (legacy) or SEMI_IMPLICIT (new — better for low-slope networks). |
| **Anderson acceleration** | `ANDERSON_ACCEL` | Typical 25–50% iteration reduction; cheap to enable. |
| **Max trials** | `MAX_TRIALS` | Iteration cap per routing step. |
| **Head tolerance** | `HEAD_TOLERANCE` | Convergence tolerance on node head. |
| **Lateral flow tol** | `LAT_FLOW_TOL` | Shown as percent; engine stores fraction (the dialog converts). |
| **System flow tol** | `SYS_FLOW_TOL` | Same percent ↔ fraction conversion. |
| **Lengthening step** | `LENGTHENING_STEP` | Conduit lengthening time step (s). |
| **Variable step factor** | `VARIABLE_STEP` | Courant-number safety fraction (0 disables). |

**Conduit / channel**

| Control | Engine option | Notes |
|---------|---------------|-------|
| **Force-main equation** | `FORCE_MAIN_EQUATION` | Hazen-Williams (H-W) or Darcy-Weisbach (D-W). |
| **Normal-flow criterion** | `NORMAL_FLOW_LIMITED` | Slope / Froude / Both / Neither. |
| **Inertial damping** | `INERTIAL_DAMPING` | None / Partial / Full — applies only to dynamic-wave routing. |
| **Min surface area** | `MIN_SURFAREA` | Lower clamp on nodal surface area. |
| **Min conduit slope** | `MIN_SLOPE` | Lower clamp on conduit slope (percent). |

### Tab 4 — System / Performance

| Control | Engine option | Notes |
|---------|---------------|-------|
| **Worker threads** | `THREADS` | OpenMP team size. **0 = auto** (engine picks based on conduit count); 1 = serial; higher values cap the team. |

The `IGNORE_*` skip-process flags live on the *Models / Processes* tab —
not here — because they're conceptually about *which* models you run, not
*how fast* you run them.

### Tab 5 — Spatial & CRS

| Control | Engine option | Notes |
|---------|---------------|-------|
| **Layer CRS** | `CRS` (read via `swmm_spatial_get_crs`) + the layer's stored SRS | Read-only label showing the layer's current authority code (e.g. `EPSG:6595`). |
| **Change…** | writes `CRS` + `layer->setSRS()` | Opens the same CRS picker the canvas uses. Picking a CRS here updates the *layer's stored CRS* and the engine's `[OPTIONS] CRS` row — it does **not** transform stored coordinates. |
| **Detect from coordinates** | (heuristic) | Inspects the model extent. If all coordinates fit within ±180° lon and ±85° lat, suggests EPSG:4326. Otherwise hints that a projected CRS is needed. Non-destructive — you must press **Change…** to apply the suggestion. |
| **Model extent** | (read-only) | The layer's full extent in its stored CRS, formatted as `X: [min, max]   Y: [min, max]`. |

For permanent coordinate **reprojection** (rewriting every node /
link / subcatchment coordinate via OGR), use the **canvas-CRS button**
in the status bar — that's the Phase 0.7 reproject prompt.

### Tab 6 — 2D Surface Routing  *(only when 2D is compiled in)*

This tab appears only when `OPENSWMM_BUILD_2D=ON`. With the default
build it is hidden. Every key on this tab is read/written via
`swmm_options_get_ext` / `_set_ext` (the engine routes 2D keys through
the extension-options map).

**CVODE solver**

| Control | Engine option |
|---------|---------------|
| Max timestep | `MAX_TIMESTEP` (s) |
| Min timestep | `MIN_TIMESTEP` (s) |
| Relative tolerance | `REL_TOLERANCE` |
| Absolute tolerance | `ABS_TOLERANCE` |
| Max CVODE steps | `MAX_CVODE_STEPS` |

**Mesh**

| Control | Engine option |
|---------|---------------|
| Dry depth threshold | `DRY_DEPTH` (m) |
| Limiter epsilon | `LIMITER_EPSILON` |

**1D ↔ 2D coupling**

| Control | Engine option |
|---------|---------------|
| Coupling Cd | `COUPLING_CD` |
| Coupling interval | `COUPLING_INTERVAL` (s; 0 = every step) |

**Linear solver**

| Control | Engine option |
|---------|---------------|
| Solver | `LINEAR_SOLVER` (GMRES / BICGSTAB / TFQMR) |
| Preconditioner | `PRECONDITIONER` (NONE / JACOBI / ILU) |
| Max Krylov dim | `MAX_KRYLOV_DIM` |

A **Write 2D results** checkbox toggles `REPORT_2D`.

### Tab 7 — Files / Output / Plugins

Tab 7 is organised as **three nested sub-tabs** so the unrelated concerns
(secondary file references, output-format and report controls, and the
plugin editor) don't fight for vertical space in one giant page:

- **Files** — secondary `[FILES]` references (rainfall / runoff / RDII /
  inflows / outflows / hot-start USE) plus the multi-row
  **Scheduled hot-start saves** table (Slice BV-01).
- **Output** — writer / container combos, the `[REPORT]` flag and
  selector group, and the override paths for the `.rpt` and `.out`
  files.
- **Plugins** — the raw `[PLUGINS]` table (plugin id / path / `id:version`
  + free-form argument string per row).

The dialog round-trips every value through the engine's existing C API
(`swmm_options_get` / `swmm_options_set` for the `RPT_*` keys and the
`[FILES]` paths, `swmm_plugins_*` for the plugin table, and
`swmm_hotstart_saves_*` for the scheduled-saves table). The legacy
EPA SWMM 5.x `[REPORT]` and `[FILES]` semantics are preserved on
Save As.

#### Files sub-tab

The **Secondary file references** group edits the `[FILES]` block:

| Row | Engine key | Mode combo |
|-----|------------|------------|
| Rainfall | `RAINFALL` | off / `USE` / `SAVE` |
| Runoff   | `RUNOFF`   | off / `USE` / `SAVE` |
| RDII     | `RDII`     | off / `USE` / `SAVE` |
| Inflows (USE only)  | `INFLOWS`  | (always USE when set) |
| Outflows (SAVE only)| `OUTFLOWS` | (always SAVE when set) |
| Hot-start file (USE)| `HOTSTART USE` | (USE only) |

All paths are stored **relative to the `.inp` directory** at save time.
Leave a row blank to omit it from `[FILES]`.

The **Scheduled hot-start saves** table is uncapped — add as many rows
as you need, each with a save-as path (relative to the `.inp`
directory) and an optional sim-time datetime. Leave the **Datetime**
cell as *(end of run)* to write the hot-start file at the end of the
simulation; otherwise the engine writes when the sim clock crosses the
chosen datetime. Backed by the `swmm_hotstart_saves_*` engine C API.

#### Output sub-tab

The **Writer / Container** group selects which plugin drives each of
the three writer roles (input / output / report). The combo's hidden
data is the plugin id — an empty id means "use the built-in `.inp` /
`.out` / `.rpt` writer". Picking a non-default entry adds the
corresponding `[PLUGINS]` row on Apply.

The **Single container** check box is enabled only when the selected
input writer plugin advertises all three roles (`INPUT_READ`,
`OUTPUT_WRITE`, `REPORT_WRITE`) for the same file extension (e.g.
GeoPackage). Checking it locks the Output and Report combos to the
input writer's plugin id.

The **Report file** and **Results output file** groups expose
overrides for the per-project `.rpt` and `.out` paths. Leaving either
blank derives the path automatically from the input file location
(sibling with `.rpt` / `.out` extension). The Browse… buttons use the
filter advertised by the matching writer combo so you only see
extensions the chosen plugin can write.

#### Plugins sub-tab

A free-form two-column table editor for the model's `[PLUGINS]`
section. **Add** appends a blank row and puts focus on the first
column; **Remove** drops the current row. Each row's columns are:

| Column | Contents |
|--------|----------|
| Plugin (path / id / id:version) | Either a plugin id from the discovery registry, an `id:version` pin, or a shared-library path. |
| Arguments | Free-form whitespace-tokenised arguments passed to the plugin's `initialize()` call. |

The first input-capable row is also used by **File → Save As** when
picking a non-`.inp` extension.

#### Validation (Phase 3.10.4)

Apply and OK run a validation pass before writing to the engine:

- **Blocking** — a plugin row with an empty plugin id, or a
  hot-start save row with an empty path, is highlighted in red and
  Apply / OK refuses to proceed until the row is fixed.
- **Non-blocking warnings** — surfaced as a Yes / No prompt so you
  can override when you mean it:
  - A `[REPORT]` selector with **Selected** chosen but the name list
    empty will silently be written as `NONE`.
  - A `.rpt` or `.out` override path whose parent directory does not
    exist on disk.

#### Report contents ([REPORT] section)

The **Report contents** group on Tab 7 edits the SWMM `[REPORT]` block —
the keys that decide *what* the engine prints to the `.rpt` text file.
This is independent of the **Reporting step** on Tab 2 (which controls
*how often* time-series rows are emitted to the binary output file).

The group has two halves: a set of flag check-boxes and three element
selectors.

**Flags**

| Check-box | Engine key | Notes |
|-----------|------------|-------|
| **Disable report file** | `RPT_DISABLED` | When checked, the engine skips writing the `.rpt` text file entirely. The five flag check-boxes and the three selectors below are greyed out because none of them have any effect when the file is disabled. |
| **Echo input summary** | `RPT_INPUT` | Adds the "Input Summary" tables at the top of the `.rpt`. |
| **Continuity report** | `RPT_CONTINUITY` | Mass-balance summary at the end of the `.rpt`. Default on. |
| **Flow statistics** | `RPT_FLOWSTATS` | Per-link flow / depth / velocity statistics table. Default on. |
| **Controls report** | `RPT_CONTROLS` | Action log for `[CONTROLS]` rules. |
| **Time-step averages** | `RPT_AVERAGES` | Adds the time-step averaging summary block. |

**Element selectors**

Each of the three selectors below (subcatchments, nodes, links) is a
three-radio group plus a comma-separated name list. The list edit is
enabled only when **Selected** is chosen.

| Radio | Engine value | Effect |
|-------|--------------|--------|
| **None** | `NONE` | Suppresses the per-element time-series table for that element type. |
| **All** | `ALL` (default) | Reports every element of that type. |
| **Selected** | comma-separated name list | Reports only the named elements (e.g. `J1,J2,J3`). The setter also accepts space-separated input — `J1 J2 J3` becomes `J1,J2,J3` on round-trip. An empty list collapses to `NONE`. |

| Selector | Engine key |
|----------|------------|
| **Subcatchments** | `RPT_SUBCATCHMENTS` |
| **Nodes** | `RPT_NODES` |
| **Links** | `RPT_LINKS` |

On Save As the engine emits the `[REPORT]` block in legacy SWMM 5.2
column order. The nine keys round-trip through the existing
`swmm_options_get` / `swmm_options_set` C API (the `RPT_*` key family
was added with Slice BV.1).

### Apply vs OK vs Cancel

- **Apply** writes any changed keys back to the engine immediately and
  re-reads them so the dialog reflects whatever the engine actually
  accepted (some keys may be clamped or normalised). Stays open.
- **OK** does the same and closes.
- **Cancel** discards pending edits — the engine is untouched.

A successful Apply / OK that wrote at least one key marks the project
**dirty** (the title gains a `*`); the changes persist on the next Save.

## Tips and gotchas

- The dialog **only writes keys that actually changed.** If you Apply
  without touching anything, nothing is written and the project is not
  marked dirty.
- Date/time entry uses your locale's calendar popup but the engine
  always sees MM/DD/YYYY + HH:MM:SS — the dialog handles the
  conversion.
- Some engine options have caps that the engine enforces silently. After
  Apply, the dialog re-reads from the engine, so if you typed
  `REPORT_STEP = 999999` and the engine clamped it, the dialog will show
  the clamped value.
- The DPS_* rows (DPS celerity / alpha / decay time) on Tab 3 are
  greyed out unless **Method** is **DYNAMIC_SLOT**.
- The Lateral / System flow tolerances on Tab 3 are shown as **percent**
  but the engine stores them as **fractions**; the dialog converts both
  ways so you only ever type / read percent.
- The 2D Surface Routing tab is **only visible when the engine is built
  with `OPENSWMM_BUILD_2D=ON`**. With the default build the tab is
  hidden — there's no point editing 2D options that the engine wouldn't
  read.
- The Files / Output / Plugins tab (Tab 7) is split into three
  **nested sub-tabs** — Files, Output, Plugins — to keep the page
  scannable. Each sub-tab edits a distinct engine surface
  (`[FILES]` + `swmm_hotstart_saves_*`, writer combos + `[REPORT]` +
  rpt/out path overrides, and the raw `[PLUGINS]` table respectively).
- The dialog re-reads every `[OPTIONS]` key from the active project's
  engine each time it opens, so the controls always reflect the current
  INP, not a snapshot. See `docs/GUI_IMPLEMENTATION_PLAN.md` §M for the
  formal hydration contract that this rule belongs to.

## Related

- [04 — Coordinate Reference Systems](04_crs.md) — the CRS option lives
  with Phase 0.7's CRS-change prompt today; it'll move into Tab 5
  (Spatial & CRS) of this dialog in a future slice.
