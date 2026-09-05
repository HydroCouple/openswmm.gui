# Verify handoff — Aquifer editor + Groundwater Exchange editor (2026-09-05)

Implements `AQUIFER_GROUNDWATER_EXCHANGE_GUI_PLAN_2026-09-05.md` (all
decisions D1–D7 taken as recommended). Authored **without a compiler**: the
sandbox has no cmake/Qt. Engine sources passed `g++ -std=c++20 -fsyntax-only`;
the GUI was **not compiled**, only statically cross-checked against the
headers. Your job: build both repos, run the suites, fix what does not compile
or fails (minimal fixes, inside this scope), then do the manual round-trip.

Both trees carry **unrelated uncommitted work** (engine: VJ/inlet-junction,
DynamicWave, fixtures; GUI: offset-mode, VJ lateral inflow, raingage
coordinates, simulation options…). Nothing from this feature is committed.
Do **not** `git stash`/`checkout`/`restore`. Commit this feature with
`git add` of the exact file list below (use `git add -p` on files that also
carry unrelated hunks — marked ⚠ below).

## Build order

1. Engine (`openswmm.engine`): configure/build the existing tree
   (`build-arm64-osx` on this Mac), run
   `ctest -R "test_engine_(gwf_api|groundwater|treatment|inp_writer_roundtrip)"`,
   then the full unit suite. **Install** the engine — the GUI resolves it via
   `find_package`, and the new `[GWF]` API must be in the installed headers
   before the GUI configures.
2. Python bindings: `cd python && pip install -e .` (or the repo's usual
   build) and `python -c "from openswmm.engine import GwfType; print(list(GwfType))"`.
3. GUI (`openswmm.gui`): `cmake --build build -j8` (or `./build-gui.sh`),
   then `ctest --test-dir build -L gui -R "aquifer|gwf|groundwater|gwsource|nodepropertyadapter|subcatchpropertyadapter|subcatch_coverage_loadings|sectiondiagram"`,
   then the full `gui` label.

## Engine — files (Phase E1)

| File | Change |
|---|---|
| `include/openswmm/engine/openswmm_subcatchments.h` ⚠ | New `[GWF]` section: `SWMM_GwfType`, `swmm_subcatch_get/set_gwf_expression`, `swmm_gwf_validate_expression`, `swmm_gwf_variable_count/name/description`, `swmm_gwf_function_count/name`. |
| `src/engine/core/openswmm_subcatchments_impl.cpp` ⚠ | Implementations over `ctx.options.ext_options["GWF:<canonical name>:LATERAL|DEEP"]`; helpers `gwf_key`, `gwf_copy_out`. |
| `src/engine/hydrology/Groundwater.hpp/.cpp` ⚠ | `groundwater::gwf_validate(expr, msg, col)` strict tokenizer/sequencer with arity checks; `GW_VAR_DESCRIPTIONS[]`. |
| `src/engine/math/MathExpr.hpp/.cpp` ⚠ | `mathexpr::function_names()` accessor (func_map order). |
| `src/engine/input/handlers/HydrologyHandler.cpp` ⚠ | `handle_gwf`: accepts `LAT*`, canonical subcatch spelling, validates → `ERR_MATH_EXPR` (233) into `ctx.errors`; bad type → `ERR_KEYWORD`. |
| `src/engine/core/SWMMEngine.cpp` ⚠ | `initHydrology()`: unparsable expression set via API → error 233 + `SWMM_ERR_PARSE` from `initialize()` instead of silent drop. |
| `tests/unit/engine/test_gwf_api.cpp` (new), `tests/unit/engine/CMakeLists.txt` ⚠ | gtest `GwfApiTest`; target `test_engine_gwf_api`. Scratch decks land in `tests/unit/engine/data/gwf_api/_*.inp` (gitignored). |
| `python/openswmm/engine/_common.pxd`, `_enums.py/.pyi`, `_subcatchments.pyx/.pyi`, `__init__.py/.pyi` | `GwfType`; `Subcatchments.get/set_gwf_expression`, `validate_gwf_expression`, `gwf_variables()`, `gwf_functions()`. (`_nodes.pyx` diff is the user's VJ work — leave it.) |
| `CHANGELOG.md` ⚠ | Unreleased/Added entry. |

Expected: `test_engine_gwf_api` green (set/get/clear, LIFECYCLE after
initialize, InpWriter round-trip, validator accept/reject with columns
`Hgww`→0, `2 * Hgww`→4, `2 HGW`→2, `HGW $ 2`→4, `LAT`/mixed-case load,
vocabulary 11/21). If the column numbers disagree with what
`gwf_validate` actually reports, fix the **test** only if the reported column
still points at the offending token; otherwise fix the validator.

MCP repo (`openswmm.mcp`) was not mounted — its subcatchment tools still need
`get/set_gwf_expression` wrappers (follow-up).

## GUI — files

New:

```
include/ui/sectionview/aquiferdiagram.h            src/ui/sectionview/aquiferdiagram.cpp
include/ui/widgets/gwfexpressionedit.h             src/ui/widgets/gwfexpressionedit.cpp
include/ui/dialogs/groundwaterexchangedialog.h     src/ui/dialogs/groundwaterexchangedialog.cpp
include/ui/properties/groundwatersummary.h         src/ui/properties/groundwatersummary.cpp
include/layers/gwsourcesummary.h                   src/layers/gwsourcesummary.cpp
tests/gui/test_aquiferdiagram.cpp
tests/gui/test_aquifer_editor_dialog.cpp           tests/gui/test_aquifer_editor_dialog_stubs.cpp
tests/gui/test_gwfexpressionedit.cpp
tests/gui/test_groundwaterexchangedialog.cpp
tests/gui/test_gwsourcesummary.cpp
tests/gui/data/gw_exchange_fixture.inp
docs/manual/31_groundwater.md
workplans/AQUIFER_GROUNDWATER_EXCHANGE_GUI_PLAN_2026-09-05.md
workplans/AQUIFER_GROUNDWATER_EXCHANGE_VERIFY_HANDOFF_2026-09-05.md
```

Modified (⚠ = file also carries unrelated uncommitted hunks; use `git add -p`):

| File | Change |
|---|---|
| `src/swmmvisactions.cpp` ⚠ | `actionNewAquifer` before `actionNewLidControl` in Data Objects group; ribbon label "Aquifer". |
| `include/ui/actioncatalog.h` ⚠ | `data.newAquifer` tab `""` → `"model"`. |
| `include/aquifer/aquiferprovider.h/.cpp` | `evapPattern()/setEvapPattern()`. |
| `include/aquifer/aquiferregistry.h/.cpp` | Pattern round-trip; `remove()` → `swmm_aquifer_delete`; `impactSummary()`. |
| `include/ui/properties/swmmaquiferpropertyadapter.h/.cpp` | Stub → 12 scalar props + `evapPattern` DataObjectRef. |
| `include/ui/dialogs/aquifereditordialog.h`, `src/ui/dialogs/aquifereditordialog.cpp` | Three-pane rebuild with diagram, groups, pattern combo, warnings, impact-aware delete. Test hooks `spinBox(int)`, `evapPatternCombo()`, `warningLabel()`, `diagram()`, `validationWarnings()`. |
| `include/ui/properties/dataobjectref.h` | `Kind::Aquifer = 11`. |
| `src/ui/properties/dataobjectpickereditor.cpp` | Aquifer population / category / "…" → `AquiferEditorDialog::pickAquifer`. |
| `include/ui/properties/swmmsubcatchpropertyadapter.h/.cpp` | `aquifer` DataObjectRef property; `groundwater` summary via `groundwaterSummary()`. |
| `src/ui/panels/swmmattributetablemodel.cpp` ⚠ | `subcatch_aquifer_ref` picker column; Groundwater cell summary; node `node_groundwater_sources_ref` cell. |
| `src/ui/properties/subcatchcompoundeditbutton.cpp` | `Kind::Groundwater` → `GroundwaterExchangeDialog` (modeless, parented to `window()`). |
| `src/ui/dialogs/subcatchcompoundeditdialog.h/.cpp` | Groundwater page removed; `pageIndexFor(Kind)`. |
| `include/ui/properties/nodecompoundeditref.h` | `Kind::GroundwaterSources`. |
| `include/ui/properties/swmmnodepropertyadapter.h`, `src/ui/properties/swmmnodepropertyadapter.cpp` ⚠ | `groundwaterSources` ref on all node kinds (VJ hunks are the user's). |
| `src/ui/properties/nodecompoundeditbutton.cpp` | GroundwaterSources: open dialog / pick menu / no-op. |
| `src/ui/dialogs/nodecompoundeditdialog.cpp` | Fifth title entry + switch case (was an out-of-bounds read). |
| `src/ui/panels/propertiespanel.cpp` ⚠ | Aquifer → `DataAquifers` in the right-click map; skip Edit… menu for GroundwaterSources. |
| `src/ui/panels/attributetablepanel.cpp` ⚠ | Aquifer → `DataAquifers` in the right-click map. |
| `src/map/tools/maptoolselect.cpp` ⚠ | `groundwaterSourcesNote()` appended to delete prompts. |
| `tests/gui/test_nodepropertyadapter.cpp` ⚠ | New compound key + `groundwaterSourcesSummaryTracksEngine`. |
| `CMakeLists.txt` ⚠ | New headers/sources registered (aquiferdiagram, gwfexpressionedit, groundwaterexchangedialog, groundwatersummary, gwsourcesummary). |
| `tests/gui/CMakeLists.txt` ⚠ | Targets `test_gwfexpressionedit`, `test_groundwaterexchangedialog`, `test_gwsourcesummary`, `test_aquifer_editor_dialog` (+`Qt6::Svg`), `test_aquiferdiagram`; `test_nodepropertyadapter` += `gwsourcesummary.cpp`; `test_subcatchpropertyadapter` += `groundwatersummary.cpp`. |
| `CHANGELOG.md` ⚠, `docs/manual/manual.md` | Entry + manual index line. |

## Known risks to check first (from the static review)

1. **Lean test link chains.** `test_aquifer_editor_dialog` includes
   `swmmmodellayer.h` + `patterneditordialog.h` header-only and stubs
   `SWMMModelLayer::ensurePatternRegistry` / `PatternEditorDialog::pickPattern`
   in `test_aquifer_editor_dialog_stubs.cpp`. If the linker asks for more
   symbols, add the source to the CMake block rather than widening the stub.
   Same for `test_groundwaterexchangedialog` (dialog reaches the layer through
   `QMetaObject::invokeMethod("markEdited")` / `("attributeChanged")` to avoid
   linking the layer — if `attributeChanged`'s signature is not
   `(const QString&)`, fix the `Q_ARG`).
2. `test_subcatch_coverage_loadings` links `subcatchcompoundeditdialog.cpp`,
   which lost its Groundwater page and `openswmm_nodes.h` include — confirm it
   still compiles and its page indices (`pageIndexFor`: LandUse 0, LidUsage 1,
   Loadings 2) match what the test drives.
3. Qt connect overloads in the new dialogs use
   `qOverload<int>(&QComboBox::currentIndexChanged)`,
   `&QDoubleSpinBox::valueChanged`, `qOverload<const QString&>(&QCompleter::activated)`.
4. `test_gwfexpressionedit` asserts `"2 * HGWW"` → col 4 and message containing
   `HGWW`; `test_groundwaterexchangedialog` asserts the fixture's `[GWF]` line
   loads and the summary reads `AQ1 → O1 (custom)` after re-pointing the node.
   These depend on E1 behaviour — run the engine tests first.
5. `AquiferDiagram` numbers in `test_aquiferdiagram` (surface 13.5, water
   table 10.0, 2 dims) were derived by hand from the builder; if the builder's
   layout constants are changed while fixing something, update the test.
6. `SWMMAquiferPropertyAdapter` now reads/writes 12 params; the Object
   Browser's Aquifer properties page should show them with unit suffixes.
7. UTF-8 `→` literals in `groundwatersummary.cpp`/tests (fine on clang/gcc).

## Manual round-trip (after all suites are green)

1. New project → Model tab: **Aquifer** button sits left of **LID Control**.
   Click → editor opens with an empty list; **New** → fill Soil/Conductivity/
   Evaporation/Elevations; the illustration callouts update as you type and
   the focused field's callout is highlighted; set Egw < Ebot → warning under
   the form and in the picture. Create a MONTHLY pattern via "…" and pick it.
2. Draw a subcatchment S1 and a junction J1. Property Browser: **Aquifer**
   row → pick the aquifer. **Groundwater** row reads `AQ1 → (no node)`;
   **Edit…** → Groundwater Exchange editor: pick J1, set A1=0.001, B1=1, type
   `0.001*(HG` → completer offers `HGW`/`HGS`; finish `0.001*(HGW-10)` →
   status "valid"; change to `HGWW` → red status with column, **Apply**
   disabled; restore, Apply. Summary now `AQ1 → J1 (custom)` in both the
   Property Browser and the subcatchment attribute table (Aquifer picker
   column + Groundwater cell).
3. Select J1: **Groundwater Sources** row reads `from S1`; Edit… opens the
   same exchange editor. Delete J1 → prompt mentions S1 losing its receiving
   node (cancel).
4. Save → `[AQUIFERS]` row ends with the pattern name; `[GROUNDWATER]` and
   `[GWF] S1 LATERAL 0.001*(HGW-10)` present. Reopen → everything reloads.
   Run (new engine) → groundwater flow appears at J1 in results.
5. Aquifer editor → Delete AQ1 → prompt says 1 subcatchment will lose it →
   confirm → S1's Aquifer row shows empty and Groundwater `(none)`; save →
   no `[AQUIFERS]`/`[GROUNDWATER]`/`[GWF]` rows.

## Commit plan

Engine: `feat(gwf): public [GWF] expression API + strict validator; LAT alias,
canonical names, load-time error 233` — files in the engine table (use
`git add -p` where ⚠). GUI: `feat(groundwater): aquifer editor with two-zone
diagram, aquifer picker, groundwater exchange editor with [GWF] expression
editing, node groundwater-sources row` — files in the GUI lists. Update the
plan's Status line to "Implemented <date>, verified <date>" when done.
