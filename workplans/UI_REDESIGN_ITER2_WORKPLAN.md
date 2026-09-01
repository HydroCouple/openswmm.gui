# swmmvis GUI Redesign — Iteration 2: ArcGIS-Pro Ribbon, Icons, Dialog Stacking + Dialog Sweep & Persistence

## Status: DRAFT (iteration 2) — P0-P9 of iteration 1 EXECUTED 2026-08-01 (109/109 gate)

## Iteration-2 Context

User feedback on the delivered tabbed toolbar (P6). Eight asks:
1. **Taller ribbon + larger icons matching ArcGIS Pro** (32px icons, ~86px content row).
2. **Icon + text on every action** (accessibility) — Qt::ToolButtonTextUnderIcon.
3. **Separators grouping related actions within a tab** — resolved with user to FULL
   ArcGIS captioned groups (labeled group boxes, caption beneath each cluster).
4. **Select + Select By Region consolidated into one dropdown button** (same
   MenuButtonPopup pattern as Plot Profile at swmmvis.cpp:1485-1489); the face button
   must visibly show the active tool's toggled state (setDefaultAction sync).
5. **Overflow**: tabs overrun the window edge — resolved with user to family
   consolidation dropdowns (Add Node ▾ / Add Link ▾ / Climate ▾ / Add Data Object ▾ /
   Import ▾) PLUS auto-compact responsiveness (groups step Full → Compact(icon-only)
   → Collapsed(single dropdown) from the trailing end as width shrinks).
6. **Icons**: user added resources/images/undo.svg + redo.svg (VERIFIED present) — wire
   to edit.undo/edit.redo. Design new SVGs (house style: viewBox 0 0 24 24, stroke
   #777777, stroke-width 2, fill none) for the 23 remaining icon-less catalog entries
   (dock toggles ×7, styleManager, layerStylingDock, userFlags, exportMap, clearRecent,
   plugins, about, keyboardShortcuts, minimize, zoom, landUse, aquifer, snowpack,
   unitHydrograph, street, inlet) + dedicated LID/Pollutant (replace "Layers"
   placeholder) + commandPalette.
7. **Modeless dialog stacking regression — ROOT-CAUSED**: the old behavior was NSWindow
   child-window attachment (`attachAsChildWindow`, macoswindowutils.mm:16-50 —
   `addChildWindow ordered:NSWindowAbove` on the dialog's resolved top-level parent =
   the main window; AppKit keeps children in an ordered per-parent stack = exactly
   "stayed on top depending on the order opened"). Entered in 33dff07; REMOVED in
   5d43e28 blaming an input freeze — but ddca63d (12 min later) root-caused that freeze
   as the modal-exec-during-mousepress QNSView button-latch, fixed by fire-on-release.
   The removal was a misdiagnosis; ALL machinery is intact (attach fn, detach already
   wired to Close/Hide in the eventFilter, AppKit link in CMake). Side casualty:
   `floatingPanelFlags()`/`stayAboveAppFlags()` became macOS no-ops (bare Qt::Dialog /
   empty flags) — 13 call sites across 12 dialog classes now have NO stay-above
   mechanism on macOS.
   **Fix**: 2-line restore of `attachAsChildWindow(dlg)` on QEvent::Show gated
   `windowModality() == Qt::NonModal` (swmmvisapplication.cpp eventFilter ~:185-212)
   + manual freeze retest (right-click cell pick → plot dialog → click map/toolbar/
   second dialog; verify no phantom-drag freeze). Attach is idempotent (early-out when
   already attached) so the find-or-create show/raise sites (swmmvis.cpp:2370, 2452,
   2492, 2632, 2683, 2738…) are safe. **Fallback if retest fails**: pure-Qt re-raise of
   visible modeless dialogs (tracked open-order list) on QEvent::ApplicationActivate /
   main-window WindowActivate. Modeless population: ComparisonPlot/ProfilePlot/
   MeshProfilePlot/RasterProfilePlot + 5 singleton editors (Curve/Pattern/Transect/
   Rules/Timeseries) + StatisticsDashboard/StatusReport/SimulationOptions/
   LayerStyleDialog.
8. **Dialog-wide sweep (ADDED 2026-08-01)**: "the same accessibility and uniform styling
   analysis ... for all of the dialogs associated with swmmvis" + "styling and layout ...
   saved in QSettings in between sessions so dialogs retrieve the latest views and layouts
   used." Interpretation (fixed): per-dialog VIEW/LAYOUT state persists — geometry,
   splitters, header states, current tab, nav page, view-mode toggles — NOT data content
   (global theme already persists via Appearance mode). User decisions:
   meshgenerationdialog.cpp IS in scope but minimal-diff only (the user's uncommitted
   mesh/DTM edits live in that file); the 7 never-constructed dialogs (CustomReport,
   CalibrationData, ScatterPlot, TabularResults, SublayerStyle, WMSConnection,
   WMTSConnection) ARE in scope, treated as live UI. → Full design + phases in
   "Dialog Track (D1–D6)" below.

Facts verified this session:
- Profile dropdown pattern to reuse: QToolButton via toolBar->widgetForAction +
  setMenu + MenuButtonPopup (swmmvis.cpp:1485-1489).
- No setIconSize/setToolButtonStyle anywhere yet (clean slate for sizing).
- 105 catalog rows, 25 without icons (list above; undo/redo covered by user SVGs).
- Embedded toolbar widgets are all member pointers inserted at action anchors
  (swmmvis.cpp:1031-1058 analysis combos, :1203-1223 animation cluster) — migrating
  them into ribbon groups = changing insertWidget targets only; wiring untouched.
- toolActionKeys objectName sync is action-based → unaffected by split buttons
  (face mirrors defaultAction's checked state).

Dialog-track facts (verified 2026-08-01, three fan-out audits):
- 54 header-declared QDialog subclasses (+ file-local SymbolDetailsDialog in
  categorizedrendererpanel.cpp:213, + 2 transient prompts swmmvis.cpp:1938/:2575); ALL
  hand-coded C++ — forms/swmmvis.ui is the only .ui file. 7 are dead code (never
  constructed); user chose to include them as live.
- dialoglayoutpersistence.{h,cpp}: free fns saveDialogLayout/restoreDialogLayout —
  geometry (QRect, screen-clamped) + named-QSplitter states under
  Dialogs/<windowObjectName>/...; NO-OP when the window objectName is empty. Only 2
  dialogs fully wired (TimeseriesEditor, import/ImportFeatureLayer); only 7 dialogs call
  setObjectName at all; the only named splitters repo-wide are "main" ×2 +
  "xsectionShapeSplitter"; NO named views/tabs/stacks anywhere; nothing persists
  QHeaderView/tab-index/page-index anywhere. 4 dialogs use divergent ad-hoc schemes:
  HydrographGroupEditor, RulesEditor, PatternEditor (its plotStyle/* keys must keep
  working), TransectEditor (saves in DESTRUCTOR). 47 dialogs resize() unconditionally in
  ctor (ordering hazard the watcher design dissolves).
- App seam VERIFIED: SWMMVisApplication::eventFilter sees every top-level dialog's
  Show/Close/Hide — BUT its Close/Hide branch is #ifdef Q_OS_MACOS-only, so auto-persist
  must be a SEPARATELY-INSTALLED filter (also avoids any R1 merge collision in that fn).
- A11y baseline: setBuddy ×1 repo-wide, setTabOrder ×0, accessible names ×0 in all of
  src/ui/dialogs/. 444 QFormLayout::addRow(QString,...) rows = FREE implicit buddies,
  only 9 carry mnemonics; 44 addTab calls, 0 mnemonics (QTabBar supports '&'); 13
  form-less dialogs need ~55 explicit setBuddy (timeseries 14, addbasemap 8, …);
  LabeledControls never sets buddies (5 ctor fixes cover 3 consumer files); 7 icon-button
  factory lambdas already take tooltip → ~50 icon-only buttons fixable at once. BUG:
  pollutanteditordialog.cpp:117 tr("I&I Concentration") renders "II Concentration" with
  accidental Alt+I (must be I&&I).
- Styling: lint covers every dialog file with 0 current hits BUT 2 live lint-invisible
  defects — simulationoptionsdialog.cpp:1783-1797 variable-assigned "#ffc8c8" error fill
  (illegible on dark; worst survivor) and statusreportdialog.cpp:612,653 HTML
  <span color:#b01c00> — plus 59 raw QIcon(":/swmmvis/...") in dialogs (dark-broken), 4
  literal "color: palette(mid)" dupes that should be hintStyle(), 9 pseudo-heading <b>
  sites, 10 setFixed* sites (CommandPalette's 2 are by design). Hand-rolled button rows
  in 9 dialogs; the other 46 already use QDialogButtonBox.
- Test infra: 12 dialogs have tests; per-binary org/app-name + group-wipe idiom
  (test_shortcut_editor), STRONGEST isolation = QTemporaryDir + IniFormat setPath
  (test_pattern_editor_dialog.cpp:618-624 — promote to member); offscreen screen is
  ~800×600 and clampToAvailableScreens_ REWRITES out-of-bounds rects (test rects must sit
  inside availableGeometry); round-trip shapes to copy: persistence_PlotStyleSurvivesReopen
  + lastTabPersists; NO construct-all-53 harness is feasible (ctor deps) — use a shared
  assertion header + per-dialog tests; AUTOMOC needs include/-headers listed as sources
  (CMakeLists note :1659).

## Iteration-2 Implementation Plan (R0–R6)

Discipline per iteration 1: every phase ends with clean build, targeted tests, full
`ctest -L gui` gate (109 baseline + additions incl. lint_chrome_colors), launch smoke.

### Architecture decisions

**A. RibbonGroup = widgets INSIDE the existing per-tab QToolBars** (bars kept — zero
saveState churn, controller + test_compact_toolbar untouched; chevron stays as final
backstop). New `openswmmvis::ui::RibbonGroup : QWidget` added via `QToolBar::addWidget`:
content row of auto-raise QToolButtons (`setDefaultAction` — icon/text/checked/enabled
mirroring + triggered routing free) + caption QLabel beneath (AlignHCenter,
`setForegroundRole(QPalette::Mid)` = hintText token, 0.85× font) + right VLine QFrame
(`setForegroundRole(QPalette::Dark)` = border token). `setFixedHeight(kRibbonRowHeight=86)`
all modes; `kRibbonIconFull=32`, `kRibbonIconCompact=24`. API: addAction (optional short
label override), addFamily, addWidget(w, stretch), buttonForAction (re-anchors the
Plot-Profile dropdown), setMode, widthForMode (cached off-screen measurement),
isCollapsible (false when hosting member widgets → solver sees equal widths).
**Zero stylesheets/hex — palette roles only → lint needs no allowlist entries; light/dark
free via ThemeManager palette.** (Permitted knob if Fusion padding is tight: geometry-only
QSS `QToolBar QToolButton { padding: 2px 6px; }` in the ThemeManager overlay.)

**B. Auto-compact = pure solver + thin applier.** Header-only
`include/ui/toolbars/ribbonlayoutsolver.h`: `solveRibbonModes(availableWidth, groups
{full,compact,collapsed,collapsible}, spacing) → QVector<RibbonMode>` — demote trailing→
leading Full→Compact, then trailing→leading Compact→Collapsed (collapsible only).
`RibbonCompactor : QObject` event-filters each group-bearing bar (Resize/Show/
LayoutRequest → compressed singleShot relayout). Hysteresis: promotions only when they
also hold at width−32px (pure `applyWithHysteresis` helper, unit-tested). Modes: Full =
TextUnderIcon 32px + caption; Compact = IconOnly 24px + caption; Collapsed = content
hidden, one InstantPopup QToolButton titled by caption whose menu = the group's actions.

**C. Split-button families = `RibbonSplitButton : QToolButton`** (MenuButtonPopup —
verified pattern swmmvis.cpp:1485-1489). Face = last-used member via setDefaultAction;
persisted `SWMMVis::Ribbon/LastUsed/<familyId>` (objectName; unknown → first). Face
promotes on any member `triggered` AND on checkable member `toggled(true)` — so
toolActionKeys programmatic tool sync (Esc → select) swaps the face and shows checked
state. Families: select{Select, SelectByPolygon}, addNode{4}, addLink{5}, climate{5},
addDataObject{7 data.new*}, import{6}.

**D. Group layout data = static RibbonTabSpec/RibbonGroupSpec/RibbonItemSpec tables in
src/swmmvisactions.cpp** (NOT catalog columns — catalog stays identity/shortcut/menu/
palette; layout is presentation). Tolerant name resolution like the registry sweep.

**E. Per-tab groups** (caption {contents}):
- Home: Project{New,Open,Save} · History{Undo,Redo} · Navigate{Pan,ZoomIn,ZoomOut,
  Extent,ToSelection} · Select{Select▾, Invert, Upstream, Downstream} · Inspect{Measure,
  Search,Copy} · Import{Import▾} · Run{Execute,Pause,Cancel}
- Model: Edit{EditExisting} · Draw{AddNode▾, AddLink▾, Subcatchment, RainGage, Text} ·
  Climate{Climate▾} · Data Objects{AddDataObject▾} · Setup{Options, UserFlags,
  ImportFeatureLayer} · Mesh 2D{GenerateMesh}; TerrainToolbar stays as sibling bar.
- Mesh 2D: Mesh{GenerateMesh}; MeshEditingToolbar sibling bar (NOT dissolved).
- Analysis (bar rebuilt in code, objectName "toolBarAnalysis" KEPT): Results Layers
  {1D/2D combos + live check member widgets} · Report{Summarize, Report, TabularView} ·
  Plots{PlotTimeSeries, PlotProfile+dropdown} · Network Analysis{FlowBalance↓↑,
  TravelTime↓↑, MassBalance}
- Results (bar rebuilt, objectName "toolBarAnimation" KEPT): Playback{SkipBack,SkipFwd,
  Play,Pause,Stop} · Timeline{slider(stretch=1), window spin, datetime, speed — member
  widgets} · Display{ShowLegend, SetStyle}
- View: Panels{7 dock toggles} · Styling{LayerStylingDock, StyleManager} ·
  Start{ShowWelcome}

### Phases

**R0 — Baseline**: branch `ui-iter2`; build + full suite (record 109/109) + smoke.

**R1 — macOS dialog stacking restore** (src/swmmvisapplication.cpp ONLY): re-add the
pre-5d43e28 hunk in the Show lambda —
`#ifdef Q_OS_MACOS if (dlg->windowModality()==Qt::NonModal)
openswmmvis::platform::attachAsChildWindow(dlg); #endif` — replacing the NOTE comment
(update it: ddca63d re-diagnosis). Machinery intact: attach idempotent
(macoswindowutils.mm:39 early-out), detach-on-Close/Hide already wired (:176-183),
addChildWindow ordered:NSWindowAbove = open-order stacking at normal level. Hand-rolled
show/raise sites need no change (re-show attached dialog = no Hide → still attached;
raise() reorders children correctly).
**MANUAL freeze-retest checklist (user-assisted; must pass before R2):** (1) load model +
2D results; (2) right-click cell → popover → plot dialog (the ddca63d scenario) — no
freeze; (3) click/pan/drag map, toolbar, docks — input alive; (4) open second modeless
dialog — both stack above main in open order after clicking main window; (5) Cmd+Tab
away/back — dialogs behind other apps when inactive; (6) re-plot to the same open dialog
— stacking + input intact; (7) close via X/Esc, reopen, minimize/restore main.
**Designed fallback if retest fails** (5-line revert + pure Qt): track
`QList<QPointer<QDialog>> mModelessOrder` in the same eventFilter (append on NonModal
Show, drop on Close/Hide/Destroyed); on ApplicationActivate + main-window WindowActivate,
`raise()` each in order (never activateWindow).

**R2 — Ribbon framework, dormant**: NEW ribbongroup.h/.cpp, ribbonsplitbutton.h/.cpp,
ribbonlayoutsolver.h, ribboncompactor.h/.cpp (+CMake near :614/:1280); NEW
tests/gui/test_ribbongroup.cpp (links the 4 ribbon TUs standalone): defaultAction
mirroring, mode switching (style/iconSize/visibility, constant height), width
monotonicity, solver cases (fits, trailing-first, two-pass, non-collapsible clamp,
degenerate widths), hysteresis dead band, split-button restore/promote-on-trigger/
promote-on-toggled(true)/unknown-name fallback. Wire user SVGs: qrc aliases Undo/Redo →
images/undo.svg,redo.svg; catalog edit.undo/edit.redo icon cells → "Undo"/"Redo"
(test_action_catalog + test_icon_factory pick them up automatically — must land together).

**R3 — Swap tabs to captioned groups.**
R3.1 code-built tabs: rewrite the four addActions lists in initializeCompactToolbar
(swmmvisactions.cpp:143-190) into RibbonTabSpec tables (families still inline plain
buttons this phase); buildRibbonRows() populates the existing bars.
R3.2 .ui-authored two: delete toolBarAnimation/toolBarAnalysis blocks from
forms/swmmvis.ui (:740-783; action definitions STAY); create replacement bars with the
SAME objectNames at the top of initializeToolBars (swmmvis.cpp:732 — before the
animation/analysis init fns and before initializeSettings' restoreState, preserving
iteration-1 ordering); swmmvis.h gains the two bar members + 7 group members;
initializeAnimationToolBar populates Playback/Timeline/Display groups (insertWidget
anchor calls :1203-1224 → group->addWidget in creation order);
initializeAnalysisLayerCombos populates Results Layers (:1019-1059 anchors gone);
Plot-Profile dropdown re-anchors via mGroupPlots->buttonForAction (:1485-1489); drop the
TabularView insertAction special case (swmmvisactions.cpp:194-196 — it's a Report row).
Gate: form audit + compact toolbar + hydration contract + full suite; smoke: six tabs at
86px, scrubber stretches, combos live, tool-sync radio behavior, relaunch layout.

**R4 — Family split-buttons**: flip the six table rows to familyId+members
(swmmvisactions.cpp only). Smoke: Select▾ face follows active tool incl. programmatic
Esc-to-select (checked visible both members); families place features; last-used faces
survive relaunch; menubar routes unchanged.

**R5 — Auto-compact on**: construct one RibbonCompactor per group-bearing bar at the end
of buildRibbonRows. Widget-level test: 3-group bar on resizable window — shrink steps
trailing group Full→Compact→Collapsed, leading stays Full; slow grow across boundary
doesn't flap; collapsed popup triggers actions. Smoke: narrow drag steps down
right-to-left; widget groups + Terrain/Mesh bars never collapse; chevron only extreme.

**R6 — Icons + catalog + qrc (atomic)**: author 24 new SVGs in resources/images/ (house
style viewBox 0 0 24 24, stroke #777777, width 2, fill none) per design-intent table
(dock_layers stacked sheets; dock_object_browser tree; dock_properties label-value rows;
dock_attribute_table grid w/ header; dock_legend swatch column; dock_simulation_status
gauge; dock_message_logs console; style_manager palette; layer_styling sliders;
user_flags pennant; export_map map+arrow; clear_recent broom; plugins puzzle; about info
circle; keyboard_shortcuts keyboard; window_minimize/window_zoom frames; land_use
parcels; aquifer strata+waves; snowpack flake over layers; unit_hydrograph axes+curve;
street road section; inlet curb grate; lid_control substrate+sprout; pollutant
droplet+dots; command_palette prompt+cursor). qrc + catalog icon-cell updates in the SAME
change (iconAliasesResolveInQrc/allCatalogAliasesRender enforce); commandPalette
Search→CommandPalette, LID/Pollutant Layers→dedicated. IconFactory sweep applies them —
no wiring. Smoke light+dark at 32px and 24px.

### Risk register (iteration 2)
saveState: all six bar objectNames preserved, version stays 2 · hydration contract:
statusbar untouched, test in every gate · toolActionKeys: actions never recreated;
split-button listens to toggled(true) · attach retest fails → designed Qt fallback,
5-line revert · .ui edit churns ui_swmmvis.h (21 refs, all migrated R3.2) · chevron+
QWidgetAction clunky → near-unreachable post-R5, accepted interim R3-R4 · long
TextUnderIcon labels → per-item label override · icons/catalog atomicity → sequenced
R2/R6 · transitional text-only ribbon buttons for icon-less actions until R6 (accepted).

### Out of scope (iteration 2)
Status-bar/dock redesigns · menu-IA or shortcut changes · Windows/Linux ribbon QA beyond
compile · tab/button QSS re-skin · contextual-tab tinting · animated transitions ·
dissolving Terrain/MeshEditing toolbars · sibling-bar equal-height polish · adding the 6
menu-only data.new* to the family (stays 7) · S4/S5 symbology.

### Verification (iteration 2)
Per-phase gates above. Program-level: full `-L gui` (110 tests incl. test_ribbongroup)
green at every phase; macOS launch smoke per phase; R1 interactive checklist is
USER-ASSISTED (I implement + launch; you exercise the right-click-plot freeze scenario);
final visual QA: ArcGIS-scale ribbon in light+dark, narrow-width stepping, dialog
stacking during a real map↔dialog editing session.

## Iteration-2 Dialog Track (D1–D6) — ask 8, runs after R6

Scope: 53 dialogs (54 header-declared minus CommandPalette persistence opt-out) + dialog-
owned child panels as a11y ride-alongs (~160 more form rows: swmm2dresultsstylepanel 28,
labelstab 14, symbologytab(s), 11 files in src/ui/dialogs/editors/ — symbolstyleeditors2d
alone 47). Same per-phase discipline as R-track: clean build, full `-L gui` green
(baseline count grows each phase), macOS launch smoke.

### Design decisions (fixed)
1. **DialogLayoutWatcher : QObject** — NEW app-level event filter installed as a SEPARATE
   filter (one line in the SWMMVisApplication ctor next to the existing
   installEventFilter(this)); ZERO edits inside the existing eventFilter body (its
   Close/Hide branch is macOS-only, and R1 edits that fn — no collision). Semantics: on
   QEvent::Show of a top-level QDialog with non-empty objectName, no "noLayoutPersistence"
   dynamic property, and unset "layoutRestoredOnce" instance property →
   restoreDialogLayout(dlg) SYNCHRONOUSLY (pre-map = no flicker; runs AFTER ctor resize()
   so saved geometry wins — the 47 unconditional ctor resize() calls become first-run
   defaults, untouched, and per-dialog `if(!restore)` guards are never needed again). On
   QEvent::Hide || QEvent::Close → saveDialogLayout(dlg) (Hide covers QDialog::done()/
   exec accept-reject which never send Close; double-fire idempotent; destruction-
   without-hide = accepted miss). Synchronous restore vs R1's DEFERRED attach singleShot:
   order-independent.
2. **Geometry format stays QRect + clampToAvailableScreens_** (the helper's deployed
   format). The 4 ad-hoc dialogs stop writing their saveGeometry() byte-array keys; stale
   keys are ignored — never read, never deleted (one-time fallback to ctor defaults).
   PatternEditor's dialogs/patternEditor/plotStyle/* read/write stays untouched (its
   existing test guards it).
3. **Key scheme** — all under `Dialogs/<objectName>/`: `geometry` (QRect),
   `splitter/<name>`, NEW `header/<viewName>` (QHeaderView::saveState of a named view's
   header), `tab/<tabWidgetName>` (int currentIndex, bounds-checked on restore),
   `page/<stackName>` (int, bounds-checked), `toggle/<actionName>` (bool restored via
   setChecked so toggled() fires and views update). Restore order: geometry → splitters →
   headers → tabs → pages → toggles. objectName = class name; multi-instance compound
   editors and SimulationOptions' dual exec/modeless mode share one class key
   (last-close-wins, accepted + documented in the helper docstring).
4. **Opt-in naming rule** (extends the helper's existing philosophy): only NAMED children
   persist. Name user-arranged splitters, views whose column layout matters, user-nav
   QTabWidgets, user-nav QStackedWidgets, checkable view QActions. NEVER name data-driven
   stacks — compound-edit dialogs' stacks follow the object TYPE; persisting them would
   fight the data.
5. **Nav-sync for page restore**: Preferences/SimOptions drive m_pages one-way from the
   category list (verified simulationoptionsdialog.cpp:253-254) — restoring the stack
   alone would desync the highlight, so each gains ONE reverse connect (pages
   currentChanged → list setCurrentRow; same-row set is a loop-safe no-op).
6. **Editor-family affordance**: convert existing hand-rolled Close/OK rows to
   QDialogButtonBox in the 9 dialogs that have rows (customreportdialog already includes
   the header but hand-rolls Close); do NOT bolt boxes onto box-less modeless editors
   (persistent tool windows) — verify Esc-close + sane initial focus instead.
7. **Tab order**: audit-and-fix-on-break only (iteration-1 philosophy); no blanket
   setTabOrder churn.

### D1 — Persistence core: vocabulary + watcher + unit tests
Extend include/ui/dialogs/dialoglayoutpersistence.h + src/.../dialoglayoutpersistence.cpp
with header/tab/page/toggle save+restore (walk findChildren, skip empty objectNames);
update the header docstring to the watcher-era recipe ("wiring = naming"). NEW
include/ui/dialogs/dialoglayoutwatcher.h + src/ui/dialogs/dialoglayoutwatcher.cpp
(Q_OBJECT eventFilter override + inline constexpr kNoLayoutPersistenceProp /
kLayoutRestoredOnceProp). One install line in the SWMMVisApplication ctor; app CMake +1
cpp. NEW tests/gui/test_dialog_layout_persistence.cpp (add_swmmvis_gui_test; include/-
headers listed for AUTOMOC): isolation = QTemporaryDir MEMBER + IniFormat setPath in
initTestCase; cases: geometry round-trip (rects inside offscreen availableGeometry),
named-vs-unnamed splitter opt-in, header state, tab/page bounds-check vs shrunken widget,
toggle restore fires toggled(), empty-objectName no-op, opt-out property,
once-per-instance guard, clamp rewrite of out-of-bounds rect, watcher end-to-end
(install, show/hide a named dialog). Behavior change for unnamed dialogs: none.
Gate: build + full suite + smoke (move/resize TimeseriesEditor, close, reopen).

### D2 — Ad-hoc migration + rich-state adopters (~20 dialogs)
Remove now-redundant explicit code: timeserieseditordialog.cpp (ctor restore :135,
closeEvent save :144 — keep the names), import/importfeaturelayerdialog.cpp
(if(!restore) guard → plain resize; closeEvent save). Migrate the 4 ad-hoc schemes to
naming-only: hydrographgroupeditor.cpp (closeEvent/showEvent QSettings + byte keys out),
ruleseditordialog.cpp, patterneditordialog.cpp (drop geometry/splitterState writes ONLY;
plotStyle stays), transecteditordialog.cpp (destructor-save pattern dies). Leave
StatsSummaryPanel's self-managed ComparisonPlotDialog/Stats* keys functioning. Rich
adopters (naming + child naming only): comparisonplotdialog (name m_splitter/
m_chartsOuter/m_chartsSplitter + 7 checkable view actions; singleton re-show protected by
the once-guard), layerstyledialog (4 QTabWidgets incl. nested), statisticsdashboarddialog
(tabs + 3 sortable table headers), preferencesdialog + simulationoptionsdialog (name
m_pages + reverse nav-sync connect; simopts' unnamed splitter :251), node/link/subcatch
compound-edit dialogs (named views/headers; type-stacks NOT named), the 7-editor family
Aquifer/Inlet/LandUse/LidControl/Pollutant/Snowpack/Street (one mechanical pattern: self
name + splitter "main"), profile-plot family Profile/MeshProfile/RasterProfile (geometry
+ checkable toolbar toggles), crsselectiondialog (5 launch sites, one shared class key).
Tests: watcher-path round-trips added to test_timeseries_editor_dialog +
test_pattern_editor_dialog (plotStyle test stays green untouched).
Gate + smoke: ComparisonPlot re-show unclobbered; Preferences remembers its page;
LayerStyle remembers its tab; stale ad-hoc keys visibly ignored.

### D3 — Remainder naming sweep (~33 dialogs, geometry-only)
setObjectName(<ClassName>) + name any user-facing splitter in every remaining dialog —
about, addbasemap, annotationstyle, chartproperties, climatology, colorramp,
comparisonpairs, crschange, curve, legendproperties, licenseagreement, newproject,
plugins, profileoptions, profilepathpicker, statusreport, stylemanager,
sublayerselection, userflags, userflagvalues, … INCLUDING the 7 dead dialogs (treated
live per user decision). meshgenerationdialog: minimal-diff — objectName + names on
existing splitters only, no restructuring. CommandPalette: objectName +
noLayoutPersistence property. The 2 transient swmmvis.cpp prompts (:1938, :2575) +
SymbolDetailsDialog stay unnamed (implicit opt-out; a11y ride-along only).
Gate: build + full suite + sampled smoke sweep.

### D4 — A11y mechanical sweep + shared checker
Bug fix: pollutanteditordialog.cpp:117 → I&&I. Mnemonics into the 444 addRow(QString,…)
literals (31 form dialogs = free implicit buddies; per-dialog uniqueness discipline;
simopts' 54 rows/29 group boxes done carefully) + 44 addTab mnemonics. Explicit setBuddy
in the 13 form-less dialogs (~55 labels: timeseries 14, addbasemap 8, linkcompound 6,
climatology 6, transect/meshgen(minimal)/curve/colorramp 5 each, …).
src/ui/widgets/labeledcontrols.cpp: setBuddy in the 5 ctors (:35,:56,:78,:96,:128) → 3
consumer files fixed free. The 7 QToolButton factory lambdas (hydrographgroupeditor:262,
patterneditordialog:359, timeserieseditordialog:1788, ruleseditordialog, layerstyledialog,
editors/rulebasedrendererpanel, editors/categorizedrendererpanel) gain
setAccessibleName(tip) → ~50 icon-only buttons. Button-box conversions in the 9
hand-rolled-row dialogs; Esc-close + initial-focus verification for box-less modeless
editors (no new boxes). Fixed-size relaxation at 8 sites (preferences:331,
statusreport:346-347, comparisonplot:173,179, simopts:247 nav width,
rulebasedrendererpanel:184, profilelayerpanel:124; KEEP commandpalette's 2). Ride-along
child panels get the same mnemonic pass. NEW tests/gui/dialog_a11y_checks.h (header-only,
QVERIFY2-based): assertDialogA11y composing assertLabelsHaveBuddies (form-layout label OR
buddy; "a11yDecorative" property opt-out), assertMnemonicsUnique (labels/buttons/tabs per
window), assertIconButtonsNamed (icon-only ⇒ accessibleName + tooltip),
assertPersistenceNaming (non-empty dialog objectName; all QSplitters named) — wired into
the 12 existing dialog tests IN THIS PHASE so the sweep lands guarded.
Gate + smoke: Alt-mnemonic walk (Preferences, SimOptions, one editor), VoiceOver spot
check.

### D5 — Styling uniformity
include/ui/theme/themehelpers.h += sectionHeadingStyle() (theme-live bold heading) +
errorFillStyle() (tokenized field-background error fill). Fix the 2 live defects:
simulationoptionsdialog.cpp:1783-1797 badStyle #ffc8c8 → errorFillStyle();
statusreportdialog.cpp:612,653 HTML spans → interpolate ThemeManager colors().error
.name(). 4 hint literals → hintStyle() (stylemanagerdialog:123, kindtreesymbologypanel:80,
editors/featurestyleeditor:100, panels/layerstylingdock:74). 9 pseudo-heading sites →
sectionHeadingStyle(). 59 raw QIcon(":/swmmvis/…") in dialogs → IconFactory::icon(alias)
(timeseries 13, profileplot 10, transect 10, comparisonplot 9, meshprofile 6,
rasterprofile 6, curve 5). meshgenerationdialog: in-place literal→helper swaps only
(minimal-diff), then RETIRE its lint-allowlist entry (attributetablepanel becomes the
sole entry).
Gate + smoke BOTH themes: dark-mode icon legibility; SimOptions invalid-event fill
legible on dark.

### D6 — Lint hardening + coverage completion
scripts/lint_chrome_colors.sh: add hex-in-string-with-color-context pattern (catches
variable-assigned styles AND rich-text color:#hex), setColor 3-arg/ColorGroup forms;
CHROME_PATHS += include, src/map, src/simulation, src/swmmvisprojectwindow.cpp — must
pass clean post-D5. NEW tests/gui/test_dialog_a11y_standalone.cpp: constructs the cheap
standalone dialogs (the 7 dead ones + plugins + licenseagreement), runs assertDialogA11y
+ one watcher save/restore round-trip on a representative (drop any ctor-heavy offender
from the target and note it — it stays covered by the sweeps). Extend the remaining
existing dialog tests (chartproperties, userflags, userflagvalues, climatology,
crschange, about) with the checker.
Gate: full `-L gui` green (~112+ targets), hardened lint green, final smoke: 5-dialog
persistence sweep + mnemonic walk in both themes.

### D-track risk register
Settings bleed between tests (documented test_selectionops.cpp:341) → QTemporaryDir+
IniFormat member idiom mandatory in every new persistence test · mnemonic collisions
across 444 rows → uniqueness assert lands WITH the D4 sweep in 12 tests, widened D6 ·
toggle/ restore fires side effects (replots) → restore runs on Show after full
construction; per-dialog remedy = un-name the action · meshgen collides with user's
uncommitted edits → additive names + in-place literal swaps only, trivially rebasable ·
dead-dialog tests pull heavy link deps → verify constructibility first, drop offenders
from the test target only · R1 shares the app-filter seam → watcher is a SEPARATE
installed filter, zero eventFilter-body edits · header/tab blobs vs changed models →
restoreState rejects mismatched blobs, ints bounds-checked · multi-instance editors share
a class key → last-close-wins, documented · offscreen clamp rewrites rects → test rects
inside availableGeometry; exact-equality asserts offscreen only · singleton re-show
clobber → layoutRestoredOnce instance guard.

### D-track out of scope
Data-content persistence · theme persistence (done, Appearance mode) · main-window/
panel/dock state (AttributeTablePanel + StatsSummaryPanel schemes left functioning
as-is) · data-symbology colors (colorramp/categoricalpalette/seriesstyle) · blanket
setTabOrder · .ui conversion · geometry persistence for the 2 transient swmmvis.cpp
prompts + SymbolDetailsDialog (a11y ride-along only) · Windows/Linux visual QA beyond
build+ctest.

## Iteration-1 record (EXECUTED — kept for reference)

## Context

The current swmmvis UI works well functionally, but lacks a unified, consistent theme and
character. The user wants:

1. **Toolbar/ribbon-centric redesign** — decided: **tabbed compact toolbar** (flat single-row
   tab strip swapping the visible tool row: Home / Model / Mesh-2D / Analysis / Results / View
   style), NOT a full Office ribbon.
2. **Full native macOS menubar retained** — every capability mirrored in menus (macOS
   conventions, Help search, VoiceOver discovery); the tabbed toolbar is the primary visual
   surface.
3. **Unified theme system** — light + dark from one design-token system, following system
   appearance with manual override.
4. **All capabilities keyboard-accessible** — curated default shortcuts + searchable command
   palette (Cmd+Shift+P) reaching every registered action + shortcut rebinding editor with
   conflict detection.
5. **Modern accessibility standards** — accessible names/descriptions, focus/tab order,
   contrast, not-color-alone semantics, text scaling tolerance.

Per CLAUDE.md: MVC architecture, modern UI configurations, follow vetted workplans.

## Decisions (confirmed with user 2026-07-31)

| Topic | Decision |
|---|---|
| Chrome style | Tabbed compact toolbar (single-row tab strip + one tool row) |
| Menu bar | Full native menubar kept, mirrors all toolbar actions |
| Theme | Light + dark from shared tokens, follow-system + manual override |
| Keyboard | Default shortcuts + command palette + rebinding editor |

## Exploration findings

### A. Main window / chrome architecture (report 1 of 3 — COMPLETE)

- `SWMMVis : QMainWindow` — src/swmmvis.cpp (7,753 LOC god object). All menu/toolbar/dock/
  statusbar wiring inline. Single Designer form forms/swmmvis.ui (2,106 lines, 76 QActions).
- Central: `QMdiArea` (TabbedView) + welcome tab; each project = `SWMMVisProjectWindow`
  (QMdiSubWindow) wrapping a `MapCanvas`. `onActiveSubWindowChanged` rebinds all shared
  docks/toolbars to focused project — the single hub.
- **Actions: ~105 main-window-level** (76 in .ui + ~29 programmatic); 310 `new QAction`/
  `addAction` sites repo-wide (incl. context menus). **No action registry** — string-keyed
  `findChild<QAction*>` lookups; `SWMMVisProjectWindow::toolActionKeys()` maps 25 map tools →
  action objectNames (must preserve or rewrite).
- **Menus (7)**: File, Edit (**EMPTY**), View (1 item), Data (13 programmatic), Tools, Window,
  Help. Badly underpopulated vs toolbars. `docs/manual/02_interface.md` documents intended
  richer IA (File/Edit/View/Project/Report/Tools/Window/Help) never built.
- **Toolbars (6, stacked 4+ rows)**: Main (28 actions), Editing (27), Animation (8 + 6 embedded
  widgets: scrubber/slider/datetime/speed), Analysis (8 + 5 embedded widgets: layer combos +
  live-render check), TerrainToolbar, MeshEditingToolbar (subclasses in ui/toolbars/).
- **Docks (8 runtime)**: Layers (LayerTreePanel), Object Browser, Properties (QPropertyModel
  grid), Attribute Table, Legend (hidden default), Layer Styling (hidden default), Simulation
  Status, Message Logs. Dead: AnalysisToolBox placeholder, OverviewMapPanel (never
  instantiated).
- **Status bar: ~20 permanent widgets** (engine combo, flow units, progress, offsets, coords,
  scale, CRS…) src/swmmvis.cpp:1565-1745.
- **Tech**: QWidgets everywhere; QtQuick only inside MapCanvas via offscreen QQuickWidget QSG
  (1 QML file). Style = Fusion + 12-line style.qss (QToolTip only). No ribbon/3rd-party UI lib.
- **Dialogs**: 53 QDialog subclasses; find-or-create block copy-pasted 8×; app-wide eventFilter
  (swmmvisapplication.cpp:155-202) handles macOS modeless raise/detach; dialoglayoutpersistence
  helpers for geometry/splitters (10 dialogs).
- **Build**: single monolithic CMake target, hand-maintained flat source list (no globs);
  resources/swmmvis.qrc 117 entries; 122 SVGs (66 chrome icon aliases; typo alias
  `TraveTimeUpstream`).
- **Persistence**: QSettings `SWMMVis::MainWindow` saveState/restoreState — renaming
  toolbars/docks invalidates saved layouts.
- Sibling repo SWMM-GUI = legacy EPA Delphi GUI (feature/IA reference only).
- ~110 GUI test files in tests/gui/, several assert toolbar/action behavior.

### B. Theming/styling state (report 2 of 3 — COMPLETE)

- **No theme system.** App-level styling = one line: `setStyle("Fusion")`
  (swmmvisapplication.cpp:83, re-forced swmmvis.cpp:551). resources/styles/style.qss (12
  lines, QToolTip only) is ORPHANED dead code — in no qrc, never loaded.
- **No dark mode**: zero hits for colorScheme/styleHints/NSAppearance; no Appearance page in
  Preferences (11 categories, none theme); no appearance QSettings key. Fusion follows system
  palette on Qt 6.5+ but ~60 hardcoded-light sites would break.
- **63 inline setStyleSheet() across 22 files.** Same semantic, divergent values:
  error-red = #c0392b / #c62828 / #C62828; hint-gray = "color: gray" (13 sites) / #555 (3) /
  palette(mid) (4 — the only theme-aware pattern, the one to standardize on). Hardcoded
  light banner backgrounds (ruleseditordialog #E8F5E9/#FFEBEE etc.). Icon-in-stylesheet
  toggles swmmvis.cpp:1637,1688.
- **639 QColor( constructions.** Legit data ramps (colorramp.cpp 137, categoricalpalette 67)
  vs chrome offenders: profileplotwidget.cpp:60-69 (kAxisColor/kGridColor/kBackground...),
  meshprofileplotwidget.cpp:41-43, seriesstyle kCycle[12], legendoverlaystyle.
- **4 setPalette writes force light**: stylepreviewswatch.cpp:23-25, profileplotwidget.cpp:
  348-350, meshprofileplotwidget.cpp:83-85, attributetablepanel.cpp:518-525 (HighlightedText
  black).
- **Icons: 120 SVGs + 2 PNGs, flat dir.** All monochrome but hardcode stroke #777777 —
  `currentColor` in 0 of 120; no tint helper/factory; 130 literal QIcon() calls; 0
  QIcon::fromTheme. viewBox: 90× "0 0 24 24", 30 files at 21 other sizes; stroke-width house
  style 2 but 9 variants. **Broken ref `:/swmmvis/Edit`** (curveeditordialog.cpp:451,
  patterneditordialog.cpp:441 render empty). 6 orphaned SVGs; alias typos TraveTimeUpstream,
  AddDelimetered, thermomether.svg. 5 different hardcoded icon sizes (16/18/20/64×18/112×84).
- **Fonts mostly OK**: QFontDatabase::systemFont is the pattern; one bad literal — splash
  `QFont("Segoe UI Semibold", 20, 2)` (Windows-only family on macOS-primary app,
  swmmvissplashscreen.cpp:25).
- **"Unified styling S1-S3" ≠ app chrome** — it's map-layer symbology (classificationscheme/
  classificationeditor shared across 5 style dialogs; Slice US.* breadcrumbs ~40 sites).
  Deferred S4/S5 = LayerSymbologyPanel generalization + styleChanged(scopeId) MVC channel —
  separate scope, don't conflate.
- **Workplans**: none cover app-chrome/theme/ribbon (workplans/ = all 2D solver). Backup dir
  openswmm.gui-workplans-backup/ has GUI_IMPLEMENTATION_PLAN.md (1.4MB master, 27 src refs) +
  styling plans. docs/manual/02_interface.md = best spec of current+intended chrome. So per
  CLAUDE.md §5.0 a NEW vetted workplan is required (this plan).
- QSettings: 176 refs; main-window keys `SWMMVis::MainWindow/{WindowState,Geometry,
  RecentFiles,...}`; dialoglayoutpersistence.cpp centralizes dialog geometry (some bypass).
  Org scope hydrocouple/calebbuahin.github.io.
- forms/swmmvis.ui contains no styleSheet properties — clean slate for theming.

### C. Shortcuts/accessibility audit (report 3 of 3 — COMPLETE)

**Shortcuts — 40 call sites total; main window has 7 working shortcuts (9%).**
- 13 of 76 .ui actions declare shortcuts; **6 are BROKEN**: bare words `Copy`, `Search`,
  `Execute`, `Select`, `Zoom In`, `Zoom Out` parse as hardware media keys (Qt::Key_Copy etc.),
  never fire on normal keyboards (Designer copy-paste fill: statusTip=whatsThis=shortcut same
  string). Working 7: Ctrl+N/O/S/Shift+S/P/,/Q — File/app-level only.
- No working Ctrl+C, Ctrl+F, zoom keys, or Run key. 63/76 actions shortcut-less (all editing,
  navigation, animation, analysis mouse-only).
- Dialog/panel shortcuts: 23 (mix of StandardKey ×14 — the good pattern — and hardcoded ×22);
  2 shortcuts wrapped in tr() (comparisonplotdialog.cpp:304,386 — translator can break);
  bare-letter F/E in curveeditordialog with default WindowShortcut context (keystroke-eating
  risk).
- **Empty Edit menu** — no Undo/Redo/Cut/Copy/Paste/Delete/Select All at app level;
  MapCanvas::undoStack() exists but no app-level undo/redo actions. View menu has 1 item.
- **No toggleViewAction() anywhere — 9 of 10 docks have no menu entry/shortcut**; closed docks
  unreachable by keyboard.
- ~50 context-menu-only commands (layertreepanel 40 actions, objectbrowserpanel 20,
  legendoverlay 9).
- No shortcut editor, no QKeySequenceEdit, no central registration, nothing persisted;
  PreferencesDialog (11 categories) has no Keyboard section. QSettings namespace to extend:
  `SWMMVis/...` (preferencesmanager.h/.cpp owns it).
- No Q_OS conditionals in shortcut code; relies on Qt::CTRL→Cmd mapping (correct); .ui uses
  menuRole (20×, incl. PreferencesRole).

**Accessibility — zero API usage.**
- 0 setAccessibleName / setAccessibleDescription / QAccessible. 0 setTabOrder / <tabstops>.
  0 setBuddy — form fields unlabeled to AT everywhere. 8 setFocusPolicy calls total.
- setToolTip 346 / setStatusTip 15 / setWhatsThis 0 in code (.ui: 54/42/35, whatsThis mostly
  placeholder dupes).
- Mnemonics: .ui menus/actions mostly have `&`; only 16 `&` strings in all of src/ (Tools/Help
  menu titles missing them; dialogs/panels essentially none; no setBuddy so label mnemonics
  inert).
- **MapCanvas = GPU/QSG pixels, 100% invisible to screen readers** (mapcanvas + 3 QSG renderers
  = 7,696 lines; 11 render sublayers equally opaque). 12 paintEvent widgets + 22
  QGraphicsItem/delegate paints; LegendOverlay (743 lines) has no focus policy/keys/tooltips,
  9 right-click-only actions.
- Map tools handle only Escape/Enter/Delete; no arrow-key panning or feature traversal.
  Good keyboard-nav exemplars to copy: rangeslider.cpp:178, cursorwindowslider.cpp:143,
  rulecodeeditor.cpp:116.
- **Color-only semantics**: simulation warnings amber-only; import preview
  create/update/skip/error by hue only; flow arrows speed-by-hue; all scalar results hue-only;
  no CVD-safe constraint in categoricalpalette/colorramp.
- Hardcoded colors: 149 QColor literals + 22 hex strings + 28 setStyleSheet color calls;
  Fusion style forced twice (swmmvisapplication.cpp:83, swmmvis.cpp:551) suppressing platform
  high-contrast.
- Text scaling: 10 setFixedSize-family + 86 setMinimum-family pixel calls + 11 font-size calls;
  splash hardcoded 600×400; DPR handled correctly in render path.
- **Tests: zero** shortcut or a11y coverage in 162 test files (no QTest::keyClick anywhere).

### Notable pre-existing bugs to fix during redesign
- 6 broken bare-word shortcuts in forms/swmmvis.ui (Copy/Search/Execute/Select/Zoom In/Out).
- Icon alias typo `TraveTimeUpstream` in swmmvis.qrc.
- tr()-wrapped shortcut strings in comparisonplotdialog.cpp:304,386.
- Bare-letter shortcuts F/E with WindowShortcut context in curveeditordialog.cpp:384,455.

## Implementation plan

**Program shape: 10 phases, each leaves the app buildable and strictly better.** Foundation
(P0-P1) → theme (P2-P4) → menu IA (P5) → chrome swap (P6) → keyboard (P7-P8) → a11y (P9).
Full menu mirroring lands BEFORE old toolbars dissolve so no capability is ever orphaned.
First execution step: save this plan as `workplans/UI_REDESIGN_WORKPLAN.md` (gitignored dir,
per convention) so it's the vetted preconfigured workplan per CLAUDE.md §5.0.

### Architecture decisions

**A1. Action catalog + registry (adopt-in-place, data-driven)** — follows the proven
`swmmvis_hydration_audit.h` data-header + contract-test idiom:
- `include/ui/actioncatalog.h`: `inline constexpr std::array<ActionCatalogEntry, N>` —
  fields: `id` ("file.new"), `objectName` ("actionNew"), `category`, `defaultShortcut`
  (portable string or "std:Copy" for StandardKey), `tab`, `menuPath`, `tags` bitmask
  (RequiresProject, RequiresEditSession, Checkable, Contextual2D). Headless-testable.
- `ActionRegistry` (include/ui/actionregistry.h, src/ui/actionregistry.cpp): QObject singleton
  (PreferencesManager pattern). `registerAction(id, QAction*, meta)`, `action(id)`,
  `allActions()`, `setEnabledByTag()`, `setUserShortcut()` (persists QSettings
  `SWMMVis::Shortcuts/<id>`, portable text, empty = cleared), `effectiveShortcut()`,
  `resetShortcut()`, `conflictingActionId()`; signals actionRegistered/shortcutChanged.
- **Adopts EXISTING QAction objects** (ui->actionXxx) — never re-creates. So all
  `ui->actionXxx` access, `toolActionKeys()` objectName sync, and saved state keep working.
- Registration in new TU `src/swmmvisactions.cpp` (`SWMMVis::registerActions()`, called after
  setupUi at swmmvis.cpp:286) + at creation sites for programmatic actions (Data menu :3301,
  terrain profile, mesh toolbar, Window menu :5764).
- `applyEditSessionToActions`/`applyProjectOpenToActions` (swmmvis.cpp:738,752) become
  `setEnabledByTag()` one-liners; dangling names simply aren't cataloged.
- ~50 context-menu-only commands (layer tree/object browser/legend) NOT registered in v1
  (transient lifetime) — documented exclusion, future follow-up.
- ≈117 registered ids; the 7 duplicate data-strip actions are consolidated onto the 13
  Data-menu QActions.

**A2. Tabbed compact toolbar = tab-strip row + visibility-swapped QToolBar rows.** No
QStackedWidget, no re-hosting:
- Row 1: fixed `QToolBar` "toolBarTabStrip" hosting QTabBar (documentMode, drawBase false)
  + spacer + command-palette corner button; then `addToolBarBreak(TopToolBarArea)`.
- Row 2: per-tab QToolBars (setMovable false); activating a tab shows its toolbar(s), hides
  others; two bars can share a tab side-by-side. QToolBar's built-in extension chevron =
  overflow for free; saveState keeps working (all rows have objectNames).
- The 4 widget-heavy toolbars (Animation, Analysis, Terrain, MeshEditing) are adopted
  WHOLESALE — their 11 embedded widgets and insertWidget anchors never move.
- `CompactToolbarController : QObject` (include/ui/toolbars/compacttoolbarcontroller.h +
  .cpp): addTab(id, title, {toolbars}, contextual), setTabVisible (QTabBar::setTabVisible),
  last-tab persistence `SWMMVis::MainWindow/CompactToolbarTab`.
- toolBarMain + toolBarEdit deleted from .ui (action definitions stay); actions re-laid onto
  code-built `toolBarHome` + `toolBarModel`. Animation→Results tab, Analysis→Analysis tab
  (stay in .ui; only actionShowMassBalance moves animation→analysis bar).
- Contextual Mesh 2D tab: hidden until active project has a 2D mesh (wired in
  onActiveSubWindowChanged, the existing rebind hub); visibility only, no auto-switch.
- macOS: `unifiedTitleAndToolBarOnMac` → **false** (.ui:28); override
  `SWMMVis::createPopupMenu()` to suppress toolbar-visibility context menu.
- State: `saveState(2)`/`restoreState(state, 2)` version bump (swmmvis.cpp:3650,3664) —
  stale blobs fail gracefully to default layout.

**A3. Tab set** (every action also menu-reachable; `[new]` = created by this program):
| Tab | Groups → contents |
|---|---|
| Home | File: New/Open/Save · History: undo/redo `[new]` · Navigate: Pan/ZoomIn/Out/Extent/ToSelection · Select: Select/ByPolygon/Invert/Upstream/Downstream/Measure/Search/Copy · Add Data: SWMM output/Vector/Raster/WMS/Delimited(/Basemap hidden) · Run: Execute/Pause/Cancel |
| Model | Session: EditExisting · Nodes: Junction/Outfall/Divider/Storage · Links: Pipe/Pump/Orifice/Weir/Outlet · Hydrology: Subcatchment/RainGauge/Temp/Wind/Snow/Evap/Solar · Annotate: Text · Data objects: 7 of 13 Data-menu actions (same QActions) · Project: Options/UserFlags/SetCRS/ImportFeatureLayer · 2nd bar: TerrainToolbar |
| Mesh 2D (contextual) | GenerateMesh + MeshEditingToolbar as-is |
| Analysis | Active results: 1D/2D combos + live-render (existing widgets) · Reports: Summarize/Report/TabularView · Plots: TimeSeries/Profile · Network stats: FlowBalance↑↓/TravelTime↑↓/MassBalance |
| Results | Transport: SkipBack/Play/Pause/Stop/SkipForward · Scrub: slider/window/datetime/speed widgets · Display: ShowLegend/SetStyle |
| View | Panels: 9 dock toggleViewActions `[new]` · Styling: StyleManager/LayerStylingDock · ShowWelcome |
| Menu-only | SaveAs, ExportMap, Print, Exit, ClearRecent, Preferences, Plugins, Help, About, 6 remaining Data actions, window.*, appearance.{system,light,dark} `[new]`, commandPalette `[new]`, keyboardShortcuts `[new]` |

**A4. Menu IA (full mirror, native menubar)** — reorganized in forms/swmmvis.ui + programmatic
(src/swmmvisactions.cpp); supersedes empty-Edit/1-item-View; informed by
docs/manual/02_interface.md:
File (New/Open/Recent/Save/SaveAs/—/Import ▸ 6 items/—/ExportMap/Print/—/Preferences
[PreferencesRole]/Exit) · Edit (Undo/Redo/—/Copy/—/Select group/—/Find/—/EditExisting) ·
View (zooms/Pan/Measure/—/Panels ▸ 8 dock toggles/—/StyleManager/LayerStyling/Legend/—/
Appearance ▸ System-Light-Dark/—/Command Palette/ShowWelcome) · Model (Add Node ▸/Add Link ▸/
Subcatchment/Climate ▸/Text/—/Add Data Object ▸ 13/—/SetCRS/SimOptions/UserFlags/—/
GenerateMesh/Mesh ▸/TerrainProfile) · Analysis (Run/Pause/Stop/—/Summarize/Report/Tabular/—/
plots/—/network stats) · Results (playback/—/SetStyle) · Window (unchanged + registered) ·
Help (Help/Keyboard Shortcuts…/—/Welcome/About). Mnemonics added throughout (fixes
Tools/Help gaps).

**A5. Theme system — C++ tokens (no JSON), ThemeManager palette + minimal QSS overlay:**
- `include/ui/theme/themetokens.h`: `struct ThemeColors` semantic roles (accent, error,
  warning, success, hint, surface tiers, border, focusRing, selection, banner bg/fg trios,
  plot* chrome, canvasSelectionHighlight) + `lightColors()`/`darkColors()`; WCAG AA values
  (text ≥4.5:1, glyphs ≥3:1) enforced by unit test.
- `ThemeManager` (include/ui/theme/thememanager.h, src/ui/theme/thememanager.cpp): singleton
  created in SWMMVisApplication ctor after setStyle(Fusion) (swmmvisapplication.cpp:83).
  Mode {System, Light, Dark} persisted via PreferencesManager `%1/Appearance/Mode`;
  System tracks QStyleHints::colorScheme (Qt 6.11 confirmed); apply() = full QPalette (incl.
  Disabled group) + SMALL generated QSS overlay (tab strip, :focus outline, toolbar padding
  only); signal themeChanged().
- Consumption: prefer palette roles (hint → PlaceholderText/mid, matching the 4 existing
  palette(mid) sites); helpers `theme::applyHint/applyError/applyBanner` reconnecting on
  themeChanged. 4 forced-light setPalette writes deleted/tokenized.
- Plot chrome constants (profileplotwidget.cpp:60-69, meshprofileplotwidget.cpp:41-43) →
  ThemeColors.plot*; QSG selection colors (swmm2dmeshqsgrenderer.cpp:381-383,
  swmm2dresultsqsgrenderer.cpp:523-524) → canvasSelectionHighlight token.
  **Data symbology (colorramp/categoricalpalette/seriesstyle) untouched — not chrome.**
- PreferencesDialog gains "Appearance" category + `openAtCategory()`.

**A6. Icon pipeline:**
- One-time checked-in `scripts/convert_icons_currentcolor.py`: #777777→currentColor across
  120 SVGs; viewBox normalized to 0 0 24 24 where losslessly scalable; results committed.
- `IconFactory` + `ThemedIconEngine : QIconEngine` (include/ui/theme/iconfactory.h,
  src/ui/theme/iconfactory.cpp): paint-time currentColor substitution per mode
  (Normal/Disabled/Selected/Active from ThemeColors), QSvgRenderer, pixmap cache per
  (alias,color,size,DPR), cleared on themeChanged.
- Applied in one sweep: registerActions() re-assigns every registered action's icon from
  catalog alias. 130 literal QIcon() sites elsewhere migrate opportunistically.
- Compat qrc aliases ADDED (old kept, 172 refs keep resolving): `Edit` (fixes broken
  :/swmmvis/Edit), `TravelTimeUpstream`, `AddDelimited`. Toolbar icon size unified 20×20.

**A7. Shortcut scheme** — catalog authoritative; StandardKey-first; Qt::CTRL→Cmd; avoids
macOS-reserved keys. Fix 6 broken .ui shortcuts: Copy→std:Copy, Search→std:Find,
Execute→Ctrl+R, Select→(none; Esc already returns to select), Zoom In/Out→std:ZoomIn/Out.
New defaults: sim.stop Ctrl+. · zoomExtent Ctrl+Shift+F · zoomToSelection Ctrl+Shift+J ·
measure Ctrl+Shift+M · editExisting Ctrl+E · commandPalette Ctrl+Shift+P · dock toggles
Ctrl+Alt+1…8 · plotTimeSeries Ctrl+T / plotProfile Ctrl+Shift+T · tabularView Ctrl+Shift+A ·
undo/redo std · help std:HelpContents · window.minimize Ctrl+M. Playback + add-object
actions deliberately unbound by default (rebindable; palette-reachable). Context: registry
actions = WindowShortcut on main window; dialog shortcuts stay dialog-scoped (fix the 2
bare-letter WindowShortcut risks in curveeditordialog in P9). Conflicts: exact dup in scope
= hard error (must clear first); macOS-reserved/StandardKey override = warning.

**A8. Command palette** — `CommandPalette` + `CommandPaletteModel`
(include/ui/widgets/commandpalette{,model}.h + src/...): frameless modeless QDialog, closes
on Esc/WindowDeactivate; QLineEdit + QListView + delegate (icon, title, category chip,
shortcut hint); pure unit-testable `fuzzyScore()` (prefix > word-boundary > subsequence);
Enter triggers/toggles; disabled shown grayed. Scope = all registered actions (incl. dock
toggles + dialog launchers). Launch: Ctrl+Shift+P, View menu, tab-strip corner button.

**A9. Shortcut editor** — "Keyboard" page in PreferencesDialog hosting
`ShortcutEditorWidget` (include/ui/widgets/shortcuteditorwidget.h + .cpp): filter + QTreeView
by category (Command/Shortcut/Default), QKeySequenceEdit + Assign/Clear/Reset/Reset All,
inline conflict label, hard-conflict blocked. Writes through ActionRegistry (applies live,
persists). No import/export (cut as speculative). help.keyboardShortcuts →
openAtCategory("Keyboard").

**A10. Accessibility scope:**
- Chrome pass: accessibleName/Description on ~20 status-bar widgets, 11 embedded toolbar
  widgets, terrain/mesh controls, tab strip, 8 docks, MDI area, palette.
- Buddy + mnemonic sweep top 15 dialogs (Preferences, SimulationOptions, TimeSeries, Curve,
  Pattern, Rules, ProfileOptions, ComparisonPlot, StatusReport, StatisticsDashboard,
  StyleManager, WMSConnection, ImportPreview, HydrographGroup, LicenseAgreement).
- Tab order: audit-and-fix-on-break, no blanket setTabOrder churn.
- Focus visibility: 2px focusRing-token outline via ThemeManager QSS for
  QToolButton/QPushButton/QTabBar::tab/QComboBox under Fusion, both schemes.
- Not-color-alone: importpreviewmodel (src/ui/dialogs/import/) gains Create/Update/Skip/Error
  status TEXT; simulationstatusmodel (src/simulation/) warning icon + "Warning:" prefix;
  flowarrowsublayer legend numeric speed-bin labels (+ width cue if contained).
- MapCanvas pragmatic scope: accessibleName/description + keyboard fallback (arrows pan 10%,
  Shift fine, +/- zoom) in keyPressEvent when tool doesn't consume. Attribute Table/Object
  Browser/Properties documented as the accessible per-feature route (full native QTableView/
  QTreeView semantics already). NO parallel AT scene for the GPU canvas — out of proportion.
- Text scaling: fix worst offenders only (preferencesdialog.cpp:62 fixed→minimum width +
  clip-at-1.3× findings); ~90 minimum-size calls tolerable, left. Splash left (decorative).
- High contrast: forced Fusion ignores Windows high-contrast — accepted, documented; AA
  tokens are the mitigation.

**A11. God object: surgical.** New code lives in new classes/TUs (ActionRegistry, catalog,
CompactToolbarController, ThemeManager, IconFactory, CommandPalette, ShortcutEditorWidget,
src/swmmvisactions.cpp). Status-bar wiring, dock init, MDI rebind hub, dialog launches stay
in place. No MainWindowChrome/MenuBuilder frameworks.

### Phases

Every phase: `cmake --build build -j 8` clean; `ctest --test-dir build -L gui
--output-on-failure` green (pre-existing failures excepted); app launches + opens a model.
Screenshots/artifacts → `test_artifacts/ui_redesign/<phase>/` (reviewable location per
CLAUDE.md §4.1). If offscreen tests SIGKILL: `codesign --force --sign -` the stale
install/Darwin libomp.dylib first (known gotcha).

**P0 — Guardrails + quick wins.** Fix 6 broken .ui shortcuts; add compat qrc aliases (Edit,
TravelTimeUpstream, AddDelimited); interim View→Panels submenu from 8 dock
toggleViewActions (ends "docks unreachable"); NEW test tests/gui/test_ui_form_audit.cpp —
instantiates generated Ui::SWMMVis standalone (VERIFIED feasible: no custom widgets
instantiated by the form; generated header includes only Qt headers) + asserts every action
has text+tooltip, no media-key shortcuts, no duplicate shortcuts, every dock has a menu
toggle. CMake: add_swmmvis_gui_test (existing helper, tests/gui/CMakeLists.txt:18) +
AUTOUIC. Verify: build, new test, Cmd+C/F/R fire, dock close/reopen.

**P1 — Action catalog + registry.** New: actioncatalog.h, actionregistry.h/.cpp,
swmmvisactions.cpp (+ declare registerActions in include/swmmvis.h). Edits: ctor call after
setupUi; applyEditSession/ProjectOpen → setEnabledByTag; register-at-creation (Data :3301,
terrain, mesh toolbar, Window :5764); undo/redo QActions created (disabled; wired P5).
CMake: 4 files, new "UI action registry" section near :603. Tests: test_action_catalog.cpp
(headless: unique ids/objectNames/shortcuts, all parse, category+menuPath present);
test_action_registry.cpp (adopt-in-place identity, override persist/restore, enable-by-tag,
conflict lookup, QTest::keyClick fires). No visual change.

**P2 — Theme core.** New: themetokens.h, thememanager.h/.cpp. Edits: swmmvisapplication.cpp:83
(instantiate + apply), preferencesmanager.{h,cpp} (appearanceMode), preferencesdialog.{h,cpp}
(Appearance page, openAtCategory), swmmvissplashscreen.cpp:25 (drop Segoe UI — drive-by).
Tests: test_theme_manager.cpp (modes, palette roles, themeChanged, offscreen System→Light
fallback); test_theme_contrast.cpp (WCAG luminance ratios both schemes). Manual: dark mode
flips chrome (some widgets still light until P4 — accepted transitional state); System
follows macOS appearance live.

**P3 — Icon pipeline.** New: scripts/convert_icons_currentcolor.py (run once, SVGs
committed), iconfactory.h/.cpp. Edits: swmmvisactions.cpp icon sweep; swmmvis.qrc orphan
fixes. Tests: test_icon_factory.cpp (non-null per catalog alias, light≠dark≠disabled,
cache stability); catalog test asserts tab entries resolve icons. Manual: icons legible in
both schemes; curve/pattern editor Edit buttons show icons.

**P4 — Chrome color migration.** Replace 63 inline stylesheets (error/hint → helpers or
palette roles; banners → applyBanner; swatch-CSS generators → one shared helper; checkbox
icon-URL styles swmmvis.cpp:1637,1688 → IconFactory); delete/tokenize 4 forced-light
setPalette writes; plot chrome + legendoverlaystyle + maptoolselect chrome + import tints →
tokens; QSG selection → token; drop px font-size (hydrographgroupeditor.cpp:438). Data
colors untouched. NEW lint: scripts/lint_chrome_colors.sh + allowlist, registered as ctest
(label gui). Verify: lint + full -L gui + dark/light screenshots with a model open.

**P5 — Menu IA.** forms/swmmvis.ui: Edit/View content, new Model/Analysis/Results menus,
mnemonics, actionShowMassBalance bar move. swmmvisactions.cpp: Data submenu under Model,
Import submenu, Appearance radios, palette/shortcuts entries (disabled until P7/P8).
swmmvis.cpp: undo/redo rebind to active canvas undoStack in onActiveSubWindowChanged
(canUndo/RedoChanged → enable). Tests: form audit walks menubar — every catalog menuPath
reachable, Edit non-empty, all titles mnemonic'd. Manual: menu walkthrough vs A4; undo/redo
after map edit; macOS app menu roles intact.

**P6 — Tabbed compact toolbar (the visible swap).** New: compacttoolbarcontroller.h/.cpp.
Edits: .ui (delete toolBarMain/toolBarEdit blocks, unifiedTitleAndToolBarOnMac false);
swmmvis.cpp initializeToolBars → controller + toolBarHome/toolBarModel from registry per A3,
adopt 4 surviving toolbars, createPopupMenu override, contextual Mesh-2D in
onActiveSubWindowChanged, saveState(2)/restoreState(...,2); swmmvisactions.cpp data strip
reuses Data actions. Tests: test_compact_toolbar.cpp (tab switch visibility, contextual
hide/show, last-tab persistence, every catalog tab entry on exactly one tab); form audit
(old toolbars gone, actions present). Manual (the big one): 6 tabs cycle; embedded widgets
work (scrubber, combos, BC editors); overflow chevron at narrow width; map-tool radio sync
(toolActionKeys untouched); saveState round-trip; Mesh 2D appears when mesh loads;
screenshots both schemes.

**P7 — Command palette.** New: commandpalette{,model}.h/.cpp (4 files). Edits: enable View
entry + app.commandPalette registration; lazy find-or-create launch; tab-strip corner
button. Tests: test_command_palette.cpp (fuzzy-score units; model filtering; offscreen
keyClicks type-filter-Enter triggers action; Esc closes; disabled not triggerable).

**P8 — Shortcut editor.** New: shortcuteditorwidget.h/.cpp. Edits: preferencesdialog
Keyboard category; help.keyboardShortcuts → openAtCategory("Keyboard"). Tests:
test_shortcut_editor.cpp (assign persists + updates live QAction; hard conflict blocked
naming other command; reserved warning; reset one/all). Manual: rebind, relaunch, survives.

**P9 — Accessibility hardening.** Edits: swmmvis.cpp status-bar/embedded-widget accessible
names; toolbar control names; 15-dialog buddy/mnemonic/tab-order sweep (+ fix 2
curveeditordialog shortcut contexts); import preview status text
(src/ui/dialogs/import/importpreviewmodel.cpp); simulation status warning icon+prefix
(src/simulation/simulationstatusmodel.cpp); flowarrowsublayer legend labels; mapcanvas
accessible name/desc + arrow-pan/zoom fallback; thememanager focus-ring QSS;
preferencesdialog.cpp:62 fixed→minimum. Tests: test_a11y_lint.cpp (action text+tooltip hard
floor, dock toggles, accessible-name inventory walk); compact-toolbar focus traversal;
canvas keyboard test (offscreen-instantiable — existing canvas tests prove it). Manual:
VoiceOver pass over toolbar/status bar/one dialog; Tab-walk two dialogs; larger-text sanity.

### Risk register

| Risk | Mitigation |
|---|---|
| saveState blob incompatible after toolbar changes | saveState(2) version bump; graceful default-layout fallback; dock objectNames never renamed |
| Hydration-contract test coupling (test_options_hydration_contract ↔ swmmvis_hydration_audit.h) | Status-bar widgets/objectNames untouched; run that test explicitly in P6/P9 |
| toolActionKeys objectName coupling breaks tool radio sync | Adopt-in-place registry; dedicated P6 manual check |
| .ui hand-edits corrupt form | Small reviewed XML diffs; form-audit test instantiates form in CI |
| macOS unified-toolbar oddities | unifiedTitleAndToolBarOnMac=false in P6; macOS-primary visual QA |
| Offscreen SIGKILL (stale libomp signature) | codesign --force --sign - pre-step, documented |
| Dark regressions in untouched dialogs P2→P4 | Accepted transitional state; P4 lint prevents backsliding; default mode stays System |
| Embedded-widget toolbars vs tab switching | Hidden/shown only, never reparented; anchors stay |
| Translation churn | Out of scope; one lupdate at program end |
| Stale QSettings shortcut overrides for removed ids | Registry ignores unknown ids (debug log) |

### Out of scope (explicit)
S4/S5 symbology unification (must not conflict — P4 touches chrome colors only, never
renderer/symbology data colors) · dialog content redesigns beyond a11y sweep · full AT
object model for GPU canvas · welcome-screen redesign (inherits palette) · translations ·
registering ~50 context-menu commands (follow-up) · shortcut import/export.

## Verification

Per-phase recipes above. Program-level:
1. Build: `cmake --build build -j 8` after every phase.
2. Tests: `ctest --test-dir build -L gui --output-on-failure` (offscreen; codesign gotcha
   noted). New tests per phase: form audit → catalog/registry → theme/contrast → icons →
   chrome-color lint → compact toolbar → palette → shortcut editor → a11y lint.
3. Manual smoke on macOS after P2/P4/P6/P9: launch app, open a model, run simulation,
   animate results, switch light/dark, keyboard-only session (palette + shortcuts + tab
   navigation), VoiceOver spot-check (P9).
4. Artifacts (screenshots light+dark per phase) → `test_artifacts/ui_redesign/<phase>/`
   (untracked, reviewable per CLAUDE.md §4.1).
5. Update CHANGELOG.md upon release commits (CLAUDE.md §5.2).

---

## EXECUTION RECORD (2026-08-01, autonomous run)

All phases R0–R6 + D1–D6 executed on `swmm6_gui` (no `ui-iter2` branch: a stale
`.git/HEAD.lock` blocked ref updates — remove with `rm .git/HEAD.lock` before
committing). Gates: R0 109/109 → R2 110/110 (test_ribbongroup, 15 cases) →
D1 111/111 (test_dialog_layout_persistence, 10 cases) → D4 112/112
(test_dialog_a11y_standalone) → FINAL 112/112 + hardened lint green.

Deviations / discoveries:
- QToolBar hid shrunken RibbonGroups forever: nested-layout size-hint caches
  don't invalidate through hidden ancestors (updateGeometry no-ops when
  hidden). Fix: RibbonGroup overrides sizeHint/minimumSizeHint from direct
  child measurement; RibbonCompactor re-runs the bar layout after mode
  changes. Widget-level test reproduces + guards it.
- Plot-Profile dropdown moved from initializeMapTools to
  initializeCompactToolbar (ribbon button must exist first).
- SimulationOptions/Preferences page restore required one reverse
  connect each (pages currentChanged → category list, qOverload<int>).
- Hardened lint's first catch: mapcanvas.cpp:2006 tooltip `#444` →
  palette PlaceholderText interpolation (fixed, not allowlisted).
- Standalone a11y test dropped WMS/WMTS (their layer headers drag
  GDAL/QtNetwork); those dialogs stay covered by the sweeps.

Deferred (documented, low-risk):
- Explicit setBuddy for ~50 hand-built labels in the form-less dialogs
  (LabeledControls' 5 internal buddies landed; QFormLayout rows are
  auto-buddied). The a11y checker is in place to drive this follow-up.
- Hand-rolled button-row → QDialogButtonBox conversions (9 dialogs;
  modeless editors keep their affordances deliberately).
- <b> pseudo-headings left as rich text (bold is theme-safe);
  sectionHeadingStyle() exists for new sites. RptSyntaxHighlighter's
  four report-syntax QColors left (report-content coloring).
- Mnemonics: 256 inserted (37 tabs + 219 rows); dense dialogs skip
  letters once exhausted rather than collide.

USER-SIDE CHECKLIST (pending):
1. R1 macOS stacking freeze-retest (right-click cell → plot dialog; input
   alive; two dialogs stack in open order; Cmd+Tab behavior).
2. Visual QA: six ~86px captioned-group tabs in light + dark; narrow-window
   stepping Full → Compact → Collapsed right-to-left; split-button faces
   follow the active tool (Esc → Select).
3. Dialog persistence session: move/resize + splitters + tabs on a few
   dialogs, relaunch, confirm layouts return.
4. rm .git/HEAD.lock, then commit (user-owned).
