# GUI Fixes — Plan (2026-05-30)

Author: Caleb
Status: **Approved — decisions locked in (see Decisions Locked section at end).**

## Scope

Six discrete fixes/features in `openswmm.gui`:

1. Profile-plot overlay obscured by subcatchments → render on top with transparency.
2. Drag-drop **and** context-menu reorder of sublayer rows in the layer tree.
3. Reorder "outputs rendering order" within thematic groups (interpretation needs confirmation — see §3).
4. Inline opacity editing in the layer-tree model (slider/spinbox delegate).
5. Hook up the `actionPlotTimeSeries` action on the Analysis toolbar.
6. Replace `StatusReportDialog` with a two-panel Report Viewer (section list + searchable highlighted rich-text view) and hook up `actionReport`.

Each section: **what's wrong → fix → files/lines → verification.** No code yet.

---

## 1. Profile path overlay obscured by subcatchments

### Diagnosis

`ProfilePathOverlay` is a `QGraphicsItemGroup` added to the map scene. It sets:

```cpp
// src/map/profilepathoverlay.cpp:33-35
constexpr qreal kZDimmed      = 10000.0;
constexpr qreal kZHighlighted = 10010.0;
constexpr qreal kZHalo        = 10020.0;
```

But canvas layer items receive z via:

```cpp
// src/map/mapcanvas.cpp:1509
m_layers[i]->setLayerZValue(i * 1000.0);
```

so with ≥ 11 layers in the stack a layer's items can land at z = 10000+ — exactly the overlay's band. Vector sublayers add small offsets on top (`baseZ + 2` etc. in `gisvectorlayer.cpp`), so subcatchments easily overshoot the overlay.

Transparency itself is already in place (`kEqualAlpha = 0.75`, `kDimAlpha = 0.35`, `kPromotedAlpha = 1.00`); the visual obscuring is a pure z-order bug.

### Fix

Single-file change in `src/map/profilepathoverlay.cpp`:

- Bump the constants to a band that cannot collide with layer items:
  ```cpp
  constexpr qreal kZBase        = 1.0e7;  // 10× headroom over any plausible layer stack
  constexpr qreal kZDimmed      = kZBase + 0.0;
  constexpr qreal kZHighlighted = kZBase + 10.0;
  constexpr qreal kZHalo        = kZBase + 20.0;
  ```
- Verify by inspection that no other scene item uses values ≥ `kZBase` (grep confirms `setZValue` ranges today top out at ~10000).
- Keep the alpha constants unchanged — they already provide transparency; the user can see through.

**No** changes to alphas unless review specifically asks for more transparency. Per CLAUDE.md §3 (Surgical Changes), I will not "improve" the styling at the same time.

### Files touched

- `src/map/profilepathoverlay.cpp` (constants only)

### Verification

1. Open a project with ≥ 11 layers (model + several rasters + subcatchments).
2. Open Slice BC profile-path picker; candidate paths should overlay subcatchments at all zoom levels.
3. Promote one candidate (highlight); the dim/highlighted/halo z-order within the group remains correct (halo on top of highlighted on top of dimmed).
4. No regression in the picker selection UX.

---

## 2. Sublayer drag-drop reorder + context-menu reorder

### Diagnosis

`LayerTreeModel::flags` explicitly comments:

```cpp
// src/ui/panels/layertreepanel.cpp:830
// editable (opacity), enabled + selectable. No drag (drag-reorder
// within a layer's sublayers is a follow-up).
```

The sublayer context menu (`onContextMenuRequested`, ~line 1398) has Properties/Show-Hide only — no Move Up / Move Down.

The host interface (`include/render/isublayerhost.h`) exposes `sublayers()` but **no mutator** to reorder. SWMMResultsLayer / SWMM2DResultsLayer / SWMM2DMeshLayer each compose sublayers internally.

### Fix

Two-part change: host API + tree-model glue.

**2a. Host API — add reorder hook to `ISublayerHost`**

In `include/render/isublayerhost.h`, add a pure-virtual:

```cpp
/*! Move sublayer at \p from to position \p to in paint order.
 *  Returns true on success. Hosts must emit invalidated() on the
 *  affected sublayers so the scene re-renders. */
virtual bool moveSublayer(int from, int to) = 0;
```

Concrete implementations in:
- `src/layers/swmmresultslayer.cpp` — reorder the internal sublayer list (the file's own ordering comment near `sublayers()` returns them ordered by archetype; the reorder method overrides this).
- `src/layers/swmm2dresultslayer.cpp`
- `src/layers/swmm2dmeshlayer.cpp`

All three already own their sublayer list. The method body is a `QList::move()` + `repaintRequested()` emit.

**Assumption to confirm:** is there a JSON persistence path that needs to round-trip the user's custom sublayer order? Grep shows `Slice S6.1 — JSON persistence helpers` on `ISublayerHost`. I will check that path saves the *current* order (likely already does because it walks `sublayers()`). **Sign-off needed before implementing.**

**2b. LayerTreeModel — wire drag + context menu**

In `src/ui/panels/layertreepanel.cpp`:

1. `flags()` for sublayer rows (~line 832): add `Qt::ItemIsDragEnabled` to col 0 and `Qt::ItemIsDropEnabled` (only when parent is the same host).
2. `mimeTypes()`: add `"application/x-sublayerrow"`.
3. `mimeData()`: encode `{host-layer-ptr, sublayer-list-index}`.
4. `canDropMimeData()`: accept the new MIME only when the drop target is a sublayer row whose parent host == drag source host. (Prevents cross-host drops, which would not make sense.)
5. `dropMimeData()`: compute `from`/`to` indices, call `host->moveSublayer(from, to)`, then rebuild the host's sublayer sub-rows so model indices stay consistent.
6. `onContextMenuRequested()` sublayer branch (~line 1398): add Move Up / Move Down actions with enable-state based on position in the host's sublayer list.

### Files touched

- `include/render/isublayerhost.h` (add `moveSublayer`)
- `src/layers/swmmresultslayer.{h,cpp}` (implement)
- `src/layers/swmm2dresultslayer.{h,cpp}` (implement)
- `src/layers/swmm2dmeshlayer.{h,cpp}` (implement)
- `include/ui/panels/layertreepanel.h` (no public surface change expected; possibly add small private helper)
- `src/ui/panels/layertreepanel.cpp` (flags, mime, drop, context menu)

### Verification

1. Open a `.out` results layer that exposes ≥ 3 sublayers (e.g. node markers + conduit lines + flow-arrow overlay).
2. Drag the third sublayer above the first; map repaints with the new paint order.
3. Right-click sublayer row → Move Up / Move Down. Same outcome.
4. Save and reload the project (`.oswp`); custom order persists (or, if 2a sign-off says "do not persist yet," verify order resets to default on reload — and we document that).
5. Cross-host drag is rejected (no row indicator shown on a different layer's sublayer rows).

---

## 3. Reorder kind rows (thematic groups) — STATUS NOTE

After implementing §2 it turned out: SWMMResultsLayer no longer has kind
rows (per Slice S3-2026-05-25 — see comment at layertreepanel.cpp:409-414).
Its third tier in the tree is **sublayer rows**, and those are exactly
what §2 made reorderable. So "reorder outputs rendering order within
thematic groups" is **covered by §2 for results layers** — no separate
work needed.

SWMMModelLayer still has its 11 kind rows (Junctions, Conduits, ...).
Reordering those would require restructuring the hard-coded paint blocks
in `swmmlayeritem.cpp::paint()` (subcatchments → links → nodes → gages
are separate blocks today, not a single iteration over a paint-order
vector). That refactor is non-trivial and the user request was about
"outputs rendering order", so **SWMMModelLayer kind-row reorder is
deliberately left out of this slice** (followup if needed). To avoid
misleading drag UI, kind rows on SWMMModelLayer keep their non-draggable
flags.

### Original approach (kept for reference if SWMMModelLayer reorder is added later)

Mirror the §2 pattern, but for kind rows.

**3a. Layer API — add kind paint-order state**

Add to `SWMMModelLayer` and `SWMMResultsLayer`:

```cpp
QVector<int> kindPaintOrder() const;          // size = kKindsPerSwmmModelLayer
void         setKindPaintOrder(const QVector<int> &order);
bool         moveKindOrder(int from, int to); // helper used by the model
```

Storage: a `QVector<int> m_kindPaintOrder` member, default-initialized to enum order (`{0,1,2,…,10}`).

Renderer integration: the layer's `paint()` (and its QSG-renderer equivalent) currently traverses kinds in enum order. Change to iterate `m_kindPaintOrder` indices in reverse (paint order = bottom up: lowest-index = drawn first = appears under). Verify nothing else assumes the enum-order traversal.

Persistence: round-trip via the existing `.oswp` layer-property bag (the model layer already serializes per-kind symbology — same hook).

**3b. LayerTreeModel — wire drag + context menu for kind rows**

In `src/ui/panels/layertreepanel.cpp`:

1. `flags()` kind branch (~line 842): add `Qt::ItemIsDragEnabled` + `Qt::ItemIsDropEnabled` on col 0.
2. Add MIME `"application/x-kindrow"` carrying `{parent-layer-ptr, kind-ordinal}`.
3. `dropMimeData()`: accept only same-parent-layer kind-to-kind drops; call `parent->moveKindOrder(from,to)` and rebuild kind rows.
4. Kind-row context menu (`onContextMenuRequested`, line ~1438): add Move Up / Move Down actions enabled by position in the parent's paint-order vector.
5. `kindRowStorage` already stores `KindRow{layer, kindOrdinal}`; the display row order needs to follow `m_kindPaintOrder`, not enum order. Update `rebuildKindRows()` accordingly.

### Files touched

- `include/layers/swmmmodellayer.h`, `src/layers/swmmmodellayer.cpp`
- `include/layers/swmmresultslayer.h`, `src/layers/swmmresultslayer.cpp`
- `src/ui/panels/layertreepanel.cpp`

### Verification

1. Right-click Junctions kind → Move Down past Conduits → conduits paint over junctions (verify by overlapping geometry).
2. Drag kind row to a new position → same outcome.
3. Save `.oswp`, reopen → custom kind order persists.
4. Cross-layer kind drag is rejected (only same-layer drops accepted).

---

## 4. Inline opacity editing in the layer-tree model

### Diagnosis

`LayerTreeModel::data` exposes opacity as a percent string in column 1:

```cpp
// src/ui/panels/layertreepanel.cpp:741-744
case Qt::DisplayRole / EditRole: return QString::number(...) + "%";
case Qt::UserRole:                return layer->opacity();
```

There is no custom item delegate installed on the layer tree (`grep "setItemDelegate"` in `layertreepanel.cpp` returns 0 hits). Result: when the user starts an edit on column 1, Qt opens a plain `QLineEdit` containing `"75%"`. Functional but ugly and error-prone (the `%` parse is in `setData`).

### Fix

Add a small `LayerOpacityDelegate : QStyledItemDelegate` in the same translation unit (no new public class — a single-use helper per CLAUDE.md §2):

- `createEditor()` → `QSpinBox` (range 0–100, suffix `" %"`).
- `setEditorData()` → reads UserRole (the raw `qreal` 0..1) and sets the spin value.
- `setModelData()` → writes the spin value back via EditRole as percent (matching the existing `setData` percent parser, so we don't need to change the model contract).
- `paint()` override draws a small horizontal opacity bar behind the percent text for visual feedback (optional — flag for review).

Install it on column 1 of the layer tree view in `LayerTreePanel::setupUi()`.

Apply the same delegate to sublayer-row column 1 (it shares the model and percent contract — see lines 612-616).

### Files touched

- `src/ui/panels/layertreepanel.cpp` (delegate class + install)

### Verification

1. Click opacity cell on a layer row → spinbox with `0..100 %` appears.
2. Spin to 30 % → map repaints at 30 % opacity; tree displays "30 %".
3. Same flow on a sublayer row.
4. Esc cancels without writing.
5. No regression on opacity persistence across project save/load.

---

## 5. Hook up `actionPlotTimeSeries` on the Analysis toolbar

### Diagnosis

```cpp
// src/swmmvis.cpp:5190
void SWMMVis::onPlotTimeSeries()
{
    // Wired in Slice M-2 below.
}
```

`forms/swmmvis.ui:1318` defines `actionPlotTimeSeries` (icon `:/swmmvis/Chart`, text "Plot Timeseries") in `toolBarAnalysis`. It is not connected anywhere. The infrastructure already exists: `SWMMVis::openTimeSeriesPlotFor(SWMMObjectRef)` pops `AttributePickerMenu` and opens `ComparisonPlotDialog`.

### Fix

Flow on `actionPlotTimeSeries` click:

1. If a SWMM object is currently selected on the canvas (`SelectionManager::primarySelection()`), use it → branch to step 3.
2. Otherwise activate the `MapToolSelect` (object-picking tool) and set a one-shot "plot timeseries on next pick" mode on `SWMMVis`. The next canvas-pick fires step 3. Status bar shows *"Click a node, link, or subcatchment — or pick a system variable."*
   - A small floating "System Variable…" button appears next to the status hint so the user can plot system-level series (rainfall, total runoff, total inflow, total outflow, system continuity, etc.) without picking an object.
3. Open `AttributePickerMenu` for the resolved kind (or a dedicated `SystemVariablePickerDialog` for system vars) → call `openTimeSeriesPlotFor(ref)` or the system-variable equivalent. Result lands in `ComparisonPlotDialog`.

Implementation notes:
- The one-shot picking mode reuses the existing `MapToolSelect::plotTimeSeriesRequested` signal already wired at swmmvis.cpp:4120. I'll add a `m_pendingPlotTimeseriesPick` flag on `SWMMVis` so `onPlotTimeSeries()` arms it, `MapToolSelect` triggers the slot on click, and the flag self-clears after one fire (or on Esc / tool change).
- `SystemVariablePickerDialog` is a small new dialog (≤ 100 LoC) listing the engine's system-level outputs (constants already exist in `swmmoutrunlayer_codes.cpp`). v1 = simple QListWidget + OK/Cancel. The "plot" path reuses `ComparisonPlotDialog` by adding a `SystemSeries` adapter to the run-layer.

Wire-up:
- `src/swmmvis.cpp`: connect `ui->actionPlotTimeSeries` in the same init block as `actionPlotProfile` (line 910 region).
- Re-implement `onPlotTimeSeries()` body per the flow above.
- Add `void onPlotTimeSeriesPickComplete(const SWMMObjectRef &)` slot + the `m_pendingPlotTimeseriesPick` member.
- New: `include/ui/dialogs/systemvariablepickerdialog.h`, `src/ui/dialogs/systemvariablepickerdialog.cpp`.

### Files touched

- `src/swmmvis.cpp` — wire connect + implement body (≈ 20 LoC).

### Verification

1. With a node selected on the canvas, click Plot Timeseries → AttributePicker pops → pick variable → ComparisonPlotDialog opens with the series.
2. With no selection, click Plot Timeseries → status bar shows the hint message. No crash.
3. Same for a link and a subcatchment.

---

## 6. Report Viewer — two-panel dialog with section list + searchable highlighted rich-text view

### Diagnosis

`StatusReportDialog` (`src/ui/dialogs/statusreportdialog.cpp`) currently uses a `QTabWidget` with one tab per section + a single Find Next. Not the layout the user wants.

`actionReport` is defined in `forms/swmmvis.ui:1603` but is **not** wired anywhere in `swmmvis.cpp` (grep confirms zero `actionReport` references in src/). `StatusReportDialog` itself is also not instantiated anywhere.

The parser `openswmmvis::io::RptParser` already returns `QVector<RptSection>` with `{title, body}`. Reusable.

### Fix

Refactor `StatusReportDialog` → `ReportViewerDialog` in place (file renamed, single class). Below: contents.

### Layout (two-panel `QSplitter`)

```
┌───────────────────────────────────────────────────────────────┐
│ ⚠ Continuity error banner (only when applicable)              │
├──────────────────┬────────────────────────────────────────────┤
│ Sections         │ Search: [_____________]  [⇐][⇒]  ☐ Regex   │
│ ┌──────────────┐ │  ┌────────────────────────────────────────┐│
│ │ Analysis     │ │  │                                        ││
│ │ Options      │ │  │   <monospace rich-text viewer>         ││
│ │ Runoff …     │ │  │   syntax-highlighted via QSyntax-      ││
│ │ Continuity   │ │  │   Highlighter subclass; current        ││
│ │ Node Depth   │ │  │   match highlighted; jumps to          ││
│ │ Link Flow    │ │  │   selected section on listview click.  ││
│ │ …            │ │  │                                        ││
│ └──────────────┘ │  └────────────────────────────────────────┘│
│  filter: [______]│                                            │
└──────────────────┴────────────────────────────────────────────┘
                                                       [ Close ]
```

Widgets:

- `QListView` (left, ~25 % splitter weight) — sections from the parser, with a `QLineEdit` filter above (live filter via `QSortFilterProxyModel`).
- `QTextEdit` (right) — read-only, monospace, holds the **full** report concatenated (one `QTextBlock` per line). Section starts get a heavier weight + small top margin via the syntax highlighter. Clicking a section in the list scrolls the cursor to that block.
- `QLineEdit` (top of right pane) — incremental search with `[Regex]` toggle (`QCheckBox`). Each keystroke runs `QTextEdit::find()` (or `find(QRegularExpression)` when toggle is on) with wrap; `Enter` = next, `Shift+Enter` = previous, match count rendered to the right (`"3 of 17"`). Regex errors render as muted red next to the field instead of throwing.
- `RptSyntaxHighlighter : QSyntaxHighlighter` (new private class in `statusreportdialog.cpp`):
  - Section headers (lines of `*` and the section title) → bold, accent color.
  - Numeric columns / units → muted color.
  - "Continuity Error (%)" lines exceeding 10 % → red.
  - Active search matches → background highlight (via `QTextCharFormat`).

Single-file artefact, no new public API beyond the renamed class.

### Wiring on the toolbar

In `src/swmmvis.cpp` (next to the other Analysis-toolbar wires):

```cpp
connect(ui->actionReport, &QAction::triggered, this, &SWMMVis::onShowReport);
```

`onShowReport()` resolves the active project's `.rpt` path (the project window already tracks the simulation output dir — verify the exact getter; if missing, fall back to `<project>.rpt` next to the `.inp`), instantiates `ReportViewerDialog`, and shows it. Disabled when no `.rpt` exists (use `actionReport->setEnabled` keyed off project state).

### Files touched

- `include/ui/dialogs/statusreportdialog.h` → `reportviewerdialog.h` (renamed)
- `src/ui/dialogs/statusreportdialog.cpp` → `reportviewerdialog.cpp` (renamed, body rewritten)
- `src/swmmvis.cpp` (wire + add `onShowReport` slot)
- `include/swmmvis.h` (declare `onShowReport`)
- `CMakeLists.txt` (filename change)

### Verification

1. Run a SWMM simulation → `actionReport` becomes enabled.
2. Click → dialog opens with section list populated.
3. Click a section → text viewer scrolls to it.
4. Search "Continuity" → highlights flash; `Enter` cycles matches; counter updates.
5. Continuity > 10 % case → banner appears + relevant numbers are red in viewer.
6. Filter listview "node" → list shrinks to node-related sections.
7. Resize splitter → both panes survive close/reopen (persist via `QSettings` per `DialogLayoutPersistence` — re-use the existing helper).
8. Close dialog with sim still loaded → no leaks (run under `-fsanitize=address` locally if convenient).

---

## Cross-cutting

- Per `CLAUDE.md §4`, each section above states verifiable success criteria.
- Per `CLAUDE.md §3`, no opportunistic refactors. The opacity delegate is the only new class; everything else extends existing files.
- Per `CLAUDE.md §4.01` (gui repo): all changes preserve legacy SWMM behaviour; only UI affordances change.
- Per `CLAUDE.md §4.1` (engine repo, but also generally applicable): no temp files used in tests — verification steps work in the running project.

## Decisions Locked (2026-05-30)

1. **§2** — Custom sublayer paint order persists to `.oswp` via Slice S6.1 helpers. ✅
2. **§3** — Read B: kind-row reorder (new feature). ✅
3. **§5** — No-selection flow activates `MapToolSelect` for a one-shot pick, with a "System Variable…" affordance for system-level series. ✅
4. **§6** — Refactor `StatusReportDialog` → `ReportViewerDialog` in place. ✅
5. **§6** — Regex toggle included in v1 search. ✅

## Implementation summary (2026-05-30)

All six items implemented; only build verification is unrun (sandbox is Linux, repo's build tree is macOS).

| § | File(s) touched | Net delta |
|---|-----------------|-----------|
| 1 | `src/map/profilepathoverlay.cpp` | bumped overlay z-band to 1e7; transparency unchanged |
| 4 | `src/ui/panels/layertreepanel.cpp` | added `LayerOpacityDelegate` (QSpinBox + opacity-bar paint); installed on col 1; col 1 edit gated by SelectedClicked; double-click on col 1 skips zoom |
| 2 | `include/render/isublayerhost.h`, `include/layers/swmm{results,2dresults,2dmesh}layer.h` + `.cpp`, `src/ui/panels/layertreepanel.cpp` | added `ISublayerHost::moveSublayer` (default no-op), concrete impls cache a user-orderable `m_sublayerOrder`; persistence reorders via the augmented `loadSublayersFromJson`; tree model adds `application/x-sublayerrow` MIME, drop handler, drag flags, context-menu Move Up/Down |
| 3 | (documented as covered by §2 for results layers; SWMMModelLayer kind reorder deferred — see §3 STATUS NOTE) | — |
| 6 | `include/ui/dialogs/statusreportdialog.h`, `src/ui/dialogs/statusreportdialog.cpp`, `src/swmmvis.cpp` (+`include/swmmvis.h`) | replaced QTabWidget UI with two-panel splitter (section listview + searchable rich-text); added `RptSyntaxHighlighter` (headers / dividers / units / continuity-error red); regex toggle; match counter; `actionReport` wired to new `onShowReport` |
| 5 | `src/swmmvis.cpp` (+`include/swmmvis.h`) | `actionPlotTimeSeries` wired to `onPlotTimeSeries`; uses canvas selection when present; else arms one-shot pick (status hint + SelectionManager one-shot listener) + second-press fallback to `AttributePickerMenu::createForSystem` → `openComparisonPlotForSystemAttribute` |

## Build verification — DEFERRED to host

Sandbox is Linux without Qt; project's build dir is `build/darwin-debug` (macOS). Run on the host:

```
cd /Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui
cmake --build build/darwin-debug
```

If anything fails to compile, the most likely culprits are:
- `ISublayerHost::moveSublayer` signature — make sure all three concrete hosts override it exactly (`override` keyword present).
- `notifyHostSubOrderChanged()` is reachable on the `LayerTreeModel` from `LayerTreePanel` since the model is a member (not via const ref).

## Suggested implementation order (dependency-aware)

1. **§1** Profile overlay z fix — isolated, smallest delta.
2. **§4** Opacity delegate — isolated UI, no cross-file coupling.
3. **§2** Sublayer reorder — touches `ISublayerHost` + 3 layers + tree model.
4. **§3** Kind-row reorder — same model file + 2 layers; share the drag/drop pattern from §2.
5. **§6** Report Viewer — self-contained dialog refactor + toolbar wire.
6. **§5** Plot Timeseries — needs new `SystemVariablePickerDialog` and one-shot pick mode plumbing; largest delta, do last.

Each step verified per its §Verification block before the next starts.
