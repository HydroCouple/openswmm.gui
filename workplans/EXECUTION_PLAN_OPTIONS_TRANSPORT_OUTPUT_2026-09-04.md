# Execution plan — Simulation Options restructure · process gating · MSX parity · 2D output controls · GW transport authoring

Date: 2026-09-04
Repos: `openswmm.gui`, `openswmm.engine`
Status: **EXECUTION PLAN — governs the order of work; nothing in it is implemented yet.**

Consolidates, in dependency order, the three plans written today:
- **[OPT]** `openswmm.gui/workplans/SIMULATION_OPTIONS_AND_PROCESS_GATING_PLAN_2026-09-04.md`
- **[GW]** `openswmm.engine/plans/transport/GW_TRANSPORT_HEAT_MSX_PLAN_2026-09-04.md`
- **[OUT]** `openswmm.engine/plans/2D_OUTPUT_FLOAT32_AND_VARIABLE_SELECTION_PLAN_2026-09-04.md`

---

## 1. Working protocol (every phase)

1. **I implement** one phase, in both repos where needed, and write `workplans/handoffs/HANDOFF_P<n>_<slug>.md` (GUI repo) containing: exact file list per repo, build commands, automated tests to run with expected results, a **UI acceptance script** (numbered clicks and what must be visible), the commit commands (file-by-file; `git add -p` for any file another session has touched), and a "report back" template.
2. **Validator agent** builds both repos, runs the tests, walks the UI script, and replies with the template filled: `BUILD engine/gui: OK|FAIL (log excerpt)`, `TESTS: <name>: PASS|FAIL`, `UI: step n: OK|FAIL (what was seen)`, `TREE: git status --short of both repos`, `VERDICT: COMMIT|FIX`. It does **not** commit.
3. **On COMMIT** I stage exactly the handoff's file list and commit (or hand the exact commands back if `.git/index.lock` blocks me). **On FIX** I patch and re-issue the handoff with a `.1` suffix. No phase starts until the previous one is committed and the CI matrix (macOS arm/intel, Ubuntu, Windows — `build_and_test.yml`, engine ref `swmm6_rel`) is green.
4. Engine commits push to `swmm6_rel` **before** the GUI commit that depends on them (the GUI CI checks the engine out from that branch).
5. Both trees carry other sessions' work (GUI: Plot-Rainfall button, Time Series editor, Object Browser; engine: the S4 remainder). Every handoff lists what **not** to stage. Whole-file `git add` is allowed only for files the handoff certifies as single-owner.

## 2. Decisions assumed (from the three plans' decision tables)

The phases below assume the recommended answers. Confirm or override before P1; an override re-scopes only the phase that cites it.

| From | Assumed |
|---|---|
| OPT §9 #1 | Inherit-on transport with per-domain opt-out (no result change for existing decks) |
| OPT §9 #2 | One `TRANSPORT_*` key per species class |
| OPT §9 #3 | `[INITIAL_QUALITY]` is the single authoring surface for MSX initial values; `[REACTION_QUALITY] NODE\|LINK` read as alias |
| OPT §9 #4 | `.ard` boundaries/sources converted to solver-agnostic inflow records |
| OPT §9 #5 | Sidebar becomes a two-level `QTreeWidget` |
| OPT §9 #6 | Track I-b (`INFIL_DESTINATION SUBCATCH_AQUIFER`) pulled ahead of G1 |
| OPT §9 #7 | CSV sidecar **and** hotstart species block both in scope |
| OUT §4 | `OUTPUT_PRECISION FLOAT32` and `REPORT_2D_VARIABLES DEFAULT` become defaults; `EDGE_FLUX` stays in DEFAULT |
| GW §10 | D1 inline advection, D2 `tank=true`+`a_s`, D3 `R_f` only, D4 implicit vertical chain, D5 air-temperature surface BC, D6 geothermal-flux default, D7 evapoconcentration, D8 arithmetic mixing, D9 auto-migrate legacy GW constants, D10 clamp at −5 °C, D11 dedicated Sources & Wells dialog |

## 3. Hard dependency to state up front

The GW transport **solver** phases (GW T7.1–T7.5, G-T7.d results) require the two-zone groundwater kernel (G1 in `TWO_ZONE_GROUNDWATER_EXPLICIT_LTS_PLAN_2026-08-15.md`), which is **not started** and is not part of the three plans. Everything GW that is *authoring* — sections, C API, editors, round trips — is scheduled below (P7) and is UI-testable without the kernel. The solver work is listed as P9 and needs a separate G1 execution plan first; I flag this rather than schedule work I cannot make UI-testable.

---

## 4. Phases

Each phase: **Goal → Scope (engine / GUI) → UI-testable outcome → Automated gates → Commit boundary → Est.**

### P0 — Land the pending work, baseline the tree
- **Scope.** Commit the rain-gage X/Y work per `RAINGAGE_COORDINATES_HANDOFF_2026-09-04.md` (already written). Verify `swmm6_rel` CI is green after yesterday's header fix. Record the current no-edit behaviour of the Simulation Options dialog on three reference decks (Example1, a US 2D deck, an FV deck) as the **baseline round-trip test** `test_simulationoptions_roundtrip` — it must show `wroteAnyChanges() == false` after open→OK with no edits. This test is the safety net for P3 and P4 and lands *before* any dialog change.
- **UI outcome.** Rain gage X/Y editable in Properties and Attribute Table; map follows.
- **Gates.** `test_raingagecoords`, new `test_simulationoptions_roundtrip` on 3 decks; CI green.
- **Commit.** GUI: 1 commit (rain gage) + 1 commit (round-trip test). Engine: none.
- **Est.** 0.5 d.

### P1 — 2D results file: precision, compression, variable selection, cadence  *(OUT, whole plan)*
Smallest, most self-contained, immediately visible win; also de-risks every later phase that runs 2D decks in the UI (smaller files, faster loads).
- **Engine.** `[2D_OPTIONS] OUTPUT_PRECISION`, `OUTPUT_COMPRESSION`, `REPORT_2D_VARIABLES`, `REPORT_2D_SPECIES`, `REPORT_2D_STEP`; `SolverOptions2D` fields; parse / `is2DOptionKey` / `options_get_ext` / `InpWriter` / GeoPackage; writer creates only selected datasets as `H5T_IEEE_F32LE` + shuffle+deflate, geometry stays f64; root attrs `precision`, `compression`; honour 2D step; `swmm_2d_output_variables()` helper. Tolerance table for `compare.py`. Tests: key round trip; F32 vs F64 field tolerances + size ratio; MINIMAL file; step 1/12.
- **GUI.** 2D page Output group: Precision, Compression, 2D report step (greyed "= REPORT_STEP" until edited), Variables checklist with Default/Minimal/All + tooltips naming which layers need each, Species sub-list, live size estimate line. `SWMM2DResultsLayer` / `Mesh2DRunLayer` / `mesh2dh5reader` treat every time-varying dataset as optional (greyed plot attribute with tooltip instead of load failure).
- **UI outcome.** Open a 2D deck → Simulation Options → 2D Surface Routing → set MINIMAL + Float32 + 1 h step → Run → results load; velocity attribute greyed with tooltip; depth animates; file on disk ≥4× smaller than the same run with ALL/Float64 (validator records both sizes).
- **Gates.** engine `test_2d_output_options`, `test_2d_output_precision`; GUI `test_simulationoptions_roundtrip` (still `n==0`), `test_mesh2dh5reader_optional_datasets`.
- **Commit.** Engine 1 commit (push first); GUI 1 commit.
- **Est.** 3–4 d.

### P2 — Process gating engine + hub page + 2D Processes page  *(OPT §4, §5; adds pages to the existing dialog, no restructure yet)*
- **Engine.** `TransportPolicy::resolve` + `SpeciesRegistry` as the single row-layout authority (replaces the three re-derivations in `ArdEngine`, `LagrangianSolver`, `SurfaceRouter2D` — bit-identity gate on the quality regression set); `swmm_get_transport_matrix`; `.rpt` matrix header; `[2D_OPTIONS] TRANSPORT_POLLUTANTS/MSX/AGE/TEMPERATURE`, `INFILTRATION`, `EVAPORATION`, `INFIL_STEP` (moved, alias kept), `INFIL_DEFAULT_METHOD`, `INFIL_DESTINATION` (LOST only accepted; others refused with the reserved message). Tests: default-preserving bit-identity; `TRANSPORT_POLLUTANTS NO` ⇒ `n_pollut=0` with age/heat intact; `INFILTRATION NO` ⇒ zero `infil_out`; `EVAPORATION NO` ⇒ zero `evap_out`.
- **GUI.** Replace "Models / Processes" content with the **Processes & Modules hub** (Domains / Processes / Species classes groups + engine-computed matrix preview). New **2D › Processes** page (Rainfall, Infiltration group with Edit per-cell… → existing GG0 editor, Evaporation, Transport switches). Both are *additional* pages in the current sidebar; the tree restructure is P3.
- **UI outcome.** Open a 2D+pollutant deck → hub shows matrix with 2D ✓ for pollutants → untick 2D pollutants → matrix ✗ → Run → results have no species dataset, 1D quality unchanged; toggle Infiltration/Evaporation and see the mass-balance rows in the `.rpt` go to zero. `.rpt` header prints the matrix.
- **Gates.** engine `test_transport_policy`, `test_2d_process_keys`, full quality regression set bit-identical; GUI `test_simulationoptions_roundtrip`, new `test_processes_hub` (toggle → key written; matrix cell state).
- **Commit.** Engine 2 commits (TransportPolicy; 2D process keys) — push; GUI 1 commit.
- **Est.** 5–6 d.

### P3 — Dialog restructure: page classes, tree sidebar, one gating mechanism  *(OPT §3)*
- **GUI only.** `SimOptionsPage` base (`read/write/gate`), ~12 page classes extracted from the monolith; `QTreeWidget` sidebar with the §3.1 tree (General / Hydrology / 1D Network / 2D Surface / Quality & Transport / Files); `PageGate` + `refreshGates()` replacing the six ad-hoc mechanisms; `applyEngineConstraints` feeds gates; parent-row redirect walks to the first enabled leaf; `FLOW_ROUTING` moves to 1D › Routing; DW / FV / KW-Steady child pages; 2D split into Hydrodynamics / Coupling / Processes / Performance & Output (Groundwater hidden until the component exists); Quality split into Species & Domains / Solver / Initial Conditions / Reserved Species / Reactions.
- **UI outcome.** Every key still where a user can find it (validator walks a checklist of 20 named keys); switching `FLOW_ROUTING` DYNWAVE↔FV flips the DW/FV pages' enabled state live; unticking the 2D module greys the whole 2D parent except Mesh; Quality parent greys on a deck with no pollutants/age/heat/reactions; Apply → re-read shows clamped values.
- **Gates.** `test_simulationoptions_roundtrip` (`n==0` on 3 decks — the load-bearing gate), new `test_simulationoptions_gates`, existing dialog tests.
- **Commit.** GUI 1 commit (large; single-owner files only — the dialog files are currently untouched by other sessions, re-verify at the time).
- **Est.** 6–7 d.

### P4 — MSX parity, engine + editors  *(OPT §6.1–6.3)*
- **Engine.** Unified `SpeciesRegistry::find` used by `Inflow.cpp`, `PostParseResolver` `[INITIAL_QUALITY]` (lift the MSX refusal; `[REACTION_QUALITY] NODE|LINK` becomes an alias the writer no longer emits), `[DWF]`, `.ard` `resolve_species`, 2D resolvers; `ExtInflowKind::MSX` + `species_row`, CONCEN/MASS with species units, consumers at the node seam for LEGACY/LARD (`msx_node_conc`) and ARD, plus the 2D coupling tuple; `.ard` `[TRANSPORT_BOUNDARIES]`/`[TRANSPORT_SOURCES]` → `ExtInflow` records (+ new `LINK_SOURCE` kind); `swmm_ext_inflow_get_kind`. Tests: `test_species_resolver`, `test_msx_inflows` (3 solvers, injected mass = ∫Q·C), `test_initial_quality_msx`.
- **GUI.** `InitialQualityDialog`: registry constituent combo with kind badge, 2D tab for `[2D_INITIAL_QUALITY]`; Inflows editor: registry combo, units follow species, CONCEN/MASS; Quality › Initial Conditions and Quality › Reactions pages populated.
- **UI outcome.** Open an MSX deck → Node properties → Inflows → add a CONCEN row for an MSX species → Run → the species concentration plot at the downstream node shows the inflow; `[INITIAL_QUALITY]` row for an MSX species seeds t=0 in the plot; saving writes `[INITIAL_QUALITY]`, not `[REACTION_QUALITY] NODE`.
- **Gates.** engine tests above + ARD/LARD/LEGACY regression set; GUI `test_initialqualitydialog_msx`, `test_inflows_editor_msx`, round-trip test.
- **Commit.** Engine 2 commits (resolver; MSX inflows/IQ) — push; GUI 1 commit.
- **Est.** 6–7 d.

### P5 — Initial concentrations from more sources: CSV sidecar + hotstart species block  *(OPT §6.2 items 4–5)*
- **Engine.** `[INITIAL_QUALITY] FILE <csv>` reader (element,constituent,value; appended before resolution); hotstart species block (pollutants+MSX+age+temperature by name, D-IQ5 — wins over everything); version bump. Tests: precedence order; hotstart round trip incl. MSX.
- **GUI.** InitialQualityDialog "Import from CSV…"; hotstart save/use already on Files tab — add the species-block indicator.
- **UI outcome.** Import a CSV of initial concentrations → rows appear → Run → t=0 values match; save hotstart → second run with USE hotstart starts from end-of-run concentrations (plot shows continuity).
- **Gates.** engine `test_initial_quality_sources`, `test_hotstart_species`; GUI dialog test.
- **Commit.** Engine 1 (push); GUI 1.
- **Est.** 3–4 d.

### P6 — Track I-b: 2D infiltration → legacy subcatchment aquifer  *(OPT §7 "now", GW plan §0 dependency-free part)*
- **Engine.** `INFIL_DESTINATION SUBCATCH_AQUIFER`: cell→subcatchment containment at initialize, recharge added to `Groundwater.cpp` upper-zone inflow, ledger rows both sides, warning when a cell lies in no subcatchment (falls back to LOST). Test: mass conservation surface→aquifer→node.
- **GUI.** Destination combo enables `Subcatchment aquifer`; `2D aquifer` stays disabled with the "groundwater release" tooltip.
- **UI outcome.** 2D deck with subcatchments+aquifers → set destination → Run → `.rpt` groundwater section shows the infiltrated volume as recharge; node GW inflow appears.
- **Gates.** engine `test_2d_infil_to_aquifer`; GUI round-trip.
- **Commit.** Engine 1 (push); GUI 1.
- **Est.** 3 d.

### P7 — GW transport authoring surfaces (no solver)  *(GW T7.0, G-T7.a/b/c)*
- **Engine.** `[GW_TRANSPORT_OPTIONS]`, `[GW_TRANSPORT_PARAMS]`, `[GW_SORPTION]`, `[GW_INITIAL_QUALITY]` (+FILE), `[GW_BOUNDARY_QUALITY]`, `[GW_SOURCES]` parsers + writers + GeoPackage; `SUBSURFACE` scope token in the `.rxn` parser (`[REACTION_SUBSURFACE]`); `openswmm_gw_transport.h` C API over pending rows (OPENED-state editing contract, display units before initialize); all inert at run time with a single warning "groundwater transport authored but the integrated groundwater kernel is not available in this build". Tests: every section round-trips `.inp` and `.gpkg`; API get/set; unit handling.
- **GUI.** `GwTransportModel`; 2D › Groundwater page (visible when `[GW_*]` sections exist or the user enables authoring) with the Transport group; InitialQualityDialog Groundwater tab (scope/zone/layer/species, CSV, pick-on-map); aquifer-transport attribute-table block + properties + mesh-gen region defaults; edge-BC Quality sub-table on the mesh edge editor; **Sources & Wells** dialog + map glyph layer + adapter + undo.
- **UI outcome.** Author every GW transport input through the UI on a 2D deck, save, reopen — everything round-trips; Run produces the single "kernel not available" warning and otherwise identical results.
- **Gates.** engine `test_gw_transport_sections`, `test_gw_transport_api`; GUI `test_gwtransport_editors`.
- **Commit.** Engine 2 (sections/API; SUBSURFACE token) — push; GUI 2 (model+pages; sources dialog/layer).
- **Est.** 8–9 d. *Sequence after the in-flight Time Series editor lands — the TS pickers depend on it.*

### P8 — Groundwater page in the restructured dialog + `GW_ET` / `AQUIFER_2D` authoring  *(GW §7 "with G1" authoring half, OPT §7)*
- **Scope.** Keys `GROUNDWATER ON`, `GW_ET NONE|CAPILLARY_RISE|BOUNDARY_ET|BOTH`, `INFIL_DESTINATION AQUIFER_2D` accepted **as authoring** (still refused at run without the kernel, same warning as P7); Groundwater page groups from `INTEGRATED2D_GW_GUI_PLAN` §1 wired to `model.i2d`; mutual-exclusion gate between Infiltration method and GW closure shown live.
- **UI outcome.** Enable GW on the page → Infiltration destination locks to `2D aquifer`, method group re-labels to the closure's job; save/reopen round-trips.
- **Est.** 3 d.

### P9 — **Blocked: GW transport solver** (GW T7.1–T7.5, G-T7.d results)
Requires the G1 kernel. Not scheduled here. Prerequisite deliverable: a G1 execution plan in the same protocol (kernel steps 1–8, each UI-visible through the water-table layer). Once G1 lands, the GW plan's T7.1–T7.5 slot in with their manufactured gates (Ogata–Banks, Domenico, Carslaw–Jaeger, batch-reactor parity, tuple conservation, PER_SUBCATCH migration) and the results layers.

---

## 5. Order and calendar

```
P0 (0.5d) → P1 (3–4d) → P2 (5–6d) → P3 (6–7d) → P4 (6–7d) → P5 (3–4d) → P6 (3d) → P7 (8–9d) → P8 (3d)  ≈ 38–44 working days
                                                                              ↑ P7 waits for the Time Series editor slice
P9 after a G1 execution plan.
```
Rationale for the order: P1 is independent and speeds every later UI run; P2 must precede P3 so the restructure moves finished pages rather than inventing them; P3 must precede P4/P5's new Quality pages; P4 precedes P7 because the GW editors reuse the registry combo and the MSX inflow kinds; P6 is independent and can be swapped anywhere after P2 if you want a GW-visible result earlier.

## 6. Standing rules for every handoff
- Both repos built from the branch heads the handoff names; engine pushed to `swmm6_rel` before the GUI is tested against it.
- Reference decks for UI scripts live in `openswmm.gui/tests/gui/data/` (add a 2D+pollutant+MSX deck in P1 and reuse it throughout).
- Output artefacts under `tests/output/<phase>/` in each repo (CLAUDE.md §4.1), never `/tmp`.
- Commit messages follow the conventional prefixes already in both logs (`feat(2d):`, `fix(options):`, …); plans/handoffs under `plans/` and `workplans/` are **never** staged.
- A phase is done when: tests green locally, UI script all OK, CI green, commit hashes recorded at the bottom of its handoff.
