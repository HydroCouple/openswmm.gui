# SWMM 5.2.4 / 5.3.0 in SWMMVis — implementation status + 1D↔2D coupling via HydroCouple (2026-09-04)

**Status:** Part A is an audit (facts, verified against the repos on
2026-09-04). Part B is a **DRAFT FOR REVIEW** plan; nothing in Part B is
implemented, and it needs the §B.8 decisions before any code is written
(CLAUDE.md §5.0).

**Direction set by the owner on 2026-09-04:** both legacy engines (5.3.0 and
5.2.4) must be couplable to the 2D surface solver, and **all 1D↔2D coupling
is eventually to go through HydroCouple**. Part B is written to that end
state: the near-term work is the HydroCouple components themselves, and the
only non-HydroCouple coupling driver is a test harness inside the engine repo.

Parent plans (every decision there stands unless changed here):
- `MULTI_ENGINE_VERSION_SUPPORT_PLAN_2026-08-01.md` (base) and its
  `..._V2_2026-09-03.md` amendment (M-1 … M-6 sequencing).
- `LIVE_1D_RESULTS_PLAN_V2_2026-09-03.md` (live results by tailing the `.out`).
- `HydroCoupleSDK/plans/PROVIDER_PIPELINE_PLAN_2026-09-03.md` (components live
  in `HydroCoupleComponents`; `@from` bindings; Composer v2 execution panel).

Repos: `openswmm.gui` @ `1aed4df` (swmm6_gui), `openswmm.engine` @ `801b57f1`
(swmm6_rel), `swmm5.2.4/openswmm.engine.5.2.4` @ `0bd9f3d2` (build-v5.2.4),
`HydroCouple`, `HydroCoupleSDK`, `HydroCoupleComponents`, `HydroCoupleComposer`.

---

## Part A — Where the program stands

### A.1 Live rendering (LIVE_1D_RESULTS_PLAN_V2) — DONE except the manual acceptance

| Phase | State | Evidence |
|---|---|---|
| E1 writers flush per report period (6.x + legacy 5.3.0) | **DONE** — engine `10479a69` | `src/engine/plugins/DefaultOutputPlugin.cpp:216` `std::fflush`; `src/legacy/engine/output.c:520` `fflush(Fout.file)`. Flush timing ≤ noise (`tests/artifacts/live_1d/flush_timing.csv`). |
| E2 `OutputReader::openLive/refresh` + C API | **DONE** — engine `10479a69` | `include/openswmm/engine/openswmm_output.h:176,190,196` `swmm_output_open_live / _refresh / _is_live`; `test_engine_output_reader_live` green (targeted ctest 16/16). |
| G1 `SWMMResultsLayer::openResultsLive / refreshLive / periodsAppended` | **DONE** — gui `92bf1fd` | `src/layers/swmmresultslayer.cpp`. |
| G2 runner tick → early open → finish-in-place | **DONE** — gui `92bf1fd` (deviation: rides the existing `progressChanged`, no `resultsFileGrew` signal) | `src/swmmvis.cpp:3219` `tickLive1DResults`, `:8052` tick wiring, `:8125` finish handler adopts the footer in place. |
| G3 `ComparisonPlotDialog::appendChartTails` | **DONE** — gui `92bf1fd` | ctest `profilebuilder|comparisonplot` 11/11. |
| G4 `ProfileSourceFetcher::appendTail` + `ProfileBuilder::appendPeriods` | **DONE** — gui `92bf1fd` | `AppendPeriods_MatchesFullComputeExactly` green. |
| G5 end-to-end parity (both engines, manual smoke + CSV prefix check) | **NOT DONE** | `LIVE_1D_RESULTS_VERIFY_HANDOFF_2026-09-03.md` Steps 3–4 have no Results entry. Owed. |
| G6 preference + toolbar toggle + CHANGELOG | **DONE** | `swmmvis.cpp:1518` "Live 1D" checkbox; `PreferencesManager::liveResults1DEnabled`; CHANGELOG entry (line 22). |

Two housekeeping facts: (1) the working tree carries an **uncommitted CHANGELOG
edit** that changes the documented checkbox label from `"Live 1D"` to `" 1D"`
while the code still says `tr("Live 1D")` — looks accidental, verify before
committing. (2) Both parent plan documents are untracked by decision
(gui `0d1c59b` "untrack the live-1D / multi-engine plan documents"); they show
as `??` in `git status`.

**Consequence for 5.x and for HydroCouple:** the live mechanism is
engine-agnostic *and orchestrator-agnostic* — the GUI tails whatever `.out`
(and, after Phase L, whatever `.h5`) a run writes. A 5.2.4 run renders live
the moment its worker exists and its writer flushes per period (V2 plan §2.3);
a HydroCouple-driven run renders live the same way. Nothing on the live side is
waiting on 5.x or HydroCouple work.

### A.2 5.2.4 as a selectable engine (MULTI_ENGINE V2, items M-1 … M-6) — only step 0 done

| Item | State | Evidence (2026-09-04) |
|---|---|---|
| Phase 1 step 0 — 5.2.4 build modernisation | **DONE** | `build-v5.2.4` @ `0bd9f3d2` = `359ca277` (presets, vcpkg, GoogleTest, OpenMP gate) + two CI fixes (`2c76170b`, `0bd9f3d2`). `CMakePresets.json`, `vcpkg.json`, `.github/workflows/build-and-test.yml` (4-leg matrix, triggers only on `build-v5.2.4`), `tools/verify_524_parity.sh` — last local run **154 passed, 0 failed, 0 skipped** (`verification/parity_run.log`, `.out` byte-identical stock vs branch). Local, untracked: `verification/524_parity/{stock,branch,report.txt}`, `verification/smoke/`, `.parity/stock-v5.2.4/`, `workplans/HANDOFF_BUILD_MODERNIZATION_2026-08-16.md`. |
| M-1 minimal worker (`src/worker/`) + step 4b `fflush` + tag `v5.2.4-swmmvis.1` | **NOT STARTED** | no `src/worker/` on the branch; `grep fflush src/solver/output.c` → nothing; tags are `v5.2.3 v5.2.4 v6.0.0-alpha.{1,2,3}` only. |
| Phase 1 steps 1–4 backports (unknown section/option skip, warning callback, running mass balance) | **NOT STARTED** | `src/solver/include/swmm5.h` exports exactly the stock 20 (`swmm_run … swmm_decodeDate`); `project.c:463-652` still `ERR_KEYWORD` (fatal) on unknown options. |
| M-2 GUI cmake: `EngineVersions.cmake`, `LegacyEngineWorker.cmake`, two versioned workers, manifest | **NOT STARTED** | `cmake/` holds `CopyVcpkgLibs, FetchHydroCoupleOgc, FetchOpenSWMMEngine, FetchQPropertyModel, FindOpenMP` only; bundle blocks still copy the engine install's single `openswmm-legacy-worker`. |
| M-3 `EngineRegistry` + `engines.json` + dispatch + gating | **NOT STARTED** | `src/simulation/simulationrunner.cpp:268` `useLegacy = engineVersion.startsWith("5.")`; `:70-76` single hard-coded worker name; "5.2.4" appears only in a doc comment (`simulationoptionsdialog.h:78,328`). |
| M-4 CI: fetch/build/bundle 5.2.4, gates | **NOT STARTED** | `.github/workflows/build_and_test.yml` knows only `OPENSWMM_ENGINE_REF: swmm6_rel`; gates at `:583-590` and `:800` assert the single unversioned worker. |
| M-5 Phase 4 compat `.inp` write (engine `InpWriter` profile + GUI run artifact) | **NOT STARTED** | no `InpWriteOptions` / compat profile in `src/engine/core/InpWriter.hpp` or the C API. |
| M-6 docs + CHANGELOGs | **NOT STARTED** | — |
| V2 §5 decisions (reordering, FP flags, continuity "—", sibling-dir name) | **OPEN** | unanswered in the plan. |

**Critical path to "5.2.4 selectable in the status bar and rendering live":**
M-1 → M-2 → M-3, plus Phase 4 compat write, which is *required* (not optional)
until backports 1–2 land, because the GUI's writer emits v6-only sections
(`[USER_FLAGS]`, `[PLUGINS]`, `[2D_*]`) that stock 5.2.4 rejects.

### A.3 What a user sees per engine, today vs. after the critical path

| Capability during a run | 6.x | 5.3.0 | 5.2.4 today | 5.2.4 after M-1..M-3 + Phase 4 |
|---|---|---|---|---|
| Selectable in the engine combo | yes | yes | **no** | yes |
| Live 1D map animation + profile/comparison plots | yes | yes | — | yes |
| Running continuity in the job row | yes | yes | — | "—" until backport step 4 |
| Warnings streamed to the log | yes | yes | — | `.rpt` only until step 3 |
| v6-flavoured `.inp` accepted | yes | yes | — | via compat write (skip+warn after steps 1–2) |
| 2D surface coupling | yes (native, in-process) | **no — Part B (HydroCouple)** | — | **no — Part B (HydroCouple)** |
| Pause/resume, plugins, dynamic slot | yes | no | — | no |

---

## Part B — 1D↔2D coupling for 5.3.0 and 5.2.4, via HydroCouple (DRAFT FOR REVIEW)

### B.0 Goal and end state

Run a meshed SWMMVis model with **SWMM 5.3.0 or SWMM 5.2.4 as the 1D solver**
and the OpenSWMM explicit marcher as the 2D solver, selectable from the GUI,
with post-run and live 2D results — where the coupling is a **HydroCouple
composition** of a 1D component and a 2D component. The same composition with
the 6.x 1D component is the instrument that proves the framework path against
the native in-process coupling, and the seam through which "all 1D↔2D via
HydroCouple" is reached later.

Hard constraints carried over: (1) 5.2.4 numerics stay **bit-identical** to
EPA (public API only; backports are IO/API-only); (2) 5.3.0's parity corpus
against 6.x stays green (its source is in-tree and already modified, so
API-only additions are allowed, solver-path changes need a decision);
(3) **one 2D solver** — the marcher in `openswmm.engine`, not a port.

### B.1 Facts the design rests on (verified 2026-09-04)

**6.x coupling contract** (`src/engine/core/SWMMEngine.cpp`):
- B2c pre-routing: `surface_router_.updateOutfallsPreRouting(ctx_)` (`:3517`) — coupled outfall depth = max(standard, 2D head).
- Inside the dynamic-wave Picard loop: `computeCouplingConductances` → `dw.nodeSumDqdh(gn) += G` (`:3633-3636`), EXPLICIT node continuity only — the "windowless coupling stabilizer".
- B3+ post-routing: `advancePostRouting(ctx_, dt_routing, current_time)` (`:3756`) — orifice exchange from the **frozen post-step 1D heads** (`nodes.head/depth/invert_elev/full_depth`, ft→m via `len_1d_to_2d`), 2D co-advance over `[t, t+dt]` (sync batch ≤ `clamp(MAX_TIMESTEP, routing_step, 60)` s; longer spans falsified in `DECOUPLING_VIABILITY_STUDY_2026-07-30.md`), exchange delivered back as `nodes.coupling_inflow` (a rate over `coupling_delivery_remaining`, `:7740-7849`), and `coupled_node[]` changing ponding/surcharge handling in `DynamicWave.cpp:1471, 3544, 3865`.
- The router + coupling + API + 2D output plugin read **27 distinct `SimulationContext` members** — not separable from the context without a large refactor. Hence the 2D component wraps the *engine* in a host mode, not the router.

**5.3.0 runtime surface** (`src/legacy/engine`, dylib `libopenswmm.legacy.engine.6.0.0.dylib`, 40 exports):
`swmm_open/start/step/stride/end/report/close`, `swmm_run_with_callback`,
`swmm_getValue/setValue` **and** `swmm_getValueExpanded/setValueExpanded`,
`swmm_setWarningCallback`, `swmm_getRunningMassBalErr`, `swmm_useHotStart/saveHotStart`,
`swmm_getNodeStats/…`. Setters that matter (`swmm5.c`): `swmm_NODE_LATFLOW` (`:1412, :2328`),
`swmm_NODE_HEAD` → `setOutfallStage`, **permanently** `FIXED_OUTFALL` (`:2273, :2338`),
**pre-start** `swmm_NODE_SURCHARGE_DEPTH` (`:2304`) and `swmm_NODE_PONDED_AREA` (`:2312`),
`swmm_NODE_POLLUTANT_LATMASS_FLUX` (`:2276`, a future transport item),
`swmm_GAGE_RAINFALL / _SCALEFACTOR`, `swmm_ROUTESTEP`. Dynamic-wave node
denominator: `Xnode[i].sumdqdh` (`dynwave.c:79, :722`) — the place a
conductance hook would go.

**5.2.4 runtime surface** (`src/solver/include/swmm5.h`, `swmm5.c`, 20 exports):
`swmm_step/stride`; getters `swmm_NODE_{TYPE,ELEV,MAXDEPTH,DEPTH,HEAD,VOLUME,LATFLOW,INFLOW,OVERFLOW}`,
`swmm_{ROUTESTEP,CURRENTDATE,ELAPSEDTIME,FLOWUNITS}`, `swmm_getIndex/getName/getCount`;
setters `swmm_NODE_LATFLOW` → `Node.apiExtInflow` (cfs, summed every routing step in
`routing.c:450`, sign unchecked), `swmm_NODE_HEAD` → `setOutfallStage` (permanent FIXED),
`swmm_GAGE_RAINFALL`, `swmm_LINK_SETTING`, `swmm_ROUTESTEP`. **No** surcharge-depth /
ponded-area setters, **no** DW hook, **no** outfall-type restore.

**Process/link facts:** the 5.3.0 dylib (40 exports) and the 6.x dylib (2,197
exports) share **zero** symbol names; the 6.x dylib exports none of the 20
5.2.4 API names → one process can host a 5.x engine and the 6.x engine
together, which is what an in-process HydroCouple composition needs. Both
5.x engines are **process-global** (one instance per process).

**HydroCouple facts:** interfaces in `HydroCouple/include/hydrocouple*.h` —
`IModelComponent`, `ITimeModelComponent`, `IInput / IOutput / IMultiInput / IAdaptedOutput`,
`IArgument`, and the **id-keyed exchange items** `IIdBasedComponentDataItem` /
`ITimeIdBasedComponentDataItem` (node-name-keyed exchange with no index mapping).
SDK (`HydroCoupleSDK`) supplies the abstract bases, `@from` argument bindings and
staged initialisation (provider-pipeline P1). Components live in
`HydroCoupleComponents` (P2 decision): today `meshgenerator`, `timeseriesprovider`,
`smoke` (dlopen scaffold + ABI stamp). FVQUAL is a loadable component with a
`fvqual run` CLI (P3) — the pattern for a component that also runs standalone.
Composer v2 (P4) has the document/`@from`/execution work in progress
(`SimulationManager`, `ModelInitializer` progress callback owed at S4.4).
**The Composer already runs headlessly:** `HydroCoupleComposer --run/-r
<composition>` "runs a composition headlessly and exits", built for CI and
cluster use (`HydroCoupleComposer/src/app/main.cpp:4-6, :128-130`). Two
load-bearing build facts: HydroCouple `5ac202c` visibility push is required for
cross-image `dynamic_cast`; component builds must put the SDK's vcpkg prefix on
`CMAKE_PREFIX_PATH`.

**GUI 2D results path:** live 2D is in-process only (`simulationrunner.cpp:324-604`,
bulk getters → `EngineMesh2DSource`); the legacy branch emits `qQNaN() /* legacy: no 2D */`
(`:756`). Post-run 2D comes from `HDF5Mesh2DSource` (`swmm2dresultslayer.h:78-330`) —
engine- and orchestrator-agnostic. `applyEngineConstraints` gates 2D options on
`m_engineVersion.startsWith("5.")` (`simulationoptionsdialog.cpp:170`).

### B.2 Options considered

| | A. Extract 2D as a library | B. Coupled driver inside each 5.x worker | **C. HydroCouple composition (chosen — owner direction)** | D. 2D host in the GUI, per-step IPC |
|---|---|---|---|---|
| Engine work | refactor the router off the context | additive host mode + C API | **same additive host mode + C API** (the 2D component wraps it) | as B |
| 1D side | new lib seam | worker code per engine | **one `Swmm5Component` source → 5.2.4 / 5.3.0 plugins, plus `OpenSwmm1DComponent` for 6.x** | worker protocol |
| Orchestrator | worker | worker (shipped, then thrown away) | **HydroCouple runtime** (Composer or headless runner) | GUI runner thread |
| Headless / CI | yes | yes | yes (component CLI / runner) | no |
| Reaches "all 1D↔2D via HydroCouple" | no | no | **yes** | no |
| Effort to first coupled 5.x run | largest | smallest | medium (3 components + runner) | small |

B is **not shipped** (it would be a second orchestrator to retire later). Its
one useful part — a minimal loop that drives the host mode — survives only as
the engine-repo test harness for gate 1. A is rejected. D is rejected.

### B.3 Target architecture

```
HydroCouple composition (.hcp)  — run by the Composer, or headless from SWMMVis
┌──────────────────────────────┐  node_head[id], node_depth[id], outfall_discharge[id]  ┌──────────────────────────────┐
│ 1D component (exactly one)   │ ───────────────────────────────────────────────────▶  │ OpenSWMM2DComponent           │
│  Swmm524Component            │                                                       │  6.x engine, EXTERNAL_1D mode │
│  Swmm530Component            │ ◀───────────────────────────────────────────────────  │  (swmm_2d_external_* inside)  │
│  OpenSwmm1DComponent (6.x)   │  exchange_flow[id], outfall_head[id], [conductance[id]]│                               │
└──────────────┬───────────────┘                                                       └───────────────┬───────────────┘
        writes .out (tailed live by SWMMVis)                                                writes .h5 (tailed live, Phase L)
```

- **Exchange items** are `ITimeIdBasedComponentDataItem`s keyed by **node id**
  (SWMM names). Units SI: head/depth m, flows m³/s, conductance m³/s per m.
  Each component resolves ids against its own model; a missing id is a
  `prepare()` failure, not a silent drop.
- **Clock and order.** The 1D component owns the clock (its routing step is
  variable under dynamic wave). Pull semantics reproduce the native
  ordering: at step *n* the 1D component requests `exchange_flow(t_n)`; the 2D
  component advances `[t_{n-1}, t_n]` from the 1D state published at
  `t_{n-1}` and answers; the 1D applies the rate over step *n*. That is the
  native "exchange from frozen post-step heads, delivered during the next
  step" with one-step lag; the router's internal `MAX_TIMESTEP` batching is
  unchanged.
- **Optional `conductance[id]` item** carries the Picard stabilizer *G* to a
  1D component that can apply it (6.x via a new forcing call; 5.3.0 only if
  decision 4 accepts the hook; 5.2.4 never). Its presence is what lets the
  6.x-over-HydroCouple composition match native bit-for-bit (gate 1b).
- **Results never cross HydroCouple.** The 1D component writes the `.out`,
  the 2D component writes the `.h5`; SWMMVis renders both by tailing files
  (live 1D done; live 2D is Phase L). No exchange item is needed for rendering.
- **One 5.x engine per process** (process-global state). A composition with
  two SWMM 5 components needs HydroCouple's distributed mode
  (`hydrocoupledistributed.h`); out of scope here.

### B.4 Work by repository

#### B.4.1 `openswmm.engine` (swmm6_rel) — the 2D host mode and the 6.x 1D seam

- `[2D_OPTIONS] EXTERNAL_1D YES` (also `swmm_2d_set_external_1d(engine, 1)`
  before initialize): parse the **full** v6 `.inp`, initialize 2D, and in
  `step()` skip subcatchment runoff/infiltration/groundwater, 1D routing,
  quality and the 1D output plugin; keep the gage + climate update (what
  `updateRainfall` reads). Reuse the `do_routing_` seam (`SWMMEngine.cpp:1112`)
  and the `IGNORE_*` pattern. The host writes no `.out`.
- C API (`openswmm_2d.h`; **SI**, bulk arrays, ids resolved by the caller
  through `swmm_node_index`):
  `swmm_2d_external_coupled_nodes(engine, int* idx, int* n)`;
  `swmm_2d_external_set_node_state(engine, const int* idx, const double* head_m, const double* depth_m, const double* outfall_q_m3s, int n)`
  (writes `ctx.nodes.head/depth`; `outfall_q` replaces the native `inflow − outflow` sample in `accumulateOutfallDischargeStep`);
  `swmm_2d_external_step(engine, double dt_s)` (= `advancePostRouting` + `elapsed_ms / next_report_ms` bookkeeping + report-instant flush and `.h5` snapshot);
  `swmm_2d_external_get_exchange(engine, const int* idx, double* q_m3s, int n)` (`coupling_inflow`, + = into the node);
  `swmm_2d_external_get_outfall_head(engine, const int* idx, double* h2d_m, int* wet, int n)`;
  `swmm_2d_external_get_conductance(engine, const int* idx, double* G, int n)` (the `computeCouplingConductances` result, SI);
  `swmm_2d_external_finalize(engine)`.
- 6.x **1D** side (for `OpenSwmm1DComponent`): `IGNORE_2D YES` already exists;
  add `swmm_forcing_node_coupling_conductance(engine, node, G)` (applied in the
  Picard loop exactly where `computeCouplingConductances` is today) and
  `swmm_forcing_node_lat_inflow` / `swmm_forcing_node_head_boundary` are already
  there. Optional `[2D_OPTIONS] COUPLING_CONDUCTANCE YES|NO` so gate 1 can run
  native-without-stabilizer.
- **Gate-1 harness** (test-only, `tests/unit/engine/test_2d_external_1d.cpp`):
  a second 6.x instance with `IGNORE_2D` acts as the 1D, driven through the API
  above by the test. This is the *only* non-HydroCouple coupling driver, and it
  never ships.
- `Default2DOutputPlugin`: SWMR write + per-report flush (Phase L).

#### B.4.2 5.x engines — API parity so both components can behave the same

| Need | 5.3.0 (`src/legacy/engine`, in-tree) | 5.2.4 (`build-v5.2.4`, bit-identical rule) |
|---|---|---|
| Lateral inflow per routing step | `swmm_NODE_LATFLOW` (exists) | exists |
| Outfall stage from 2D | `swmm_NODE_HEAD` (exists; sticks FIXED) | exists; sticks FIXED |
| **Restore authored outfall type** when the 2D cell dries | add: `swmm_setValue(swmm_NODE_HEAD, i, NaN)` restores type (API-only) | **backport step 7**, same semantics (API-only) |
| **Seal coupled junctions** (no `overflow` at a coupled node; head may rise above rim; the orifice law spills it — what 6.x `coupled_node` does) | `swmm_NODE_SURCHARGE_DEPTH` + `swmm_NODE_PONDED_AREA` setters **exist** (pre-start) | **backport step 8**: the same two pre-start setters (`Node.surDepth`, `Node.pondedArea`; IO/API-only) |
| Conductance stabilizer | **optional** `swmm_NODE_COUPLING_CONDUCTANCE` → `Xnode[i].sumdqdh += G` (`dynwave.c:722`); zero default; **solver-path change — decision 4** | not possible |
| Running continuity on the job row | `swmm_getRunningMassBalErr` (exists) | after backport step 4 |
| Warnings to the log | `swmm_setWarningCallback` (exists) | after backport step 3 |
| Future transport item | `swmm_NODE_POLLUTANT_LATMASS_FLUX` (exists) | — |

Tag `v5.2.4-swmmvis.2` after steps 7–8 (they are tiny; the parity gate proves them inert).

#### B.4.3 `HydroCoupleComponents` — three components, two of them from one source

- **`OpenSWMM2DComponent`** (`components/openswmm2d/`): wraps the 6.x engine
  in `EXTERNAL_1D` mode through the C API. Arguments: full `.inp`
  (or `@from` a provider), mesh file, `.h5` output path. Inputs:
  `node_head`, `node_depth`, `outfall_discharge` (id-based). Outputs:
  `exchange_flow`, `outfall_head`, `conductance` (id-based); `cell_depth` /
  `cell_head` on the mesh (`ITimeSeriesTINComponentDataItem`) for other
  consumers. `update()` = `set_node_state` + `external_step(dt)`.
- **`Swmm5Component`** (`components/swmm5/`, one source parameterised over the
  5.x toolkit → plugins `Swmm524Component` (5.2.4 compiled in statically from
  the pinned tag, as M-2 does for the worker) and `Swmm530Component` (links
  `openswmm::legacy::engine`)). Arguments: compat `.inp` (v6-only sections
  stripped — Phase 4), `.rpt`/`.out` paths. Inputs: `lateral_inflow`,
  `outfall_stage`, optional `conductance` (5.3.0 only). Outputs: `node_head`,
  `node_depth`, `outfall_discharge`. `prepare()` seals the nodes named by the
  `lateral_inflow` input via the surcharge-depth / ponded-area setters;
  `update()` = `swmm_step` + publish; `lateral_inflow` is applied with the
  withdrawal guard (`|Q_spill| ≤ V_node/dt`, clamped remainder reported).
  The component also runs standalone with a CLI that speaks the worker
  protocol (`hello / dates / progress / continuity / warning / error`), the
  `fvqual run` pattern — so it can *replace* the M-1 worker binary later
  without a GUI change (decision 5).
- **`OpenSwmm1DComponent`**: the 6.x engine with `IGNORE_2D`; same items via
  the forcing API; carries the `conductance` input. Test instrument first,
  the future "all 1D via HydroCouple" path second.
- Pins: the 5.2.4 tag is pinned in two places (GUI `EngineVersions.cmake`,
  components repo) — keep them equal by convention and assert it in CI
  (decision 9).

#### B.4.4 Runner and Composer

- A composition template per 1D engine (`.hcp`): one 1D component, one 2D
  component, six id-based links, pull-driven. Authored once, parameterised by
  file paths through arguments.
- **Runner = the Composer's existing headless mode**
  (`HydroCoupleComposer --run <composition>`, `main.cpp:128-130`). What it
  lacks for SWMMVis is the **worker protocol on stdout** (`hello / dates /
  progress / continuity / warning / error`): add a `--progress-json` (or
  similar) flag that forwards `SimulationManager` progress (P4 S4.4's
  `ModelInitializer` callback) and the components' continuity figures as
  JSON lines. SWMMVis then spawns the Composer exactly as it spawns a worker
  today; no SDK-level runner is needed.

#### B.4.5 `openswmm.gui` (SWMMVis)

- M-3 registry/manifest: the seam the base plan left open (`family` /
  `runMode`) gains `runMode: "hydrocouple"` with a `composition` template
  path and `capabilities.twoD: true` for 5.3.0 and 5.2.4 once their
  components exist. `applyEngineConstraints` (`simulationoptionsdialog.cpp:170`)
  stops disabling 2D for those entries.
- Run orchestration (`SWMMVis::onRunSimulation`): legacy5 + mesh present →
  write the compat `.inp` (1D component input) and the full `.inp` (2D
  component input), instantiate the composition template with the paths,
  spawn the headless runner. Without a mesh → today's worker path.
- `SimulationRunner` legacy branch: parse `twoDContinuity`; on finish open the
  `.h5` (path from `[2D_OPTIONS] OUTPUT_FILE`, same rescan as `:328-331`)
  through `HDF5Mesh2DSource` — the existing post-run path.
- Live 2D (Phase L): `HDF5Mesh2DSource::openLive / refresh` (SWMR read +
  `H5Drefresh` + re-read the time extent) on the same `progressChanged` tick as
  live 1D. Mesh geometry from the `.h5` header. Also gives live 2D to 6.x CLI
  and Composer runs.

### B.5 Known fidelity gaps vs 6.x native

1. **Stabilizer**: 5.2.4 never gets it; 5.3.0 only under decision 4; 6.x-over-HydroCouple gets it through the `conductance` item. Sized by gates 1/1b/2, not assumed.
2. **Outfall feedback**: both 5.x stick FIXED until the restore setter lands (B.4.2); the components engage the stage only while the 2D cell is wet.
3. **Transport / heat / age**: out of scope. The host receives no 1D species; 5.3.0's lat-mass-flux setter is the future item.
4. **Running continuity** stays "—" for 5.2.4 until backport step 4; 2D continuity is available from day one.
5. **Per-step framework overhead**: the exchange is a few thousand doubles per routing step over coupled nodes only; the 2D advance is inside the component. Measured on the corpus (gate 2 records wall time next to native).

### B.6 Verification gates (transparent IO: artefacts in tracked folders)

1. **Engine self-coupling** (`test_engine_2d_external_1d`): native with `COUPLING_CONDUCTANCE NO` vs a second 6.x instance driven through the external API — `.h5` fields and `swmm_2d_get_mass_balance` **bit-identical**. Then native with the stabilizer ON: the delta is gap B.5-1 isolated from plumbing. `tests/output/2d_external_1d/`.
2. **1b — framework path**: composition `OpenSwmm1DComponent + OpenSWMM2DComponent` with the `conductance` item linked vs native 6.x+2D — **bit-identical**; without the item vs gate-1 external run — bit-identical. Proves HydroCouple adds nothing numerically. Artefacts in `HydroCoupleComponents/verification/openswmm_1d2d/`.
3. **Corpus, three-way**: 5.2.4+2D, 5.3.0+2D, 6.x+2D on the 2D corpus (Bellinge slice, weir_culvert, road_culvert, East Boston 2D): 2D mass-balance closure, exchange totals (spill / drain m³), per-cell peak depth, clamped-withdrawal ledger, wall time. Tolerances set from the first run. `verification/524_2d/<case>/report.md` (branch) and the 5.3.0 equivalent under `openswmm.engine/verification/`.
4. **Parity stays green**: 5.2.4 stock vs branch `.out` (`verify_524_parity.sh`) after steps 7–8; the in-tree v6-vs-5.3.0 corpus (`tests/parity/run_corpus.sh`) after any 5.3.0 change. Proves the coupling additions are inert when unused.
5. **GUI**: from SWMMVis, on a meshed model, run 5.3.0 and 5.2.4 compositions — job rows with 2D continuity, post-run 2D layer from the `.h5`, comparison plot of a cell series across the three engines; Phase L: the 2D animation range grows during the run.
6. **Windows SWMR** (CI "live tail 2D" step): open the `.h5` in SWMR-read while the runner writes; the time extent must grow. Fallback per decision 6.

### B.7 Phases and sequencing

```
Prereqs (multi-engine plan): M-1 worker → M-2 GUI builds it → M-3 registry (+ runMode "hydrocouple"); Phase 4 compat write (strip only)

C-E1  engine: EXTERNAL_1D + swmm_2d_external_* + get_conductance + forcing_node_coupling_conductance
      + COUPLING_CONDUCTANCE option + gate-1 harness                                          → gate 1
C-E2  5.x API parity: 5.3.0 outfall restore (+ optional conductance hook, decision 4);
      5.2.4 backports 7 (restore) + 8 (surcharge/ponded setters); tag -swmmvis.2              → gate 4
C-H1  HydroCoupleComponents: OpenSWMM2DComponent                                              → loads, runs a rain-only mesh
C-H2  HydroCoupleComponents: Swmm5Component → 524/530 plugins; OpenSwmm1DComponent;
      component CLI speaks the worker protocol                                                → each runs a model standalone
C-H3  composition templates + Composer `--run` JSON-progress flag                             → gate 1b, then gate 2
C-G1  GUI: runMode hydrocouple, compose + spawn, twoDContinuity, .h5 post-run                 → gate 5 (post-run)
L-E3  Default2DOutputPlugin SWMR write + per-report flush                                     → gate 6
L-G7  HDF5Mesh2DSource::openLive / refresh on the progress tick                               → gate 5 (live)
C-D   docs, manifest notes, CHANGELOGs (engine, gui, components)
```

C-E1 and C-E2 are independent of M-* and can start now. C-H1 needs C-E1;
C-H2 needs C-E2 (for sealing/restore) and, for 5.2.4, the M-1 tag; C-H3 needs
C-H1/C-H2; C-G1 needs M-3 + C-H3. L-* are independent of everything above.

### B.8 Decisions needed

1. **Confirm HydroCouple is the only shipped coupling path** for 5.x (no worker `--2d`); the engine harness stays test-only.
2. **Sealing** coupled junctions at `prepare()` through the 5.x API (5.3.0 now; 5.2.4 after backport 8) rather than compat-writer rows — recommend API (the model file stays untouched; one mechanism for both).
3. **Outfall restore** in both 5.x engines (5.3.0 in-tree; 5.2.4 backport 7, API-only) — recommend yes.
4. **5.3.0 conductance hook** (`Xnode.sumdqdh += G`, zero default, solver-path) — recommend **defer** until gate 2 quantifies the gap; if accepted, the v6-vs-5.3.0 corpus is the guard.
5. **Uncoupled 5.x runs**: keep the M-1 worker (ships sooner, on the live-5.2.4 critical path) and let the `Swmm5Component` CLI replace it once it speaks the same protocol — recommend this order.
6. **Live 2D transport**: HDF5 SWMR tail first; raw frame sidecar only if gate 6 fails on Windows.
7. **6.x native in-process coupling**: keep as the reference and fast path after gate 1b; retiring it is a separate, later decision.
8. Confirm transport/heat/age coupling is out of scope.
9. **5.2.4 pin** lives in two repos (GUI, components) — equal-by-convention + CI assert, or a single shared pin file? Recommend the CI assert.
10. Runner: use `HydroCoupleComposer --run` plus a JSON-progress flag (recommended; the headless mode already exists) rather than a separate SDK executable — confirm, and confirm SWMMVis may bundle the Composer binary alongside the workers.

### B.9 Risk register

| Risk | Likelihood | Mitigation |
|---|---|---|
| Explicit-source coupling churns at stiff nodes without the stabilizer | Med | gates 1–2 before GUI exposure; decision 4 for 5.3.0; conductance item for 6.x |
| Withdrawal clamps leave 2D/1D ledgers inconsistent | Med | component ledger on the continuity line; a `return_undelivered` API only if material |
| Outfall type stuck FIXED | Med | wet-gated engagement; restore setters (B.4.2) |
| One 5.x engine per process | Certain | documented; two-5.x compositions need distributed mode (out of scope) |
| HydroCouple build traps (visibility, vcpkg prefix) | Med | provider-pipeline memory: `5ac202c` + `CMAKE_PREFIX_PATH`; CI builds the components |
| Composer P4 S4.4 (progress callback) not landed when C-H3 starts | Med | runner emits progress from the components' own callbacks meanwhile |
| HDF5 SWMR on Windows / network shares | Med | gate 6; sidecar fallback |
| Pin drift between GUI and components repos | Low | CI assert (decision 9) |
| Two 6.x-lib runtimes (HDF5, OpenMP) in one process with a 5.x engine | Low | same bundling the GUI already does; codesign after install (known macOS trap) |
| Scope creep into transport | Med | decision 8 |
