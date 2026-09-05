# Groundwater flow + transport (pollutants / heat / MSX) as HydroCouple components — comprehensive program plan (2026-09-05)

**Status: DRAFT FOR REVIEW.** Part A is an audit (verified against the repos on
2026-09-05). Parts B–F are the plan. Nothing in Parts B–F is implemented.
Decisions in §E must be settled before code (CLAUDE.md §5.0).

**Purpose.** One document that (1) reviews where groundwater flow and its
pollutant / heat / MSX transport stand in the engine and the GUI, (2) fleshes
out how they — and any other process, e.g. FVQual — become HydroCouple
components that are *discoverable* from a SWMM `.inp` (`[PROCESS_COMPONENTS]`)
and from the SWMMVis GUI, and *orchestratable* from HydroCoupleComposer, and
(3) aligns the six plans below into one sequence with one set of decisions.

**Decisions taken by the owner on 2026-09-05 (this document is written to them):**

| # | Decision |
|---|---|
| D-C1 | **One `integrated2d` component** (surface + two-zone groundwater + all transport, one `model.i2d`), per D-UT9 / D-GW3. GW flow and GW transport / heat / MSX are capability toggles inside it, and it exposes per-domain exchange items so other components can couple to it. No separate GW component. |
| D-C2 | **Component wrappers live in `openswmm.engine`** under `src/couplers/` (engine-as-component, `integrated2d` standalone façade, future `Swmm5Component`). `HydroCoupleComponents` holds only components with no engine dependency (mesh, time-series providers, FVQual). Amends `ENGINE_524_…` §B.4.3. |
| D-C3 | **Discovery seam first.** Shared loader ABI + `[PROCESS_COMPONENTS]` library rows + GUI Process Components table before the GW kernel; the kernel proceeds in parallel. |
| D-C4 | This document lives in `openswmm.gui/workplans/`; the engine and Composer plans get a pointer. |
| D-C5 | **FVQual is a design constraint, not a deliverable.** Every seam here must admit an FVQual-class component (external repo, own mesh/output format, `.rxn`-style kinetics, standalone CLI) **but no FVQual code, component, composition or editor is implemented under this plan.** Hosting semantics are proven with an in-tree test component instead (§C.6). |
| D-C6 | **No `HydroCoupleSDK` dependency anywhere in the SWMM implementation** — engine, `src/couplers/*`, and SWMMVis link only the header-only `HydroCouple` interfaces (plus the upstreamed ABI header). Composition JSON is written by SWMMVis as plain JSON against the Spec v1.1 schema; the SDK is a Composer-side dependency only. Not re-openable by a later mounting of the SDK. |

**Plans aligned here** (every decision in them stands unless amended in §A.4 / §E):

| Plan | Repo | Role in this document |
|---|---|---|
| `ENGINE_524_STATUS_AND_2D_COUPLING_PLAN_2026-09-04.md` | gui | 5.x ↔ 2D via HydroCouple; Part B target architecture; gates 1–6 |
| `plans/transport/PROCESS_COMPONENT_IMPLEMENTATION_PLAN_2026-09-01.md` (v3) | engine | HC1–HC5: linkage, hosting seam, exchange catalogue, `OpenSWMMModelComponent` |
| `plans/transport/GW_TRANSPORT_HEAT_MSX_PLAN_2026-09-04.md` | engine | T7.0–T7.5 + G-T7.a–d: GW solute / MSX / age / heat |
| `plans/TWO_ZONE_GROUNDWATER_EXPLICIT_LTS_PLAN_2026-08-15.md` | engine | G1 / G2 kernel (D-GW1..4) |
| `plans/transport/TRANSPORT_IO_PLUGIN_CONFIG_PLAN.md`, `UNIFIED_TRANSPORT_MASTER_PLAN.md` (D-UT2, D-UT8, D-UT9), `plans/PLUGIN_SDK.md` | engine | `[PROCESS_COMPONENTS]`, config-file contract, `PluginType::PROCESS_COMPONENT` |
| `SIMULATION_OPTIONS_AND_PROCESS_GATING_PLAN_2026-09-04.md`, `EXECUTION_PLAN_OPTIONS_TRANSPORT_OUTPUT_2026-09-04.md` (P0–P9), `INTEGRATED2D_GW_GUI_PLAN_2026-08-15.md` (GG0–GG6), `AQUIFER_GROUNDWATER_EXCHANGE_GUI_PLAN_2026-09-05.md`, `TRANSPORT_QUALITY_GUI_PLAN_2026-08-12.md` | gui | GUI authoring / gating / results |
| `MULTI_ENGINE_VERSION_SUPPORT_PLAN{,_V2}` | gui | `EngineRegistry` manifest, `runMode` |
| `HydroCoupleComposer/plans/COMPOSER_MODERNIZATION_PLAN_2026-08-24.md` (§9 P6, E5/E6) | composer | loader ABI, composition spec, headless run, FVQual |

**Not mounted on 2026-09-05, therefore not verified:** `HydroCoupleSDK`
(`plans/PROVIDER_PIPELINE_PLAN_2026-09-03.md`, `io/compositionspec.h`,
`ModelInitializer`), `HydroCoupleComponents`, `FVQual`, and
`swmm5.2.4/openswmm.engine.5.2.4`. Claims about them below are quoted from the
mounted plans and marked *(unverified)*.

Repos at audit: `openswmm.engine` @ `2fa426d1` (swmm6_rel, dirty ~40 files),
`openswmm.gui` @ `3e8cb4c` (swmm6_gui, dirty 159 entries), `HydroCouple` @
`bef95cb` (ABI 2), `HydroCoupleComposer` @ `22e5abe`.

---

## Part A — Where the program stands (audit)

### A.1 Engine (`openswmm.engine`)

| Area | State | Evidence |
|---|---|---|
| **GW flow, 1D** | **DONE (legacy parity).** Per-subcatchment two-zone ODE, RKF45, SoA state; `[AQUIFERS]/[GROUNDWATER]/[GWF]` parsed; `[GWF]` expressions compiled. New `[GWF]` editor API (`swmm_subcatch_get/set_gwf_expression`, `swmm_gwf_validate_expression`, ERROR 233) is **in the dirty working tree, uncommitted**. | `src/engine/hydrology/Groundwater.cpp:255-403, :349` (ode::integrate), `:592` (validator); `HydrologyHandler.cpp:415-458`; `CHANGELOG.md` Unreleased |
| **GW flow, 2D two-zone kernel** (`integrated2d`) | **NOT STARTED — 0 of 4 code steps.** `src/engine/2d/subsurface/` does not exist. Only Track I per-cell 2D infiltration landed (`310e8ffb`); its aquifer destinations are refused by the C API (D-I4). | `UNIFIED_PLAN_STATUS_2026-08-29.md:47-51`; `Infil2D.hpp:83-86`; `ApiInfil2D.cpp:124-130` |
| **1D transport** (ARD, reactions/MSX, age, heat incl. bed + hyporheic, LARD) | **DONE.** Four in-tree process components registered under `std::call_once` at open; ids `org.hydrocouple.openswmm.{transport.ard, reactions, heat, waterage}`. | `SWMMEngine.cpp:299-313`; `UNIFIED_PLAN_STATUS…md` §2–3 |
| **2D surface transport** (Phase 3, S1–S5) | **S1–S4 LANDED** (`6d369d5d`, `4c169dbe`): species/age/temperature rows on the marcher, `reactArdStage` per wet cell, node-row publish, 1D↔2D tuple queues. S5 (`CELL2D` heat storage) open. *Status doc (08-29) predates this and still says 0/7.* | `S4_OVERLAND_TRANSPORT_HANDOFF_2026-09-02.md`; `git log` |
| **GW transport / heat / MSX** (T7) | **NOT STARTED**, blocked on the kernel. Today: one constant `c_gw` per pollutant applied only to positive GW inflow; constant GW temperature and age; no soil thermal model. | `QualityRouting.cpp:680-716`; `GW_TRANSPORT_HEAT_MSX_PLAN…md` §1 |
| **MSX** | Native reactions component (`.rxn`, MSX conventions, `ReactionIntegrator` element-agnostic, hydvars `D Q U RE US FF AV HRT DT TEMP`). No EPANET-MSX library, no `.msx` reader. `SUBSURFACE` scope not implemented. | `ReactionIntegrator.hpp:104`; `ReactionTokens.hpp:41-54` |
| **`[PROCESS_COMPONENTS]`** | Parsed (`id key="value"`, `config=` extracted). Registry is an **in-process string-id table**; six ids pre-seeded, four real, `transport.lard` and `integrated2d` placeholders. **A library path in a row is a hard error** ("arrives with plan phase HC2"). IO3a `ComponentConfigSave` hook exists. C API: count/get/find/register/remove only. | `ProcessComponentsHandler.cpp:53-79`; `ProcessComponentRegistry.cpp:144-159, :189-260, :216-223`; `openswmm_process_components.h:46-72` |
| **IO plugin loader** | Real dlopen system, one exported symbol `openswmm_plugin_info`, **no ABI stamp**, same-toolchain C++ ABI; scans `<enginedir>`, `/plugins`, `/components`. `PluginType {INPUT, OUTPUT, REPORT, STATE_IO}` — no `PROCESS_COMPONENT`. | `PluginFactory.cpp:131-156, :186-224, :250-258`; `IPluginComponentInfo.hpp:30, :75, :406-413` |
| **HydroCouple linkage** | **None.** No header, no `find_package`/FetchContent; `IModelComponent` appears zero times in source. Only reverse-DNS id strings and HC2 doc comments. | grep; `ProcessComponentRegistry.hpp:26-30` |
| **`EXTERNAL_1D` / `swmm_2d_external_*`** | **Do not exist** (zero hits). `swmm_forcing_node_{lat_inflow, head_boundary, quality}` exist; no `_temperature` / `_age` forcing. | `openswmm_forcing.h:146, :160, :175` |

### A.2 GUI (`openswmm.gui` / SWMMVis)

| Area | State | Evidence |
|---|---|---|
| **1D aquifers / GW exchange editors** | First-cut provider/registry/editor committed. Full rebuild (aquifer diagram, `[GWF]` expression editor, Groundwater Exchange dialog, node GW summary) is **written, untracked, never compiled** (handoff authored without a compiler). The plan's `remove()` bug (never reached `swmm_aquifer_delete`) is fixed in the same uncommitted tree (`aquiferregistry.cpp:77-88`) — also unverified. | `AQUIFER_GROUNDWATER_EXCHANGE_VERIFY_HANDOFF_2026-09-05.md:5-8`; `src/aquifer/aquiferregistry.cpp:77-88` |
| **2D groundwater GUI** (GG0–GG6) | Not started, except GG0's `[2D_INFILTRATION_*]` read/write in `inpmeshwriter/reader`. | `INTEGRATED2D_GW_GUI_PLAN…md:3`; `src/mesh/inpmeshwriter.cpp:179-360` |
| **Transport / quality editors** | Landed: Quality & Transport page (`ebf28ae`), Reaction System editor (`84450a8`), Water Age Sources (`f5e0d9b`), Heat Config, Initial Quality, species result attributes. Owed: 2D transport group, G6/G7. | `TRANSPORT_QUALITY_GUI_PLAN…md` §7 |
| **MSX in the GUI** | Handled only under the *reactions* name. No MSX species in `[INFLOWS]` / `InitialQualityDialog` (OPT §6 parity gap). The reaction editor **auto-registers** the component via `swmm_process_component_register` — the only `[PROCESS_COMPONENTS]` touchpoint in the GUI. | `reactionsystemeditordialog.cpp:40, :1102-1129` |
| **Component / plugin UI** | `[PLUGINS]` table (`PluginsTableModel`, dialog-owned, not project-scoped). Tools → Plugins is **read-only** (AA-2 owes enable/add). **No Process Components table**, no discovery of component libraries, no argument editor. `FileFilterRegistry` already registers `ComponentConfigRead` filters for `*.rxn *.ard *.lard *.heat *.age *.i2d`. | `simulationoptionsdialog.cpp:3717-3760`; `pluginsdialog.h:8-10`; `filefilterregistry.cpp:103-118` |
| **Engine registry / run modes** | No `EngineRegistry`, no `engines.json`; dispatch is `engineVersion.startsWith("5.")`; single unversioned legacy worker; engine combo disabled. Live 1D results by tailing the `.out` **done**; live 2D is in-process only. | `simulationrunner.cpp:70-113, :268`; `swmmvis.cpp:1924-1931` |
| **Process gating** | Six independent mechanisms (`IGNORE_*` boxes, object-count enables, `applyEngineConstraints`, `set2DRowEnabled`, quality-solver enables). No Domain × Process × Species matrix. Options have no model layer (key-by-key `swmm_options_get/set` in the dialog). | `simulationoptionsdialog.cpp:168, :429-442, :604-620, :1533, :3091-3115` |
| **HydroCouple in the GUI** | `HydroCoupleOgc` = WMS/WMTS/WFS basemap clients only. Not the coupling framework. | `cmake/FetchHydroCoupleOgc.cmake`; `addbasemapdialog.cpp:27-28` |

### A.3 HydroCouple, SDK, Composer, FVQual

| Item | State | Evidence |
|---|---|---|
| **Interfaces** | HydroCouple 2.0, header-only, Qt-free C++20, `HYDROCOUPLE_ABI_VERSION = 2`. `IModelComponentInfo::createComponentInstance()`, `IModelComponent` 15-state lifecycle, `ErrorEntry` queue, `Capability` set, `BufferDescriptor` hyperslabs, `IArgument` with `File`/`YAML` input types and `fileFilters()`, id-keyed items (`IIdBasedComponentDataItem`, `ITimeIdBasedComponentDataItem`), TIN items, distributed layer. **Unreleased: `WorkflowStatus` renumbered (breaking) — pin a commit.** | `hydrocouple.h:73, :354, :514, :545, :1603, :2147`; `CHANGELOG.md` |
| **Loader convention** | The standard defines **none**. Python loads `CreateComponentInfo` (no stamp). **Composer defines the contract**: `hydrocouple_component_abi_v1()` (C, stamp `hc-abi/1;iface=2;cxx=…;stdlib=…;bits=…`) + `hydrocouple_component_info_v1()`; legacy `CreateComponentInfo` accepted as "unstamped". Header is deliberately Qt-free "so it can be upstreamed into the SDK unchanged". Directories from `--components` / window; no manifest, scan + non-fatal skip. | `HydroCoupleComposer/include/plugins/componentabi.h`; `componentlibrary.cpp:184-237`; `componentregistry.cpp:45-105`; `HydroCouple/python/hydrocouple/loader.py:23` |
| **Composition format** | JSON **Composition Spec v1.1** owned by the SDK (`hydrocouplesdk/io/compositionspec.h`, *unverified*): `components[{id, info.component_info_id, arguments{literal | @from}, execution{mode}}]`, `connections[{from, to}]`, `writers`, `run.manifest`, `workflow{strategy…}`. Legacy `.hcp` XML is import-only. | `artifacts/headless/composition.json`; `tests/gui/test_compositiondocument.cpp:378-460`; `include/project/hcpimporter.h` |
| **Headless run** | `HydroCoupleComposer --run <composition> [-c dir]…` — same `ComponentRegistry`/`CompositionDocument`/`SimulationManager` as the window; progress every 50 steps on stdout; exit 0 only on success. No JSON progress protocol yet. `SimulationManager` creates a fresh component set per run and drives the SDK `ModelInitializer`. | `src/app/main.cpp:36-112, :127-153`; `include/simulation/simulationmanager.h:1-23` |
| **Composer plan status** | A, B (M1 headless verified), C1–C5 complete; B5 typed argument editors added 2026-08-25; **E5 FVQual editors gated on E6 "FVQual HydroCouple component not yet implemented"**. Python scripting plan: draft, not started. | `COMPOSER_MODERNIZATION_PLAN…md:3, :2269-2273` |
| **FVQual** | 3D hydrostatic FV receiving-water model (tri/quad + σ, UGRID NetCDF, `.rxn` MSX-convention kinetics), pinned `527b129`, `fvqual run` CLI. **No HydroCouple component yet.** | Composer plan `:78-89` |
| **HydroCoupleComponents / SDK** | *(unverified)* `meshgenerator`, `timeseriesprovider`, `smoke` (dlopen scaffold + ABI stamp) per the 524 plan; SDK 2.1.0 supplies abstract bases, `@from` bindings, staged initialization. Zero references to `HydroCoupleComponents` in any mounted repo. | `ENGINE_524…md:141-157` |

### A.4 Plan conflicts and their resolution

| # | Conflict | Resolution in this plan |
|---|---|---|
| 1 | **Component home.** PROCESS_COMPONENT plan: `openswmm.engine/src/couplers/hydrocouple/`. ENGINE_524 §B.4.3: `HydroCoupleComponents`. | **D-C2**: engine repo. `src/couplers/hydrocouple/` (6.x wrappers), `src/couplers/swmm5/` (5.x wrappers, 5.2.4 fetched from its pinned tag). `HydroCoupleComponents` keeps engine-independent components. The 5.2.4 pin then lives in one repo (retires 524 decision 9). |
| 2 | **Granularity / what the 2D component is.** D-UT9: `integrated2d` = surface + GW + transport, one config. ENGINE_524: `OpenSWMM2DComponent` = 6.x engine in `EXTERNAL_1D` host mode. | **They are the same component in two hosting modes** (§B.3). Id `org.hydrocouple.openswmm.integrated2d`; *embedded* mode shares the engine's `SimulationContext` (today's in-process path, unchanged numerics); *standalone* mode owns a 6.x engine opened with `EXTERNAL_1D` (524 §B.4.1) and serves the 524 exchange items. `OpenSWMM2DComponent` is retired as a name. |
| 3 | **Loader symbol.** Engine plans: `hydrocouple_component_info()`. Composer: stamped `hydrocouple_component_abi_v1` + `hydrocouple_component_info_v1`. | **Composer's convention is canonical** (§B.2). It already exists, is stamped, and accepts the Python legacy symbol. Upstream `componentabi.h` to the HydroCouple repo; engine `PluginFactory` dlsyms the same two names. |
| 4 | **SDK dependency.** PROCESS_COMPONENT D-PC2 (revised): implement interfaces directly, no SDK (assumed Qt). Composer targets SDK 2.1.0 and puts `CompositionSpec`/`ModelInitializer` there. | **D-C6**: engine, couplers and SWMMVis are **SDK-free by decision**, not by circumstance (interfaces + ABI header only). The *composition file* is still Spec v1.1: SWMMVis **writes** it as plain JSON (no SDK needed to emit), Composer reads it with the SDK. Anything the SDK would have supplied (abstract bases, argument helpers, staged init) is written minimally in `src/couplers/hydrocouple/` (§B.9). |
| 5 | **Direction A vs B ordering.** PROCESS_COMPONENT D-PC1: engine-as-component (A) first; hosting (B/HC5) "last, only if a deck needs it". Owner's ask: discovery from the `.inp` and GUI, FVQual-ready. | **D-C3**: the *discovery seam* (loader + `[PROCESS_COMPONENTS]` library rows + GUI table) goes first because it is small and unblocks every component, in-engine or Composer. Full **hosting semantics** (stepping a foreign component per routing step) is proven with an in-tree test component (§C.6) so the seam is ready for FVQual-class components without implementing FVQual (D-C5). Direction A follows in parallel. |
| 6 | **Status drift.** `UNIFIED_PLAN_STATUS_2026-08-29` says Phase 3 0/7; S1–S4 have landed since. | Part A above is the current stock-take; §C.7 requires each round to end by writing to this file's §C tables. |
| 7 | **Clock ownership.** 524 §B.3: the 1D component owns the clock, pull semantics. PROCESS_COMPONENT D-PC3: one routing step per `update()`. | Compatible: in Composer the 1D component is the puller and each `update()` is one routing step; in-engine, `SWMMEngine` is the clock and calls hosted bindings once per routing step (§B.5). |

---

## Part B — Target architecture

### B.1 One component model, three hosts

```
                          ┌──────────────────────── component libraries (.dylib/.so/.dll) ────────────────────────┐
                          │  hydrocouple_component_abi_v1()  +  hydrocouple_component_info_v1()  (§B.2)           │
                          │  openswmm.engine/src/couplers/*      HydroCoupleComponents/*      FVQual/*   3rd-party │
                          └───────────────┬──────────────────────────────┬──────────────────────────┬─────────────┘
                                          │ dlopen + stamp check         │                          │
   ┌──────────────────────────────────────▼───────────┐   ┌──────────────▼──────────────┐   ┌───────▼────────────────┐
   │ HOST 1: openswmm.engine (in-process)             │   │ HOST 2: SWMMVis (GUI)        │   │ HOST 3: Composer       │
   │  PluginFactory → PROCESS_COMPONENT               │   │  discovers through HOST 1's  │   │  ComponentRegistry     │
   │  [PROCESS_COMPONENTS] rows → registry → binding  │   │  C API; edits rows + config; │   │  composition.json      │
   │  built-ins: reactions, ard, heat, waterage,      │   │  runs HOST 1 in-process or   │   │  SimulationManager     │
   │  integrated2d (embedded)                         │   │  spawns HOST 3 headless      │   │  --run --progress-json │
   └──────────────────────────────────────────────────┘   └──────────────────────────────┘   └────────────────────────┘
```

Invariants:
1. **One binary per component, loadable by every host.** A component built once
   in the engine repo loads in the engine CLI, in SWMMVis (through the engine),
   and in Composer, with the same id, arguments and exchange items (D-UT2).
2. **One configuration file per component** (D-UT8), handed to it as its
   `File`-type `IArgument` named `config` in every host. The `.inp` row and the
   composition entry are two serializations of the same thing (§B.7).
3. **Results never cross the framework** (524 §B.3): the 1D side writes `.out`,
   `integrated2d` writes `.h5`, FVQual writes UGRID NetCDF; SWMMVis renders by
   tailing files. Exchange items carry *state*, not results.
4. **No exchange item bypasses the engine C API** (PROCESS_COMPONENT §4.2). If
   a wrapper needs state, the API grows a getter first.
5. **Search paths are one convention across hosts** (§B.2.3).

### B.2 Discovery contract (the seam D-C3 puts first)

#### B.2.1 ABI — adopt Composer's `componentabi.h`, upstream it

- Move `HydroCoupleComposer/include/plugins/componentabi.h` to
  `HydroCouple/include/hydrocouplecomponentabi.h` unchanged (it was written
  for that), Composer includes it from there. Stamp stays
  `hc-abi/1;iface=2;cxx=<family-major>;stdlib=<…>;bits=<…>`.
- A component library exports exactly `hydrocouple_component_abi_v1` and
  `hydrocouple_component_info_v1`; `HYDROCOUPLE_DECLARE_COMPONENT(InfoType)`
  emits both. `CreateComponentInfo` (Python legacy) is accepted and reported
  *unstamped* by every host, exactly as Composer does today.
- The `IComponentInfo` is library-owned; instances are host-owned and are
  destroyed before unload (Composer's ownership rule, now the engine's too).
- **The engine's IO-plugin symbol (`openswmm_plugin_info`) is untouched** in
  this program; putting IO plugins on a stamp is a separate, later item.

#### B.2.2 Engine loader (`PluginFactory`) — a second dlsym target

- `PluginType::PROCESS_COMPONENT` (PLUGIN_SDK.md planned extension; master
  plan §3.1). `scan_directory` keeps its filter; `load_library` tries
  `openswmm_plugin_info` first, then the stamp symbol → compare against the
  engine's own `HYDROCOUPLE_COMPONENT_ABI_STAMP` → on agreement
  `hydrocouple_component_info_v1`, else `CreateComponentInfo` (unstamped). A
  library with neither is skipped silently (today's rule); a library with a
  **mismatched stamp is refused with the two stamps in the message** and
  recorded as a load failure the C API can list.
- Discovered `IModelComponentInfo`s are indexed by `id()`; `kindOf` by
  `dynamic_cast` as Composer does (needs the HydroCouple `5ac202c`
  visibility fix — 524 plan "load-bearing build facts").
- HC1 linkage as written: `FetchContent` HydroCouple **pinned to a commit**,
  `OPENSWMM_WITH_HYDROCOUPLE` option (OFF by default until gate D passes on all
  three CI legs, then ON). When OFF, `[PROCESS_COMPONENTS]` library rows keep
  today's hard error, reworded to name the option.

#### B.2.3 Search paths — one convention for all hosts

| Source | Engine (`PluginFactory`) | Composer (`ComponentRegistry`) | SWMMVis |
|---|---|---|---|
| Next to the host library | `<enginedir>`, `<enginedir>/plugins`, `<enginedir>/components` (exists) | `<composer>/components` (add) | inherits engine's |
| Environment `HYDROCOUPLE_COMPONENT_PATH` (path-list) | add | add | add |
| Explicit | `swmm_component_search_path_add()` (new C API) | `--components <dir>` (exists) | Preferences → Component directories → passed to the engine at startup and to Composer as `--components` |
| Per-deck | `[PROCESS_COMPONENTS] … library="rel/or/abs/path"` resolved against the `.inp` directory | composition `components[].info.library` *(proposed; unverified whether Spec v1.1 has a library hint — if not, SWMMVis passes the deck dir as `--components`)* | — |

#### B.2.4 `[PROCESS_COMPONENTS]` row grammar (extends `TRANSPORT_IO_PLUGIN_CONFIG_PLAN` §2)

```
[PROCESS_COMPONENTS]
;;ComponentId                            Arguments (key="value")
org.hydrocouple.openswmm.reactions       config="model.rxn"                      ; built-in (today)
org.hydrocouple.openswmm.integrated2d    config="model.i2d"                      ; built-in, embedded mode
org.fvqual.fvqual                        config="bay.fvq" library="../fvqual/libfvqual.dylib"   ; external, explicit path
org.example.shade                        config="shade.cfg"                      ; external, found on the search path
```

Resolution order for token 0: (1) built-in registry id; (2) discovered
library id (search paths); (3) `library=` path if given; (4) **a bare path
as token 0 is accepted as shorthand** (`library=` with the id read from the
library) — this replaces today's hard error. Unknown id after all four →
error listing the search paths tried. Duplicate-id, pending-phase and
missing-`config` rules unchanged.

Every other `key="value"` maps to an `IArgument` of the same id on the
component (`IArgument::initialize(value, ArgumentInputType::String)`); `config`
maps with `ArgumentInputType::File`. Unknown keys are an open-time error
naming the component's argument list.

#### B.2.5 C API additions (`openswmm_process_components.h`)

```c
int swmm_component_search_path_add(const char* dir);              /* process-global */
int swmm_component_library_count(void);                            /* discovered infos, incl. failures */
int swmm_component_library_get(int i, SWMM_ComponentLibraryInfo*); /* id, caption, version, path, kind,
                                                                      stamp (or "unstamped"), load_error */
int swmm_component_info_argument_count(const char* id);
int swmm_component_info_argument_get(const char* id, int j, SWMM_ComponentArgumentInfo*); /* id, optional,
                                                                      input types, file filters, default */
int swmm_component_info_item_count(const char* id, int dir);      /* dir: 0 inputs, 1 outputs */
int swmm_component_info_item_get(const char* id, int dir, int j, SWMM_ComponentItemInfo*);
int swmm_component_info_capabilities(const char* id, unsigned* mask);
```
Argument/item enumeration instantiates the component
(`createComponentInstance()`), reads `arguments()/inputs()/outputs()` and
destroys it without `initialize()` — Composer B5 does the same for its typed
argument editors. Python/MCP bindings follow in the same round (the audit's
"no bindings" gap).

Built-in components (reactions, ard, heat, waterage) are **listed through the
same enumeration** with `kind = builtin`, one `config` argument each and an
empty item list, so the GUI has one source of truth for the Processes hub.
They do **not** become `IModelComponent`s in v1 (PROCESS_COMPONENT §3: they
are config appliers, not steppers).

### B.3 `integrated2d` — one id, two hosting modes (resolves conflict 2)

| | Embedded (in-process) | Standalone (Composer / headless) |
|---|---|---|
| Who steps it | `SWMMEngine` as today (`updateOutfallsPreRouting` → Picard conductances → `advancePostRouting`, `SWMMEngine.cpp:3517, :3637, :3756`). **Bypasses the binding**; numerics unchanged; gate S0 = bit-identical to today. | Composer pulls `update()`; inside, `swmm_2d_external_set_node_state` → `_external_step(dt)` → `_get_exchange/_get_outfall_head/_get_conductance` (524 §B.4.1, C-E1). |
| Engine instance | shares `SimulationContext` (27-member coupling, not separable — 524 §B.1) | owns a 6.x `SWMM_Engine` opened with `[2D_OPTIONS] EXTERNAL_1D YES`; runoff / routing / quality / `.out` skipped |
| Config | `model.i2d` (D-UT8) + the parent `.inp`'s `[2D_*]` sections until re-homed (LTS plan D-I5) | `config="model.i2d"`, `inp="full.inp"`, `h5="results.h5"` |
| Exchange items | served to *other hosted components* only (§B.5); the engine reads state directly | `node_head`, `node_depth`, `outfall_discharge` (in); `exchange_flow`, `outfall_head`, `conductance` (out) — `ITimeIdBasedComponentDataItem` keyed by node id, SI (524 §B.3); `cell_depth`, `cell_head`, `water_table`, `gw_sat_conc[s]`, `gw_temp_*` as `ITimeSeriesTINComponentDataItem` (PROCESS_COMPONENT §8) |
| Toggles | `SURFACE_ROUTING`, `GROUNDWATER`, `TRANSPORT_*`, `HEAT` in its `[2D_OPTIONS]`-lineage section (D-UT9) | same file, same toggles |
| Capabilities | — | `{}` in v1; `Checkpointing` **not claimed** until the hotstart record carries temperature and the σ-layer species blocks (PROCESS_COMPONENT §6 option b; GW plan §3.4 hotstart bump) |

The standalone façade is `src/couplers/hydrocouple/Integrated2dComponent.{hpp,cpp}`;
the embedded registration stays in `ProcessComponentRegistry` (the
`integrated2d` placeholder becomes real at G1 step 1). Both report the same
`IComponentInfo`.

### B.4 Groundwater flow + transport inside `integrated2d` (what this program builds)

Physics and numerics are **as specified in the two engine plans**; this
section only fixes what the componentization adds or changes.

| Topic | Source plan | Componentization delta |
|---|---|---|
| Two-zone explicit-LTS kernel, closures A/B, shared mesh, `PER_SUBCATCH` mode | LTS plan G1 (steps 1–8), G2 | Kernel state is owned by the component; `[2D_AQUIFER*]` sections re-home to `model.i2d` (D-I5) when the standalone façade lands, not before. |
| Track I-b `SUBCATCH_AQUIFER` (2D infiltration → legacy aquifer) | OPT §7, EXEC P6 | Independent of the kernel; ships ahead (EXEC order). |
| GW solutes (ARD, `R_f`, dispersion), MSX `SUBSURFACE` scope, age, heat with conduction, node exchange tuple, `BedExchange t_gr` closure, `addGwLoads` rewrite | GW transport plan T7.0–T7.5, D1–D11 | Node exchange tuple `(mass, age_volume, enthalpy)` becomes three exchange items in standalone mode: `node_quality_flux[s]`, `node_age_volume_flux`, `node_enthalpy_flux` (+ = into the node). **Owed on the 1D side:** `swmm_forcing_node_temperature` / `_age` (only `_quality` exists, `openswmm_forcing.h:175`). |
| Row layout authority `TransportPolicy` / `SpeciesRegistry` | OPT §5.2 (P1), GW plan T7.0 | Also the authority for the component's item list (`gw_sat_conc[s]` enumerates from it). |
| Results | GW plan §3.4/§5.6 (HDF5 per-zone UGRID variables) | Unchanged; SWMMVis tails the `.h5` in both modes (Phase L). |
| Legacy 1D aquifers alongside `integrated2d` | OPT §5.4 | A subcatchment belongs to one of: legacy aquifer, `SUBCATCH_AQUIFER` destination, or an `integrated2d` cell column — the engine refuses double-booking at open (new check, EXEC P6). |

### B.5 In-engine hosting of foreign components (Direction B, HC2a/HC5)

Proven with the in-tree test component (stream F); the first real consumer
(FVQual or another third-party component) is outside this plan (D-C5). Design
as in PROCESS_COMPONENT §3/§10 with these fixings:

- `ProcessComponentBinding { bind(ctx) ; step(ctx, dt) ; finish(ctx) }` wraps a
  loaded `IModelComponent`: `bind` = `initialize()` (arguments from the row) →
  `validate()` → link items → `prepare()`; `step` = push engine state into the
  component's linked inputs (bulk C API → `BufferDescriptor`, `Host`,
  rank 1), `update()`, pull linked outputs into the forcing API with `RESET`
  semantics (§4.3 rule); `finish` = `finish()`. Drain `errors()` after every
  call; `Fatal` fails the run with id + code + message (D-PC4 revised).
- **Linking rule v1 — by item id.** A hosted component's inputs/outputs whose
  ids match the engine catalogue (`NodeDepth`, `NodeHead`, `OutfallDischarge`,
  `NodeLateralInflow`, `NodeHeadBoundary`, `NodeQualityFlux[s]`,
  `ElementAirTemperature`, … — PROCESS_COMPONENT §4.3 ∪ 524 §B.3) are linked
  automatically; ids resolve against element names; a required input with no
  match is an open-time error. Arbitrary wiring and adapted outputs are **not**
  supported in-engine — that is what Composer is for (§B.6). (§E D-A5.)
- Clock: the engine is the clock; a hosted component that needs a larger step
  makes `update()` a no-op until due; one that needs a smaller step
  sub-cycles inside `update()` (PROCESS_COMPONENT risk 3, unchanged).
- Threading: bindings step on the engine's main thread after routing, before
  the output plugins' `update()`.

### B.6 Engine as a component (Direction A, HC3) and Composer orchestration

- `OpenSWMMModelComponent` (`src/couplers/hydrocouple/`, id
  `org.hydrocouple.openswmm.engine`): the 1D engine with `IGNORE_2D`, lifecycle
  per PROCESS_COMPONENT §5.1, `ITimeModelComponent`, error queue,
  capabilities `{Cloneable}` only (checkpoint debt §6). Its catalogue is the
  **union** of PROCESS_COMPONENT §4.3 and the 524 §B.3 coupling items
  (`node_head`, `node_depth`, `outfall_discharge` out; `exchange_flow`,
  `outfall_head`, `conductance` in — the last through the new
  `swmm_forcing_node_coupling_conductance`, 524 §B.4.1). This retires the
  separate `OpenSwmm1DComponent` name in 524 §B.4.3.
- `Swmm5Component` (`src/couplers/swmm5/`, one source → `Swmm524Component`
  static from the pinned tag, `Swmm530Component` linking
  `openswmm::legacy::engine`) exactly as 524 §B.4.3, including the standalone
  CLI speaking the worker protocol (decision 5 there).
- **Composition templates** (Spec v1.1 JSON) per 1D engine, generated by
  SWMMVis from the deck (§B.7), run with `HydroCoupleComposer --run
  <composition> --components <dirs> --progress-json`; the JSON-progress flag
  forwards `SimulationManager` progress plus each component's continuity as
  the worker protocol lines (524 §B.4.4). SWMMVis bundles the Composer binary
  next to the workers (524 decision 10).
- **FVQual-class components (not implemented here, D-C5).** The seams above
  are sized so that a receiving-water component like FVQual can later join
  as any other library: it would implement the ABI in its own repo (Composer
  E6), consume `outfall_discharge` / `node_quality_flux[s]` /
  `node_enthalpy_flux` at its boundary cells, and write its own result file
  (UGRID NetCDF) that SWMMVis does not need to read. The only item this plan
  owes toward that is that those three outputs exist on
  `OpenSWMMModelComponent` (A0/A1) and that the search-path / row grammar
  accept an external library (D2/D3).

### B.7 The `.inp` row ⇄ composition entry mapping (what lets the GUI generate compositions mechanically)

| `[PROCESS_COMPONENTS]` row | Spec v1.1 component entry |
|---|---|
| token 0 = id (or resolved from `library=`) | `"info": {"component_info_id": "<id>"}`; `"id"` = the row's id, suffixed on duplicates |
| `config="x"` | `"arguments": {"config": {"values": ["x"]}}` (path re-based to the composition's directory) |
| other `k="v"` | `"arguments": {"k": {"values": ["v"]}}` |
| built-in ids (reactions, ard, heat, waterage) | **not emitted** — they are applied inside the 1D engine component from the same `.inp` |
| `integrated2d` row + a mesh present | emitted as the standalone `integrated2d` entry with `inp="<full.inp>"`, `h5=…`; the 1D entry gets the compat `.inp` (5.x) or `IGNORE_2D` (6.x) |
| links | in-engine: item-id convention (§B.5); in the composition: the six 1D↔2D connections from the template + `@from` bindings for shared files (`mesh`), + any connections the user authored in Composer and saved back |

The composition is a **run artifact** written next to the `.inp` (transparent
IO, CLAUDE.md §4.1), never the source of truth for a SWMMVis project; the
`.inp` is. A composition edited in Composer that adds components the deck does
not name is imported back as new `[PROCESS_COMPONENTS]` rows (id + config
only; connections outside the item-id convention are kept in the composition
sidecar and flagged in the row as `composition="model.json"`).

### B.8 GUI (SWMMVis) — discovery, authoring, gating, running

MVC per CLAUDE.md §5.1: one project-scoped model per concern; every view is a
`QAbstractItemModel` client.

1. **`ProcessComponentRegistry` (project-scoped, `src/components/`)** — the
   model of the deck's `[PROCESS_COMPONENTS]` rows over
   `swmm_process_component_*`, plus the process-global discovered-library list
   over `swmm_component_library_*`. Replaces the reaction editor's private
   auto-registration (`reactionsystemeditordialog.cpp:1102-1129`), which
   becomes `registry->ensure(id, defaultConfig)`.
2. **Simulation Options → Files / Output / Plugins → Process Components
   table** (beside the `[PLUGINS]` table, same `PathBrowseDelegate`): columns
   Id (combo of built-ins + discovered, with kind badge and version), Config
   (browse, filters from `FileFilterRegistry::ComponentConfigRead` and from the
   component's own `IArgument::fileFilters()`), Arguments (generic key/value
   editor built from `swmm_component_info_argument_*`), Status (resolved /
   unstamped / load error text). "Add from library…" browses a
   `.dylib/.so/.dll` and fills the row (bare-path shorthand).
3. **Tools → Plugins (AA-2)** — gains a *Components* group: search paths
   (Preferences → Component directories), Rescan, per-library stamp / error,
   and the argument and item lists read-only. Enable/disable stays out of
   scope (a row in the deck is the enable).
4. **Processes & Modules hub** (OPT §3.2) reads its Domain × Process × Species
   matrix from the engine's `TransportPolicy` **and** lists non-built-in rows
   from the registry with their capabilities; a component's own editor opens
   by id (reactions → Reaction System editor; `integrated2d` → the 2D ›
   Groundwater page; anything else → the generic argument editor, plus
   "Open in Composer…" which writes the composition (§B.7) and launches
   Composer on it).
5. **GW authoring and results** — unchanged from the GUI plans: aquifer /
   `[GWF]` editors (AQUIFER plan G1–G6, *after* they compile), GG0–GG6, G-T7.a–d,
   EXEC P2–P8 (gating, page classes, MSX parity, CSV/hotstart seeds, Track
   I-b, GW authoring). The only additions: the Processes hub row for
   `integrated2d` shows its mode (embedded / standalone) and the Groundwater
   page is gated on the row being present.
6. **Run orchestration** — `EngineRegistry` + `engines.json` (MULTI_ENGINE V2)
   with `runMode: inprocess | subprocess | hydrocouple` (524 §B.4.5).
   Rule: 6.x with only built-ins → in-process; any deck with a non-built-in
   `[PROCESS_COMPONENTS]` row whose component is not hostable in-engine (§B.5
   not landed, or arbitrary wiring) → `hydrocouple`; 5.x + mesh →
   `hydrocouple`. Progress / continuity / warnings arrive through the worker
   protocol either way; results by tailing `.out` / `.h5` (live 1D done, live
   2D Phase L).

### B.9 Files (planned, by repo)

- **HydroCouple:** `include/hydrocouplecomponentabi.h` (moved from Composer).
- **engine:** `cmake/FetchHydroCouple.cmake`; `include/openswmm/plugin_sdk/IPluginComponentInfo.hpp` (+`PROCESS_COMPONENT`); `src/engine/plugins/PluginFactory.cpp` (second dlsym + stamp); `src/engine/plugins/ProcessComponentRegistry.{hpp,cpp}` (library resolution, `ProcessComponentBinding.hpp`); `include/openswmm/engine/openswmm_process_components.h` (+§B.2.5); `src/couplers/hydrocouple/{ComponentBase, ArgumentBase, IdBasedItem, TinItem, ErrorQueue}.{hpp,cpp}` (the minimal SDK-free bases, D-C6), `{OpenSWMMModelComponent, Integrated2dComponent, ExchangeCatalogue}.{hpp,cpp}`; `src/couplers/swmm5/…`; `src/engine/2d/subsurface/*` (LTS plan), `SubsurfaceTransport*` (GW plan §9); `include/openswmm/engine/openswmm_gw_transport.h`; `openswmm_2d.h` (+`swmm_2d_external_*`); `openswmm_forcing.h` (+`node_coupling_conductance`, `node_temperature`, `node_age`); `tests/components/smoke/` (the test double); `tests/unit/engine/test_process_component_{loader,binding,hosting}.cpp`; `test_2d_external_1d.cpp`.
- **gui:** `include/components/processcomponentregistry.h`, `src/components/…`; `include/ui/models/processcomponentstablemodel.h`; `include/ui/dialogs/componentargumenteditor.h`; `pluginsdialog` (AA-2); `include/core/engineregistry.h` + `resources/engines.json`; `src/simulation/compositionwriter.{h,cpp}`; plus the GW/transport GUI files already listed in the GUI plans.
- **Composer:** `src/app/main.cpp` (`--progress-json`); include the upstreamed ABI header; `ComponentRegistry` env-var path.
- **FVQual:** nothing (D-C5).

---

## Part C — Work plan (streams, phases, gates)

Streams run in parallel where their dependencies allow. Every phase names the
gate that closes it; every gate writes its artefacts to a tracked folder
(CLAUDE.md §4.1) and its result to §C.7 of this file.

### C.1 Stream D — Discovery seam (first, D-C3)

| Phase | Repo | Content | Gate |
|---|---|---|---|
| D0 | HydroCouple, Composer | Upstream `componentabi.h`; pin HydroCouple commit; Composer includes the upstream header; `HYDROCOUPLE_COMPONENT_PATH` in `ComponentRegistry`. | Composer test suite green; `test_component_loader` unchanged. |
| D1 | engine | HC1 FetchContent (pinned) + `OPENSWMM_WITH_HYDROCOUPLE`; compile-check TU of all headers in both option states, three CI legs; CI guard that no engine/gui target includes or links `hydrocouplesdk` (D-C6). | CI matrix green both states; guard fails on a planted SDK include. |
| D2 | engine | `PluginType::PROCESS_COMPONENT`; second dlsym + stamp; discovered-info index; §B.2.5 C API + Python/MCP bindings; `tests/components/smoke` (stamped) and a deliberately mis-stamped twin. | **Gate D-a:** smoke loads from all three search-path sources; mis-stamped twin refused with both stamps in the message; unstamped `CreateComponentInfo` library reported "unstamped". |
| D3 | engine | `[PROCESS_COMPONENTS]` §B.2.4 grammar: library rows resolve (retire the hard error), bare-path shorthand, unknown-key error, argument mapping. | **Gate D-b:** a deck naming a nonexistent library fails at open with the dlopen text (PROCESS_COMPONENT §10 falsifier); corpus untouched (bit-identical `.out`/`.rpt`). |
| D4 | gui | `ProcessComponentRegistry` model; Process Components table; reaction editor rerouted; AA-2 Components group; Preferences component dirs → engine + Composer. | **Gate D-c:** no-edit round trip `n == 0`; add-row → engine row → file → reopen; smoke library visible in Tools → Plugins with stamp; `test_processcomponents_table.cpp`. |
| D5 | all | Docs: PLUGIN_SDK.md ("planned extension" → reference), manual chapter, CHANGELOGs. | — |

### C.2 Stream A — Engine as a component, Composer coupling

| Phase | Content | Depends | Gate |
|---|---|---|---|
| A0 | Owed getters/forcing: `swmm_heat_get_bed_temperature{,_bulk}`; `swmm_forcing_node_coupling_conductance`; `swmm_forcing_node_temperature` / `_age`; `[2D_OPTIONS] COUPLING_CONDUCTANCE YES|NO`. | — | unit tests; corpus bit-identical |
| A1 (HC3) | `OpenSWMMModelComponent` with the union catalogue (§B.6), `{Cloneable}` only. | D2, A0 | **Gate A-a:** Composer loads it, runs a corpus deck headless, `.out` bit-identical to CLI. |
| A2 (HC4.2) | openswmm ⇄ `HTSComponent` composition vs H6b internal. | A1, HydroCoupleComponents mounted | band recorded, not tuned |
| A3 | Composer `--progress-json`; SWMMVis `EngineRegistry` + `runMode: hydrocouple` + composition writer (§B.7) + spawn; job row parity with the worker path. | A1, D4 | **Gate A-b:** from SWMMVis, a 6.x deck run through Composer renders live 1D exactly as in-process. |
| A4 (HC4.1/4.3) | ⇄ `GWComponent` (node Darcy exchange — the external reference for G1), ⇄ `RHEComponent`. | A1 | recorded bands |

### C.3 Stream G — `integrated2d`: kernel, transport, standalone mode

| Phase | Content | Depends | Gate |
|---|---|---|---|
| G-P | Land the pending 2D state: S5 (`CELL2D` heat storage); resolve D-N1 tier-ceiling risk (eleven fixed-array sites, two GPU OOB); **a 2D deck in the corpus** (status doc: none today). | — | `run_corpus.sh` includes a 2D deck |
| G1 | Two-zone explicit-LTS kernel steps 1–8 (LTS plan); `integrated2d` registry placeholder becomes real (embedded); `[2D_AQUIFER*]` in `.inp` for v1 (D-I5). | G-P; T7.0 in parallel | LTS plan gates G-A / G-B; GROUNDWATER OFF bit-identical |
| T7.0 | `TransportPolicy` row authority (OPT P1); `SUBSURFACE` scope token; `[GW_*]` parsers/writers; `openswmm_gw_transport.h` stubs. | OPT P1 | GW plan gate 9 (round trips) |
| T7.1–T7.5 | GW solutes, MSX, age, heat, `PER_SUBCATCH` migration, hotstart + HDF5 (GW plan §8). | G1 | GW plan gates 1–8 |
| G-S (= 524 C-E1) | `EXTERNAL_1D` host mode + `swmm_2d_external_*`; `Integrated2dComponent` standalone façade with the 524 item set + TIN items; gate-1 harness. | G1 (for GW items; surface-only can start after A0) | **Gate S0:** embedded ≡ today bit-identical. **Gate S1 / S1b (= 524 gates 1, 1b):** standalone driven by the harness, then by Composer with `OpenSWMMModelComponent`, bit-identical `.h5` + mass balance to native. |
| G2 | Kernel closure B, wells/boundaries per LTS plan; item list grows accordingly. | G1 | LTS plan gates |

### C.4 Stream U — GUI groundwater / transport authoring and results

| Phase | Content | Depends | Gate |
|---|---|---|---|
| U0 (= EXEC P0) | **Compile, verify, commit the untracked aquifer / `[GWF]` work** (incl. the `AquiferRegistry::remove` → `swmm_aquifer_delete` fix); land `test_simulationoptions_roundtrip`; commit the engine's dirty `[GWF]` API. | — | AQUIFER handoff steps executed with a compiler; ctest green |
| U1 (= EXEC P2–P3) | Process gating engine + hub + 2D Processes page; page classes / tree sidebar. | D4 (hub reads the registry) | EXEC gates |
| U2 (= EXEC P4–P5) | MSX parity (`[INITIAL_QUALITY]`, `[INFLOWS]` CONCEN/MASS for MSX species), CSV sidecar, hotstart species block. | — | EXEC gates |
| U3 (= EXEC P6, GG0) | Track I-b `SUBCATCH_AQUIFER`; per-cell infiltration editors. | — | EXEC / GG0 gates |
| U4 (= EXEC P7, G-T7.a–c) | GW transport authoring surfaces (options group, IQ Groundwater tab, aquifer-transport columns, edge BC quality, Sources & Wells). | T7.0 | GW plan gate 9 |
| U5 (= EXEC P8, GG1–GG3) | Groundwater page, `GW_ET` / `AQUIFER_2D`, aquifer params editor, water-table sublayer. | G1 | GG gates |
| U6 (= EXEC P9, GG4–GG6, G-T7.d) | Results: per-zone species / temperature / age layers, σ-column inspector, plots; live 2D (Phase L). | T7.5, L-E3/L-G7 | 524 gate 5 (live) |

### C.5 Stream X — 5.x engines through HydroCouple (524 Part B, amended)

Unchanged in content; amended homes and names: `Swmm5Component` in
`openswmm.engine/src/couplers/swmm5/`; `OpenSWMM2DComponent` → `integrated2d`
standalone; `OpenSwmm1DComponent` → `OpenSWMMModelComponent`. Sequence
C-E2 → C-H2 → C-H3 → C-G1 as written, with C-H1 replaced by G-S and C-H3's
templates generated by §B.7. Gates 2–6 of the 524 plan unchanged; gate 4
(5.2.4 parity) after backports 7–8. Prereqs M-1 → M-2 → M-3 stand.

### C.6 Stream F — Third-party hosting, proven with a test component (FVQual excluded, D-C5)

| Phase | Content | Depends | Gate |
|---|---|---|---|
| F1 | In-tree test component `org.hydrocouple.openswmm.test.sink` (`tests/components/sink/`, built as a real library): arguments `config` + `scale`; inputs `outfall_discharge`, `node_quality_flux[s]`, `node_enthalpy_flux`; output a scalar time series of received volume/mass/energy; writes a CSV. It is the stand-in for an FVQual-class receiving-water component. | D2 | loads in all three hosts (gate D-a) |
| F2 | Composition `OpenSWMMModelComponent → sink` generated by SWMMVis ("Open in Composer…" / `runMode: hydrocouple`). | A1, A3, F1 | **Gate F-a:** received totals equal the 1D outfall totals from the `.out` / `.rpt` to 1e-10; the composition round-trips through Composer save → SWMMVis import (§B.7). |
| F3 (HC2a/HC5) | In-engine hosting: `ProcessComponentBinding`, item-id linking, error drain; the sink as a `[PROCESS_COMPONENTS]` row run by the engine CLI and by SWMMVis in-process. | D3, F1 | **Gate F-b:** in-engine totals ≡ Composer totals (F-a) bit-for-bit; doubling-test binding observable via C API; nonexistent library → dlopen text. |
| F-out | **FVQual component, compositions, Composer E5/E6 editors** | — | **Not in this plan.** Tracked in `COMPOSER_MODERNIZATION_PLAN` E5/E6 and the FVQual repo; the gates above are what make it a drop-in later. |

### C.7 Sequencing and critical paths

```
D0 → D1 → D2 → D3 → D4 → D5                                     (discovery; ~3–4 weeks)
A0 → A1 → A3 ─────────────────────────────┐
                                           ├→ F2 → F3
F1 (test sink component, after D2) ────────┘
G-P → G1 → T7.1..T7.5 → G-S(GW items) ; G-S(surface-only) after A0  → S1/S1b → X: C-H2 → C-H3 → C-G1
U0 → U1(D4) → U2 → U3 → U4(T7.0) → U5(G1) → U6(T7.5)
```

Critical path to "GW transport visible in SWMMVis": **G-P → G1 → T7.1–T7.5 →
U6** (kernel-bound; the GW transport plan's own estimate is ~24 engine days
after G1). Critical path to "any external component discoverable from a deck
and the GUI": **D0–D4** (independent of the kernel). Critical path to "a third-party
receiving-water component coupled from SWMMVis" (the FVQual-shaped case, with
the test sink standing in): **D2 → A1 → A3 → F2** (Composer path; no engine
hosting needed).

Each round ends by updating the tables in this section (status-drift rule,
A.4 #6).

---

## Part D — Verification gates (consolidated)

| Gate | Proves | Artefacts |
|---|---|---|
| D-a / D-b / D-c | one binary, three hosts; stamp refusal; deck failure text; GUI round trip | `openswmm.engine/tests/output/process_components/`, `openswmm.gui/tests/gui/data/` |
| A-a / A-b | framework adds nothing to a 1D run; SWMMVis-through-Composer ≡ in-process (live 1D) | `openswmm.engine/verification/hydrocouple/`, gui handoff |
| S0 / S1 / S1b | embedded ≡ today; standalone ≡ native; Composer ≡ harness (bit-identical `.h5`) | `tests/output/2d_external_1d/`, `verification/openswmm_1d2d/` |
| G-A / G-B, T7 gates 1–9 | kernel conservation across tiers; Ogata–Banks, Domenico, Carslaw–Jaeger, batch-reactor parity, tuple closure 1e-10, `PER_SUBCATCH` migration, GROUNDWATER OFF bit-identity | `tests/benchmarks/manufactured/gw-transport-*/`, `tests/output/` |
| X 2–6 | three-way corpus, 5.2.4 parity, GUI job rows, Windows SWMR | per 524 §B.6 |
| F-a / F-b | test sink coupled via Composer; hosted in-engine bit-identical | engine `tests/output/hosting/`, gui handoff |

---

## Part E — Decisions required

### E.1 New in this plan

| # | Decision | Recommendation |
|---|---|---|
| D-A1 | Canonical home of the component ABI header | `HydroCouple/include/hydrocouplecomponentabi.h` (moved from Composer, unchanged); the engine avoids the SDK, so the header cannot live only there. |
| D-A2 | Search-path convention incl. `HYDROCOUPLE_COMPONENT_PATH` | Adopt §B.2.3 in all three hosts. |
| D-A3 | `[PROCESS_COMPONENTS]` grammar: id-first with optional `library=`; bare path as shorthand | Yes (§B.2.4). |
| D-A4 | Engine couplers and SWMMVis SDK-free | **Settled by D-C6** — permanent; the small base classes live in `src/couplers/hydrocouple/`. |
| D-A5 | In-engine linking by item-id convention only; arbitrary wiring via Composer | Yes for v1; a `[COUPLINGS]` section is a later slice if a deck needs it. |
| D-A6 | `integrated2d` embedded stepping bypasses the binding (numerics unchanged) | Yes; S0 guards it. |
| D-A7 | Capabilities: claim `Cloneable` only; `Checkpointing` after the hotstart carries temperature + σ-layer species | Yes (PROCESS_COMPONENT §6 option b). |
| D-A8 | Built-in four stay config-appliers, listed but not `IModelComponent`s | Yes (PROCESS_COMPONENT §3). |
| D-A9 | Composition is a run artifact next to the `.inp`; `.inp` stays the source of truth; Composer-side additions import back as rows + `composition=` sidecar | Yes (§B.7). |
| D-A10 | Hosting is proven with the in-tree test sink; FVQual and any other real third-party component are out of scope (D-C5) | Yes. |
| D-A11 | Retire names: `OpenSWMM2DComponent` → `integrated2d` standalone; `OpenSwmm1DComponent` → `OpenSWMMModelComponent` | Yes. |
| D-A12 | `OPENSWMM_WITH_HYDROCOUPLE` default | OFF until gate D-a passes on all CI legs, then ON. |

### E.2 Carried forward, still open (settle in the same review)

| Source | Items | Recommendation here |
|---|---|---|
| 524 §B.8 | 1 HydroCouple only shipped 5.x coupling; 2 seal via API; 3 outfall restore; 4 5.3.0 conductance hook; 5 M-1 worker first; 6 SWMR first; 7 keep native 6.x; 8 transport out of scope for 5.x; 9 pin; 10 Composer `--run` | Accept 1–3, 5–7, 10 as recommended there; **4 defer**; **8 amended:** transport tuple items exist in the 6.x component, 5.x gets `NODE_POLLUTANT_LATMASS_FLUX` only when a case needs it; **9 retired** by D-C2. |
| GW transport §10 | D1–D11 | Accept all recommendations as written. |
| OPT §9 | 1–7 | Accept recommendations (EXEC already assumes them); item 6 (Track I-b ahead) = U3. |
| LTS plan | D-N1 tier ceiling; 2D corpus deck | Resolve in G-P before G1. |
| MULTI_ENGINE V2 §5 | worker ordering, FP flags, block 5.2.4 until backports, sibling-dir name | Unchanged; needed for stream X only. |
| PROCESS_COMPONENT | D-PC1 (A first) | **Amended by D-C3:** discovery seam first, A in parallel, hosting on demand. |
| Composer | E6 (FVQual component), `--progress-json`, license direction (openswmm.gui GPL → reimplement, never copy) | E6 stays in the Composer plan, **not here** (D-C5); `--progress-json` = A3; note the ABI header moves the *other* way (Composer → HydroCouple), which is fine if Composer's own license permits; **confirm**. |

---

## Part F — Risks

| Risk | L | Mitigation |
|---|---|---|
| C++ ABI across images (same-toolchain requirement; RTTI across images) | Med | Stamp refusal before any C++ call; HydroCouple `5ac202c` visibility fix; CI builds smoke with the same toolchain; C-API shim fallback (D-PC5) |
| Planning GW transport against an unwritten kernel | High | Only §3.4 SoA layout, the G-B accumulator table and step 20 are binding (GW plan §11); T7.0 and GUI authoring bind to sections/API only |
| Kernel-bound critical path stalls the whole GW story | High | D-C3: discovery, Direction A, FVQual and all authoring UIs proceed without it; Track I-b gives users 2D infiltration → aquifer early |
| `.inp` ↔ composition drift | Med | Composition is generated, never hand-maintained for SWMMVis projects; import-back limited to rows + sidecar |
| Status drift across six plans | Certain | §C.7 is the single status table; a round is not closed until it is written here |
| Two `SimulationContext` owners in standalone mode (1D engine + `integrated2d`'s own engine) | Med | Process-global 5.x is one per process; two 6.x instances are already supported; gate S1b measures overhead |
| Hotstart / checkpoint debt visible through capabilities | Med | D-A7 |
| Uncompiled GUI code in the tree (aquifer work, 159 dirty entries) | High | U0 first; nothing in stream U starts on top of uncompiled code |
| SDK creeping back in through "convenience" (bases, `ModelInitializer`, `CompositionSpec` writer) | Med | D-C6; CI asserts no `hydrocouplesdk` include or link in engine/gui targets; SWMMVis's composition writer is a schema-validated JSON emitter with a fixture test against Composer's own reader (gate F-a round trip) |
| Unverified repos (SDK, Components, FVQual, 5.2.4) | Med | Mount them for the next pass; §A.3 claims marked *(unverified)* are re-checked before D0 |
| Scope creep into hosting semantics or into FVQual itself | Med | F3 limited to the test sink; D-A5 keeps v1 linking trivial; D-C5 excludes FVQual work |

---

## Appendix — Crosswalk (what this document amends)

| Plan | Section | Amendment |
|---|---|---|
| ENGINE_524 | §B.4.3 component home; names `OpenSWMM2DComponent`, `OpenSwmm1DComponent`; decision 9 | D-C2, D-A11; decision 9 retired |
| PROCESS_COMPONENT v3 | D-PC1 ordering; §10 `hydrocouple_component_info()`; §12 "HC5 last" | D-C3; §B.2.1 symbol names; HC5 proven with the test sink (F3), real consumers out of scope (D-C5) |
| COMPOSER_MODERNIZATION | E5/E6 (FVQual) | untouched; explicitly **not** pulled into this plan (D-C5) |
| PLUGIN_SDK.md | "Planned extension" note | becomes the §B.2.2 reference after D2 |
| TRANSPORT_IO_PLUGIN_CONFIG_PLAN | §2 row grammar | §B.2.4 (library rows, shorthand, argument mapping) |
| UNIFIED_PLAN_STATUS_2026-08-29 | Phase 2 "refused", Phase 3 "0/7" | superseded by §A.1 |
| MULTI_ENGINE V2 | manifest | `runMode: hydrocouple` + composition template path (as 524 §B.4.5) |
| GW transport plan, LTS plan, OPT / EXEC, GG, AQUIFER plans | — | unchanged; sequenced in §C |
