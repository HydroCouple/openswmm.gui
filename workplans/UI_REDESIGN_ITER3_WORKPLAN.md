# swmmvis GUI Redesign — Iteration 3: Revision Fixes (workplans/revision.md)

## Context

The user tested the iteration-2 ribbon/dialog redesign and filed `workplans/revision.md` with ~11 gaps, plus one more from review: truncated/codey ribbon labels. Exploration root-caused every one:

- **One dominant root cause** explains four complaints (Model tab "has no groups", bogus dropdowns on Edit Existing / Generate Mesh, "missing" Import Feature Layer, DTM hover-Z gone): the TerrainToolbar (~700px of min-width widgets) shares a single toolbar row with the Model ribbon bar, starving the solver → all six Model groups demote to caption-dropdowns, and the terrain combo (the ONLY setter of `mActiveTerrain`, which gates the hover-Z readout) is squeezed into the chevron.
- **Mesh Editing toolbar "missing"**: attached to the contextual Mesh 2D tab which only appears for `SWMM2DMeshLayer` (`SWMM2DResultsLayer` is a sibling class, not a subclass — swmmvisactions.cpp:349-358), and `finalize()` dropped its dedicated toolbar row. Its mesh hover-Z label is hidden with it.
- **Equal-width groups**: `RibbonGroup` has default Preferred policy, no max width, bars have no trailing spacer → QToolBarLayout hands leftover space equally to every group AND every button inside groups.
- **Labels truncated / codey**: ~35 actions surface raw programmer names on ribbon faces AND menus because the `.ui` text is codey (`actionTemperature` literal objectName, `AddJunction`, `FlowBalanceDownstream`, misspelled `Add Delimetered Data`); split-button family members mostly lack short labels; the Properties dock's runtime title ("Properties — layer (N features)") leaks into its ribbon face. Mechanism verified: QToolButton renders `action->iconText()`; there is NO elision anywhere and `\n` already renders as two lines (QToolButton sizeHint honors it) — the only blocker for ArcGIS-style wrapping is the 86px fixed row height (needs ~100 for icon 32 + 2 text lines + caption).
- **Analysis combo overlap**: `widthForMode` sums raw `sizeHint().width()` which excludes `setMinimumWidth(160)` on the combos (ribbongroup.cpp:216; swmmvis.cpp:1038,1049) → group under-reports its own hard minimum.
- **Giant slider knob**: `CursorWindowSlider` has vertical Preferred policy (swmmvis.cpp:1187) → stretches to ~63px in the row; thumb painted `height()-2` tall (cursorwindowslider.cpp:97).
- **Dialog squeeze**: `OpenSWMM::Ui::wrapInScrollArea` exists (include/ui/uiscrollhelpers.h:43) and SimulationOptions already funnels every page through it (simulationoptionsdialog.cpp:300-304); PreferencesDialog adds 13 raw pages with no scroll.
- **Dialog toolbar icons**: full inventory done (below) — ~12 actively wrong icons, ~29 icon-less buttons, 3 off-style SVGs, 2 dialogs bypassing IconFactory theming.

## User decisions (fixed)
1. **Commit first, two separate commits**: (a) UI redesign, (b) user's mesh/DTM stream. `rm .git/HEAD.lock` first (verified stale 0-byte). No AI attribution in messages. NEVER push. workplans/ + test_artifacts/ stay untracked.
2. **Terrain/MeshEditing bars: own row, tab-scoped** — Terrain gets a full row under the ribbon on the Model tab; MeshEditing on the Mesh 2D tab; Mesh 2D tab also triggers on 2D results layers; mesh hover-Z mirrored to the status bar.
3. **Scroll: explicit per-dialog sweep** (no automatic watcher wrapping).
4. **Labels: ArcGIS-style two-line wrapping + human names** (no camelCase/code names anywhere user-visible).

---

## Phase 0 — Two commits

Remove stale lock: `rm .git/HEAD.lock`.

**Commit (a) — UI redesign iteration 2** (first): everything EXCEPT the mesh-stream files below. Includes forms/swmmvis.ui, resources/*, all new src/ui/** + include/ui/**, src/swmmvisactions.cpp, src/swmmvis.cpp, src/map/mapcanvas.cpp (verified a11y/theming only), the ~70 dialog cpps, scripts/, root CMakeLists.txt, all new tests except test_dtmthinner_banded.cpp.

**Commit (b) — mesh/DTM/2D-results stream** (second):
- include/mesh/dtmthinner.h, src/mesh/dtmthinner.cpp, include/mesh/meshgenerator.h, src/mesh/meshgenerator.cpp, include/mesh/meshstagecache.h
- include/layers/swmm2dresultslayer.h, src/layers/swmm2dresultslayer.cpp + include/render/sublayers/contourbandsublayer.h, scalarfillsublayer.h (opacity 0.85→0.60 default flip) + tests/gui/test_2d_sublayers.cpp (matching QCOMPARE)
- tests/gui/test_dtmthinner_banded.cpp, tests/verification/dtmthinner_scaling_check.*
- src/ui/dialogs/meshgenerationdialog.cpp — mixed file, goes here (171 lines are the mesh pipeline; only ~3 lines are iteration-2 persistence naming — note in commit message)

**One hunk-split file**: tests/gui/CMakeLists.txt — `git add -p`, stage everything except the `test_dtmthinner_banded` registration hunk (~@@ -1396) into (a); remainder into (b).

Gate: `git status` clean except untracked dirs; build + `ctest -L gui` (112/112) still green.

## Phase 1 — RibbonGroup sizing: honest widths, left-packing, no single-tool dropdowns

Files: include/ui/toolbars/ribbongroup.h, src/ui/toolbars/ribbongroup.cpp, src/ui/toolbars/ribboncompactor.cpp, src/swmmvisactions.cpp, tests/gui/test_ribbongroup.cpp.

1. **widthForMode respects child minimums** (fixes combo overlap): ribbongroup.cpp:216 → `row += qMax(w->sizeHint().width(), w->minimumWidth());`
2. **Left-packing** (fixes equal-width inflation):
   - RibbonGroup ctor: `setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed)` — group can neither inflate nor squeeze; inner icon spacing stays exactly spacing(2).
   - `addWidget(w, stretch)`: when stretch > 0, flip group to `(Expanding, Fixed)` — only the Timeline slider group expands.
   - One trailing spacer per non-Expanding bar (Home, Model, Mesh2D, View, Analysis; NOT Animation) added in initializeCompactToolbar: zero-min `QWidget` objectName `ribbonBarSpacer`, Expanding horizontal — leftover collects at row end ("unused gap at end is fine").
   - RibbonCompactor::relayoutNow (ribboncompactor.cpp:77): subtract one layout `spacing` from available width when a `ribbonBarSpacer` child exists.
   - `minimumSizeHint` stays `== sizeHint` — width shrink remains solver-owned, preserving the hard-won hidden-chevron staleness fix.
3. **Single-action groups never collapse** (fixes Edit Existing / Generate Mesh dropdowns): clamp Collapsed→Compact in `setMode` and `widthForMode` when `mActions.size() <= 1`; `groupWidths().collapsible = mCollapsible && mActions.size() > 1` — solver's existing non-collapsible skip does the rest, zero solver changes. (Split-button families have >1 mActions and keep collapsing — their face is already a dropdown, correct.)

New tests (test_ribbongroup.cpp): `widthForModeRespectsChildMinimumWidth`, `groupSizePolicyFixedByDefaultExpandingForStretchWidget`, `singleActionGroupNeverCollapses`, `compactorSubtractsSpacerSpacing`.

Gate: build + ctest -R ribbongroup + full `-L gui`. Smoke: Home tab left-packed with right-side gap; Analysis combos never overlap at narrow widths.

## Phase 2 — Human labels + ArcGIS-style two-line wrapping

Files: forms/swmmvis.ui, src/swmmvis.cpp, src/swmmvisactions.cpp, include/ui/toolbars/ribbongroup.h, tests/gui/test_ribbongroup.cpp.

1. **Fix the source texts** (benefits menus too — they currently show codey names): rename every codey `.ui`/programmatic action text to proper human form with spaces + mnemonic + `…` where a dialog opens. Full offender list from exploration; highlights: `actionTemperature`/`actionSnow`/`actionEvaporation` (texts are literal objectNames, forms/swmmvis.ui:1364/1349/1379) → "Temperature…", "Snow…", "Evaporation…"; `AddJunction`→"Add Junction", likewise Outfall/Storage/Pipe/Pump/Orifice/Weir/Outlet/FlowDivider/Subcatchment (:1418-1553); `SolarRadiation`→"Solar Radiation…"; `FlowBalanceDownstream` etc. (:1613-1658) → "Flow Balance Downstream" family; `SummarizeResults`→"Summarize Results"; `ShowLegend`→"Show Legend"; `Add Delimetered Data`→"Add Delimited Data" (misspelling, :1756); `Plot Timeseries`→"Plot Time Series" (:1310); strip the trailing space in `Add Vector Data ` (:1786).
2. **Ribbon face labels**: extend the `kShortLabels` iconText table (src/swmmvisactions.cpp:156-183) to cover ALL ribbon-visible actions, especially the split-button family members that currently show raw text (addNode/addLink/climate/addDataObject/import families). Use `\n` for two-line wrapping at word boundaries where a label exceeds ~10 chars: e.g. "Flow Balance\nDownstream", "Travel Time\nUpstream", "Import\nFeature Layer", "Simulation\nOptions", "Attribute\nTable", "Object\nBrowser", "Simulation\nStatus", "Select by\nPolygon", "Time\nSeries" style. Single-word labels stay one line. Verified: `\n` renders today (no elision code, QToolButton sizeHint honors it); widthForMode picks up the narrower wrapped width automatically.
3. **Row height**: `kRibbonRowHeight` 86 → 100 (include/ui/toolbars/ribbongroup.h:42) so icon 32 + two text lines + caption fit without clipping (budget verified: 86 leaves ~69px for the button; two lines need ~66 + frame margins).
4. **Pin dynamic dock labels**: the Properties dock toggleViewAction text mutates at runtime to "Properties — <layer> (N features)" (propertiespanel.cpp:356/814/1026) and leaks into the ribbon. Setting `iconText` in the kShortLabels sweep pins the face (iconText, once set, is immune to later setText) — cover all 7 dock toggles.
5. **Collapsed faces / captions**: keep `mCollapsedButton->setText(caption)` raw (tests match it); captions stay short one-liners ("Network Analysis" is the longest — acceptable).

Tests: no existing test asserts labels (verified — test_action_catalog has no text field, test_ui_form_audit only asserts non-empty). Add `wrappedLabelFitsRow` to test_ribbongroup.cpp: a button with "Two\nLine" iconText has sizeHint height ≤ row budget and narrower width than the unwrapped text.

Gate: build + full `-L gui`. Smoke: no camelCase anywhere on ribbon or in menus; long labels wrap to two centered lines like ArcGIS Pro; Properties face stays "Properties" after selecting a layer.

## Phase 3 — Tab-scoped second rows + Mesh 2D tab gating

Files: include/ui/toolbars/compacttoolbarcontroller.h/.cpp, src/swmmvisactions.cpp, src/swmmvis.cpp, tests/gui/test_compact_toolbar.cpp.

1. **`CompactToolbarController::setOwnRow(QToolBar*)`** + two-pass `finalize()`: pass 1 adds all shared-row bars (row 2), then one `addToolBarBreak`, then pass 2 adds own-row bars (row 3). Two-pass is essential — a mid-loop break would push later tabs' bars down. `applyVisibility()` unchanged (already show/hides all of a tab's bars); a row whose bars are all hidden consumes zero height (asserted in a test since the design leans on it).
2. Callers: `setOwnRow(mTerrainToolbar)` (Model tab), `setOwnRow(mMeshEditingToolbar)` (Mesh 2D tab), before finalize(). Remove the now-redundant legacy `insertToolBarBreak(mMeshEditingToolbar)` at swmmvis.cpp:857.
3. **Mesh 2D tab triggers on results too**: swmmvisactions.cpp:349-358 lambda also matches `qobject_cast<SWMM2DResultsLayer *>` (rename lambda `hasMesh2DContent`). Makes actionPick2DCells + actionMeshProfile reachable with results-only files.

Freeing the Model row makes all six Model groups render Full at normal widths (restores "keep actions separated unless resizing warrants collapsing") and makes the terrain combo reachable again → DTM hover-Z pipeline re-enabled.

New tests (test_compact_toolbar.cpp): `ownRowBarSitsBelowBreak` (toolBarBreak true), `hiddenOwnRowConsumesNoHeight`, `applyVisibilityStillTabScoped`.

Gate: build + targeted + full `-L gui`. Smoke: Model tab = six Full groups + Terrain row below; Mesh 2D tab appears with a results-only file; other tabs show no empty third row.

## Phase 4 — Mesh hover-Z to the status bar

Files: include/ui/toolbars/mesheditingtoolbar.h/.cpp, include/swmmvis.h, src/swmmvis.cpp.

1. `MeshEditingToolbar`: new signal `hoverElevationChanged(double z, bool finite)`, emitted from the existing `onHoverElevation` slot (mesheditingtoolbar.cpp:1009). In-toolbar label stays.
2. `SWMMVis`: factor the body of `onCursorPositionChanged` (swmmvis.cpp:7467) into `updateCoordinateReadout()`; new member `std::optional<double> mMeshHoverZ` + new slot `onMeshHoverElevation` that sets/clears it and refreshes the readout. Precedence: mesh Z when the probe hit (`"Z: %1 (mesh)"`), else terrain Z as today. Connect in initializeMeshEditingToolBar.
3. Widen `mLineEditCoordinates` minimum 120 → 220 (swmmvis.cpp:1728) so "X, Y  Z: n unit" is never clipped.

Gate: build + full suite. Smoke: mesh Z visible in status bar with Mesh 2D tab inactive; DTM Z off-mesh.

## Phase 5 — Animation slider geometry

Files: src/ui/widgets/cursorwindowslider.cpp, src/swmmvis.cpp.

1. swmmvis.cpp:1187 → vertical policy `Fixed` (resolves to the 24px minimumHeight, centered in the row — no more tall slab).
2. Thumb clamp at cursorwindowslider.cpp:97: `const int thumbH = qMin(height() - 2, 18); QRect t(xCur - kThumbHalfW, (height() - thumbH) / 2, 2 * kThumbHalfW, thumbH);` — short fixed-height knob.
3. Minimum span 240 → 320 (swmmvis.cpp:1184); Phase 1's widthForMode fix makes the Timeline group report it honestly; existing stretch-1 keeps fill-on-grow.

Gate: build + full suite. Smoke: small pill knob; slider fills leftover width; narrow window degrades without overlap.

## Phase 6 — Explicit scroll sweep (Preferences first, then ~13 dialogs)

Pattern: `OpenSWMM::Ui::wrapInScrollArea` (include/ui/uiscrollhelpers.h:43), funnel idiom from simulationoptionsdialog.cpp:300-304. Use `kSpinMinWidthPx`/`kComboMinWidthPx` (uiscrollhelpers.h:29-30) where page widgets lack minimums so horizontal scrolling engages instead of zero-width clipping.

1. **PreferencesDialog** (src/ui/dialogs/preferencesdialog.cpp): add `addCategory(title, page)` doing addItem + `m_pages->addWidget(wrapInScrollArea(page, m_pages))`; replace the 13 addItem (:66-78) + 13 raw addWidget (:88-100) pairs. Lower `setMinimumSize(560,420)` → `(480,320)`.
2. **Sweep** (wrap each tab/stack page at insertion; lower minimum sizes): layerstyledialog.cpp (finish the partial job — :491, :541, :629, :786, :868 raw; :644 already wrapped), climatologydialog, hydrographgroupeditor, lidcontroleditordialog, linkcompoundeditdialog, nodecompoundeditdialog, subcatchcompoundeditdialog, profileoptionsdialog, statisticsdashboarddialog, sublayerstyledialog, addbasemapdialog, kindtreesymbologypanel, rulesymbologytab.
3. **Excluded**: splitter-based editors (timeserieseditordialog etc.) — scroll areas would fight the splitters. DialogLayoutWatcher needs no change (restored small geometries now show scrollbars — desired).

Gate: build + full suite (watch test_dialog_a11y_standalone, test_dialog_layout_persistence, test_climatologydialog — findChild is recursive so viewport interposition should be inert; run the a11y test per dialog batch). Smoke: Preferences shrunk to minimum scrolls on both axes, all 13 categories.

## Phase 7 — Dialog toolbar icon pass

House style: 24×24 viewBox, stroke #777777, width 2, round caps/joins, fill none. IconFactory recolors ONLY the grays #777777/#626262/#989898/#A4A0A0/#9E9E9E — new SVGs must use #777777.

**7a — Author SVGs + qrc aliases (~41 new + 3 redraws)** in resources/images/ + resources/swmmvis.qrc:
- Tier 1 (replaces actively wrong icons, 12): Rotate, Scale, Snap, Paste, AddRow, DeleteRow, InsertVertex, DeleteVertex, TimeCursor, SystemSeries, PickFromMap, ResultSources
- Tier 2 (fills icon-less toolbar holes, 14): ChartProperties, ExportImage, PlotStyle, LockX, LockY, PanelLeft, PanelBottomSlider, PanelStats, Plot1v1, Configure1v1, ChartsOnly, CellBoundaries, ProfileMarker, Rename
- Tier 3 (shared vocabulary for icon-less button rows, 15): Add, Delete (trash), MoveUp, MoveDown, Duplicate, Refresh, Browse, ExportCsv, AutoStretch, Bold, Italic, FindPrev, FindNext, SelectAll, SelectNone
- Redraws of off-style existing files: style.svg (22×21 filled → 24-grid stroked), clear.svg (18×20 archive tray → proper glyph; delete concept splits to new `Delete`), divider.svg (drop `<filter>` drop-shadow/clipPath)

**7b — Wire into dialogs** (highlights; full inventory in exploration record):
- timeserieseditordialog.cpp: Rotate (was Ruler :398), Scale (was Style paintbrush :403), Snap (was ToggleOn :416), AddRow (was New :437), DeleteRow (was Clear :443), Paste (was AddDelimetered :455), wire existing Undo/Redo aliases to the QUndoStack actions (:424-427 — free win)
- transecteditordialog.cpp: InsertVertex/DeleteVertex (:388/:395), ChartProperties (was Settings :412), ExportImage (was SaveAs :417)
- comparisonplotdialog.cpp: TimeCursor (was Divider :308), SystemSeries (was Globe :320), PickFromMap (was Node :328), ExportImage (:298), and the 6 bare-text view toggles (:346-396) get PanelLeft/PanelBottomSlider/PanelStats/Plot1v1/Configure1v1/ChartsOnly
- profileplotdialog.cpp: ResultSources (was Chartpie :239), ExportImage (:246), ChartProperties (:249); meshprofile/rasterprofile: CellBoundaries, ProfileMarker (shared), ChartProperties
- curveeditordialog.cpp: LockX/LockY (:488/:497); patterneditordialog + hydrographgroupeditor: PlotStyle, Rename, plus **migrate raw `QIcon(":/swmmvis/…")` → IconFactory::icon** (patterneditordialog.cpp:366, hydrographgroupeditor.cpp:242,:512 — currently unthemed in dark mode)
- Icon-less rows (rulebasedrendererpanel, categorizedrendererpanel, ruleseditordialog, rulesymbologytab `+↑↓` glyphs, statusreportdialog `◀▶`, labelstab B/I, colorramp editors, stylemanagerdialog, sublayerselectiondialog, statisticsdashboard/customreport/tabularresults ExportCsv, editor-family +New/−Delete pairs): apply Tier-3 vocabulary
- Consistency: collapse the `Edit`/`SelectEdit` duplicate alias to one; unify dialog toolbar icon sizes (18px vs 20px drift → 20px)

Gate: build + `ctest -R icon_factory` + full `-L gui`; visual pass of each dialog toolbar in light AND dark themes.

---

## Verification (program level)

- Every phase: clean build + full `ctest --test-dir build -L gui --output-on-failure` (baseline 112/112, growing with new cases) + launch smoke. Offscreen SIGKILL gotcha: `codesign --force --sign -` stale install/Darwin libomp.dylib.
- Final manual QA: (1) Model tab — six Full captioned groups, Terrain row below, no dropdown on Edit Existing / Generate Mesh, Feature Layer visible; (2) all labels human-readable, long ones wrapped to two lines, no camelCase on ribbon or in menus; (3) load 2D-results-only file — Mesh 2D tab appears, mesh editing toolbar on its own row; (4) hover DTM (after picking terrain in combo) and mesh — Z in status bar regardless of active tab; (5) narrow-window stepping still Full→Compact→Collapsed trailing-first, left-packed at all widths; (6) Results tab slider knob small, slider spans the gap; (7) Preferences + swept dialogs scroll instead of squeeze; (8) dialog toolbars iconified, legible in both themes.
- Commits: user reviews/pushes; CHANGELOG.md update deferred to release per CLAUDE.md §5.2.

## Risks
- Fixed size policy on groups changes QToolBarLayout math everywhere at once — fallback if the chevron misbehaves: keep Preferred + refresh `setMaximumWidth(widthForMode(mMode))` in applyMode.
- Row height 86→100 changes every ribbon screenshot/expectation; test_ribbongroup uses the constant symbolically so no hardcoded-86 breakage expected (verify by grep).
- Renamed action texts flow into menus AND saved shortcut/ persistence keyed by objectName (objectNames unchanged — safe); collapsed-face tests match raw captions (kept raw).
- Two-pass finalize reorders bars within row 2; iteration-2 saveState blobs restore harmlessly (bars immovable, self-heals on finalize).
- Scroll wrapping interposes viewports — findChild is recursive, but run dialog tests per batch.

---

# EXECUTION RECORD (2026-08-01/02, autonomous run)

## Gates
- Phase 0: two commits landed — 34014fa `feat(ui): ribbon redesign iteration 2` (166 files) + b3eb7a4 `perf(mesh): banded DTM thinning…` (15 files, includes the mixed meshgenerationdialog.cpp + the tests/gui/CMakeLists.txt hunk split via git apply -R --cached). Stale .git/HEAD.lock removed. NOT PUSHED (user pushes).
- Phase 1 (ribbon sizing): 112/112. New tests: widthForModeRespectsChildMinimumWidth, groupSizePolicyFixedUntilStretchWidget, singleActionGroupNeverCollapses, compactorLeftPacksWithTrailingSpacer.
- Phase 2 (labels): 112/112. 23 codey `.ui` action texts renamed (incl. "Add Delimetered"→"Add Delimited" misspelling, trailing-space strip); kShortLabels grown 22→~70 entries with `\n` two-line wrapping; kRibbonRowHeight 86→100; dock faces pinned via iconText (Properties dynamic-title leak fixed). New test wrappedLabelFitsRow.
- Phase 3 (own rows + mesh2d gating): 112/112. CompactToolbarController::setOwnRow + two-pass finalize; legacy insertToolBarBreak removed (swmmvis.cpp); mesh2d tab now also triggers on SWMM2DResultsLayer. New tests ownRowBarSitsBelowBreak (NB: toolBarBreak(home) is TRUE by design — strip→row-2 break), hiddenOwnRowConsumesNoHeight.
- Phase 4 (hover-Z): 112/112. MeshEditingToolbar::hoverElevationChanged → SWMMVis::onMeshHoverElevation → updateCoordinateReadout (mesh Z wins, "Z: n (mesh)"); coord widget min 120→220.
- Phase 5 (slider): 112/112. Vertical Fixed policy, thumb clamped min(height-2,18) centered, min width 240→320.
- Phase 6 (scroll sweep): 112/112. Preferences addCategory funnel (13 pages) + min 560×420→480×320; wrapped: layerstyle Information/Source/Rendering/Metadata tabs, climatology ×6, lidcontrol ×4, profileoptions ×2, addbasemap ×4, node/link/subcatch compound stacks (4+2+3).
  SKIPPED with reason (self-scrolling views; wrapping = nested scrollbars): hydrographgroupeditor (RTK QTableView tabs in splitter), statisticsdashboard (QTableView tabs), sublayerstyledialog (QTreeView tabs), kindtreesymbologypanel + rulesymbologytab (renderer panels/lists), layerstyle Symbology tab (panel navigation).
- Phase 7a: 41 new SVGs + 3 redraws (style/clear/divider — off-grid/filter defects) in resources/images, all #777777 stroke (IconFactory recolor set), 41 qrc aliases added; contact-sheet render verified.
- Phase 7b: wired — timeseries (Rotate/Scale/Snap/AddRow/DeleteRow/Paste + Undo/Redo aliases), transect (InsertVertex/DeleteVertex/ChartProperties/ExportImage), comparison (TimeCursor/SystemSeries/PickFromMap/ExportImage + 6 view-toggle icons), profile plot (ResultSources/ExportImage/ChartProperties), mesh/raster profile (CellBoundaries/ProfileMarker, forced-TextOnly removed), curve (LockX/LockY + Edit→SelectEdit alias collapse), pattern + hydrograph (raw QIcon→IconFactory theming migration, PlotStyle, Add/Delete/Rename, sizes 18→20), status report (FindPrev/FindNext), rulesymbologytab (+/↑/↓ glyphs→icons; test locators moved to tooltips), labelstab (Bold/Italic glyphs), auto-stretch pair, ExportCsv trio, rule-based + categorized renderer panels, style manager, sublayer selection, 7-editor family + curve/transect/pattern/rules/timeseries list panes ("+ New"/"− Delete" prefixes retired for icons).
  CMake: test_pattern_editor_dialog + test_rulesymbologytab gained iconfactory/thememanager sources + Qt6::Svg.
- ENGINE RELINK (user request mid-run): openswmm.engine rebuilt (build/darwin Release, incremental — pump-curve "*" fix etc.), `cmake --install` clean to install/Darwin, libomp.dylib re-codesigned (SIGKILL gotcha), GUI rebuilt against it.
- FINAL: 112/112 gui tests + hardened chrome-color lint green + 8s cocoa launch smoke OK. Iteration-3 changes are UNCOMMITTED on swmm6_gui (user commits/pushes).

## Deviations from plan
- Mnemonic additions during the .ui rename pass were deliberately skipped (collision risk across menus; existing mnemonics preserved). Names + ellipsis/spacing only.
- Climate family texts got no trailing "…" to match sibling "Wind".
- Scroll sweep skips listed above (nested-scrollbar avoidance beats blanket coverage).
- StyleManager "Export…" reuses the ExportCsv document-out glyph (generic-export reading).
- resources/about/components.json + licenses/openswmmcore.txt (engine re-branding edits found in tree) were folded into commit (a).

## USER-SIDE CHECKLIST (iteration 3)
1. Model tab: six Full captioned groups left-packed, gap at right; Terrain bar on its own row below; Edit Existing / Generate Mesh are plain buttons (icon-only at worst); Import Feature Layer visible in Setup.
2. Labels: no camelCase anywhere on ribbon or menus; long faces wrap to two lines; Properties face stays "Properties" after selecting a layer.
3. Load a 2D-results-only file: Mesh 2D tab appears; Mesh Editing bar on its own row; hover mesh → status bar "Z: n (mesh)" on ANY tab; pick a terrain in the Terrain combo → DTM Z + canvas bubble return.
4. Results tab: small pill slider knob; slider ≥320px and stretches; narrow window degrades without overlap (Analysis combos too).
5. Preferences + swept dialogs: shrink → scrollbars both axes, no squeezing.
6. Dialog toolbars in light AND dark: new glyphs legible and recolored (esp. pattern/hydrograph, which were unthemed before).
7. Commit when satisfied (iteration-3 files) — no AI attribution, you push.

## POST-RETEST FIX (2026-08-02, user report: mesh toolbar still missing)
Root cause CONFIRMED by headless repro (SWMMVIS_OPEN_ON_STARTUP + offscreen +
QT_PLUGIN_PATH, junction_coupling.inp): onActiveSubWindowChanged blanket-disconnects
`(canvas, layerAdded, this, nullptr)` for the results combos (swmmvis.cpp:~5450),
which silently killed updateMesh2DTabVisibility's refresh connection made earlier in
the same function — and the mesh layer is adopted ASYNCHRONOUSLY after activation, so
the Mesh 2D tab never revealed. (Latent since iteration 2.)
Fix: the mesh-tab refresh now connects with mCompactToolbar as the RECEIVER CONTEXT
(blanket disconnect targets receiver `this` only); warning comment added at the
blanket-disconnect site. Verified headless: refresh fires with has2d=1 after adoption.
Gate: 112/112 + relaunch.
Terrain hover-Z: pipeline verified intact and unchanged (combo select → activeTerrainChanged
→ setActiveTerrain → status bar Z + canvas bubble; sidecar restore path too). Retest by
picking the DTM in the Terrain combo (Model tab) then hovering.

## POST-RETEST ROUND 2 (2026-08-02)
- Tab strip showed scroll arrows despite ample room: revealing/hiding a tab changes the
  QTabBar's size hint but the host strip QToolBar never re-lays (same lazy-layout trap
  as the ribbon chevron). Fix: CompactToolbarController::relayoutStrip() (updateGeometry
  + invalidate + setGeometry) called from setTabVisible + finalize. New test
  revealingTabWidensStripNoScrollers.
- Families unstacked per user: Import (Home, 6 buttons), addNode/addLink → captioned
  "Nodes" / "Links" / "Draw" groups (Model), Data Objects (7 buttons). Select and
  Climate split-buttons KEPT (not in the user's list). RibbonSplitButton machinery
  retained for those two.
- Live render toggle moved from Analysis "Results Layers" group to the Results tab's
  "Display" group (Show Legend / Set Style / Live render) — the during-run display
  controls' home. State management (refreshActiveResultsCombos) unchanged via the
  member pointer; Display group becomes a rigid widget host (won't collapse).
Gate: 112/112 + lint + relaunch.

## POST-RETEST ROUND 3 (2026-08-02) — revision.md v2: ribbon-native Terrain/Mesh
User rewrote workplans/revision.md: no second-row toolbar "slivers"; integrate the
Terrain + Mesh Editing controls INTO the ribbon, with a contextual Mesh 2D tab and a
NEW contextual Terrain tab.
- TerrainToolbar: rebuilt as captioned RibbonGroups hosting its widgets directly —
  Active Terrain (combo) | Vertical Units (DEM combo, conversion, × factor) |
  Invert Offsets (Node Δ / Link Δ) + trailing ribbonBarSpacer. All signals/
  rebind/restoreState logic untouched.
- MeshEditingToolbar: captioned groups each hosting a MINI QToolBar so the entire
  QAction-based contextual show/hide machinery (updateEnabledState) works unchanged —
  Mesh | Vertices | Edges | Coupling | 2D Results. addToolAction/addToolWidget now
  target the 2D Results group's bar (addToolSeparator = API-compat no-op).
  NEW RibbonGroup::refreshWidth() + MeshEditingToolbar::refreshGroupWidths() re-measure
  groups + re-run the host layout when clusters show/hide (QToolBar layouts never
  self-refresh on child hint changes).
- Tabs: Model = {Model bar} only; NEW contextual "terrain" tab {TerrainToolbar} gated
  on any GISRasterLayer; mesh2d tab unchanged. setOwnRow no longer used by the app
  (API + tests kept). updateMesh2DTabVisibility renamed updateContextualTabs, now
  gates BOTH tabs, refresh still anchored on mCompactToolbar (blanket-disconnect-safe).
Gate: 112/112 + lint + headless mesh-open smoke + relaunch.
