# HANDOFF — Compile, Test & Verify: Layer Styling / Theming / Labeling Overhaul

**Date:** 2026-08-16
**Companion plan:** `workplans/LAYER_STYLING_LABELING_PLAN_2026-08-16.md` (read it
first — it records every defect, decision, and change of this session).
**Your job:** build the tree, fix any compile/link/moc errors that surface, run
the targeted unit tests, execute the manual verification matrix, and fix issues
that arise. Everything was written without a compiler available, so expect a
small number of mechanical errors (missing include, signature drift) — the
"Likely failure modes" section tells you where to look first.

---

## 1. What changed (inventory by feature)

### F1 — Properties routing / current-theme selection
- `src/ui/dialogs/layerstyledialog.cpp` — `focusInitialSubject()` compares
  mnemonic-stripped tab text against `plainTabText(tr("S&ymbology"))`.
- `include/ui/panels/layertreepanel.h` + `src/ui/panels/layertreepanel.cpp` —
  `layerPropertiesRequested(OpenSWMMVisLayer*, const QString &routingId = {})`;
  "Styles ▸ Edit Symbology…" emits the `"symbology"` sentinel.
- `src/swmmvis.cpp` (~line 2024) — connect lambda takes `(layer, routingId)`.

### F2 — SymbologyTab truthfulness
- `include/ui/dialogs/symbologytab.h` / `src/ui/dialogs/symbologytab.cpp` —
  new private slot `syncToInstalledRenderer()`; queued connects to
  `rendererChanged` on all five layer types (Model, Results, GISVector,
  2DMesh, 2DResults — new includes for the last three); deterministic
  `"single"` fallback; `currentRendererIdFor` reads
  `ctx.hostLayer->renderer()` for category-less contexts.

### F3 — Attribute dropdowns + independent size attribute
- `include/render/renderers/graduatedrenderer.h` / `src/render/renderers/graduatedrenderer.cpp`
  — `sizeAttribute` / `sizeValueMin/Max` / `sizeForValue` / `widthForValue`;
  reworked `symbolFor` (color axis skipped when classify value missing; size
  axes read the independent attribute); JSON keys `sizeAttribute`,
  `sizeValueMin`, `sizeValueMax`.
- `src/layers/swmmmodellayer.cpp` — `classifyGraduatedIfNeeded` rewritten
  (sampleAttr lambda + size-range derivation).
- `src/layers/swmmresultslayer.cpp` — needed-vars loop collects the size
  attribute; `rebuildKindFeatureOverrides` resolves size values + range
  (full-run cache preferred, frame extremes as stand-in);
  `attrs.insert(sizeAttr, …)` per row.
- `include/ui/dialogs/editors/kindrendererpanel.h/.cpp` — "Color by:" relabel,
  new "Size by:" combo (`m_sizeAttrRow`/`m_sizeAttrCombo`,
  `onSizeAttributeChanged`).
- `include/ui/dialogs/editors/featurestyleeditor.h/.cpp` — attribute QLineEdit
  → editable QComboBox populated from IAttributeProvider.

### F4 — Labeling overhaul
- `src/map/swmmlayeritem.cpp` — label pass rewritten: expression/fieldName
  resolution, all four kinds labelled, per-kind SWMMElementSymbol
  showLabel/labelFont/labelColor overrides, device-space LabelPainter
  drawing, MapCanvas scale-window gating (walks widget parents), greedy
  priority-ordered collision pruning, per-feature text/priority cache
  (`LabelTextCache` in `include/map/swmmlayeritem.h`).
- `src/layers/swmmresultslayer.cpp` — `refreshLabels`: layer-level
  LabelConfig fallback (`subLc.enabled ? subLc : labelConfig()`),
  scale gating via scene views → MapCanvas, cosmetic halo pen. New includes:
  `map/mapcanvas.h`, `render/labelpainter.h`, `<QGraphicsView>`.
- NEW `include/ui/widgets/labelconfigeditor.h` +
  `src/ui/widgets/labelconfigeditor.cpp` — full-fidelity LabelConfig editor;
  registered in `CMakeLists.txt` (header ~line 771, source ~line 1340).
- `src/ui/dialogs/kindtreesymbologypanel.cpp` — `makeSublayerLabelBox` hosts
  LabelConfigEditor; `src/ui/dialogs/editors/featurestyleeditor.cpp` — labels
  group replaced by LabelConfigEditor (`m_labelEditor`).

### F5 — Adapter ownership + Cancel rollback
- `include/layers/swmmelementsymboladapter.h` — `resyncFrom()`.
- `include/layers/swmmmodellayer.h` / `src/layers/swmmmodellayer.cpp` —
  persistent adapter set (`m_symbolAdapters`, `elementSymbolAdapter(routingId)`,
  `resyncSymbolAdapter`); `styleSubjects()` wraps persistent adapters;
  every `set*Symbol` resyncs; arrow setters resync via `linkKindRoutingId`.
- `include/layers/gisvectorsymboladapter.h` (`resyncFrom`),
  `include/layers/gisvectorlayer.h` / `src/layers/gisvectorlayer.cpp`
  (persistent `m_symbolAdapter`).
- `include/render/stylefileio.h` / `src/render/stylefileio.cpp` —
  `styleToJson` / `applyStyleJson` (in-memory full-style snapshot; results
  layer labelConfig now included; labelConfig always emitted).
- `src/ui/dialogs/layerstyledialog.cpp` / `.h` — Cancel restores full-style
  JSON + subject snapshots; Apply/import re-baseline; EditLayerStyleCommand
  carries styleBefore/styleAfter; `m_styleSnapshot`/`m_undoStyleBaseline`.
- `src/ui/dialogs/ilayerstylesubject.cpp` — lossless QFont snapshot.
- `src/ui/dialogs/editors/singlesymbolrendererpanel.cpp` — model branch uses
  `elementSymbolAdapter()`; `m_modelSubject` removed; added `<memory>`.

### F6 — Virtual junctions kind-tree row
- `src/ui/dialogs/kindtreesymbologypanel.cpp` / `.h` — category-less row
  under Nodes (model layers only), `mountVirtualJunctionsEditor()` via
  StyleEditorRegistry; selection handler normalises to column 0.

### F7 — SublayerStyleDialog retired
- DELETED: `include/ui/dialogs/sublayerstyledialog.h`,
  `src/ui/dialogs/sublayerstyledialog.cpp`. CMake entries removed; stale
  includes removed from `layertreepanel.cpp`, `legenddock.cpp`; comments
  updated in `legenddock.h`, `isublayer.h`.

### F8 — Model-edit invalidation channel
- `SWMMModelLayer` ctor: self-connects on `modelEdited` + `attributeChanged`
  bump `m_editRevision` immediately and coalesce
  `invalidateDerivedStyleCaches()` via `QTimer::singleShot(0, …)`
  (`m_derivedStyleCachesPending` guard; `<QTimer>` include added).
  `invalidateDerivedStyleCaches` clears graduated independent size ranges +
  rebuilds those kinds' override caches.

### F9 — Real Outlets symbol channel
- `outletSymbol()/setOutletSymbol()` (header + cpp); ctor seeds `m_outletSym`
  (fillColor 140,100,60 / outlineWidth 1.5 — identical to the old
  renderer-only seed) and builds the CatOutlets renderer from it;
  `setKindRenderer` CatOutlets back-writes; adapter binding table uses the
  real channel; RuleList back-prop `CatOutlets` case simplified to
  `applyAndWrite(&L::outletSymbol, &L::setOutletSymbol)` (manual arrow-copy
  block removed). **Deliberate:** outlet link PEN stays prefs-driven
  (`linkPenForType` case 4, QSG `lstyle[4]`) — do NOT "fix" that without a
  decision; it would change on-screen visuals.

### F10 — Mesh persistent adapters
- `include/layers/swmm2dmeshlayer.h` — `QObject *m_meshEdgeAdapter/`
  `m_meshNodeAdapter`; `src/layers/swmm2dmeshlayer.cpp` — `addAdapter`
  creates once, reuses (forward-seed still runs per fetch).

### F11 — QSG label contract documentation
- Doc comments only: `src/map/swmmlayerqsgrenderer.cpp` (file header),
  `include/layers/swmmmodellayer.h` (`QsgKind` block),
  `src/map/swmmlayeritem.cpp` (label block).

---

## 2. Build

macOS (this machine): the tree was configured with the `Darwin-local` user
preset and has an existing `build/` dir.

```bash
cd /Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui
cmake --build build -j 8 2>&1 | tee /tmp/swmmvis_build.log
```

If the incremental build behaves oddly after the file deletions (F7), re-run
the configure step first (`cmake --preset Darwin-local`) so the removed
sources drop out of the generated build system. A full clean rebuild is the
fallback, not the first resort.

Iterate: fix errors → rebuild → repeat until clean, **including warnings in
the files listed in §1** (treat new -Wunused / -Wsign-compare in touched files
as defects). Do not "fix" errors by deleting functionality; consult §4 first,
then the plan document, then make the minimal correct change.

## 3. Automated tests

Test executables are registered via `add_test` in `tests/*/CMakeLists.txt`.
Run the targeted set (names may differ slightly — `ctest -N` to list):

```bash
cd build
ctest -N | grep -iE "renderer|symbol|label|style|serial|rule" # discover names
ctest --output-on-failure -R "ifeaturerenderer"    # GraduatedRenderer symbolFor/JSON
ctest --output-on-failure -R "labelpainter"        # scaleVisible / placement
ctest --output-on-failure -R "symbolstyleadapter"  # adapter contract (F10 must not break)
ctest --output-on-failure -R "rule"                # Rule/RuleList round-trip
ctest --output-on-failure -R "projectserializer"   # kindRenderers persistence (F9!)
ctest --output-on-failure -R "multikindrenderer|layertreecategories|featuresublayer"
```

Then the full suite: `ctest --output-on-failure`.

Expected sensitivities:
- `test_ifeaturerenderer` — `symbolFor` was restructured (F3). The size/width
  axes are default-off and behaviour with axes off is unchanged, so existing
  assertions should hold. If a case fails on the missing-classify-value path:
  the old code returned the bare base symbol; the new code does too (color
  axis is gated on `ok`) — investigate before touching the test.
- `test_projectserializer` + fixtures with an `"Outlets"` kindRenderers key
  (`tests/unit/data/mesh_sync_fixture.oswp`,
  `tests/gui/data/typed_selection_fixture.oswp`) — loading now back-writes
  `m_outletSym` via `setOutletSymbol` (F9). That is the INTENDED new
  behaviour; if a test asserts `m_outletSym`/arrow state stays default after
  load, the test needs updating, not the code.
- `test_symbolstyleadapter` — F10 did not change adapter semantics (ctor,
  prop keys, `changed` emission untouched); failures here mean an accidental
  edit — diff `symbolstyleadapter.*` against git (should be untouched).

**Write two new unit tests** (put beside the existing ones, register in the
matching CMakeLists):
1. `GraduatedRenderer` independent size axis: set classifyAttribute="a",
   sizeAttribute="b", `setOutputSizeEnabled(true)`, `setSizeValueRange(0,10)`,
   `setOutputSizeRange(2,12)`; `symbolFor` with attrs {a:…, b:5} must write
   size ≈ 7 into a base symbol carrying a `"size"` prop; b missing → no size
   write; JSON round-trip preserves the three new fields; back-compat: JSON
   without `sizeAttribute` → `sizeAxisIndependent()==false`.
2. `LabelConfig`/subject QFont round-trip: build a `LayerStyleSubject` over a
   `SwmmElementSymbolAdapter`, set a distinctive `labelFont`
   (family/size/bold), `snapshot()` → mutate → `restore()` → font equals the
   original (verifies the F5 QFont fix).

## 4. Likely failure modes → fixes

| Symptom | Probable cause | Fix |
|---|---|---|
| moc error / undefined `staticMetaObject` for `LabelConfigEditor` | new files not picked up by AUTOMOC | confirm both files are in the main target lists in `CMakeLists.txt` (~771 header / ~1340 source); re-run configure |
| `SwmmElementSymbolAdapter` incomplete type in `swmmmodellayer.cpp` / `singlesymbolrendererpanel.cpp` / `kindtreesymbologypanel.cpp` | include ordering | ensure `#include "layers/swmmelementsymboladapter.h"` is present in the .cpp (it is in all three — verify no header-only usage elsewhere, e.g. any NEW caller of `elementSymbolAdapter()` must include it) |
| `static constexpr KindBinding kBindings[]` fails on older compilers | PMF-in-constexpr-aggregate edge case | drop `constexpr` → `static const KindBinding kBindings[]` |
| connect on `attributeChanged` fails to compile | signal signature drift | signal is `attributeChanged(const QString&)` in `swmmmodellayer.h`; adjust lambda arity to match the actual declaration |
| `QTimer` unknown in `swmmmodellayer.cpp` | include | `<QTimer>` was added near the other Qt includes; verify it survived |
| `qMetaTypeId<QFont>()` errors in `ilayerstylesubject.cpp` | missing include | `<QFont>` was added; keep it above the anonymous namespace |
| duplicate-symbol / missing-file errors mentioning `sublayerstyledialog` | stale generated build files | re-run the configure step; grep the tree — the only remaining mentions must be prose comments |
| `LayerStyleDialog` cancel crashes on a mesh layer | `applyStyleJson` on a layer type styleToJson barely covers | acceptable: for mesh layers `styleToJson` holds only `renderer`; guard is `!styleJson.isEmpty()`; if it misbehaves, skip the applyStyleJson call when `layerType` key ≠ Model/Results/Vector |
| Labels double-draw or vanish under GPU rendering | someone gated the CPU label block on `qsgOwnsKind` | the label block must NOT be gated — see the contract comments (F11) |
| Deadlock/re-entrancy in symbology dialog when flipping inner Mode combo | queued `syncToInstalledRenderer` remounting mid-signal | it must stay `Qt::QueuedConnection`; do not "simplify" to direct |
| Outlets edits revert or ping-pong with Conduits | a leftover conduit alias | grep `model.outlets` — the binding must be `outletSymbol/setOutletSymbol` everywhere; `setConduitSymbol` must NOT resync `model.outlets` |

## 5. Manual verification matrix

Load a model with all element kinds (e.g. an `examples/` model), run a
simulation for results-layer checks. Verify each row; file follow-up notes in
the plan document.

**A. Routing & theme selection**
1. Right-click each sublayer kind (model layer + results layer) → Properties
   → dialog opens ON the Symbology tab with that kind selected in the tree.
2. Layer row → Styles ▸ Edit Symbology… → Symbology tab front.
3. With a Graduated theme installed on Conduits: reopen Properties → the
   Renderer combo shows "Graduated" (not Single Symbol).
4. In a Single Symbol editor, flip the embedded Mode combo to Graduated →
   the OUTER Renderer combo updates to Graduated (queued; allow an event
   loop tick).

**B. Theme application + attributes**
5. Graduated on Junctions, "Color by:" invertElev → map recolors; switch
   attribute → recolors again (breaks re-derived).
6. Enable "Size by value", pick a DIFFERENT "Size by:" attribute → marker
   sizes track the second attribute while colors track the first.
   "(same as color)" restores bin-mapped sizing.
7. Results layer: graduated Conduits colored by flow, sized by velocity —
   animate; sizes update per frame; ranges stable (FixedOverRun).
8. Results sublayer editor: Attribute is a dropdown listing result variables.
9. Edit a static attribute (e.g. a node invert) with an independent-size
   theme active → sizes re-normalise (after the coalesced rebuild tick).

**C. Labeling**
10. Labels tab on the MODEL layer: enable → names appear for nodes AND links,
    subcatchments, gages (visible kinds only). Font/color/halo/background and
    placement all take effect. Labels stay constant-size across zoom.
11. Field: set a field name → labels show that value; Expression
    `{name}: {invertElev}` → composite text.
12. Scale window: set "Out 1:" to a value you can cross by zooming → labels
    hide/show at the threshold (model layer immediately; results layer on
    next refresh tick).
13. Priority field + dense area → lower-priority labels dropped, no overlaps.
14. Per-kind Labels group in a kind's Symbology editor (showLabel +
    labelFont/labelColor) → that kind labels even with the layer Labels tab
    off, using the override font/color.
15. RESULTS layer Labels tab: enable → labels appear (layer-level fallback);
    per-sublayer label editor (full LabelConfigEditor) overrides when its own
    "Show labels" is on.
16. Toggle GPU rendering (QSG kinds on) → labels still render once, correctly.

**D. Cancel / undo / persistence**
17. Open Properties, change a kind's fill color AND switch another kind to
    Graduated → Cancel → BOTH revert on the map.
18. Same edits → OK → Edit ▸ Undo → both revert; Redo → both return.
19. Apply, then more edits, then Cancel → only post-Apply edits revert.
20. Import style → Cancel → import survives (baseline reset).
21. Set outlet flow arrows on → save project → reload → arrows still on (F9).
22. Open/close the Properties dialog 10× → no adapter growth on the layer
    (spot-check: object dump or debugger; previously 12 leaked per open).

**E. Structure**
23. Kind tree shows "Virtual junctions" under Nodes (model layer only);
    clicking it (either column) mounts the symbol editor; edits repaint the
    virtual-junction ring markers. Absent on results layers.
24. Legend dock right-click → Edit Style… still works (SublayerStyleDialog
    removal did not break it).
25. 2D mesh layer Properties → Sublayers (edges/nodes) editors work; edits
    apply; reopen — same values (persistent adapters).

## 6. Acceptance

- Clean build, no new warnings in touched files.
- Targeted + full ctest green (with the two new tests added).
- Matrix rows A1–E25 pass, or each failure is fixed and re-verified.
- Update `CHANGELOG.md` only if this lands as part of a release commit
  (repo convention: changelog updates on releases).
- Record anything left open in `LAYER_STYLING_LABELING_PLAN_2026-08-16.md`.
