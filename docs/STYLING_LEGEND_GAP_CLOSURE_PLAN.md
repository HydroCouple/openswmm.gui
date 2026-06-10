# openswmm.gui — Styling, Contextual Symbology & GIS-Grade Legend: Gap Closure Plan

## Implementation status (2026-06-10)

| Item | Status |
|---|---|
| A1.1 per-frame rebin un-gated (constructor connect) | DONE |
| A1.2 canonical colour convention (`SymbolProps`), tolerant spec readers, JSON rehydration incl. raster keys | DONE + tests |
| A1.3 archetype-seeded `RendererFactory` adopted at all 4 construction sites | DONE + tests |
| A1.4 abandoned GL renderer deleted | DONE |
| A2.1 eager per-kind renderers on `openResults()` + fetch collects renderer attrs + serializer default-elision (`kindRendererIsDefault`) | DONE |
| A2.2 FixedOverRun classifies against full-run range (async re-classify hook); FixedUser min/max spins | DONE |
| A2.3 legend-from-renderer in `sublayerLegendItems()` (kind-qualified classKeys) | DONE |
| A2.4 `setVariable`/`setColorRamp` facades over kind renderers | DONE |
| A3.1 2D band/isoline/velocity knobs are facades over sublayer model (dead dialog controls revived) | DONE |
| A3.2 `DepthColorRampStyle` ramp/range-mode upgrade | DEFERRED |
| A3.3 2D scalar-field switch (depth/wse/vmag) | DEFERRED |
| A3.4 dormant 2D `m_renderer` removal | DEFERRED (legend routing now bypasses it) |
| A3.5 2D sublayer persistence | DEFERRED (2D results layers not yet serialized at all) |
| A3.6 2D panel rewire | LARGELY MOOT (A3.1 revived the existing panel's controls) |
| A4.1 `RendererPanelContext::resolve` capability snapshot | DONE |
| A4.2 declarative applicability gating + greyed dropdown rows w/ tooltips | DONE |
| A4.3 type-filtered attribute combos (graduated numeric-only; categorized strings-first) | DONE |
| A4.4 range-mode row gated on attribute dynamism | DONE |
| A4.5 output-axes UI (size-by-value points / width-by-value lines), archetype-gated | DONE |
| B1 `LegendContent` consolidation; results-layer kind-qualified legend facade; overlay right-click colour edit fixed for multi-kind layers; rendererChanged/variableChanged live legend sync | DONE |
| B2 RampBar/units legend item kinds | NOT STARTED |
| B3 legend ordering + per-item fonts model | NOT STARTED |
| B4 LegendPainter (columns, ramp bar paint) | NOT STARTED |
| B5 drag-reorder + font override UI | NOT STARTED |
| B6 scaled PNG/SVG export | NOT STARTED |

Verification (2026-06-10): full gui ctest sweep green (67 suites passed)
except pre-existing working-tree breakage unrelated to this work:
- compile/link failures against WIP editor-dialog changes:
  `test_objectbrowser_add_new_dispatch`, `test_pattern_editor_dialog`,
  `test_curve_editor_dialog`, `test_nonspatial_adapters`,
  `test_subcatchpropertyadapter`, `test_timeseries_editor_dialog`
- engine-side user-flags round-trip failures (`swmm_engine_open` rejects the
  written .inp): `test_userflagsmodel`, `test_userflags_roundtrip`
Three stale test-target link gaps were repaired in passing
(`test_2d_sublayers` += colorramp.cpp, `test_symbolstyleadapter` +=
markershape.{cpp,h}, `test_featuresublayer` += labelconfig.cpp).


All paths below are relative to `/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui`.

## Context

A code-derived review (ignoring existing `docs/*.md` plan/status files, per user request) of the
styling/theming and legend systems found that the static-layer renderer architecture
(`IFeatureRenderer` + Single/Graduated/Categorized/RuleBased, QPropertyModel-driven editors,
legend-from-renderer) is sound and further along than the docs suggest — but several
load-bearing gaps remain, verified directly against the code:

1. **Per-frame rebin never fires from a loaded project** — the
   `currentTimeStepChanged → rebinDynamicRulesIfNeeded()` connect lives inside
   `buildRuleListLazy()` (`src/layers/swmmresultslayer.cpp:1629`); unless the symbology
   dialog is opened, `PerFrameAutoStretch` silently does nothing.
2. **Legend ≠ renderer for results layers** — `sublayerLegendItems()` synthesizes 5 equal bins
   from the legacy `m_colorRamp` instead of consulting `kindRenderer(c)->legendSymbolItems()`.
3. **Renderer construction paths disagree** — `SymbologyTab`/`makeDefaultKindRenderer` create
   renderers with empty base symbols; `overrideColorInPlace` only writes into pre-existing
   color keys, so paint silently falls back to the legacy ramp (root of the "link color" bug).
4. **Two color encodings** — renderers write QColor variants; categorized panel writes hex
   strings; typed specs (`MarkerSymbolLayerSpec`) read `value<QColor>()` which can't parse hex.
5. **2D results dialog has dead controls** — legacy setters write `filled_contours_` /
   `isolines_*` fields (`src/layers/swmm2dresultslayer.cpp:1317-1368`) but paint gates on
   sublayer visibility/style bags (`:158-160`). Dual source of truth; panel checkboxes no-op.
6. **No contextual gating** — `RendererPanelContext` (`include/ui/dialogs/irendererpanel.h:69`)
   has no applicability mechanism; every renderer is offered for every kind (incl. RainGages
   with zero attributes); attribute combos ignore `AttributeField::type` (numeric vs string);
   graduated output axes (size/width) have model+persistence+paint but **no UI** and need
   archetype gating; range-mode row gates on host type, not attribute dynamism.
7. **Legend is below GIS standard** — no row reorder, no per-item fonts, single column, no
   continuous ramp-bar element, no units, clipboard-only export at 1x; on-canvas overlay's
   right-click color edit is broken for `SWMMModelLayer` rows (works in dock); three
   near-duplicate `legendItemsFor()` helpers must stay in lockstep manually.
8. **Dead/parallel state** — abandoned GL renderer (`mapcanvas.h` comment confirms) still reads
   `SWMMElementSymbol` struct; `attributecandidates.cpp` hardcoded lists have one remaining
   consumer.

Stale findings corrected during review (do NOT re-implement): `AttributeSource`/`RangeMode`
types exist (`include/render/attributesource.h`); `IAttributeProvider` is implemented on all 4
layer types; categorized + rule-based editor panels exist and work; 1D paint already consults
renderers via override caches; per-kind renderers persist in `.oswp`; `LegendOverlayStyle`
persistence is wired (`src/project/projectserializer.cpp:553,868`).

**User decisions:** delete abandoned GL renderer now, defer parallel-struct retirement;
rule-based editor gets minimal polish only (no nested rule tree); styling workstream first,
then legend.

---

## Step 0 — Save this plan to the repo (user request)

Write this plan (Context + phases below) to
`openswmm.gui/docs/STYLING_LEGEND_GAP_CLOSURE_PLAN.md` as the first implementation action.

---

## Workstream A — Styling unification + contextual symbology

### Phase A1 — Correctness prerequisites (shippable alone)

- **A1.1 Un-gate per-frame rebin**: move the `currentTimeStepChanged →
  rebinDynamicRulesIfNeeded()` connect from `buildRuleListLazy()` into the `SWMMResultsLayer`
  constructor (drop duplicate). `fetchResultsForStep` populates `m_*ResultsByVar` before the
  signal fires, so ordering is safe.
- **A1.2 Canonical color helpers**: add `SymbolProps::readColor/writeColor` to
  `include/render/symbolstyle.h` + `src/render/symbolstyle.cpp` — reads accept QColor variant
  OR hex string (back-compat forever); writes emit one canonical form. Convert all
  readers/writers: `src/render/{marker,line,fill}symbollayer.cpp` spec readers;
  `overrideColorInPlace` in `src/render/renderers/{graduated,singlesymbol,categorized,unclassedcolors}renderer.cpp`;
  `makeColourSymbol` in `src/ui/dialogs/editors/categorizedrendererpanel.cpp`;
  `extractStyleColor` in `src/layers/swmmresultslayer.cpp`.
- **A1.3 One renderer factory, archetype-seeded**: new `include/render/rendererfactory.h` +
  `src/render/rendererfactory.cpp` —
  `makeRenderer(rendererId, archetype, previous=nullptr, fields=nullptr)` seeds
  baseSymbol/fallbackSymbol with archetype-appropriate prop skeleton (Point: fillColor+size+shape;
  Line: color+width+dash; Polygon: fillColor+outline), carries forward symbol +
  classifyAttribute from `previous` when compatible, seeds first matching attribute from
  `fields`. Use from `src/ui/dialogs/symbologytab.cpp::installRendererClassIfChanged`,
  `KindRendererPanel::onModeChanged`, `Rule::setRendererById` (`src/render/rule.cpp`),
  `makeDefaultKindRenderer` (`src/layers/swmmresultslayer.cpp`). Kills the
  empty-base-symbol → silent-legacy-fallback failure for nodes AND links.
- **A1.4 Delete abandoned GL renderer**: remove `src/map/swmmlayerglrenderer.cpp` + header,
  `m_glRenderers` member/take-block in `map/mapcanvas.{h,cpp}`, CMake entries. QSG path is
  unaffected (already renderer-driven via `featureColor()`).

Tests: new `tests/gui/test_symbolprops_color.cpp` (hex⇄variant through every spec reader);
extend `tests/gui/test_ifeaturerenderer.cpp` (factory seeds per archetype; `symbolFor` yields
color for Point and Line).

### Phase A2 — Renderer = single source of truth for 1D results

- **A2.1 Eager per-kind renderers**: in `openResults()`, install
  `makeRenderer("graduated", archetypeFor(cat), …)` for all result-bearing categories
  (`sourceKind=Dynamic`, `rangeMode=FixedOverRun`, attribute = kind's default result variable).
  Override-cache path becomes the only paint path; legacy `localRamp` branch stays as
  data-missing fallback only. Elide default renderers in `src/project/projectserializer.cpp`
  serialization so `.oswp` files don't bloat.
- **A2.2 Run-range classification**: make `FixedOverRun` true to its name — when classifying,
  request `ensure*AttributeRange(outCode)`; on background scan completion re-run
  `autoClassify` against the full-run range for `FixedOverRun` renderers. `FixedUser` skips
  auto-classify; add min/max spin boxes to `KindRendererPanel` range row (visible only for
  FixedUser).
- **A2.3 Legend-from-renderer**: `sublayerLegendItems()` emits header +
  `kindRenderer(cat)->legendSymbolItems()` (stamping sublayerId/classKey); synthesized 5-bin
  block becomes no-renderer fallback only.
- **A2.4 Demote legacy fields**: `setVariable()` becomes a facade that rewrites
  `classifyAttribute` on scope renderers; `colorRamp()/setColorRamp/autoStretchColorRamp`
  delegate to renderer ramps. `m_variable` stays as toolbar convenience.

Files: `src/layers/swmmresultslayer.cpp` + header,
`src/ui/dialogs/editors/kindrendererpanel.{h,cpp}`, `src/project/projectserializer.cpp`.
Tests: new `tests/gui/test_resultslayer_renderer_unification.cpp` (override caches populated
after open; legend rows == renderer items; setVariable rewrites renderers; `.oswp` round-trip
of Jenks-7 + per-bin override). Risk: first-paint visuals shift — seed renderer ramps with the
same sampled ranges the legacy path used; before/after screenshots on `examples/` models.

### Phase A3 — 2D results: one model, range modes, persistence (needs only A1.2)

- **A3.1 Sublayer bags become the only model**: legacy setters forward —
  `setFilledContours(on)` → `m_contourBandSublayer->setVisible(on)`; `setIsolines*` →
  `IsolineStyle`; `setVelocityVectors*` → `VelocityVectorStyle`;
  `setColorClasses/setColorRampStyle/setDryDepth/setMaxDepth` → `DepthColorRampStyle`. Delete
  shadowed private fields; paint reads bags exclusively. This revives every dead dialog
  control and keeps layer-tree toggles in sync.
- **A3.2 Upgrade `DepthColorRampStyle`** (`include/render/sublayers/depthcolorrampsublayer.h`):
  `attribute` ("depth"|"wse"|"vmag"), `RasterColorRamp ramp` (replaces lowColor/highColor;
  `fromJson` migrates 2-stop), `rangeMode` (RangeMode), `userMin/userMax`, `classes`,
  `graduated`, `dryDepth`. Paint maps value→`ramp.colorAt()`; band colors sample the same ramp
  (replace hardcoded viridis). Range per tick: FixedOverRun = running max (formalized),
  PerFrameAutoStretch = frame min/max, FixedUser = user values. Factor
  `computeDisplayRange(mode, frameMinMax, runningMax, userMinMax)` as a pure helper for tests.
- **A3.3 Scalar-field switch**: implement `attribute` in `applyCurrentDepths_()` — depth
  (as-is), wse (depth + ground elev), vmag (already computed). Trim
  `SWMM2DResultsLayer::availableAttributes()` to exactly these three.
- **A3.4 Delete dormant `m_renderer`** from `SWMM2DResultsLayer` (continuous per-cell scalars
  style through bags; per-triangle `symbolFor` adds indirection nobody reads).
- **A3.5 Persistence**: additive `result2DLayerSublayers` key in
  `src/project/projectserializer.cpp` using existing
  `ISublayerHost::saveSublayersToJson/loadSublayersFromJson`; apply on the HDF5-layer
  construction path in `src/swmmvis.cpp`.
- **A3.6 Panel rewrite-in-place**: `src/ui/dialogs/swmm2dresultsstylepanel.{h,cpp}` — add
  output-variable combo (provider-fed), range-mode combo, `ColorRampComboBox`; route every
  control through style bags / `ISublayer::setVisible`.

Tests: extend `tests/gui/test_2d_sublayers.cpp` (setter forwarding; ramp JSON migration;
`computeDisplayRange` arithmetic). Risk: per-triangle `colorAt` perf — pre-sample a 256-entry
LUT per frame if profiling shows regression.

### Phase A4 — Declarative contextual gating (needs A1.3; UI-only)

- **A4.1 Central capability snapshot**: extend `RendererPanelContext`
  (`include/ui/dialogs/irendererpanel.h`) with resolved fields —
  `QVector<AttributeField> fields; Archetype archetype; bool animated;` +
  `hasNumeric()/hasString()` + static `resolve(layer, category, rule)` centralizing the
  provider lookup + rule→layer parent-walk currently duplicated in
  `kindrendererpanel.cpp` (×2) and `categorizedrendererpanel.cpp`.
- **A4.2 Per-renderer applicability** on the registry entry:
  `std::function<bool(const RendererPanelContext&)> applicable` + `disabledReason`.
  Graduated → `hasNumeric()`; Categorized/RuleBased → `!fields.isEmpty()`; Single → always.
  `SymbologyTab` disables non-applicable combo rows with tooltip — RainGages then offer
  Single only, Graduated greyed with "no numeric attributes".
- **A4.3 Type-filtered attribute combos**: `KindRendererPanel` filters
  `type != QMetaType::QString`; `CategorizedPanel` lists strings first then numerics; both
  keep the renderer's current attribute prepended if missing.
- **A4.4 Gate range-mode row by `field.isDynamic`** of the selected attribute, not host type.
- **A4.5 Output-axes UI** in `KindRendererPanel`, archetype-gated: Point → "Size by value" +
  min/max px spins; Line → "Width by value"; Polygon → neither. (Model/persistence/paint
  already exist — pure UI.)

Files: `include/ui/dialogs/irendererpanel.h` + cpp, `src/ui/dialogs/symbologytab.cpp`,
`src/ui/dialogs/kindtreesymbologypanel.cpp`,
`src/ui/dialogs/editors/{kind,categorized,graduated}rendererpanel.cpp`.
Tests: extend/new `tests/gui/test_rendererpanelcontext.cpp` — `resolve()` + applicability
matrix (RainGages / model junctions / results conduits / 2D).

### Phase A5 — Provider completion; retire `attributecandidates`

- Extend `SWMMModelLayer::availableAttributes` with string fields the categorized sampler
  reads via `identifyByName` (verify each key against the map first) + missing numerics.
- Switch `src/ui/panels/layertreepanel.cpp` grey-out to
  `provider->availableAttributes(cat).isEmpty()`; delete
  `include/render/attributecandidates.h`, `src/render/attributecandidates.cpp`, CMake entry.
- Delete `suggestedAttributesFor` fallback in categorized panel (combo stays editable).
- `PerAttributeThemingWidget::populateCombos` switches to the primary layer's provider
  (labels gain units).

### Phase A6 — Editor polish & current-style readback completeness

- Replace `FeatureStyleEditorBase`'s free-text attribute `QLineEdit`
  (`src/ui/dialogs/editors/featurestyleeditor.cpp:51`) with an editable, provider-fed,
  type-filtered combo.
- Remove the duplicated class-switcher: strip the stale `Mode:` combo (incl. "Categorized
  (coming soon)") from `KindRendererPanel` — the outer `SymbologyTab` dropdown owns class
  switching; two stacked dropdowns can currently disagree.
- Rule-based panel minimal polish (per user decision): attribute-token insert menu fed by
  `ctx.fields`; live expression validation via `ExpressionEvaluator::eval` with inline error
  label. Nested rule tree explicitly deferred.
- 1D variable-selector convenience: slim header row above the kind tree in
  `KindTreeSymbologyPanel` (results layers only) embedding `PerAttributeThemingWidget`.

---

## Workstream B — GIS-grade legend (after A2 lands; B1 can start anytime)

### Phase B1 — Shared legend-content utility + routing/live-sync fixes

- New `include/render/legendcontent.h` + `src/render/legendcontent.cpp`: one canonical copy of
  the three duplicated helpers (`featureRendererFor`, `legendItemsFor`, `firstSymbolColor`) +
  facade dispatch (`supportsClassEdit/colorForClass`) so the overlay context menu handles
  multi-kind layers exactly like `legendclasseditcommands.cpp` already does.
- Add a legend facade to `SWMMResultsLayer` mirroring `SWMMModelLayer`'s: aggregate
  `kindRenderer(c)` slots with kind-qualified classKeys (same `0x1F` separator), fall back to
  layer-level renderer.
- Replace local helpers in `src/ui/widgets/legendoverlay.cpp` (fixes broken right-click color
  edit on model-layer rows), `src/ui/models/legendlayertreemodel.cpp`, and
  `src/map/legendclasseditcommands.cpp` with `LegendContent::` calls.
- Connect `rendererChanged` (and 2D equivalents) in `LegendLayerTreeModel::connectLayer`
  alongside `repaintRequested` → legend refreshes on variable/renderer change.

Tests: new `tests/gui/test_legendcontent.cpp` — overlay path and dock path yield identical
rows; classKey round-trip through `setColorForClass`.

### Phase B2 — Legend content model: item kinds, ramp payload, units

- `include/render/legendsymbolitem.h`: add
  `enum class Kind { SymbolRow, RampBar, TextRow, GroupHeader }` + RampBar payload
  (`RasterColorRamp ramp; QVector<double> rampTicks; QString unit;`) — additive JSON.
- `GraduatedRenderer`: `LegendMode { DiscreteRows, RampBar }` + `unitSuffix`. RampBar mode
  emits ONE item (ramp + ticks from `lastBreaks()` + unit, `classKey="ramp"`); DiscreteRows
  unchanged but appends unit to labels. Same `LegendMode` for the raster
  single-band pseudocolor renderer.
- Populate `unitSuffix` from `AttributeField::unit` where panels set `classifyAttribute`; for
  results default renderers, set when `setVariable` runs.
- Default `DiscreteRows` keeps current visuals until UI opts in (dormant until B4).

### Phase B3 — Presentation model: ordering + per-item fonts in `LegendOverlayStyle`

- `ItemOverride` gains `QFont font; bool hasFont;`. New per-layer ordering:
  `itemOrder(layerKey)` / `setItemOrder()` / `itemOrderChanged(layerKey)` — ordered classKey
  list; consumers sort renderer items by index-of(classKey), unknown keys keep renderer order
  and append (survives renderer regeneration; stale keys ignored, same convention as dormant
  bin color overrides).
- New chrome properties: `columns` (int), `titleAlignment` (registered enum for stock
  QPropertyModel editors), `showUnits` (bool), `freePosition` (QPoint — persists Free-anchor
  drag).
- All additive in `toJson/fromJson` → automatically persisted through the existing
  `kLegendOverlay` serializer path (no serializer change).
- Move `applyItemOverrides` + new `applyItemOrder` into `LegendContent` so overlay and dock
  cannot drift.

Tests: extend `tests/unit/test_legendoverlaystyle.cpp` + `test_projectserializer.cpp`
(order/font survive save/load; unknown-classKey tolerance).

### Phase B4 — Rendering: extract `LegendPainter`; ramp bar, columns, units, fonts

- New `include/ui/widgets/legendpainter.h` + cpp: pure paint class over the shared models —
  `sizeHint()`, `paint(QPainter&, scale)` returning hit-test bands (QRect-based;
  `layerAtY/itemAtY` become point-based). Paints SymbolRow / RampBar (vertical gradient +
  min/max/break ticks + unit) / GroupHeader / TextRow; flows layer sections into
  `style->columns()` columns (never splits a ramp bar); per-item font from override.
- `LegendOverlay::paintEvent` shrinks to chrome + `m_layerBands = painter.paint(p)`.
- Dock: `LegendLayerTreeModel` gains `ItemKindRole`/`RampRole`;
  `LegendColorDelegate` paints a mini gradient chip for RampBar rows (not editable).

Defaults (`columns=1`, DiscreteRows) keep current pixel output — golden-image test for the
default path. Tests: new `tests/gui/test_legendpainter.cpp` (sizeHint vs columns; band
coverage; scale 1 vs 2 aspect).

### Phase B5 — Configurability UI: drag-reorder + properties + per-item fonts

- `LegendLayerTreeModel`: implement drag-drop (`mimeData/dropMimeData`, pattern from
  `LayerTreeModel`), constrained to item rows within the same layer; drop pushes new
  `SetLegendItemOrderCommand` (`src/map/legendclasseditcommands.{h,cpp}`, style-targeted,
  undoable via `MapUndoStack`).
- `LegendDock`: `InternalMove` drag mode; context menu gains "Override font…" (QFontDialog)
  and "Reset font/label/order".
- `LegendPropertiesDialog`: new properties appear automatically via QPropertyModel; add
  `displayLabelFor` entries ("Layout — Columns", "General — Title alignment", "General —
  Show units").
- LegendOverlay needs zero work — shared style + B4 painter pick everything up via signals
  (MVC requirement satisfied by construction).

Tests: new `tests/gui/test_legenddock_reorder.cpp` (reorder, cross-layer rejection, single
undoable command, undo restores).

### Phase B6 — Export: scaled PNG + SVG

- `LegendOverlay`: "Export legend…" (QFileDialog *.png/*.svg, scale combo for PNG) via
  `LegendPainter` rendered into `QImage`/`QSvgGenerator` (Qt6::Svg already linked); fix
  "Copy as image" to render at `devicePixelRatio` (currently 1x).

---

## Explicitly deferred (with rationale)

- Parallel symbol struct retirement (`GISVectorSymbol`, `SWMMElementSymbol`) — high effort,
  low visibility; structs become derived caches with one write path opportunistically, full
  retirement is a later standalone workstream.
- QGIS-style nested rule tree — flat first-match table covers threshold/flag use cases.
- Print composer — modest export (B6) only.

## Cross-cutting risks

- `.oswp`/`.swmm-style.json` back-compat: every schema change additive; color reads accept
  hex + variant forever; `lowColor/highColor` → ramp migration shim.
- Eager renderers change first-paint visuals (A2.1) — seed from the same sampled ranges;
  before/after screenshots on `examples/` models.
- Build hygiene: new Q_OBJECT headers must be listed in `CMakeLists.txt` for AUTOMOC; beware
  stale `SWMMVis.app` bundles (known project gotchas).
- Async range scans racing classification — A2.2 re-classifies on scan completion, not block.
- Uncommitted WIP in the tree — rebase onto HEAD state at implementation time.

## Verification

Per-phase unit tests named above (run via the project's ctest/gui test target). End-to-end app
checks after each phase:

1. **A1**: load a results run; set a kind to Graduated from the SymbologyTab dropdown (not the
   inner panel) → links AND nodes recolor from the renderer ramp; set PerFrameAutoStretch,
   close dialog, save, reopen, play → bins move per frame.
2. **A2**: legend dock shows Jenks/categorized swatches matching the map; `.oswp` round-trip
   preserves classification + per-bin overrides.
3. **A3**: 2D example — toggle bands/isolines/velocity from BOTH the dialog and the layer
   tree (must stay in sync); scrub to peak under PerFrame vs FixedOverRun; save/reopen keeps
   2D style.
4. **A4**: RainGages offer Single only (Graduated greyed + tooltip); model Graduated combo has
   no string fields; conduits expose width axis, junctions size axis.
5. **A6**: dialog walkthrough on model/results/2D layers; selecting any sub-layer/kind shows
   its CURRENT style; Cancel rolls back everywhere.
6. **B1–B6**: right-click color edit works on overlay model-layer rows with undo; switch
   results variable → legend updates; RampBar mode shows gradient bar with ticks + units;
   drag-reorder a row in dock → overlay reorders instantly, undo restores, reopen project
   keeps order; export SVG/2x PNG.

Use stale-bundle-aware launch (darwin vs darwin-debug app bundles) when verifying.
