# Rain-gage map position (X / Y) in the editors — handoff

Date: 2026-09-04
Repo: `openswmm.gui` only (the engine already exposes the API — no engine change).
Status: IMPLEMENTED, NOT COMPILED (the implementing session had no Qt/CMake). Build, run §3, then commit per §5.

⚠️ **The working tree contains at least two other sessions' in-flight work** (a "Plot Rainfall…" button in the Properties panel; a Time Series editor / registry / provider slice; Object Browser changes). §5 stages file-by-file and hunk-by-hunk for that reason. Do **not** `git add -A`. This is the same trap that broke CI on 2026-09-03, when a whole-file `git add` of `SWMMEngine.cpp` shipped half of an unrelated slice.

---

## 1. The gap

A rain gage is a placed object — it has `[SYMBOLS]` X/Y — but the DA.2 adapter set derives it from `SWMMDataObjectPropertyAdapter` (the *non-spatial* base), so:

- **Properties dialog**: no X/Y rows at all. The gage was the only placed object whose location could not be typed in — it could only be drawn with Add Rain Gage, and never corrected afterwards (the Move tool skips gages too; see §4).
- **Attribute Table**: X/Y were present but `ro(...)` — read-only.

The engine has had `swmm_spatial_get_gage_coord` / `swmm_spatial_set_gage_coord` all along, and the layer already read them (`gageCoord`, `applyGageAdd`, delete/undo snapshots). Only the edit path was missing.

## 2. What was implemented

Everything mirrors the existing node-coordinate design, deliberately — same deferral, same commit special-case, same reasons.

| File | Change |
|---|---|
| `include/layers/swmmmodellayer.h`, `src/layers/swmmmodellayer.cpp` | New `bool applyGageMove(int idx, double newX, double newY)` — the `[SYMBOLS]` twin of `applyNodeMove`: engine write, `m_gages[idx].x/y`, the cached scene point (CRS transform + the scene-space Y flip, exactly as `appendGageSceneEntry` does), `m_kdDirty` / `m_needsRebuild` / `++m_geomRevision`, `recomputeExtentFromCaches()`, then `repaintRequested()` + `modelEdited()`. No attached-link bbox pass — gages have none. Placed immediately before `applyGageAdd`. |
| `include/ui/properties/swmmraingagepropertyadapter.h`, `.cpp` | `xCoord` / `yCoord` `Q_PROPERTY`s + getters (read straight from the engine) + setters that **do not write the engine** — they `emit coordChangeRequested(x, y)` with both coordinates, exactly like `SWMMNodePropertyAdapter::setXCoord`. New `coordChangeRequested` signal. Labels added to `displayLabelFor` ("X Coordinate" / "Y Coordinate"). Setting the value it already holds is a no-op. |
| `src/ui/panels/propertiespanel.cpp` | In the rain-gage block of `showDataObject`, connect `coordChangeRequested` → `swmm_gage_index` → `layer->applyGageMove(...)` → `emit objectEdited(name)`. Added `#include <openswmm/engine/openswmm_gages.h>`. |
| `src/ui/panels/swmmattributetablemodel.cpp` | New `gageCoordX()` / `gageCoordY()` column specs (setter tags `gage_coord_x` / `gage_coord_y`), replacing the read-only `ro("X"…)` / `ro("Y"…)` in the `CatRainGages` schema; new `commitValueDirect` special-case routing them through `applyGageMove`, mirroring the `node_coord_x/y` case directly above it. Keys stay `"X"` / `"Y"`, so the **read** path is unchanged (the identify map). No `SetterEntry` is added — the two-argument engine setter does not fit that single-double shape, which is why the node case is special-cased too. |
| `tests/gui/test_raingagecoords.cpp` (new), `tests/gui/CMakeLists.txt` | Four tests, see §3. |

Deliberately **not** changed: the numeric values, the `[SYMBOLS]` writer, and the Move tool's gage filter (§4).

## 3. Build & verify

```
cd openswmm.gui
cmake --build build-rt10 -j                     # or ./build-gui.sh
ctest --test-dir build-rt10 -R "test_raingagecoords|test_raingagefilecolumn|test_attributetableschema|test_gageassignment" --output-on-failure
```

`test_raingagecoords` (fixture `typed_selection_fixture.inp`, gage `S1` at −2000/−2000):

1. `adapterReadsAndDefersCoordinateWrites` — the adapter reports −2000/−2000, both properties are on the meta-object (so `QPropertyModel` renders them), the labels are right, `setXCoord` emits `coordChangeRequested(newX, oldY)` and leaves the engine untouched, and re-setting the current value emits nothing.
2. `layerMoveUpdatesEngineAndCaches` — `applyGageMove` moves the engine value **and** `identifyByName("S1")["X"/"Y"]`, emits `modelEdited` + `repaintRequested`, and refuses an out-of-range index.
3. `tableCoordinateColumnsAreEditableAndCommit` — the X/Y columns now carry setter tags, `flags()` reports `ItemIsEditable`, and `setData` reaches the engine and reads back.
4. `editedCoordinateSurvivesSaveAndReopen` — writes `raingage_coords_out.inp` next to the fixture and reopens it.

Manual checks:
- Select a rain gage on the map → Properties shows **X Coordinate** / **Y Coordinate** at the top; type a new X → the gage symbol moves on the canvas *immediately* (this is the whole point of routing through `applyGageMove`; a bare engine write leaves it stale until the next rebuild) and the title bar shows the project as modified.
- Attribute Table → Rain Gages: X and Y accept typed values, the map follows, Ctrl+Z undoes the cell edit.
- Save → reopen: the new position is in `[SYMBOLS]`.
- Zoom-to-extent after moving a gage far outside the network: the extent includes it (`recomputeExtentFromCaches`).

## 4. Known gap left open (not in scope, flag if you want it)

`MapToolMoveNode` still filters rain-gage hits out (`src/map/tools/maptoolmovenode.cpp:39,65`), so a gage cannot be **dragged** on the map, and there is no `MoveGageCommand` in `mapundostack.cpp` — the undo stack has add/delete for gages but no move. The typed-coordinate path added here is undoable through the attribute table's edit command; the Property Browser path is not (same as the node X/Y browser path today). Making the gage draggable is a separate slice: it needs the tool filter relaxed, a `MoveGageCommand`, and the hit-test priority order thought through (gages sit above nodes in `identifyAt`).

## 5. Commit

Nothing here is committed. Files are listed exactly; check `git diff --cached --stat` before each commit.

**a. Files that are 100% this work — stage whole:**
```
git add include/layers/swmmmodellayer.h src/layers/swmmmodellayer.cpp \
        include/ui/properties/swmmraingagepropertyadapter.h \
        src/ui/properties/swmmraingagepropertyadapter.cpp \
        src/ui/panels/swmmattributetablemodel.cpp \
        tests/gui/test_raingagecoords.cpp tests/gui/CMakeLists.txt
```
Verify first that `swmmattributetablemodel.cpp` and `tests/gui/CMakeLists.txt` carry no other session's hunks (`git diff -- <file>`): at the time of writing they carried only this work, but another session is active in this tree.

**b. `src/ui/panels/propertiespanel.cpp` — MIXED. Stage with `-p`:**
```
git add -p src/ui/panels/propertiespanel.cpp
```
Accept exactly two hunks:
- the `#include <openswmm/engine/openswmm_gages.h>` line;
- the `coordChangeRequested` → `applyGageMove` connect at the end of the rain-gage block.

Skip every `m_plotRainGageButton` hunk and the `setupUi` button hunk — those belong to the Plot-Rainfall slice, whose declaration lives in the **unstaged** `include/ui/panels/propertiespanel.h`. Staging them without that header is exactly the CI break of 2026-09-03. (The connect was deliberately positioned away from the `m_plotRainGageButton->show();` line so the two fall in separate hunks — if a future rebase fuses them, use `e` to drop the `show()` line.)

**Do not stage**: `include/ui/panels/propertiespanel.h`, `CHANGELOG.md`, `CMakeLists.txt`, anything under `include/timeseries/`, `src/timeseries/`, `src/ui/dialogs/timeserieseditordialog.*`, `src/ui/panels/objectbrowserpanel.*`, `src/map/mapcanvas.cpp`, `include/swmmvis.h`, `src/swmmvis.cpp`, `tests/unit/**`, `tests/gui/test_timeseries_editor_dialog.cpp`, `workplans/**`, or the pre-existing modified `tests/gui/data/*.inp`.

```
git commit -m "feat(raingage): make the gage map position editable

A rain gage is a placed object, but its property adapter derives from the
non-spatial data-object base, so the Property Browser exposed no X/Y and the
Attribute Table carried them read-only — the gage was the one placed object
whose location could not be typed in. Add xCoord/yCoord to the adapter and
make the table's X/Y columns editable, both committing through the new
SWMMModelLayer::applyGageMove (the [SYMBOLS] twin of applyNodeMove) so the
cached scene point, the spatial index and the model extent move with the
engine value instead of drifting until the next geometry rebuild."
```

Then update `CHANGELOG.md` at the next release per CLAUDE.md §5.2 (not now — it is already modified by another session).
