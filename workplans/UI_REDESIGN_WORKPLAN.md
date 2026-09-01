# swmmvis GUI Redesign — Unified Theme, Tabbed Toolbar, Shortcuts, Accessibility

## Status: FINAL — ready for approval

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

## EXECUTION RECORD (2026-08-01): ALL PHASES P0-P9 COMPLETE — final gate 109/109 gui tests. See memory ui-redesign-program-progress for follow-ups.
