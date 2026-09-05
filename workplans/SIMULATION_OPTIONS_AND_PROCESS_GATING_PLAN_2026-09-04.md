# Simulation Options restructure, cross-domain process gating, 2D process enables, and MSX parity — plan of attack

Date: 2026-09-04
Repos: `openswmm.gui` (dialog, editors), `openswmm.engine` (option keys, gating semantics, species resolver, inflow/IQ parity)
Status: **PLAN FOR REVIEW — nothing implemented.** Findings verified against source on 2026-09-04 (file:line refs below). Decisions the user must make are collected in §9; the phasing in §8 assumes the recommended answers.

Related plans this one builds on and does not duplicate: `openswmm.engine/plans/transport/UNIFIED_TRANSPORT_MASTER_PLAN.md` (SpeciesRegistry §4.1), `OVERLAND_TRANSPORT_HEAT_MSX_PLAN_2026-09-01.md` + `S4_OVERLAND_TRANSPORT_HANDOFF_2026-09-02.md` (2D species rows), `MULTISPECIES_REACTIONS_MSX_PLAN.md`, `AMENDMENT_1_WATER_AGE_AS_SPECIES_2026-08-23.md` (the precedent for a non-pollutant `[INFLOWS]` constituent), `TWO_ZONE_GROUNDWATER_EXPLICIT_LTS_PLAN_2026-08-15.md` (track I infiltration, G1 groundwater), `openswmm.gui/workplans/INTEGRATED2D_GW_GUI_PLAN_2026-08-15.md` (§1 already sketches the 2D Groundwater / Infiltration groups).

---

## 0. The four asks, restated as design problems

| # | Ask | Underlying problem |
|---|---|---|
| A | Break the 1D hydraulics and 2D pages into coherent tabs, enabling/disabling as needed | The dialog has one page-gating primitive (`set2DRowEnabled`, sidebar-item flags) and six unrelated widget-gating mechanisms; the selector that decides which solver family applies (`FLOW_ROUTING`) lives on a different page from the ~35 widgets it governs. |
| B | Expose 2D process enables (infiltration, …) | 2D processes are enabled by **presence of rows**, not by keys; the dialog exposes exactly the `[2D_OPTIONS]` key set and none of the 2D sections (`[2D_INFILTRATION*]`, `[2D_INITIAL_QUALITY]`, `[2D_BOUNDARY_QUALITY]`). |
| C | Delineate how process formulations initiate across 1D / groundwater / 2D — does 1D quality imply 2D quality? MSX? | Today it is implicit and asymmetric: 2D transport is unconditional (mesh + pollutants + `IGNORE_QUALITY NO` ⇒ on); groundwater has no transported quality at all; MSX runs in all three 1D solvers and 2D but not runoff/LID/GW; there is no per-domain opt-out anywhere. |
| D | MSX species parity with pollutants for initial concentrations (multiple sources) and CONCEN / MASS inflows | Four independent species-name resolvers; `[INFLOWS]` and `[INITIAL_QUALITY]` refuse MSX names while `[2D_INITIAL_QUALITY]` / `[2D_BOUNDARY_QUALITY]` accept them; MSX initial values live only in `[REACTION_QUALITY]`; `.ard` `[TRANSPORT_BOUNDARIES]`/`[TRANSPORT_SOURCES]` are MSX-only **and** EULERIAN_ARD-only. |
| E (added) | Enable infiltration and evapotranspiration for the 2D groundwater model | The 2D groundwater kernel (integrated2d two-zone, G1) is **not implemented**; `Infil2D` has reserved destinations `SUBCATCH_AQUIFER`/`AQUIFER_2D`; 2D evaporation is unconditional from the climate rate and has no key. |

---

## 1. Findings (current state)

### 1.1 Dialog (`openswmm.gui/src/ui/dialogs/simulationoptionsdialog.cpp`)
Settings-style `QListWidget` + `QStackedWidget` (`buildUi` :355-421, `addCategory` :423). Ten sidebar rows. The two crowded pages:

- **Routing & Hydraulics** (:821-1177): 6 groups / ~35 controls across three mutually exclusive solver families — Surcharge (`SURCHARGE_METHOD`, `DPS_*`, `TPA_CELERITY`), Solver (`NODE_CONTINUITY`, `ANDERSON_ACCEL`, `MAX_TRIALS`, `HEAD_TOLERANCE`, `LENGTHENING_STEP`, `VARIABLE_STEP`, `MINIMUM_STEP`), FV solver (11 `FV_*` keys), FV performance (`FV_BACKEND`, `FV_MIN_PARALLEL_CELLS`, `FV_COMPACTION`, `FV_LTS*`, `FV_CFL_CENSUS_INTERVAL`), Unsteady friction, Conduit/channel. FV groups are greyed, not hidden, for a DYNWAVE user (17 dead widgets to scroll past). `FLOW_ROUTING` itself is on the Models page.
- **2D Surface Routing** (:2083-2350): 8 groups in one column — Time stepping, Explicit marcher (θ, CFL, LTS tiers, H_MOVE, Froude, advection), Performance (`BACKEND`), Mesh (`DRY_DEPTH`, `LIMITER_EPSILON`, `FLUX_DH_EPS`), Cell closure (VFR), 1D↔2D coupling, Rainfall (`RAINFALL_MODE`), Output.

Gating today: `applyEngineConstraints` (one-shot capability probe, :168-352), `updateFvFieldsEnabled` (:1330), `updateSurchargeFieldsEnabled` (:1317), `updateQualitySolverFieldsEnabled` (:1533), `m_ufSupported` flag, and the single page-level primitive `set2DRowEnabled` (:429-442) driven by `IGNORE_2D`. `IGNORE_QUALITY` gates **nothing** outside its own checkbox. Keys already split by implementation convenience: `FV_SCALAR_SCHEME` on the Quality page, `LAT/SYS_FLOW_TOL` on Dates, `REPORT_SIGNED_HEADS` on Files/Output, two backend combos (`FV_BACKEND` on Hydraulics, 2D `BACKEND` on the 2D page) with overlapping enums.

### 1.2 Engine process enables (`openswmm.engine`)
- `IGNORE_RAINFALL/SNOWMELT/GROUNDWATER/RDII/ROUTING/QUALITY/2D` — `SimulationOptions.hpp:544-570`, parsed `OptionsHandler.cpp:314-327`. Positive enables: `QUALITY_SOLVER`, `WATER_AGE`, `HEAT_TRANSPORT` (:172-201). Components (MSX, ARD, LARD, heat, age, integrated2d) enable by `[PROCESS_COMPONENTS]` id (`ProcessComponentRegistry.cpp:139-161`).
- **2D transport is automatic**: `SurfaceRouter2D.cpp:709-745` — rows = pollutants (unless `IGNORE_QUALITY`) + MSX (if reactions active and no WALL species) + age + heat. No `[2D_TRANSPORT]` switch, no per-domain opt-out.
- **2D infiltration**: on iff `[2D_INFILTRATION]`/`[2D_INFILTRATION_DEFAULTS]` rows exist (`Infil2D.hpp:164,212,241`); only key `[2D_INFILTRATION_OPTIONS] INFIL_STEP` (`SectionHandlers2D.cpp:762`). Destination `LOST` only; `SUBCATCH_AQUIFER`/`AQUIFER_2D` reserved (`Infil2D.hpp:85-86`).
- **2D evaporation**: unconditional from the climate rate + per-cell forcing (`SurfaceRouter2D.cpp:1052-1061`, sink :1680); no key. Removes volume, never mass.
- **2D rainfall**: `RAINFALL_MODE` only.
- **Groundwater quality**: none transported (legacy parity); pollutant GW concentration column at the node seam (`QualityRouting.cpp:680-715`); age/heat as source volumes; no MSX.
- **Species coverage matrix (what runs where when the parent is on)**

| Domain | Pollutants | MSX | Age | Heat |
|---|---|---|---|---|
| Runoff / LID | yes | **no** | yes | yes |
| Groundwater | seam column only | no | source vol | source vol |
| 1D LEGACY / ARD / LARD | yes | yes (all three) | yes | yes |
| 2D surface | yes | yes (bulk; WALL refused) | yes | yes |
| 2D infiltration sink | LOST ledger | LOST | LOST | LOST |

- `SpeciesRegistry::transported_count()` is documented as the unifier but has **zero callers**; the row layout is re-derived in `ArdEngine::initialize`, `LagrangianSolver`, and `SurfaceTransportState` (`SurfaceRouter2D.cpp:709-745`).

### 1.3 Initial concentrations and inflows
- Pollutants: `[POLLUTANTS] Cinit`; `[INITIAL_QUALITY] NODE|LINK` (`QualityHandler.cpp:406`, applied `SWMMEngine.cpp:7280-7305`, C API `openswmm_initial_quality.h`); `[2D_INITIAL_QUALITY] '*'|TAG|CELL` (`SectionHandlers2D.cpp:825`); `[INFLOWS] CONCEN|MASS` (`Inflow.cpp:150-240`, MASS ÷ 28.317); `[DWF]`; `[RDII]`/GW columns; `[2D_BOUNDARY_QUALITY]`; rain column → `tr.rain_conc`. **Hotstart quality restore is unimplemented for everyone** (`SWMMEngine.cpp:7289`, D-IQ5 placeholder).
- MSX: initial only via `[REACTION_QUALITY] GLOBAL|NODE|LINK` (`ReactionData.hpp:120-131`, C API `openswmm_reactions.h:171-183`); `[2D_INITIAL_QUALITY]`/`[2D_BOUNDARY_QUALITY]` **do** accept MSX names (`SurfaceRouter2D.cpp:786,843`); `[INITIAL_QUALITY]` **refuses** them (`PostParseResolver.cpp:1700-1707`); `[INFLOWS]` cannot name them (`Inflow.cpp:221-227` drops the row); `.ard` `[TRANSPORT_BOUNDARIES]`/`[TRANSPORT_SOURCES]` are MSX-only and ARD-only (`ArdConfig.cpp:320-329, 383-403`). `[DWF]`, `[RDII]`, `[TREATMENT]`, `[LOADINGS]`/`[BUILDUP]`/`[WASHOFF]` are pollutant-only.

---

## 2. Design principle: an explicit Domain × Process × Species contract

Everything below follows one rule, stated once so the dialog, the engine keys and the docs agree:

> **A process is owned by a domain. Species are owned by the project. A domain carries a species class iff (a) the species class is enabled at project level, (b) the domain is enabled, and (c) the domain's transport switch for that class is not explicitly off.** Defaults are *inherit on* (today's behaviour) so existing decks run unchanged; the new keys only add opt-outs and make the implicit explicit.

Domains: **Hydrology** (runoff, snowmelt, LID, RDII), **Groundwater** (legacy per-subcatchment aquifer; later integrated2d GW), **1D network**, **2D surface**. Species classes: **Pollutants**, **MSX species**, **Water age**, **Temperature**.

Consequence for ask C: enabling water quality for 1D **does** initiate it for 2D (and would for GW transport once that exists) — by inheritance — but the user can switch any domain's transport off independently, and the dialog shows the resulting matrix.

---

## 3. Part A — Dialog restructure

### 3.1 Proposed page tree
Keep the settings-style sidebar (it already scales and supports per-row gating), but make it **two-level**: parent rows are domains, child rows are pages. `QListWidget` cannot nest; switch the sidebar to a `QTreeWidget` with the same `addCategory` contract (`addCategory(parentTitle, title, page)`). Gating stays "item flags + redirect current row", now applied to whole parents.

```
General
  ├ Title / Notes
  ├ Dates & Times                     (SKIP_STEADY_STATE + LAT/SYS_FLOW_TOL move to 1D › Dynamic Wave)
  ├ Processes & Modules   ★ NEW HUB   (§3.2 — the matrix; replaces "Models / Processes")
  ├ Spatial & CRS
  └ System / Performance              (THREADS + effective counts; both BACKEND combos surface here read-only
                                       with "edit on the owning page" links)
Hydrology                             (gated: any of rainfall/runoff/snow/GW/RDII on)
  ├ Infiltration & Runoff             (INFILTRATION model — moves off the Models page; ALLOW_PONDING)
  ├ Groundwater                       (gated IGNORE_GROUNDWATER; today only a placeholder + link to the aquifer editor;
                                       grows the integrated2d GW group in Part E)
  └ Snowmelt / RDII                   (links to their editors; IGNORE_* live on the hub)
1D Network                            (gated IGNORE_ROUTING)
  ├ Routing                           (FLOW_ROUTING selector HERE, + Conduit/channel group: FORCE_MAIN_EQUATION,
                                       NORMAL_FLOW_LIMITED, INERTIAL_DAMPING, MIN_SURFAREA, MIN_SLOPE, MINIMUM_STEP,
                                       LENGTHENING_STEP, VARIABLE_STEP — the shared knobs)
  ├ Dynamic Wave                      (gated FLOW_ROUTING==DYNWAVE: Surcharge group, Solver group
                                       (NODE_CONTINUITY, ANDERSON_ACCEL, MAX_TRIALS, HEAD_TOLERANCE), Unsteady friction,
                                       SKIP_STEADY_STATE block)
  ├ Finite Volume                     (gated FLOW_ROUTING==FV: FV solver + FV performance + Unsteady friction + FV_SCALAR_SCHEME
                                       — FV_BACKEND stays here as the owning page)
  └ Kinematic / Steady                (gated KINWAVE|STEADY: today nothing beyond the shared knobs → show a
                                       one-line note; page exists so the tree is complete and future keys have a home)
2D Surface                            (gated IGNORE_2D — the existing set2DRowEnabled semantics)
  ├ Mesh                              (unchanged; never gated — creating a mesh is what turns 2D on)
  ├ Hydrodynamics                     (Time stepping + Explicit marcher + Mesh tolerances + Cell closure)
  ├ Coupling                          (1D↔2D coupling group; the coupled-node summary; later the GW coupling)
  ├ Processes            ★ NEW        (§4: Rainfall mode; Infiltration enable/step/default method/destination +
                                       "Edit per-cell…"; Evaporation enable; Transport per-class switches from the hub,
                                       shown read-only with a link)
  ├ Groundwater          ★ NEW, hidden until the integrated2d component exists (§7)
  └ Performance & Output              (BACKEND, REPORT_2D, OUTPUT_FILE)
Quality & Transport                   (gated: pollutants exist OR age OR heat OR reactions component registered)
  ├ Species & Domains    ★ NEW        (the Domain × Species matrix — §5; QUALITY_SOLVER; OUTFALL_BACKFLOW_QUALITY)
  ├ Solver                            (ARD / LARD groups as today, gated by QUALITY_SOLVER)
  ├ Initial Conditions                (InitialQualityDialog inline or launched; grows MSX rows — §6)
  ├ Reserved Species                  (WATER_AGE, HEAT_TRANSPORT + heat coefficients + WaterAgeSourcesDialog)
  └ Reactions (MSX)                   (gated: reactions component registered — read-only summary of species, link to .rxn;
                                       later the initial/inflow parity editors of §6)
Files / Output / Plugins              (unchanged inner tabs)
```

### 3.2 The Processes & Modules hub (replaces Models / Processes)
One page, three groups, no numeric knobs:

1. **Domains** — checkboxes `1D network` (always on), `Hydrology`, `Groundwater`, `2D surface` → `IGNORE_ROUTING/RAINFALL+…/GROUNDWATER/2D`. Each shows the object-count availability rule already implemented (`readFromEngine :3099-3113`) as a greyed reason ("no aquifers in model").
2. **Processes** — the existing inverted `IGNORE_*` boxes regrouped under their domain headings, plus the new 2D rows from §4 (Infiltration, Evaporation, Rainfall) and Snowmelt/RDII.
3. **Species classes** — `Pollutants` (`IGNORE_QUALITY`), `Water age` (`WATER_AGE`), `Temperature` (`HEAT_TRANSPORT`), `Multispecies reactions` (component registered — read-only here, with "Manage components…" link).

Below the groups: a live **matrix preview** (rows = domains, cols = species classes; ✓ / ✗ / — n/a) computed from the same rule the engine will use (§5.3 exposes it via C API so the GUI never re-implements it).

### 3.3 Gating model (one mechanism, replacing six)
Introduce `PageGate { std::function<bool()> enabled; QString reasonWhenOff; }` per tree row and per group box. A single `refreshGates()` runs after every read, every hub toggle, and every `FLOW_ROUTING` / `QUALITY_SOLVER` change. Gates:

| Row / group | Enabled when |
|---|---|
| Hydrology parent | any hydrology process on |
| Groundwater page | `!IGNORE_GROUNDWATER && aquifer_count > 0` |
| 1D › Dynamic Wave | `FLOW_ROUTING == DYNWAVE` |
| 1D › Finite Volume | `FLOW_ROUTING == FV && fvSupported` |
| 1D › Unsteady friction group (both pages) | `m_ufSupported && routing ∈ {DYNWAVE, FV}` |
| Surcharge sub-groups | as `updateSurchargeFieldsEnabled` today |
| 2D parent | `!IGNORE_2D` (existing semantics, Mesh page exempt) |
| 2D › Processes › Infiltration group | `!IGNORE_2D && (2D GW off ∥ not present)` — mutual exclusion per engine D-I4 |
| Quality parent | pollutants>0 ∥ WATER_AGE ∥ HEAT_TRANSPORT ∥ reactions registered |
| Quality › Solver ARD/LARD groups | as `updateQualitySolverFieldsEnabled` today |
| Quality › Reactions | reactions component registered |

Disabled rows stay visible but greyed with the reason as tooltip (discoverability), except pages for components that are not compiled in (`OPENSWMM_HAS_2D` off), which are hidden. `applyEngineConstraints` becomes the *capability* source for gates rather than a one-shot `setEnabled`, which removes the "sticky child disable" fragility noted at :263.

### 3.4 Migration rules (no data-model change)
Every key keeps its section and spelling; only widget placement changes. `writeToEngine`/`readFromEngine` split into per-page `read()/write()` members registered with the page so the 4,600-line monolith becomes ~12 page classes (`SimOptionsPage` base with `read(engine)`, `write(engine, &nChanged)`, `gate()`). Dirty counting, `optionValueEquals`, `wroteAnyChanges()` and the Apply→re-read cycle are preserved. `FastPreset` moves to the Performance page but writes `MINIMUM_STEP` through the Routing page's object, not directly.

Tests: extend `tests/gui/test_simulationoptionsdialog.cpp` with (a) a round-trip test that every key read on open is written back unchanged when OK is pressed with no edits (`n == 0`), on Example1, a US 2D deck and an FV deck — this pins that the split lost nothing; (b) gate tests: toggle `FLOW_ROUTING` and assert the DW/FV rows flip; toggle the 2D module and assert the parent row; (c) the existing `fastPresetHasBalancedRecipe` etc.

---

## 4. Part B — 2D process enables

### 4.1 Engine keys (all `[2D_OPTIONS]`, all default to today's behaviour)

| Key | Values | Default | Semantics |
|---|---|---|---|
| `INFILTRATION` | `YES\|NO` | `YES` if any `[2D_INFILTRATION*]` rows exist, else `NO` | Explicit switch. `NO` with rows present ⇒ rows kept, `Infil2D::active_=false`, warning. Lets a user turn infiltration off for a run without deleting per-cell data. |
| `INFIL_STEP` | seconds | project step | Move from `[2D_INFILTRATION_OPTIONS]` to `[2D_OPTIONS]` (keep the old section as an accepted alias; writer emits the new place). |
| `INFIL_DEFAULT_METHOD` | `NONE\|HORTON\|MOD_HORTON\|GREEN_AMPT\|MOD_GREEN_AMPT\|CURVE_NUMBER\|CONSTANT` | `NONE` | The `'*'` row of `[2D_INFILTRATION_DEFAULTS]` as a key so the dialog can offer it without the table editor (engine D-I6 already enumerates these). |
| `INFIL_DESTINATION` | `LOST\|SUBCATCH_AQUIFER\|AQUIFER_2D` | `LOST` | Project default for the destination column; `AQUIFER_2D` refused until G1 lands (as now). |
| `EVAPORATION` | `YES\|NO` | `YES` | New switch for the unconditional climate-rate sink (`SurfaceRouter2D.cpp:1052`). `NO` ⇒ zero 2D evaporation, ledger row stays. |
| `RAINFALL_MODE` | existing | existing | Unchanged; joins the Processes page. |
| `TRANSPORT_POLLUTANTS`, `TRANSPORT_MSX`, `TRANSPORT_AGE`, `TRANSPORT_TEMPERATURE` | `YES\|NO` | `YES` | Per-class 2D opt-outs — the 2D column of the §5 matrix. Consumed at `SurfaceRouter2D.cpp:717-730` (each `np/nm/na/nt` term gets `&& opt`). |

Engine changes: `SolverOptions2D` fields + `SectionHandlers2D` parse/`is2DOptionKey`/`options_get_ext` round trip + `InpWriter` (emit only non-default, like `IGNORE_2D`) + `GeoPackage` schema column. Tests: `tests/unit/engine/test_2d_options_*` round trip; an infiltration deck with `INFILTRATION NO` books zero `infil_out`; `EVAPORATION NO` books zero `evap_out`; `TRANSPORT_POLLUTANTS NO` sizes `n_pollut = 0` with age/heat rows intact.

### 4.2 GUI — 2D › Processes page
Groups: **Rainfall** (`RAINFALL_MODE`), **Infiltration** (`INFILTRATION`, `INFIL_STEP`, `INFIL_DEFAULT_METHOD`, `INFIL_DESTINATION`, "Edit per-cell infiltration…" → the GG0 editor already specified in `INTEGRATED2D_GW_GUI_PLAN` §3.2), **Evaporation** (`EVAPORATION`; a read-only line showing the 1D climate source so the user knows where the rate comes from), **Transport** (the 2D column of the matrix, editable here *and* on the hub — one model, two views, MVC per CLAUDE.md §5.1).

---

## 5. Part C — Process formulation initiation across domains

### 5.1 Policy (recommended; alternatives in §9)
- **Inherit-on.** Enabling a species class at project level turns it on in every domain that can carry it. This is the current behaviour and the legacy-SWMM expectation (pollutants flow from runoff to routing without a second switch).
- **Per-domain opt-out, per class.** New keys give each domain a transport switch per class. 2D: the four `[2D_OPTIONS] TRANSPORT_*` keys of §4.1. 1D: none needed — `IGNORE_QUALITY` already is the 1D+hydrology pollutant switch; MSX in 1D is governed by the reactions component; age/heat by `WATER_AGE`/`HEAT_TRANSPORT`. Groundwater: no transported quality exists (§5.4).
- **MSX follows pollutants' plumbing, not a separate switch.** If the reactions component is registered, MSX rows appear wherever pollutants do *and* the solver supports them (1D: all three; 2D: bulk only, WALL refused with the existing warning). Runoff/LID/GW get no MSX — that is a solver capability, reported as "n/a" in the matrix, not a user toggle.
- **The matrix is engine-computed.** New C API `swmm_get_transport_matrix(engine, SWMM_TransportMatrix*)` returning, per (domain, class): `ENABLED`, `DISABLED_BY_USER` (which key), `UNAVAILABLE` (why: no objects / solver lacks it / component not registered). The dialog renders it; the `.rpt` header prints it (users asked "why is there no 2D quality" more than once — the report should answer).

### 5.2 Where the rule lives in the engine
One function, `TransportPolicy::resolve(const SimulationContext&) → TransportMatrix`, in a new `src/engine/transport/TransportPolicy.{hpp,cpp}`. `SurfaceRouter2D.cpp:709-745`, `ArdEngine::initialize`, `LagrangianSolver` row setup and `QualityRouting` consult it instead of re-deriving `np/nm/na/nt`. This also retires the triplicated row-layout code and gives `SpeciesRegistry::transported_count()` its first caller (the registry becomes the single ordering authority: pollutants, MSX in ReactionData order, `__WATER_AGE__`, `__TEMPERATURE__` last — the S4 convention).

### 5.3 What changes for users
Nothing, by default. A deck that today runs 2D quality still does; one that wants 2D hydrodynamics without 2D transport (the common "quality is a 1D question" case, and a real performance lever — S1–S4 rows cost per-cell memory and a reaction stage) writes `TRANSPORT_POLLUTANTS NO` or unticks one cell of the matrix.

### 5.4 Groundwater
Legacy has no GW quality state and neither do we; the seam column (`c_gw`) and age/heat source volumes stay. Groundwater transport is Phase T7 of `TWOD_TRANSPORT_PLAN.md`, blocked on the G1 kernel. The matrix shows the GW row as "n/a — seam concentration only (legacy)" with a tooltip, so the answer to "should quality be initiated for groundwater" is visible rather than silent.

---

## 6. Part D — MSX parity: initial concentrations from multiple sources, CONCEN / MASS inflows

### 6.1 One species resolver
`SpeciesRegistry::find(name) → {kind, global_index, row}` becomes the only lookup used by `Inflow.cpp` (today `pollutant_names.find`), `PostParseResolver` `[INITIAL_QUALITY]` (today refuses MSX), `[DWF]`, the `.ard` resolver (`resolve_species`), and the 2D resolvers (`tr.rowIndex`). Reserved names (`__WATER_AGE__`, `__TEMPERATURE__`) resolve to their kinds. Unknown names produce **one** warning format everywhere. This is prerequisite to everything below and removes the 1D-vs-2D contradiction.

### 6.2 Initial concentrations — sources and precedence (extends D-IQ7)
Lowest to highest:
1. `[POLLUTANTS] Cinit` / `[REACTION_QUALITY] GLOBAL <species>` — project defaults.
2. `[INITIAL_QUALITY] NODE|LINK <elem> <constituent> <value>` — **now accepts MSX names** (lift `PostParseResolver.cpp:1700-1707`; keep `[REACTION_QUALITY] NODE|LINK` as an accepted alias that the writer no longer emits — one authoring surface). Same for the C API: `swmm_init_quality_set` accepts any registry name.
3. `[2D_INITIAL_QUALITY]` for cells — already accepts everything; unchanged.
4. **Sidecar / external table** (new, optional): `[INITIAL_QUALITY] FILE <path>` — a CSV `element,constituent,value` for the GIS-derived case (the "from various sources" ask). Implemented as a reader that appends rows before resolution; no new data model.
5. **Hotstart** — implement the reserved D-IQ5 slot: a species block (pollutants + MSX + age + temperature, keyed by name) in the hotstart file; wins over everything. Pollutants need this as much as MSX does.

GUI: `InitialQualityDialog` grows a constituent combo populated from the registry (kind shown as a badge: Pollutant / MSX / Age / Temperature), an "Import from CSV…" button feeding source 4, and a 2D tab for `[2D_INITIAL_QUALITY]` ('*' / TAG / CELL rows). The Quality › Initial Conditions page hosts it.

### 6.3 Inflows — CONCEN and MASS for MSX species
- `ExtInflowKind` gains `MSX` (mirror the `AGE` kind added at `Inflow.cpp:169-204`); `ExtInflowData` carries `species_row`. `[INFLOWS] <node> <species> <ts> CONCEN|MASS [mfactor] [sfactor] [base] [pattern]` parses for any registry name; `MASS` keeps the `/LperFT3` convention with the species' declared units from `[REACTION_SPECIES]`.
- Consumers at the node seam: LEGACY/LARD via `msx_node_conc` mass-rate accumulation (same place `addCouplingLoads` adds pollutant mass), ARD via its node mass rows, 2D via the coupling tuple's MSX half (S4). Add `[DWF]` MSX rows the same way (pattern-scaled concentration on the DWF flow); `[RDII]`/GW columns stay pollutant-only (they are `[POLLUTANTS]` columns by construction; an MSX equivalent would be `[REACTION_SPECIES]` columns — defer).
- `.ard` `[TRANSPORT_BOUNDARIES]`/`[TRANSPORT_SOURCES]`: keep, but make them solver-agnostic by converting them at resolve time into the same `ExtInflow` records (boundaries → `CONCEN` at the node; distributed conduit sources → a new `LINK_SOURCE` kind consumed by all three 1D solvers). Then the `ArdConfig.cpp:320-329` "inert under LEGACY/LARD" warning goes away.
- C API: `swmm_ext_inflow_add` already takes a constituent string — it just needs the registry resolver; add `swmm_ext_inflow_get_kind` so the GUI can label rows. `openswmm_inflows.h` doc update.

GUI: the Inflows editor's constituent combo lists registry names with kind badges; `CONCEN`/`MASS` radio stays; the units label follows the species' declared units. Attribute Table / Properties (node inflows compound editor) inherit automatically because they use the same editor.

### 6.4 Tests (engine)
`test_species_resolver.cpp` (one name → same answer from every section); `test_msx_inflows.cpp` (CONCEN and MASS on a 3-node deck under LEGACY, ARD, LARD: injected mass equals ∫inflow·conc bit-for-bit across solvers within the existing ARD/LARD tolerance decks); `test_initial_quality_msx.cpp` (`[INITIAL_QUALITY]` MSX row seeds `msx_node_conc`; precedence order; CSV sidecar; hotstart round trip). GUI: `test_initialqualitydialog` MSX rows; inflow editor combo contents.

---

## 7. Part E — 2D groundwater: infiltration and evapotranspiration enables

Ground truth: the integrated2d two-zone kernel is **G1 in `TWO_ZONE_GROUNDWATER_EXPLICIT_LTS_PLAN_2026-08-15.md`, not started**; track I (per-cell 2D infiltration, destination `LOST`) is implemented. So this part has two horizons.

**Now (no kernel):** the §4 `INFILTRATION` / `INFIL_DESTINATION` / `EVAPORATION` keys and the 2D › Processes page. `INFIL_DESTINATION AQUIFER_2D` and `SUBCATCH_AQUIFER` appear in the combo disabled with the "available from the groundwater release" tooltip (as `INTEGRATED2D_GW_GUI_PLAN` §1 already specifies). `SUBCATCH_AQUIFER` — routing 2D infiltration into the *legacy* per-subcatchment aquifer of the containing subcatchment — is implementable **before** G1 (it only needs cell→subcatchment containment and a recharge add into `Groundwater.cpp`); recommend pulling it forward as **track I-b**, because it is the first thing a user will try once the destination combo exists.

**With G1:** the 2D › Groundwater page (hidden until `org.hydrocouple.openswmm.integrated2d` is registered) carries the `GROUNDWATER` toggle and the closure/soil groups from `INTEGRATED2D_GW_GUI_PLAN` §1, plus two process rows this plan adds to that design:
- **Infiltration → aquifer**: when GW is on, `INFIL_DESTINATION` locks to `AQUIFER_2D` and the Infiltration group's *method* becomes the unsaturated-zone closure's job — the mutual exclusion the engine already validates (D-I4). The dialog shows this as a state change, not a hidden group.
- **Evapotranspiration**: `GW_ET` = `NONE | CAPILLARY_RISE | BOUNDARY_ET | BOTH` per manuscript §2.8 (capillary rise from the water table + boundary ET drawn from the unsaturated column), with the source being the same climate PET the 1D side uses (`SUBCATCHMENT_PET_PRESCRIPTION_PLAN` is implemented and is the rate provider). Surface `EVAPORATION` (§4) and `GW_ET` are separate rows: one removes ponded water, the other soil water; both book to the ledger. Species: ET removes volume only (concentrates), consistent with the surface sink.

Engine keys go into `model.i2d` per D-UT8/D-GW3, surfaced through `swmm_options_get_ext` like `[2D_OPTIONS]`. The transport matrix (§5) gains a "2D groundwater" row when the component is registered (Phase T7).

---

## 8. Phasing and estimates

| Phase | Content | Repo | Est. | Depends on |
|---|---|---|---|---|
| **P0** | Decisions in §9 | — | review | — |
| **P1** | Engine: `TransportPolicy` + `swmm_get_transport_matrix`; `[2D_OPTIONS] TRANSPORT_*`, `EVAPORATION`, `INFILTRATION`, `INFIL_STEP/DEFAULT_METHOD/DESTINATION` keys; `.rpt` matrix header; tests | engine | 3–4 d | S4 landed (row layout) |
| **P2** | GUI: page-class split + `QTreeWidget` sidebar + `PageGate`; the hub page with the matrix; 2D › Processes page; no-edit round-trip tests | gui | 5–7 d | P1 for the matrix API (can stub) |
| **P3** | Engine: unified species resolver; `[INITIAL_QUALITY]` MSX; `[INFLOWS]`/`[DWF]` MSX CONCEN/MASS; `.ard` boundaries/sources → solver-agnostic; tests | engine | 5–6 d | P1 (registry as ordering authority) |
| **P4** | GUI: InitialQualityDialog (registry combo, 2D tab, CSV import); Inflows editor MSX rows; Quality › Initial Conditions / Reactions pages | gui | 3–4 d | P3 |
| **P5** | Engine: hotstart species block (D-IQ5) incl. MSX; `[INITIAL_QUALITY] FILE` sidecar | engine | 3 d | P3 |
| **P6** | Engine: track I-b `INFIL_DESTINATION SUBCATCH_AQUIFER` | engine | 2–3 d | P1 |
| **P7** | 2D › Groundwater page + `GW_ET`, `AQUIFER_2D` destination | both | with G1 | G1 kernel |

Verification gates per phase: existing regression corpus bit-identical when no new key is set (P1, P3, P5 are default-preserving by construction — assert it); dialog no-edit round trip `n == 0` on the three reference decks (P2, P4); the CI matrix (macOS arm/intel, Ubuntu, Windows) green before each merge — and **commit per file with `git add -p` where trees are shared** (the 2026-09-03 break).

---

## 9. Decisions required before P1

1. **Inherit-on with per-domain opt-out** (§5.1, recommended) vs **explicit per-domain enable** (2D transport off unless asked). Inherit-on keeps every existing deck's results; explicit-enable changes results for every 2D+quality deck in the corpus and needs a migration note. Recommend inherit-on.
2. **One `TRANSPORT_*` key per class** (four keys, recommended — the matrix maps 1:1) vs a single `TRANSPORT` list key (`TRANSPORT POLLUTANTS MSX AGE`).
3. **`[INITIAL_QUALITY]` becomes the single authoring surface for MSX initial values** (recommended; `[REACTION_QUALITY] NODE|LINK` read as alias, no longer written) vs keeping both surfaces live.
4. **`.ard` `[TRANSPORT_BOUNDARIES]`/`[TRANSPORT_SOURCES]` converted to solver-agnostic inflow records** (recommended) vs left ARD-only with the existing warning.
5. **Sidebar as `QTreeWidget`** (recommended, keeps the settings-style layout and the existing gating primitive) vs a top-level `QTabWidget` with inner tabs per domain (closer to the literal "tab pages" wording; loses the always-visible overview and needs a new gating primitive for tab enable/disable).
6. **Pull `SUBCATCH_AQUIFER` forward (track I-b)** ahead of G1, or wait for the integrated kernel.
7. Scope check: the §6.2 CSV sidecar and §6.2/5 hotstart species block are both "initial concentrations from various sources" — confirm both are wanted in this round or defer hotstart to the quality close-out backlog.

---

## 10. Risks
- The dialog split touches every key round trip; the no-edit `n == 0` test is the safety net and must land **before** the split (P2 step 1).
- `TransportPolicy` replacing three row-layout sites is a bit-identity risk for ARD/LARD/2D quality decks — run the full quality regression set, not just unit tests.
- Two sessions are actively editing `openswmm.gui` (Plot-Rainfall button, Time Series editor) and the engine tree carries the uncommitted S4 remainder; sequence P1/P3 behind the S4 commit and stage hunk-wise.
- `QTreeWidget` gating of *parent* rows must not strand the current selection (extend the redirect in `set2DRowEnabled :429-442` to walk to the first enabled leaf).
