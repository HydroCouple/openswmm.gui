# swmmvis GUI Redesign — Iteration 4 (workplans/revision.md, 7 items)

## Context

Post-iteration-3 retest produced a rewritten `workplans/revision.md` with 7 items: 4 ribbon layout refinements, a hover-Z fix (both terrain AND mesh Z in the status bar), a major unified Land Use / Buildup / Washoff / Sweeping editing feature (engine API expansion authorized), and a 2D-defaults Preferences page. Exploration root-caused everything:

- **Item 5 root causes**: (a) `mActiveTerrain` stays null until the user manually picks a DEM — adding a raster never auto-selects (`terraintoolbar.cpp:326-352` emits only on pointer change; `rebuildCombo` never selects); (b) `GISRasterLayer::valueAt` (src/layers/gisrasterlayer.cpp:402-449) **ignores its `canvasSRS` parameter** — any on-the-fly reprojection samples out-of-bounds → NaN; (c) mesh-Z *masks* terrain-Z in `updateCoordinateReadout` (swmmvis.cpp:7531) instead of showing both.
- **Item 6 blockers**: engine `InpWriter.cpp` never emits `[COVERAGES]` or `[LOADINGS]` (silent data loss on save — coverage entered in the existing dialog is dropped); no `swmm_landuse_rename`/`swmm_pollutant_rename` (GUI rename silently duplicates engine objects); no initial-loadings setter. The buildup/washoff C API is complete (`openswmm_quality.h:70-126`), the parser handles all 6 sections, and a full referential-integrity framework exists (`openswmm_edit.h`: `swmm_landuse_analyze_impact`/`_delete` with cascade reports) that the GUI never calls.
- **Item 7**: `PreferencesManager::SimulationDefaults` is the exact precedent (Preferences page → `synthesizeBlankInp` on File→New → `SimulationOptionsDialog` missing-key fallbacks). `read2DFromEngine` (simulationoptionsdialog.cpp:1576-1613) hardcodes fallbacks; `module2DEnabled` pref exists but has **zero consumers**; MeshGenerationDialog::seedDefaults (meshgenerationdialog.cpp:2527-2582) is fully hardcoded.

## User decisions (locked)
1. 2D Defaults Preferences page covers **both** [2D_OPTIONS] solver keys AND mesh-generation defaults.
2. **Full loadings editing**: engine setter + writer + subcatchment-dialog UI.
3. Standing conventions: no AI attribution in commits; NEVER push; workplans/ + test_artifacts/ + Testing/ untracked; every phase gated on build + full `ctest -L gui` + lint; engine rebuilt via build/darwin → `cmake --install` → `codesign --force --sign - install/Darwin/bin/libomp.dylib`.

---

## Phase 1 — Ribbon refinements (revision items 1–4)

**Files**: src/swmmvis.cpp, src/ui/toolbars/mesheditingtoolbar.{cpp,h}, src/ui/toolbars/terraintoolbar.{cpp,h}, src/swmmvisactions.cpp, resources/images/ + resources/swmmvis.qrc, tests/gui/.

1. **Analysis combos stacked vertically** (item 1): in `initializeAnalysisLayerCombos` (swmmvis.cpp:1023-1086) replace the 4 flat `addWidget` calls with ONE wrapper `QWidget` + 2×2 `QGridLayout` (row 0: "1D results:" label + combo; row 1: "2D results:" label + combo) added via `mGroupResultsLayers->addWidget(wrapper)`. Raise combo `setMinimumWidth` 160 → 260; set the wrapper's own minimumWidth so `widthForMode` (which takes `qMax(sizeHint, minimumWidth)` of direct children — ribbongroup.cpp:234) reports honestly. Height budget: ~78px content (100 − margins − caption) fits two ~26px rows. Precedent: mesh toolbar wrapper pages (mesheditingtoolbar.cpp:199-216).
2. **Mesh 2D group order — Coupling last** (item 2a): reorder ctor calls at mesheditingtoolbar.cpp:74-78 to `Mesh, Vertices, Edges, 2D Results, Coupling` — then step 4's new Profile group goes between 2D Results and Coupling → final order **Mesh | Vertices | Edges | 2D Results | Profile | Coupling**.
3. **Remap 1D↔2D icon** (item 2b): author `remap1d2d.svg` (house style: 24×24 viewBox, stroke #777777, width 2, round caps, fill none — a small circle (1D node) and a triangle (2D cell) joined by two opposing arrows), qrc alias `Remap1D2D`, `m_actRemap->setIcon(IconFactory::icon("Remap1D2D"))` at mesheditingtoolbar.cpp:184. Give `m_actAutoCouple` a reused icon (`Snap`) so the Coupling group isn't half-iconed.
4. **Mesh profile → standalone group** (item 4): new `m_barProfile = makeGroupBar(tr("Profile"))`; add `MeshEditingToolbar::addProfileAction(QAction*)` routing there; SWMMVis (swmmvis.cpp:1020) calls it instead of `addToolAction(actMeshProfile)`. Cell selection + contextual attribute widgets (Pick 2D Cells, cell info, Manning's, tag) stay in "2D Results" — profile no longer oscillates with `updateEnabledState`'s show/hide (mesheditingtoolbar.cpp:1244-1250).
5. **Terrain profile group** (item 3): new RibbonGroup `tr("Profile")` in TerrainToolbar; add `TerrainToolbar::addProfileAction(QAction*)` that inserts the group **before** the trailing spacer (terraintoolbar.cpp:118-124). SWMMVis:795-811 drops the raw `addSeparator()+addAction()` (which currently lands after the spacer, floating right) and calls the new method. Host via an inner mini-QToolBar (same `makeGroupBar` shape) so the checkable action renders as a proper ribbon button.

Tests: extend test_compact_toolbar / add cases: stacked-combos group height ≤ kRibbonRowHeight and width ≥ wrapper minimum; mesh toolbar group order assertion (captions sequence); terrain profile group precedes spacer.

Gate: build + full `ctest -L gui` + smoke (Analysis tab: stacked wide combos; Mesh 2D: coupling last, remap icon; Terrain: Profile group visible left of the gap).

## Phase 2 — Hover-Z: terrain + mesh both in status bar (item 5)

**Files**: src/layers/gisrasterlayer.cpp, src/swmmvisprojectwindow.cpp, src/ui/toolbars/terraintoolbar.cpp, src/swmmvis.cpp.

1. **CRS fix (the actual "does not render" bug)**: make `GISRasterLayer::valueAt` honor `canvasSRS` — transform (mapX, mapY) canvas→raster CRS before the pixel math when the SRSs differ (reuse the layer's existing reprojection transform objects from the render path; cache the coordinate transform, don't rebuild per sample at mouse-move rate).
2. **Auto-select DEM**: in `TerrainToolbar::rebuildCombo` (terraintoolbar.cpp:326-352), when the current selection is `(none)` and ≥1 raster exists, select the first raster and emit `activeTerrainChanged` — so hover-Z works immediately after adding a DEM. Manual re-selection still respected; `restoreState` (QSignalBlocker paths) untouched.
3. **Show both Zs** in `updateCoordinateReadout` (swmmvis.cpp:7500-7544): replace the mask-precedence with concatenation — terrain first, mesh second:
   `X, Y  Z: 12.345 ft (terrain)  Z: 12.301 (mesh)` — each part appended only when its value exists. Label the terrain Z with `(terrain)` for symmetry now that both can appear.
4. **Widen** `mLineEditCoordinates` minimum 220 → 320 (swmmvis.cpp:1739) — "Expand length if warranted".

Gate: build + suite; smoke with a mesh model + DEM in a reprojected canvas: both Zs live while hovering the mesh over terrain, terrain-only Z off-mesh, works with the Terrain tab not current.

## Phase 3 — Preferences: 2D Defaults page (item 7)

**Files**: include/core/preferencesmanager.h, src/core/preferencesmanager.cpp, src/ui/dialogs/preferencesdialog.{cpp,h}, src/swmmvis.cpp (synthesizeBlankInp), src/ui/dialogs/simulationoptionsdialog.cpp, src/ui/dialogs/meshgenerationdialog.cpp, tests/gui/.

1. **Storage**: new POD `PreferencesManager::TwoDDefaults` mirroring `SimulationDefaults` (preferencesmanager.h:383-437 pattern): the 17 [2D_OPTIONS] keys (fields + compiled-in initializers matching the current hardcoded fallbacks at simulationoptionsdialog.cpp:1576-1613) **plus** the mesh-generation seeds (minAngle 33.0, maxArea 0, maxSteiner −1, idwPower 2.0, simplifyEps, snapEps, nodeFlattenRadius, minNodeSeparation on/2m, thinning on/0.6/3, boundaryBufferAuto, maxBoundaryEdge, manningsN 0.035, outputExternal — from meshgenerationdialog.cpp:2527-2582; store SI values, unit-scale at consumption exactly as seedDefaults does today). Persist under `"SWMMVis/Preferences/TwoDDefaults"` with the same `readSetting`/`put` idiom (preferencesmanager.cpp:1389-1456); one `preferenceChanged("Defaults", "TwoDDefaults")` emit.
2. **Page**: `buildTwoDDefaultsPage()` following `buildSimulationDefaultsPage` (preferencesdialog.cpp:536-697) — group boxes: "2D solver ([2D_OPTIONS])", "Wet/dry & VFR", "Coupling", "Rainfall & reporting", "Mesh generation defaults". Insert `addCategory(tr("2D Defaults"), …)` after "Dynamic Wave Defaults" (preferencesdialog.cpp:84-96 — insertion is label-safe). Wire the three parallel blocks: readFromManager / writeToManager / onResetToDefaults.
3. **Consumers**:
   - `synthesizeBlankInp` (swmmvis.cpp:3964-4055): when `simulationDefaults().module2DEnabled`, emit a `[2D_OPTIONS]` section from `TwoDDefaults` — this also **revives the currently-dead `module2DEnabled` preference**.
   - `read2DFromEngine` (simulationoptionsdialog.cpp:1576-1613): replace every hardcoded fallback string with the `TwoDDefaults` value (same lockstep idiom as the 1D tab, comment at :2200-2203).
   - `MeshGenerationDialog::seedDefaults` (meshgenerationdialog.cpp:2527-2582): source seeds from `TwoDDefaults` instead of literals (keep the unit-scaling at :2551).
4. Contract test: extend tests/gui/test_2d_vfr_options_contract.cpp (or a sibling) — File→New with module2DEnabled produces [2D_OPTIONS] matching prefs; mesh dialog seeds match prefs.

Gate: build + suite; smoke: change a 2D default → File→New shows it in Simulation Options 2D tab; mesh dialog opens with the preferred min-angle.

## Phase 4 — Engine: quality round-trip + APIs (item 6, engine side)

**Repo**: /Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine. Build → install → codesign libomp before GUI phases 5–6.

1. **InpWriter emits `[COVERAGES]`** (between [LANDUSES] :1480 and [BUILDUP] :1490): rows `subcatch landuse percent` from `ctx.subcatches.coverage` (percent convention — confirmed by test_site_drainage_builder.cpp:361-370), skip zeros.
2. **InpWriter emits `[LOADINGS]`** (after [WASHOFF] :1507): rows from `ctx.subcatches.conc`, skip zeros.
3. **New C API**: `swmm_subcatch_set_initial_loading(engine, sc_idx, pollut_idx, double)` + getter alias (over `ctx.subcatches.conc`), in openswmm_subcatchments.h + _impl.cpp (:727-755 neighborhood).
4. **New C API**: `swmm_landuse_rename(engine, idx, const char*)` and `swmm_pollutant_rename(...)` following the existing node/link/pattern rename pattern. Pollutant rename must update name-stored co-pollutant references.
5. **Doc fix**: `swmm_subcatch_set/get_coverage` `@param fraction (0–1)` → percent 0–100 (openswmm_subcatchments.h:410 — behavior is percent; doc lies).
6. **Bulk coverage getter** (for the GUI matrix): `swmm_subcatch_get_coverages(engine, sc_idx, double* out, int n_landuses)` to avoid O(n_sc×n_lu) FFI churn.
7. **EXT buildup guard**: document that `coeff3` holds a time-series table index when `func_type==EXT` (QualityHandler.cpp:249-256) — GUI renders EXT rows read-only for C3 this iteration.
7b. **New C API — treatment expression validation**: `swmm_treatment_validate_expression(engine, expr, errbuf, buflen, int* col_out)` mirroring `swmm_control_validate_rule` (openswmm_controls.h:169): non-mutating parse via `openswmm::treatment::parse` (src/engine/quality/Treatment.cpp), returning a human message + error position instead of today's bare `SWMM_ERR_BADPARAM` (openswmm_quality_impl.cpp:224-253 recompiles+mutates — unusable per keystroke). Requires threading error messages out of `Treatment.cpp` parse failures. Two grammar caveats to encode: unknown non-alpha chars are currently *silently skipped* (Treatment.cpp:194-197) — tighten to a parse error in validate (and ideally in parse) so the GUI banner matches engine acceptance; co-treatment `R_<pollutant>`/`C_<pollutant>` is parseable only via the lookup overload that production never calls (SWMMEngine.cpp:1985, :5066) — exclude pollutant-prefixed vars from validation/completion this iteration.
8. **GeoPackage parity (audited — the .gpkg path has a bigger hole than .inp)**: no `landuses`/`buildup`/`washoff`/`coverages`/`loadings` tables exist at all, and the existing `pollutants` table drops `Crdii`/`Cdwf`/`Cinit` (`ii_conc` column declared but never bound; `c_dwf`/`init_conc` columns absent). Fix in src/engine/input/geopackage/:
   - **Schema** (GeoPackageSchema.cpp, house conventions: `simulation_id` composite keys, name-string refs, `UNIQUE` + FK like `treatment` :619-626 / `subcatch_adjustments` :516-524): new tables `landuses(simulation_id, landuse_id, sweep_interval, sweep_removal, last_swept, comment)`, `buildup` + `washoff` keyed `UNIQUE(simulation_id, landuse_id, pollutant_id)` (mirroring `treatment`), `subcatch_coverages(simulation_id, subcatch_id, landuse_id, percent, last_swept)`, `subcatch_loadings(simulation_id, subcatch_id, pollutant_id, init_buildup)` (shape of `hotstart_subcatch_pollutant_state` :902-918). Add missing `c_dwf`/`init_conc` columns to `pollutants`.
   - **Writer** (GeoPackageWriter.cpp — pollutants block :1006-1032 neighborhood): emit all new tables + bind the three dropped pollutant fields.
   - **Reader** (GeoPackageReader.cpp): read them back, using the established `column_exists()`/`pragma_table_info` legacy-fallback pattern (:690, :951-963) so pre-existing .gpkg files still open.
9. **Tests**: round-trip cases in tests/unit/engine/test_editor_roundtrip_api.cpp — coverages + loadings survive write→read; rename preserves buildup/washoff/coverage columns and co-pollutant refs. **GPKG**: extend tests/unit/engine/test_geopackage.cpp fixture (`build_test_context()`) with landuses + buildup + washoff + coverages + loadings + full pollutant fields, and round-trip assertions for each (currently zero quality coverage beyond one pollutant's units).
10. **Python API parity (every C-API change lands in Python too, plus gap-covering audit)**:
   - New bindings: `set_initial_loading`/`get_initial_loading` (_subcatchments.pyx + .pyi), `Landuse.rename`/`Pollutant.rename` (_quality.pyx/_pollutants), bulk `get_coverages` (_subcatchments.pyx), `validate_treatment_expression` (_quality.pyx).
   - **Audit + fill existing quality gaps** in the Python layer while there: verify `Landuses`/`Pollutants` expose delete + `analyze_impact` (the openswmm_edit.h framework) and coverage set/get; wrap whatever is missing so the Python surface matches the C API 1:1 for landuse/pollutant/coverage/buildup/washoff/loadings.
   - pytest round-trips for each new/filled binding (existing round-trip test files in python/tests are the home; same pattern as prior editor-API round-trips).
   - Rebuild the editable install (`pip install -e` refresh — stale-.so gotcha) before running pytest.

Gate: engine unit tests green + Python pytest round-trips green; `cmake --install` to install/Darwin; `codesign --force --sign - install/Darwin/bin/libomp.dylib`; GUI reconfigure+rebuild links clean.

## Phase 5 — GUI: unified Land Use editor (item 6, main dialog)

**Files**: new src/ui/dialogs/landuseunifiededitor.{cpp,h} (replaces LandUseEditorDialog registration), new table models in include/ui/models/ + src/ui/models/, src/landuse/landuseregistry.{cpp,h}, src/pollutant/pollutantregistry.cpp, src/ui/editors/comprehensiveeditorregistry.cpp, src/swmmvisactions.cpp, tests/gui/.

**Shape** = Tier C house pattern (`HydrographGroupEditor`, hydrographgroupeditor.h:8-32 is the style guide): non-modal singleton-raise, list pane + tabbed detail pane, layer/registry-signal sync.

1. **Pane 1**: land use list (reuse `LandUseListModel`) + New/Delete/Rename toolbar (Tier-3 icon vocabulary: Add/Delete/Rename) + `QSortFilterProxyModel` search field.
2. **Pane 2 tabs** for the selected land use:
   - **General & Sweeping**: name, sweep interval (days), sweep removal fraction — today's form fields (the landuseeditordialog.cpp:111 "edited elsewhere" note dies; elsewhere now exists).
   - **Buildup**: `QTableView` + new `BuildupTableModel` (rows = pollutants; cols = Function [NONE/POW/EXP/SAT/EXT combo], C1, C2, C3, Normalizer [AREA/CURB]) over `swmm_buildup_get/set`. EXT rows: C3 read-only (table-index guard, Phase 4.7).
   - **Washoff**: `WashoffTableModel` (rows = pollutants; cols = Function [NONE/EXP/RC/EMC], Coeff, Exponent, Sweep Effic %, BMP Effic %) over `swmm_washoff_get/set`. Sweeping properly split: per-land-use interval/removal on General, per-pollutant sweep efficiency here — matching [WASHOFF] columns.
   - Rows appear/disappear automatically with pollutant add/remove: models re-dimension on `PollutantRegistry` `providerAdded`/`providerAboutToBeRemoved` — the "dynamically add and remove sections" requirement.
3. **Registry extension**: `LandUseRegistry` gains buildup/washoff load/save walking the C API in `loadFromEngine`/`saveToEngine`; **fix the two latent defects**: `remove()` now calls `swmm_landuse_analyze_impact` → confirm dialog listing cascades ("N buildup rows, M washoff rows, K subcatchment coverages affected") → `swmm_landuse_delete`; `rename()` calls the new `swmm_landuse_rename` (no more silent engine-side duplication). Mirror the rename fix in `PollutantRegistry` (`swmm_pollutant_rename`).
4. **Launch surfaces**: swap registrations at comprehensiveeditorregistry.cpp:320-324 to the unified editor (singleton-raise idiom :78-86); add "Land Use" to the ribbon Data Objects group (swmmvisactions.cpp:293-295 list + kShortLabels "Land\nUses"); existing menu/Object Browser/property-panel entries route through the same registry entry, unchanged.
5. **Sync seam**: registry signals keep the property panel / Object Browser live, per the established MVC-synchronized-UIs convention; registry `saveToEngine` on dialog finished stays (behavior never regresses mid-edit).

Tests: new test_landuse_unified_editor.cpp — buildup/washoff round-trip (set row → engine get matches), pollutant-add re-dimensions rows, delete shows impact and removes engine object, rename preserves matrices. Remember the IconFactory test-target gotcha (add iconfactory/thememanager/Qt6::Svg to the test target).

## Phase 6 — Subcatchment coverage + initial loadings UI (item 6, subcatchment side)

**Files**: src/ui/dialogs/subcatchcompoundeditdialog.cpp + header, src/ui/properties/swmmsubcatchpropertyadapter.cpp, src/ui/panels/swmmattributetablemodel.cpp, src/ui/panels/propertiespanel.cpp, tests/gui/.

1. **Coverage page rebuild** (subcatchcompoundeditdialog.cpp:70-128): replace the read-only-table + single set-row combo with an **editable full-matrix table**: one row per land use (ALL land uses, including 0%), editable percent column via new `CoverageTableModel` over `swmm_subcatch_get_coverages` (bulk, Phase 4.6) / `swmm_subcatch_set_coverage`. Rows track `LandUseRegistry` `providerAdded`/`providerAboutToBeRemoved`/`providerRenamed` live (fixes the populate-once combo bug at :296). Footer sum label with soft warning when Σ > 100%.
2. **Initial Loadings page** (new page in the compound-edit stack): rows = pollutants, editable initial-buildup column, over the new `swmm_subcatch_set_initial_loading`/getter; rows track `PollutantRegistry` signals.
3. Property panel + attribute table: extend `SWMMSubcatchPropertyAdapter` with a loadings compound ref alongside `landUseRef` (swmmsubcatchpropertyadapter.cpp:442-459 pattern); register in propertiespanel (:272-277) + attribute-table compound column (swmmattributetablemodel.cpp:695 pattern).
4. Coverage entered here now actually persists (Phase 4.1 writer fix) — closes the shipped data-loss bug.

Tests: coverage matrix reflects live land-use add/remove/rename; loadings round-trip; sum warning.

## Phase 7 — Treatment expression editor: highlighting + validation + completion

**Files**: new include/quality/treatmentsyntaxhighlighter.h + src/quality/treatmentsyntaxhighlighter.cpp, new include/ui/widgets/expressionlineedit.{h}/src (or generalized RuleCodeEditor), src/ui/dialogs/nodecompoundeditdialog.cpp (Treatment page :660-719), tests/gui/.

The rules-editor stack is the template and is loosely coupled enough to clone/generalize:
- Highlighter precedent: `RuleSyntaxHighlighter` (include/controls/rulesyntaxhighlighter.h, palette-role formats, hand-written char scanner, static vocab accessors). New `TreatmentSyntaxHighlighter` (~150 lines) with vocabulary from the engine grammar (Treatment.cpp:63-85): variables `C R DT HRT Q V D AREA` (NOT legacy FLOW/DEPTH), functions `exp log ln sqrt min max abs sgn step`, operators `+ - * / ^ ( ) ,`, plus a function token class. Add the same vocab-drift guard comment/test the rule highlighter documents (rulesyntaxhighlighter.h:24-28).
- Completion precedent: `RuleCodeEditor` (include/ui/widgets/rulecodeeditor.h) — QCompleter + Ctrl+Space + auto-popup-after-2-chars; its only coupling is the keyword source (rulecodeeditor.cpp:71-98). Build a single-line `ExpressionLineEdit` variant (or generalize with an injected vocabulary provider) completing variables + functions; no pollutant-prefixed vars (see Phase 4.7b caveat).
- Validation precedent: `RuleValidator` (include/controls/rulevalidator.h — 250ms debounce, Pending-not-Invalid on empty, engine-parser-backed). New debounced call to `swmm_treatment_validate_expression` (Phase 4.7b); surface as a banner under the treatment table using `openswmmvis::ui::theme::bannerStyle()` (themehelpers.h:88, same "● Valid / ⚠ msg / Validating…" idiom as ruleseditordialog.cpp:369-397) + a per-row valid/invalid icon in the Pollutant column.
- **Delegate hosting**: treatment expressions are one-line strings in `QTableWidget` cells (nodecompoundeditdialog.cpp:682-688) — install a `QStyledItemDelegate` whose editor is the `ExpressionLineEdit` (highlighter + completer + live validation), replacing the default QLineEdit. Commit path stays `swmm_treatment_set` (:693-716) but the failure QMessageBox now shows the validator's message + position instead of "error %1".
- Fix the static hint label (:669-680) to include `D` and `AREA`.

Tests: new test_treatment_expression_editor.cpp — highlighter vocab coverage (mirroring test_rules_editor_dialog.cpp:48-133), completer non-null + contains functions, validator states (valid expr / bad LHS / unknown var), delegate returns highlighted editor.

Gate: build + full `ctest -L gui`; smoke: Treatment page cell shows colored tokens while typing, Ctrl+Space completes `HRT`, invalid expression flags the row + banner with message and position, valid one commits to engine.

## Phase 8 — Final gates + commits

- Full `ctest -L gui` (baseline 112 + new cases), chrome-color lint, headless smoke (`SWMMVIS_OPEN_ON_STARTUP=<inp> QT_QPA_PLATFORM=offscreen QT_PLUGIN_PATH=~/Qt/6.9.3/macos/plugins build/SWMMVis.app/Contents/MacOS/SWMMVis`), engine unit suite.
- Manual QA checklist: (1) Analysis stacked combos wide + no overlap at narrow widths; (2) Mesh 2D order Mesh|Vertices|Edges|2D Results|Profile|Coupling, remap icon; (3) Terrain tab Profile group; (4) both Zs in status bar over mesh-on-DEM, terrain Z with reprojected canvas, auto-selected DEM; (5) Preferences 2D Defaults → File→New → [2D_OPTIONS] + mesh dialog seeded; (6) unified Land Use editor: buildup/washoff tables re-dimension on pollutant add, delete shows cascade impact, rename keeps matrices; (7) subcatchment coverage matrix live-tracks land uses, persists through save→reopen; loadings editable; (8) save a quality model as .inp AND .gpkg, reopen both — landuses/buildup/washoff/coverages/loadings and all pollutant fields intact; (9) treatment expression cell: live highlighting, Ctrl+Space completion, invalid-expression banner with message + position.
- Commits: engine commit(s) first (separate repo), then GUI commits split: (a) ribbon/hover/preferences (items 1-5, 7), (b) land-use feature (item 6). No AI attribution; user pushes.

## Risks
- `valueAt` CRS transform at mouse-move rate — must cache the transform (build once per layer/canvas-SRS pair), not per call.
- Stacked-combo group height is tight (~78px budget) — if macOS combo metrics overflow, drop the two labels into combo placeholder text.
- Engine ABI additions require the decoupled find_package flow: engine install first, then GUI reconfigure; stale-install symptom = missing symbols at link.
- EXT buildup C3 is a table index in disguise — the table model must never present it as an editable number (read-only cell + tooltip).
- Auto-selecting a DEM changes behavior for users who deliberately keep "(none)" — only auto-select when the current selection is "(none)" AND no explicit user/restored choice exists.
- Tightening the treatment parser's silently-skipped-unknown-chars laxness (Treatment.cpp:194-197) could reject expressions in existing models that previously "worked" by accident — apply the strictness in the new validate API for sure; gate the change to `parse` itself on a check that no QA fixture relies on the laxness.

---

# EXECUTION RECORD (2026-08-02)

All 8 phases executed. Gates: GUI 116/116 (`ctest -L gui`), chrome-color
lint OK, headless smoke renders (offscreen + QT_PLUGIN_PATH), engine
110/110 + 7/7 test_quality_roundtrip + gpkg quality round-trip, pytest
5/5 new (707 total; 2 geopackage-export plugin failures verified
PRE-EXISTING via stash-bisect against the pre-change engine).

- P1 ribbon: stacked 1D/2D combos (2×2 grid wrapper, min 260px) in
  Results Layers; mesh2d group order Mesh|Vertices|Edges|2D Results|
  Profile|Coupling; Remap1D2D svg + icon (+Snap on Auto-couple); mesh +
  terrain profile actions moved into their own captioned "Profile"
  groups (terrain one before the spacer). Test: stackedComboWrapperFitsRow.
- P2 hover-Z: GISRasterLayer::valueAt now transforms canvas→raster CRS
  (cached OGR CT keyed on both SRS identities); TerrainToolbar
  auto-selects the first DEM when "(none)" and no deliberate none-pick
  (m_userChoseNone); status bar shows BOTH "Z: n unit (terrain)" and
  "Z: n (mesh)"; coord field min 320px.
- P3 prefs: PreferencesManager::TwoDDefaults (17 [2D_OPTIONS] keys +
  17 mesh-gen seeds, SI-canonical) + "2D Defaults" page; consumers:
  synthesizeBlankInp emits [2D_OPTIONS] when module2DEnabled (the
  previously-dead pref now has a consumer), read2DFromEngine fallbacks,
  MeshGenerationDialog::seedDefaults. Test: test_twod_defaults_prefs.
- P4 engine (openswmm.engine 3a28be10): see that commit — COVERAGES/
  LOADINGS writer + parse fix (loadings were a silent parse NO-OP:
  conc sized after handlers + resolver wipe), loadings/bulk-coverage
  APIs, landuse/pollutant rename, treatment validate API, gpkg quality
  tables + pollutant Crdii/Cdwf/Cinit, GROW-PRESERVING landuse/pollutant
  add (resize helpers assign-and-wipe — adds used to destroy quality
  data), Python parity. Install gotcha: top-level `cmake --install`
  dies in legacy BundleRuntimeDeps ("Resolved path is not absolute")
  BEFORE the engine component — install via
  `cmake -DCMAKE_INSTALL_PREFIX=... -P build/darwin/src/engine/cmake_install.cmake`.
- P5 unified Land Use editor: LandUseEditorDialog grew tabs (General &
  Sweeping | Buildup | Washoff) with engine-bound BuildupTableModel/
  WashoffTableModel (EnumComboDelegate combos; EXT C3 read-only guard);
  registry delete is impact-aware (impactSummary via
  swmm_landuse_analyze_impact → swmm_landuse_delete) and rename is
  engine-backed (both registries — silent-duplicate bug fixed); ribbon
  Data Objects gains Land Use. trackPollutantRegistry wired at the
  launch site (NOT in the ctor — keeps SWMMModelLayer out of test link).
  Test: test_landuse_unified_editor (6 cases).
- P6 subcatchment: coverage page rebuilt as editable full matrix (all
  land uses incl. 0%, in-place percent edit, re-lists every refresh,
  Σ>100% warning); new Loadings page/Kind + adapter loadingsRef
  Q_PROPERTY + attribute-table subcatch_loadings_ref column.
  Test: test_subcatch_coverage_loadings (find tables by header text —
  findChild order is unreliable across stacked pages).
- P7 treatment editor: treatmentexpressionedit.{h,cpp} =
  TreatmentSyntaxHighlighter (palette-role formats; vocab drift-guard
  comment + engine-validated in test) + TreatmentExpressionEdit
  (single-line QTextEdit, Ctrl+Space/2-char completion, 250ms debounced
  swmm_treatment_validate_expression) + TreatmentExpressionDelegate;
  wired into NodeCompoundEditDialog Treatment page (banner mirrors
  verdict via bannerStyle(Banner::…), rejected commits now show
  message+column). Treatment compound property item ALREADY existed for
  junction/outfall/storage/divider (adapter Q_PROPERTYs + attribute
  columns) — user requirement verified satisfied, now backed by the new
  editor. Test: test_treatment_expression_editor (4 cases).
- Engine-commit hygiene: `git add tests/unit/engine` swept ~146
  untracked test-run artifacts into the first commit — amended them out
  (artifact dirs stay untracked per convention).

## User retest checklist
1. Analysis tab: 1D/2D combos stacked, wide; no overlap when narrow.
2. Mesh 2D tab: group order …|2D Results|Profile|Coupling; Remap has an
   icon.
3. Terrain tab: Profile group (not a floating right-edge button).
4. Hover: with a DEM loaded (auto-selected now) both terrain and mesh Z
   in the status bar; works with reprojected canvas CRS.
5. Preferences → 2D Defaults: edit a value → File→New picks it up in
   Simulation Options 2D tab; Generate Mesh dialog seeds follow.
6. Model tab → Data Objects → Land Use: tabs General/Buildup/Washoff;
   add a pollutant elsewhere → rows appear; delete lists cascades.
7. Subcatchment properties → Land Uses: full matrix editing; Initial
   Loadings row present; both persist through save→reopen (.inp AND
   .gpkg — engine now writes [COVERAGES]/[LOADINGS]).
8. Node/storage properties → Treatment: colored tokens while typing,
   Ctrl+Space completes, live banner with message + column.
