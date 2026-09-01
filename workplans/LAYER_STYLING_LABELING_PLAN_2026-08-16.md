# Layer Styling, Theme Selection & Labeling Overhaul — 2026-08-16

## Goal

Fix styling for SWMM model instance and model results layers so that:
1. Right-clicking a sublayer/kind → Properties opens on the Symbology tab with that
   sublayer focused and its **current** theme selected.
2. The chosen theme applies correctly and stays in sync between UI surfaces.
3. Attribute selection uses dropdowns (not free text), with **independent**
   attributes for color-by and size-by.
4. Labeling works end-to-end (full scope: field/expression, all element types,
   scale window, priority/collision, both render paths).

## Diagnosed defects

### A. Theme selection / routing
- `LayerStyleDialog::focusInitialSubject()` (layerstyledialog.cpp:1019) compares
  tab text to `tr("Symbology")` but the tab is added as `tr("S&ymbology")`
  (:631). The comparison never matches → dialog always opens on Information and
  the routing-id walk to the clicked kind/sublayer is dead code.
- `Styles ▸ Edit Symbology…` (layertreepanel.cpp:2017) passes no routing id.
- `SymbologyTab` (symbologytab.cpp:120-133) falls back to `entries().front()`
  when no renderer is installed; registration order is static-init order across
  4 TUs, so the default shown is arbitrary.
- Outer "Renderer:" combo (SymbologyTab) and inner "Mode:" combo
  (KindRendererPanel, embedded again in FeatureStyleEditorBase preview row)
  never sync: switching inner mode to Graduated installs a graduated renderer
  while the outer combo still shows Single Symbol.

### B. Theme application
- Paint paths (swmmlayeritem.cpp CPU, swmmlayerqsgrenderer.cpp QSG) correctly
  consume the override caches; failures are staleness/sync, addressed via A.

### C. Attribute dropdowns / sizing
- `FeatureStyleEditorBase` (featurestyleeditor.cpp:52-54) uses a free-text
  QLineEdit for the classification attribute although it already queries
  `IAttributeProvider`.
- Size-by-value exists only as `GraduatedRenderer::outputSize/Width` driven by
  the *same* attribute as color. No independent size attribute exists.

### D. Labeling
1. Labels tab is a no-op for `SWMMResultsLayer`: `refreshLabels()` reads only
   per-sublayer `featureStyle()->labelConfig()`; layer-level config never read.
2. `SWMMElementSymbol::showLabel/labelFont/labelColor` are persisted but never
   rendered (dead UI in SwmmElementSymbolEditor).
3. Model-layer painter hardcodes `n.name` (swmmlayeritem.cpp:1043); `fieldName`
   and `expression` from the Labels tab are ignored.
4. Only nodes are labelled on the model layer; links/subcatchments/gages never.
5. `minScale`/`maxScale` never honored on 1D paths; `priorityField` read nowhere.
6. 1D model path re-implements label drawing in scene coordinates (glyphs scale
   with zoom) instead of using `LabelPainter`'s screen-space contract.
7. Per-sublayer label editors expose only enabled/expression/color (3 of ~15
   LabelConfig fields).

## Plan

### Phase 1 — Routing & current-theme selection
1. `focusInitialSubject()`: strip `&` from tab text before comparing (helper
   `plainTabText`). Verify: right-click sublayer → Properties lands on
   Symbology with the clicked kind focused.
2. `LayerTreePanel` layer-row `Styles ▸ Edit Symbology…`: route through a new
   signal/param carrying an explicit "open symbology" intent (empty routing id
   but Symbology tab focused).
3. `SymbologyTab`: when no renderer installed, prefer `"single"` explicitly
   before falling back to `entries().front()`.
4. Sync inner→outer: when `KindRendererPanel` installs a different renderer id,
   emit a change the `SymbologyTab` listens to so its combo reflects reality
   (`rendererInstalled(QString id)` signal → SymbologyTab slot).

### Phase 2 — Attribute dropdowns + independent sizing
1. `FeatureStyleEditorBase`: replace attribute QLineEdit with a QComboBox
   populated from `IAttributeProvider` (numeric fields), preserving current
   value.
2. `GraduatedRenderer`: add `sizeAttribute()` (empty = follow classify
   attribute, preserving today's behavior and file compatibility) with own
   min/max normalization; JSON round-trip.
3. `KindRendererPanel`: attribute combo relabeled "Color by"; new "Size by"
   combo (points/lines archetypes) enabled with the size axis checkbox.
4. Cache rebuilds (`rebuildKindFeatureColors` in SWMMModelLayer,
   `rebuildKindFeatureOverrides` in SWMMResultsLayer): compute sizes/widths from
   `sizeAttribute` when set, else classify attribute.

### Phase 3 — Labeling overhaul
1. Extract a reusable `LabelConfigEditor` widget from `LabelsTab` internals;
   use it in LabelsTab, per-sublayer boxes (kindtreesymbologypanel,
   FeatureStyleEditorBase) so all ~15 fields are editable everywhere.
2. Effective-config resolution: sublayer config used when its `enabled` is true,
   else fall back to layer-level config (results layer `refreshLabels`).
3. Model layer: label text resolver honoring `expression` tokens ({name},
   {field}) and `fieldName`; label links, subcatchments, rain gages in addition
   to nodes; per-kind `showLabel/labelFont/labelColor` act as overrides on top
   of the layer LabelConfig (wires existing dead UI).
4. Use `LabelPainter` on the 1D CPU path in screen space (resetTransform);
   remove the hand-rolled duplicate; honor `scaleVisible()` (min/max scale).
5. Collision/priority: greedy screen-space rect dedup ordered by
   `priorityField` value (descending), shared helper in LabelPainter so the 2D
   path can reuse it.
6. QSG path: the CPU label overlay remains the label mechanism when QSG
   geometry is active (as today), now correct in screen space — this is the
   documented hybrid; a pure QSG glyph atlas is out of scope.

### Phase 4 — Verification
- Build (user machine): `cmake --build build`.
- Manual checks: (a) right-click each sublayer kind → Properties → Symbology tab
  focused, current theme preselected; (b) switch theme → map updates; reopen →
  same theme shown; (c) color-by and size-by dropdowns list model/result
  attributes, independent selection works; (d) enable labels from Labels tab on
  a results layer → labels appear; field/expression respected; zoom in/out
  honors scale window; dense areas drop low-priority labels.
- Unit-testable pieces: label text resolution, size normalization from
  independent attribute, greedy collision ordering.

## Phase 5 — follow-ups (addressed same day)

All items previously listed as out of scope were resolved:

- **QSG labels** — decision: keep the CPU overlay (labels were already
  functional under QSG geometry ownership). The hybrid is now the DOCUMENTED
  contract: `qsgOwnsKind` doc (swmmmodellayer.h), top-of-file doc in
  swmmlayerqsgrenderer.cpp, and the label block in swmmlayeritem.cpp all
  state that labels are always CPU-painted and never gated on qsgOwnsKind.
  A pure-QSG glyph atlas was evaluated and rejected (cost/risk vs. benefit).
- **Cancel-rollback / adapter ownership refactor** —
  - `SWMMModelLayer::elementSymbolAdapter(routingId)`: one PERSISTENT
    adapter per kind (12 incl. virtual junctions), lazily built, resynced
    from the live struct on fetch and from every set*Symbol. styleSubjects()
    and SingleSymbolPanel now share these instances (leak + divergence gone).
  - GISVectorLayer got the same treatment (m_symbolAdapter).
  - `StyleFileIO::styleToJson/applyStyleJson`: in-memory full-style
    snapshot (layer renderer, kind renderers, label config — results-layer
    labelConfig now included too).
  - LayerStyleDialog: Cancel restores the full-style JSON + subject
    snapshots (the historical restore was re-enabled); Apply/import
    re-baseline; EditLayerStyleCommand undo/redo carries the full-style
    JSON alongside subject snapshots.
  - QFont now round-trips losslessly in subject snapshots
    (ilayerstylesubject.cpp toString/fromString).
  - Remaining: SWMM2DMeshLayer's two "Sublayers" subjects still allocate a
    SymbolStyleAdapter per styleSubjects() call (and mutate the Rule on
    read) — untouched; needs its own pass.
- **Virtual junctions in kind tree** — category-less row under Nodes
  (model layers only) mounting SwmmElementSymbolEditor via the persistent
  adapter; focusKind("model.virtualjunctions") works.
- **SublayerStyleDialog retired** — files deleted, CMake entries and stale
  includes/comments removed (layertreepanel, legenddock, isublayer docs).
- **Label attribute cache** — SWMMLayerItem::m_labelCache caches resolved
  (text, priority) per SoA group, filled lazily, invalidated by
  SWMMModelLayer::editRevision() (bumped via self-connections on
  modelEdited + attributeChanged) or when the config's text fields change.
- **SymbologyTab sync extended** — currentRendererIdFor reads the
  layer-level renderer() for category-less contexts; rendererChanged is
  wired for GISVectorLayer / SWMM2DMeshLayer / SWMM2DResultsLayer as well.
  Layer-level WRITE path intentionally not added (rule-backed layers own
  their renderer through the active Rule).
- **Size-range invalidation on model edits** —
  SWMMModelLayer::invalidateDerivedStyleCaches() (connected to
  modelEdited + attributeChanged) clears each graduated kind's independent
  size value range and rebuilds that kind's overrides; classification
  breaks are deliberately left alone (user-intent contract).

## Phase 6 — final follow-ups (addressed)

- **SWMM2DMeshLayer adapter ownership** — persistent `m_meshEdgeAdapter` /
  `m_meshNodeAdapter` (QObject*, lazily built, reused per styleSubjects
  call). Safe to cache: SymbolStyleAdapter holds only a Rule* and reads
  live (no cached state); the mesh RuleList is never rebuilt so Rule
  pointers are stable. The forward-seed (setRuleLayer) still runs per
  fetch, which is what keeps the Rule truthful.
- **Real Outlets symbol channel** — `outletSymbol()/setOutletSymbol()`
  added; ctor seeds `m_outletSym` (same values as the old renderer-only
  seed, so first-open visuals are unchanged) and builds the CatOutlets
  renderer from it; `setKindRenderer` back-writes CatOutlets;
  `elementSymbolAdapter("model.outlets")` binds to the real channel (the
  conduit-alias and its double-resync are gone); the RuleList back-prop
  writes the whole struct via setOutletSymbol (the manual arrow-field copy
  block was removed). Outlet arrow settings now survive save/reload.
  DELIBERATE: the outlet link PEN stays prefs-driven ("outlet" pen,
  linkPenForType case 4 + QSG lstyle[4]) — honouring the symbol's
  fill/size for the pen would change on-screen visuals; documented at the
  accessor.
- **Arrow setters resync** — setLinkArrowsEnabled/Size/Color/OnlyWhenFlowPos
  mutate the symbol structs in place, so they now resync the persistent
  adapters (linkKindRoutingId helper).

Verification handoff: see HANDOFF_LAYER_STYLING_VERIFY_2026-08-16.md.

---

## Phase 7 — Compile / test verification (2026-08-16, verifier session)

Executed against `HANDOFF_LAYER_STYLING_VERIFY_2026-08-16.md`. Build preset is
`default` (→ `build/`, Release/Ninja), **not** `Darwin-local` as §2 assumed.

### Build — PASS
Reconfigured (needed: the F7 deletions) then full rebuild: **3005 steps, 0
errors, 0 failed targets**. None of §4's five predicted mechanical errors
occurred. All ten §4 "likely failure mode" checkpoints verified statically
(CMake registration, adapter includes in all 4 callers, `attributeChanged`
arity, `<QTimer>`/`<QFont>` survival, `sublayerstyledialog` residue = one prose
comment in `isublayer.h:31`, `model.outlets` binding, label block not gated on
`qsgOwnsKind`, `Qt::QueuedConnection` on all five `rendererChanged` wires).

§6 "no new warnings in touched files": the only warnings landing in touched
files are the pre-existing `-Winconsistent-missing-override` pair in
`gisvectorlayer.h` / `swmmresultslayer.h`; declarations are byte-identical to
HEAD (the gisvectorlayer line just shifted by one). Nothing new.

### Tests — PASS (196/196: 147 gui + 49 unit)
Targeted set from §3 all green, including both named sensitivities
(`test_projectserializer` with the Outlets kindRenderers fixtures, and
`test_symbolstyleadapter`). Neither needed the test-side updates §3 anticipated.

Two new tests written as required:
1. `test_ifeaturerenderer.cpp` +3 slots — independent size axis: interpolation
   over its own range, immunity to the classify value, missing/non-numeric size
   value leaving the base size intact, JSON round-trip, and the no-`sizeAttribute`
   back-compat path. **Gotcha for future edits:** `setSizeAttribute()` clears the
   sampled range, so `setSizeValueRange()` must come after it.
2. NEW `tests/gui/test_layerstylesubject.cpp` — the F5 QFont snapshot/restore
   round trip over a real `SwmmElementSymbolAdapter`, plus scalars and the
   absent-key case. Registration needs `markershape.h` + `markershape.cpp` in the
   target: the adapter's `markerShape` Q_PROPERTY is a `Q_ENUM_NS`, so the
   namespace `staticMetaObject` must be emitted and linked (this was the one
   link error hit during verification).

### FIXED 2026-08-16 — was: defect found by inspection
`swmmlayeritem.cpp` per-kind label override (matrix row C14): the three
overrides were guarded inconsistently —

    if (s && s->showLabel) {
        eff.font  = s->labelFont;                      // UNGUARDED
        if (s->labelFont.pointSizeF() > 0.0) eff.fontSizePt = …;   // guarded
        if (s->labelColor.isValid())         eff.color      = …;   // guarded
    }

`SWMMElementSymbol::labelFont` is a default-constructed `QFont`, so merely
ticking a kind's "Show labels" — without touching its font — replaces the layer
LabelConfig's family/weight/slant with the application default, while the size
and colour correctly survive. Set the Labels tab to e.g. Georgia Bold, then
enable per-kind labels on Junctions: junction labels render in the system font.

Fixed by guarding the font the same way as size and colour:
`if (s->labelFont != QFont()) { eff.font = …; …fontSizePt… }`. A
default-constructed `QFont` carries an empty resolve mask, so the guard is
true exactly when the user has actually chosen a font for that kind — a
deliberate pick of the default family still sets the mask and still wins.

### NOT VERIFIED — needs a human at the screen
Matrix rows requiring click-through against a loaded model + results are
untested: **A1, A4, B5–B9, C10–C13, C16, D17, D19–D22, E25**.
`LayerStyleDialog` and `SWMMModelLayer` cannot be constructed headlessly (the
model-layer TU pulls in the whole app), so no harness was built for them.

Rows settled statically from the code: A2 (sentinel `"symbology"` → signal →
`focusInitialSubject`), A3 (`hostLayer->renderer()` read + deterministic
`"single"` fallback), C14 (see defect above), C15 (`subLc.enabled ? subLc :
labelConfig()`), D18 (`EditLayerStyleCommand` carries styleBefore/styleAfter),
E23 (`model.virtualjunctions`, model layers only), E24 (legend dock →
`LayerStyleDialog`).

Two things checked that turned out NOT to be defects: `LabelPainter::scaleVisible`
returns true on an unusable scale denominator, so labels survive exports and
canvas-less paints; and the results-layer size range prefers the full-run cache
with the frame-extremes stand-in guarded by `!sizeValueRangeValid()`, so animated
sizes don't jitter frame to frame (row B7's stability expectation).

### Phase 7b — user-reported defects (confirmed + fixed)

Two symptoms reported from the Layer Styling dock on a results layer's
Conduits: no flow-arrow toggle anywhere, and the "Size by value" min/max px
having no effect on conduit thickness. **One root cause.**

Ruled out first, empirically rather than by reading: a probe against the real
`RendererFactory` + `GraduatedRenderer` for `Archetype::Line` showed the width
axis is correct (range 1..5 → widths 1/3/5; range 10..40 → 10/25/40), the Line
skeleton does carry a `width` prop, `rebuildKindFeatureOverrides` fills
`m_kindFeatureWidths`, and the paint sites consume it.

Root cause — `RuleSymbologyTab::mountBodyForActive()` built its
`RendererPanelContext` with **only** `ctx.rule`, leaving `hostLayer` and
`category` unset. `GraduatedPanel` then took its rule-only branch into
`KindRendererPanel(rule, parent)`, which delegates with a **`CatJunctions`
sentinel**. Everything kind-scoped then resolved against Junctions:

* `isLinkKind(CatJunctions)` false ⇒ `m_arrowBox->setVisible(false)` — the flow
  arrow toggle was not hidden by a styling decision, it was never shown;
* `archetypeFor(CatJunctions)` == Point ⇒ the axis row rendered as **"Size by
  value"** (rather than "Width by value") and drove `setOutputSize*`. For a line
  symbol `symbolFor` then writes into a `"size"` prop that the Line skeleton
  does not have, so nothing changed on screen — no width was ever requested.

The user's own wording ("size by value" on a conduit) was the tell: on the
correct path that control is labelled "Width by value".

Fix: recover the kind context in `mountBodyForActive`. Only SWMM model /
results layers build a kind-indexed rule list (one Rule per Category in ordinal
order), so the recovery is guarded on the owner's type and on
`count() == NumCategories`; GIS-vector and 2D-mesh lists hold arbitrary user
rules and correctly keep the category unset. Layer-type checks use
`QObject::inherits` + `static_cast` rather than `qobject_cast`, so this TU keeps
its self-contained link footprint (`test_rulesymbologytab` links it without the
layer stack) — same technique and same reason as
`RendererPanelContext::resolve`.

Note the Properties dialog → Symbology (kind tree) path was never affected: it
passes hostLayer + category + rule, so `GraduatedPanel` takes the three-arg
branch. The bug was specific to the **Layer Styling dock**.

### C14 — closed
The unguarded per-kind label-font override was fixed in
`swmmlayeritem.cpp` (`if (s->labelFont != QFont())`) before this verifier
session got to it. No action needed; the guard now matches labelColor /
fontSizePt.

Build clean, 196/196 tests green after both.
