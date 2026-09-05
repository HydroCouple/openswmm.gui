# HANDOFF P0 — land pending work, baseline the Simulation Options dialog

Execution plan: `workplans/EXECUTION_PLAN_OPTIONS_TRANSPORT_OUTPUT_2026-09-04.md` §4 P0.
Repos: **openswmm.gui only** (no engine change in P0).
Validator: build, run the tests, walk §4, fill §6, reply. **Do not commit** — report and I commit on your COMMIT verdict.

---

## 1. What P0 delivers

| Item | Status |
|---|---|
| A. Rain-gage map position editable (Properties X/Y rows; Attribute Table X/Y columns) via `SWMMModelLayer::applyGageMove` | implemented earlier today, uncommitted; spec in `workplans/RAINGAGE_COORDINATES_HANDOFF_2026-09-04.md` |
| B. `test_simulationoptions_roundtrip` — Apply with no edits writes nothing, on a DYNWAVE, a 2D and an FV deck; plus a real edit is still detected | **new**, uncommitted |
| C. CI on `swmm6_rel` (engine) and the GUI workflow green after yesterday's header fix | verify only |

B is the load-bearing safety net for P3 (dialog restructure) and P4. If B fails on the *current* dialog, that is a pre-existing identity bug in some page's read/write pair — report it verbatim (the failure message names the section and the differing lines); do **not** patch the dialog to make it pass. I decide whether to fix it in P0.1 or record it as a known baseline.

## 2. Files (exact)

**Rain gage (item A)** — new + single-owner, stage whole:
```
tests/gui/test_raingagecoords.cpp                              (new)
include/ui/properties/swmmraingagepropertyadapter.h
src/ui/properties/swmmraingagepropertyadapter.cpp
src/ui/panels/swmmattributetablemodel.cpp
```
Mixed with another session's hunks — stage **only the listed hunks** with `git add -p`:
```
include/layers/swmmmodellayer.h        take: @@ -1637 +1638  (applyGageMove declaration)         — currently the only hunk
src/layers/swmmmodellayer.cpp          take: @@ -5291 +5291  (applyGageMove definition)
                                       skip: @@ -4890        (virtualJunctionRuleText case 617 — not ours)
src/ui/panels/propertiespanel.cpp      take: @@ -70   +71    (#include openswmm_gages.h)
                                       take: @@ -1136 +1139  (coordChangeRequested → applyGageMove connect)
                                       skip: @@ -646         (onLayerComboIndexChanged — not ours)
```
**Round-trip test (item B)** — new + single-owner:
```
tests/gui/test_simulationoptions_roundtrip.cpp                (new)
```
**Shared by A and B** — stage whole (both hunks are ours; re-check `git diff -- tests/gui/CMakeLists.txt` shows exactly the two `add_swmmvis_gui_test` blocks `test_simulationoptions_roundtrip` and `test_raingagecoords` and nothing else):
```
tests/gui/CMakeLists.txt
```
**Do not stage** (other sessions / scratch): `CHANGELOG.md`, `include/ui/properties/swmmnodepropertyadapter.h`, `src/map/mapcanvas.cpp`, `src/map/tools/maptoolselect.cpp`, `src/swmmvis.cpp`, `src/ui/properties/dataobjectpickereditor.cpp`, `tests/gui/test_nodepropertyadapter.cpp`, `workplans/**`, `tests/gui/data/*.inp` that are modified or newly generated (`simopts_roundtrip_*`, `raingage_coords_out.inp`, `offset_authored_*_out.inp`), anything `??` not listed above.

## 3. Build & automated tests

```
cd openswmm.gui
cmake --build build-rt10 -j                # or ./build-gui.sh
ctest --test-dir build-rt10 -R "test_raingagecoords|test_simulationoptions_roundtrip|test_raingagefilecolumn|test_simulationoptionsdialog|test_offsetmode_roundtrip" --output-on-failure
```
Expected:
- `test_raingagecoords`: 4/4 pass (`adapterReadsAndDefersCoordinateWrites`, `layerMoveUpdatesEngineAndCaches`, `tableCoordinateColumnsAreEditableAndCommit`, `editedCoordinateSurvivesSaveAndReopen`).
- `test_simulationoptions_roundtrip`: 4/4 pass — `dynwave1D`, `twoD`, `finiteVolume`, `editIsDetected`. Artefacts: `tests/gui/data/simopts_roundtrip_{dynwave,2d,fv}_{before,after}.inp` and `simopts_roundtrip_fv_variant.inp`.
  - If `dynwave1D`/`twoD`/`finiteVolume` **fail**, copy the full `QFAIL` message (it prints the section and up to six before-only / after-only lines) into §6. That is the finding, not a defect in the test.
  - `editIsDetected` must pass regardless — if it fails, the dialog's Apply path is not writing at all and everything else is vacuous.
- The three pre-existing suites still pass.

## 4. UI acceptance script (app built from this tree)

Deck: `tests/gui/data/typed_selection_fixture.inp` (gage `S1` at −2000/−2000).
1. Open the deck. Click the rain gage on the map → Properties panel shows **X Coordinate −2000** and **Y Coordinate −2000** near the top of the rain-gage rows. ☐
2. Type `1234.5` into X Coordinate, press Enter → the gage symbol moves on the canvas *immediately*; the window title shows the project as modified. ☐
3. Attribute Table → Rain Gages: X and Y cells are editable (double-click opens a numeric editor). Type `-6789.25` in Y → map follows. Ctrl+Z → Y reverts and the symbol moves back. ☐
4. File → Save As… `raingage_moved.inp` (next to the fixture). Open the saved file in a text editor: `[SYMBOLS]` row for `S1` shows `1234.5 -6789.25`. ☐
5. Reopen `raingage_moved.inp` in the app → gage is at the new location. ☐
6. Simulation Options → OK without touching anything → title bar does **not** become modified (no asterisk / no save prompt on close). Repeat on `mini_2d.inp`. ☐

## 5. CI check
- `openswmm.engine` `swmm6_rel` latest run: green on macos-15, macos-15-intel, ubuntu-latest, windows-latest (yesterday's 8 `no member named …` errors gone). ☐
- `openswmm.gui` latest workflow run green. ☐
If either is red, paste the first error line per runner into §6; P0 cannot close on a red matrix.

## 6. Report back (fill and reply)

```
BUILD gui: OK|FAIL  <first error if FAIL>
TESTS:
  test_raingagecoords:                 PASS|FAIL <case, message>
  test_simulationoptions_roundtrip:    dynwave1D PASS|FAIL <QFAIL text> / twoD … / finiteVolume … / editIsDetected …
  test_raingagefilecolumn / test_simulationoptionsdialog / test_offsetmode_roundtrip: PASS|FAIL
UI: step1 OK|FAIL <seen>  step2 …  step3 …  step4 …  step5 …  step6 …
CI: engine swmm6_rel GREEN|RED <runner: first error>   gui GREEN|RED <…>
TREE: <git status --short of openswmm.gui>
VERDICT: COMMIT | FIX <what>
```

## 7. Commits I will make on COMMIT (for reference — validator does not run these)

```
# 1 — rain gage
git add tests/gui/test_raingagecoords.cpp include/ui/properties/swmmraingagepropertyadapter.h \
        src/ui/properties/swmmraingagepropertyadapter.cpp src/ui/panels/swmmattributetablemodel.cpp \
        include/layers/swmmmodellayer.h
git add -p src/layers/swmmmodellayer.cpp src/ui/panels/propertiespanel.cpp     # hunks per §2
git add -p tests/gui/CMakeLists.txt                                             # test_raingagecoords block only
git commit -m "feat(raingage): make the gage map position editable"   # full message in RAINGAGE_COORDINATES_HANDOFF §5

# 2 — baseline test
git add tests/gui/test_simulationoptions_roundtrip.cpp
git add -p tests/gui/CMakeLists.txt                                             # test_simulationoptions_roundtrip block
git commit -m "test(options): no-edit Apply round trip on DYNWAVE, 2D and FV decks

Baseline for the Simulation Options restructure: opening the dialog and
applying with no edits must leave wroteAnyChanges() false and every
dialog-owned .inp section ([TITLE] [OPTIONS] [REPORT] [FILES] [EVENTS]
[2D_OPTIONS]) byte-identical; a real edit must still be counted."
```
