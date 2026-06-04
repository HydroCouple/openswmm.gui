# Rendering — 2D Results Styling Exposure Plan

**Status:** ⏳ draft 2026-05-29 — awaiting approval.

Subordinate plan under [`RENDERING_RULE_MODEL_PLAN.md`](RENDERING_RULE_MODEL_PLAN.md) and [`RENDERING_SINGLE_SYMBOL_TRANSFER_PLAN.md`](RENDERING_SINGLE_SYMBOL_TRANSFER_PLAN.md). Closes the gap noted in the SS.1 audit: the dialog mounts the Single Symbol panel for every 2D-results rule, but the adapter factory falls back to the generic `SymbolStyleAdapter` (opacity only) for the six raster `SymbolLayerKind`s plus the new VectorGlyph kind. Result: the user sees one knob (`opacity`) on every 2D rule.

This plan also adds a velocity-vector seed rule + back-propagation so the existing `VelocityVectorStyle` (which already paints magnitude-scaled arrows on the canvas — see `swmm2dresultslayer.cpp:491–594`) becomes editable through the Symbology tab.

The 1D case (`SWMMResultsLayer`) is already covered end-to-end by Slices SS.1–SS.5.

---

## 1. Current state

`SWMM2DResultsLayer` already builds 7 sublayers + 7 Rules:

| # | Sublayer | Style class | SymbolLayerKind | Painter consumes? | Dialog edits? |
|---|---|---|---|---|---|
| 1 | MeshFillSublayer | `MeshFillStyle` | Hillshade-or-similar | ✅ | ❌ (opacity only) |
| 2 | DepthColorRampSublayer | `DepthColorRampStyle` | RasterColorRamp | ✅ | ❌ (opacity only) |
| 3 | ContourBandSublayer | `ContourBandStyle` | Contour (mode=Filled) | ✅ | ❌ (opacity only) |
| 4 | IsolineSublayer | `IsolineStyle` | Contour (mode=Lines) | ✅ | ❌ (opacity only) |
| 5 | VelocityVectorSublayer | `VelocityVectorStyle` | none yet — needs `VectorGlyph` | ✅ | ❌ (opacity only) |
| 6 | MeshEdgeSublayer | `MeshEdgeStyle` | MeshEdge | ✅ | ❌ (opacity only) |
| 7 | MeshNodeSublayer | `MeshNodeStyle` | MeshNode | ✅ | ❌ (opacity only) |

The Z.6a "Rule → legacy style back-propagation" pattern in `swmm2dmeshlayer.cpp:1151–1280` covers 6 of these (everything except velocity). The corresponding `swmm2dresultslayer.cpp` `buildRuleListLazy` also wires 6 of 7 — velocity is missing entirely (no rule, no spec, no back-prop).

---

## 2. Goal

Each of the 7 rules edits its style's full Q_PROPERTY surface through the Symbology tab. Edits propagate live to the canvas via the existing `rendererReplaced` cascade. Animation paths (`currentTimeStepChanged` → `restyleScene` → repaint) are unchanged — this plan touches only the styling editor surface and the Rule → style back-prop.

---

## 3. Architecture — same pattern as SS.1

`SymbolStyleAdapter::createFor(rule)` already returns archetype-specific subclasses (`Point` / `Line` / `Polygon`) based on the first SymbolLayer's kind. The "Other" fallback returns the generic adapter. This plan extends `createFor` to cover the remaining 7 raster kinds with new archetype subclasses:

| SymbolLayerKind | Adapter | Style writeback target |
|---|---|---|
| `RasterColorRamp` | `RasterColorRampSymbolStyleAdapter` | `DepthColorRampStyle` |
| `Hillshade` (overload for MeshFill) | `HillshadeSymbolStyleAdapter` | `MeshFillStyle` |
| `Contour` (mode=Filled) | `ContourBandSymbolStyleAdapter` | `ContourBandStyle` |
| `Contour` (mode=Lines) | `IsolineSymbolStyleAdapter` | `IsolineStyle` |
| `MeshEdge` | `MeshEdgeSymbolStyleAdapter` | `MeshEdgeStyle` |
| `MeshNode` | `MeshNodeSymbolStyleAdapter` | `MeshNodeStyle` |
| `VectorGlyph` (new) | `VelocityVectorSymbolStyleAdapter` | `VelocityVectorStyle` |

Each subclass declares only its style's Q_PROPERTYs, grouped with `Q_CLASSINFO("group:…")` mirroring the source style's groups. QPropertyModel walks the concrete metaobject — no incompatible knobs surface.

Contour bands vs lines are two distinct sublayers backed by two distinct style classes; both use `SymbolLayerKind::Contour`. The factory disambiguates by reading the layer's `mode` prop (`"filled"` vs `"lines"`).

### 3.1 Why not extend `SymbolStyleAdapter` directly

Subclasses with their own metaobject keep "incompatible properties not visible" as the QPropertyModel-walked surface (same rationale as SS.1). Generic fallback retained for unknown kinds and rules with no SymbolLayer (the absolute-minimum case).

---

## 4. New plumbing

### 4.1 `SymbolLayerKind::VectorGlyph`

Added to `include/render/symbollayer.h` as the 14th kind. Round-trips through JSON via `symbolLayerKindToString` / `symbolLayerKindFromString` (token `"vectorGlyph"`).

### 4.2 `VelocityVectorSymbolLayerSpec`

New value type in `include/render/velocityvectorsymbollayer.h`, mirroring `VelocityVectorStyle`:

```cpp
struct VelocityVectorSymbolLayerSpec
{
    qreal  glyphLengthScalePxPerMps = 20.0;
    qreal  glyphLengthMinPx         = 4.0;
    qreal  glyphLengthMaxPx         = 40.0;
    qreal  glyphSpacingPx           = 30.0;
    qreal  headSizePx               = 5.0;
    QColor color                    = QColor(20, 20, 20, 220);
    qreal  dryDepthCutoff           = 0.01;

    [[nodiscard]] SymbolLayer toSymbolLayer() const;
    [[nodiscard]] static VelocityVectorSymbolLayerSpec fromSymbolLayer(const SymbolLayer &);
    void writeToSymbolLayer(SymbolLayer &) const;
};
```

Canonical prop keys: `"glyphLengthScalePxPerMps"`, `"glyphLengthMinPx"`, `"glyphLengthMaxPx"`, `"glyphSpacingPx"`, `"headSizePx"`, `"color"`, `"dryDepthCutoff"`.

### 4.3 Spec extensions (DepthColorRamp / Contour / MeshNode)

| Spec | Add fields | Source style |
|---|---|---|
| `RasterColorRampSymbolLayerSpec` | `attribute` (QString), `useLogScale` (bool), `belowMinColor` (QColor), `aboveMaxColor` (QColor) | `DepthColorRampStyle` |
| `ContourSymbolLayerSpec` | `attribute`, `belowMinColor`, `aboveMaxColor`, `bandCount` (int) | `ContourBandStyle` + `IsolineStyle` |
| `MeshNodeSymbolLayerSpec` | `highlightTagged` (bool), `taggedColor` (QColor), `taggedSizePx` (qreal) | `MeshNodeStyle` |

Adapter writes target prop keys; spec read/write follows the established pattern.

### 4.4 Seed rule + back-prop on `SWMM2DResultsLayer`

`buildRuleListLazy` appends a 7th rule:

```cpp
SingleSymbolRenderer  velRen;
SymbolStyle           sym;
SymbolLayer           layer;
layer.kind = SymbolLayerKind::VectorGlyph;
VelocityVectorSymbolLayerSpec{}.writeToSymbolLayer(layer);
sym.layers.append(layer);
velRen.setSymbol(sym);
auto *rule = m_ruleList->append(std::make_unique<Rule>(
    QStringLiteral("Velocity vectors"), std::move(velRen)));
```

`rendererReplaced` lambda mirrors the Z.6a pattern: extract `VelocityVectorSymbolLayerSpec::fromSymbolLayer(layer)`, write onto `VelocityVectorStyle` via setters; that emits `invalidated()`, which the existing wiring routes to `repaintRequested()`.

### 4.5 Audit existing 6 back-prop lambdas

The mesh-layer Z.6a lambdas may not yet write every newly-exposed field (e.g. `useLogScale` on depth, `dashPattern` on isolines, slope-driven width on mesh edges). One slice (AN.4) audits and fills gaps so the editor → style → painter cycle covers every Q_PROPERTY this plan surfaces.

---

## 5. Slice list

| # | Slice | Description | Success criterion | Status |
|---|---|---|---|---|
| **AN.1** | `VectorGlyph` kind + `VelocityVectorSymbolLayerSpec` | Enum + spec + JSON round-trip | round-trip unit test passes | ⏳ |
| **AN.2** | 7 archetype-aware adapter subclasses + factory dispatch | Each subclass declares its style's Q_PROPERTYs + groups; `createFor` routes all 13+ kinds | Factory returns the matching concrete type for each kind | ⏳ |
| **AN.3** | Velocity-vector seed rule + back-prop on `SWMM2DResultsLayer` | 7th rule appears in the rule list; edits write to `VelocityVectorStyle` | Edit color in dialog → arrows recolour on next paint | ⏳ |
| **AN.4** | Audit + fill back-prop gaps on `SWMM2DMeshLayer` Z.6a lambdas | Every Q_PROPERTY surfaced by an AN.2 adapter has a matching writeback | Manual test on snoopy lagoon: each knob changes pixels | ⏳ |
| **AN.5** | Spec extensions (DepthColorRamp / Contour / MeshNode missing fields) | New fields round-trip through JSON | JSON round-trip test passes | ⏳ |

---

## 6. Files touched (representative)

- `include/render/symbollayer.h` / `src/render/symbollayer.cpp` — add `VectorGlyph` enum value + token mapping (AN.1)
- `include/render/velocityvectorsymbollayer.h` / `src/render/velocityvectorsymbollayer.cpp` — new spec (AN.1)
- `include/render/rastersymbollayers.h` / `src/render/rastersymbollayers.cpp` — extend specs (AN.5)
- `include/render/symbolstyleadapter.h` / `src/render/symbolstyleadapter.cpp` — 7 new adapter subclasses + factory dispatch (AN.2)
- `src/layers/swmm2dresultslayer.cpp` — seed velocity rule + back-prop lambda (AN.3)
- `src/layers/swmm2dmeshlayer.cpp` — audit + extend existing 6 Z.6a back-prop lambdas (AN.4)
- `CMakeLists.txt` — register new sources

---

## 7. Out of scope (explicit non-goals)

- Adding new sublayer kinds (the 7 above are all the user-editable styles).
- Auto-stretching ramps / auto-fitting velocity scale — defaults slice, separate.
- Migration of legacy `velocityArrowScale()` / `colorClasses()` Q_PROPERTYs on `SWMM2DResultsLayer` itself. Those remain on the layer panel; the rule-driven path becomes the user-facing surface, the legacy fields become defaults the Rule's spec seeds from.
- 1D velocity arrows (deferred — engine doesn't expose link velocity directly).

---

## 8. Verification

The single qualitative check: open Layer Properties on a `SWMM2DResultsLayer`, walk all 7 rules. Each rule's renderer panel shows the full Q_PROPERTY surface for its style (the matrix in §1 column 3). Edit every visible knob — each change appears on the canvas within one paint cycle. No knob is a no-op. Animate, confirm depth + velocity update per frame.
