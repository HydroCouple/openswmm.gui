# HANDOFF — Options dialogs: inner tabs, page classes, single gating mechanism

| | |
|---|---|
| **Design of record** | `workplans/OPTIONS_DIALOG_TABBED_RESTRUCTURE_PLAN_2026-09-07.md` (referred to as **PLAN** below). Read it first, fully. This document tells you *how*; PLAN tells you *what* and *why*. Where they disagree, PLAN wins — stop and report. |
| **Repo** | `openswmm.gui` only. No engine changes. No new option keys. |
| **Branch** | `swmm6_gui`, from HEAD `f059447` or later |
| **Shape of the work** | Nine phases (T0–T8), each ending green on the verification gate (§3) and committed separately (§8). Do not start a phase until the previous one is committed. |
| **Working rules** | `CLAUDE.md` applies in full — especially §3 *Surgical Changes* (touch only what PLAN says), §4.1 *Transparent File IO* (test artifacts under `tests/gui/data/`, never `/tmp`), §5.0 (follow this plan; do not improvise a different structure). |

---

## 0. Invariants — things that must survive every phase

These are the load-bearing seams. Breaking any of them fails an existing test
or an existing convention. Verify each against the source before you begin.

| # | Invariant | Where | Why |
|---|---|---|---|
| I1 | `bool SimulationOptionsDialog::wroteAnyChanges() const` stays public, inline, same name | `include/ui/dialogs/simulationoptionsdialog.h:100` | `test_simulationoptions_roundtrip.cpp` reads it |
| I2 | `onApply()` stays a private **slot** named exactly `onApply` | header `:175-177` (`private slots:` / `onApply` / `onAccept`), `.cpp:4651` | The round-trip test calls it through `QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection)` — the dialog has **no friend classes** and you must not add one |
| I3 | The **eleven** `public static` helpers stay in `src/ui/dialogs/simulationoptionshelpers.cpp` with unchanged signatures: `parseEngineBool`, `engineBoolString`, `fastPresetValues`, `fastPresetThreads`, `formatEngineDateTime`, `parseEngineDateTime`, `oaDateFromQDateTime`, `qDateTimeFromOaDate`, `parseStepSeconds`, `optionValueEquals`, `selectedRowsDescending` | `simulationoptionshelpers.cpp` (193 lines) | `test_simulationoptionsdialog` links **only** that file plus `thememanager` — it must never instantiate the dialog (AUTOMOC → GDAL cascade; read the CMake essay at `tests/gui/CMakeLists.txt:2406-2429`). Do not move a static helper back into the main `.cpp`. |
| I4 | Constructor signature `SimulationOptionsDialog(SWMM_Engine, SWMMModelLayer*, const QString &engineVersion, SWMMVisProjectWindow*, QWidget*)` unchanged (header `:91`; member `m_projectWindow` `:327`) | header | Both existing tests construct it |
| I5 | Apply → `writeToEngine()` → `readFromEngine()` re-read cycle; OK → `writeToEngine()` → `accept()` | `.cpp:4651-4711` | Engine may clamp/normalise; the re-read is how the UI reflects it |
| I6 | The changed-count `n` is accumulated **only** through a numeric-aware `optionValueEquals` compare before `swmm_options_set` | `.cpp:4392-4403` | Otherwise every Apply dirties the project and the round-trip test fails |
| I7 | `PreferencesManager::instance()->simulationDefaults()` / `twoDDefaults()` remain the source of every missing-key fallback in reads | `.cpp:3082`, `read2DFromEngine` | Pinned by `test_twod_defaults_prefs.cpp` and the File → New synthesis path |
| I8 | Every option key keeps its section and spelling. Only widget placement changes. | — | PLAN §4.4 rule 1 |
| I9 | `QGroupBox`es move **whole**; none is split across tabs or pages | — | PLAN §4.4 rule 2 |
| I10 | `PROJECT_SOURCES` in the root `CMakeLists.txt` is an explicit list (`:1390-1409` region). Every new `.cpp` **and** `.h` is added by hand, next to `simulationoptionsdialog.cpp`. The tree never GLOBs sources. | `CMakeLists.txt:172` onward | Full-app GUI tests re-root `PROJECT_SOURCES` (`tests/gui/CMakeLists.txt:2537-2545`), so new files flow into them automatically once listed |
| I11 | Headers use `#ifndef` guards (uppercased bare filename, e.g. `HYDRAULICSPAGE_H`), never `#pragma once`; new classes live in `namespace openswmmvis::ui`; file headers carry the Doxygen block (`\file`, `\author Caleb Buahin <caleb.buahin@gmail.com>`, `\date 2026`, `\license GPL-3.0-or-later`) | any `include/ui/dialogs/*.h` | Repo convention |

---

## 1. Orientation — what you are refactoring

### 1.1 `SimulationOptionsDialog`

| Thing | Location |
|---|---|
| Header / source | `include/ui/dialogs/simulationoptionsdialog.h` (587 lines) · `src/ui/dialogs/simulationoptionsdialog.cpp` (4,711 lines) |
| Sidebar + stack | `m_categoryList` (`QListWidget`), `m_pages` (`QStackedWidget`), `m_2DRow`, `m_meshRow` — header `:357-360` |
| `buildUi()` / `addCategory()` / `set2DRowEnabled()` | `.cpp:355-421` / `:423-427` / `:429-442` |
| Page builders | `buildTitleNotesTab :444`, `buildModelsTab :522`, `buildDatesTab :634`, `buildHydraulicsTab :821`, `buildPerformanceTab :1179`, `buildQualityTransportTab :1368`, `buildSpatialTab :1573`, `buildMeshTab :1711`, `build2DTab :2083` (inside `#ifdef OPENSWMM_HAS_2D`), `buildFilesTab :3381` |
| Read side | `readFromEngine :3071-3380` (monolith), `read2DFromEngine :2375`, `readEventsFromEngine :2617`, `readReportContentsFromEngine :2946`, `readFilesSectionFromEngine :4195`, `readPluginsFromEngine :3881`, `readWriterCombosFromEngine :3973` |
| Write side | `writeToEngine :4392-4645` (monolith; sub-writers folded at `:4626-4640`; `m_wroteChanges` set at `:4642-4643`), `write2DToEngine(int&) :2446`, `writeEventsToEngine :2704`, `writeReportContentsToEngine :3008`, `writeFilesSectionToEngine :4240`, `writePluginsToEngine :4338`, `writeWriterCombosToEngine :3999` |
| Option access | `getOption(key, fallback) :2524-2532` (wraps `swmm_options_get`), `setOption(key, value) :3051-3070` (wraps `swmm_options_set`) |
| Gating (the six to replace) | `applyEngineConstraints :168-352`, `updateSurchargeFieldsEnabled :1317`, `updateFvFieldsEnabled :1330`, `updateQualitySolverFieldsEnabled :1533`, `m_ufSupported` (set in `applyEngineConstraints`, read `:1352`), `syncSkipEnabled` lambda `:733-740` |
| Scroll wrapper | `OpenSWMM::Ui::wrapInScrollArea` (`include/ui/uiscrollhelpers.h:44-50`), called once at `.cpp:426`. **Consequence:** `m_pages->widget(i)` is a `QScrollArea`; the page is `qobject_cast<QScrollArea*>(m_pages->widget(i))->widget()`. |

**Cross-page couplings you must untangle (these are traps):**

| Widget | Built in | Wired / read in | Resolution |
|---|---|---|---|
| `m_routingCombo` (`FLOW_ROUTING`) | `buildModelsTab :540-549` | `buildHydraulicsTab :1125-1132` (connects), `applyEngineConstraints :177-188`, `updateFvFieldsEnabled :1332-1350` | Moves to the Hydraulics page header (PLAN §2.1). Models gets a read-only `QLabel` mirror. |
| `m_skipSteadyBox`, `m_latFlowTolSpin`, `m_sysFlowTolSpin` (group `skipGroup`, "Skip steady state") | `buildDatesTab :712-740` | `syncSkipEnabled :733-740`; **four** read/write sites: `SKIP_STEADY_STATE` read `:3119`, write `:4435-4436`; `LAT_FLOW_TOL`/`SYS_FLOW_TOL` read `:3275-3276`, write `:4532-4535` | The whole group moves to Hydraulics › Dynamic Wave (PLAN §7 Q1). Header filing is already split: the two tolerance spins sit under the `// Tab 3 — Routing & Hydraulics` banner (`.h:404-405`), `m_skipSteadyBox` under Tab 1 (`.h:346`). |
| `m_fvScalarSchemeCombo` (`FV_SCALAR_SCHEME`) | `buildQualityTransportTab :1427` | read `:3296-3297`; write `:4567-4568` | **Stays** on Quality › Eulerian ARD (PLAN §1.4). Do not move it. |
| `FastPreset` (System / Performance) writes `MINIMUM_STEP` | `buildPerformanceTab` | `m_minStepSpin` on Hydraulics | After T4, goes through `HydraulicsPage::setMinimumStep(...)`, never through the context (PLAN §4.1). |

### 1.2 `PreferencesDialog`

| Thing | Location |
|---|---|
| Header / source | `include/ui/dialogs/preferencesdialog.h` (278 lines) · `src/ui/dialogs/preferencesdialog.cpp` (2,222 lines) |
| `buildUi()` — `addCategory` is a **local lambda**, not a member | `.cpp:61-129`; lambda at `:86-89`; category list `:90-104` |
| Page builders to restructure | `buildRenderingPage :405-560`, `buildSimulationDefaultsPage :590-752`, `buildDynamicWaveDefaultsPage :754-920`, `buildTwoDDefaultsPage :922-1163` |
| Read / write / reset | `readFromManager :1462-1664`, `writeToManager :1720-1931` (void — writes every field, no dirty count), `onResetToDefaults :1944-2171`, `applyTwoDDefaultsToWidgets :1674-1718` |
| Extraction precedent | `ObjectDefaultsPage` — `include/ui/dialogs/objectdefaultspage.h`, `src/ui/dialogs/objectdefaultspage.cpp`; `buildObjectDefaultsPage :1666-1672` is a 7-line forwarder. **Copy this pattern** for any page you extract. |
| Existing tests | **None** for the dialog. `tests/gui/test_twod_defaults_prefs.cpp` and `test_object_defaults_prefs.cpp` test `PreferencesManager` only. |

### 1.3 Existing tests you will run every phase

| Test | File | Registered | What it needs |
|---|---|---|---|
| `test_simulationoptions_roundtrip` | `tests/gui/test_simulationoptions_roundtrip.cpp` (200 lines) | `tests/gui/CMakeLists.txt` via `add_swmmvis_gui_test`, full-app link | Fixtures `tests/gui/data/typed_selection_fixture.inp` (DYNWAVE) and `mini_2d.inp` (2D); generates `simopts_roundtrip_fv_variant.inp` and `*_before/_after.inp` next to them. Slots: `dynwave1D`, `twoD`, `finiteVolume`, `editIsDetected`. |
| `test_simoptions_persistence_contract` | `tests/gui/test_simoptions_persistence_contract.cpp` | `:1599` | Engine ABI only; no dialog header. |
| `test_simulationoptionsdialog` | `tests/gui/test_simulationoptionsdialog.cpp` | `:2406-2429` | Static helpers only (I3). |

---

## 2. Contracts — write these exactly

Skeletons are normative for names, signatures and ownership. Bodies are yours.
Everything below goes in `include/ui/dialogs/simoptions/` and
`src/ui/dialogs/simoptions/` unless stated.

### 2.1 `EngineCapabilities` — `include/ui/dialogs/simoptions/enginecapabilities.h`

```cpp
namespace openswmmvis::ui {
/*! One-shot capability probe result. Replaces the in-place setEnabled() calls
 *  of SimulationOptionsDialog::applyEngineConstraints(). Probed once in the
 *  dialog constructor; never re-probed. Each field maps 1:1 to one `if` block
 *  at simulationoptionsdialog.cpp:168-352 — keep that mapping in a comment
 *  next to each field so the port is auditable. */
struct EngineCapabilities {
    QString engineVersion;
    bool fv        = false;   ///< FLOW_ROUTING FV accepted            (was: m_routingCombo FV item disable, :177-188)
    bool uf        = false;   ///< UF_* keys accepted                  (was: m_ufSupported)
    bool has2D     = false;   ///< OPENSWMM_HAS_2D && engine reports 2D
    bool tpa       = false;   ///< SURCHARGE_METHOD TPA accepted       (was: :243-265 probe + tooltips)
    bool dynSlot   = false;   ///< SURCHARGE_METHOD DYNAMIC_SLOT accepted (was: :293-310, the legacy item-disable)
    // …one field per remaining probe. Do not collapse two probes into one field.
};
EngineCapabilities probeEngineCapabilities(SWMM_Engine e, const QString &version);
}
```

### 2.2 `SimOptionsContext` — `include/ui/dialogs/simoptions/simoptionscontext.h`

```cpp
namespace openswmmvis::ui {
class SimOptionsContext {
public:
    SimOptionsContext(SWMM_Engine e, SWMMModelLayer *layer, SWMMVisProjectWindow *pw,
                      EngineCapabilities caps);

    SWMM_Engine           engine()        const { return engine_; }
    SWMMModelLayer       *modelLayer()    const { return layer_; }
    SWMMVisProjectWindow *projectWindow() const { return pw_; }
    const EngineCapabilities &caps() const { return caps_; }
    const PreferencesManager::SimulationDefaults &simDefaults() const;  // cached per read pass

    /// swmm_options_get, trimmed; `fallback` when the engine has no value or no engine.
    QString option(const char *key, const QString &fallback = {}) const;

    /// Numeric-aware compare (SimulationOptionsDialog::optionValueEquals) against
    /// option(key); only if different, swmm_options_set. Returns 1 if written else 0.
    /// ALWAYS records `key` in writtenKeys(), written or not (test seam, §5.1 test 6).
    int  writeIfChanged(const char *key, const QString &newVal);

    void beginWritePass();              // clears writtenKeys()
    QStringList writtenKeys() const;

private:
    SWMM_Engine engine_; SWMMModelLayer *layer_; SWMMVisProjectWindow *pw_;
    EngineCapabilities caps_;
    QStringList written_;
};
}
```

`option()` is the body of today's `getOption()` (`:2524-2532`). `writeIfChanged()`
is the body of today's lambda (`:4395-4403`) plus the recording line.

### 2.3 `SimOptionsPage` — `include/ui/dialogs/simoptions/simoptionspage.h`

```cpp
namespace openswmmvis::ui {
class SimOptionsPage : public QWidget {
    Q_OBJECT
public:
    explicit SimOptionsPage(SimOptionsContext &ctx, QWidget *parent = nullptr);
    virtual QString title() const = 0;      ///< Sidebar row text (tr()'d)
    virtual void    read()  = 0;            ///< engine → widgets. Block signals while setting.
    virtual int     write() = 0;            ///< widgets → engine via ctx_.writeIfChanged; return the sum.
    virtual void    refreshGates() {}       ///< Intra-page widget gates (PLAN §4.3 "widget" rows)
    virtual void    applyCapabilities() {}  ///< One-shot per-widget disable/tooltip from ctx_.caps()
    virtual bool    validate(QString *warn) { Q_UNUSED(warn); return true; }  ///< Events / Files gates today
signals:
    void gateInputsChanged();               ///< Emit from any control that feeds a PageGate
protected:
    SimOptionsContext &ctx_;
    /// Tag an option widget for the reachability test. Call for EVERY widget that
    /// reads/writes an option key: tagOption(m_minSlopeSpin, "MIN_SLOPE");
    static void tagOption(QWidget *w, const char *key);
};
}
```

`tagOption` sets `w->setProperty("optionKey", QString::fromLatin1(key))`. Widgets
that edit a non-`[OPTIONS]` thing (title text, events table, plugins table,
report flags) are tagged with the section name instead: `tagOption(w, "[EVENTS]")`.

### 2.4 Page classes — one pair each

| Class | Files | Absorbs (from `simulationoptionsdialog.cpp`) |
|---|---|---|
| `TitleNotesPage` | `titlenotespage.{h,cpp}` | `buildTitleNotesTab`, its read/write blocks |
| `ModelsPage` | `modelspage.{h,cpp}` | `buildModelsTab` minus `m_routingCombo`; + mirror label |
| `DatesPage` | `datespage.{h,cpp}` | `buildDatesTab` minus `skipGroup`; `readEventsFromEngine`, `writeEventsToEngine`, `validateEvents`, `addEventRow`, `removeSelectedEventRows`, `updateDurationLabel` |
| `HydraulicsPage` | `hydraulicspage.{h,cpp}` | `buildHydraulicsTab` + `m_routingCombo` + `skipGroup`; owns `hydraulicsTabs`; exposes `QString flowRouting() const`, `void setMinimumStep(double)`, `QTabWidget *tabs()` |
| `QualityPage` | `qualitypage.{h,cpp}` | `buildQualityTransportTab`; owns `qualityTabs`; exposes `QString qualitySolver() const` |
| `PerformancePage` | `performancepage.{h,cpp}` | `buildPerformanceTab`, `refreshThreadsEffectiveLabel`, FastPreset (calls `HydraulicsPage::setMinimumStep`) |
| `SpatialPage` | `spatialpage.{h,cpp}` | `buildSpatialTab`, `refreshSpatialSummary`, `onSpatialPickCRS`, `onSpatialDetectCRS` |
| `MeshPage` | `meshpage.{h,cpp}` | `buildMeshTab`, `refreshMeshList`, `onMeshSetActive`, `onMeshRemove`, `onMeshImport`, `on2DModuleToggled` |
| `TwoDPage` | `twodpage.{h,cpp}` | `build2DTab`, `read2DFromEngine`, `write2DToEngine`; whole class inside `#ifdef OPENSWMM_HAS_2D`; owns `twoDTabs` |
| `FilesPage` | `filespage.{h,cpp}` | `buildFilesTab`, `buildReportContentsGroup`, all `read*/write*` for report contents, writer combos, output paths, files section, plugins, hot-start saves, `validateFilesTab` |

Widget members move to the page with the same names (drop nothing, rename
nothing). `objectName`s that the tests look up (§5): `flowRoutingCombo`,
`qualitySolverCombo`, `module2DBox`, `hydraulicsTabs`, `qualityTabs`, `twoDTabs`,
`datesTabs`, `modelsTabs`, `skipSteadyBox`.

### 2.5 `PageGate` + helpers — in `simulationoptionsdialog.h/.cpp`

```cpp
struct PageGate {
    enum class Target { SidebarRow, Tab, Widget };
    Target                target;
    int                   row      = -1;
    QTabWidget           *tabs     = nullptr;
    int                   tabIndex = -1;
    QWidget              *widget   = nullptr;
    std::function<bool()> enabled;
    QString               reasonWhenOff;
};

void SimulationOptionsDialog::refreshGates();           // applies m_gates in order, then redirects
static void applyRowGate(QListWidget*, int row, bool on, const QString &reason);   // = set2DRowEnabled body + tooltip
static void applyTabGate(QTabWidget*, int idx, bool on, const QString &reason);
```

`applyTabGate` **must** do, in this order: `setTabEnabled(idx, on)`;
`setTabToolTip(idx, on ? QString() : reason)`; if `!on && currentIndex()==idx`,
`setCurrentIndex(first i with isTabEnabled(i))`. `applyRowGate` keeps the
existing redirect-to-row-0 (`:439-441`) and adds `item->setToolTip(reason)`.

Row gates read the pages, not widgets: e.g. the 2D row gate is
`[this]{ return m_modelsPage->module2DEnabled(); }`. The gate table is PLAN
§4.3 verbatim; build it in one function `buildGateTable()` called once after
all pages exist.

### 2.6 Registry — in `simulationoptionsdialog.cpp`

```cpp
QVector<SimOptionsPage*> m_pageOrder;     // sidebar order
void addPage(SimOptionsPage *p) { m_pageOrder << p; addCategory(p->title(), p); }
```

`readFromEngine()` → `for (p : m_pageOrder) p->read(); refreshGates();`
`writeToEngine()` → `ctx.beginWritePass(); int n=0; for (p) n += p->write(); if (n>0) m_wroteChanges=true; return n;`
`onApply/onAccept` validation → `for (p) if (!p->validate(&warn)) { QMessageBox…; return; }` — preserving today's two-message behaviour (`:4651-4711`) by having `DatesPage::validate` and `FilesPage::validate` return the same strings.

---

## 3. Build and test — exact commands

The configured tree on this machine is **`build/`** (Ninja, Release). Do not
configure a second tree.

```bash
cd /Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui

# Full build (writes build/build-gui.log; prints BUILD_STATUS=0 on success)
./build-gui.sh

# Just the app + one test target (faster inner loop)
cmake --build build --target SWMMVis test_simulationoptions_roundtrip -j 8

# The verification gate (§5) — run ALL of these at the end of every phase
ctest --test-dir build --output-on-failure -R '^test_simulationoptions_roundtrip$'
ctest --test-dir build --output-on-failure -R '^test_simoptions_persistence_contract$'
ctest --test-dir build --output-on-failure -R '^test_simulationoptionsdialog$'
ctest --test-dir build --output-on-failure -R '^test_simulationoptions_gates$'        # from T0
ctest --test-dir build --output-on-failure -R '^test_preferencesdialog_roundtrip$'    # from T7
ctest --test-dir build --output-on-failure -R '^test_twod_defaults_prefs$'
ctest --test-dir build --output-on-failure -R '^test_object_defaults_prefs$'

# Whole gui label (slow; run before each commit)
ctest --test-dir build --output-on-failure -L gui
```

CTest supplies `QT_QPA_PLATFORM=offscreen`, `SWMMVIS_GUI_TEST_DATA=<repo>/tests/gui/data`,
`QTEST_FUNCTION_TIMEOUT=30000`. If you run a test binary directly, export those
three yourself.

**Registering a new full-app test** — copy the `test_simulationoptions_roundtrip`
block in `tests/gui/CMakeLists.txt` verbatim, rename, and keep
`swmmvis_link_full_app_deps`. Never register a test that includes
`simulationoptionsdialog.h` as a leaf test (I3).

**Hang diagnosis.** If a dialog test stalls for ~30 s then fails with a
timeout, `onApply` popped a `QMessageBox` (a validation warning) on a fixture
that was clean before your change. That is a regression in `validate()`, not a
test to relax. Find it by running the binary directly with
`QT_QPA_PLATFORM=offscreen` and `QT_LOGGING_RULES='*.debug=true'`.

---

## 4. Phases — do them in this order

Each phase: (1) read the listed source ranges, (2) implement, (3) build, (4)
run the full §3 gate, (5) commit per §8, (6) append to the report (§9). Do not
begin the next phase with a red gate.

### T0 — Baseline and the test seam (no layout change)

1. Run the three existing tests. Record pass/fail verbatim in §9. If
   `test_simulationoptions_roundtrip` is **red on the untouched tree**, stop
   and report — that is a pre-existing identity bug, and PLAN forbids patching
   the dialog to hide it.
2. Add `tagOption(w, key)` to the header as a `private static` on the dialog
   for now (moves to `SimOptionsPage` in T1) and call it for **every** option
   widget in every `build*Tab()`. Do it mechanically: for each `writeIfChanged("KEY", …, m_widget…)` line in `writeToEngine` and each sub-writer, tag the widget with `KEY`. Non-`[OPTIONS]` editors get the section name. Add `setObjectName` for the nine names in §2.4.
3. Add `writtenKeys()` recording to the existing `writeIfChanged` lambda and
   the sub-writers (a `QStringList m_lastWriteKeys` on the dialog, public
   getter `lastWriteKeys()`; in T1 it moves to the context, and the getter
   forwards).
4. Create `tests/gui/test_simulationoptions_gates.cpp` with tests **6** and
   **8** from §5.1 only, written against the *current* layout (8 asserts the
   ten current row titles; no `QTabWidget`s exist yet, so the tab part of 8 is
   empty). Register it.
5. Gate green. Commit.

**Accept:** tests 1–3 green; test 6 proves every current option widget is laid
out and every written key has a tagged editor. If test 6(ii) finds a key with
no tagged widget, tag it — that is the point of T0. If it finds a widget with
no parent chain to `m_pages`, **report it**; it is a pre-existing orphan.

### T1 — Context, capabilities, page base, and one page

1. Read `applyEngineConstraints :168-352` end to end. Write
   `probeEngineCapabilities()` with one field per `if` block (§2.1). Keep the
   per-widget `setEnabled`/`setToolTip` calls where they are for now; they
   move into pages' `applyCapabilities()` as each page is ported.
2. Write `SimOptionsContext` (§2.2) and `SimOptionsPage` (§2.3). Move
   `tagOption` and the key recording into them.
3. Port **`SpatialPage`** (`buildSpatialTab :1573-1710`, its read block, its
   write block, `refreshSpatialSummary`, the two CRS slots). Register it
   through `addPage`. Remove the corresponding lines from the monolith.
4. Convert `readFromEngine` / `writeToEngine` to the fan-out shape (§2.6)
   with the not-yet-ported pages still handled inline below the loop.
5. Add the new files to `PROJECT_SOURCES`. Gate green. Commit.

**Accept:** round trip `n == 0` on all three decks; the CRS pick path still
sets `m_wroteChanges` (`:1679` today).

### T2 — Port the simple pages (class split only)

`TitleNotesPage`, `PerformancePage`, `MeshPage`, `FilesPage` — in that order,
one commit each or one commit for all four, your call, but the gate runs after
each. No layout changes. `FilesPage` is the largest (it owns ~1,300 lines of
read/write/validate); port it last and leave its inner tabs exactly as they
are.

**Accept:** gate green after each page; `validateFilesTab` messages unchanged
(diff the strings).

### T3 — `PageGate`, `refreshGates()`, redirect helpers

1. Implement §2.5. Build the gate table with **row** and **widget** gates only
   (no tabs exist yet): the 2D row, the Quality row (new — today `IGNORE_QUALITY`
   gates nothing), the Surcharge sub-widgets, FV limiter/tiers, UF k3, the
   two flow tolerances.
2. Delete `set2DRowEnabled`, `updateSurchargeFieldsEnabled`,
   `updateFvFieldsEnabled`, `updateQualitySolverFieldsEnabled`, the
   `syncSkipEnabled` lambda, and the `m_ufSupported` member. Every call site
   becomes `emit gateInputsChanged()` on the owning page (or, while a page is
   still inline in the dialog, a direct `refreshGates()` call).
3. `applyEngineConstraints` becomes: probe (already done in T1) + a loop
   calling `p->applyCapabilities()` + `refreshGates()`.
4. Add §5.1 tests **4a** and **5** (row variant) to the gates test.
5. Gate green. Commit.

**Accept:** with the sticky-child-disable note at `:267-268` (restated `:276-279`) in mind, verify
manually on `typed_selection_fixture.inp` that toggling the 2D module off
and on re-enables the 2D row and its children; toggling `SURCHARGE_METHOD`
enables the right sub-widgets. Tests 4a/5 pass.

### T4 — Routing & Hydraulics (the headline page)

1. Create `HydraulicsPage`. Layout, top to bottom:
   - Header `QFormLayout`: **Flow routing** — `m_routingCombo` (objectName
     `flowRoutingCombo`), same items and tooltip as `:540-549`. Connect
     `currentIndexChanged` → `emit gateInputsChanged()`.
   - `QTabWidget` `hydraulicsTabs`, tabs in this order and with these exact
     titles: **Routing** · **Dynamic Wave** · **Finite Volume** · **Unsteady Friction**.
   - Routing tab: `condGroup` (`:1135`).
   - Dynamic Wave tab: `surGroup` (`:827`), `solGroup` (`:877`), then
     `skipGroup` moved from `buildDatesTab :712-740` **whole**, including
     `m_skipSteadyBox`, both tolerance spins, and all **four** read/write
     sites (`SKIP_STEADY_STATE` read `:3119`, write `:4435-4436`;
     tolerances read `:3275-3276`, write `:4532-4535`).
   - Finite Volume tab: `m_fvGroup` (`:937`), `m_fvPerfGroup` (`:1046`).
   - Unsteady Friction tab: `m_ufGroup` (`:1099`).
2. In `ModelsPage`, replace `m_routingCombo` with a `QLabel` mirror
   ("Flow routing: **Dynamic Wave** — <a href=#>change on Routing &amp; Hydraulics</a>").
   Connect the link to `m_categoryList->setCurrentRow(hydraulicsRow)`.
   Refresh the label from `HydraulicsPage::gateInputsChanged` and after
   `read()`.
3. Add the three **tab** gates to the gate table (PLAN §4.3 rows for Dynamic
   Wave / Finite Volume / Unsteady Friction). Wire the FV-item disable from
   `:177-188` into `HydraulicsPage::applyCapabilities()`.
4. `PerformancePage`'s FastPreset now calls `m_hydraulicsPage->setMinimumStep(...)`.
5. **PLAN §7 Q1 check.** In the engine tree (sibling checkout `openswmm.engine`,
   search `SKIP_STEADY_STATE` in the routing/step code): if it is honoured
   under KINWAVE/STEADY as well as DYNWAVE, move `skipGroup` to the **Routing**
   tab instead and note it in §9. One line of layout changes; nothing else.
6. Add §5.1 tests **4b**, **5** (tab variant), **7**, and the tab titles part
   of **8**.
7. Gate green. Commit.

**Accept:** test 7 — no vertical scroll on any Hydraulics tab at 1280×800;
test 5 — Dynamic Wave → FV redirect lands on Finite Volume; `dynwave1D` and
`finiteVolume` round trips `n == 0`; `fastPresetHasBalancedRecipe` green.

### T5 — 2D Surface Routing

1. Create `TwoDPage` (`#ifdef OPENSWMM_HAS_2D` around the whole class and its
   registration, exactly as `build2DTab` is guarded today). `QTabWidget`
   `twoDTabs`: **Hydrodynamics** (`stepGroup :2091`, `m_marcherGroup :2105`) ·
   **Mesh & Closure** (`meshGroup :2216`, `closureGroup :2242`) · **Coupling**
   (`coupGroup :2282`) · **Rainfall & Output** (`rainfallGroup :2319`,
   `perfGroup :2193`, `outGroup :2338`).
2. `read2DFromEngine` / `write2DToEngine(int&)` become `read()` / `write()`.
   `write2DToEngine` takes `n` by reference today (`:2446`); `write()` returns
   its own count instead.
3. Gate green (`twoD` round trip on `mini_2d.inp`; test 7 for `twoDTabs`).
   Commit.

### T6 — Quality & Transport, Dates & Times, Models / Processes

1. `QualityPage`: header `qualitySolverCombo` (moved out of `solGroup :1374`);
   `qualityTabs`: **Solver** (rest of `solGroup`) · **Eulerian ARD**
   (`m_ardGroup :1409`, keeps `m_fvScalarSchemeCombo`) · **Lagrangian (LARD)**
   (`m_lardGroup :1440`) · **Reserved Species** (`resGroup :1476`). Two tab
   gates per PLAN §4.3. `updateQualitySolverFieldsEnabled`'s logic is now the
   two tab gates — delete any remainder.
2. `DatesPage`: `datesTabs`: **Simulation Window** (`winGroup :640`,
   `sweepGroup :745`) · **Time Steps** (`stepGroup :676`) · **Events**
   (`evGroup :777`). `skipGroup` is already gone (T4).
3. `ModelsPage`: `modelsTabs`: **Domains & Processes** (`procGroup :527`
   minus the routing combo, plus the mirror; `ignoreGroup :607`) · **Modules**
   (`modulesGroup :560`) · **Flags** (`flagsGroup :596`). Leave an empty
   `QVBoxLayout` slot named `matrixPreviewSlot` below the tab widget
   (PLAN §1.3).
4. At this point `simulationoptionsdialog.cpp` should contain no `build*Tab`,
   no inline read/write blocks, and be ≤ 700 lines. If it is not, something
   was left behind — find it before committing.
5. Complete test **8** (all sidebar titles + all tab titles). Gate green. Commit.

### T7 — `PreferencesDialog`

1. **Tests first.** Create `tests/gui/test_preferencesdialog_roundtrip.cpp`
   per §5.2 and get it green on the **untouched** dialog. How `QSettings` is
   scoped (verified): `PreferencesManager` holds a single default-constructed
   `QSettings m_settings;` (`include/core/preferencesmanager.h:658`) inside a
   function-local static singleton (`preferencesmanager.cpp:147-158`) — it
   binds to `QCoreApplication::organizationName()/applicationName()` and the
   platform native format **at first `instance()` call**, and there is no
   reload or test hook. So, in `initTestCase()` and **before anything calls
   `PreferencesManager::instance()`**, copy the redirect from
   `tests/gui/test_profile_attribute_tracks.cpp:63-66` verbatim:
   `QCoreApplication::setOrganizationName("openswmm-test")`,
   `setApplicationName("test_preferencesdialog_roundtrip")`,
   `QSettings::setDefaultFormat(QSettings::IniFormat)` (mandatory on macOS —
   `setPath` is ignored for `NativeFormat`), then
   `QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, "<repo>/tests/gui/data/prefs_roundtrip_artifacts")`.
   No `PreferencesManager` change is needed. Never write to the user's real
   settings (org `hydrocouple`, app `SWMMVis Stormwater Management Model` —
   `src/swmmvisapplication.cpp:53-58`).
2. `buildRenderingPage :405-560` → `QTabWidget` `renderingTabs`: **Labels**
   (`lodGroup :424`) · **Links & Nodes** (`linkGroup :433`, `nodeGroup :466`) ·
   **GPU** (`gpuGroup :497`) · **2D Mesh Edges** (`bcGroup :523`).
3. Merge: `buildSimulationDefaultsPage`, `buildDynamicWaveDefaultsPage`,
   `buildTwoDDefaultsPage` → one `buildSimulationDefaultsPage` returning a
   `QTabWidget` `simulationDefaultsTabs`: **Processes & Modules**
   (`procGroup :605`, `togGroup :632`) · **Hydraulics & Schedule**
   (`geomGroup :669`, `schedGroup :681`) · **Dynamic Wave: Steps & Tolerances**
   (`stepGroup :700`, `tolGroup :731`, `vsGroup :829`) · **Dynamic Wave: Solver**
   (`condGroup :769`, `solvGroup :871`) · **2D** (`solvGroup :940`,
   `wetGroup :987`, `cplGroup :1026`, `rainGroup :1048`, `meshGroup :1064`).
   Remove the two `addCategory` lines for the folded rows (`:96`, `:97`).
   `openAtCategory(const QString&)` (`:1450-1460`) has exactly one caller
   (`src/swmmvisactions.cpp:90`, `"Keyboard"`) — nothing to repoint; leave
   `openAtCategory` alone. The only other occurrences of the folded row names
   are comments (`preferencesdialog.cpp:1604`, `:1831`, `preferencesdialog.h:196`,
   `meshgenerationdialog.cpp:3990-3991`, `simulationoptionsdialog.cpp:2399`,
   `src/swmmvis.cpp:4499`); fix the ones in files you already own, and leave
   the two in do-not-touch files (§6) for the author.
4. `readFromManager` / `writeToManager` / `onResetToDefaults` /
   `applyTwoDDefaultsToWidgets` are untouched except for widget parents.
5. Optional, only if a page becomes hard to reason about: extract it as a
   widget class on the `ObjectDefaultsPage` pattern. Not required.
6. Gate green (tests 9, 10, and the two `*_defaults_prefs` tests). Commit.

### T8 — Docs and changelog

1. `docs/manual/18_simulation_options.md` — rewrite the page-by-page section
   to match PLAN §2 (tabs, the `FLOW_ROUTING` header, the Skip-steady-state
   move). Grep the manual for `Routing & Hydraulics` (5 files), `2D Surface
   Routing` (14 files), `Flow routing` (4 files) and fix every navigation
   sentence that names a page or group that moved. Tutorials
   `t01`, `t03`, `t04`, `t06` are the heavy ones.
2. `docs/manual/04_preferences.md` — the 15→12 row change; `03_projects.md`
   and `01_introduction.md` mention the folded rows.
3. `docs/manual/images/TODO.md` — add screenshot lines for each new tab set.
4. `CHANGELOG.md` → `## [Unreleased]` → `### Changed`, Keep-a-Changelog style,
   one bullet per dialog, bold em-dash lead, ~100-col wrap, listing **every**
   relocation by key name.
5. Commit.

---

## 5. Test specifications

### 5.1 `tests/gui/test_simulationoptions_gates.cpp` — full-app test

Construct the dialog exactly as `test_simulationoptions_roundtrip.cpp:53-59`
and `:118-130` do (`openLayer` → `SWMMModelLayer::loadModel` → `engine()` →
`SimulationOptionsDialog dlg(e, layer.get(), "6.0.0", nullptr, nullptr)`).
Look widgets up **only** by `objectName` via `dlg.findChild<T*>(name)` — no
friends, no new accessors beyond `lastWriteKeys()`. Helper you will need:

```cpp
static QWidget *pageAt(QStackedWidget *stack, int row) {
    auto *sa = qobject_cast<QScrollArea*>(stack->widget(row));
    return sa ? sa->widget() : stack->widget(row);
}
```

| Slot | Steps | Asserts |
|---|---|---|
| `twoDRowGate` (4a) | `module2DBox->setChecked(false)` | `m_categoryList` item for the 2D row has `Qt::NoItemFlags`; tooltip non-empty; `setChecked(true)` → flags restored |
| `qualityRowGate` (4a) | fixture with pollutants vs. `mini_2d.inp` (none) | row enabled / disabled accordingly |
| `hydraulicsTabGates` (4b) | for each of `STEADY, KINWAVE, DYNWAVE, FV`: `flowRoutingCombo->setCurrentIndex(findData(v))` | `hydraulicsTabs->isTabEnabled(i)` for i∈{Routing, Dynamic Wave, Finite Volume, Unsteady Friction} equals `{1,0,0,0}`, `{1,0,0,0}`, `{1,1,0,caps.uf}`, `{1,0,caps.fv,caps.uf}` |
| `qualityTabGates` (4b) | set `qualitySolverCombo` to each value | ARD / LARD tabs flip |
| `tabRedirect` (5) | select Dynamic Wave tab, set routing FV | `currentIndex()` == Finite Volume index; `isTabEnabled(currentIndex())` |
| `rowRedirect` (5) | select 2D row, uncheck `module2DBox` | `currentRow()` != 2D row and is enabled |
| `reachability` (6) | `invokeMethod("onApply")`; collect `findChildren<QWidget*>()` with non-null `optionKey` | (i) each has an ancestor == `m_pages` (walk `parentWidget()`); (ii) `QSet(optionKeys) ⊇ QSet(dlg.lastWriteKeys())` — on failure print the difference |
| `noScrollAt1280x800` (7) | `dlg.resize(1280,800); dlg.show(); QTest::qWait(50)`; for each row: `m_categoryList->setCurrentRow(r)`; for each `QTabWidget` found in that page, for each tab `setCurrentIndex(t)`; `QCoreApplication::processEvents()` | `qobject_cast<QScrollArea*>(m_pages->widget(r))->verticalScrollBar()->maximum() == 0`. Skip rows whose page is a table-dominated list (Mesh, Files — they legitimately scroll their tables, not the page). Name the failing row/tab in the message. |
| `structure` (8) | — | `m_categoryList` titles == PLAN §2 order; each named `QTabWidget`'s `tabText(i)` sequence == PLAN §2 |

`m_categoryList` / `m_pages` are private; find them by `objectName` too — the
stack already has `setObjectName("pages")` (`.cpp:377`); give the list
`setObjectName("categories")` in T0.

### 5.2 `tests/gui/test_preferencesdialog_roundtrip.cpp` — full-app test

| Slot | Steps | Asserts |
|---|---|---|
| `applyWithNoEditsWritesNothing` (9) | redirect `QSettings` to `tests/gui/data/prefs_roundtrip_artifacts/`; snapshot all keys/values; construct `PreferencesDialog`; `invokeMethod("onApply")`; re-snapshot | maps equal. If `PreferencesManager` normalises on write (e.g. writes defaults for absent keys), the *first* Apply may differ — then snapshot after one Apply and compare against a second. Document which in the test header. |
| `structure` (10) | — | 12 row titles per PLAN §3; `renderingTabs` and `simulationDefaultsTabs` titles per PLAN §3 |
| `noScrollAt1280x800` (10) | as 5.1 test 7 | scrollbar max 0 on Rendering and Simulation Defaults tabs |

---

## 6. Things you must not do

- Do not add `friend class` to either dialog.
- Do not move a static helper out of `simulationoptionshelpers.cpp`.
- Do not split a `QGroupBox`.
- Do not move `FV_SCALAR_SCHEME`.
- Do not change any option key's spelling, section, formatting precision, or
  default source.
- Do not regroup the Models / Processes page content (PLAN §7 Q3).
- Do not merge the Mesh and 2D rows (PLAN §7 Q4).
- Do not touch files that carry another session's uncommitted work. As of
  `f059447` (`git status` at T0 is authoritative — re-check): `src/swmmvis.cpp`,
  `include/swmmvis.h`, `src/ui/dialogs/climatologydialog.*`,
  `heatconfigdialog.*`, `meshgenerationdialog.*`, `src/mesh/*`,
  `include/mesh/*`, `docs/manual/15_climate.md`,
  `docs/manual/appendices/a02_file_formats.md`,
  `tests/gui/test_climatologydialog.cpp`, `tests/gui/test_meshquadpoints.cpp`,
  `tests/gui/test_meshquadregion_e2e.cpp`, and
  `workplans/GW_TRANSPORT_HYDROCOUPLE_COMPONENT_PROGRAM_PLAN_2026-09-05.md`.
  If a change you need lands in one of them, stop and report. The T8 doc
  sweep must skip `a02_file_formats.md`.
- Do not write test artifacts anywhere but `tests/gui/data/`.
- Do not "improve" adjacent code, comments, or formatting (CLAUDE.md §3).

---

## 7. Stop conditions — halt and report instead of guessing

1. `test_simulationoptions_roundtrip` is red on the untouched tree (T0 step 1).
2. Test 6 finds a pre-existing orphaned widget (T0 step 5).
3. A widget's read or write line cannot be attributed to exactly one page.
4. A gate in PLAN §4.3 needs a value no page exposes.
5. `EngineCapabilities` needs a probe that is not one of the existing `if`
   blocks at `:168-352`.
6. The §7 Q1 engine check (T4 step 5) is ambiguous.
7. Test 9 cannot be made green on the untouched dialog with the
   `test_profile_attribute_tracks.cpp:63-66` redirect (T7 step 1) — i.e. the
   first Apply is not idempotent even after a warm-up Apply.
8. Any file in §6's do-not-touch list needs an edit.
9. `simulationoptionsdialog.cpp` is still > 700 lines after T6 and you cannot
   see why.

---

## 8. Commit discipline

- One commit per phase (T2 may be one per page). Never `git add -A` or
  `git add .` — the tree carries unrelated uncommitted work (`git status` at
  the start of T0 shows it; leave it exactly as found).
- Stage new files whole; stage edits to existing files with `git add -p` and
  take only your hunks. `CMakeLists.txt` and `tests/gui/CMakeLists.txt` are
  shared — hunk-wise there too.
- Run `ctest --test-dir build -L gui` green before every commit.
- Messages, first line ≤ 72 chars, body explains *why*:

```
T0: SimOptions — tag option widgets, record written keys, gates test scaffold
T1: SimOptions — SimOptionsContext, EngineCapabilities, SimOptionsPage; port Spatial
T2: SimOptions — port TitleNotes, Performance, Mesh, Files as page classes
T3: SimOptions — PageGate + refreshGates() replace six ad-hoc gating paths
T4: SimOptions — Routing & Hydraulics: four tabs, FLOW_ROUTING header, skip-steady move
T5: SimOptions — 2D Surface Routing: four tabs
T6: SimOptions — Quality, Dates, Models tabbed; monolith reduced to registry + gates
T7: Preferences — Rendering tabs; fold Dynamic Wave / 2D Defaults into Simulation Defaults
T8: Docs + CHANGELOG for the options-dialog restructure
```

---

## 9. Report back — fill this in as you go, reply with it at the end

```
### Baseline (T0 step 1, untouched tree)
test_simulationoptions_roundtrip:      PASS/FAIL  <verbatim failure if any>
test_simoptions_persistence_contract:  PASS/FAIL
test_simulationoptionsdialog:          PASS/FAIL

### Per phase
| Phase | Commit | Gate | Notes / deviations from PLAN |
|-------|--------|------|------------------------------|
| T0 | <sha> | green | reachability: N widgets tagged, M keys recorded; orphans: none / <list> |
| T1 | <sha> | green | EngineCapabilities fields: <list, one per if-block> |
| T2 | <sha> | green | |
| T3 | <sha> | green | |
| T4 | <sha> | green | §7 Q1 engine check result: <DYNWAVE-only / all routing> → skipGroup on <tab> |
| T5 | <sha> | green | |
| T6 | <sha> | green | simulationoptionsdialog.cpp final line count: <n> |
| T7 | <sha> | green | QSettings redirect mechanism used: <…> |
| T8 | <sha> | green | files touched in docs/manual: <list> |

### Test 7 / 10 (no-scroll) results per page and tab
<table: row / tab / scrollbar max at 1280×800>

### Stop conditions hit
none / <which, and what you observed>

### Open questions for the author
<…>
```
