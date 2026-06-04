# Simulation Options Dialog — Hydration Review & Active-Model Binding Plan

**Scope:** `SimulationOptionsDialog` (`include/ui/dialogs/simulationoptionsdialog.h`,
`src/ui/dialogs/simulationoptionsdialog.cpp`, helpers in
`simulationoptionshelpers.cpp`).

**Date:** 2026-06-02

This document has two parts:

1. A review confirming that the modeling attributes shown when the dialog opens
   are correctly hooked to the relevant UI widgets (the read/write round-trip).
2. A plan to integrate an **active-model binding** entry point so the dialog can
   be re-pointed at a different project/model without being reconstructed.

---

## Part 1 — Hydration Review

### 1.1 Round-trip architecture

The dialog follows a single, consistent contract for every option key:

- **On construction** (`SimulationOptionsDialog::SimulationOptionsDialog`, ~L87)
  it calls, in order: `buildUi()` → `readFromEngine()` → `refreshSpatialSummary()`
  → `applyEngineConstraints()`.
- **Read** goes through `getOption(key, fallback)` (L1272), a thin wrapper over
  `swmm_options_get`. When the engine has no value for a key, the fallback is
  sourced from `PreferencesManager::instance()->simulationDefaults()` so the
  missing-key path and the new-project synthesis path stay in lockstep.
- **Write** goes through `setOption(key, value)` (L1763). When a layer is bound
  it routes through `SWMMModelLayer::setOption()`, which emits `optionsChanged()`
  so the main window + status bar live-sync. It falls back to the raw
  `swmm_options_set` engine API only when no layer is bound (engine-only tests).
- **Apply / OK** (`onApply` L3209, `onAccept` L3244) validate the Events and
  Files/Plugins tabs, call `writeToEngine()` (which diffs each key against the
  last-read value and writes only what changed), then re-hydrate so controls
  reflect any engine-side clamping. `m_wroteChanges` flips when ≥1 key is
  written, which the caller (`SWMMVis::onSimulationOptions`, L4847) uses to mark
  the project dirty and re-run the status-bar activate handler.

**Conclusion:** the read path and write path are symmetric and complete. Every
widget enumerated below hydrates on open and writes back on Apply/OK. The menu
action (`ui->actionOptions`) is connected at `swmmvis.cpp:2558`.

### 1.2 Attribute → widget map (verified)

| Tab | OPTIONS key(s) | Widget | Read | Write |
|-----|----------------|--------|------|-------|
| Title/Notes | `[TITLE]` (+ .oswp HTML) | `m_titleNotesEdit` | L1800 | L3042 |
| Models | `INFILTRATION` | `m_infiltrationCombo` | L1827 | L3065 |
| Models | `FLOW_ROUTING` | `m_routingCombo` | L1828 | L3067 |
| Models | `ALLOW_PONDING` | `m_allowPondingBox` | L1830 | L3069 |
| Models | `SKIP_STEADY_STATE` | `m_skipSteadyBox` | L1831 | L3071 |
| Models | `IGNORE_RAINFALL/SNOWMELT/GROUNDWATER/RDII/QUALITY/ROUTING` | `m_ignore*Box` (inverted: checked = active) | L1833 | L3074 |
| Models | 2D module flag (QSettings, per-.inp) | `m_module2DBox` | L1993 | L3175 |
| Dates | `START/END/REPORT_START_DATE+TIME` | `m_startEdit`/`m_endEdit`/`m_reportStartEdit` | L1848 | L3089 |
| Dates | `REPORT_STEP`/`DRY_STEP`/`WET_STEP`/`RULE_STEP`/`ROUTING_STEP`/`DRY_DAYS` | step edits | L1886 | L3101 |
| Dates | `SWEEP_START`/`SWEEP_END` | `m_sweep*Edit` | L1931 | L3124 |
| Dates | `[EVENTS]` | `m_eventsTable` | `readEventsFromEngine` L1937 | `writeEventsToEngine` L3131 |
| Hydraulics | `SURCHARGE_METHOD`, `DPS_*`, `NODE_CONTINUITY`, `ANDERSON_ACCEL`, `FORCE_MAIN_EQUATION`, `NORMAL_FLOW_LIMITED`, `INERTIAL_DAMPING`, `LENGTHENING_STEP`, `VARIABLE_STEP`, `MAX_TRIALS`, `HEAD_TOLERANCE`, `LAT/SYS_FLOW_TOL` (%↔fraction), `MIN_SURFAREA`, `MIN_SLOPE` | Tab-3 combos/spins | L1940 | L3134 |
| Performance | `THREADS` | `m_threadsSpin` | L1985 | L3171 |
| Spatial | layer CRS + extent | `m_crsLabel`/`m_extentLabel` | `refreshSpatialSummary` L858 | (CRS write via layer) |
| 2D (`OPENSWMM_HAS_2D`) | CVODE/mesh/coupling/solver keys | Tab-6 controls | `read2DFromEngine` L2006 | `write2DToEngine` L3185 |
| Files/Plugins | `[PLUGINS]`, `[FILES]`, writer combos, `RPT_*`, .rpt/.out paths | Tab-7 controls | L2010 | L3194 |

### 1.3 Engine-version gating

`applyEngineConstraints()` (L127) disables controls a legacy SWMM 5.x engine
cannot honor (DYNAMIC_SLOT surcharge, SEMI_IMPLICIT node continuity, Anderson
acceleration, plugin writers, `[PLUGINS]`). For a 6.x engine it early-returns,
leaving everything enabled.

### 1.4 Findings

- **No hydration gaps found** for the in-scope OPTIONS keys: every widget is
  both read on open and written on Apply/OK, and change-detection is correct.
- The engine-ABI itself only round-trips a subset of keys through the standard
  `swmm_options_get/set` surface (`FLOW_UNITS`, `FLOW_ROUTING`, `LINK_OFFSETS`,
  `ROUTING_STEP`, `REPORT_STEP`, `START/END_DATE`, CRS). Other keys fall through
  to `SWMM_ERR_BADPARAM` and the GUI silently uses constructor/preferences
  defaults. This is a tracked engine-API follow-up (see
  `test_options_hydration_contract.cpp`, "CX-extended-keys"), **not** a dialog
  bug.
- **`applyEngineConstraints()` is one-directional** — it only *disables*. It
  never re-enables a control. This is harmless today (the dialog is rebuilt per
  open) but becomes a live bug the moment the dialog is re-pointed at a different
  engine version (see Part 2, §2.3).
- **Mesh tab "Set Active" and "Remove" were non-functional stubs** — now fixed
  (see Part 3).

---

## Part 2 — Plan: Bind Active Model to the Dialog

### 2.1 Motivation

Today the dialog captures `m_engine`, `m_layer`, `m_engineVersion`, and
`m_projectWindow` **only in the constructor**, and `SWMMVis::onSimulationOptions`
(L4847) constructs a fresh `SimulationOptionsDialog` on every invocation. There
is no way to re-point an existing dialog at a newly-activated project/model. The
goal is a public `setActiveModel(...)` entry point that re-binds the dialog and
re-hydrates every tab in place.

### 2.2 Proposed API

Add to `SimulationOptionsDialog` (public section):

```cpp
/*! \brief Re-bind the dialog to a different active model and re-hydrate all
 *         tabs in place. Safe to call on an already-constructed dialog.
 *  \param engine        New engine handle (required, non-null).
 *  \param layer         New model layer (optional; drives Spatial + Mesh tabs).
 *  \param engineVersion Engine version string controlling tab/control gating.
 *  \param projectWindow Owning MDI window for .oswp-persisted notes (optional).
 */
void setActiveModel(SWMM_Engine engine,
                    SWMMModelLayer *layer = nullptr,
                    const QString &engineVersion = QStringLiteral("6.0.0"),
                    SWMMVisProjectWindow *projectWindow = nullptr);
```

The constructor should be refactored to delegate to this method so there is a
single binding code path:

```cpp
SimulationOptionsDialog::SimulationOptionsDialog(SWMM_Engine engine, ...)
    : QDialog(parent)
{
    setWindowTitle(tr("Simulation Options"));
    resize(620, 600);
    buildUi();                                  // build widgets ONCE
    setActiveModel(engine, layer, engineVersion, projectWindow);
}
```

### 2.3 Implementation steps

1. **Extract a rebind core.** `setActiveModel` assigns the four members, then
   re-runs the hydration sequence that the constructor runs today, minus
   `buildUi()` (widgets must not be rebuilt):

   ```cpp
   void SimulationOptionsDialog::setActiveModel(SWMM_Engine engine,
                                                SWMMModelLayer *layer,
                                                const QString &engineVersion,
                                                SWMMVisProjectWindow *projectWindow)
   {
       m_engine        = engine;
       m_layer         = layer;
       m_engineVersion = engineVersion;
       m_projectWindow = projectWindow;

       m_wroteChanges  = false;     // reset per-binding dirty flag

       readFromEngine();            // re-hydrates every tab (L1783)
       refreshSpatialSummary();     // CRS + extent come from m_layer (L858)
       refreshMeshList();           // mesh dir/list keyed off m_layer (L1002)
       applyEngineConstraints();    // see step 3 — must reset, not just disable
   }
   ```

   `readFromEngine()` already re-reads Tabs 0–7 including `read2DFromEngine`,
   `readPluginsFromEngine`, `readFilesSectionFromEngine`,
   `readReportContentsFromEngine`, and the per-.inp 2D-module QSettings flag
   (verified L2004–L2014), so no per-tab read helper needs to be called
   explicitly beyond Spatial and Mesh, which are seeded in their build methods
   rather than in `readFromEngine`.

2. **Add the two missing re-reads.** Confirm `refreshSpatialSummary()` and
   `refreshMeshList()` are idempotent (they are — both just repopulate labels /
   the list widget from `m_layer`). The 2D-module QSettings key is derived from
   `m_layer->modelFilePath()` inside `readFromEngine`, so it follows the new
   layer automatically.

3. **Make `applyEngineConstraints()` bidirectional.** Today it early-returns for
   non-legacy engines and only *disables* for legacy. Re-binding from a 5.x model
   to a 6.x model would leave controls stuck disabled. Refactor it to set the
   enabled state explicitly from a single `legacy` boolean for every control it
   touches (surcharge `DYNAMIC_SLOT` item, node-continuity `SEMI_IMPLICIT` item,
   `m_andersonAccelBox`, `m_writersGroup`, plugins view/buttons), e.g.:

   ```cpp
   const bool legacy = m_engineVersion.startsWith(QLatin1String("5."));
   m_andersonAccelBox->setEnabled(!legacy);
   m_andersonAccelBox->setToolTip(legacy ? tip : QString());
   // …same enable/clear-tooltip treatment for each gated control / combo item…
   ```

   This is the only behavioral change to existing logic; everything else is
   additive.

4. **Guard for null engine.** `setActiveModel` should early-return (optionally
   `Q_ASSERT`) on a null engine to preserve the constructor's existing
   precondition.

### 2.4 Call-site integration

Update `SWMMVis::onSimulationOptions` (swmmvis.cpp:4847) to reuse a single
persistent dialog instead of constructing one per open:

```cpp
void SWMMVis::onSimulationOptions()
{
    auto *pw = activeProjectWindow();
    if (!pw || !pw->modelLayer() || !pw->modelLayer()->engine()) { /* warn */ return; }

    if (!mSimOptionsDialog)
        mSimOptionsDialog = new SimulationOptionsDialog(
            pw->modelLayer()->engine(), pw->modelLayer(),
            pw->engineVersion(), pw, this);
    else
        mSimOptionsDialog->setActiveModel(
            pw->modelLayer()->engine(), pw->modelLayer(),
            pw->engineVersion(), pw);

    if (mSimOptionsDialog->exec() == QDialog::Accepted &&
        mSimOptionsDialog->wroteAnyChanges())
    {
        pw->setHasChanges(true);
        onActiveSubWindowChanged(pw);
    }
}
```

Add `SimulationOptionsDialog *mSimOptionsDialog = nullptr;` to `SWMMVis`.
(Accessors confirmed: `SWMMVisProjectWindow::engineVersion()` h:219,
`notesHtml()` h:225, `modelLayer()` h:65; `SWMMModelLayer::engine()` h:204,
`modelFilePath()` h:200.)

> **Alignment-with-legacy note (per CLAUDE.md §4.01):** reusing one dialog is an
> optional optimization. The minimal change that satisfies "activate assigning
> the model" is steps 1–3 plus exposing `setActiveModel`; the call-site reuse in
> §2.4 can be deferred if you prefer to keep the per-open construction for now.
> If kept per-open, **step 3 is still required** for correctness of any future
> reuse and is cheap to land now.

### 2.5 Verification (success criteria)

Follow the `test_options_hydration_contract.cpp` style (engine-ABI level, no MDI):

1. **Rebind flips a model attribute.** Build engine A with `FLOW_ROUTING=DYNWAVE`
   and engine B with `KINWAVE`. Construct the dialog on A, assert
   `m_routingCombo` shows Dynamic Wave; call `setActiveModel(B,…)`, assert it
   flipped to Kinematic Wave. Repeat for `INFILTRATION` and an `IGNORE_*` box.
2. **Rebind resets dirty flag.** After an Apply on A (`wroteAnyChanges()==true`),
   `setActiveModel(B,…)` must reset `wroteAnyChanges()` to false.
3. **Engine-version re-enable.** Construct on a `"5.2"` engine, assert
   `m_andersonAccelBox` is disabled; `setActiveModel(engine, …, "6.0.0", …)`,
   assert it is re-enabled (guards step 3).
4. **No write leakage across rebind.** Edit a value on A *without* Apply, rebind
   to B, assert B's engine value is unchanged (rebind discards pending edits, as
   constructing a fresh dialog would).
5. Manual smoke: open two projects in separate MDI subwindows, open Options
   against each, confirm every tab reflects the active project.

### 2.6 Risk / surface

- **Files touched:** `simulationoptionsdialog.h` (1 method decl),
  `simulationoptionsdialog.cpp` (constructor refactor + `setActiveModel` +
  `applyEngineConstraints` reset logic), `swmmvis.h`/`swmmvis.cpp` (optional
  call-site reuse), one new test in `tests/gui/`.
- **Behavioral change:** only `applyEngineConstraints()` gains an
  enable-branch; all other edits are additive. Diff stays surgical per
  CLAUDE.md §3.
- **Lifetime:** if §2.4 reuse is adopted, the persistent dialog is parented to
  `SWMMVis`, so Qt owns it; no manual delete. Re-binding after the underlying
  project closes must be prevented by the existing `activeProjectWindow()` guard
  already present at the call site.

---

## Part 3 — Mesh Tab Assign / Remove (implemented 2026-06-02)

### 3.1 The gap

The Mesh tab's **Set Active** and **Remove** buttons were explicit stubs. Set
Active (`buildMeshTab`, old L984) only flipped the 2D-module checkbox and showed
an info box — *"[2D_MESH_FILE] re-targeting lands alongside the Generate Mesh
dialog (Slice AU.4)."* Remove showed a similar info box and deleted nothing. Only
**Refresh**, the `.2dm` listing, and the read-only active-mesh label (which
parses `[2D_MESH_FILE] FILE <path>` out of the `.inp`) were live. So the tab
could *display* the active mesh but never *change* it.

The machinery to patch `[2D_MESH_FILE]` lived in `InpMeshWriter`, but only in
`writeExternal`/`writeInline`, both of which write a whole mesh and require a
full `MeshResult` + `CouplingMap`. The "point at an existing `.2dm`" case has no
mesh geometry to write — it only needs to strip the old block and append a new
`FILE` line. The strip helper (`stripExistingMeshSections`) was a file-static
function with no public entry point.

### 3.2 The fix

1. **New public helper** `InpMeshWriter::writeMeshFileRef(inpPath, meshFilePath,
   errorOut)` (`mesh/inpmeshwriter.h` / `.cpp`). Validates the `.2dm` exists,
   strips any inline `[2D_*]` sections and any prior `[2D_MESH_FILE]` block via
   the existing `stripExistingMeshSections(…, /*alsoMeshFileRef=*/true)`, then
   appends a fresh `[2D_MESH_FILE] FILE <path>`. Path is stored relative to the
   `.inp` directory when the mesh is a sibling, absolute otherwise — identical
   to `writeExternal`, so generation and retargeting produce the same reference
   form. Atomic via `QSaveFile`. No mesh geometry is written.

2. **`onMeshSetActive()` slot** (replaces the lambda stub). Guards for no
   selection / unsaved project, resolves the selected `.2dm` against the `.inp`
   directory, calls `writeMeshFileRef`, surfaces any error in a message box,
   flips the 2D-module checkbox on success, and re-runs `refreshMeshList()` so
   the active-mesh label updates.

3. **`onMeshRemove()` slot** (replaces the lambda stub). Guards for no
   selection, confirms with the user (with an extra warning when deleting the
   *currently-active* mesh, which would leave the `.inp` reference dangling),
   deletes via `QFile::remove`, and re-runs `refreshMeshList()`.

Both slots are declared next to the existing spatial slots in the header.

### 3.3 Design notes

- **Retarget vs. dangling reference:** deleting the active mesh is allowed but
  warned, matching SWMM's tolerance for a `[2D_MESH_FILE]` that the engine
  resolves at run time. The user can re-point with Set Active afterward.
- **Why a reference-only writer (not reuse `writeExternal`):** Set Active must
  *not* rewrite mesh geometry — the `.2dm` on disk is authoritative. Reusing
  `writeExternal` would have required round-tripping the file back into a
  `MeshResult` just to write it out again, risking lossy re-serialization. The
  thin retarget helper keeps the on-disk `.2dm` byte-for-byte intact.

### 3.4 Verification status

- **Linkage confirmed:** `InpMeshWriter` and `SimulationOptionsDialog` compile
  into the same CMake target, and `meshgenerationdialog.cpp` already calls
  `mesh::InpMeshWriter::…`, so the new symbol resolves.
- **Full build not run here:** Qt6/GDAL/vcpkg are unavailable in this
  environment, so the GUI target was not compiled. Build + the manual smoke
  steps below should be run locally before merge.
- **Suggested unit test** (`tests/`, no Qt widgets needed): write a temp `.inp`
  containing inline `[2D_VERTICES]`/`[2D_TRIANGLES]` and a stale
  `[2D_MESH_FILE]`; call `writeMeshFileRef(inp, "<dir>/m.2dm")`; assert the
  result contains exactly one `[2D_MESH_FILE]` block with `FILE  m.2dm`
  (relative form) and that the inline sections were stripped. Add a second case
  asserting an absolute path is stored when the mesh sits outside the `.inp`
  directory, and a failure case for a non-existent `.2dm`. Per CLAUDE.md §4
  (transparent file IO), write temp fixtures to a reviewable test-output dir,
  not `/tmp`.
- **Manual smoke:** open a project with ≥2 sibling `.2dm` files → Mesh tab →
  select one → Set Active → confirm the active-mesh label and the `.inp`'s
  `[2D_MESH_FILE]` both update and the 2D-module checkbox flips on; select
  another → Remove → confirm the file is gone from disk and the list.
