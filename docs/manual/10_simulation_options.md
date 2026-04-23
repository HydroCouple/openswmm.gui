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

### Tab 7 — Files / Output / Plugins  *(deferred)*

Planned content: external rain-file list, output-format selector
(native `.out` / GeoPackage / Plugin), `[PLUGINS]` editor with discovered
libraries from the IO Plugin Registry, and the `[REPORT]` controls.

This tab is not yet shipped — it depends on engine C-ABI surfaces that
aren't exposed yet (one batch each for `[FILES]`, `[REPORT]`,
`[PLUGINS]`, and runtime output-backend selection). See the
*Implementation Progress* section of the implementation plan for the
exact gap list and roadmap.

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
- The Files / Output / Plugins tab (Tab 7) is planned but **not yet
  shipped** because the engine doesn't expose the necessary C-ABI
  surface for the `[FILES]`, `[REPORT]`, `[PLUGINS]` sections, or the
  runtime output-format selection. See the implementation plan's
  Slice G-3 follow-up for the gap list.

## Related

- [04 — Coordinate Reference Systems](04_crs.md) — the CRS option lives
  with Phase 0.7's CRS-change prompt today; it'll move into Tab 5
  (Spatial & CRS) of this dialog in a future slice.
