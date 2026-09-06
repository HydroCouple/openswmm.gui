# Inlets Editor + Inlet Junction (GUI) — Verification, Build and Commit Handoff

**Date:** 2026-09-05
**For:** the agent that will compile, test, fix, and commit this work.
**Pair with:** `openswmm.engine/plans/INLET_JUNCTION_VERIFICATION_HANDOFF_2026-09-05.md` — **the engine must be built and its inlet tests green first**; the GUI links against the new C API (`swmm_inlet_get_design`, `swmm_inlet_usage_*`, `swmm_node_{is,set}_inlet`, `swmm_node_inlet_eligible`, `swmm_conduit_split_inlet`, `swmm_inlet_junction_fuse`).
**Plan implemented:** `workplans/INLET_EDITOR_AND_INLET_JUNCTION_GUI_PLAN_2026-09-05.md` Phases G1–G5 (G6 docs/CHANGELOG not done).
**Status (2026-09-06):** built and verified. The GUI sources of §6 were already committed inside the manual rewrite 4450fe3; this round built them against the engine's inlet commit (on `swmm6_rel` after e7ade2c1), ran §3's subset 9/9 after three test-side fixes (curve-kind round-trip expectation in `test_inlet_editor`, `nodeTypeLabel(5)` in `test_typeconversionflow`, an `inletUsageFor` link stub for `test_xsectgeominline`), and added the G6 CHANGELOG entry. §4's manual click-through of the rendered editor and map tools was not done (headless only); §5 deferrals stand. §4.11's 5.x block is superseded: Run now writes a SWMM 5 profile (`swmm_model_write_compat`, inlet junction → junction + `[INLET_USAGE]` on the approach conduit) next to the run outputs and feeds the 5.x worker that file, logging each substitution.

## 0. Read this first

All GUI code was written **without Qt or a compiler available** — verification was by careful reading only (includes, signal/slot signatures, Q_PROPERTY consistency, C API signatures, `.ui`/`.svg` XML well-formedness). Expect compile errors on the first build; fix at the site.

The working tree contains **unrelated pre-existing uncommitted work** (aquifer / groundwater-exchange editor, live-1D results, simulation-options round trip, a stray global replace of "Live render" that corrupted a comment in `src/map/mapcanvas.cpp:1725`, `CHANGELOG.md` ("Live 1D" → " 1D") and `forms/swmmvis.ui` ("OpenSWMM 2D" → "SWMM 2D")). Do not fold those into the inlet commit; §6 lists exactly what belongs here.

## 1. What was built

### G1 — toolbar / menu / icons
- `src/swmmvisactions.cpp`: `Draw` group → `Subcatchments`, `Rain Gages`, `Annotation`; `actionNewStreet`, `actionNewInlet` added to `Data Objects` after Transects; `actionAddInletJunction` in `Nodes` after Virtual Junction; face labels.
- `include/ui/actioncatalog.h`: ribbon tab `"model"` on `data.newStreet`/`data.newInlet`; new `model.addInletJunction` row.
- `src/swmmvis.cpp` `kEntries`: Data Objects order … Control Rules, Transects, **Streets, Inlets** ¦ Unit Hydrographs ….
- `resources/images/inlet.svg` redrawn (curb + grate); new `resources/images/inlet_junction.svg` (dashed circle + 3 bars), alias `InletJunction` in `resources/swmmvis.qrc`.

### G2 — Inlets editor (`InletEditorDialog` rebuilt in place)
- `include/inlet/inletprovider.h`/`src/inlet/inletprovider.cpp`: `InletDesignData` mirroring `SWMM_InletDesign`; enums `InletType/GrateType/ThroatType/InletCurveKind` matching the C enums.
- `inletregistry.*`: `loadFromEngine` via `swmm_inlet_get_design/get_comment`; `saveToEngine` add-then-`set_design`; `remove` via `swmm_inlet_delete`; `impactSummary()`.
- New `include/inlet/inletundocommands.h` + `.cpp` (6 commands), `include/ui/dialogs/inletpropertybag.h` + `.cpp` (grouped Q_PROPERTYs, `Q_ENUM` mirrors for combos, `isPropertyVisible`, unit-suffixed labels), `include/ui/widgets/inletdrawingview.h` + `.cpp` (`InletSceneBuilder::build` pure scene builder + `InletDrawingView`).
- `inleteditordialog.*`: three panes (list ¦ name/description/type/property tree ¦ toolbar + drawing), `floatingPanelFlags()`, undo stack, entry points `createNew(...)`, `openForInlet(name)`, `static QString pickInlet(InletRegistry*, SWMMModelLayer*, QUndoStack*, QWidget*, int compatibleXsectShape = -1)`.
- `objectbrowserpanel.cpp` `SWMMObjectRef::Inlet` branch; `comprehensiveeditorregistry.cpp` threads the undo stack; `inletlistmodel.cpp` tooltip.
- Test `tests/gui/test_inlet_editor.cpp` (registered).

### G3 — usage page + connector
- `SWMMModelLayer`: `inletUsageFor`, `applySetInletUsage`, `applyRemoveInletUsage`, `pushInletUsageEdit`, `applyInsertInletJunction`, `applyFuseInletJunction`, `applySetInlet`, `nodeIsInlet`, `engineSupportsInletJunctions()` (tri-state), rule text 623–635, `m_inletJunctionSym`, `m_inletConnectorSym`, routing ids `model.inletjunctions`/`model.inletconnectors`, `NodeGeom.isInlet`, identify popup, `inletConnectors()` cache invalidated on `attributeChanged`/`geometryChanged`/`modelEdited`.
- `mapundostack.*`: `SetInletUsageCommand` (id 24), `InsertInletJunctionCommand` (25), `FuseInletJunctionCommand` (26, `valid()`), `NodeSnapshot::isInlet` + usage snapshot in `DeleteObjectCommand`.
- `linkcompoundeditdialog.cpp` `buildInletUsagePage()` real 8-row form; conduit adapter `inletUsageRef()` label `design → node`.
- Connector drawn in `src/map/swmmlayeritem.cpp` `SWMMLayerItem::paint` (QPainter path, between links and nodes, `Qt::CustomDashLine {4,3}`).

### G4 — inlet junction kind
- `NodeKind::InletJunction = 5`; `SWMMInletJunctionPropertyAdapter : SWMMVirtualJunctionPropertyAdapter` (8 editable rows + read-only `approachStreet`, `crownElev`, `degree`); `propertiespanel.cpp` probes `swmm_node_is_inlet` before `swmm_node_is_virtual`; `DataObjectRef::Inlet` / `CaptureNode` picker kinds (`dataobjectref.h`, `dataobjectpickereditor.cpp`, `propertiespanel.cpp`, `attributetablepanel.cpp`).
- Rendering: QSG + QPainter bucket swap on `isInlet` before `isVirtual`; prefs key `inlet_junction` (`preferencesmanager.cpp`, `noderenderingprefs.*`, `preferencesdialog.cpp`); `kindtreesymbologypanel.*` rows; profile plot glyph (`profilebuilder.h`, `profilenetworkadapter*.cpp`, `profileplotwidget.cpp`).

### G5 — tool, action chain, convert, delete, gating
- New `include/map/tools/maptooladdinletnode.h` + `src/map/tools/maptooladdinletnode.cpp` (STREET-only conduit hit; opens `InletJunctionSetupDialog`; pushes `InsertInletJunctionCommand`), new `include/ui/dialogs/inletjunctionsetupdialog.h` + `.cpp`.
- `forms/swmmvis.ui` action + `menuAddNode` entry; `swmmvis.cpp` checkable list / activator / gating / status hints / capability-gated visibility; `swmmvisprojectwindow.*` tool member/accessor/activator/`toolActionKeys()`; `PreferencesManager` name prefix `IJ`.
- `maptoolselect.*`: delete prompts for inlet nodes and inlet pairs; Convert-To "Inlet Junction" (via `TypeConversionFlow::runToInletJunction`) and demotion; `maptooladdlink.cpp` comment.
- Test `tests/unit/test_inlet_junction_layer.cpp` + fixture `tests/data/inlets/street_inlet.inp` (registered in `tests/unit/CMakeLists.txt`, links `openswmm_engine`).
- Test stubs kept in step: `tests/gui/test_nodepropertyadapter_stubs.cpp` (`pushInletUsageEdit`), `tests/gui/test_typeconversionflow.cpp` (`applySetInlet`, `applySetInletUsage`).

## 2. Build

```sh
cd openswmm.gui
cmake --preset Darwin          # only if build/ is missing or CMakeLists changed (new sources were added)
sh build-gui.sh                # build/build-gui.log
```
Likely first-build failures, in order of probability:
1. **moc**: every new `Q_OBJECT` class (`InletPropertyBag`, `InletDrawingView`, `InletJunctionSetupDialog`, `OpenSWMMVisMapToolAddInletNode`, `SWMMInletJunctionPropertyAdapter`) must have its header in the `CMakeLists.txt` header list (they are) — if moc complains about `Q_ENUM` on a nested `enum class`, check `include/plot/seriesstyleobject.h:75-95` for the working pattern that was copied.
2. **`QPointer<MapCanvas>` incomplete type** in `swmmmodellayer.h` — already moved out of line (`setEditCanvas`/`editCanvas` in the `.cpp`); if AppleClang still complains, include `map/mapcanvas.h` in the `.cpp` (it is) and check nothing else instantiates the pointer in the header.
3. `InletEditorDialog` ctor arity `(registry, layer, undoStack, parent)` at the two `comprehensiveeditorregistry.cpp` launchers and the `objectbrowserpanel.cpp` singleton.
4. `.ui`: `actionAddInletJunction` declared once, referenced in `menuAddNode`; `uic` errors point at the line.
5. Unused-parameter / sign-compare warnings promoted to errors by the project's flags — fix locally.

## 3. Tests

```sh
cd build && ctest -R 'inlet|compact_toolbar|ribbongroup|nodepropertyadapter|typeconversionflow|virtual' --output-on-failure
```
Must be green: `test_inlet_editor` (provider/registry round-trip per type incl. COMBO/CUSTOM, undo/redo, bag visibility table, scene-builder callout counts), `test_inlet_junction_layer` (insert on STREET conduit → `is_inlet` + usage; undo → `.inp` byte-identical; usage set/get/remove; virtual capture node refused with 627/BADPARAM), `test_compact_toolbar` (`catalogTabIdsAreMounted` — `"model"` tab exists), `test_nodepropertyadapter`, `test_typeconversionflow`, `test_profilenetworkadapter_model`, `test_vjsourcesummary`. Then the full suite; the prior baseline was 233/234.

`test_inlet_junction_layer` is the only GUI test that exercises the real engine end-to-end; if `swmm_conduit_split_inlet` returns 623 for the fixture, check that **both halves** of the split conduit resolve the street (engine fix: `vj_split_conduit` copies `pump_curve_name`).

## 4. Manual click-through (do this — nothing here was ever rendered)

Open `openswmm.engine/examples/inlets/street_inlet_junction.inp`.

1. **Toolbar**: Model tab shows groups Select · Edit · Nodes (Junction, Virtual Junction, **Inlet Junction**, Outfall, Divider, Storage) · Links · **Subcatchments** · **Rain Gages** · **Annotation** · Climate · Data Objects (… Transect, **Street**, **Inlet**, …) · Setup · Tools · Mesh 2D. Icons recolour in dark theme (all `#777777` strokes).
2. **Menu**: Model → Data Objects order … Control Rules, Transects, Streets, Inlets ¦ Unit Hydrographs ….
3. **Inlets editor**: opens non-modal, three panes, lists Grate1/Curb1/Combo1/Custom1; selecting each swaps visible property groups (Grate / Curb Opening / Slotted / Custom) and the drawing (plan + section with dimension callouts; COMBO shows the sweeper; Custom shows the curve); GENERIC grate exposes Open Fraction + Splash Velocity; DROP CURB hides Throat; editing Length updates the callout text; Undo/Redo works; rename collision is refused with a status message; Delete on Combo1 warns about `ST_A`; Copy/Export produce an image; close, reopen the project → values persist (no more "defaults on load").
4. **Conduit inlet usage**: select `ST_A` → property "Inlets" row reads `Combo1 → MH1`; open it → 8-row form; change % clogged, Apply, Undo, Redo; Remove inlet → row reads `(none)` and the dashed connector disappears.
5. **Connector**: dashed line from `ST_A` midpoint to `MH1` and from `IJ1` to `MH2`; follows a node move on release; styleable from Symbology → Nodes → Inlet connectors; hidden when its symbol is disabled.
6. **Inlet junction**: `IJ1` drawn as a filled diamond (Symbology → Nodes → Inlet junctions changes it); identify popup "Inlet Junction · Curb1 → MH2"; property panel shows Street Max Depth, Inlet Design (picker opens the Inlets editor), Capture Node, Number of Inlets, % Clogged, Flow Restriction, Depression Height/Width, Placement, read-only Approach Street = `ST_B`, Crown Elev, Degree; edit Placement → attribute table updates.
7. **Add tool**: Inlet Junction tool; click empty canvas → status hint; click sewer pipe `SW_1` → "only on street conduits"; click `ST_C` → setup dialog (design combo lists gutter designs only, capture node combo excludes `IJ1`/virtual nodes/`ST_C`'s ends) → OK → node `IJ2` appears with a dashed connector; Undo → `ST_C` restored (save and diff the `.inp` against the original: identical); Redo.
8. **Delete**: Delete `IJ1` → 3-button prompt ("Re-fuse Conduits" default, text names Curb1 → MH2); Re-fuse → `ST_B`+`ST_C` merged; Undo → `IJ1` back with its usage. Delete `ST_C` alone → prompt offers re-fuse / delete-and-demote / cancel.
9. **Convert**: right-click `J_MID` → Convert To ▸ Inlet Junction is enabled (two STREET conduits, same section); accept → setup dialog → node becomes an inlet junction. Right-click `IJ1` → Convert To ▸ Virtual Junction / Junction demotes (confirm says not undoable). Right-click `MH1` → Inlet Junction disabled with rule text (pipes are not STREET).
10. **Profile plot**: `J_TOP → OUT_ST` shows `IJ1` with the dashed VJ outline plus the inlet glyph.
11. **Gating**: select the legacy 5.x engine in Preferences → Inlet Junction action hidden with tooltip; usage page hidden. (The engine's legacy-compat downgrade writer is **not** implemented — running a model containing `[INLET_JUNCTIONS]` on 5.x must be blocked with a clear message; verify what the pre-run compat check does and add the block if it is missing.)
12. **Save/Reopen**: save the project; the `.inp` has `[INLET_JUNCTIONS]` and `IJ1` is absent from `[JUNCTIONS]`/`[VIRTUAL_JUNCTIONS]`; reopen → everything above still holds.

## 5. Known risks / deferred (GUI)

1. **Never compiled.** Everything is reading-verified only.
2. Connector is painted in `SWMMLayerItem::paint`; with the QSG node overlay on (`QsgNodes`), verify the connectors are still visible (they should sit beneath nodes, above links). During a node drag the connector lags until release.
3. `engineSupportsInletJunctions()` is cached per layer lifetime.
4. Deferred from the plan: `InletUsagePreview` (§3.5, local-depression drawing); result rows on the inlet-junction adapter (§3.4); "pick on map" for the capture node (combos only); refusing an xsection change away from STREET under a live inlet (§6.8 — the engine backstops with 623/635); object-browser sub-count rows for Virtual/Inlet junctions (§3.1 — `SWMMObjectTreeModel` has no info-row concept); attribute-table "Change Type…" cannot *promote* to Inlet Junction; golden-image tests for the drawing; `InletEditorDialog` itself is not under test (only provider/registry/bag/scene builder); manual chapter + release notes (G6).
5. `swmm_inlet_set_design` treats the curve's actual type as authoritative for a CUSTOM design's kind; the editor's Curve Kind combo must therefore match the chosen curve or the save is rejected with BADPARAM — surface that message in the dialog if the first click-through shows a silent failure.
6. Pre-existing, not ours: `propertiespanel.cpp` kind→category switch still omits `Node`/`Subcatchment` (falls through to `DataTimeSeries`).

## 6. Commit plan (GUI)

One commit after the suite is green and the click-through passes: `feat(inlets): three-pane Inlets editor with dimensioned drawings, inlet junction node (split/fuse/convert), inlet-usage editing and capture-node connectors; toolbar groups split`.

Add whole (this effort only): `CMakeLists.txt`, `forms/swmmvis.ui` (**revert the unrelated "SWMM 2D" text hunk first, or `git add -p`**), `include/core/noderenderingprefs.h`, `include/inlet/inletprovider.h`, `include/inlet/inletregistry.h`, `include/inlet/inletundocommands.h`, `include/layers/swmmmodellayer.h`, `include/map/mapundostack.h`, `include/map/tools/maptooladdinletnode.h`, `include/map/tools/maptoolselect.h`, `include/plot/profilebuilder.h`, `include/plot/profilenetworkadapter.h`, `include/swmmvis.h`, `include/swmmvisprojectwindow.h`, `include/ui/actioncatalog.h`, `include/ui/dialogs/inleteditordialog.h`, `include/ui/dialogs/inletjunctionsetupdialog.h`, `include/ui/dialogs/inletpropertybag.h`, `include/ui/dialogs/kindtreesymbologypanel.h`, `include/ui/dialogs/linkcompoundeditdialog.h`, `include/ui/dialogs/typeconversionflow.h`, `include/ui/properties/dataobjectref.h`, `include/ui/properties/swmmnodepropertyadapter.h`, `include/ui/widgets/inletdrawingview.h`, `resources/images/inlet.svg`, `resources/images/inlet_junction.svg`, `resources/swmmvis.qrc`, `src/core/noderenderingprefs.cpp`, `src/core/preferencesmanager.cpp`, `src/inlet/inletprovider.cpp`, `src/inlet/inletregistry.cpp`, `src/inlet/inletundocommands.cpp`, `src/layers/swmmmodellayer.cpp`, `src/map/mapundostack.cpp`, `src/map/swmmlayeritem.cpp`, `src/map/swmmlayerqsgrenderer.cpp`, `src/map/tools/maptooladdinletnode.cpp`, `src/map/tools/maptooladdlink.cpp`, `src/map/tools/maptoolselect.cpp`, `src/plot/profilenetworkadapter.cpp`, `src/plot/profilenetworkadapter_model.cpp`, `src/plot/profileplotwidget.cpp`, `src/swmmvis.cpp` (**`git add -p`**: also carries the intentional "Live 2D" rename from another effort), `src/swmmvisactions.cpp`, `src/swmmvisprojectwindow.cpp`, `src/ui/dialogs/inleteditordialog.cpp`, `src/ui/dialogs/inletjunctionsetupdialog.cpp`, `src/ui/dialogs/inletpropertybag.cpp`, `src/ui/dialogs/kindtreesymbologypanel.cpp`, `src/ui/dialogs/linkcompoundeditdialog.cpp`, `src/ui/dialogs/preferencesdialog.cpp`, `src/ui/dialogs/typeconversionflow.cpp`, `src/ui/editors/comprehensiveeditorregistry.cpp`, `src/ui/models/inletlistmodel.cpp`, `src/ui/panels/attributetablepanel.cpp`, `src/ui/panels/objectbrowserpanel.cpp`, `src/ui/panels/propertiespanel.cpp`, `src/ui/properties/dataobjectpickereditor.cpp`, `src/ui/properties/swmmlinkpropertyadapter.cpp`, `src/ui/properties/swmmnodepropertyadapter.cpp`, `src/ui/widgets/inletdrawingview.cpp`, `tests/gui/CMakeLists.txt`, `tests/gui/test_inlet_editor.cpp`, `tests/gui/test_nodepropertyadapter_stubs.cpp`, `tests/gui/test_typeconversionflow.cpp`, `tests/unit/CMakeLists.txt`, `tests/unit/test_inlet_junction_layer.cpp`, `tests/data/inlets/street_inlet.inp`, `workplans/INLET_EDITOR_AND_INLET_JUNCTION_GUI_PLAN_2026-09-05.md`, this file.

Not this effort (leave alone): everything `aquifer*`/`groundwater*`/`gwf*`/`gwsource*`/`vjsourcesummary*`, `subcatch*`, `nodecompoundedit*`, `swmmattributetablemodel.cpp`, `mapcanvas.cpp`, `CHANGELOG.md` (until you add the entry), `tests/gui/test_nodepropertyadapter.cpp`, `tests/gui/data/*`, `tests/manual/*`, `tests/perf-data/*`, `verification/`, other `workplans/*`.

`CHANGELOG.md` entry (CLAUDE.md §5.2), under Unreleased: Added — Inlets editor rebuilt (three panes, per-type property groups, to-scale plan/section drawings with dimension callouts, undo); Inlet Junction node (Add tool splits a street conduit, Convert To, re-fuse on delete, property panel, symbology, profile glyph); inlet-usage editing on conduits; dashed capture-node connectors; Streets/Inlets on the Model ribbon. Changed — Model toolbar `Draw` group split into Subcatchments / Rain Gages / Annotation; Data Objects menu order (Streets, Inlets after Transects); `inlet.svg` redrawn. Requires engine ≥ the build that ships `swmm_node_is_inlet` (older engines hide the feature).
