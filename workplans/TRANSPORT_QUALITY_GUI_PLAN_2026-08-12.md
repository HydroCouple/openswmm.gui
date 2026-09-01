# Transport, Multispecies Reactions, Water Age & Heat — GUI Plan (2026-08-12)

**Status:** Proposal — for review. No implementation yet. **Blocked on** the
engine Unified Transport suite (`openswmm.engine/plans/transport/`), phase by
phase as noted in §7; interface-level placeholders (disabled categories with
`gapSliceLabel` tooltips) may land ahead of the engine per the roadmap-seam
convention in `openswmm.engine/plans/GUI_API_REQUEST_BLOCK_BA.md`.
**Companion (engine computes; GUI orchestrates/renders):**
`openswmm.engine/plans/transport/UNIFIED_TRANSPORT_MASTER_PLAN.md` and its
sub-plans: `EULERIAN_ARD_TRANSPORT_PLAN.md`, `MULTISPECIES_REACTIONS_MSX_PLAN.md`,
`WATER_AGE_TRACKING_PLAN.md`, `HEAT_TRANSPORT_PLAN.md`, `TWOD_TRANSPORT_PLAN.md`,
plus `openswmm.engine/plans/LAGRANGIAN_QUALITY_API_STRATEGY.md` (GUI data
contract for LARD, as amended 2026-08-12).

**Decisions (user-approved 2026-08-12, carried over from the engine planning session):**

| Topic | Decision |
|---|---|
| Quality engine selection | `[OPTIONS] QUALITY_SOLVER LEGACY \| EULERIAN_ARD \| LAGRANGIAN` — three-way combo, per-engine parameter groups gated by selection (FLOW_ROUTING/FV-group precedent) |
| Reaction parameterization | EPANET-MSX conventions (`[REACTION_*]` sections); one shared reaction system serves all engines — the GUI edits **one** reaction model regardless of selected solver |
| Expression editing | Engine validator is authoritative — clone the `TreatmentExpressionEdit` stack; **no client-side parsing** |
| Heat/age | Reserved species configured via dedicated pages, not fake pollutants — **⚠ AMENDED 2026-08-23 (D-Y4) FOR WATER AGE:** age is now a first-class species in the UI (species pickers, inflow editor, plot pickers) and gains a real inflow pathway, though still **not** a `[POLLUTANTS]` row. Temperature keeps this line as written. See `openswmm.engine/plans/transport/AMENDMENT_1_WATER_AGE_AS_SPECIES_2026-08-23.md` |
| ARD numerical core | The Eulerian ARD engine reuses the 1D FV transport kernels; **FV implementation overrides HydroCouple/CSH conventions on conflict** (engine D-UT7). GUI consequence: ARD scheme combos expose the FV scheme set and alias the `FV_*` option keys |
| External config files | Each process component is configured by its own external file registered in `[PROCESS_COMPONENTS]`; the legacy `.inp` carries only coarse toggles (engine D-UT8, `plans/transport/TRANSPORT_IO_PLUGIN_CONFIG_PLAN.md`). GUI consequence: domain editors bind to component config files (§4.1 below); Files/Plugins tab gains a Process Components table |

---

## 1. User-facing behavior

1. **Project ▸ Simulation Options** gains a new sidebar category **"Quality &
   Transport"** (between "Routing & Hydraulics" and "System/Performance"):
   - **Water Quality Solver** combo: *Legacy (complete mix)* → `LEGACY`
     (default), *Eulerian ARD (advection–reaction–dispersion)* → `EULERIAN_ARD`,
     *Lagrangian (LARD)* → `LAGRANGIAN`. Tooltip names the `QUALITY_SOLVER` key.
   - **Eulerian ARD group** (enabled only for `EULERIAN_ARD`): Scalar scheme
     (*Upwind / MUSCL / QUICKEST-ULTIMATE* → `ARD_SCALAR_SCHEME`, aliasing
     `FV_SCALAR_SCHEME` — the engine's FV transport kernels are the ARD core,
     rev. 2 of the engine plan), Limiter combo (enabled only for MUSCL →
     `ARD_LIMITER`), Dispersion (*Off / Fischer / Value* + spinbox →
     `ARD_DISPERSION`), Transport mesh target Δx (`ARD_TARGET_DX`, shown only
     when `FLOW_ROUTING` ≠ FV), Quality step (`QUALITY_STEP`). When
     `FLOW_ROUTING FV` is selected, the existing FV group's scalar-scheme
     widgets and this group edit the **same aliased keys** — one shared widget
     set, surfaced in both places, so the two pages can never disagree.
   - **Lagrangian group** (enabled only for `LAGRANGIAN`): Dispersion
     (*Off/RWPT* → `DISPERSION`), Max segments per link
     (`MAX_SEGMENTS_PER_LINK`), Quality step (shared `QUALITY_STEP` widget).
   - **Water Age group**: *Track water age* checkbox (`WATER_AGE`), *Snowpack
     ages* checkbox (`WATER_AGE_SNOW`), *Subcatchment aquifer age state*
     checkbox (`WATER_AGE_GW_STATE`), and an **Edit Source Ages…** button
     opening the Water Age Sources editor (§3.4).
   - **Heat Transport group**: *Simulate heat transport* checkbox
     (`HEAT_TRANSPORT`) gating: Evaporation / Convection / Radiation /
     Sediment-exchange checkboxes, water density + specific heat spinboxes,
     wind-function a/b, Bowen coefficient, pressure ratio, albedo, emissivities,
     Brunt A/B, sky-view factor (`[HEAT_OPTIONS]` keys), and an **Edit
     Meteorology & Sources…** button opening the Heat configuration editor
     (§3.5).
   - The existing `IGNORE_QUALITY` checkbox moves onto this page (kept on
     Models/Processes as well is NOT done — single home; the hydration
     contract test updates accordingly).
2. **2D Surface Routing page** (existing, `#ifdef OPENSWMM_HAS_2D`) gains a
   **2D Transport group**: *Transport on 2D mesh* (`2D_TRANSPORT`), dispersivity
   α_L + molecular ε (`2D_DISPERSION`), *Reactions on cells* (`2D_REACTIONS`),
   *Heat on cells* (`2D_HEAT`).
3. **Model ▸ Add Data Object** and the Object Browser **Data** section gain a
   **"Reaction System"** entry opening the Reaction System editor (§3.2) — one
   editor for species, coefficients, terms, expressions, sources, initial
   quality, and reaction options (matching MSX file structure), synchronized
   with the rest of the UI per CLAUDE.md §5.1.
4. **Property panel / compound editors:** storage nodes gain a *Mixing model*
   enum property (`CMSTR | TWO_COMPARTMENT | FIFO | LIFO`); nodes gain
   *Transport boundary* and links gain *Dispersion coefficient* properties when
   `EULERIAN_ARD` is active.
5. **Results:** species (per pollutant/MSX species), water age (hours), and
   temperature (°C) become selectable everywhere results are themed or
   plotted — map theming combos, profile/time-series plots, tabular results,
   and 2D scalar fill.

## 2. Architecture (MVC per CLAUDE.md §5.1)

- **Model** — the engine handle remains the model. New Registry/Provider pairs
  following the `PollutantRegistry` idiom
  (`include/pollutant/pollutantregistry.h`):
  - `openswmmvis::reaction::ReactionSystemRegistry` — species, parameters,
    constants, terms, pipe/tank expressions, sources, initial quality, and
    reaction options, backed by `openswmm_reactions.h` (engine plan
    `MULTISPECIES_REACTIONS_MSX_PLAN.md` §4). Signals per the uniform registry
    API (`providerAdded/…/providerParamsChanged`).
  - Water-age sources and heat met/source configuration are **not** separate
    registries — they are option-like keyed tables read/written through
    `openswmm_water_age.h` / `openswmm_heat.h` and held by lightweight table
    models bound to the engine handle (the `QualityFunctionTableModel`
    apply-as-you-go pattern, `include/ui/models/qualityfunctiontablemodels.h`).
- **Controller** — mutations via engine C API through the registries/table
  models; spatial-element properties (link dispersion, storage mixing model,
  node boundary) via `SWMMModelLayer::apply*()` + `MapCommand` (new command
  ids, append-only).
- **Views** — Simulation Options page, Reaction System editor, Water Age
  Sources editor, Heat configuration editor, property adapters, results
  pickers. Singleton dialogs use `static QPointer<T>` raise-not-recreate with
  the `[[feedback_mvc_synchronized_uis]]` tag.

## 3. New files

All filenames lowercase-no-separator; headers mirrored in `src/`; every file
added to the explicit `CMakeLists.txt` header/source lists.

### 3.1 Simulation options

No new files — extend `SimulationOptionsDialog`:
`buildQualityTransportTab()` registered in `buildUi()`
(`src/ui/dialogs/simulationoptionsdialog.cpp:288-312` `addCategory` chain);
gating helper `updateQualitySolverFieldsEnabled()` modeled on
`updateFvFieldsEnabled()` (`:1104`); round-trip in `readFromEngine()` /
`writeToEngine()` with `writeIfChanged` + `selectComboByData`; engine-version
gating via `applyEngineConstraints()` (`:167-190`) so LARD/ARD combo items
disable gracefully against engines without `QUALITY_SOLVER` (multi-engine
policy per `workplans/MULTI_ENGINE_VERSION_SUPPORT_PLAN_2026-08-01.md`).
Defaults seeded in `PreferencesManager` (`TwoDDefaults` pattern) and
`SWMMModelLayer::createBlankEngine`.

### 3.2 Reaction System editor

```
include/ui/dialogs/reactionsystemeditordialog.h     ReactionSystemEditorDialog
include/ui/models/reactionspecieslistmodel.h        species list (BULK/WALL, units, atol/rtol)
include/ui/models/reactioncoefficienttablemodels.h  ParameterTableModel, ConstantTableModel,
                                                    TermTableModel (name + expression)
include/ui/models/reactionexpressiontablemodel.h    scope (PIPE/TANK/SURFACE2D) × species ×
                                                    {RATE|EQUIL|FORMULA} × expression
include/ui/models/reactionsourcetablemodel.h        node sources (CONC/MASS/FLOWPACED/SETPOINT + pattern)
include/ui/models/reactioninitialqualitymodel.h     GLOBAL/NODE/LINK initial values
include/ui/widgets/reactionexpressionedit.h         ReactionExpressionEdit + ReactionSyntaxHighlighter
                                                    + ReactionExpressionDelegate
include/reaction/reactionsystemregistry.h           registry/provider (see §2)
```

Editor layout: comprehensive-editor idiom (list pane + `QTabWidget` detail
pane, `QSplitter`, non-modal, `WA_DeleteOnClose`), tabs **Options / Species /
Coefficients / Terms / Expressions / Sources / Initial Quality** — mirroring
the `[REACTION_*]` section structure so users familiar with MSX files map 1:1.
`ReactionExpressionEdit` clones the `TreatmentExpressionEdit` stack
(`include/ui/widgets/treatmentexpressionedit.h`; debounced engine validation,
QCompleter, highlighter with VOCAB DRIFT GUARD) calling
`swmm_reaction_validate_expression(engine, scope, expr, errbuf, n, &col)`
(engine prerequisite §6.1); completer vocabulary from the discovery getters
(species/params/constants/terms + hydraulic variables `D,Q,U,Re,Us,Ff,Av,HRT,DT`).
Per-element parameter overrides (`[REACTION_PARAMETERS]`) are edited from the
link/node property panel (a `*CompoundEditRef` page in
`NodeCompoundEditDialog` / a new link compound page), not in the system editor —
same split as treatment today.

New `SWMMModelLayer::DataCategory::DataReactionSystem` (append-only, before
`NumDataCategories`) wired through the full checklist established by
`DataInlets`: `dataObjectCount/dataObjectNameAt/suggestUniqueDataObjectName/
createDataObject/delete` switches (`src/layers/swmmmodellayer.cpp:1558,1631,
1731,1831,1953`), tree label + icon (`src/ui/panels/swmmobjecttreemodel.cpp:92,132`),
`ComprehensiveEditorRegistry::populateOnce()` entry (with `gapSliceLabel`
"Requires engine with multispecies reactions" until the engine phase lands),
Add-menu row (`src/swmmvis.cpp:3436-3452`), property category
(`src/ui/panels/propertiespanel.cpp:987`), `DataObjectRef::Kind`.

### 3.3 Reserved-species awareness

`PollutantEditorDialog` and pollutant pickers exclude reserved species
(`__WATER_AGE__`, `__TEMPERATURE__`); results pickers show them with friendly
labels ("Water age (hours)", "Temperature (°C)") — mapping via the species
registry kind (engine master plan §4.1).

**⚠ AMENDED 2026-08-23 (D-Y4):** the exclusion now applies to
`__TEMPERATURE__` **only**. Water age is offered in species pickers and in
the inflow editor's constituent list (rounds Y4 / Z1), labelled and united
as hours — never as mg/L. `PollutantEditorDialog` still excludes age,
because age has no buildup/washoff/decay parameters to edit; "first-class"
means *selectable and plottable*, not *editable as a pollutant*. See
`openswmm.engine/plans/transport/AMENDMENT_1_WATER_AGE_AS_SPECIES_2026-08-23.md`.

### 3.4 Water Age Sources editor

```
include/ui/dialogs/wateragesourcesdialog.h    WaterAgeSourcesDialog
include/ui/models/wateragesourcetablemodel.h  rows: source kind (RAINFALL/DWF/GW/RDII/
                                              EXTERNAL_INFLOW/IFACE/INITIAL_STATE/BOUNDARY_2D),
                                              scope (GLOBAL/NODE/SUBCATCH/EDGE_BC + picker),
                                              value (hours) | timeseries (DataObjectRef)
```

Table with `EnumComboDelegate` for kind/scope, `DataObjectRef`-typed picker
cells for node/subcatchment/timeseries columns. Backed by
`openswmm_water_age.h`. Launched from the options page and from Model menu.

### 3.5 Heat configuration editor

```
include/ui/dialogs/heattransportdialog.h      HeatTransportDialog (tabs:
                                              Meteorology / Radiation / Sources / Sediment)
include/ui/models/heatforcingtablemodel.h     element-range rows: variable
                                              (RELATIVE_HUMIDITY/AIR_TEMPERATURE/WIND_SPEED/
                                              SHORTWAVE), start/end element pickers,
                                              VALUE|TIMESERIES
include/ui/models/heatsourcetablemodel.h      source temperatures (DWF/GW/RDII/EXTERNAL/RAINFALL)
                                              + direct HEAT sources (J/s/m over element ranges)
```

Global met defaults continue to come from Climatology
(`src/ui/dialogs/climatologydialog.cpp`); that dialog's Temperature tab gains
Relative-Humidity and Shortwave sub-sections when the engine adds them as
climate inputs (heat plan §2.4) — per-element overrides live here. Sediment
tab edits HTS-style parameters per element range (depth, thermal diffusivity,
advection coefficient).

### 3.6 Results (design decision D-G1)

**D-G1 — dynamic result descriptors, not enum growth.** Species results are
dynamic (N species per run), so extending the flat compile-time enums
(`SWMMResultVariable`, `include/layers/swmmresultslayer.h:52-74`;
`PlotAttribute`, `include/plot/plotattribute.h`) with base+index encodings
does not scale. Proposal: introduce a runtime descriptor
`ResultDescriptor { kind, engineCode, speciesIndex, label, units }` surfaced
through `IRunLayer` (`include/plot/irunlayer.h`) and
`IAttributeProvider::availableAttributes` (`include/render/iattributeprovider.h`),
with the existing enums retained as fixed descriptors so current call sites
are untouched. `AttributePickerMenu::createForObjectKind` gains a "Water
Quality ▸" submenu enumerating descriptors from the loaded run.
*Alternative (rejected unless review disagrees):* reserve enum blocks
(e.g. 100 + p) — simpler but caps species count and leaks indices into
`.oswp` persistence.

Surfaces lighting up with descriptors: map theming combos
(`PerAttributeThemingWidget`, `src/ui/widgets/perattributethemingwidget.cpp:60-76`),
profile/time-series plots (`swmmoutrunlayer_codes.cpp` mapping to
`SWMM_OUT_NODE_QUAL`/`SWMM_OUT_LINK_QUAL` + species index), tabular results +
statistics dashboard (currently quality-blind), and 2D via a
species/temperature/age-fed `ScalarFillSublayer`
(`include/render/sublayers/`) reading the transport variables from the 2D
HDF5 sidecar through `IMesh2DSource` extensions. Per-element ARD profiles
(`TRANSPORT_DETAILED_OUTPUT` sidecar) register a `FileFilterRegistry`
ResultsRead `FilterKind`.

## 4. Actions & wiring

### 4.1 Process-component config file binding (engine D-UT8)

- The **Files/Plugins tab** of Simulation Options gains a **Process
  Components table** (reusing the `PluginsTableModel` + `PathBrowseDelegate`
  pattern of the existing `[PLUGINS]` table): component combo populated from
  the `PROCESS_COMPONENT` plugin registry, `config=` path cell with browse,
  extra `key="value"` argument cells for third-party components; row
  presence = enabled.
- `FileFilterRegistry` gains ResultsRead-style `FilterKind`s for the config
  types (`.rxn`, `.ard`, `.lard`, `.heat`, `.age`, `.i2d`) so browse dialogs
  filter correctly. Note engine D-UT9 (as amended): 2D surface + two-zone
  groundwater + their transport/heat are **one unified component**
  (`integrated2d`) on the shared mesh, with one config file `model.i2d` —
  the existing 2D options page and mesh tooling become views over that
  config when the component is registered, and groundwater/GW-transport
  settings join the same page family (capability toggles: surface-only /
  GW-only incl. PER_SUBCATCH / fully integrated). Legacy embedded
  `[2D_*]`/`[2D_MESH_FILE]` remains a deprecated path with a one-click
  "externalize to component config" migration. Groundwater/infiltration
  configuration and subsurface visualization are planned separately in
  `workplans/INTEGRATED2D_GW_GUI_PLAN_2026-08-15.md` (reuses this plan's
  D-G1 descriptors and §4.1 config-file binding rules).
- Domain editors (Reaction System, Water Age Sources, Heat, ARD/2D option
  groups) keep editing **engine state** — the engine remains the model; Save
  persists through the component's `saveData()` to its bound config file
  (path shown in the editor title bar) and marks the project dirty via the
  usual mechanism. `swmm_process_component_reload_config` backs a
  "Revert to file" action with staleness guard.
- Enabling a coarse toggle (e.g. `HEAT_TRANSPORT`) with no registered
  component prompts a one-step "Create + register config file" flow
  (default filename beside the `.inp`); the options page shows the bound
  filename next to each toggle. Validation errors from the
  toggle/registration consistency rule surface in the existing options
  validation panel.

- `actionEditReactionSystem`, `actionEditWaterAgeSources`,
  `actionEditHeatTransport` registered in `src/swmmvisactions.cpp` (Model
  ribbon row) + Object Browser context menus via the comprehensive-editor
  registry entry.
- New `MapCommand` ids (append-only, next free block) for link dispersion,
  storage mixing model, node transport boundary edits.
- Undo: registry-backed editors follow the existing pollutant/landuse
  convention (apply-as-you-go, no undo stack); spatial property edits go
  through `MapCommand` as usual.

## 5. Docs & tests

- `docs/manual/10_simulation_options.md` — "Quality & Transport" section;
  new manual pages `docs/manual/xx_reactions.md`, `xx_heat_transport.md`,
  `xx_water_age.md` indexed from `manual.md`.
- `tests/gui/test_options_hydration_contract.cpp` — rows for every new option
  key (defaults, round-trip, bad-enum rejection).
- New QtTests: `test_reaction_system_editor.cpp` (registry sync, expression
  validation path — mirrors `test_treatment_expression_editor.cpp`),
  `test_water_age_sources_model.cpp`, `test_heat_forcing_model.cpp`,
  `test_result_descriptors.cpp`.
- All test artifacts written under the repo's test-output directory, not temp
  folders (CLAUDE.md §4.1).

## 6. Engine prerequisites (additions requested to the engine plans)

Filed per convention as `openswmm.engine/plans/GUI_API_REQUEST_TRANSPORT.md`
when implementation starts; headline symbols the engine plans must include:

1. `swmm_reaction_validate_expression(engine, scope, expr, errbuf, n, &col)` —
   GUI usage: `ReactionExpressionEdit` debounced validation (mirrors
   `swmm_treatment_validate_expression`,
   `src/ui/widgets/treatmentexpressionedit.cpp:225`).
2. Discovery getters for completer/highlighter vocabulary: species / parameter
   / constant / term counts + names + kinds + units, hydraulic-variable and
   function name lists (drift-guard source of truth).
3. Species-kind query (`POLLUTANT | RESERVED_AGE | RESERVED_TEMPERATURE |
   MSX_BULK | MSX_WALL`) for §3.3 filtering — registry per master plan §4.1.
4. Output enumeration of dynamic quality variables per run (species count,
   labels, units) for D-G1 descriptors — extension of `openswmm_output.h`.
5. Water-age / heat source-table CRUD (`openswmm_water_age.h`,
   `openswmm_heat.h`) with `*_count` companions and caller-allocated buffers,
   per the snake_case C-API house rules.
   **Water-age half ✅ engine `d7b6c079` (X5). Heat half still owed.**
5a. **The options page needs C-API keys, not just parser keys** (Y0's
   lesson, engine `948b2840`): the `[OPTIONS]` parser accepting a key says
   nothing about `swmm_options_get/set` dispatching it — G1g's seven
   transport keys were missing from the API dispatch until Y0. **The same
   trap waits for the heat option keys at G4g** — verify against
   `swmm_options_set`, not the parser, before declaring G4g unblocked.
6. 2D sidecar variable metadata (species/temperature/age names + units) via
   `IMesh2DSource`-consumable HDF5 attributes (engine 2D plan §3.1).

## 7. Implementation phases

```
G1  Simulation Options "Quality & Transport" page + 2D transport group +
    preferences defaults + engine-version gating.       [after engine T0–T2 options exist]
    ✅ CORE LANDED `ebf28ae` (Y1, 2026-08-23): page + hydration + gating +
    capability probe. NOT in Y1: 2D transport group (needs the 2D keys),
    preferences defaults (page falls back to engine defaults — arguably
    better, pinned by the hydration contract), IGNORE_QUALITY relocation
    (deviation UPHELD at validation — one checkbox out of a six-sibling
    group is worse than the labelled inconsistency; amend §1.1's line).
    ⚠ The dialog's widgets have NO automated observer (the :1996 trap) —
    the dialog-harness round is owed before Y3 and would close FV's gap too.
    → verify: hydration-contract rows pass against new engine; combo items
      disabled + tooltip against an old engine build.
G2  Reaction System editor + registry + DataReactionSystem category +
    expression stack with engine validation.            [after engine R1–R2, prereqs 1–3]
    → verify: round-trip a translated MSX example (nh2cl) through the editor,
      save .inp, engine parses; registry signals update Object Browser +
      property panel simultaneously (MVC gate).
G3  Water Age Sources editor + options wiring.          [after engine A1]
    ◐ Y3 LANDED `f5e0d9b` (2026-08-23): the editor itself, fully test-
    observed (dependency-light dialog, ClimatologyDialog precedent; §3.4's
    WaterAgeSourceTableModel deviation upheld — engine handle IS the model).
    ✅ Y3b LANDED `bc4e07c` — the "owed / unreachable" note below is STALE
    and is kept only as the corrected record: `actionEditWaterAgeSources`
    exists in forms/swmmvis.ui (:682 menu, :1396 action) with the handler
    SWMMVis::onEditWaterAgeSources (swmmvis.cpp:6941) AND an options-page
    button (simulationoptionsdialog.cpp:1287). The editor IS reachable.
    → verify: source table round-trips [WATER_AGE_SOURCES]; per-scope pickers
      resolve names after rename (registry rename signal test).
G4  Heat configuration editor + Climatology RH/shortwave sub-sections.
                                                        [after engine H1–H3]
    → verify: forcing table round-trips [HEAT_METEOROLOGY]/[HEAT_SOURCES];
      element-range pickers validate start<=end ordering.
G5  Results descriptors (D-G1): map theming, plots, tabular, statistics,
    ◐ Y2a LANDED `dcc20e6` (2026-08-23): map theming half — species as
    dynamic attributes, name-keyed tokens, reserved labels/units. Y2b owed:
    plots/tabular/stats/.oswp round-trip, warn-on-miss, per-species units.
    ⚠ RE-SCOPE (Y3 handoff §7, 2026-08-23): Y2b is NOT one round. The plot
    surface is a fixed 37-value PlotAttribute enum consumed by
    attributesForKind/pickers/comparison plot/tabular/statistics; D-G1
    forbids base+index extension, so species plotting needs the
    ResultDescriptor-through-IRunLayer refactor. Split: Y2b-1 descriptor
    plumbing in IRunLayer (✅ DONE gui `dae4bad` 2026-08-24; IRunLayer must
    stay header-only — stub tests link no plot TUs) · Y2b-2 pickers ✅ DONE
    gui `7a5f732` 2026-08-24 (all pickers + quick menus + Add Series form;
    codes by NAME reorder-proof) ·
    Y2b-3 ✅ DONE gui `9e63357` 2026-08-24 (warn-on-miss + .oswp token gate;
    per-species units from the .out still owed to a later round) ·
    Y2b-3 tabular/statistics + .oswp round-trip + warn-on-miss.
    profile plots for species/age/temperature.          [after engine T2/T3/T4 outputs]
    → verify: .out with 3 pollutants + age + temperature exposes 5 dynamic
      descriptors in every picker; .oswp persistence of a species theme
      survives reload; legacy .out (no quality) shows no submenu.
G6  2D transport rendering (ScalarFillSublayer feed + style panel row) +
    per-element ARD profile sidecar reader.             [after engine S1–S5]
    → verify: 2D tracer animation renders; sublayer style round-trips .oswp.
G7  Storage mixing-model / link dispersion / node boundary properties +
    MapCommands; docs/manual pages; CHANGELOG (CLAUDE.md §5.2).
    → verify: undo/redo cycles; manual builds in Doxygen without warnings.

--- Added by AMENDMENT 1 / D-Y4 (user, 2026-08-23) -----------------------
    Water age becomes a first-class species in the UI. Full record:
    openswmm.engine/plans/transport/AMENDMENT_1_WATER_AGE_AS_SPECIES_2026-08-23.md
Z1  [ENGINE] ✅ DONE (engine `4639be37`, 2026-08-24) — `__WATER_AGE__`
    accepted, VALUE and TIMESERIES, routed to the age-volume accumulator;
    InpWriter round-trip; C API; the current SILENT DROP (Inflow.cpp:303,
    p < 0) replaced by resolution or a warning.   [after engine X4 ✅]
    ⚠ Must FLIP A1a's landed TIMESERIES-deferral gate, not delete it.
    ⚠ Must define precedence vs the Y3 source table and warn on conflict.
    → verify: a time-varying age inflow shifts the outfall age on the
      measured schedule; a misspelled species warns; corpus 19/19.
Y4  ✅ DONE gui `94ff3b5` 2026-08-24 — age in the INFLOW EDITOR (hours
    label, data-keyed combo, MASS gated off); §3.3's exclusion lifted for
    age only. AMENDMENT D-Y4 COMPLETE.
    → verify: an age inflow authored in the UI round-trips through
      save/reopen and appears in the plot picker.
```

## 8. Risks / open notes

- **Engine sequencing** — every phase is gated on engine deliverables; the
  `gapSliceLabel` placeholder mechanism lets the category ship early without
  dead-end UX.
- **Multi-engine degradation** — `QUALITY_SOLVER` etc. must disable cleanly
  against older engines (constraint probing via `swmm_options_get` fallback
  returns), per the multi-engine version-support plan.
- **D-G1 descriptor persistence** — themes/plots referencing a species by
  index must survive model edits that reorder species; persist by species
  *name* in `.oswp`, resolve to index at load, warn on miss.
- **Editor scope creep** — the Reaction System editor intentionally excludes
  per-element parameter overrides (property-panel concern) and any reaction
  visualization/plotting (future work) to stay reviewable.
- **Legacy 5.x engine path** — quality/transport UI is new-engine only; the
  legacy worker path hides the page entirely.
