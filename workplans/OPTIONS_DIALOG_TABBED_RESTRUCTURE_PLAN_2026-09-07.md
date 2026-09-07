# Options Dialogs — Inner-Tab Restructure, Page Classes, and a Single Gating Mechanism

| | |
|---|---|
| **Date** | 2026-09-07 |
| **Repo** | `openswmm.gui` only — no engine changes, no new option keys |
| **Status** | **APPROVED DESIGN — ready to implement.** All decisions resolved (§7). Implementation instructions: `workplans/handoffs/HANDOFF_OPTIONS_DIALOG_TABS_2026-09-07.md` |
| **Supersedes** | Part A (§3.1–3.4) of `SIMULATION_OPTIONS_AND_PROCESS_GATING_PLAN_2026-09-04.md` and P3 of `EXECUTION_PLAN_OPTIONS_TRANSPORT_OUTPUT_2026-09-04.md`, with the departures recorded in §1 |
| **Extends** | The same treatment to `PreferencesDialog`, which the 2026-09-04 plan did not cover |
| **Verified against source** | 2026-09-07, HEAD `f059447`. All `file:line` references below are to that revision. |

---

## 0. Problem statement

> "For the simulation options dialog and properties dialog — especially 1D and 2D
> model options — there are so many options that trigger scrolling. I want to
> combine into contextually relevant tab pages when it makes sense."

Two dialogs share one flat settings-style sidebar idiom (`QListWidget` +
`QStackedWidget`, each page wrapped by `OpenSWMM::Ui::wrapInScrollArea`) and
one symptom: pages that stack six to eight `QGroupBox`es in a single column.

| Dialog | Source size | Sidebar rows | Pages that scroll at 1280×800 |
|---|---|---|---|
| `SimulationOptionsDialog` | 4,711 lines `.cpp`, 587 `.h` | 10 | **Routing & Hydraulics** — 6 groups, ~35 controls, three mutually exclusive solver families; **2D Surface Routing** — 8 groups; Dates & Times — 5 groups; Quality & Transport — 4 groups |
| `PreferencesDialog` | 2,222 lines `.cpp`, 278 `.h` | 15 | **Rendering** — 5 groups; **Simulation Defaults**, **Dynamic Wave Defaults**, **2D Defaults** — 4–5 groups each |

`UI_REDESIGN_ITER3_WORKPLAN.md` Phase 6 made these pages *scroll* gracefully.
This plan makes them not *need* to. The secondary defect it fixes is
structural: `FLOW_ROUTING` — the selector that decides which of the three 1D
solver families applies — lives on the Models page, two sidebar rows away from
the ~35 widgets it governs, and a DYNWAVE user scrolls past 17 greyed
finite-volume widgets to reach the conduit settings.

---

## 1. Departures from the 2026-09-04 plan

### 1.1 Sidebar structure (OPT §9 #5) — resolved: flat sidebar + inner tabs

The 2026-09-04 plan recommended a two-level `QTreeWidget` sidebar; its
alternative was a top-level `QTabWidget`. **Neither is adopted.** The existing
flat `QListWidget` sidebar stays, and a `QTabWidget` is placed *inside* each
page that has outgrown a single column.

| Kept | Cost |
|---|---|
| `addCategory(title, page)` contract, the always-visible page overview, the existing row-level gate (`set2DRowEnabled`, `simulationoptionsdialog.cpp:429-442`) | A **tab-level** gate is needed in addition to the row-level one. `QTabWidget::setTabEnabled` + `setTabToolTip` provides it, but a disabled tab can be neither clicked nor current — so the selection-stranding hazard OPT §10 attributed to tree parents applies to the tab bar instead (§4.3). |
| No tree-selection redirect logic | Domain grouping (Hydrology / 1D / 2D / Quality) is expressed by row order and page titles, not nesting. Pages the tree would have made *siblings under a parent* become *tabs within one row*. |

### 1.2 Depth — resolved: full P3

Not merely re-parenting widgets. Both the `SimOptionsPage` class split (OPT
§3.4) and the single `PageGate` mechanism (OPT §3.3) land.

### 1.3 Scope boundary — GUI only

The following parts of OPT Part A depend on engine work (OPT §8 P1) and are
**out of scope**. Each has a reserved landing place so the later work is
additive:

| Deferred | Blocked on | Reserved slot |
|---|---|---|
| Live Domain × Species matrix preview on the Models page | `swmm_get_transport_matrix` (OPT §5.3) | Below the tab bar on Models / Processes |
| 2D › Processes tab (infiltration / evaporation enables) | `[2D_OPTIONS] INFILTRATION`, `EVAPORATION`, `INFIL_*` (OPT §4.1) | Tab slot after Coupling on 2D Surface Routing |
| 2D › Groundwater | G1 integrated2d kernel (OPT §7) | — |
| Quality › Initial Conditions / Reactions (MSX) | OPT §6 | Tab slots after Reserved Species on Quality & Transport |
| Models / Processes → "Processes & Modules" hub regrouping (OPT §3.2) | Same matrix API; half a feature without it (§7 Q3) | Page is tabbed, content unchanged |

### 1.4 Corrections to OPT §3.1 found during source verification

Two relocations in the 2026-09-04 tree are **not** carried forward:

- **`FV_SCALAR_SCHEME` stays on Quality & Transport.** OPT §3.1 put it under
  1D › Finite Volume by key-name association. The source says otherwise
  (`simulationoptionsdialog.cpp:1422-1426`, restated at `:4564-4566`): the key
  is read by the Eulerian ARD transport engine under *any* routing model, and
  two widgets writing one key is a defect. It lands on the Eulerian ARD tab.
- **`MINIMUM_STEP` / `LENGTHENING_STEP` / `VARIABLE_STEP` follow their group.**
  OPT §3.1 split the Solver group, sending these three to the shared page. The
  three are constructed in the DYNWAVE Solver group today (`:877-930`) and
  `FastPreset` writes `MINIMUM_STEP` through that group. Moving whole groups,
  never individual widgets, is the risk-control rule of this plan (§4.4); the
  Solver group moves intact to the Dynamic Wave tab.

---

## 2. Target structure — `SimulationOptionsDialog`

Ten sidebar rows, current order preserved. `→ tabs` marks a page that gains an
inner `QTabWidget`. **Bold** marks a widget group that changes page. Every
`QGroupBox` moves *whole*; no group is split.

```
Title / Notes                 unchanged

Models / Processes          → tabs   Domains & Processes │ Modules │ Flags
  Domains & Processes         Process models (INFILTRATION, ALLOW_PONDING; FLOW_ROUTING
                              becomes a read-only mirror — §2.1) · Active processes (IGNORE_*)
  Modules                     Modules group (2D module, components) · "Manage components…"
  Flags                       Options / flags
  ── reserved: matrix preview (§1.3)

Dates & Times               → tabs   Simulation Window │ Time Steps │ Events
  Simulation Window           Simulation window · Sweep / antecedent
  Time Steps                  Time steps
  Events                      Events ([EVENTS]) table
  ── **Skip steady state** group (SKIP_STEADY_STATE, LAT_FLOW_TOL, SYS_FLOW_TOL) leaves
     this page (§7 Q1)

Routing & Hydraulics        → tabs   Routing │ Dynamic Wave │ Finite Volume │ Unsteady Friction
  ── page header, above the tab bar, always visible:
     **FLOW_ROUTING** selector (from Models / Processes — §2.1)
  Routing                     Conduit / channel (FORCE_MAIN_EQUATION, NORMAL_FLOW_LIMITED,
                              INERTIAL_DAMPING, MIN_SURFAREA, MIN_SLOPE) — every solver
  Dynamic Wave                Surcharge handling · Solver (NODE_CONTINUITY, ANDERSON_ACCEL,
                              MAX_TRIALS, HEAD_TOLERANCE, LENGTHENING_STEP, VARIABLE_STEP,
                              MINIMUM_STEP) · **Skip steady state**
                                                            gate: FLOW_ROUTING == DYNWAVE
  Finite Volume               Finite volume solver · Finite volume performance
                                                            gate: FLOW_ROUTING == FV && caps.fv
  Unsteady Friction           Unsteady friction            gate: caps.uf && routing ∈ {DYNWAVE, FV}

Quality & Transport         → tabs   Solver │ Eulerian ARD │ Lagrangian (LARD) │ Reserved Species
  ── page header: QUALITY_SOLVER selector (moves out of the Water quality solver group)
  Solver                      remainder of Water quality solver
  Eulerian ARD                Eulerian ARD (incl. FV_SCALAR_SCHEME — §1.4)
                                                            gate: QUALITY_SOLVER == ARD
  Lagrangian (LARD)           Lagrangian (LARD)             gate: QUALITY_SOLVER == LARD
  Reserved Species            Reserved species (WATER_AGE, HEAT_TRANSPORT + coefficients,
                              Water-age sources link)
  ── reserved: Initial Conditions, Reactions (MSX) (§1.3)

System / Performance          unchanged — Parallelisation + FastPreset
Spatial & CRS                 unchanged
Mesh                          unchanged — never gated (creating a mesh is what turns 2D on)

2D Surface Routing          → tabs   Hydrodynamics │ Mesh & Closure │ Coupling │ Rainfall & Output
  Hydrodynamics               Time stepping · Explicit marcher
  Mesh & Closure              Mesh (DRY_DEPTH, LIMITER_EPSILON, FLUX_DH_EPS) ·
                              Cell closure (wetting / drying)
  Coupling                    1D ↔ 2D coupling
  Rainfall & Output           Rainfall · Performance (BACKEND) · Output (REPORT_2D, OUTPUT_FILE)
  ── reserved: Processes tab after Coupling (§1.3)

Files / Output / Plugins      unchanged — already inner-tabbed
```

**Effect on the two motivating pages:** Routing & Hydraulics drops from ~35
controls in one column to at most 13 per tab, with the finite-volume widgets
behind a greyed tab rather than in the scroll path. 2D Surface Routing drops
from 8 stacked groups to at most 3 per tab.

### 2.1 Selector relocation and read-only mirrors

`FLOW_ROUTING` gates three of the four Hydraulics tabs, so it can neither live
inside one of them nor stay two sidebar rows away. It becomes a page header
above the tab bar. Today the combo is *built* on Models (`:540-549`) and
*wired* on Hydraulics (`:1125-1132`) — the relocation resolves that split
ownership rather than creating it.

The Models / Processes page keeps a **read-only one-line mirror** ("Flow
routing: Dynamic Wave — *change on Routing & Hydraulics*") whose link selects
that sidebar row. It is a `QLabel` refreshed from the owning page's signal, not
a second editable widget — one key, one editor, no synchronisation burden.
`QUALITY_SOLVER` receives the same treatment on the Quality page (header) with
no mirror needed elsewhere.

---

## 3. Target structure — `PreferencesDialog`

Fifteen rows become **twelve**: the *Dynamic Wave Defaults* and *2D Defaults*
rows fold into *Simulation Defaults* as tabs — the literal answer to "combine
into contextually relevant tab pages".

```
General · Selection · Canvas & CRS          unchanged

Rendering                   → tabs   Labels │ Links & Nodes │ GPU │ 2D Mesh Edges
  Labels                      Label Rendering
  Links & Nodes               Link Pens · Node Symbols
  GPU                         GPU Rendering
  2D Mesh Edges               2D Mesh Boundary-Condition Edges

Simulation                                  unchanged

Simulation Defaults         → tabs   Processes & Modules │ Hydraulics & Schedule │
                                     Dynamic Wave: Steps & Tolerances │ Dynamic Wave: Solver │ 2D
  Processes & Modules         Process models · Process modules
  Hydraulics & Schedule       Hydraulics · Schedule
  Dynamic Wave: Steps & Tol.  Time steps · Solver tolerances · Variable timestep     (§7 Q2)
  Dynamic Wave: Solver        Conduit / channel · Solver
  2D                          2D solver · Wet/dry & VFR · 1D↔2D coupling · Rainfall & reporting ·
                              Mesh generation defaults

Object Defaults · Map Display · Measure Tool · Plots · Naming · Appearance · Keyboard   unchanged
```

`PreferencesDialog` reads and writes `PreferencesManager`
(`readFromManager()` / `writeToManager()`, `preferencesdialog.cpp:1462`,
`:1720`), not the engine. It takes the tab restructure and, where a page is
extracted, the `ObjectDefaultsPage` extraction pattern already in the tree
(`include/ui/dialogs/objectdefaultspage.h`) — **not** the `SimOptionsPage`
engine contract. It has **no dirty count and no automated test today**; §6
adds both a structural and a no-edit round-trip test before it is touched.

---

## 4. Mechanism

### 4.1 Page contract — `SimOptionsPage` and `SimOptionsContext`

Pages must not reach into the dialog's private `getOption` / `setOption`
(`simulationoptionsdialog.cpp:2524-2532` / `:3051-3070`) or its `writeIfChanged` lambda
(`:4395-4403`). Those become a small context object the dialog owns and hands
to every page:

```cpp
// include/ui/dialogs/simoptions/simoptionscontext.h
namespace openswmmvis::ui {
class SimOptionsContext {
public:
    SWMM_Engine           engine()        const;
    SWMMModelLayer       *modelLayer()    const;
    SWMMVisProjectWindow *projectWindow() const;
    const EngineCapabilities &caps() const;         // §4.2 — the one-shot probe result
    const PreferencesManager::SimulationDefaults &simDefaults() const;

    QString option(const char *key, const QString &fallback = {}) const;   // swmm_options_get
    /// Numeric-aware compare (optionValueEquals) then swmm_options_set.
    /// Returns 1 if the key was written, else 0. Records key in writtenKeys().
    int     writeIfChanged(const char *key, const QString &newVal);
    QStringList writtenKeys() const;                // every key offered to writeIfChanged
                                                    // in the current write pass (§6 test 6)
};

class SimOptionsPage : public QWidget {
    Q_OBJECT
public:
    explicit SimOptionsPage(SimOptionsContext &ctx, QWidget *parent = nullptr);
    virtual QString title() const = 0;              // sidebar row text
    virtual void    read()  = 0;                    // engine → widgets
    virtual int     write() = 0;                    // widgets → engine; returns #changed
    virtual void    refreshGates() {}               // intra-page tab/group gating
    virtual void    applyCapabilities() {}          // one-shot per-widget disable/tooltip
                                                    // from ctx.caps() (was applyEngineConstraints)
signals:
    void gateInputsChanged();                       // any control that feeds a PageGate changed
protected:
    SimOptionsContext &ctx_;
};
}
```

Preserved verbatim, because tests depend on them (see handoff §0): the public
`wroteAnyChanges()` accessor; `onApply` as a meta-invokable private slot; the
eleven `public static` helpers in `simulationoptionshelpers.cpp`; the Apply →
`readFromEngine()` re-read cycle. `readFromEngine()` / `writeToEngine()` become
fan-outs over the page registry, and `n` — the changed count that sets
`m_wroteChanges` — is the sum of every page's `write()`.

`FastPreset` (System / Performance) writes `MINIMUM_STEP` **through the
Hydraulics page object** (`HydraulicsPage::setMinimumStep`), never through the
context directly (OPT §3.4).

Target layout: one file pair per page under `include/ui/dialogs/simoptions/`
and `src/ui/dialogs/simoptions/`, namespace `openswmmvis::ui`, added
explicitly to `PROJECT_SOURCES` (`CMakeLists.txt:1390-1409` — the tree never
GLOBs). `simulationoptionsdialog.cpp` retains `buildUi`, `addCategory`, the
gate table, Apply/OK, validation, and the page registry — target ≤ 700 lines.

### 4.2 Capabilities — `EngineCapabilities`

`applyEngineConstraints()` (`:168-352`) probes the engine once and disables
widgets in place, which produced the "sticky child disable" fragility noted at
`:267-268` (and again `:276-279`). It becomes a pure probe returning a value:

```cpp
struct EngineCapabilities {
    bool fv          = false;   // FLOW_ROUTING FV accepted
    bool uf          = false;   // unsteady-friction keys accepted (was m_ufSupported)
    bool has2D       = false;   // OPENSWMM_HAS_2D && engine reports 2D
    bool tpa         = false;   // SURCHARGE_METHOD TPA accepted (:243-265)
    bool dynSlot     = false;   // SURCHARGE_METHOD DYNAMIC_SLOT accepted (:293-310)
                                // …one field per remaining `if` block at :168-352
    QString engineVersion;
};
```

Computed in the dialog constructor, stored in the context, consulted by gates
(§4.3) and by each page's `applyCapabilities()`. Probed once, never
re-probed.

### 4.3 Gating — `PageGate` and `refreshGates()`

One mechanism replaces six:

| Replaced | Today |
|---|---|
| `set2DRowEnabled` | `:429-442` |
| `updateFvFieldsEnabled` | `:1330-1356` |
| `updateSurchargeFieldsEnabled` | `:1317-1328` |
| `updateQualitySolverFieldsEnabled` | `:1533` |
| `m_ufSupported` flag + the `syncSkipEnabled` lambda | `:1352`, `:733-740` |
| `applyEngineConstraints` one-shot `setEnabled` | `:168-352` → §4.2 |

```cpp
struct PageGate {
    enum class Target { SidebarRow, Tab, Widget };
    Target                target;
    int                   row      = -1;        // SidebarRow
    QTabWidget           *tabs     = nullptr;   // Tab
    int                   tabIndex = -1;        // Tab
    QWidget              *widget   = nullptr;   // Widget (a QGroupBox or control)
    std::function<bool()> enabled;
    QString               reasonWhenOff;        // tooltip on the greyed row / tab / widget
};
```

`SimulationOptionsDialog::refreshGates()` evaluates every gate, applies it, and
runs after every `read()`, after any page emits `gateInputsChanged()`, and
after `applyCapabilities()`. Gate table:

| Target | Kind | Enabled when | Reason when off |
|---|---|---|---|
| 2D Surface Routing row | row | `!IGNORE_2D` (Mesh row exempt) | "Enable the 2D module on Models / Processes › Modules" |
| Quality & Transport row | row | `pollutants > 0 ‖ WATER_AGE ‖ HEAT_TRANSPORT ‖ reactions component registered` | "No pollutants, water age, heat, or reactions in this model" |
| Hydraulics › Dynamic Wave | tab | `FLOW_ROUTING == DYNWAVE` | "Applies to Dynamic Wave routing only" |
| Hydraulics › Finite Volume | tab | `FLOW_ROUTING == FV && caps.fv` | "Applies to Finite Volume routing only" / "This engine does not support FV" |
| Hydraulics › Unsteady Friction | tab | `caps.uf && routing ∈ {DYNWAVE, FV}` | "Unsteady friction applies to Dynamic Wave and Finite Volume" / "Not supported by this engine" |
| Quality › Eulerian ARD | tab | `QUALITY_SOLVER == ARD` | "Select the Eulerian ARD solver" |
| Quality › Lagrangian (LARD) | tab | `QUALITY_SOLVER == LARD` | "Select the Lagrangian solver" |
| DPS celerity / alpha / decay | widget | `SURCHARGE_METHOD == DYNAMIC_SLOT` | as today |
| TPA celerity | widget | `SURCHARGE_METHOD == TPA` | as today |
| FV limiter | widget | `FV_ORDER == 2` | as today |
| FV LTS tiers | widget | `FV_LTS` checked | as today |
| UF k3 | widget | `UF_METHOD != NONE` | as today |
| LAT_FLOW_TOL / SYS_FLOW_TOL | widget | `SKIP_STEADY_STATE` checked | as today |

Disabled rows and tabs stay **visible but greyed** with `reasonWhenOff` as
tooltip. Pages for features not compiled in (`OPENSWMM_HAS_2D` off) are
**hidden**, as today.

**The tab-redirect rule (§1.1).** After applying a `Tab` gate that disables the
tab currently shown, `refreshGates()` selects the first enabled tab of that
`QTabWidget`. Switching `FLOW_ROUTING` from DYNWAVE to FV while viewing the
Dynamic Wave tab must land on Finite Volume, never on a greyed page. The row
redirect keeps the existing behaviour (`:439-441`). Pinned by §6 test 5.

### 4.4 Migration rules

1. **Every key keeps its section and spelling.** Only widget placement changes.
2. **Groups move whole.** No `QGroupBox` is split across tabs or pages.
3. **One key, one editor.** Mirrors are `QLabel`s.
4. **`objectName` on every gated `QTabWidget`, every selector, and every
   option widget** (`w->setObjectName("flowRoutingCombo")`;
   `w->setProperty("optionKey", "FLOW_ROUTING")`). This is the test seam —
   the dialogs have no friend classes and gain none.
5. Page-build order is a dependency: the Hydraulics page (owner of
   `FLOW_ROUTING`) must be constructed before any page or gate that reads it.
   The registry constructs pages in sidebar order and wires cross-page signals
   after all pages exist.

---

## 5. Phasing

Nine phases, each independently green on the §6 gate and committed
separately. Estimates assume a full-app test rebuild (~several minutes) per
cycle — `test_simulationoptions_roundtrip` links all of `PROJECT_SOURCES`.

| Phase | Deliverable | Est. |
|---|---|---|
| **T0** | Baseline. Run §6 tests 1–3 on the current tree and record results. Add the `optionKey` / `objectName` seam to the *existing* widgets (no layout change) and land §6 tests 6 and 8 against the current layout, so the reachability set is pinned **before** anything moves. | 1 d |
| **T1** | `SimOptionsContext`, `EngineCapabilities`, `SimOptionsPage`, page registry. Port **Spatial & CRS** only (1 group). Round trip `n == 0`. | 1 d |
| **T2** | Port Title / Notes, System / Performance, Mesh, Files / Output / Plugins as page classes — class split only, no layout change. | 1.5 d |
| **T3** | `PageGate` + `refreshGates()` + tab-redirect helper. Replace all six mechanisms. Row and widget gates only (no tabs exist yet), so behaviour is observably identical. §6 tests 4a, 5 land here against the row gate. | 1.5 d |
| **T4** | **Routing & Hydraulics** page class: four tabs, `FLOW_ROUTING` header, Skip-steady-state group moved in from Dates, Models mirror. §6 tests 4b, 5, 7. | 2 d |
| **T5** | **2D Surface Routing** page class: four tabs. | 1 d |
| **T6** | Quality & Transport (four tabs, `QUALITY_SOLVER` header), Dates & Times (three tabs), Models / Processes (three tabs) page classes. | 2 d |
| **T7** | `PreferencesDialog`: §6 tests 9–10 first (baseline), then Rendering tabs, then merge the three Defaults rows. | 1.5 d |
| **T8** | `docs/manual/18_simulation_options.md`, `04_preferences.md`, `images/TODO.md`; `CHANGELOG.md` *Unreleased › Changed* entry. | 0.5 d |

**≈ 12 days.** OPT §8 estimated 5–7 d for the tree version; the difference is
`PreferencesDialog` (T7), the tab gate and its redirect (T3), the
reachability seam (T0), and the one-page-at-a-time porting order chosen so
every step is verifiable.

---

## 6. Verification gate — runs at the end of every phase

All fixtures and generated artifacts are written under `tests/gui/data/`, never
to a temp directory (CLAUDE.md §4.1). Commands are in the handoff §3.

| # | Test | Asserts |
|---|---|---|
| 1 | `test_simulationoptions_roundtrip` (exists — OPT P0) | Open → Apply with no edits → `wroteAnyChanges() == false` and the owned `.inp` sections are byte-identical, on `typed_selection_fixture.inp` (DYNWAVE), `mini_2d.inp` (2D), and the generated FV variant. `editIsDetected` stays green. |
| 2 | `test_simoptions_persistence_contract` (exists) | Unchanged, green. |
| 3 | `test_simulationoptionsdialog` (exists) | Unchanged, green — the eleven static helpers keep their signatures and stay in `simulationoptionshelpers.cpp`. |
| 4 | **new** `test_simulationoptions_gates` | (a) toggle the 2D module → 2D row flags flip; (b) set `flowRoutingCombo` to DYNWAVE / FV / KINWAVE / STEADY → `hydraulicsTabs` enabled-states match §4.3; set `qualitySolverCombo` → ARD / LARD tabs match. |
| 5 | same file | **Redirect.** Select Dynamic Wave tab, set routing to FV → `hydraulicsTabs->currentIndex()` is Finite Volume and `isTabEnabled(current)`. Same for the 2D row. |
| 6 | same file | **Reachability.** Collect every widget with an `optionKey` property; assert (i) each has an ancestor chain reaching `m_pages`, and (ii) the set of `optionKey` values ⊇ `SimOptionsContext::writtenKeys()` after one Apply. A widget built but never laid out, or a key written by no visible editor, fails here. |
| 7 | same file | **No scroll at 1280×800.** `dlg.resize(1280, 800); dlg.show();` then for every sidebar row and every tab: the page's `QScrollArea::verticalScrollBar()->maximum() == 0`. This *is* the acceptance criterion for the ask. Runs under the offscreen QPA; if a platform's font metrics fail it, the fix is layout, not the threshold. |
| 8 | same file | **Structure.** Tab titles per `QTabWidget` equal §2 exactly; sidebar row titles equal §2 in order. |
| 9 | **new** `test_preferencesdialog_roundtrip` | Point `QSettings` at `tests/gui/data/prefs_roundtrip_artifacts/`; construct the dialog; invoke `onApply`; every `QSettings` key/value identical before and after. |
| 10 | same file | **Structure + no-scroll** for `PreferencesDialog` per §3, same technique as 7–8. |

**Manual acceptance (once, at T8):** both dialogs at 1280×800 on macOS, no
page needs vertical scrolling with default content; every relocated control is
found where §2/§3 say it is; tooltips on greyed tabs read correctly.

---

## 7. Decisions — resolved 2026-09-07

| # | Question | Decision |
|---|---|---|
| Q1 | `LAT_FLOW_TOL` / `SYS_FLOW_TOL` — leave on Dates & Times or move? | **Move**, as their whole *Skip steady state* group, to Hydraulics › Dynamic Wave (OPT §3.1's placement). Their `SKIP_STEADY_STATE` gate (`:733-740`) becomes a widget-level `PageGate` and travels with them. *Implementer check:* if engine source shows `SKIP_STEADY_STATE` is honoured under every routing method, the group lands on the shared **Routing** tab instead — same mechanics, one line changed. |
| Q2 | Preferences › Dynamic Wave carries five groups even as one tab | **Split into two tabs** — *Steps & Tolerances* and *Solver* — rather than nest a tab widget inside a tab page. |
| Q3 | Regroup Models / Processes into the OPT §3.2 hub now? | **No.** Tab the existing groups; content unchanged until the engine matrix API exists. |
| Q4 | Merge Mesh and 2D Surface Routing into one sidebar row? | **No.** Mesh is list management, not options; the "Mesh row is exempt from the `IGNORE_2D` gate" rule stays row-level. |
| Q5 | Deliverable shape | Design of record (this file) + implementation handoff (`handoffs/HANDOFF_OPTIONS_DIALOG_TABS_2026-09-07.md`). |

---

## 8. Risks and mitigations

| Risk | Mitigation |
|---|---|
| The page split touches every key round trip | T0 pins the safety net and the reachability set *before* anything moves; T1/T2 port one page at a time; §6 tests 1 and 6 run every phase. |
| A relocated control silently orphaned (built, never laid out) | §6 test 6 (i). |
| A key written by a widget that is no longer reachable | §6 test 6 (ii). |
| Tab gating strands the current tab on a greyed page | §4.3 redirect; §6 test 5. |
| `onApply` / `onAccept` pop a `QMessageBox` on warnings; under the offscreen QPA this hangs the test until `QTEST_FUNCTION_TIMEOUT` (30 s) | Fixtures are clean today; any new warning on them is a regression to fix, not to dismiss. Handoff §3 says how to spot it. |
| `test_simulationoptionsdialog` must never instantiate the dialog (AUTOMOC → GDAL cascade, `tests/gui/CMakeLists.txt:2406-2429`) | New tests are full-app tests registered like `test_simulationoptions_roundtrip`; the helper file stays leaf-linkable. |
| `PreferencesDialog` has no observer today | §6 tests 9–10 land at the *start* of T7. |
| Shared trees — `src/swmmvis.cpp`, dialog headers, `CMakeLists.txt` carry other in-flight work | Commit per phase; stage hunk-wise (`git add -p`); never `git add -A`. |
| `applyEngineConstraints` semantics drift when it becomes a probe | `EngineCapabilities` fields map 1:1 to the existing `if` blocks at `:168-352`; T1 ports the probe and asserts each field against the current behaviour on the three fixtures. |
| Muscle memory and the manual go stale for relocated controls | Read-only mirrors with jump links (§2.1); T8 doc pass; CHANGELOG entry names every relocation. |
