# Aquifers & Groundwater Exchange Editors — GUI Work Plan

**Status:** Implemented 2026-09-05, verified 2026-09-05 (engine e7ade2c1; GUI test subset + engine suite green; see `AQUIFER_GROUNDWATER_EXCHANGE_VERIFY_HANDOFF_2026-09-05.md`)
**Companion (engine):** Phase E1 below; to be copied to `openswmm.engine/plans/GWF_EXPRESSION_API_PLAN_2026-09-05.md` on approval
**Related:** `workplans/INTEGRATED2D_GW_GUI_PLAN_2026-08-15.md` (2D per-cell aquifers — out of scope here; the ribbon already carries `actionMesh2DGWParams` "Aquifer Parameters" on the Mesh tab, so the new 1D entry must use a distinct label), `openswmm.engine/plans/TWO_ZONE_GROUNDWATER_EXPLICIT_LTS_PLAN_2026-08-15.md`

## Context — what exists today

More is in place than the toolbar suggests. The gaps are wiring, depth, and one missing engine API.

| Piece | State | Reference |
|---|---|---|
| `AquiferProvider` / `AquiferRegistry` (MVC model, engine round-trip of the 12 `SWMM_AquiferParam`s) | Working | `include/aquifer/aquiferprovider.h:29-43`, `src/aquifer/aquiferregistry.cpp:101-136` |
| `AquiferEditorDialog` | First cut: list + 12 flat spin boxes, no diagram, no evap-pattern field, hard-coded unit strings, no validation | `src/ui/dialogs/aquifereditordialog.cpp:81-157` |
| `actionNewAquifer` | Exists in catalog and **Model → Data Objects menu**, but `tab=""` and it is **not in the ribbon "Data Objects" group** | `include/ui/actioncatalog.h:134`, `src/swmmvis.cpp:3777`, `src/swmmvisactions.cpp:305-310` |
| Editor registry entry (`DC::DataAquifers` → browse / create-new) | Working | `src/ui/editors/comprehensiveeditorregistry.cpp:271-302, 525-528` |
| `SWMMAquiferPropertyAdapter` (properties grid page for an aquifer) | Stub (name only); header cites a stale engine gap `DA-ENG-06` — the param API now exists | `src/ui/properties/swmmaquiferpropertyadapter.cpp` (13 lines) |
| Registry `remove()` | **Never calls `swmm_aquifer_delete`** — deleting in the editor leaves the aquifer in the engine and in the saved `.inp` (LandUseRegistry does it right: `src/landuse/landuseregistry.cpp:94`) | `src/aquifer/aquiferregistry.cpp:50-57` |
| Subcatchment → aquifer / receiving node / A1..Hstar | Editable only inside `SubcatchCompoundEditDialog` Groundwater page (combo + 8 spins + Apply) | `src/ui/dialogs/subcatchcompoundeditdialog.cpp:182-243, 407-441` |
| Properties grid / attribute table exposure | One compound cell `groundwater` with summary "aquifer set"/"(none)" | `src/ui/properties/swmmsubcatchpropertyadapter.cpp:533-543`, `src/ui/panels/swmmattributetablemodel.cpp:923, 2741-2745` |
| `[GWF]` LATERAL / DEEP custom-flow expressions | Parsed, evaluated, written by the engine; **no C API** (only `swmm_options_get/set_ext` with key `"GWF:<subcatch>:LATERAL|DEEP"`), **no validator**, no GUI at all | engine `HydrologyHandler.cpp:413-434`, `SWMMEngine.cpp:7119-7149`, `InpWriter.cpp:1449-1469`, `openswmm_model.h:492,507` |
| Expression-editor precedent (highlighter + completer + debounced engine validation) | `TreatmentExpressionEdit`, `ReactionExpressionEdit` | `include/ui/widgets/treatmentexpressionedit.h:64-98`, `openswmm_quality.h:219` |
| Diagram precedent (value model + pure builder + `SectionPreviewWidget`) | `LidLayerDiagram` in `LidControlEditorDialog` | `include/ui/sectionview/sectiondiagram.h:273-352`, `include/ui/sectionview/lidlayerdiagram.h:57-96`, `src/ui/dialogs/lidcontroleditordialog.cpp:367-412` |

Engine facts that shape the design (see `openswmm.engine`):

- Aquifer params 0,1,2,6,11 must lie in [0,1]; 9,10 may be negative; others ≥ 0 (`openswmm_subcatchments_impl.cpp:1145-1160`). Params 0,1,2,9,10,11 are pre-start-only; 3–8 are runtime-editable.
- Evap pattern: `swmm_aquifer_get_evap_pattern` / `set_evap_pattern` (`openswmm_subcatchments.h:945,961`), pre-start-only.
- `[GROUNDWATER]`: `swmm_subcatch_get/set_aquifer`, `get/set_gw_node`, `get/set_gw_params(surf_elev,a1,b1,a2,b2,a3,tw,hstar)` (`openswmm_subcatchments.h:570-610`), all pre-start-only.
- GWF vocabulary: variables `HGW HSW HCB HGS KS K THETA PHI FI FU A` (`Groundwater.hpp:62-81`), functions `abs sgn sqrt log exp sin cos tan asin acos atan step min max cot sinh cosh tanh coth log10 acot` (`MathExpr.cpp:70-81`), operators `+ - * / ^ ( )`, case-insensitive. LATERAL is **added to** the standard A1/A2/A3 flow; DEEP **replaces** the standard deep-percolation term (`Groundwater.cpp:154, 204`).
- The new engine silently accepts a malformed or misspelled expression (unknown identifier → 0.0, `MathExpr.cpp:367`; parse failure → expression dropped at `start()`, `SWMMEngine.cpp:7130`). Validation therefore cannot be "whatever the engine accepts" — it needs a strict validator, as treatment already has (`Treatment.cpp:346`).
- `swmm_aquifer_delete(engine, idx, SWMM_ImpactReport*)` + `swmm_aquifer_analyze_impact` exist (`openswmm_edit.h:166,347`); referencing subcatchments get `gw_aquifer = -1`.

## Scope

1. Ribbon: **Aquifers** entry in the Data Objects group, immediately before LID Control.
2. Aquifer editor rebuilt on the LID-editor pattern: list pane (add/delete), grouped form incl. the evaporation pattern, and a **two-zone groundwater illustration with callouts** that track the form and highlight the focused field.
3. Subcatchments get a direct **Aquifer** picker row (properties grid + attribute table), separate from the exchange editor.
4. New **Groundwater Exchange** editor (receiving node, surface elevation, A1/B1/A2/B2/A3/Twgr/Hstar, LATERAL and DEEP expressions with validation + auto-suggest), opened from the properties grid and the attribute table.
5. Engine: GWF get/set/validate API (+ Python/MCP), because the GUI must not write stringly-typed `ext_options` keys or ship its own parser.

Out of scope: 2D per-cell aquifers (`INTEGRATED2D_GW_GUI_PLAN`), undo for compound edits (matches every existing compound dialog — direct engine write + `markEdited`), legacy-engine (5.2) parity for the GWF API.

## Decisions requested before implementation

| # | Question | Recommendation |
|---|---|---|
| D1 | GWF storage path: new engine API (E1) vs GUI writing `swmm_options_set_ext("GWF:…")` directly with a GUI-side validator | **E1 API.** Matches the treatment/reaction precedent, gives the MCP/Python the same surface, and keeps the case-sensitivity of the ext key out of the GUI. If E1 must slip, G4 can ship behind the ext-option path with an identical widget API and a GUI-local strict tokenizer — flagged as a temporary shim. |
| D2 | Where the exchange editor lives: new `GroundwaterExchangeDialog` **replacing** the Groundwater page of `SubcatchCompoundEditDialog`, vs adding it alongside | **Replace.** One editor per datum (MVC rule); the compound ref `Kind::Groundwater` simply opens the new dialog. The old page's Apply logic moves over verbatim. |
| D3 | Node side: add a read-only "Groundwater inflow from: S1, S3 → Edit…" row on node adapters (junction/outfall/storage/divider, VJ) that opens the exchange dialog for the picked subcatchment | **Include (G5, small).** The request names "nodes and expressions for aquifer inflows into nodes"; the data is owned by the subcatchment, so the node row is a navigational summary, not a second editor. |
| D4 | Illustration content: aquifer-only parameters vs also showing subcatchment-level Esurf / receiving node | Aquifer-only values drawn; ground surface and the lateral-flow arrow to "receiving node" drawn **schematically and labelled "per subcatchment"** so the picture is the full two-zone concept without inventing numbers. |
| D5 | Deleting an aquifer that subcatchments reference | Prompt using `swmm_aquifer_analyze_impact` ("3 subcatchments will lose their aquifer"), then `swmm_aquifer_delete`. Also fixes the current bug where deletion never reaches the engine. |
| D6 | Soft validation in the aquifer form (WP ≤ FC ≤ Porosity, Umc ≤ Porosity, Egw ≥ Ebot) | Inline warning text + amber callout in the diagram; **not** blocking (engine bounds [0,1] / ≥0 stay the hard limits via spin-box ranges). |
| D7 | Engine parser leniencies: accept legacy `LAT` abbreviation; resolve `[GWF]` subcatchment names case-insensitively; report an error (not silence) for an unparsable expression at `start()` | Do all three in E1 — small, and the editor's verdict should equal the engine's. |

## Phase E1 — Engine: `[GWF]` API + validator (`openswmm.engine`)

| File | Change |
|---|---|
| `include/openswmm/engine/openswmm_subcatchments.h` | New section "Custom groundwater flow expressions ([GWF])": `enum SWMM_GwfType { SWMM_GWF_LATERAL = 0, SWMM_GWF_DEEP = 1 }`; `swmm_subcatch_get_gwf_expression(engine, idx, type, buf, buflen)`, `swmm_subcatch_set_gwf_expression(engine, idx, type, expr)` (NULL/empty clears; pre-start-only via `CHECK_GEOMETRY`); `swmm_gwf_validate_expression(engine, expr, errbuf, buflen, col_out)` (grammar-only, never mutates — same contract as `swmm_treatment_validate_expression`). Also `swmm_gwf_variable_count/name(engine, i)` and `swmm_mathexpr_function_count/name` so the GUI completer reads the vocabulary from the engine instead of a copied list. |
| `src/engine/core/openswmm_subcatchments_impl.cpp` | Implement over `ctx.options.ext_options["GWF:<exact subcatch name>:LATERAL|DEEP"]` (single source of truth stays where the parser/writer already look). |
| `src/engine/hydrology/Groundwater.hpp/.cpp` | `int gwf_validate(const std::string&, std::string& msg, int& col)`: strict tokenizer (unknown character = error, as `treatment::validate`), balanced parens, operator/operand sequencing, identifiers must be in `GW_VAR_NAMES` or `func_map` (case-insensitive), function arity (`min`/`max` = 2, others = 1). |
| `src/engine/input/handlers/HydrologyHandler.cpp:413-434` | Accept `LAT*` as LATERAL; look up the subcatchment name via the registry so the stored key uses the canonical `[SUBCATCHMENTS]` spelling; run `gwf_validate` and push `ERR_MATH_EXPR`-equivalent into `ctx.errors` on failure. |
| `src/engine/core/SWMMEngine.cpp:7119-7149` | On parse failure raise an error instead of silently dropping the expression. |
| `python/openswmm/engine/_subcatchments.pyx/.pyi`, `_enums.py/.pyi`; `openswmm.mcp/.../tools/subcatchments.py` | `Subcatchments.get/set_gwf_expression`, `validate_gwf_expression`, `GwfType` enum; MCP tool wrappers. |
| `tests/unit/engine/test_groundwater.cpp` (+ new `test_gwf_api.cpp`) | Set/get/clear round-trip through `InpWriter`; validator accepts the `_hydrology_rt*` fixtures and the manual's `0.001*(Hgw-10)`; rejects `Hgww`, `log(`, `min(HGW)`, `2 HGW`, `HGW $ 2` with the right column; `LAT` keyword and mixed-case subcatch name both resolve. |
| `CHANGELOG.md` | Entry under the GUI-editor round-trip APIs block. |

Verification: engine unit tests green; install the engine (`find_package` consumer) before G4.

## Phase G1 — Ribbon entry, registry fixes, evap pattern

| File | Change |
|---|---|
| `src/swmmvisactions.cpp:305-310` | Insert `"actionNewAquifer"` before `"actionNewLidControl"` in the Data Objects `addGroup`. |
| `src/swmmvisactions.cpp:202` (kShortLabels) | Add `{"actionNewAquifer", QT_TR_NOOP("Aquifer")}` (distinct from the Mesh tab's "Aquifer\nParameters" at `:214`). |
| `include/ui/actioncatalog.h:134` | `tab` `""` → `"model"`. Icon alias `Aquifer` already resolves (`resources/swmmvis.qrc:88`). |
| `include/aquifer/aquiferprovider.h`, `src/aquifer/aquiferprovider.cpp` | Add `QString evapPattern()` / `setEvapPattern()` + `evapPatternChanged` (folded into `paramsChanged` for the registry). |
| `src/aquifer/aquiferregistry.cpp` | `loadFromEngine`/`saveToEngine` round-trip the pattern via `swmm_aquifer_get/set_evap_pattern`; `remove()` calls `swmm_aquifer_delete` (D5) and re-syncs indices; new `impactSummary(AquiferProvider*)` wrapping `swmm_aquifer_analyze_impact` for the delete prompt. |
| `include/ui/properties/swmmaquiferpropertyadapter.h/.cpp` | Replace stub with 12 scalar `Q_PROPERTY`s (labels via `UnitSystem`) + `evapPattern` `DataObjectRef` (Pattern, `typeLock` MONTHLY) so the Object Browser's properties page is usable; drop the stale `DA-ENG-06` comment. |

Verify: ribbon shows Aquifer left of LID Control; menu unchanged; new aquifer → save → `[AQUIFERS]` row with `ETupat`; delete → row gone and referencing subcatchments show `(none)`.

## Phase G2 — Aquifer editor with two-zone illustration

**New `include/ui/sectionview/aquiferdiagram.h` + `src/ui/sectionview/aquiferdiagram.cpp`** (pure builder, mirrors `lidlayerdiagram`):

```
struct AquiferDiagramInput {
    double porosity, wiltingPoint, fieldCapacity, conductivity, conductSlope,
           tensionSlope, upperEvapFrac, lowerEvapDepth, lowerLossCoeff,
           bottomElev, waterTableElev, upperMoisture;
    QString evapPattern, lengthLabel, rateLabel;
    int activeParam = -1;                 // AquiferProvider::Param of the focused field
    QStringList warnings;                 // D6 soft-validation texts
};
SectionDiagramModel buildAquiferDiagram(const AquiferDiagramInput &);
```

Drawing (all via existing `DiagramPoly/Ground/Arrow/Dim/Leader/Vegetation` primitives):

- Ground surface line with vegetation, labelled *ground surface (Esurf — per subcatchment)*; infiltration arrow down (**FI**), ET arrows up split into upper-zone ET (**ETu — Upper Evap. Fraction**, plus the pattern name when set) and lower-zone ET reaching **ETs — Lower Evap. Depth** (dimension line from surface).
- **Upper (unsaturated) zone** polygon, soil texture; callouts: Porosity, Wilting Point, Field Capacity, Initial Upper Moisture (Umc). Leader text renders value + unit.
- **Water table** line with dim from bottom: *Egw — Initial Water Table Elev.*; **Lower (saturated) zone** polygon (water texture).
- Percolation arrow upper→lower with callouts Ksat, Kslope, Tslope; lateral flow arrow from the saturated zone to a schematic node symbol labelled *to receiving node (per subcatchment)*; deep-percolation arrow through the bottom: *Seep — Lower Loss Coefficient*.
- **Aquifer bottom** line with dim *Ebot — Bottom Elevation*.
- Water-table position scaled between bottom and surface when `Egw > Ebot`; otherwise drawn at the bottom with `unknown` hatch and a warning callout (D6). `activeParam` bolds/highlights its leader, exactly as `activeLayer` does for LID tabs.

**`src/ui/dialogs/aquifereditordialog.cpp` rebuild** (keep the class, its statics `createNew`/`pickAquifer`, list model, and rename/uniqueness logic):

| Area | Change |
|---|---|
| Layout | Three-pane `QSplitter` (list / form / `SectionPreviewWidget`), stretch 0/2/2 as LID (`lidcontroleditordialog.cpp:79-198`). |
| Form | Group boxes instead of 12 flat rows: *Soil* (Por, WP, FC, Umc), *Conductivity* (Ksat, Kslope, Tslope), *Evaporation* (ETu, ETs, Evap. Pattern picker — `DataObjectPickerEditor` with `DataObjectRef::Pattern`, "…" → `PatternEditorDialog::pickPattern`), *Elevations & Losses* (Ebot, Egw, Seep). Unit suffixes from `UnitSystem::instance()` (`lengthLabel()`, rain-rate label) replacing the hard-coded "in/hr or mm/hr". Tooltips carry the SWMM manual one-liners. |
| Live diagram | `refreshDiagram_()` fills `AquiferDiagramInput` from widgets on every `valueChanged`/focus change, sets `activeParam` from `QApplication::focusWidget()`; call `zoomToExtents()` on bind. |
| Validation (D6) | `validationWarnings_()` → status label under the form + `warnings` in the diagram input. |
| Delete | Impact prompt from `AquiferRegistry::impactSummary` (D5). |
| Persistence | Dialog + splitter `setObjectName` retained for `DialogLayoutPersistence`; `DialogRegistry` tracks it automatically (modeless top-level). |

Tests: new `tests/gui/test_aquiferdiagram.cpp` (headless builder: leader/dim counts, water-table placement, unknown-hatch when Egw ≤ Ebot, active highlight), new `tests/gui/test_aquifer_editor_dialog.cpp` (add/rename/delete, spin ↔ provider sync, pattern round-trip to engine, warnings text), both registered in `tests/gui/CMakeLists.txt` (template: `test_landuse_unified_editor` `:1007-1029`, `test_sectionmodelbuilders` `:1678-1686`). Root `CMakeLists.txt`: add the two aquiferdiagram files next to `lidlayerdiagram` (headers `:592`, sources `:1258`).

## Phase G3 — Subcatchment receiving-aquifer picker

| File | Change |
|---|---|
| `include/ui/properties/dataobjectref.h` | `Kind::Aquifer = 11` ("[AQUIFERS] entries; '…' opens `AquiferEditorDialog::pickAquifer`"). |
| `src/ui/properties/dataobjectpickereditor.cpp:75, 220, 251` | Populate from `swmm_aquifer_count/id`; map to `DataAquifers`; "…" branch calling `AquiferEditorDialog::pickAquifer` (exists, `aquifereditordialog.cpp:283-306`). |
| `include/ui/properties/swmmsubcatchpropertyadapter.h/.cpp` | New `Q_PROPERTY(DataObjectRef aquifer READ aquiferRef WRITE setAquiferRef NOTIFY changed)` placed just above `groundwater`; setter → `swmm_subcatch_set_aquifer` (`-1` on empty), `markEdited`. |
| `src/ui/panels/swmmattributetablemodel.cpp:~923` | Picker column `subcatch_aquifer` (same `DataObjectRef` cell pattern as rain gage, `:902`), left of the Groundwater compound column. |
| `src/ui/dialogs/subcatchcompoundeditdialog.cpp` | Nothing — aquifer choice moves out of the Groundwater page in G4. |

Tests: extend `tests/gui/test_nonspatial_adapters.cpp` / subcatch adapter test for the new ref; attribute-table column test if one exists for `subcatch_raingage`.

## Phase G4 — Groundwater Exchange editor (properties grid + attribute table)

**New widget `include/ui/widgets/gwfexpressionedit.h` + `.cpp`** — a copy of `TreatmentExpressionEdit` (`treatmentexpressionedit.h:34-122`) with:

- `GwfSyntaxHighlighter` whose variable/function lists come from `swmm_gwf_variable_*` / `swmm_mathexpr_function_*` (E1) — no hard-coded vocabulary in the GUI.
- `QCompleter` items carry a description column (e.g. `HGW — water table height above aquifer bottom (ft)`), units switched by `UnitSystem`; Ctrl+Space or 2-char prefix, as today.
- Debounced `swmm_gwf_validate_expression`; `validationChanged(ok, msg, col)`; error column underlined via the highlighter's extra selection (existing idiom).
- `GwfExpressionDelegate` only if a table cell is needed (not planned — the dialog uses two inline editors).

**New dialog `include/ui/dialogs/groundwaterexchangedialog.h` + `src/ui/dialogs/groundwaterexchangedialog.cpp`**, constructed from a `SubcatchCompoundEditRef` (kind `Groundwater`):

| Group | Contents |
|---|---|
| Header | Subcatchment name; aquifer name (read-only link "Change… " → G3 picker) — the aquifer is chosen on the subcatchment, not here (D2). |
| *Receiving node* | `DataObjectPickerEditor` (`DataObjectRef::Node`); Surface Elevation (unit suffix). |
| *Standard lateral flow* | A1, B1, A2, B2, A3, Twgr (threshold water-table elev.), Hstar (channel bottom) with the formula shown as static rich text: `Q_lat = A1·(HGW−H*)^B1 − A2·(HSW−H*)^B2 + A3·HGW·HSW`. |
| *Custom expressions* | Two `GwfExpressionEdit` rows: **LATERAL** ("added to the standard flow") and **DEEP** ("replaces the standard deep percolation"); per-row status line with the validator message; "Insert variable" popup listing the 11 variables with descriptions (same as the completer source). |
| Buttons | Apply / Close. Apply = the existing page's writes (`swmm_subcatch_set_gw_node`, `set_gw_params`) + `swmm_subcatch_set_gwf_expression` ×2; refuses to apply while either expression is invalid (button disabled + tooltip). Enabled-state gated on the aquifer being set (else a hint "Assign an aquifer to enable groundwater exchange"). |

Wiring:

| File | Change |
|---|---|
| `src/ui/properties/subcatchcompoundeditbutton.cpp:41` (`onClicked`) and `src/ui/panels/attributedelegates.cpp:246-263` | `Kind::Groundwater` → open `GroundwaterExchangeDialog` (modeless, `WA_DeleteOnClose`); other kinds unchanged. |
| `src/ui/dialogs/subcatchcompoundeditdialog.cpp:182-243, 407-441` | Remove the Groundwater page and its members (D2). |
| `src/ui/properties/swmmsubcatchpropertyadapter.cpp:533-543`, `src/ui/panels/swmmattributetablemodel.cpp:2741-2745` | Summary text becomes `AQ1 → J12` / `AQ1 → J12 (custom)` / `(none)`; factor into one helper `groundwaterSummary(engine, idx)` so both views agree. |
| `src/ui/panels/attributedelegates.cpp:237-242` | `displayText` uses the same summary. |
| `CMakeLists.txt`, `tests/gui/CMakeLists.txt` | Add the four new files; tests below. |

Tests: `tests/gui/test_gwfexpressionedit.cpp` (completer proposals for `H`, `th`; validator error column; empty text OK), `tests/gui/test_groundwaterexchangedialog.cpp` (loads existing values from a fixture `tests/gui/data/gw_exchange_fixture.inp` with `[AQUIFERS]/[GROUNDWATER]/[GWF]`; Apply writes params + expressions; Apply disabled on invalid expression; summary strings). Existing `test_treatment_expression_editor.cpp` is the template.

## Phase G5 — Node-side summary row (D3)

| File | Change |
|---|---|
| New `include/layers/gwsourcesummary.h` + `src/layers/gwsourcesummary.cpp` | `QStringList groundwaterSourceSubcatchments(SWMM_Engine, int nodeIdx)` — scan subcatchments whose `gw_node == nodeIdx` and `gw_aquifer >= 0` (pattern: `vjsourcesummary` in the VJ plan). |
| `include/ui/properties/swmmnodepropertyadapter.h/.cpp` | Read-only `groundwaterInflow` row ("from S1, S3") on all node kinds incl. VJ; a `NodeCompoundEditRef` `Kind::GroundwaterSources` whose button pops a chooser of those subcatchments and opens `GroundwaterExchangeDialog` for the pick. |
| `src/ui/panels/swmmattributetablemodel.cpp:2775-2810` | Same cell on node rows. |
| `src/map/tools/maptoolselect.cpp` delete prompts | Append "N subcatchment(s) discharge groundwater to this node; they will lose their receiving node" (engine already nulls `gw_node`, writer then omits the `[GROUNDWATER]` row with a warning — surface that). |

Tests: `tests/gui/test_gwsourcesummary.cpp`; `test_nodepropertyadapter.cpp` gains the new row.

## Phase G6 — Docs, changelog, verification

- `docs/manual/06_object_browser.md` (Aquifer editor section with a screenshot of the illustration), new `docs/manual/13_groundwater.md` (aquifers, subcatchment assignment, exchange editor, expression grammar table with the 11 variables and 21 functions, LATERAL-adds / DEEP-replaces semantics). Link from `manual.md`.
- `CHANGELOG.md` (GUI) + engine changelog entry (E1).
- Manual round-trip: New Aquifer from the ribbon → fill via form, watch callouts → assign to S1 via the properties-grid Aquifer row → open Groundwater "Edit…" from the attribute table → pick J1, set A1/B1, type `0.001*(HGW-10)` LATERAL (completer offers `HGW` after `HG`), misspell `HGWW` and confirm the red underline + disabled Apply → Apply → save → `[AQUIFERS]`, `[GROUNDWATER]`, `[GWF]` rows present → reopen → all values and the expression reload → run under the new engine and confirm groundwater flow appears at J1 → delete the aquifer → impact prompt → S1 shows `(none)`.

## Sequencing

```
E1 (engine API + validator)        → verify: engine tests; install
G1 (ribbon, registry, pattern)     → verify: ribbon order, delete reaches engine       [no E1 dependency]
G2 (diagram + editor rebuild)      → verify: diagram + dialog tests                     [no E1 dependency]
G3 (aquifer picker)                → verify: adapter/table tests                        [no E1 dependency]
G4 (exchange editor + GWF edit)    → verify: widget/dialog tests, manual round-trip     [needs E1 or D1 shim]
G5 (node summary)                  → verify: summary test                               [after G4]
G6 (docs, changelog, round-trip)
```

G1–G3 can proceed while E1 is reviewed. Estimated size: E1 ~400 lines engine + bindings; G2 ~700 (diagram ~350, dialog rework ~350); G4 ~800; G1/G3/G5 ~150 each.
