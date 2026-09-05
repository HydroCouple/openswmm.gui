# Inlets Editor, Inlet Junction, and Model-Toolbar Cleanup — GUI Work Plan

**Status:** IMPLEMENTED (G1–G5, unverified build) 2026-09-05 — see `workplans/handoffs/INLET_JUNCTION_GUI_VERIFICATION_HANDOFF_2026-09-05.md` for the build/test/click-through/commit procedure, the deferred items, and the file inventory. G6 (manual chapter, CHANGELOG) not started.
**Companion engine plan:** `openswmm.engine/plans/INLET_JUNCTION_IMPLEMENTATION_PLAN_2026-09-05.md` (Phases E1–E5 are prerequisites as noted per phase below)
**Builds on:** `VIRTUAL_JUNCTION_GUI_PLAN_2026-08-01.md` (decisions D-G1..D-G3 carry over), `VJ_LATERAL_INFLOW_GUI_PLAN_2026-09-04.md`, `SECTION_PREVIEW_WORKPLAN.md`, `DIALOG_WINDOW_MANAGEMENT_PLAN_2026-08-16.md`

Three deliverables:

1. **Inlets editor** — a Transect-grade editor (list | properties | high-fidelity drawing with dimension callouts) replacing the current two-pane `InletEditorDialog`, launched from Model → Data Objects immediately after Transects.
2. **Inlet junction** — a new node kind inserted by splitting a STREET conduit (or converting a junction between two same-section STREET conduits), with its own icon in the Nodes toolbar group, a dashed connector to its capture (underdrain) node, property panel, undo, and rendering.
3. **Model toolbar cleanup** — Subcatchment, Rain Gage and Text each become their own captioned group; Streets and Inlets join the Data Objects ribbon group.

MVC contract unchanged (CLAUDE.md §5.1): the engine SoA is the model, `SWMMModelLayer` is the single mutation surface, every mutation is an `apply*` wrapped in a `MapCommand`, views sync via `geometryChanged` / `attributeChanged` / `repaintRequested`.

---

## 1. Current state (verified 2026-09-05)

| Area | State | Anchors |
|---|---|---|
| Data Objects menu | Built programmatically; entry order: Time Series, Curves, Patterns ¦ LID, Pollutants, Land Uses ¦ Aquifers, Snowpacks ¦ Control Rules, **Transects**, Unit Hydrographs ¦ Streets, **Inlets** | `src/swmmvis.cpp:3739-3821` (`kEntries` :3770-3785) |
| Inlets editor | Exists: `InletEditorDialog` — list + `QFormLayout` (Name, Type combo GRATE/CURB/SLOTTED/CUSTOM, Length, Width, Grate Type line-edit, Open Area, Splash). No undo, no preview, no throat, no curb height, no COMBO, no drop types, no custom curve picker, no `floatingPanelFlags()`, no `openForInlet`/`pickInlet`, no `SWMMObjectRef::Inlet` branch in the object browser. Header comments and the "engine cannot read back" label are stale — `swmm_inlet_get_params`/`get_type` exist | `include/ui/dialogs/inleteditordialog.h`, `src/ui/dialogs/inleteditordialog.cpp:36-69,126-131`; `src/inlet/inletregistry.cpp:80-140`; `src/ui/panels/objectbrowserpanel.cpp:677-690` |
| Transect editor (model) | 3-pane `QSplitter` 1:3:3 — list+Add/Delete ¦ name, comments, `QPropertyModel` property tree, station table ¦ toolbar + `TransectChartView`; `QUndoCommand`s in `transectundocommands.h`; `createNew`/`openForTransect`/`pickTransect` entry points; `floatingPanelFlags()` + `objectName` persistence | `src/ui/dialogs/transecteditordialog.cpp:57-152,164-421`; `include/transect/transectundocommands.h` |
| Street editor preview | `StreetSectionPreview : QWidget` plain `paintEvent` driven by a provider — the pattern for a parametric drawing | `include/ui/dialogs/streeteditordialog.h:60-71` |
| Inlet usage on conduits | Compound row "Inlets" on the conduit adapter; dialog page is a placeholder label; count hardcoded absent (BN-LINK-11) | `swmmlinkpropertyadapter.cpp:491-500`; `linkcompoundeditdialog.cpp:721-736` |
| Toolbar | `mToolBarModel` groups: Select, Edit, Nodes {Junction, Virtual Junction, Outfall, Divider, Storage}, Links, **Draw {Subcatchment, Rain Gauge, Text}**, Climate (family), Data Objects {…no Street/Inlet…}, Setup, Tools, Mesh 2D. Grouping *is* the separator (`RibbonGroup`) | `src/swmmvisactions.cpp:282-314`; face labels `:163-243` |
| Node kinds | `SWMMNodePropertyAdapter::NodeKind {Junction, Outfall, Storage, Divider, VirtualJunction}`; adapter switch probes `swmm_node_is_virtual` before kind | `swmmnodepropertyadapter.h:62-63`; `propertiespanel.cpp:619-660` |
| VJ rendering | Shares `CatJunctions` (D-G1); `m_virtualJunctionSym`; QSG bucket swap on `isVirtual`; symbology row `model.virtualjunctions`; prefs key `virtual_junction`; profile-plot dashed outline | `swmmmodellayer.h:2466`; `swmmlayerqsgrenderer.cpp:1289-1340`; `kindtreesymbologypanel.cpp:235-249`; `profileplotoptions.cpp:35-37` |
| Dashed lines | `SWMMElementSymbol` has no pen style; no dashed model link exists. Dashing exists in `LineSymbolLayerSpec` (`penStyle`, `customDash`) and as ad-hoc `QPen(Qt::CustomDashLine)` in results layers | `swmmmodellayer.h:74-115`; `render/linesymbollayer.h:106-145`; `swmmresultslayer.cpp:308-320` |
| VJ tool/undo | `OpenSWMMVisMapToolAddVirtualNode` (conduit-hit only, D-G3); `InsertVirtualJunctionCommand` id 22, `FuseVirtualJunctionCommand` id 23; next free ids 24+ (18–20 also free; 12/13 are duplicated — do not reuse) | `map/tools/maptooladdvirtualnode.*`; `mapundostack.h:960-1017` |
| Icons | `#777777` gray family is recolored by `IconFactory`; `virtual_junction.svg` = dashed open circle; `inlet.svg` = rect with three bars; `junction.svg` = solid open circle | `iconfactory.cpp:21-41`; `resources/images/` |
| Capability gating | No manifest yet (MULTI_ENGINE V2 not done); pattern = probe an API once, cache tri-state, disable + tooltip | `simulationoptionsdialog.cpp:198,232`; `REACTION_EDITOR_GATE_HANDOFF` |
| Docs | `docs/inlet-tutorial/` complete (EPA-derived, includes HEC-22 method write-up and inlet-type images); `docs/manual/` has zero inlet content | `docs/inlet-tutorial/inlet-tutorial.md` |

---

## 2. Deliverable 1 — Inlets editor

### 2.1 Menu placement

Move the two street-drainage entries so the menu reads … Control Rules, Transects, **Streets, Inlets** ¦ Unit Hydrographs … (`kEntries`, `src/swmmvis.cpp:3770-3785`). Streets precede Inlets because an inlet design is only meaningful against a street section (legacy browser order is Streets → Inlets → Transects, `Ubrowser.pas`). Icon stays `Inlet` (`inlet.svg`); redraw it as a curb-and-grate glyph if the current three-bar rectangle reads as "table" next to the new node icon (**D-G4**). Keep `actionNewInlet` objectName so the catalog row (`actioncatalog.h:140`) and existing tests hold; set its ribbon-tab field to `"model"` so it can join the ribbon group (§4).

### 2.2 Dialog structure (modeled on `TransectEditorDialog`)

`InletEditorDialog` is rebuilt in place (same class/file names, same `objectName`s so layout persistence carries over):

- Ctor: `QDialog(parent, floatingPanelFlags())`, non-modal, `resize(1180, 760)`, `QSplitter` 1:3:3 with `objectName("main")`, `QStatusBar` with a permanent hint label.
- **Left pane** — `QListView` on `InletListModel` (existing), Add/Delete with `IconFactory` `Add`/`Delete`. Delete warns which conduits/inlet junctions reference the design (via `swmm_inlet_analyze_impact`, `openswmm_edit.h:182`).
- **Middle pane** — Name `QLineEdit`; Description `QTextEdit`; **Inlet Type** combo `GRATE, CURB OPENING, COMBINATION, SLOTTED DRAIN, DROP GRATE, DROP CURB, CUSTOM` (legacy labels, `Uinlet.pas:31-44`); a `QPropertyModel` tree over a new `InletPropertyBag` whose groups are shown/hidden by type, exactly the legacy tab-visibility table (`Dinlet.pas:241-298`):

  | Group | Properties | Shown for |
  |---|---|---|
  | Grate | Grate Type (combo of 8), Length, Width, Open Fraction*, Splash-over Velocity* | GRATE, COMBINATION, DROP GRATE (*GENERIC only) |
  | Curb Opening | Length, Height, Throat Angle (VERTICAL/INCLINED/HORIZONTAL) | CURB, COMBINATION, DROP CURB (throat hidden for DROP CURB) |
  | Slotted Drain | Length, Width | SLOTTED |
  | Custom | Curve kind (Diversion: captured vs approach flow ¦ Rating: captured vs depth), Curve (picker → Curves editor `pickCurve`) | CUSTOM |

  Units from the project unit system (ft/m, ft/s / m/s) in the property labels, as `Dinlet.pas:324-350`. Validation the legacy dialog lacks: lengths/widths/height > 0, open fraction in (0,1], unique name (`hasName` pre-check to avoid no-op undo entries — `transecteditordialog.cpp:572-580`), custom curve type must match the chosen kind (legacy `Dinlet.pas:111-169`).
- **Right pane** — toolbar (Fit, Zoom In/Out, Copy, Export Image; no edit modes) + `InletDrawingView` (§2.3).
- Entry points: `createNew(...)`, `openForInlet(name)`, static `pickInlet(registry, layer, undoStack, parent, compatibleShape)` returning a name (for the usage editors), plus the `SWMMObjectRef::Inlet` branch in `objectbrowserpanel.cpp` and the properties-panel data-object row.
- Undo: `include/inlet/inletundocommands.h` — `SetInletTypeCommand`, `SetInletParamsCommand` (bag snapshot), `SetInletCurveCommand`, `RenameInletCommand`, `SetCommentsCommand`; `if (m_undoStack) push else direct`, as the transect dialog.
- Provider/registry: `InletProvider` gains the full field set (grate/curb/slotted/custom groups + throat + curve kind); `InletRegistry::loadFromEngine` reads values via `swmm_inlet_get_params2` (engine E2) and drops the dirty-flag/skip logic and stale comments.

### 2.3 `InletDrawingView` — high-fidelity drawing with dimension callouts

New `include/ui/widgets/inletdrawingview.h` / `src/ui/widgets/inletdrawingview.cpp`. A `QGraphicsView` over a `QGraphicsScene` rebuilt from the provider on every `paramsChanged()` (a scene rather than a raw `paintEvent` so zoom/pan/export reuse `SectionDiagramScene`-style plumbing; see `SECTION_PREVIEW_WORKPLAN.md` §2 for the sampler/scene split). Pure function `InletSceneBuilder::build(const InletProvider&, UnitSystem, const InletTheme&) → QGraphicsScene*` so it is unit-testable and reusable by the property panel and the identify popup.

Per type (engineering-drawing style, theme colours from `IconFactory` palette, hatched pavement, thin dimension lines with arrowheads and text, all sizes to scale from the entered values with a minimum on-screen size):

| Type | Views | Dimensioned callouts |
|---|---|---|
| GRATE | Plan (bars drawn per grate type: parallel bars at the P_BAR spacing, curved vane arcs, tilt-bar hatch, reticuline lattice, GENERIC = dotted fill labelled with open fraction) + Section (gutter with grate flush, `Sx`, `Sw`) | L, W, bar spacing note, open-area %, `V₀` for GENERIC |
| CURB OPENING | Elevation (curb face with opening) + Section (throat drawn at 90°/45°/0° per throat angle) | L, h, throat angle |
| COMBINATION | Plan (grate + curb, sweeper length `L_curb − L_grate` shaded) + Section | L_grate, W, L_curb, h, sweeper length |
| SLOTTED DRAIN | Plan + Section (slot in pavement) | L, w |
| DROP GRATE | Plan + Section in a trapezoidal/rectangular channel | L, W, perimeter note |
| DROP CURB | Plan (4-sided opening) + Section | L (×4 sides), h |
| CUSTOM | The curve plotted (captured vs approach flow, or captured vs depth) with axes in project units | curve name, kind |

A "Local depression" overlay (a, W_local) is **not** drawn here — it is a usage property, drawn in the usage editors' small preview (§3.5). Street context (curb height, gutter depression) is taken from a "Preview with street:" picker defaulting to the first street, purely visual.

Verification: golden-image tests under `tests/ui/inlet_drawing/` for each type in both themes (`test_artifacts/` convention), plus a unit test that every dimension callout text equals the provider value formatted in the active unit system.

### 2.4 Inlet usage editing on conduits (closes BN-LINK-11)

Replace the placeholder page in `LinkCompoundEditDialog::buildInletUsagePage()` with the legacy 8-row form (`Dinletusage.pas:72-98`): Inlet design (picker filtered by conduit shape compatibility — STREET for gutter types, RECT_OPEN/TRAPEZOIDAL for DROP_*, CUSTOM anywhere, `Uinlet.pas:524-575`), Capture node (picker + "pick on map" using the existing node-pick flow), Number of inlets, % clogged, Flow restriction, Depression height, Depression width, Placement. Backed by `swmm_inlet_usage_*` (engine E2) through `SWMMModelLayer::applySetInletUsage/applyRemoveInletUsage` + `SetInletUsageCommand` (id 24). The conduit adapter's "Inlets" row shows `design (→ node)`. Requires engine Phase E2.

---

## 3. Deliverable 2 — Inlet junction

### 3.1 Node kind, flag and rendering (per D-G1)

- `NodeKind::InletJunction = 5`. Adapter switch (`propertiespanel.cpp:643-658`): probe `swmm_node_is_inlet` before `swmm_node_is_virtual` (an inlet junction is also virtual in the engine).
- `SWMMModelLayer`: `NodeGeom.isInlet` mirror; `m_inletJunctionSym` (default: `QColor(0,120,255)` fill, square/diamond marker distinct from the VJ circle, size 9); QSG bucket swap on `isInlet` in `swmmlayerqsgrenderer.cpp:1289-1340`; routing id `model.inletjunctions`; kind-tree row under Nodes; prefs key `inlet_junction` (`preferencesmanager.cpp:87,118,703-714`, `noderenderingprefs.*`, reset list `preferencesdialog.cpp:2022`); identify popup "Inlet Junction · <design> → <capture node>".
- Object browser: the VJ deferred "sub-count row" is still deferred; add both VJ and inlet-junction sub-counts together in this slice (`swmmobjecttreemodel.cpp`) — one change, two rows.
- Profile plot: inlet junction drawn with the VJ dashed outline plus a small inlet glyph; the capture node is not in the street profile.

### 3.2 Dashed connector to the capture node

A new non-hydraulic overlay drawn by the model layer for every inlet usage (both conduit-attribute inlets and inlet junctions): a `Qt::CustomDashLine` polyline from the host (conduit midpoint for link usages — legacy `TMap.DrawInletSymbol`, `Umap.pas:1047-1070`; node position for inlet junctions) to the capture node, with a small inlet glyph at the host end. Implementation: a `LineSymbolLayerSpec`-backed sublayer (`render/linesymbollayer.h`) keyed `model.inletconnectors`, colour = inlet junction symbol outline, dash `{4,3}` (matches `profileplotoptions.cpp:35-37`), drawn beneath nodes and above links, styleable from the symbology tree, hit-testable only for identify. Not selectable, not editable; the *capture node* is changed from the property panel. (**D-G5**: alternative is a field on `SWMMElementSymbol`, rejected — touches style JSON round-trip.)

### 3.3 Add tool and conversion

- `OpenSWMMVisMapToolAddInletNode` (copy of `maptooladdvirtualnode.*`): conduit-hit only (D-G3), **accepts only STREET conduits** (status-bar hint otherwise — "Inlet junctions can only be placed on street conduits"), name prefix `IJ` in `PreferencesManager`. On click, opens a small modal `InletJunctionSetupDialog` (design picker filtered to gutter types, capture-node picker with map pick, placement) *before* mutating, then pushes `InsertInletJunctionCommand` (id 25) → `applyInsertInletJunction(link, t, node, newLink, design, captureNode, …)` → `swmm_conduit_split_inlet` (engine E5). Undo = `swmm_inlet_junction_fuse` (exact inverse incl. usage).
- Action `actionAddInletJunction` — full chain as VJ: `forms/swmmvis.ui` (menu Model → Add Node after Virtual Junction), `actioncatalog.h` row (icon alias `InletJunction`, ribbon `model`, `RequiresProject`), face label `"Inlet\nJunction"`, toolbar Nodes group after `actionAddVirtualJunction`, checkable-tool list `swmmvis.cpp:1737`, activator `:4002`, project-window tool + `toolActionKeys()` (`swmmvisprojectwindow.cpp:338,1908,1952`), gating list `swmmvis.cpp:884`, status hints `:6375`.
- Icon `resources/images/inlet_junction.svg`: 24×24, `#777777` strokes only — the dashed VJ circle with a short horizontal grate bar pattern across it (three 1-px bars), so it reads as "junction + inlet" beside `junction.svg` / `virtual_junction.svg` / `inlet.svg`. Register alias `InletJunction` in `swmmvis.qrc`.
- **Join/convert**: Convert To ▸ "Inlet Junction" in `typeconversionflow` for a junction whose two links are same-section STREET conduits (probe `swmm_node_inlet_eligible`, show rule text via `virtualJunctionRuleText` extended with 623–631); prompts the same setup dialog; not undoable (typeconversion precedent). Also "Virtual Junction → Inlet Junction" and the reverse (demote keeps the node as VJ; demote-to-junction available too).
- Delete (`maptoolselect.cpp:723-836`): same three-way prompt as VJ ("Re-fuse Conduits" default); the informative text adds "Inlet <design> → <capture node> will be removed".
- Link-draw tool: inlet junctions rejected as endpoints exactly like VJs (`maptooladdlink.cpp:98-101`).

### 3.4 Property adapter

`SWMMInletJunctionPropertyAdapter : SWMMVirtualJunctionPropertyAdapter` exposing: `rimDepth` (labelled "Street Max Depth" — engine D-E2 flood threshold), `inletDesign` (picker → `pickInlet`), `captureNode` (node picker + map pick), `numInlets`, `pctClogged`, `flowRestriction`, `depressionHeight`, `depressionWidth`, `placement`; read-only `approachStreet` (upstream conduit), `crownElev`, `degree`; results rows `statPeakCapture`, `statPctCaptured`, `statBackflowFreq` when results are loaded; compound refs inherited from the VJ adapter (inflows/DWF/RDII/treatment are legal on a VJ since 2026-09-04). Writes go through `applySetInletUsage` (one undoable command) — the same command as §2.4 with `host_kind = node`.

### 3.5 Usage preview

`InletUsagePreview` (small `QWidget`, `paintEvent`): the street section (reuse `XsectSampler::fromStreet`, `sectionmodelbuilders.cpp:97-113`) with the local depression `a_local × w_local` drawn at the curb and the chosen design's section glyph — shared between §2.4's page and §3.4's compound dialog.

### 3.6 Capability gating

Probe `swmm_node_is_inlet` on project open (tri-state cache in `SWMMModelLayer`); when absent (older engine) hide `actionAddInletJunction`, the Convert-To entries and the §2.4 page, with tooltip "Requires engine ≥ <version>". Inlet-junction models opened with a legacy 5.x engine selected trigger the compat-writer downgrade defined in the engine plan §2.3, surfaced in the existing pre-run compat warning.

---

## 4. Deliverable 3 — Model toolbar cleanup

`src/swmmvisactions.cpp:282-314`:

```cpp
addGroup(mToolBarModel, tr("Nodes"),
         {"actionAddJunction", "actionAddVirtualJunction", "actionAddInletJunction",
          "actionAddOutfall", "actionAddFlowDivider", "actionAddStorage"});
addGroup(mToolBarModel, tr("Links"), { …unchanged… });
addGroup(mToolBarModel, tr("Subcatchments"), {"actionAddSubcatchment"});
addGroup(mToolBarModel, tr("Rain Gages"),    {"actionRainGauge"});
addGroup(mToolBarModel, tr("Annotation"),    {"actionAddText"});
// Climate family unchanged
addGroup(mToolBarModel, tr("Data Objects"),
         {"actionNewTimeSeries", "actionNewCurve", "actionNewPattern",
          "actionNewControlRule", "actionNewTransect", "actionNewStreet", "actionNewInlet",
          "actionNewLidControl", "actionNewPollutant", "actionNewLandUse",
          "actionEditReactionSystem", "actionEditHeatConfig"});
```

Captions are the single point of change; `RibbonCompactor` handles overflow. Set the `ribbonTab` field of `data.newStreet` / `data.newInlet` (`actioncatalog.h:139-140`) to `"model"`. Add face labels `"Street"`, `"Inlet"` to `kShortLabels`. Verify with the existing ribbon layout test (or add `test_ribbon_groups` asserting group captions and order) and a screenshot in both themes.

---

## 5. Decision points for review

| ID | Question | Recommendation |
|---|---|---|
| D-G4 | Redraw `inlet.svg` | Yes, curb + grate glyph; keeps alias |
| D-G5 | Connector as sublayer vs. `SWMMElementSymbol` field | Sublayer (§3.2) |
| D-G6 | Setup dialog before insert vs. insert-then-edit in property panel | Dialog first: the engine requires a design and capture node for the node to be valid, and a half-configured node would fail validation on save |
| D-G7 | Inlet junction marker shape | Filled diamond with grate bars (distinct from VJ open circle and junction filled circle); confirm against the user-supplied icon once drawn |
| D-G8 | Draw the dashed connector for existing conduit-attribute inlets too | Yes — parity with the legacy map (`Umap.pas:1047-1070`) and it makes the capture-node relationship visible for both kinds |
| D-G9 | Preview technology: `QGraphicsScene` builder vs. `paintEvent` widget like `StreetSectionPreview` | Scene builder (export/zoom/reuse); `StreetSectionPreview` stays as is |

---

## 6. Conceptual issues and mitigations

1. **The GUI already has an Inlets editor; rebuilding it changes a shipped surface.** Keep class names, `objectName`s, `DataInlets` registry hooks and `actionNewInlet`, so persistence, tests and the menu chain are untouched; only the body changes.
2. **Editor fidelity depends on the engine schema (E1).** Until `swmm_inlet_get_params2` lands the editor cannot show throat, curb height, COMBO or custom-curve kind. Phase G1 can be built against a stubbed provider; do not ship it against the 5-arg API.
3. **Node/link namespace overlap** — the capture-node picker must list nodes only, never links; the connector overlay must be keyed by (host kind, index), not by name.
4. **`SwmmCategory` ordinals are frozen** — no `CatInletJunctions`; D-G1 approach means no legend entry unless the kind tree grows a row (it does in §3.1).
5. **Undo id collisions** — ids 12/13 are duplicated already; new ids 24 (`SetInletUsageCommand`), 25 (`InsertInletJunctionCommand`), 26 (`FuseInletJunctionCommand`), 27 (`SetInletJunctionCaptureNodeCommand` if not folded into 24).
6. **Type conversion is not undoable** (precedent) — the confirm dialog says so.
7. **Deleting the capture node** — `DeleteObjectCommand` must clear the usage's capture node (engine cascade already renumbers `inlet_usages`, `ObjectDeleter.cpp:1402-1484`); GUI prompts "N inlets discharge to this node; their capture node will be cleared and the inlets disabled".
8. **Reassigning a conduit's cross-section away from STREET** while it hosts an inlet or touches an inlet junction must be refused with rule 623 text (xsection edit dialog + attribute table).
9. **Multi-engine**: the compat writer (V2 plan, not done) is the correct home for the downgrade; until it exists, running an inlet-junction model on 5.x must be blocked with a clear message, not silently written.

---

## 7. Phased work plan

```
Phase G0  Review; resolve D-G4..D-G9; engine E0 signed
          → verify: recorded here

Phase G1  Toolbar cleanup (§4) + menu reorder (§2.1) + icons (inlet.svg redraw, inlet_junction.svg)
          → verify: ribbon test; screenshots light/dark; no engine dependency

Phase G2  Inlets editor rebuild (§2.2–2.3): provider/registry full schema, undo commands,
          InletDrawingView + scene builder, entry points, object-browser branch
          [needs engine E1/E2 for live data; buildable against stub]
          → verify: golden images per type/theme; unit tests on callout text; undo/redo round-trip;
            create→save→reopen shows identical values (no more "defaults on load")

Phase G3  Inlet usage on conduits (§2.4) + usage preview (§3.5) + dashed connector overlay (§3.2)
          for conduit-attribute inlets [needs engine E2]
          → verify: usage set/clear/undo; connector follows node moves; symbology row works

Phase G4  Inlet junction: flag mirror, rendering, prefs, kind tree, identify, browser sub-counts,
          property adapter (§3.1, §3.4) [needs engine E3/E4]
          → verify: adapter switch picks InletJunction before VirtualJunction; rows write-through;
            attribute table in sync

Phase G5  Add tool, setup dialog, action chain, convert-to, delete/re-fuse prompts, link-draw
          rejection, capability gating (§3.3, §3.6) [needs engine E5]
          → verify: click street conduit → configured node; click pipe → hint; undo restores the
            .inp byte-identically; convert eligible junction; prompts show design/capture node

Phase G6  Docs: docs/manual chapter "Streets and Inlets" (editor, usage, inlet junction), update
          docs/inlet-tutorial steps 5–6 for the new editor, release notes, CHANGELOG on release
          → verify: manual builds; feature demo checklist
```
