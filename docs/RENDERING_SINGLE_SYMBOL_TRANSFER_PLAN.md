# Rendering — Single-Symbol Attribute Transfer Plan

**Status:** ⏳ draft 2026-05-29 — awaiting approval.

Subordinate plan under [`RENDERING_RULE_MODEL_PLAN.md`](RENDERING_RULE_MODEL_PLAN.md). Closes the gap noted in §20.7 ("Z.11a paint" prereq) and §20.2 (Rule path is missing knobs that the legacy per-kind path exposes).

This plan covers exactly one user-visible promise: **every attribute that the Single Symbol panel exposes for an object type renders on screen for that object type, in both the SWMM instance layer and the SWMM results layer, with no inapplicable knobs appearing in the editor.**

It does not extend the set of renderer classes (that's Z.9 / Z.10), the symbol-level paint order (Z.11a), or the data-defined override surface (Z.7). It plumbs the *Single Symbol* attribute path through the three layer types end-to-end.

---

## 1. Why this plan exists

Three things diverge today:

1. **The Rule-path panel shows fewer knobs than the legacy panel.** `SymbolStyleAdapter` (`include/render/symbolstyleadapter.h`) exposes 6 Q_PROPERTYs (`opacity`, `fillColor`, `strokeColor`, `strokeWidth`, `markerSize`, `markerShape`). The legacy `SwmmElementSymbolAdapter` (`include/layers/swmmelementsymboladapter.h`) exposes 12 (adds `outlineColor`, `outlineWidth`, `showLabel`, `labelFont`, `labelColor`, `showArrows`, `arrowSize`, `arrowColor`, `arrowOnlyWhenFlowPos`). Same panel, two adapter contracts.
2. **Edits via the Rule path don't reach `SWMMModelLayer`'s painter for most attributes.** `src/map/swmmlayeritem.cpp` reads the legacy `m_*Sym` `SWMMElementSymbol` struct directly for fill, outline, size, marker shape, labels, arrows. The only field it consults from the Rule's `SingleSymbolRenderer` is the line-symbol-layer `offsetPx` (`lineOffsetForKindRenderer`). So changing fill colour in the Rule-path Single Symbol panel for Junctions has no effect on screen.
3. **The editor offers attributes that don't apply to the target geometry.** A line kind (Conduits) currently exposes `markerSize` and `markerShape` through the QPropertyModel; a point kind (Junctions) exposes `strokeWidth` of the line semantic. The user wants incompatible knobs hidden — not greyed-out, not no-op writes, *hidden*.

The 2D mesh layer already solved (2) with the **Z.6a step 3** pattern in `src/layers/swmm2dmeshlayer.cpp` (lines 1151–1280): each Rule's `rendererReplaced` signal lands in a lambda that extracts a typed spec from the Rule's first `SymbolLayer` and writes the relevant fields onto the legacy sublayer style. This plan extends that pattern to SWMM 1D (model + results).

---

## 2. Current state — three adapter paths

| Path | Trigger | Adapter / style class | Property surface | Painter consumes? |
|---|---|---|---|---|
| **Rule path** | `RendererPanelContext::rule != nullptr` | `SymbolStyleAdapter` over the Rule's `SingleSymbolRenderer::symbol()` | 6 Q_PROPERTYs | Only line offset (1 of 6) for `SWMMModelLayer`; color/size/width/shape via `symbolFor()` for `SWMMResultsLayer`; labels and arrows: no |
| **Model legacy** | `qobject_cast<SWMMModelLayer*>(ctx.hostLayer)` w/ category | `SwmmElementSymbolAdapter` over `SWMMElementSymbol` | 12 Q_PROPERTYs grouped Fill / Outline / Symbology / Labels / Flow arrows | Yes — `swmmlayeritem.cpp` reads `m_*Sym` directly |
| **Results sublayer** | `qobject_cast<SWMMResultsLayer*>(ctx.hostLayer)` w/ category | `PointFeatureSublayerStyle` (4 knobs) / `LineFeatureSublayerStyle` (7 knobs incl. arrows + `renderAsLine`) / `PolygonFeatureSublayerStyle` (3 outline/fill knobs) plus base `FeatureSublayerStyle` (attribute / color / useColorRamp) | Geometry-specific (already) | Yes for color/size/width/dash via `symbolFor()` extract helpers; labels: no (live on a separate decoration path, not here) |

The Rule path is the union path — it has to read and write *something* that ends up on the canvas regardless of which underlying layer hosts the Rule.

---

## 3. Goal — attribute matrix by geometry archetype

The Single Symbol panel surfaces exactly the attributes appropriate to the underlying geometry. Anything else is hidden (not present in the property tree, not greyed-out).

**Point archetype** — Junctions, Outfalls, Storage, Dividers, RainGages (model) and the equivalent SWMMResultsLayer point categories:

| Attribute | Q_PROPERTY | Group | Notes |
|---|---|---|---|
| opacity | `opacity` | Symbology | overall symbol opacity, [0, 1] |
| markerShape | `markerShape` | Symbology | typed `MarkerShape` enum (existing `MarkerShapeEditor`) |
| markerSize | `markerSize` | Symbology | px |
| fillColor | `fillColor` | Fill | |
| outlineColor | `outlineColor` | Outline | maps to legacy `outlineColor` + SymbolLayer prop `outlineColor` |
| outlineWidth | `outlineWidth` | Outline | px |
| showLabel | `showLabel` | Labels | |
| labelFont | `labelFont` | Labels | |
| labelColor | `labelColor` | Labels | |

**Line archetype** — Conduits, Pumps, Orifices, Weirs, Outlets:

| Attribute | Q_PROPERTY | Group | Notes |
|---|---|---|---|
| opacity | `opacity` | Symbology | |
| lineColor | `lineColor` | Line | reads SymbolLayer `color` |
| lineWidth | `lineWidth` | Line | reads SymbolLayer `width` |
| dashPattern | `dashPattern` | Line | `Qt::PenStyle` enum |
| offsetPx | `offsetPx` | Line | perpendicular polyline offset (Slice Z.5b; already plumbed) |
| renderAsLine | `renderAsLine` | Line | conduits default true; pumps/orifices/weirs/outlets default false (current `LineFeatureSublayerStyle`) |
| showLabel | `showLabel` | Labels | |
| labelFont | `labelFont` | Labels | |
| labelColor | `labelColor` | Labels | |
| showArrows | `showArrows` | Flow arrows | |
| arrowSize | `arrowSize` | Flow arrows | px |
| arrowColor | `arrowColor` | Flow arrows | |
| arrowOnlyWhenFlowPos | `arrowOnlyWhenFlowPos` | Flow arrows | model-only knob; results sublayer ignores |

**Polygon archetype** — Subcatchments:

| Attribute | Q_PROPERTY | Group | Notes |
|---|---|---|---|
| opacity | `opacity` | Symbology | |
| fillColor | `fillColor` | Fill | |
| fillOpacity | `fillOpacity` | Fill | results sublayer already has this; model layer inherits via opacity |
| outlineColor | `outlineColor` | Outline | |
| outlineWidth | `outlineWidth` | Outline | px |
| showLabel | `showLabel` | Labels | |
| labelFont | `labelFont` | Labels | |
| labelColor | `labelColor` | Labels | |

The arrow group is hidden for point and polygon archetypes. The marker group is hidden for line and polygon archetypes. The line group is hidden for point and polygon archetypes (subcatchment outline lives in Outline, not Line).

---

## 4. Architecture decisions

### 4.1 Single archetype-aware adapter — `SymbolStyleAdapter` grows three subclasses

`SymbolStyleAdapter` becomes the union; archetype is picked at construction by inspecting the Rule's `SingleSymbolRenderer::symbol().layers.first().kind`. Three subclasses:

```cpp
class PointSymbolStyleAdapter   : public SymbolStyleAdapter { /* marker + label props */ };
class LineSymbolStyleAdapter    : public SymbolStyleAdapter { /* line + label + arrow props */ };
class PolygonSymbolStyleAdapter : public SymbolStyleAdapter { /* fill + outline + label props */ };
```

The factory `SymbolStyleAdapter::createFor(Rule*)` picks the subclass based on the rule's geometry. Each subclass exposes only the Q_PROPERTYs that apply to its archetype, with `Q_CLASSINFO("group:…")` grouping that mirrors the existing `SwmmElementSymbolAdapter` groups. The QPropertyModel-driven editor then *only sees* the appropriate properties — no special-casing needed in the editor.

Why subclass rather than runtime hide: the QPropertyModel walks the metaobject. There is no per-instance property filter. Subclassing is the cheapest path to "incompatible properties not visible at all".

### 4.2 Rule → legacy-style back-propagation (model layer)

`SWMMModelLayer` wires per-category `rendererReplaced` handlers in the same shape as `swmm2dmeshlayer.cpp` Z.6a step 3. For each of the 11 categories, when its Rule's renderer is a `SingleSymbolRenderer`, the handler:

1. Reads the first `SymbolLayer`'s props bag,
2. Maps them into the matching `SWMMElementSymbol` fields,
3. Calls the existing `set*Symbol(sym)` setter (already flags `m_needsRebuild = true` and emits `repaintRequested()`).

The painter stays as it is — it reads `m_*Sym`. The Rule path becomes a *writer* into that struct. Edits via the legacy `SwmmElementSymbolAdapter` continue to work unchanged because they target the same setter.

This is intentional: it preserves the legacy painter and the per-kind defaults, avoids the ~500 LOC SymbolStyle-through-SWMM1D-paint refactor noted in `RENDERING_RULE_MODEL_PLAN.md` §20.7, and bounds the change to a single integration file plus the adapter class.

### 4.3 Rule → results sublayer style back-propagation

`SWMMResultsLayer` already consults `kindRenderer->symbolFor()` for per-feature color / size / width / shape / dash via the `extractStyle*` helpers (`src/layers/swmmresultslayer.cpp` lines 1850–1854). What's missing is:

- **Labels and label appearance** — currently not in the SymbolLayer props bag. Add label fields to `MarkerSymbolLayerSpec` and `LineSymbolLayerSpec` (`showLabel`, `labelFont`, `labelColor`) and wire `rendererReplaced` handlers per category to push these onto the matching `FeatureSublayerStyle` derived class.
- **Arrow attributes for line categories** — extract `showArrows` / `arrowSize` / `arrowColor` from the `LineSymbolLayer.arrowSpec` (already exists in `RENDERING_RULE_MODEL_PLAN.md` §6.2) and write onto `LineFeatureSublayerStyle::showFlowArrows` / `arrowLengthPx` / `arrowColor`.

The existing painter then consumes them as it does today.

### 4.4 How single-symbol coexists with animated outputs

This was the user's explicit concern. Two cases:

**Case A — Active Rule's renderer is Single Symbol.** The Single Symbol panel edits the static SymbolStyle. There is no per-frame attribute binding. Animation playback redraws the same colour/size every frame — single-symbol is geometry styling, not animation. The legend dock shows one swatch.

**Case B — Active Rule's renderer is Graduated / Categorized / Heatmap (animation case).** The Single Symbol panel is **not** visible. The Symbology tab's renderer dropdown is on Graduated; its panel surfaces classification controls (attribute, breaks, ramp, rebin-per-frame) per `RENDERING_RULE_MODEL_PLAN.md` §14. The static fallback colour for un-classifiable features is one knob on the Graduated panel, not a separate single-symbol pane.

Where this currently confuses the editor: a layer with a *mix* of single-symbol kinds (e.g., RainGages — no result data) and graduated kinds (Junctions — depth animated). The Rule Model handles this by giving each kind its own Rule, so the active-rule combo selects which kind's panel is showing. This plan does not change that — it only ensures that for the active rule that *is* Single Symbol, its full attribute set lands on screen.

**Failure mode this plan closes:** a user switches the Junctions Rule from Graduated to Single Symbol mid-session for a static look, edits fill colour, and nothing changes on the canvas. After this plan, the back-propagation handler writes the new fill into `SWMMElementSymbol::fillColor` (model) or `PointFeatureSublayerStyle::color` (results) and the painter picks it up on the next repaint.

### 4.5 Live attributes vs. animation overrides — paint order

When animation is running and the active Rule for, say, Junctions, is Graduated, per-feature colour comes from the renderer's `symbolFor()` output via `m_kindFeatureColors`. The static `m_*Sym.fillColor` is the *fallback* used only when `kindUsesOverrides(cat) == false`. The painter already gates on this. This plan preserves that gating — back-propagation only touches the static fields and doesn't fight the per-feature override path.

When the user switches the Rule back to Single Symbol, `kindUsesOverrides()` flips to false (no per-feature override array), and the painter naturally falls back to the static `m_*Sym` fields — which now reflect the Single Symbol panel's edits.

---

## 5. Slice list

Each slice ships with a verifiable success criterion (per `CLAUDE.md` §4) and is sized for one focused session.

Legend: ⏳ pending · 🟡 partial · ✅ shipped

| # | Slice | Description | Success criterion | Status |
|---|---|---|---|---|
| **SS.1** | Archetype-aware `SymbolStyleAdapter` subclasses | Split into `PointSymbolStyleAdapter` / `LineSymbolStyleAdapter` / `PolygonSymbolStyleAdapter`. Each exposes only its archetype's Q_PROPERTYs with `Q_CLASSINFO("group:…")` markers matching `SwmmElementSymbolAdapter`. Factory `createFor(Rule*)` picks the subclass from the first `SymbolLayer.kind`. | Unit test: factory returns the correct subclass for each `SymbolLayerKind`; QPropertyModel walks each subclass's metaobject and lists exactly the matrix in §3 (10 / 13 / 8 properties for point / line / polygon). | ⏳ |
| **SS.2** | `SingleSymbolPanel` consumes the new factory | `singlesymbolrendererpanel.cpp` Rule path calls `SymbolStyleAdapter::createFor(rule)` instead of constructing the base class. No other panel changes. | Manual test on the snoopy lagoon fixture: opening Symbology → Single Symbol shows the matrix from §3 per category; no marker knobs on Conduits, no arrow knobs on Junctions. | ⏳ |
| **SS.3** | Extend `MarkerSymbolLayerSpec` / `LineSymbolLayerSpec` with label + arrow fields | Add `showLabel` / `labelFont` / `labelColor` to both specs; add `showArrows` / `arrowSize` / `arrowColor` / `arrowOnlyWhenFlowPos` to line spec (mirroring `SWMMElementSymbol`). JSON round-trip preserves them. | New `test_symbolstylespecs_labels_arrows.cpp` round-trips fixtures with each field set. | ⏳ |
| **SS.4** | Rule → `SWMMElementSymbol` back-propagation in `SWMMModelLayer` | For each of 11 categories, connect `rule->rendererReplaced` → lambda that extracts the typed spec and calls the matching `set*Symbol(sym)`. Pattern mirrors `swmm2dmeshlayer.cpp` Z.6a step 3 (lines 1151–1280). | Edit fill colour in the Single Symbol panel via Rule for Junctions → canvas repaint shows the new fill colour. Repeat for outline / marker shape / labels / arrows on Conduits. | ⏳ |
| **SS.5** | Rule → `FeatureSublayerStyle` back-propagation in `SWMMResultsLayer` | Add per-category `rule->rendererReplaced` lambdas that copy label fields from `MarkerSymbolLayerSpec` / `LineSymbolLayerSpec` onto the matching `PointFeatureSublayerStyle` / `LineFeatureSublayerStyle` / `PolygonFeatureSublayerStyle`. Color / size / width / dash already flow via `symbolFor()` extract helpers; this slice fills in labels and arrows. | Edit label visibility on Junctions results layer via Single Symbol → label appears/disappears on the canvas. Edit arrow colour on Conduits results → arrow recolours. | ⏳ |
| **SS.6** | Restore-defaults parity | Calling `resetKindRendererToDefaults()` (results) or `set*Symbol(defaults)` (model) refreshes the Single Symbol panel via the existing `rendererReplaced` cascade. | Click "Reset" — panel widgets re-read default values without a manual close/reopen. | ⏳ |
| **SS.7** | Animation-mode visibility test | Switch the active Rule's renderer to Graduated → Single Symbol panel disappears from the Symbology tab; switch back → it reappears with the static values intact. | Manual fixture: Junctions Graduated on depth, edit ramp, switch back to Single Symbol — fill colour edit from SS.4 is preserved across the swap. | ⏳ |
| **SS.8** | Verification — pixel-diff regression | Render the snoopy lagoon fixture with a known Rule List, save baseline, edit every attribute in the §3 matrix via the Single Symbol panel (Rule path), save edited image, diff. Each edit must show its effect (no silent no-ops). | Diff report shows pixel changes for every edited attribute, no changes for unedited ones. | ⏳ |

Total ETA: 6–8 sessions at the slice cadence established by Z.13 / Z.14 / Z.15.

---

## 6. Files touched (representative)

- `include/render/symbolstyleadapter.h` + `src/render/symbolstyleadapter.cpp` — split into 3 subclasses + factory (SS.1)
- `src/ui/dialogs/editors/singlesymbolrendererpanel.cpp` — Rule path uses factory (SS.2)
- `include/render/markersymbollayerspec.h` / `linesymbollayerspec.h` (+ matching `.cpp`) — add label + arrow fields (SS.3)
- `src/layers/swmmmodellayer.cpp` — wire 11 `rendererReplaced` lambdas in the ctor (SS.4)
- `src/layers/swmmresultslayer.cpp` — wire 11 `rendererReplaced` lambdas for label + arrow back-propagation (SS.5)
- `tests/gui/test_symbolstyleadapter.cpp` — extend with archetype-factory cases (SS.1, SS.7)
- New: `tests/gui/test_symbolstylespecs_labels_arrows.cpp` (SS.3)

---

## 7. Out of scope (explicit non-goals)

- Symbol Levels cross-feature ordering (Z.11a) — still requires the SymbolStyle-through-paint-host refactor.
- Replacing `m_*Sym` with a `SymbolStyle` member on `SWMMModelLayer`. Back-propagation is the chosen compromise.
- Adding new renderer classes (Heatmap, Cluster, etc.) — Z.9 owns those.
- Data-defined overrides on the new label / arrow fields — Z.3's `ε` button already handles every Q_PROPERTY uniformly; no extra wiring needed once SS.1 lands.
- Rule path for `GISVectorLayer` — that layer's painter already consumes the SymbolStyle via `symbolFor()`. Same audit pattern applies but is a separate plan if needed.

---

## 8. Verification — how we know it worked

The single qualitative check: open Layer Properties on a `SWMMModelLayer`, select Junctions, switch Symbology to Single Symbol via the Rule path, change every visible attribute one by one. Each change appears on screen within one paint cycle. No knob in the panel is a no-op. No relevant knob is missing.

The same check for Conduits (line), Subcatchments (polygon), and the equivalent categories on `SWMMResultsLayer`.

Pixel-diff baselines (SS.8) automate this for regression.
