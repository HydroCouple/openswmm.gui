# P6 — GL paint reroute + `SWMMElementSymbol` removal (#33) and symbology undo (#36)

Status: planned. These are the two remaining P6 items. Both are **destructive or
broad** and must be run with a compiler in the loop (incremental build +
visual verify between stages), not as a single blind batch. This document is
the vetted, staged roadmap — execute top-to-bottom, building after each stage.

---

## Current state (verified by source audit, 2026-06-01)

- **GPU/QSG path reads the legacy struct, not the renderer.** In
  `src/map/swmmlayerqsgrenderer.cpp`, `updatePaintNode()`'s content rebuild
  bakes per-vertex node colours from the per-**kind** struct accessor
  (`m_layer->junctionSymbol().fillColor`, etc., ~line 830-839 and 859-870),
  with an explicit TODO at ~line 818: *"no per-feature override yet — wire to
  layer->featureColor()."* Links use `conduitSymbol().fillColor` (single
  colour, ~line 678) and subcatch uses `subcatchmentSymbol().fillColor`
  (~line 670). **Consequence (X3):** Graduated / Categorized / per-feature
  renderer colours render correctly on the CPU `SWMMLayerItem` path (which
  uses `featureColor()`), but the GPU path shows a single per-kind colour →
  GPU diverges from CPU.
- **`featureColor(Category, idx)` is indexed by per-category row**
  (`m_kindFeatureColors[c][idx]`, `swmmmodellayer.cpp:2291`). The CPU painter
  feeds it `b.indices[j]` (precomputed per-category rows in node "buckets",
  `swmmlayeritem.cpp:761`). The QSG loop iterates the **global node SoA**
  (`m_nodes[i]`), so it needs an SoA-index → (category, row) mapping.
- **`SWMMElementSymbol` surface:** 71 occurrences across 5 .cpp files —
  `swmmmodellayer.cpp` (63), `swmmlayeritem.cpp` (4), `swmmvisprojectwindow.cpp`
  (2), `markersymbollayer.cpp` (1), `swmmlayerglrenderer.cpp` (1, dormant).
- **Persistence (X5 "whole-model persistence") is already done** —
  `projectserializer` writes/reads each layer's per-kind renderer JSON
  (`kindRenderers`), labelConfig, and result-layer renderers (`makeRendererFromJson`
  + `setKindRenderer`, lines 644 / 767). #36 reduces to **undo** only.

---

## #33 — Stage 1: GPU path honours per-feature colours (additive, no deletion)

Goal: GPU view matches CPU for Graduated / Categorized / per-feature colours.
Nothing is deleted; the struct stays as the single-symbol fallback.

1. **Add an SoA-indexed colour accessor on `SWMMModelLayer`** (so the mapping
   lives where the data is, and the QSG loop stays simple):
   ```cpp
   // Returns the per-feature override colour for the node/link/subcatch at
   // the given *SoA* index, or an invalid QColor when there's no override
   // (caller falls back to the kind's struct fillColor). Maps SoA index →
   // (category,row) via the same path rebuildKindFeatureColors used.
   QColor nodeFeatureColor(int soaIdx) const;
   QColor linkFeatureColor(int soaIdx) const;
   QColor catchFeatureColor(int soaIdx) const;
   ```
   Implement by resolving the element name (`m_nodes[soaIdx].name`) →
   `m_objectLocation[name]` → `(cat,row)` → `featureColor(cat,row)`. For perf
   on large models, precompute an `m_nodeSoaToRow` (and link/catch) `QVector<int>`
   in `rebuildFlagArrays()`/geometry build so the hot loop is an array index,
   not a hash lookup. **Verify** the per-category row order matches
   `m_kindFeatureColors` population order (objectNameAt order).
   - **Simplification found:** `m_kindFeatureColors[c]` is already **SoA-indexed**
     (swmmmodellayer.cpp:2349-2369), so `featureColor(cat, soaIdx)` takes the SoA
     index directly. The QSG node loop's `i` IS that index → no name lookup /
     precompute needed. The accessors above are unnecessary for nodes.
2. **Nodes — DONE (2026-06-01).** QSG node loop (`swmmlayerqsgrenderer.cpp`
   ~858-880) now reads `m_layer->featureColor(cat, i)` per node (cat from
   nodeType), falling back to the kind struct colour when invalid; selection
   override unchanged. Nodes already use QSGVertexColorMaterial so per-vertex
   colours were trivial. **Build + visually verify** a Graduated junction
   renderer matches CPU with GPU on.
3. **Links + subcatch — STILL TODO (Stage 1b).** These render through *single
   flat-colour* QSG nodes (`lines` = QSGFlatColorMaterial set from
   `conduitSymbol().fillColor`; `catchFill` from `subcatchmentSymbol()`). Per-
   feature colour needs converting them to **vertex-coloured** nodes
   (`makeColoredNode` / `ColoredPoint2D`) and baking `featureColor(cat, i)` per
   segment/triangle at upload — a material + geometry-attribute change. Do this
   as a separate build-checked increment; until then graduated conduits/subcatch
   still show a single colour on the GPU path (CPU path is correct).
4. **Verify (build + visual):** load a model, set a Junction Graduated renderer
   by an attribute, GPU rendering ON. The GPU view must match the CPU view
   (toggle GPU off/on). Check selection highlight still wins. Diff a screenshot
   GPU vs CPU.

Stage 1 alone closes the user-facing X3 divergence. Do **not** proceed to
Stage 2 until Stage 1 is built and visually confirmed.

---

## #33 — Stage 2: retire `SWMMElementSymbol` (+ G-2: `GISVectorSymbol`)

Only after Stage 1 is green. The struct is the single source for single-symbol
kinds today (M2 keeps it in sync with the renderer both ways). "Removal" means
making the **renderer** the sole source and deleting the struct + its API.
Do it in **build-between-each batches**:

- **Batch A — make accessors derive from the renderer.** Reimplement
  `junctionSymbol()`/`conduitSymbol()`/… to *derive* a transient
  `SWMMElementSymbol`-equivalent from `kindRenderer(c)` on demand (instead of
  returning `m_*Sym`). Keep the struct type temporarily. Build + verify paint +
  editors unchanged.
- **Batch B — drop the `m_*Sym` members** and `syncSingleRendererFromStruct` /
  `styleFromElementSymbol` / `elementSymbolFromStyle` round-trips; have
  `set*Symbol()` write straight to the renderer. Build + verify each editor
  (single symbol, marker shape/colour, line, polygon) still applies.
- **Batch C — consumers.** `swmmlayeritem.cpp` (4), `markersymbollayer.cpp` (1),
  `swmmvisprojectwindow.cpp` (2), dormant `swmmlayerglrenderer.cpp` (1): route
  through the renderer / new accessors; delete the dormant GL ref.
- **Batch D — delete the struct type** + any now-orphaned headers. Full `ctest`.
- **G-2 (parallel):** same pattern for `GISVectorLayer::m_symbol`
  (`GISVectorSymbol`) — paint straight from the renderer, retire the struct.
  Lower priority; the bidirectional bridge already keeps it in sync, so this is
  pure cleanup.

Risk: high. Each batch must build before the next. Stage 2 is *architecture
cleanup* — once Stage 1 lands, GPU==CPU and the struct is an internal detail, so
Stage 2 can be deferred without any user-facing gap.

---

## #36 — Symbology undo/redo (persistence already done)

Legend edits already push `QUndoCommand`s to the canvas undo stack
(`SetRendererClassColorCommand` / `…SizeCommand`). Dialog / kind-tree edits
apply directly (with in-session Cancel-rollback only). To get app-level undo:

1. **A renderer-install command.** Add `SetKindRendererCommand(layer, category,
   newRendererJson)` capturing old+new renderer JSON; redo/undo call
   `setKindRenderer(cat, makeRendererFromJson(...))`. Route
   `KindRendererPanel::installRenderer` and `kindtreesymbologypanel` edits
   through it instead of calling `setKindRenderer` directly.
2. **Sublayer-style + label edits.** Wrap `FeatureSublayerStyle` mutations
   (incl. the new labelConfig) in a `SetSublayerStyleCommand(style, oldJson,
   newJson)` using the style's `toJson()/fromJson()`.
3. **GIS / single-symbol adapter edits.** Same pattern via the adapter's
   symbol JSON.
4. Keep merge (`mergeWith`) for rapid spinbox/slider drags so one drag = one
   undo step (mirror the legend commands).

Additive (no deletions), but broad (touches every editor). Build incrementally;
verify undo/redo round-trips a colour edit, a renderer-type switch, and a label
edit.

---

## Recommended order

1. #33 Stage 1 (closes the real GPU≠CPU bug; contained, verifiable).
2. #36 undo (additive; independent).
3. #33 Stage 2 + G-2 (cleanup; deferrable; most destructive — last).
