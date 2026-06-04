# Rendering — Dialog Demo Readiness Plan

**Status:** ⏳ draft 2026-05-29 — awaiting approval.

Subordinate plan under [`RENDERING_RULE_MODEL_PLAN.md`](RENDERING_RULE_MODEL_PLAN.md), specifically §4 (Symbology tab UI mock) and §11.5 (Joins). Builds on Slices SS.1–SS.5 (single-symbol surface) and AN.1–AN.4 (2D-results raster surface) — both shipped.

The user-facing complaint this plan closes: **"the properties dialog does not allow me to select what attribute to use for the styling and coloring."**

Goal: a near-term, demonstrable end-to-end theming experience on a fixture model + results — pick an attribute from a dropdown, pick a ramp from a dropdown, get correctly-coloured features on the canvas, animate.

---

## 1. Why this plan exists

The audit shows the pieces are wired but the editor surfaces are incomplete:

| Renderer panel | Attribute picker | Ramp picker | Classes / mode | Status |
|---|---|---|---|---|
| **Single Symbol** | n/a (single color) | n/a | n/a | ✅ |
| **Graduated** | ❌ **MISSING** | ✅ ColorRampComboBox (23 presets) | ✅ EqualInterval / Quantile / Manual + classes spinner | 🟡 |
| **Categorized** | 🟡 Editable combo, but list is **hardcoded** to 3–4 items per kind | ✅ inline swatches | ✅ Categories table + "sample unique" | 🟡 |
| **Rule-based** | implicit in expression | inline swatch | n/a | ✅ |
| **AN.* raster adapters** | 🟡 Exposed as free-text QString Q_PROPERTY | n/a (color stops) | classes via `bandCount` / `isoValueCount` | 🟡 |
| **Layer-level Graduated** | ❌ placeholder ("not yet supported") | — | — | ❌ |

There is no formal API on any layer that says *"here are the attributes you can theme by"* — every panel hardcodes its suggestions. For a SWMM 1D results layer, the painter has the eight standard output codes (depth, head, inflow, lateralInflow on nodes; flow, depth, velocity, capacity, volume on links; runoff, infiltration, evaporation, snowDepth on subcatchments), but the dialog can't enumerate them.

For SWMM 1D model statics (invertElev, maxDepth, length, slope, roughness, diameter, area, impervious%, …) the situation is worse — no API at all, just hand-rolled `suggestedAttributesFor()` in `categorizedrendererpanel.cpp:114`.

---

## 2. Architecture — `IAttributeProvider` seam

Add one virtual interface that every themeable layer implements:

```cpp
// include/render/iattributeprovider.h
namespace OpenSWMM::Render {

struct AttributeField
{
    QString name;                      // canonical key, used by renderer
    QString displayName;               // i18n label for the combo
    QVariant::Type type = QVariant::Double;
    bool    isDynamic = false;         // varies per animation frame
    QString unit;                      // "m", "m/s", "cfs", ""
};

class IAttributeProvider
{
public:
    virtual ~IAttributeProvider() = default;

    /*! Available themeable fields for a category. Empty for layers
     *  that don't carry per-category attributes. Order = preferred
     *  presentation order in the picker. */
    [[nodiscard]] virtual QVector<AttributeField>
        availableAttributes(OpenSWMMVis::SwmmCategory cat) const = 0;
};

} // namespace
```

Implementations (each ~30 LOC):

| Layer | Fields surfaced |
|---|---|
| `SWMMModelLayer` | per-kind static engine fields: `Junctions/Outfalls/Storage/Dividers` → `invertElev`, `initDepth`, `maxDepth`, `surfaceArea`. `Conduits/Pumps/Orifices/Weirs/Outlets` → `length`, `slope`, `roughness`, `maxDepth`, `diameter`, `inletOffset`, `outletOffset`. `Subcatchments` → `area`, `width`, `slope`, `impervPct`, `nImperv`, `nPerv`. `RainGages` → none (no per-feature attrs). `tag` available everywhere. |
| `SWMMResultsLayer` | per-kind output codes from `nodeOutCodeForAttribute` / `linkOutCodeForAttribute` / `subcatchOutCodeForAttribute`. All `isDynamic = true`. |
| `SWMM2DResultsLayer` | mesh fields: `depth`, `head`, `vmag`, `vx`, `vy`. All `isDynamic = true`. |
| `GISVectorLayer` | reuses existing `fieldNames()` → wrap each in an `AttributeField`. |

Renderer panels (`GraduatedRendererPanel`, `CategorizedRendererPanel`, the AN.* raster adapters' attribute slots) consume `IAttributeProvider` via `RendererPanelContext::hostLayer`. If `qobject_cast<IAttributeProvider*>(ctx.hostLayer)` succeeds, the panel populates its attribute combo from `availableAttributes(*ctx.category)`. Otherwise it falls back to the existing free-text path (preserves current Rule-based behaviour, GIS-vector layers without category, etc.).

This is the smallest API that closes every gap in the audit.

---

## 3. Slice list

Each slice gated with a verifiable success criterion (per `CLAUDE.md` §4). Ordered for shortest path to demo.

| # | Slice | Description | Success criterion | LOC est. |
|---|---|---|---|---|
| **DM.1** | `IAttributeProvider` interface + SWMMResultsLayer impl | Header-only interface; SWMMResultsLayer returns the 8 standard 1D output codes per category, all `isDynamic=true`. | Calling `availableAttributes(CatConduits)` returns 5 entries including `flow` / `depth` / `velocity`. Unit test. | ~120 |
| **DM.2** | GraduatedRendererPanel attribute picker | `KindRendererPanel` ctor adds a labelled QComboBox above the existing graduated controls. Populated from `IAttributeProvider::availableAttributes(*ctx.category)` (when host is an `IAttributeProvider`). Selecting an entry calls `Rule->renderer()->setClassifyAttribute(name)`. Combo is hidden when the host has no provider (preserves rule-based + GIS-vector flow). | Open dialog on a 1D results layer fixture, pick "Conduits → Graduated", attribute combo lists 5 entries, picking `flow` recolours conduits live. | ~80 |
| **DM.3** | SWMMModelLayer + SWMM2DResultsLayer + GISVectorLayer impls | Add `availableAttributes` to each. Model layer lists static engine fields per kind. 2D layer lists `depth`/`head`/`vmag`. GIS layer wraps `fieldNames()`. | All four layer types return non-empty lists for their respective categories. Unit test per layer. | ~150 |
| **DM.4** | CategorizedRendererPanel uses provider | Replace `suggestedAttributesFor()` hardcoding with a provider lookup. Falls back to the existing hardcoded list when no provider is available. | Categorized panel on a model layer's Conduits shows `length` / `slope` / `roughness` / etc. in the combo. | ~40 |
| **DM.5** | AN.* raster adapters' `attribute` editor delegate | Register a custom `QPropertyItemDelegate` for the `attribute` Q_PROPERTY on the four raster adapters that expose it (RasterColorRamp, ContourBand, Isoline, plus VelocityVector if added later). Delegate consults `IAttributeProvider` from the context. Falls through to QLineEdit when no provider. | Depth Color Ramp rule's `attribute` row in the SingleSymbolPanel tree shows a dropdown of 2D variables, not free text. | ~60 |
| **DM.6** | Enable layer-level Graduated for SWMMResultsLayer | Remove the "Graduated rendering at the layer level is not yet supported" placeholder in `graduatedrendererpanel.cpp:45–48`. Wire the layer-scope path to consult `setRenderer()` on the layer's primary kind — this is the **Z.3** path (see RuleList vocabulary). For the demo, scoping to a single chosen category is sufficient. | Layer-level Graduated panel works on a results layer; doesn't crash. | ~40 |

**Demo path is DM.1 → DM.2 → DM.3 → DM.4**, ~390 LOC. DM.5 and DM.6 are polish.

---

## 4. UI mock (post DM.2)

```
┌─ Symbology ──────────────────────────────────────────────────────────┐
│  Active Rule:  [ Conduits ▾ ]  [+] [Dup] [Del] [↑] [↓]                │
│  ──────────────────────────────────────────────────────────────────  │
│  Renderer:      [ Graduated ▾ ]                                       │
│  ──────────────────────────────────────────────────────────────────  │
│  ▸ Classify                                                            │
│      Attribute  [ flow (m³/s) ▾ ]                          ← DM.2     │
│                 ├─ depth (m)                                          │
│                 ├─ velocity (m/s)                                     │
│                 ├─ flow (m³/s)            ✓                            │
│                 ├─ capacity                                            │
│                 └─ volume (m³)                                        │
│      Mode       [ Quantile ▾ ]   Classes  [ 5 ]                       │
│      Ramp       [▒▒▒▒▒▒▒▒▒▒▒ Viridis ▾]    [Auto-classify from data]  │
│                                                                       │
│  ▸ Breaks  (5 rows)                                                   │
│      Lower    Upper    Colour    Label                                │
│      0.00     0.34     ▒         0.00–0.34                            │
│      0.34     0.71     ▒         0.34–0.71                            │
│      ...                                                              │
└──────────────────────────────────────────────────────────────────────┘
```

The Attribute combo is the only net-new widget. Everything else already ships from `KindRendererPanel`.

---

## 5. Files touched (representative)

- `include/render/iattributeprovider.h` — new (DM.1)
- `include/layers/swmmresultslayer.h` + `src/layers/swmmresultslayer.cpp` — implement `IAttributeProvider` (DM.1)
- `src/ui/dialogs/editors/kindrendererpanel.cpp` — add attribute combo, populate from provider (DM.2)
- `include/layers/swmmmodellayer.h` + `src/layers/swmmmodellayer.cpp` — implement provider (DM.3)
- `include/layers/swmm2dresultslayer.h` + `src/layers/swmm2dresultslayer.cpp` — implement provider (DM.3)
- `include/layers/gisvectorlayer.h` + `src/layers/gisvectorlayer.cpp` — implement provider (DM.3)
- `src/ui/dialogs/editors/categorizedrendererpanel.cpp` — replace `suggestedAttributesFor` (DM.4)
- `src/ui/dialogs/editors/singlesymbolrendererpanel.cpp` — register attribute combo delegate on raster adapters (DM.5)
- `src/ui/dialogs/editors/graduatedrendererpanel.cpp` — remove layer-level placeholder (DM.6)
- `CMakeLists.txt` — new header registered for AUTOMOC where needed
- `tests/gui/test_attributeprovider.cpp` — new (DM.1, DM.3)

---

## 6. Out of scope (explicit non-goals)

- Joined-field discovery (Z.16 / RuleListPlan §11.5) — `IAttributeProvider` returns engine-native fields only for now. Joined fields land when the runtime join engine ships.
- Data-defined override (`ε` button) — separate Z.7 surface; this plan only wires the primary attribute combo.
- Auto-stretching the range on attribute change — nice but not blocking demo.
- Custom user-authored attribute expressions (`sqrt(flow)/area`) — explicitly Z.7 territory.
- Migrating 2D hardcoded color stops to `RasterColorRamp` library — separate slice; doesn't block attribute picker.

---

## 7. Verification

**Demo fixture:** `examples/demo_weir_culvert/weir_culvert.inp` (+ `weir_culvert.out` + `weir_culvert.2d.h5`). Coupled 1D–2D model — 1000 m × 100 m flat plain with a transverse weir at X = 500 m and a 500 m culvert (50 segments, Ø 0.5 m) buried 1 m below the surface, three pipe junctions 2D-coupled, SCS Type II 24-hour rainfall driving a flood event. Exercises every path this plan touches in one project: SWMM 1D nodes + links (model + results), SWMM 2D mesh (`.2d.h5` carries depth + velocity components per cell), and the coupling between them.

Demo script (the single qualitative check):

**1D model statics — DM.3 + DM.4** (color conduits by static field)

1. Open `weir_culvert.inp`.
2. Right-click the SWMM 1D model layer → Layer Properties → Symbology.
3. Pick rule "Conduits". Renderer → **Categorized**.
4. Attribute combo: should list `length` / `slope` / `roughness` / `diameter` / `inletOffset` / `outletOffset` / `maxDepth` / `tag` (the new `availableAttributes(CatConduits)` output, not the hardcoded short list).
5. Pick `diameter`. Click **Sample unique values**. Table populates with the conduit diameters present in the model (mostly Ø 0.5 m for the culvert + the connector pipes).
6. Pick distinct colours per diameter, OK. Conduits recolour by pipe size — the culvert main line vs the connector outlets are visually distinct.

**1D results dynamics — DM.1 + DM.2** (animate conduits by velocity)

1. Same project (`.out` autoloads alongside `.inp`).
2. SWMM 1D results layer → Layer Properties → Symbology. Pick rule "Conduits". Renderer → **Graduated**.
3. Attribute combo (the net-new widget): should list `flow (m³/s)` / `depth (m)` / `velocity (m/s)` / `capacity` / `volume (m³)`. Pick **velocity**.
4. Ramp combo: pick **Viridis**. Click **Auto-classify from data**. 5 breaks materialise from the run's observed velocity range.
5. Close. Press Play.
6. The 500 m culvert lights up green→yellow→red as the flood wave moves through. The connector outlets stay blue (low velocity drainage).

**2D dynamics — DM.3 + DM.5** (animate the heatmap by velocity magnitude)

1. Same project. SWMM 2D results layer → Layer Properties → Symbology.
2. Pick rule "Depth color ramp". `attribute` row in the property tree.
3. Combo (DM.5) should list `depth (m)` / `head (m)` / `vmag (m/s)`. Pick **vmag**.
4. Press Play.
5. Heatmap shows velocity magnitude instead of depth — the channel above the weir + the culvert capture zone both light up sharply during the peak.
6. While playing: open the rule "Velocity vectors", change `glyphLengthScalePxPerMps` from 20 → 50. Arrows scale up live, every frame.

Pixel-diff regression baselines for the three demo cases live under `tests/gui/fixtures/dm_demo/`.

---

## 8. Trade-offs called out

**Adding a layer-level interface vs panel-side hardcoding.** The audit shows panels already hardcode suggestions. Adding `IAttributeProvider` centralises the knowledge where the data lives (the layer), so future additions (new engine field, new joined column, new computed attribute) flow to every panel automatically. The cost is one virtual call per panel construction. Worth it.

**Why not a `QStringList`?** The richer `AttributeField` (display name + type + isDynamic + unit) lets the combo show "flow (m³/s)" instead of "FLOW", and lets the Graduated panel decide whether to offer "Recompute breaks per frame" — Z.7 already gates that on `isDynamic`. The struct buys forward compat without changing the interface.

**Per-category vs per-layer.** `availableAttributes(category)` matches the existing per-kind rule model. A future layer-scope renderer that themes across all kinds would query each category and union the results.
