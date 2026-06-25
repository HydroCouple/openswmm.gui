# Attribute Editor Wiring Plan — 2026-06-04

Goal: every attribute carried by a SWMM model object is configurable from the
Property Browser (`AttributePanel`) and, where the legacy SWMM-GUI exposed it
tabularly, from the Attribute Table (`SWMMAttributeTableModel`) — with the
right editor per type: inline combobox for enum codes, pickers for named-object
references, compound dialogs only for genuinely structured data.

Primal objective per `CLAUDE.md` §4.01: legacy alignment. Parity source of
truth is `SWMM-GUI/Epaswmm5/objprops.txt` (row lists per object) and the
`D*.pas` dialogs. This plan extends — does not replace — the slice roadmap in
`docs/GUI_IMPLEMENTATION_PLAN.md` (slices BN–BU) and the gap items G1–G17 in
`docs/EDITOR_PARITY_GAP_ANALYSIS.md`.

---

## 1. Architecture recap (the four wiring recipes)

All editing flows through one of four existing mechanisms. New work should
reuse these; no new frameworks.

**R1 — Scalar Q_PROPERTY (Property Browser).**
Adapter in `src/ui/properties/swmm*propertyadapter.cpp`: add Q_PROPERTY +
getter calling `swmm_*_get_*`, setter calling the engine (or the
`SWMMModelLayer::apply*` MVC helper when map/table must refresh), emit
`changed()`, add a `displayLabelFor()` case. QPropertyModel renders
spinbox/lineedit automatically.

**R2 — Enum combobox.**
Property Browser: declare the enum with `Q_ENUM` on the adapter (pattern:
`SWMMLinkPropertyAdapter::WeirType`); QPropertyModel renders a combobox of key
names. When key names are too cryptic for users (long descriptive code lists),
use a small value-type + custom editor instead (see R4 / Phase 0 culvert
pattern). Attribute Table: `enumCol()` + a `*Values()` pair-list builder in
`swmmattributetablemodel.cpp`, plus a `SetterEntry` in `setterFor()`.

**R3 — Named-object reference (curve / time series / transect / pattern…).**
`DataObjectRef`-typed Q_PROPERTY → `DataObjectPickerEditor` (combo + "…"
browse), already registered in `attributepanel.cpp`. Pattern:
`SWMMPumpPropertyAdapter::pumpCurve`.

**R4 — Custom value type + custom inline editor.**
For cells that need a bespoke widget: value struct with
`Q_DECLARE_METATYPE` + `QMetaType::registerConverter<T, QString>` (display
text) + editor widget with `Q_PROPERTY(T value … USER true)` + registration
via `delegate->registerCustomTypeEditorCreator(...)` in `attributepanel.cpp`.
Compound dialogs (`NodeCompoundEditDialog`, `LinkCompoundEditDialog`) remain
only for genuinely multi-field data (xsection geometry, inflows, DWF, RDII,
treatment, inlet usage).

Attribute Table edits all route through the setter-tag dispatch table in
`swmmattributetablemodel.cpp` (`setterFor()`), keeping one write path per
attribute.

---

## 2. Phase 0 — Culvert code combobox + encoding fix (DONE 2026-06-04)

Problem 1: culvert code required opening `LinkCompoundEditDialog` (a separate
GUI) from the Property Browser. It is a single enum code — a combobox suffices.
Problem 2: the dialog's combobox labels were built with
`QString::fromLatin1(c.label)` on literals containing U+2014 (—), producing
mojibake ("â€”…") — the illegible symbol reported in that combobox.
Problem 3: the attribute table's culvert combo listed only 5 of the 57
HDS-5 codes.

Changes:

- `include/ui/properties/culvertcodes.h` + `src/ui/properties/culvertcodes.cpp`
  (NEW): single source of truth for all 57 FHWA HDS-5 culvert codes + group
  names, transcribed from legacy `SWMM-GUI/Epaswmm5/Dculvert.dfm` (TreeView
  NodeData). ASCII-only labels (no em-dashes/multiplication signs) so no
  encoding path can corrupt them. Helpers: `culvertCodes()`,
  `culvertCodeLabel(code)`, `culvertCodeComboLabel(code)`.
- `include/ui/properties/culvertcoderef.h` + `.cpp` (NEW): `CulvertCodeRef`
  value type {engine, layer, linkName, code} + QString display converter,
  mirroring `LinkCompoundEditRef`'s registration dance.
- `include/ui/properties/culvertcodecombobox.h` + `.cpp` (NEW): QComboBox
  editor with `Q_PROPERTY(CulvertCodeRef value … USER true)`; applies
  selection immediately via `SWMMModelLayer::applyLinkCulvertCode` (engine
  fallback for standalone use), then emits `valueChanged()` — same
  apply-as-you-go contract the dialog page had.
- `swmmlinkpropertyadapter.h/.cpp`: `culvertCode` Q_PROPERTY retyped
  `LinkCompoundEditRef` → `CulvertCodeRef`; summary now shows the descriptive
  label, not just "Code N".
- `attributepanel.cpp`: metatype/converter/editor-creator registration for
  `CulvertCodeRef`.
- `linkcompoundeditdialog.h/.cpp`: culvert page removed (struct
  `CulvertCode`, `kCulvertCodes`, `buildCulvertCodePage`, `m_cvCodeCombo`,
  `m_cvSuppressApply`). Dialog now hosts XSection + InletUsage only.
- `linkcompoundeditref.h`: `Kind::CulvertCode` removed.
- `swmmattributetablemodel.cpp`: `culvertCodeValues()` now built from the
  shared table — full 0/1–57 list in both views.
- `CMakeLists.txt`: new sources/headers listed.

Verify: build; select a conduit → Property Browser "Culvert Code" row shows
the descriptive label and edits inline via combobox (no dialog); Attribute
Table conduit "Culvert code" column offers all 57 codes; labels legible
(no mojibake); value round-trips to `[XSECTIONS]` culvert field on save.

Encoding hygiene rule (repo-wide, from this bug): never pass a string literal
containing non-ASCII through `QString::fromLatin1`; use `QString::fromUtf8`,
`QStringLiteral`, or keep the literal ASCII. MSVC `/utf-8` is already set
(CMakeLists.txt:47). Audit hits when touching files: `grep -n "fromLatin1"`
and check the argument for non-ASCII bytes.

---

## 3. Phase 1 — Link attribute-table parity (DONE 2026-06-04)

The Property Browser link adapters (Slices SB/SD) were ahead of the table.
`schemaForCategory()` per link category is now at `objprops.txt` parity:

- Conduits: added Tag, initialFlow, maxFlow, lossInlet, lossOutlet, lossAvg
  (via read-modify-write wrappers over the engine's atomic triple
  `swmm_link_get/set_loss_coeff`), seepRate, and Barrels (first
  `EditorKind::Integer` column; new `intCol()` helper).
- Pumps: added Tag, startupDepth, shutoffDepth (BN-LINK-05), and (same-day
  follow-up) the Pump Curve picker cell: `CompoundEditDelegate` now also
  dispatches `DataObjectRef` cells to `DataObjectPickerEditor`; the engine
  write happens in `commitValueDirect` (`swmm_link_set_pump_curve`) because
  the picker carries no setter callback by design.
- All categories (same-day follow-up): per-object "User Flags" compound
  cell (UserFlagsEditRef → UserFlagValuesDialog), Property Browser parity,
  appended ahead of the per-flag columns in `appendUserFlagColumns()`.
- Orifices: added Tag, Type enum (SIDE/BOTTOM), Open/Close Rate (raw 1/s
  per engine; hours-display conversion deferred, matches browser row).
- Weirs: added Tag, Type enum (TRANSVERSE/SIDEFLOW/V-NOTCH/TRAPEZOIDAL/
  ROADWAY). Follow-up: roadway width/surface + end coeff + coeff curve
  rows once engine accessors exist.
- Outlets: added Tag, Rating Curve enum (legacy display order
  FUNCTIONAL/DEPTH, TABULAR/DEPTH, FUNCTIONAL/HEAD, TABULAR/HEAD with
  engine numeric encoding), Coefficient (shared cd field), Exponent.
- New dispatch entries: `link_tag` (string path via
  `swmm_link_set/get_tag`) plus one SetterEntry per column above.

Verify (manual, per category): edit each new cell, confirm engine value via
the browser and `.inp` export; new-tag→dispatch consistency was audited
statically (every schema tag resolves in `setterFor()`).

Test posture note: there is no attribute-table-model test target today, and
the two closest precedents (`test_linkpropertyadapter`,
`test_control_rule_models`) are disabled in `tests/gui/CMakeLists.txt`
because linking the SWMMModelLayer chain is too heavy. Engine round-trips
for every accessor the new columns call are already covered by
`test_linkpropertyadapter` (initialFlow/maxFlow/seepRate/barrels/loss
triple/orificeType/…) pending its re-enablement. A lightweight
schema-contract test becomes feasible once the layer-stub follow-up named
in that CMakeLists TODO lands.

## 3b. Browser→table parity pass (DONE 2026-06-04)

Full audit of every adapter Q_PROPERTY vs `schemaForCategory()`; all gaps
closed so the table now mirrors the Property Browser row-for-row (editable
rows; sim-time/derived ro rows like gage currentRainfall stay browser-only):

- Weirs: offsetUp/offsetDn. Outlets: tabular rating-curve picker (shares
  the `link_pump_curve_ref` tag — engine shares the curve slot). Conduits:
  inlet-usage cell. All link categories: From/To Node read-only columns
  (resolved live from the engine; identifyByName carries no endpoint keys).
- Outfalls: fixed stage (type-guarded getter wrapper over the shared
  `outfall_param` union slot), tidal-curve + stage-time-series pickers
  (write path dispatched by tag in `commitValueDirect`; engine setters flip
  the outfall type — same invariant as the browser), and the four node
  compound cells. Storage/Dividers: four compound cells; dividers also got
  ponded area.
- Rain gages: Rain Type + Data Source enum combos and the Rain File path
  (name-keyed `swmm_file_path_get/set` adapted to the (engine, idx)
  dispatch shape via `swmm_gage_id`).

## 4. Phase 2 — Node gaps

- Outfalls: ensure stage elevation (numeric), tidal curve + stage time series
  (R3 pickers, conditionally enabled on outfall type) are present in browser
  AND table (Slice DA.4.3 started the browser side); table needs
  outfall-specific columns. [DONE — see §3b]
- Storage: storage curve (R3 picker: functional vs tabular per legacy —
  functional coefficients as numerics) [DONE — Slice AG.4, see below],
  exfiltration/seepage parameter rows (suction, conductivity, initial
  deficit) wherever engine accessors exist; file engine-API gap tickets
  where they don't. [exfiltration rows still pending — engine accessors
  `swmm_node_get/set_exfil_params` exist (openswmm_nodes.h:416/427)]

  Slice AG.4 (storage geometry, browser + table parity):
  - `DataObjectRef::StorageCurve` kind (engine table type 1 = CURVE_STORAGE)
    added to `dataobjectref.h`; picker populate + "…" browse cases wired in
    `dataobjectpickereditor.cpp` and the two mirror switches in
    `attributepanel.cpp` / `attributetablepanel.cpp`.
  - `SWMMStoragePropertyAdapter`: `StorageShape` enum (Functional/Tabular,
    derived from whether `storage_curve >= 0`), `storageCurve` picker, and
    `storageCoeffA/ExpB/ConstC` rows over the engine's atomic
    `swmm_node_get/set_storage_functional` triple (read-modify-write per row).
    `AttributePanel` greys curve vs. coefficient rows by live shape, mirroring
    the outfall stage-data wiring.
  - Attribute table (`swmmattributetablemodel.cpp`): Storage Shape enum,
    Storage Curve picker (`node_storage_curve_ref`), and the three coefficient
    columns, with read-modify-write wrappers + a whole-row repaint on the
    shape flip so the sibling curve cell tracks the change.
  - Tests: `tests/gui/test_nodepropertyadapter.cpp` —
    `storageFunctionalRoundTrip`, `storageTabularRoundTripAndShapeSwitch`,
    `storageGeometryPropsWritableViaMetaObject` (real-engine, no mocks).
- Junctions: already near-parity; confirm ponded area + surcharge depth in
  both views.

## 5. Phase 3 — Subcatchment gaps (DONE 2026-06-18)

All R1/R3 unless noted; these were the largest legacy-parity holes
(`objprops.txt` Subcatchment rows). All now wired in BOTH the Property
Browser (`SWMMSubcatchPropertyAdapter`) and the Attribute Table
(`schemaForCategory(CatSubcatchments)` + `setterFor` + `commitValueDirect`):

- Rain gage assignment (R3 picker over gage names). [DONE — reuses
  `DataObjectRef::RainGage`; adapter `rainGageRef`/`setRainGageRef`,
  table tag `subcatch_rain_gage_ref`.]
- Outlet assignment (combined node + subcatchment picker). [DONE — new
  `DataObjectRef::SubcatchOutlet` kind listing nodes then subcatchments;
  the WRITE slot resolves the name to `swmm_subcatch_set_outlet` (node) or
  `set_outlet_subcatch` (cascade), node taking precedence. Read prefers the
  cascade outlet (`get_outlet_subcatch >= 0`) then the node.]
- Infiltration model (R2 enum: Horton/Mod-Horton/Green-Ampt/Mod-GA/CN) +
  per-model parameter rows with conditional greying (mirrors the storage
  Functional/Tabular pattern in `AttributePanel`). [DONE — required a new
  engine setter `swmm_subcatch_set_infil_model` (see engine note below). The
  per-row param setters read-modify-write one term and restore the model code
  so Mod-Horton/Mod-GA aren't demoted by the canonical `set_infil_*` calls.]
- Land-use coverage, Groundwater linkage, LID usage — all three are tabs of
  the new `SubcatchCompoundEditDialog` (R4), opened from a
  `SubcatchCompoundEditRef` cell (mirror of `NodeCompoundEditRef`). [DONE —
  apply-as-you-go pages: coverage via `swmm_subcatch_get/set_coverage`;
  groundwater via the new aquifer/gw-node/gw-params engine API; LID via the
  newly-implemented `swmm_lid_usage_add/count/get/remove`.]

Engine-API gap closure (openswmm.engine, 2026-06-18) — these accessors did
not exist and were added (declarations + impl + GoogleTest round-trips in
`tests/unit/engine/test_subcatch_editor_api.cpp`):
- `swmm_subcatch_set_infil_model` (model code was read-only).
- `swmm_subcatch_set/get_aquifer`, `set/get_gw_node`, `set/get_gw_params`
  (no subcatch→aquifer linkage or `[GROUNDWATER]` routing param API existed;
  values stored raw to match `HydrologyHandler::handle_groundwater`).
- `swmm_lid_usage_add` (was a stub) now stores rows; added
  `swmm_lid_usage_count/get/remove` over the existing `LidUsageStore` SoA.

Still deferred (engine accessors absent): LID-control layer getters
(`set_surface/soil/storage/drain` have no readback), and the
`subcatch_landuse_ref`/etc. table cells reuse the compound no-op write path
(the dialog performs the engine writes). Curb length / N-Perv pattern remain
as future rows.

## 6. Phase 4 — Rain gages + data objects

- Rain gage: rain interval, time-series assignment (R3), scale factor,
  editable X/Y in table (engine setters exist).
- Curves/Patterns/Time Series adapters: keep point editing in their dedicated
  editors (BQ), but surface type/metadata rows as editable where engine
  allows (e.g., curve type enum).

## 7. Phase 5 — Stub adapters (currently name-only)

Flesh out, in legacy-parity row order, using R1/R2/R3:
aquifers (~14 scalar rows), snow packs (3 sub-pack parameter groups —
compound or grouped rows), LID controls (per-layer structured data — dedicated
editor dialog per GUI_IMPLEMENTATION_PLAN Slice BP), inlets + streets
(scalar rows are declared as labels already — wire getters/setters),
pollutant buildup/washoff per land use (compound tab on land use adapter).
File engine-API gap tickets for any missing accessor rather than bypassing
the engine.

## 8. Editor inventory for complex types (target state)

| Type | Editor | Recipe |
|---|---|---|
| Enum codes (outfall/divider/orifice/weir/outlet-rating/infil model, flap gate, units…) | inline combobox | R2 |
| Culvert code (57 HDS-5) | inline combobox, shared table | R4 (done, Phase 0) |
| Named refs (curves, time series, transects, patterns, aquifers, snow packs, gages, co-pollutant) | picker combo + browse | R3 |
| XSection (shape + geom1–4 + barrels) | LinkCompoundEditDialog (rich shape editor, §S.11) | R4 |
| Inflows / DWF / RDII / Treatment | NodeCompoundEditDialog tabs | R4 |
| Inlet usage | LinkCompoundEditDialog page (Slice BO 6.5.8) | R4 |
| Land-use coverage / groundwater / LID usage | new SubcatchCompoundEditDialog tabs | R4 |
| Tabular data (curve/TS/pattern/transect/hydrograph points) | dedicated editors (BQ) | existing |
| User flags | UserFlagsEditButton | existing |
| Coordinates | numeric cells routed via applyNodeMove | existing |

## 9. Ordering and verification

Order: Phase 0 (done) → 1 → 2 → 3 → 4 → 5. Each phase:

1. Diff target rows against `objprops.txt` → verify: row lists match.
2. Implement adapter/table wiring → verify: build + manual round-trip edit.
3. `.inp` save/reload → verify: edited values persist and re-display.
4. Cross-view sync → verify: edit in table reflects in browser and map
   without reload (MVC `attributeChanged` path), per CLAUDE.md §5.1.

Tests: extend the existing attribute-table model tests with one
set/get round-trip per new setter tag; test outputs written under
`docs/test_output/` (transparent file IO rule), not temp dirs.
