# HANDOFF — GUI Delete for All Object Types (Object Browser · Map · Attribute Table)

Date: 2026-07-22
Author: first-pass implementation (written without a local build — see "Build status" below)
Companion plan: `workplans/GUI_DELETE_ALL_OBJECTS_PLAN_2026-07-22.md`
Master plan this slices from: `workplans/OBJECT_DELETION_AND_UNDO_PLAN_2026-07-16.md`

## Purpose of this handoff

A first pass of GUI delete has been implemented and committed to the working tree. It was written by reading source only — **it has NOT been compiled or run.** Your job: build it, fix compile/runtime errors, verify behavior (incl. Windows), and extend coverage to the remaining object types. Treat every code block below as "best-effort, verify against the compiler."

---

## Build status — DO THIS FIRST

Nothing here has been compiled. Before anything else:

```
cmake --preset <your-preset>          # or your normal configure step
cmake --build --preset <your-preset>  # expect possible errors in the 3 files below
```

Then run the GUI test subset and a smoke test (load an `.inp`, try each delete path).

The three changed files:
- `include/map/mapundostack.h`
- `src/map/mapundostack.cpp`
- `src/ui/panels/objectbrowserpanel.cpp`

No files were added and no `CMakeLists.txt` changes were needed (all three are already in the build). If you add new files (e.g. a `DeleteImpactDialog`), wire them into CMake.

---

## What was implemented (committed)

### 1. Object browser — Delete for spatial objects (`objectbrowserpanel.cpp`)
The spatial-category **leaf** context menu (junctions/outfalls/storage/dividers, conduits/pumps/orifices/weirs/outlets, subcatchments, rain gages) gained a **"Delete…"** action. It maps `ref.objectType` → `DeleteObjectCommand::TargetKind` and pushes a `DeleteObjectCommand` onto `m_canvas->undoStack()` — the exact path the map tool and attribute table already use (node→link cascade + undo included). Falls back to `m_layer->apply*Delete(name)` when there is no undo stack (headless/tests). Single-object (right-clicked leaf); multi-select is a follow-up.

### 2. New `DeleteDataObjectCommand` (`mapundostack.h/.cpp`)
Undoable deletion of **curves, time series, and transects** — the three non-spatial data types that have registry-backed CRUD + engine delete today. Design:
- `redo()`: resolve the owning registry (`ensureCurveRegistry` / `ensureTimeseriesRegistry` / `ensureTransectRegistry`), `findByName`, `registry->remove(provider)`, then `registry->saveToEngine(layer->engine())` to flush the engine tables.
- `undo()`: `registry->create(name[, type])`, repopulate from a full snapshot captured in the constructor (points + all metadata), then `saveToEngine`.
- `static bool supports(ObjectType)` → true only for Curve/TimeSeries/Transect, so menus can grey unsupported types.
- Command id = **17** (16 is `DeleteObjectCommand`; confirm no other command already uses 17).

Snapshot fields captured (verify these getters/setters against the headers):
- **Curve**: `type()`, `points()` (`CurvePoint{x,y}`) → restore via `create(name,type)` + `setAllPoints`.
- **TimeSeries**: `unitsLabel/description/sourceMode/filePath/columnSelector/fileMTime`, `points()` (`TimeseriesPoint{time,value}`) → restore via `create(name)` + setters + `setAllPoints`.
- **Transect**: `comments`, roughness (`nLeftBank/nRightBank/nChannel`), banks (`xLeftBank/xRightBank`), encroachment (`xLeftEncroachment/xRightEncroachment`), modifiers (`stationMultiplier/elevationOffset/meanderFactor`), `points()` (`TransectPoint{station,elevation}`) → restore via `create(name)` + setters + `setAllPoints`.

### 3. Object browser — Delete for data objects (`objectbrowserpanel.cpp`)
The **data-object leaf** menu (SectionData) gained **"Delete…"**: enabled when `DeleteDataObjectCommand::supports(ref.objectType)`, disabled with a tooltip otherwise (patterns, pollutants, aquifers, snowpacks, LID controls, streets, inlets, land uses, hydrograph groups, control rules). On confirm it pushes a `DeleteDataObjectCommand`.

---

## KNOWN RISKS / things to verify (I could not compile)

1. **Referential integrity on data-object delete is NOT handled.** `registry->remove` + `saveToEngine` rebuilds the engine tables by clear+re-add from the registry — it does **not** nullify dependents. Deleting a curve still referenced by a pump, or a time series referenced by an inflow/rain gage, will leave a dangling reference (the engine's `swmm_table_delete` referential logic is bypassed by the registry path). **Options:** (a) route data-object delete through `swmm_table_delete`/`swmm_transect_delete` on the engine instead of/in addition to the registry, keeping the registry in sync; or (b) add a pre-delete reference scan + warn. This is the single most important correctness item — see the master plan G3/G4 and the impact-preview note below.
2. **Time-series point cache.** `TimeseriesProvider::points()` may be empty if the point cache was disposed (`disposePointCache`, file-sourced series). Snapshot may under-capture; undo could lose points for file-backed series. Guard: check `isPointCacheLoaded()` and reload before snapshot, or snapshot the file source only.
3. **Registry `saveToEngine` overload.** Curve exposes only `saveToEngine(void*)`; timeseries/transect also have a no-arg cached-handle version. Code uses `saveToEngine(layer->engine())` uniformly — confirm `SWMM_Engine` converts to the `void*` param (it does at existing call sites in `comprehensiveeditorregistry.cpp`).
4. **Command id 17 uniqueness** — grep for `id() const override` / `return 17` to be sure.
5. **View refresh.** Data-object delete relies on the registries' `providerAboutToBeRemoved` / `providerAdded` signals to refresh the object tree, properties panel, and any open editor. Verify the tree model + panels are actually connected to those signals; if not, emit the appropriate `SWMMModelLayer` change signal after mutation.
6. **`#include "selection/selectionmanager.h"` added to `mapundostack.h`** for `SWMMObjectRef`. Check for include cycles (should be fine; `swmmmodellayer.h` already pulls it in).
7. **Namespaces** used: `openswmmvis::curve::{CurveRegistry,CurveProvider,CurveType,CurvePoint}`, `openswmmvis::timeseries::{TimeseriesRegistry,TimeseriesProvider,TimeseriesPoint}`, `openswmmvis::transect::{TransectRegistry,TransectProvider,TransectPoint}`. Verify exact spellings/namespaces.
8. **Undo restores registry state but engine indices shift.** After undo re-adds an object, other in-flight name→index references should be fine (registry is name-keyed), but re-run the INP round-trip oracle to be sure.

---

## Windows parity (user reported gaps — please characterize and fix)

The user is hitting gaps specifically on Windows. There are **no `#ifdef Q_OS_WIN` guards** around any delete path, and key handling already covers both `Key_Delete` and `Key_Backspace` (`maptoolselect.cpp:609`, `attributetablepanel.cpp:414`). Please verify on Windows:
- Delete + Backspace keys fire in the map canvas, attribute table (row-selection state, not cell-edit), object browser tree.
- Right-click "Delete…" appears and fires in all three surfaces.
- Focus handling: does the map canvas have focus to receive the key after a selection?
- Multi-select delete.
- Confirm the Windows build links the **same** engine that exposes `swmm_*_delete` + `analyze_impact` (the header audited was `openswmm_edit.h` from the alpha.1 package; confirm the linked engine matches). A build/engine mismatch would present as "delete does nothing" on Windows only.

Ask the user for concrete symptoms; the fix location depends on which of the above fails.

---

## Remaining work to reach "all object types"

### A. Engine Phase 0.2 (separate repo `openswmm.engine`) — REQUIRED for the other ~10 types
No engine delete API exists for: pattern, pollutant, aquifer, snowpack, LID control, street, inlet (design), land use, hydrograph group, control rules. Add `swmm_<type>_delete` + `swmm_<type>_analyze_impact` following the existing six (`swmm_table_delete` etc.), extend `SWMM_RefType`, and cascade/nullify per the master plan §0.2 table. Ship, version-bump, relink the GUI.

### B. Extend `DeleteDataObjectCommand` once engine APIs exist
For each new type: add its `case` to `supports()`, `redo()`, `undo()`, and a snapshot/restore pair using its registry (`ensurePatternRegistry`, `pollutant/aquifer/...Registry`). Same shape as the three already done.

### C. Attribute table
`objectTypeForCategory` (in `attributetablepanel.cpp`) maps only spatial categories; the attribute table currently surfaces **only spatial network objects**, which already have delete. If/when data-object categories are added to the table, route them through `DeleteDataObjectCommand` (guard with `supports()`), mirroring `deleteObjects` (~:1628). No table-specific delete logic needed.

### D. Layer tree (`layertreepanel.cpp`)
Per-object rows can get the same "Delete…" delegating to `DeleteObjectCommand` / `DeleteDataObjectCommand`. Optional; low priority.

### E. Impact-preview dialog (master plan Phase 1.3 / this plan Phase 4)
Add `DeleteImpactDialog` running `swmm_*_analyze_impact` and grouping "Will also be deleted" / "References will be cleared" / "Control rules affected". Switch all delete entry points from the count-only `QMessageBox::question` to it, batching multi-object deletes into one dialog. This is also the natural home for the data-object referential-integrity warning (risk #1).

### F. Multi-select delete in the object browser
Current browser delete acts on the right-clicked leaf only. Extend to `m_view->selectionModel()->selectedRows()`, collect same-section refs, wrap N commands in one `QUndoCommand` macro (as `attributetablepanel.cpp:1654` does).

---

## Verification checklist (definition of done)

- [ ] Project builds on macOS **and** Windows with the 3 changed files.
- [ ] Object browser: right-click Delete on a junction removes it + its links; Ctrl+Z restores both.
- [ ] Object browser: right-click Delete on a curve/time series/transect removes it; Ctrl+Z restores it with all points + metadata intact.
- [ ] Unsupported data types show a disabled "Delete…" with tooltip.
- [ ] Delete a curve referenced by a pump → decide + implement the referential-integrity behavior (warn or nullify); confirm no engine crash on next run.
- [ ] INP round-trip oracle: build model → delete → undo → `building_write_model` matches pre-delete (write outputs under `workplans/test_output/deletion_undo/`).
- [ ] Windows key/menu parity confirmed for all three surfaces.
- [ ] `CHANGELOG.md` updated (CLAUDE.md §5.2).

## Files touched
- `include/map/mapundostack.h` — `DeleteDataObjectCommand` declaration + includes (`<QDateTime>`, `<QPair>`, `selection/selectionmanager.h`).
- `src/map/mapundostack.cpp` — `DeleteDataObjectCommand` implementation + registry/provider includes.
- `src/ui/panels/objectbrowserpanel.cpp` — `<QMessageBox>` include; spatial-leaf "Delete…"; data-leaf "Delete…".
