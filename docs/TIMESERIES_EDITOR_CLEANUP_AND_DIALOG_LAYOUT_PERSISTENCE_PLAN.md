# Time Series Editor Cleanup + Project-Wide Dialog Layout Persistence — Plan of Attack

**Status:** Draft — awaiting review.
**Author:** drafted 2026-05-30.
**Scope:** GUI only (`openswmm.gui`). No engine changes.

This plan covers four user-requested changes, batched because they touch overlapping code paths:

1. Collapse the **two timeseries CRUD UI surfaces** down to the one with the
   list view + add/delete buttons.
2. **Zoom-to-extent on load** for the timeseries editor's plot.
3. **Disable editing** when the active timeseries is file-backed
   (`SourceMode::ExternalFile`) — verify the existing implementation and close
   any gaps.
4. **Fix the timeseries editor's starting layout** (plot pane currently opens
   nearly collapsed) and persist size + splitter sizes via `QSettings`, then
   roll the persistence pattern out to every dialog with geometry/splitter
   state worth remembering.

---

## 1. Findings — What "two CRUD editors" actually refers to

There is **one** `TimeseriesEditorDialog` class
(`include/ui/dialogs/timeserieseditordialog.h`) but it ships **three
constructors**, two of which produce visibly different UIs. The agent that
swept the codebase confirmed there is no separate second-editor file (the
`timeseriesplotdialog.*` references in `docs/GUI_IMPLEMENTATION_PLAN.md` are a
plotting dialog, not a CRUD editor, and the source file has already been
removed — only the `.o` lingers in `build/`).

The two UI surfaces are:

| Variant | Constructor | UI shape | Used by |
|---|---|---|---|
| **3-pane (KEEP)** | `TimeseriesEditorDialog(TimeseriesRegistry*, QUndoStack*, TimeseriesProvider*, parent)` — line 80 in `.cpp` | List (left) + Grid (mid) + Chart (right). List has New/Delete/Rename buttons. Calls `buildListPane_()`. | `createNew()` factory; `pickTimeseries()` static; Object-Browser double-click (via single-provider ctor that walks parent chain to find the registry); `swmmvis.cpp:629`. |
| **2-pane (REMOVE)** | `TimeseriesEditorDialog(QVector<TimeseriesProvider*>, QUndoStack*, parent)` — line 143 in `.cpp` | Grid + Chart only. **No list, no Add/Delete buttons.** Header comment labels it "legacy multi-column CSV import scenarios". | Direct callers that pass a `QVector<TimeseriesProvider*>` AND any single-provider call whose provider has no registry in its parent chain (the fallback path in the convenience ctor at lines 116-141). |

> **Assumption to confirm:** when you said "the one with the list view kept and
> the other removed" you mean removing the 2-pane (multi-provider) variant.
> If you instead meant a different second editor surface I missed, point me at
> it before implementation.

### Why this came up

The single-provider convenience ctor (line 116) routes through the
multi-provider ctor first, *then* tries to discover a registry via the
provider's parent chain to retro-fit the list pane. When discovery fails — or
when a caller hands in a `QVector` directly — the user sees the 2-pane
variant. From the user's point of view this looks like "a second CRUD
editor".

## 2. Plan — Step by step

Each step has its own verification check, per the project's goal-driven-
execution policy.

### Step A — Eliminate the 2-pane variant (registry becomes required)

1. **Header:** delete the `QVector<TimeseriesProvider*>` constructor declaration
   and the single-provider convenience constructor (it only existed to forward
   to the vector ctor). Keep the registry-based constructor + `createNew` +
   `pickTimeseries` as the only public entry points.
   - File: `include/ui/dialogs/timeserieseditordialog.h` (lines 98-104 and
     90-96 respectively).
2. **Implementation:** delete the two ctor bodies in `.cpp` (lines 116-158).
   Remove any helper code that exists solely to support multi-provider /
   no-registry mode (audit `buildUi_`, `m_providers` is a vector — keep that
   shape because the grid model still supports multi-column; just always
   populate it from the registry path).
3. **Call-site migration** — every caller must hand in the registry. Known
   call sites (from prior audit):
   - `src/swmmvis.cpp:629` (`pickTimeseries`) — already registry-based; no
     change.
   - `src/ui/panels/objectbrowserpanel.cpp` — uses single-provider ctor;
     change to the registry ctor with the provider's registry passed
     explicitly (the registry is accessible via `swmmvis.cpp` already; pipe it
     down via signal-emit metadata, or fetch it from a known global accessor —
     pick whichever matches the existing wiring, surface the choice in review).
   - `src/ui/panels/attributepanel.cpp`,
     `src/ui/panels/attributetablepanel.cpp` — inline picker uses
     `pickTimeseries` (modal); no change needed.
   - `src/ui/dialogs/nodecompoundeditdialog.cpp` — same (`pickTimeseries`).
   - `src/ui/properties/dataobjectpickereditor.cpp` — same.
   - `include/ui/editors/comprehensiveeditorregistry.h` — uses `createNew`;
     no change.
4. **Test fallout:** the 4 dispatch tests in
   `tests/gui/test_objectbrowser_add_new_dispatch.cpp` already use
   `createNew`; they should still pass. The
   `tests/gui/test_timeseries_editor_dialog.cpp` test may exercise the
   multi-provider ctor — audit and migrate to the registry ctor.

**Verify:** project builds; `test_objectbrowser_add_new_dispatch` and
`test_timeseries_editor_dialog` pass; manual smoke-test: open editor from
Object Browser, from `pickTimeseries` flow, from `Add New…` flow — list pane
present in all three.

### Step B — Zoom-to-extent on open

The dialog already exposes a `zoomToExtent()` action (`m_actZoomExt`) wired
to `m_chartView->zoomToExtent()` (line 545) but never *fires* it during open.

1. After binding the initial provider in `buildUi_` (or end of the registry
   ctor, after `rebindActiveProvider_`), call `m_chartView->zoomToExtent()`.
2. Also call it inside `rebindActiveProvider_` whenever a new provider is
   picked from the list — so switching series re-fits the chart.
3. Guard with: if the provider has zero points, skip (otherwise zoom-to-extent
   on an empty series produces a degenerate axis range).

**Verify:** open the editor on a populated series → chart shows full extent;
switch to another series with a different time range → chart re-fits; new
empty series → chart shows a sensible empty-state axis (existing fallback).

### Step C — File-backed timeseries read-only

The infrastructure is already in place:

- `TimeseriesTableModel::setData` returns false when any provider is external
  (`src/ui/panels/timeseriestablemodel.cpp:162`).
- `refreshSourceModeCardForProvider_()`
  (`src/ui/dialogs/timeserieseditordialog.cpp:1118-1178`) disables Edit /
  Rotate / Scale / AddRow / DeleteRow / Paste actions when external.
- Add-row / Delete-row / Paste also bail with a status message at the call
  sites (lines 761, 841, 897).

**What to verify (and fix if missing):**

1. The **chart's** drag-edit handlers — confirm they short-circuit in
   `TimeseriesEditChartView` when the provider is external (the toolbar
   disable is necessary but not sufficient if a user still has Edit mode
   armed from before the switch). If missing, add a guard at the start of
   the mouse handlers.
2. The **list pane's** Rename button — renaming a file-backed series is fine
   (the name is project-side metadata), but **Delete** removes the series
   from the registry, which is also fine. No change.
3. **Source-mode card:** make sure switching to External via the radio
   doesn't allow editing during the brief window before the file path is
   picked. The existing code does call `refreshSourceModeCardForProvider_()`
   after `setSourceMode`; spot-check.

**Verify:** create an inline series → edit OK; switch source mode to
External and browse to a CSV → grid + chart go read-only; toolbar edit
buttons gray out; chart drag does nothing; Detach-to-Inline restores edit
capability.

### Step D — Initial sizing of the timeseries editor

The current `buildUi_` does:

```cpp
m_splitter->setStretchFactor(0, 1);  // grid
m_splitter->setStretchFactor(1, 2);  // chart (wider by default)
```

**Two problems:**

1. The list pane is added *after* `buildUi_` returns, via `buildListPane_()`.
   That pushes the grid/chart to indices 1/2 in the splitter, so the
   stretch-factor calls above point at the wrong widgets. The plot ends up
   with the grid's stretch factor (1) and the list ends up with the chart's
   (2) — explaining the "almost collapsed" plot.
2. Even without that bug, `setStretchFactor` alone is fragile because Qt
   uses sizeHint as the seed; the chart's sizeHint is small.

**Fix:**

1. Move the stretch-factor / explicit-size code into `buildListPane_()` (or
   into a `finalizeSplitterLayout_()` helper called at the end of the
   registry ctor) so it runs *after* all panes are inserted.
2. Apply explicit initial sizes via `m_splitter->setSizes({200, 300, 600})`
   (list, grid, chart — chart largest), and set stretch factors `0, 1, 3`
   so resizing the dialog preferentially grows the chart.
3. Set a sensible default dialog `resize()` — bump from 1100×560 to 1400×720
   (numbers up for discussion; matches plot-heavy use).
4. Set `setMinimumWidth(180)` on the list pane and a sensible minimum on the
   chart so neither can collapse to zero by accident.

These hard-coded sizes are the *defaults* only; QSettings restoration in
Step E overrides them on subsequent opens.

**Verify:** delete the QSettings key, launch the editor — plot is the widest
pane, list is narrow, grid is in between, and the proportions hold when the
dialog is resized.

### Step E — QSettings dialog layout persistence (project-wide)

#### E.1 — Reusable helper (not a base class)

Existing dialogs that already persist state (`TransectEditorDialog`,
`RulesEditorDialog`, `HydrographGroupEditor`) each do it inline in their own
ctor / dtor / closeEvent. There is **no shared helper**. I propose a small
free-function helper rather than a new `PersistentDialog` base class,
because:

- A base class requires changing the inheritance hierarchy of every dialog,
  which collides with the surgical-changes principle.
- A helper is opt-in per dialog and one-line to use.

Sketch — new files `include/ui/dialogs/dialoglayoutpersistence.h` +
`src/ui/dialogs/dialoglayoutpersistence.cpp`:

```cpp
namespace openswmmvis::ui {

/// Saves dialog geometry + every named QSplitter under \p root.
/// Group key is `Dialogs/<root->objectName()>` — caller MUST setObjectName.
void saveDialogLayout(QWidget *root);

/// Inverse of saveDialogLayout. Returns true if any state was restored.
bool restoreDialogLayout(QWidget *root);

} // namespace openswmmvis::ui
```

Semantics:

- `saveDialogLayout`: stores `geometry()` and, for every `QSplitter *` child
  reachable via `findChildren<QSplitter*>()` that has a non-empty
  `objectName()`, stores `splitter->saveState()` under
  `Dialogs/<root>/splitter/<splitterObjectName>`. Splitters without an
  objectName are skipped silently (forces callers to opt in deliberately).
- `restoreDialogLayout`: inverse. Returns false if the dialog has no
  previously-saved geometry — caller can apply defaults in that case.

Usage pattern per dialog:

```cpp
// In ctor, AFTER buildUi_:
setObjectName(QStringLiteral("TimeseriesEditorDialog"));
m_splitter->setObjectName(QStringLiteral("main"));
if (!openswmmvis::ui::restoreDialogLayout(this)) {
    // apply hard-coded defaults from Step D
}

// In closeEvent (override if not already):
void TimeseriesEditorDialog::closeEvent(QCloseEvent *e) {
    openswmmvis::ui::saveDialogLayout(this);
    QDialog::closeEvent(e);
}
```

QSettings keys live under the existing
organization=`hydrocouple` / application=`OpenSWMM Stormwater Management
Model` namespace, beneath a new top-level `Dialogs/` group so they don't
collide with anything else.

#### E.2 — Apply to TimeseriesEditorDialog

Wire the helper as shown above. Verify with two test cases:

- Open editor, resize dialog + splitter, close, reopen → same geometry.
- Delete the QSettings key (or call `QSettings::clear()` in a test
  harness), reopen → defaults from Step D.

#### E.3 — Apply to remaining dialogs

Phased rollout to keep PRs reviewable. The audit found 38 QDialog subclasses;
17 of them have a `QSplitter`. Apply in this order:

**Phase 1 — high-value editors (immediate after Time Series):**

- `CurveEditorDialog` (`include/ui/dialogs/curveeditordialog.h`)
- `PatternEditorDialog`
- `RulesEditorDialog` (replace existing manual code with helper)
- `HydrographGroupEditor` (replace existing manual code with helper —
  preserves the existing default `setSizes({220, 540, 520})` fallback)
- `TransectEditorDialog` (replace existing manual code with helper)
- `NodeCompoundEditDialog`
- `LinkCompoundEditDialog`

**Phase 2 — analysis / plot dialogs:**

- `ComparisonPlotDialog`
- `ProfilePlotDialog`
- `StatisticsDashboardDialog`
- `ScatterPlotDialog`
- `MeshProfilePlotDialog`
- `TabularResultsDialog`

**Phase 3 — configuration / spatial dialogs:**

- `SimulationOptionsDialog`
- `ProfileOptionsDialog`
- `PreferencesDialog`
- `LayerStyleDialog`
- `ChartPropertiesDialog`
- `AddBasemapDialog` (two splitters)
- `WMSConnectionDialog`, `WMTSConnectionDialog`
- `CRSSelectionDialog`
- `CustomReportDialog`

**Skip:** trivial dialogs without persistent state worth remembering —
`AboutDialog`, `LicenseAgreementDialog`, `CRSChangeDialog`,
`PluginsDialog` — unless you call them out.

For each phase, identical mechanical change per dialog:

1. `setObjectName(...)` in ctor.
2. `setObjectName(...)` on each meaningful `QSplitter`.
3. Add `restoreDialogLayout(this)` after layout build; apply defaults if
   it returns false.
4. Add or extend `closeEvent` to call `saveDialogLayout(this)`.

**Verify (per phase):** existing dialog tests still pass; one
new test `tests/gui/test_dialog_layout_persistence.cpp` exercises the helper
against a fixture dialog (round-trips geometry + splitter state through an
in-memory `QSettings`).

---

## 3. Risks / open questions

- **Engine ↔ GUI registry plumbing for the registry ctor.** The Object
  Browser path currently lets us fall back to the convenience ctor when no
  registry pointer is at hand. Removing that fallback requires every call
  site to know its registry. Should be straightforward — the registry is a
  project-wide singleton-ish accessible from `swmmvis` — but it's the
  highest-risk change.
- **Dialog `objectName()` collisions.** If any existing dialog already sets
  `objectName` for QSS theming, the persistence helper will reuse it.
  Unlikely to collide but worth a grep before Step E.
- **Multi-monitor / display-disconnect edge cases.** `QSettings`-restored
  geometry can land off-screen if the saved monitor is gone. The helper
  should clamp restored geometry to the current available geometry
  (`QGuiApplication::screenAt(rect.center())`) — add as a one-liner.
- **Tests that construct dialogs without showing them.** `saveDialogLayout`
  shouldn't run on close if the dialog was never shown (would persist the
  default size as if user-chosen). Gate save on `isVisible()` having been
  true at some point — track with a `m_shownAtLeastOnce` flag set in
  `showEvent`.

## 4. Out of scope

- Main-window persistence (already works; not touching it).
- Dock-widget persistence (already works via Qt's `saveState/restoreState`).
- Any feature work on the editors beyond the four requested items.
- Adding new fields / behaviour to `TimeseriesProvider` or
  `TimeseriesRegistry`.

## 5. Estimated effort

| Step | Files touched | Effort |
|---|---|---|
| A — Remove 2-pane ctor + migrate call sites | ~6 files | 0.5 day |
| B — Zoom on load | 1 file | 0.1 day |
| C — File-backed read-only verify + small fixes | 2 files | 0.2 day |
| D — Initial sizing fix | 1 file | 0.1 day |
| E.1 — Persistence helper + test | 3 new files | 0.5 day |
| E.2 — Wire into Time Series editor | 1 file | 0.1 day |
| E.3 Phase 1 — 7 editor dialogs | 14 files | 0.7 day |
| E.3 Phase 2 — 6 plot dialogs | 12 files | 0.6 day |
| E.3 Phase 3 — ~10 config dialogs | ~20 files | 0.8 day |
| **Total** | | **~3.5 dev-days** |

---

## 6. Step F — Lazy load / dispose for file-backed timeseries (added 2026-05-30)

### Current state

- The **Reload** button already exists
  (`m_extReloadBtn` → `onReloadExternalFile_`, line 1270 in
  `src/ui/dialogs/timeserieseditordialog.cpp`). It calls
  `loadExternalFileIntoProvider_(p, p->filePath(), col)` which re-parses the
  file and refills `TimeseriesProvider::m_points`. **Keep it as-is** under the
  new policy.
- Today the file-backed point cache (`m_points`) lives on the provider for
  the life of the project. Because `TimeseriesProvider` is registry-owned
  and the registry outlives any single dialog session, the cache stays
  resident even when the user has switched to a different series. With
  large `.dat` / `.csv` files this is exactly the wasted memory you're
  flagging.

### Goal

For `SourceMode::ExternalFile` providers only:

- Don't read the file until the user actually views the series in the
  editor (lazy load).
- Free `m_points` as soon as focus moves to a different series (dispose).
- Inline / Geopackage providers are unaffected — their points are the
  authoritative storage and must stay resident.

### Design

**Provider side** (`include/timeseries/timeseriesprovider.h` +
`.cpp`):

1. Add `bool isPointCacheLoaded() const noexcept` — true when the in-memory
   cache is populated, false when it's been disposed (Inline mode: always
   true; External mode: depends on lazy state).
2. Add `void disposePointCache()` — clears `m_points` and emits
   `pointsChanged()`. No-op for Inline / Geopackage modes (precondition
   asserted; disposing an authoritative cache would lose data).
3. Add a `pointCacheDisposed()` signal so views can re-bind to an
   empty-state placeholder without ambiguity (versus a series that's
   legitimately empty).

The on-disk file path + column selector + cached mtime already live on the
provider (`m_filePath`, `m_columnSelector`, `m_fileMTime`). That's the
durable state. The point vector becomes a derived, disposable cache.

**Dialog side** (`src/ui/dialogs/timeserieseditordialog.cpp`):

1. In `rebindActiveProvider_(newP)`:
   - **Before unbinding** the previous provider: if it was External AND
     `isPointCacheLoaded()` is true, call `prevP->disposePointCache()`.
   - **After binding** `newP`: if `newP` is External and the cache is
     **not** loaded, lazily call `loadExternalFileIntoProvider_(newP,
     newP->filePath(), newP->columnSelector())`. The Reload button's path
     is unchanged — it always re-reads regardless of cache state.
2. When the dialog is closed: if the currently-bound provider is External,
   dispose its cache on the way out so the registry doesn't carry the
   memory around after the editor goes away.
3. The initial open: only load the cache for the series the dialog opens
   on. Don't pre-warm the rest of the registry.

**Anywhere else that reads `provider->points()`** must tolerate a disposed
cache for External providers. Two strategies:

- Easiest: read sites for External providers must call
  `ensurePointCache()` (a new convenience that calls `loadExternalFileIntoProvider_`-equivalent if not loaded). The dialog editor uses this on rebind; other UI surfaces (Object Browser tooltip, plot dialog) use the same path.
- A pragmatic stopgap if other UI surfaces don't read the points anyway:
  audit `points()` callers (small number — `TimeseriesTableModel`,
  `TimeseriesEditChartView`, the engine flush path, and any plot dialog
  that consumes a TS). Document each caller's policy; if a caller wouldn't
  reasonably hold a TS open without the editor being open, no change
  needed.

> **Engine flush path caveat.** When the project saves to `.inp`, the
> writer must NOT trigger a full file-read of every External series
> (defeats the purpose). The writer for ExternalFile-mode series writes
> the `FILE "path"` reference only — no point data. Verify
> `swmm_model_write` / the `[TIMESERIES]` writer respects this. Likely
> already correct since External-mode points are read-only and not
> round-tripped, but call out in review.

### MVC consistency

`pointCacheDisposed()` is a state-change signal. The grid model
(`TimeseriesTableModel`) and the chart view
(`TimeseriesEditChartView`) already subscribe to `pointsChanged()` /
`pointsInserted()` / `pointsRemoved()`. Either route `disposePointCache`
through `pointsChanged()` (simplest — they'll just see an empty series),
or add an explicit slot. Lean toward the former unless tests need to
distinguish "disposed" from "legitimately empty".

### Failure modes

- **File disappears between switch-away and switch-back.** The lazy reload
  attempt will fail; surface the existing "⚠ File not found" path in
  `refreshSourceModeCardForProvider_`. The grid + chart go empty;
  Reload stays available so the user can re-pick.
- **File mtime changed.** The existing staleness banner already covers this
  on bind. With lazy loading the staleness check naturally runs on every
  switch-to, which is a small UX upgrade.
- **Multi-dialog edge case.** Two TimeseriesEditor instances bound to
  different series, both External — disposal logic must be keyed on
  per-provider state, not per-dialog. Since the cache lives on the
  provider, this is correct by construction. But if two dialogs ever bind
  to the **same** External provider, the first one to switch away would
  dispose the cache out from under the other. Mitigation: refcount the
  cache (each dialog increments on bind, decrements on unbind; dispose
  only at refcount zero). Probably overkill for now — call out as a known
  limitation unless we expect this case.

### Verify

- Open the editor on an External series → file reads once, points appear.
- Switch to a different series in the list → previous provider's
  `m_points.size()` is now 0 (assertable via test hook); new provider's
  cache is populated.
- Switch back to the original → file reads again, points re-appear.
- Click Reload while a series is bound → file re-read; behaviour
  identical to today.
- Close the dialog → all External providers in the registry report
  `isPointCacheLoaded() == false`.
- Memory check: open a project with one large External TS (~10M points,
  ~200 MB resident), close the editor → RSS drops by the cache size.

### Estimated effort

Add ~0.4 day on top of the existing budget (provider API change + dialog
wiring + 2 tests).

---

## 7. Step G — Mesh toolbar select tools share one radio group with the general-purpose Select (added 2026-05-30)

### Current state

- The general-purpose select tool is `OpenSWMMVisMapToolSelect`
  (`include/map/tools/maptoolselect.h`), bound to `mSelectTool` in
  `SWMMVisProjectWindow`. The main toolbar's `ui->actionSelect`
  activates it (`src/swmmvis.cpp:904-906`).
- Mesh-side selection tools:
  - `MapToolMeshSelectVertex` → `MeshEditingToolbar::m_actEditVertex`,
    activated via `editVertexToggled` signal
    (`src/swmmvis.cpp:604-610`).
  - `MapToolMeshSelectEdge` → `MeshEditingToolbar::m_actEditEdge`,
    activated via `editEdgeToggled` signal
    (`src/swmmvis.cpp:611-617`).
  - `MapToolPick2DCells` → `actPick2DCells` (objectName
    `actionPick2DCells`) on the mesh toolbar
    (`src/swmmvis.cpp:680-696`).
- Inside `MeshEditingToolbar`, Vertex + Edge are already mutually
  exclusive via the `m_editGroup` `QActionGroup`. `actPick2DCells` was
  deliberately kept **out** of `m_editGroup` (`src/swmmvis.cpp:674-677`
  comment).
- A canvas-level sync mechanism already enforces "one active map tool at
  a time": `SWMMVisProjectWindow::toolActionKeys()` maps each map tool
  to a UI-action `objectName`, and the lambda at `src/swmmvis.cpp:3997-4024`
  listens to `MapCanvas::activeToolChanged` and `setChecked(true)` on
  the matching action while `setChecked(false)`-ing all others. The
  lambda uses `QSignalBlocker` to prevent the documented reentrancy
  crash.

**The gap.** `toolActionKeys()` (`src/swmmvisprojectwindow.cpp:1101-1128`)
includes `mSelectTool`, `mPick2DCellsTool`, `mMeshProfileTool`, etc., but
**does NOT include `mMeshSelectVertexTool` or `mMeshSelectEdgeTool`** —
because those actions live on `MeshEditingToolbar`, not on SWMMVis
directly, and have no `objectName` for `findChild<QAction*>` to resolve.

Net effect: toggling the main **Select** action does not visually
uncheck Edit Vertex / Edit Edge, and toggling Edit Vertex doesn't
uncheck **Select** or Select 2D Cells. The underlying map tool *is*
correctly swapped — only the toolbar checked state goes out of sync.
That's exactly the UX bug the user is calling out.

### Design

The existing `activeToolChanged` sync is the right machinery; it just
needs two more entries in `toolActionKeys()`. To make that work,
`m_actEditVertex` and `m_actEditEdge` need stable `objectName`s.

Changes:

1. **In `MeshEditingToolbar` ctor** (`src/ui/toolbars/mesheditingtoolbar.cpp`):
   - `m_actEditVertex->setObjectName(QStringLiteral("actionMeshSelectVertex"));`
   - `m_actEditEdge->setObjectName(QStringLiteral("actionMeshSelectEdge"));`
   - No other changes — keep them in `m_editGroup` (the exclusive
     QActionGroup between vertex + edge is correct and doesn't conflict
     with the canvas-level sync; QActionGroup only enforces "≤1
     checked among this group", which is consistent with the wider
     radio policy).

2. **In `SWMMVisProjectWindow::toolActionKeys()`**
   (`src/swmmvisprojectwindow.cpp:1101-1128`): add two entries beside
   the existing lazy-tool entries:
   ```cpp
   { mMeshSelectVertexTool, QStringLiteral("actionMeshSelectVertex") },
   { mMeshSelectEdgeTool,   QStringLiteral("actionMeshSelectEdge")   },
   ```
   The "lazy tools — null entries are tolerated" comment above
   `mPick2DCellsTool` already covers the case where the tool pointer is
   null until first activation; no extra guard needed.

3. **No change** to `actPick2DCells`, `actMeshProfile`, or `ui->actionSelect`
   — they're already registered. The expanded `toolActionKeys()` lets
   the existing sync lambda handle the four-way radio relationship
   among: `actionSelect`, `actionMeshSelectVertex`,
   `actionMeshSelectEdge`, `actionPick2DCells` (plus `actionMeshProfile`
   etc., which all naturally participate as siblings — toggling Select
   unchecks them too, which is the intended behaviour).

4. **No `QActionGroup` widening.** Don't add `actPick2DCells` /
   `ui->actionSelect` to `m_editGroup`. The QActionGroup is per-toolbar
   and per-widget; cross-toolbar mirroring is the sync lambda's job, not
   QActionGroup's. Mixing the two would risk fighting over checked
   state on tool-switch.

5. **Reentrancy.** The sync lambda already wraps its `setChecked` calls
   in `QSignalBlocker`. The new entries inherit that protection. No
   additional guards required.

### Behaviour after change

- User clicks **Select** on the main toolbar: `mSelectTool` activates →
  `activeToolChanged(mSelectTool)` → sync lambda unchecks
  `actionMeshSelectVertex`, `actionMeshSelectEdge`, `actionPick2DCells`
  (and any other tool action) and checks `actionSelect`.
- User clicks **Edit Vertex**: `MapToolMeshSelectVertex` activates →
  sync lambda unchecks `actionSelect`, `actionMeshSelectEdge`,
  `actionPick2DCells` and checks `actionMeshSelectVertex`. The existing
  `m_editGroup` redundantly unchecks Edit Edge — harmless.
- User clicks **Edit Vertex** again (turn off): existing handler at
  `src/swmmvis.cpp:608-609` already calls `activateSelectTool()` on
  toggle-off, so the canvas falls back to the general-purpose Select
  and the sync lambda flips `actionSelect` on.
- Same logic for **Edit Edge** and **Select 2D Cells**.

### Out of scope under this step

- The other map tools that already participate via `toolActionKeys()`
  (`actionPan`, `actionZoomIn`, etc.) — already covered by the sync; no
  change.
- The "Trace Profile Path" tool (`actionMeshProfile`) — already
  participates.
- Any tool that lives outside the canvas active-tool model
  (e.g. `actionSelectUpstream` / `actionSelectDownstream` which are
  currently unimplemented per `src/swmmvis.cpp:972-977`).

### Verify

- Activate Select on the main toolbar → all three mesh selection
  actions (Edit Vertex / Edit Edge / Select 2D Cells) visually
  uncheck.
- Activate Edit Vertex → Select on the main toolbar visually unchecks;
  Edit Edge and Select 2D Cells uncheck.
- Activate Edit Edge → Edit Vertex, Select, Select 2D Cells all
  uncheck.
- Activate Select 2D Cells → Edit Vertex, Edit Edge, Select all
  uncheck.
- Esc / canvas-side tool reset → Select becomes active → other three
  uncheck.
- Open a project with a 2D mesh layer (this auto-activates Pick 2D
  Cells per the existing flow) — no recursion / crash (the existing
  reentrancy guard at `src/swmmvis.cpp:4002-4014` still applies).

### Estimated effort

~0.2 day (three small code changes + one new gui test that drives the
four toggle paths and asserts checked-state across the four actions).

### Files touched

- `src/ui/toolbars/mesheditingtoolbar.cpp` — 2 `setObjectName` calls
  in the ctor.
- `src/swmmvisprojectwindow.cpp` — 2 entries in `toolActionKeys()`.
- `tests/gui/test_mesh_toolbar_select_radio.cpp` — new ~80-line
  GUI test (or extension of an existing mesh-toolbar test if one is
  closer in scope).

---

## 8. Step H — All dialogs stay on top of the main window (added 2026-05-30)

### Current state

Window-flag usage across the 38 QDialog subclasses is inconsistent:

- **Already `Qt::Tool | Qt::WindowStaysOnTopHint`** (correct today):
  `TimeseriesEditorDialog`, `CurveEditorDialog`, `PatternEditorDialog`,
  `TransectEditorDialog`, `RulesEditorDialog`.
- **`Qt::Window | Qt::WindowStaysOnTopHint`** (full window with min/max +
  pinned): `ProfilePlotDialog`, `MeshProfilePlotDialog`,
  `ProfileOptionsDialog`.
- **`Qt::Tool` only** (floats above parent on macOS/Windows but is NOT
  pinned globally): `ChartPropertiesDialog`, `LegendPropertiesDialog`,
  `SublayerStyleDialog`.
- **Default flags** (the map-click-hides-it problem the user is
  flagging): `NodeCompoundEditDialog`, `LinkCompoundEditDialog`,
  `HydrographGroupEditor`, `ComparisonPlotDialog`, `ScatterPlotDialog`,
  `StatisticsDashboardDialog`, `TabularResultsDialog`,
  `SimulationOptionsDialog`, `PreferencesDialog`, `LayerStyleDialog`,
  `StyleManagerDialog`, `ColorRampEditorDialog`,
  `AnnotationStyleDialog`, `AddBasemapDialog`,
  `WMSConnectionDialog`, `WMTSConnectionDialog`, `CRSSelectionDialog`,
  `CRSChangeDialog`, `NewProjectDialog`, `MeshGenerationDialog`,
  `CustomReportDialog`, `CalibrationDataDialog`, `StatusReportDialog`,
  `PluginsDialog`, `AboutDialog`, `LicenseAgreementDialog`.

> 28 of the 38 dialogs are subject to the bug.

### Design

The user-stated goal — "stay on top of the main application so they
are not hidden when the map is clicked" — is precisely what
`Qt::WindowStaysOnTopHint` solves. Match the existing
`TimeseriesEditorDialog` convention: **`Qt::WindowStaysOnTopHint`
added to whatever flags the dialog already has** (don't change
`Qt::Tool` vs `Qt::Window` choices — those are deliberate and affect
title-bar buttons + taskbar behaviour).

Implementation: a new free helper in the same header that hosts the
persistence helpers (Step E):

```cpp
namespace openswmmvis::ui {
/// Pin the dialog above the main window so a map click doesn't hide it.
/// Idempotent. Safe on modal dialogs (no-op effect) and on dialogs
/// that already have the flag.
void applyAlwaysOnTopPolicy(QDialog *d);
} // namespace openswmmvis::ui
```

Body is one line: `d->setWindowFlag(Qt::WindowStaysOnTopHint, true);`

Each dialog ctor adds one call at the end (alongside the
`restoreDialogLayout` call from Step E):

```cpp
openswmmvis::ui::applyAlwaysOnTopPolicy(this);
```

For dialogs that already have the flag (the 5 editors + 3 plot
dialogs above), the call is a no-op and can either stay (for
consistency) or be skipped — favour staying so future readers find
the policy in every dialog.

### Why a separate helper instead of folding into `restoreDialogLayout`

Two distinct concerns: layout *persistence* (Step E) and *stacking
policy* (Step H). Bundling them would surprise readers — a function
named `restoreDialogLayout` mutating window flags would be unexpected.
Keep them adjacent in the same file but separately callable.

### Why not `Qt::Tool` only

`Qt::Tool` makes a window float above its parent toplevel, but on
some macOS configurations the canvas widget can still steal stacking
order during heavy redraws. `WindowStaysOnTopHint` is the unambiguous
fix the existing editor dialogs already adopted, so this step extends
that convention rather than reinvents it.

### Caveats / what the user should know

- **`WindowStaysOnTopHint` is global**, not "above the main window
  only". When the user Cmd-Tabs / Alt-Tabs to a browser, SWMM's
  open dialogs will float above the browser too. On macOS the flag
  is partially relaxed on app deactivation (Qt behaviour), so this
  is less obtrusive than on Linux/Windows but not absent.
- **Modal dialogs** (`QDialog::exec()` family — `NewProjectDialog`,
  `CRSChangeDialog`, `PluginsDialog`, `AboutDialog`,
  `LicenseAgreementDialog`, the file/CRS pickers, etc.) are already
  on top of their parent by definition. Adding the flag is a
  harmless no-op but technically unnecessary. Skipping them keeps
  the rollout focused; including them keeps the policy uniform. The
  checklist below picks "include all" as the default — fewer
  exception cases for future maintainers.
- **`AboutDialog` / `LicenseAgreementDialog`** are short-lived modal
  dialogs; the flag is meaningless for them but harmless. Include
  them under the uniform policy.

### Rollout

Piggyback on Step E's three-phase dialog rollout. Each dialog touched
for layout persistence gets the `applyAlwaysOnTopPolicy(this)` call
added in the same edit. No separate phase needed.

The 5 + 3 dialogs that already have the flag get the helper call
substituted in (replacing the inline `Qt::Tool | Qt::WindowStaysOnTopHint`
constructor argument — leave the `Qt::Tool` / `Qt::Window` choice
intact, just route the stay-on-top half through the helper). Net
diff: one line added per dialog in the worst case.

### Verify

- For each dialog touched: open it, click the canvas → dialog stays
  visible.
- Modal dialogs: smoke-test that `exec()` still returns and the
  dialog is correctly destroyed.
- macOS app switch: Cmd-Tab away from SWMM → other apps come
  forward; Cmd-Tab back → dialogs reassert on top. (Not a
  regression — that's standard `WindowStaysOnTopHint` behaviour
  on macOS.)
- Existing plot dialog tests that drive `setVisible(true/false)`
  still pass (the helper does not change visibility, just flags).

### Estimated effort

~0.3 day (helper + rollout). The actual rollout is one-line-per-
dialog and happens during the Step E rollout phases at no
additional walltime cost — included here as a separate budget for
transparency.

### Files touched

- `include/ui/dialogs/dialoglayoutpersistence.h` /
  `src/ui/dialogs/dialoglayoutpersistence.cpp` — add helper next to
  the Step E helpers.
- Every dialog `.cpp` touched in Step E rollout: +1 line.
- `tests/gui/test_dialog_always_on_top.cpp` — new ~40-line test
  asserting `windowFlags() & Qt::WindowStaysOnTopHint` after the
  helper call.

---

## 9. Review checklist

Before I start implementing, please confirm:

- [ ] The "two CRUD editors" are the 3-pane (registry) variant vs the 2-pane
      (multi-provider) variant of `TimeseriesEditorDialog`, and removing the
      2-pane variant is the intent.
- [ ] Free-function helper (not a base class) for QSettings persistence is
      the preferred shape.
- [ ] Sequencing — Time Series first (steps A-E.2 + F), then Phase 1
      dialogs, etc. — matches your priorities.
- [ ] Initial splitter sizes 200/300/600 + dialog 1400×720 are reasonable
      defaults to start from (these are easy to tune in review).
- [ ] Skipping `AboutDialog`/`LicenseAgreementDialog`/`PluginsDialog` from
      Phase 3 is correct.
- [ ] Lazy-load / dispose on switch is per-provider (not refcounted) — i.e.
      we accept the "two dialogs on the same External provider" edge case
      as a known limitation for now.
- [ ] Disposing the cache on dialog close (not just on switch) matches your
      memory-conservation intent.
- [ ] Mesh toolbar radio-grouping handled via `toolActionKeys()` expansion +
      `setObjectName` (canvas-level sync), **not** by widening the
      `MeshEditingToolbar::m_editGroup` QActionGroup across toolbars.
- [ ] Always-on-top policy uses `Qt::WindowStaysOnTopHint` (pins globally
      with macOS app-deactivation relaxation), not just `Qt::Tool` (pins
      only above parent). Apply uniformly to all dialogs including modal
      and short-lived ones.
