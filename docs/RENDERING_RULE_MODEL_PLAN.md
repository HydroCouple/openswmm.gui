# Rendering — Layer Rule Model Plan

**Status:** ⏳ draft 2026-05-27 — awaiting approval.

This plan supersedes the user-facing portions of:
- `RENDERING_OUTPUT_SUBLAYERS_PLAN.md` — the **rendering primitives** described there (FeatureSublayer, MeshNodeSublayer, MeshEdgeSublayer, MeshFillSublayer, DepthColorRampSublayer, IsolineSublayer, ContourBandSublayer, VelocityVectorSublayer, FlowArrowSublayer) stay as C++ paint primitives. What changes is the user-facing vocabulary and dialog surface.
- `RENDERING_STATIC_STYLING_PLAN.md` — the `KindSublayer` hierarchy proposed there is withdrawn (never shipped).
- `GUI_IMPLEMENTATION_PLAN.md §X.3.2` — the "kind-tree on left" layout in the Symbology tab is replaced by an Active Rule combo + Rule List.

It builds on (does not replace):
- `GUI_IMPLEMENTATION_PLAN.md §J` — Theming / Styling / Legend architecture. `IFeatureRenderer`, `SymbolStyle`, `ColorRamp`, `IntervalBinner`, `DataDefined` are the substrate this plan layers `Rule` on top of.
- `GUI_IMPLEMENTATION_PLAN.md §N.5–§N.9` — mesh edges, contours, velocity, HGL, labels. Those rendering capabilities survive intact; the Rule Model just repackages them as stackable Rules instead of `LayerPropertiesDialog → Mesh` checkboxes.
- `GUI_IMPLEMENTATION_PLAN.md §W` — Unified `LayerStyleDialog`. The dialog frame stays; only its Symbology tab changes shape.
- `GUI_IMPLEMENTATION_PLAN.md §X` — QGIS-style tab set (Information / Source / Symbology / Labels / Rendering / Metadata). Tab set stays; the new tabs from this plan (Diagrams / Temporal / Mask / Auxiliary Storage / Joins) extend it.

---

## 1. Why this plan exists

Three forces converged on 2026-05-27:

1. **User pass on the styling dialog.** The kind-tree-on-left layout (`§X.3.2`) and the multi-row sublayer panel (`RENDERING_OUTPUT_SUBLAYERS_PLAN.md`) both feel like internal plumbing leaking into the UI. The user wants a combo-box-driven Symbology tab whose vocabulary spans every layer kind (vector / raster / SWMM model / SWMM 1D results / SWMM 2D results / TIN) without having to know there are sublayers underneath.
2. **GIS parity audit.** Reviewing QGIS 3.34 and ArcGIS Pro feature coverage against ours: we are missing data-defined overrides on every property, symbol levels (cross-feature paint order), heatmap / point-cluster / point-displacement / inverted-polygon renderers, blend modes, draw effects, and several Layer Properties tabs (Diagrams / Temporal / Mask / Auxiliary Storage / Joins). The sublayer-only model can't host any of these.
3. **Animation parity.** Result-layer animation today drives per-frame paint via the `isDynamic()` flag on sublayers. Folding animation under a uniform Rule model — where any property can bind to a time-varying attribute with fixed bins computed once at Rule creation — makes the 1D and 2D result animation surfaces identical and the legend dock trivial.

The pivot: **`Rule` is the user-facing styling unit. A layer owns an ordered, stackable list of Rules. The Symbology tab edits one Rule at a time, selected via a combo at the top.** This term extends QGIS's Rule-based-renderer rule to **layer scope** (one level up from where QGIS uses it), keeping the GIS vocabulary consistent.

---

## 2. Vocabulary (aligned with QGIS + ArcGIS Pro)

| Concept | Term used here | QGIS equivalent | ArcGIS Pro equivalent | Notes |
|---|---|---|---|---|
| The Layer Properties dialog | **Layer Properties** | Layer Properties | Layer Properties / Symbology pane | Container, unchanged from §X |
| The styling tab | **Symbology** | Symbology | Symbology | Unchanged from §X |
| One styled aspect of a layer | **Rule** | Rule (inside Rule-based renderer) | — | We extend QGIS's Rule from sub-renderer scope to layer scope. A Rule is a (filter expression, renderer, symbol, decorations) tuple. Every renderer class (Single / Categorized / Graduated / Heatmap / etc.) is usable inside a Rule. |
| The ordered list of stacked Rules on one layer | **Rule List** | Rule list (inside Rule-based renderer) | — | Our extension: stacks Rules at layer scope. Paints in list order; top of list paints last. Per-Rule `isVisible`, `minScale`, `maxScale`. |
| The combo at the top of the Symbology tab | **Active Rule picker** | Style picker / Styles ▸ submenu | Symbology level dropdown | Combo + `[+] [Duplicate] [Delete] [↑] [↓] [Save…] [Load…]` row |
| The classification scheme inside one Rule | **Renderer** | Renderer | Primary symbology | Single Symbol / Categorized / Graduated / Rule-based / Heatmap / Point Cluster / Point Displacement / Inverted Polygon / 2.5D / Unclassed |
| The visual mark for one feature class | **Symbol** | Symbol | Symbol | Marker / Line / Fill |
| Composable parts inside one Symbol | **Symbol Layer** | Symbol Layer | Symbol layer | QGIS-native concept; one Symbol stacks N Symbol Layers (e.g., outer ring + inner fill + center dot). Distinct from the layer-scope Rule List above. |
| Cross-feature paint order | **Symbol Levels** | Symbol Levels | Symbol layer drawing order | New checkbox + ordering dialog |
| Per-property attribute expression binding | **Data-defined override** | Data-defined override | Vary symbology by attribute | "ε" button next to every property |
| Cross-tool style file | `.qml` (QGIS) + `.swmm-rule.json` (ours) | `.qml` | `.lyrx` | We write both. `.qml` for QGIS round-trip on the vector subset; `.swmm-rule.json` for the full feature set including mesh edges / contours / animation bindings. |

### 2.1 Why "Rule" and not "Style"

Three considerations pushed the term:
1. **Stacking.** QGIS Styles are single-active per layer; QGIS Rules stack. Our model stacks. The QGIS API surface (`QgsRuleBasedRenderer::Rule`) reads as the natural starting point even though we lift the concept to layer scope.
2. **Filter expression.** Each Rule can carry a filter expression (e.g., `flooded_depth > 0.05`) — the standard QGIS Rule semantic. This is how the user gets "show velocity vectors only where speed > 0.3 m/s" without modifying the renderer.
3. **Vocabulary economy.** "Style" is overloaded across CSS, Qt, and QGIS. "Rule" is unambiguous in a GIS context once the reader has seen a Rule-based renderer.

We extend QGIS's concept in exactly one way: a Rule's filter can be empty, and its renderer can be any renderer class (not only Single Symbol). When the filter is empty and the renderer is Single Symbol, a Rule reduces to a QGIS Single-Symbol layer. When the filter is non-empty and the renderer is Single Symbol, a Rule reduces to one entry in a QGIS Rule-based renderer. When the filter is empty and the renderer is Graduated / Categorized, a Rule reduces to a QGIS Graduated / Categorized layer.

---

## 3. Data model

### 3.1 `Rule` class

```cpp
class Rule : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(bool isVisible READ isVisible WRITE setVisible NOTIFY changed)
    Q_PROPERTY(QString filterExpression READ filter WRITE setFilter NOTIFY changed)
    Q_PROPERTY(double minScale READ minScale WRITE setMinScale NOTIFY changed)
    Q_PROPERTY(double maxScale READ maxScale WRITE setMaxScale NOTIFY changed)
    Q_PROPERTY(BlendMode blendMode READ blendMode WRITE setBlendMode NOTIFY changed)

public:
    IFeatureRenderer*   renderer() const;        // owned (§J.2)
    Symbol*             symbol() const;          // owned (§J.3)
    DecorationSet*      decorations() const;     // owned — labels, arrows, contours, hillshade, edges
    DataDefinedMap*     dataDefined() const;     // owned — per-property attribute bindings

    QJsonObject         toJson() const;
    static Rule*        fromJson(const QJsonObject&);
    Rule*               clone() const;
};
```

### 3.2 `RuleList` on `OpenSWMMVisLayer`

```cpp
class OpenSWMMVisLayer : public QObject {
public:
    int             ruleCount() const;
    Rule*           ruleAt(int index) const;
    int             activeRuleIndex() const;     // editor focus, not paint order
    void            setActiveRuleIndex(int);

    void            addRule(Rule*);              // takes ownership
    void            insertRule(int index, Rule*);
    void            removeRule(int index);
    void            moveRule(int from, int to);

    QJsonArray      ruleListToJson() const;
    void            ruleListFromJson(const QJsonArray&);

signals:
    void ruleListChanged();
    void activeRuleChanged(int index);
};
```

`styleSubjects()` from §W stays as a deprecated alias that returns a synthetic subject per Rule for backwards-compat with editors still using the `ILayerStyleSubject` interface. Removed in Z.8.

### 3.3 Persistence — `.swmm-rule.json`

```json
{
  "version": 1,
  "rules": [
    {
      "name": "Junctions by flooding",
      "isVisible": true,
      "filterExpression": "flooded_vol > 0",
      "minScale": 0,
      "maxScale": 0,
      "blendMode": "Normal",
      "renderer": {
        "class": "GraduatedRenderer",
        "attribute": "flooded_vol",
        "binner": { "mode": "Quantile", "classes": 5, "breaks": [0.0, 0.12, 0.34, 0.71, 1.43, 4.62] },
        "ramp": { "name": "Viridis", "invert": false },
        "rebinPerFrame": false
      },
      "symbol": {
        "archetype": "Marker",
        "layers": [
          { "shape": "Circle",
            "size": 12.0,
            "fillColor": "#3b528b",
            "outline": { "color": "#222", "width": 0.5, "style": "SolidLine" } }
        ]
      },
      "decorations": {
        "labels": { "expression": "format(flooded_vol,2) || ' m³'", "halo": { "size": 1.0, "color": "#fff" } }
      },
      "dataDefined": {
        "symbol.layers[0].size": "8 + 16 * sqrt(flooded_vol / max_flooded_vol)"
      }
    }
  ],
  "symbolLevels": { "enabled": false, "order": [] }
}
```

The file also accepts a `legacySublayerMap` block for migrating shipped `.oswp` sublayer keys (see §11). `.qml` export writes the vector-renderer subset for QGIS round-trip; full-fidelity round-trip uses `.swmm-rule.json`.

---

## 4. Symbology tab UI

```
┌─ Symbology ──────────────────────────────────────────────────────────┐
│  Active Rule:  [ Junctions by flooding ▾ ]  [+] [Dup] [Del] [↑] [↓]   │
│  ┌─ Rule List ────────────────────────────────────────────────────┐  │
│  │ ☑ Junctions by flooding         GraduatedRenderer    ε         │  │
│  │ ☑ Conduits by flow direction    SingleSymbol+Arrows  ε         │  │
│  │ ☐ Subcatchments runoff fill     GraduatedRenderer    ε         │  │
│  └────────────────────────────────────────────────────────────────┘  │
│  ──────────────────────────────────────────────────────────────────  │
│  Renderer:      [ Graduated ▾ ]                                       │
│  Filter:        [ flooded_vol > 0                              ] [ε]  │
│  Scale-visible: min [ 1:0 ]  max [ 1:0 ]   Blend [ Normal ▾ ]         │
│  ──────────────────────────────────────────────────────────────────  │
│  ▸ Classify     Attribute [ flooded_vol ▾ ] [ε]                       │
│                 Mode [ Quantile ▾ ]  Classes [ 5 ]                    │
│                 Ramp [▒▒▒▒▒▒ Viridis ▾]    [Recompute breaks…]        │
│                 ☐ Recompute breaks per animation frame                │
│                                                                       │
│  ▸ Symbol       Archetype: Marker                                     │
│    ├ Layer 1   Circle, 12 px, fill ▒, outline 0.5 px solid ▒    [ε]  │
│    └ [+ Symbol Layer]                                                 │
│                                                                       │
│  ▸ Decorations                                                        │
│    ├ Labels    expression [format(flooded_vol,2) || " m³"]      [ε]  │
│    ├ Halo      size 1.0  color ▒                                      │
│    └ [+ Decoration]   (Arrows · Halo · Contour · Hillshade · Edges)   │
│                                                                       │
│  Preview:  [ live swatch — driven by the topmost Symbol Layer ]       │
└──────────────────────────────────────────────────────────────────────┘
```

Every `ε` button opens the data-defined override editor. The Rule List supports drag-reorder; per-row checkbox toggles `isVisible`. The Active Rule combo + Rule List are two views of the same model — picking from the combo selects the row, clicking the row sets the active editor.

The decoration set is open: a Rule can have any subset of {labels, arrows, halo, contour-overlay, hillshade, edges, nodes}. Adding a decoration is `[+ Decoration]`. This is how a mesh layer expresses its current four-checkbox Mesh tab state — as four Rules in the Rule List (Cells / Edges / Nodes / Contours), each one Symbol + one decoration. The `LayerPropertiesDialog → Mesh` tab is retired in Z.6.

---

## 5. Renderer roster

Inherited from §J and extended by this plan:

| Renderer | Source | Slice |
|---|---|---|
| Single Symbol | §J.2 (existing) | Z.3 |
| Categorized | §J.2 (existing) | Z.3 |
| Graduated | §J.2 (existing) | Z.3 |
| Unclassed Colors (continuous, no bins) | new | Z.5 |
| Rule-based (nested rules inside one Rule) | §J.2 (existing) | Z.3 |
| Heatmap (point density kernel) | new | Z.9 |
| Point Cluster | new | Z.9 |
| Point Displacement | new | Z.9 |
| Inverted Polygon | new | Z.9 |
| 2.5D / Extrusion | new — deferred | Z.10 |

Classification modes inside Graduated:
- Equal Interval, Quantile, Standard Deviation, Jenks, Log (existing in §J.4)
- **Pretty Breaks** (new) — round to 1/2/2.5/5 × 10ⁿ
- **Manual** (new) — histogram-aware editor with drag-handle breaks
- **Continuous** (new) — no bins; per-feature interpolated color (sibling of Unclassed renderer)

---

## 6. Symbol model

A `Symbol` has an archetype (Marker / Line / Fill) and an ordered list of `SymbolLayer` instances. Stacking Symbol Layers within a Symbol is the QGIS-native compositional model and is **distinct** from the layer-scope Rule List.

### 6.1 Marker Symbol Layer

```cpp
struct MarkerSymbolLayer {
    enum Shape {
        Circle, Square, Triangle, Diamond, Star, Cross, Plus,
        XCross, Pentagon, Hexagon, Arrow, EquilateralTriangle, HalfCircle
    };
    Shape       shape;
    qreal       sizePx;             // outer extent
    QColor      fillColor;
    QPen        outlinePen;         // color + width + Qt::PenStyle
    qreal       rotationDeg;
    QPointF     offsetPx;
    // all properties data-defined-overridable
};
```

### 6.2 Line Symbol Layer

```cpp
struct LineSymbolLayer {
    QPen            pen;                    // color + width + Qt::PenStyle (Solid/Dash/Dot/DashDot/DashDotDot/Custom)
    Qt::PenCapStyle cap;                    // Flat / Square / Round
    Qt::PenJoinStyle join;                  // Miter / Bevel / Round
    QVector<qreal>  customDashPattern;      // when pen.style == Qt::CustomDashLine
    qreal           offsetPx;               // perpendicular offset
    bool            drawArrows;
    struct {
        QColor      color;
        qreal       lengthPx;
        qreal       widthPx;
        enum Placement { End, Both, Centered, RepeatEveryNPx, AtVertices } placement;
        qreal       spacingPx;              // for RepeatEveryNPx
        bool        reverse;                // flow direction
    } arrowSpec;
};
```

### 6.3 Fill Symbol Layer

```cpp
struct FillSymbolLayer {
    QBrush          brush;                  // color or pattern
    QPen            outlinePen;
    QPointF         offsetPx;
    enum Style { Solid, Hatch, Cross, BackDiag, FwdDiag, Dense1..Dense7 } style;
};
```

### 6.4 Raster + Mesh Symbol Layers

```cpp
struct RasterColorRampSymbolLayer {
    ColorRamp       ramp;
    IntervalBinner  binner;                 // or null = unclassed
    double          clampMin, clampMax;
    QColor          noDataColor;
    qreal           opacity;
};

struct HillshadeSymbolLayer {
    double          azimuthDeg;             // 0–360
    double          altitudeDeg;            // 0–90
    double          zExaggeration;          // 0.5–10
    double          shadowFloor;            // 0–1
    BlendMode       blend;                  // Multiply by default
};

struct ContourSymbolLayer {
    enum Mode { Lines, Filled, Both };
    Mode            mode;
    IntervalBinner  binner;
    ColorRamp       ramp;
    QColor          lineColor;
    qreal           lineWidthPx;
    LabelConfig     labelConfig;
    int             labelEveryN;
};

struct MeshEdgeSymbolLayer {
    QPen            pen;
    int             lodMinZoom;             // decimate below this zoom
};

struct MeshNodeSymbolLayer {
    MarkerSymbolLayer marker;
};
```

Hillshade / Contour / MeshEdge / MeshNode Symbol Layers consume the existing §N.5–§N.6 code paths unchanged — the Rule Model is the new packaging, not new pixels on the canvas.

---

## 7. Data-defined overrides

Every Q_PROPERTY on every `Symbol`, `SymbolLayer`, `Renderer`, `Decoration`, and `Rule` carries a small "ε" button that opens an expression editor. The expression evaluates per feature; the static value is the fallback when no expression is set.

```cpp
class DataDefinedMap {
public:
    void                bind(const QString& propertyPath, const QString& expression);
    void                unbind(const QString& propertyPath);
    bool                isBound(const QString& propertyPath) const;
    QString             expressionFor(const QString& propertyPath) const;
    QVariant            evaluate(const QString& propertyPath,
                                 const FeatureRef& feature,
                                 const QVariant& fallback) const;
};
```

`propertyPath` uses dot-and-index notation matching the JSON model: `symbol.layers[0].size`, `renderer.binner.classes`, `decorations.labels.haloSize`. The expression engine reuses §J's `DataDefined` stub — this plan promotes it from stub to first-class.

Standard expressions:
- `flooded_vol > 0` (filter)
- `sqrt(flow / max_flow) * 6` (size override)
- `if(flooded_vol > 1, red, blue)` (color override)
- `"node: " || node_id || " (" || elev || "m)"` (label expression)

The expression engine sits behind a `QgsExpression`-shaped API to keep `.qml` round-trip plausible for the standard subset.

---

## 8. Symbol Levels

QGIS-native cross-feature paint order. Off by default; when enabled, the renderer paints all features's Layer-0 Symbol Layers first, then all features's Layer-1 Symbol Layers, and so on. The order is configurable per Rule via a small dialog (drag rows). The default flat order is Rule-order then Symbol-Layer-order.

Use case: a conduit Rule with Symbol = `[outline 1.5 px white]` + `[fill 1 px blue]` paints all white outlines first across the whole network, then all blue fills — so intersections look connected rather than fragmented.

Slice Z.11.

---

## 9. Decorations

A decoration is a paint-pass attached to a Rule that consumes the Rule's filter + renderer output but emits independent geometry on top. Pluggable list:

| Decoration | Reuses | Paint pass | Slice |
|---|---|---|---|
| Labels | §L.BI.2 LabelEngine v2 | after symbols | Z.3 |
| Halo | §L.BI.2 LabelHalo | inside Labels | Z.3 |
| Arrows | LineSymbolLayer.arrowSpec | inside Line symbol | Z.5 |
| Contours | §N.6 BJ.2 contour generator | after fill | Z.6 |
| Hillshade | §N AU.6.4-lite | before fill (Multiply) | Z.6 |
| Edges (mesh / TIN wireframe) | §N.5 AZ.3.4 | after fill, before nodes | Z.6 |
| Nodes (TIN vertex markers) | MarkerSymbolLayer | after edges | Z.6 |

A Rule can have any combination. Mesh "show four things at once" = four Rules each with one decoration, or one Rule with four decorations — both expressible.

---

## 10. Layer Rendering tab additions (extends §X.3.1)

The §X Rendering tab gains:

- **Layer blend mode** — Normal / Multiply / Screen / Overlay / Darken / Lighten / ColorDodge / ColorBurn / HardLight / SoftLight / Difference / Exclusion (matches `QPainter::CompositionMode`)
- **Feature blend mode** — same list, applied between features within the layer
- **Draw effects** — Drop Shadow / Inner Shadow / Outer Glow / Inner Glow / Blur (per Symbol Layer; opens a sub-dialog)
- **Refresh rate** — auto-refresh interval seconds (0 = off) — useful for live result layers

Existing scale-dependent visibility stays. Per-Rule scale-visibility is in the Rule itself, not on the Rendering tab.

---

## 11. New Layer Properties tabs

Added per R2 decision:

### 11.1 Diagrams (Z.12)

Embedded charts on features. Per-feature pie / bar / time-series. For SWMM:
- Junctions: embedded depth-vs-time micro-chart that animates with the timeline
- Storage units: pie of inflow source fractions
- Subcatchments: bar of runoff vs infiltration

API:
```cpp
class DiagramSpec {
    enum Type { Pie, Bar, TimeSeries, Histogram };
    Type            type;
    QStringList     attributes;       // for static charts
    QString         seriesExpression; // for time-series — evaluates a vector
    QSizeF          sizePx;
    QPointF         offsetPx;
    QList<QColor>   palette;
    // standard data-defined overrides
};
```

### 11.2 Temporal (Z.13)

Time-aware layer config. For SWMM result layers, replaces the implicit animation controller:
- **Time field** — for vector: a datetime attribute; for results: implicit (output timestep)
- **Mode** — Single instant / Range / Cumulative
- **Frame rate** — fps for playback
- **Loop / Pong** — playback mode
- **Time range** — start / end (defaults to layer extent)

The status-bar animation widget reads from this tab instead of from a hardcoded controller.

### 11.3 Mask (Z.14)

Clip a layer to a polygon. For SWMM:
- Clip a DEM to the study-area polygon
- Clip a 2D mesh result to a subcatchment for focused inspection
- Inverted mask: hide everything inside the study area to show context

API: pick another layer (must be polygon vector) as the mask source; choose `Clip Inside` or `Clip Outside`.

### 11.4 Auxiliary Storage (Z.15)

Per-feature manual style overrides persisted in a sidecar SQLite DB. Use cases:
- "This one junction is highlighted red because I'm investigating it"
- "These three conduits are dashed because they're proposed not built"
- Overrides survive Rule edits — they're a layer above the Rule List

The DB stores `(feature_id, property_path, value_or_expression)` tuples; render path consults it after Data-defined overrides.

### 11.5 Joins (Z.16)

Join external CSV / DBF / SQLite to a layer's attribute table on a key field. SWMM use cases:
- Join observed depths CSV to a node layer; symbology can target the joined `observed_depth` field
- Join calibration metadata to subcatchments
- Cross-reference cost data on conduits

Joins are lazy (rebuilt on attribute access). The join definition lives in the layer's `.oswp` block.

---

## 12. Style I/O — `.qml` + `.swmm-rule.json` + Style Manager

### 12.1 File formats

- **`.swmm-rule.json`** — full-fidelity: every Rule + decoration + data-defined override + animation binding + symbol levels.
- **`.qml`** — QGIS interop subset: maps Rules with Single/Categorized/Graduated renderers + Marker/Line/Fill symbols to QGIS Rule-based renderer entries. Heatmap / Cluster / Displacement / Inverted Polygon / 2.5D / mesh decorations are omitted with a warning. Round-trip QGIS 3.34 → SWMMVis → QGIS preserves the vector subset.
- **`.lyrx`** — deferred (Z.17 stretch goal). ArcGIS Pro layer file.

### 12.2 Context-menu entries

The layer-tree right-click menu gains a **Styles ▸** submenu (matches QGIS):
- Edit Symbology…  *(opens Layer Properties → Symbology)*
- Copy Style
- Paste Style
- Save Style…    *(file dialog: `.qml` or `.swmm-rule.json`)*
- Load Style…    *(file dialog)*
- Save as Default  *(writes `<source>.qml` next to the layer source)*
- Restore Default

### 12.3 Style Manager dialog

A modal dialog listing user-saved Symbols, Color Ramps, Label Configurations, and complete Rules. Drag-and-drop into the Symbology tab. Persisted as `~/.config/SWMMVis/style-manager.db` (SQLite). Includes seed library with the SWMM defaults (11 model-layer category Rules, 4 mesh decorations, common ramps).

Slice Z.17.

---

## 13. Layer Styling dock (non-modal)

A dockable panel that hosts the same `SymbologyTab` widget the modal dialog uses. For animation workflows, the dock is the killer feature — drag a color-ramp stop while playback is running and the canvas re-renders live.

Architecture:
- `LayerStylingDock` embeds `SymbologyTab` (the same widget).
- Dock tracks the layer-tree's selection; switching layers swaps the embedded widget's bound `OpenSWMMVisLayer*`.
- No model duplication; cancel-rollback works because the dock binds to the live `Rule` instances directly (edits commit immediately; undo handled by `QUndoStack`).
- `View → Layer Styling Panel` toggles visibility.

Slice Z.18.

---

## 14. Animation semantics

Per Q4: **fixed bins** by default.

### 14.1 Binning lifecycle

When a Rule's renderer is Graduated and bound to a time-varying attribute (Z.7 detects this via `IFeatureRenderer::isAttributeDynamic()`):

1. At Rule creation, the GUI samples the attribute across all frames at a configurable sample rate (default: 20% of frames evenly spaced) and computes breaks once.
2. Breaks freeze in the Rule's persistence; subsequent playback uses these breaks.
3. The user can:
   - **Manually override breaks** via the histogram-aware Manual mode editor.
   - **Recompute breaks** on demand via a button (re-runs step 1).
   - **Opt-in "Recompute per frame"** checkbox — breaks recompute each frame from that frame's data only. Defaults off because of the shimmer.

### 14.2 Per-frame evaluation path

Every frame:
- `currentTimeChanged(t)` propagates to every Rule whose renderer or any data-defined override binds to a dynamic attribute.
- Each affected Rule re-evaluates per-feature: render-pass is invalidated, paint is requested.
- The legend dock subscribes to `Rule::breaksChanged()` (Z.7 amendment: legend dock listens to the active Rule's renderer); when breaks are fixed, no legend updates per frame — just the title bar timestamp.

### 14.3 Performance budget

Target: 24 fps on the snoopy lagoon fixture (15k cells × 200 frames × 3 stacked Rules). Measured at every Z.7 review; falls into a perf-regression slice if breached.

---

## 15. Migration from sublayer-keyed `.oswp`

`.oswp` files shipped under §W's persistence schema use per-Category sublayer IDs (`results.junctions`, `results.conduits`, `results.pumps`, …). On project load:

1. `OpenSWMMVisLayer::ruleListFromJson()` detects absence of a `rules` array and presence of a `sublayers` array → triggers migration.
2. Each sublayer entry → one Rule with `name = sublayer.name`, `isVisible = sublayer.isVisible`, renderer + symbol cloned from the sublayer's `Symbol` (via existing `FeatureSublayerStyle::toJson`).
3. `MeshNodeSublayer` / `MeshEdgeSublayer` / `MeshFillSublayer` / contour sublayers each become a single Rule with one decoration.
4. Migration emits a `MigrationLog` entry; user sees a one-shot dialog "Project migrated to Rule Model — review Symbology tab?" with a Show Diff button.

Migration is one-way. Saving the project rewrites in the Rule format. The shipped sublayer rendering primitives (`FeatureSublayer`, `MeshNodeSublayer`, etc.) stay in the code as the C++ paint primitives a Rule's Symbol delegates to — they're an implementation detail the user no longer sees.

---

## 16. Slice list

Each slice gated with a verifiable success criterion (per CLAUDE.md §4).

Legend: ✅ shipped · 🟡 partial (sub-slice progress in §20) · ⏳ pending · 🚫 deferred · 🚧 blocked

| # | Slice | Description | Success criterion | Status |
|---|---|---|---|---|
| **Z.1** | `Rule` + `RuleList` data classes | Q_PROPERTY-driven Rule, RuleList on OpenSWMMVisLayer, JSON round-trip | 12-fixture round-trip + legacy sublayer migration test passes pixel-diff < 1 LSB | ✅ |
| **Z.2** | Backwards-compat alias `styleSubjects() → RuleList` | Every editor from §W U-V2/V3/V4/V5 still renders | Existing `.oswp` files open with golden-image regression passing | ✅ |
| **Z.3** | `SymbologyTab` rewrite — Active Rule combo + Rule List + per-row visibility + drag-reorder | Combo, list, three editors (Single/Categorized/Graduated) all functional; cancel rollback works | All §W U-V2/V3/V4/V5 fixtures render unchanged with new wrapper | ✅ |
| **Z.4** | Marker Symbol Layer + outline QPen + attribute-driven size | All 13 shapes render; QPen edits propagate; size driven by `sqrt(flow)/max_sqrt(flow)` expression | Render fixture; <16 ms latency per edit on the snoopy lagoon | ✅ |
| **Z.5** | Line Symbol Layer + arrow spec + Unclassed renderer | All `Qt::PenStyle`/`PenCapStyle`/`PenJoinStyle` combos render; arrows place at 5 positions correctly | Arrow placement matches `QPainterPath::pointAtPercent` for 8/24/40 px lengths | ✅ |
| **Z.5b** | Perpendicular polyline offset algorithm + SWMM1D paint integration | `offsetPolyline()` with miter+bevel joins; SWMM1D link paint shifts perpendicular to forward dir; Graduated/Categorized per-feature offset | ✅ algo + SWMM1D QPainter (SingleSymbol fast path AND per-feature for Graduated/Categorized via `featureOffset` cache); QSG host + GISVector items still deferred | ✅ |
| **Z.6** | Raster + TIN Symbol Layers — color ramp / hillshade / contour / edges / nodes | Mesh `LayerPropertiesDialog → Mesh` settings auto-convert to a 4-Rule stack on load | Existing mesh `.oswp` files open with byte-identical pixels | ✅ |
| **Z.6a** | Z.6 paint integration (mesh QSG renderer reads from specs + Rule → legacy propagation) | QSG mesh renderer reads from spec snapshots each frame; dialog edits propagate via Rule signals to legacy fill/edge/contour/node sublayer styles | ✅ — 6 Rules (Mesh fill / Hillshade / Contour bands / Contour lines / Mesh edges / Mesh nodes) wire end-to-end | ✅ |
| **Z.7** | Animation binding — fixed bins from sample, opt-in per-frame rebin | 1D + 2D results animate via dynamic Rules; CPU <60% at 24 fps on snoopy lagoon | Perf benchmark passes; bins stable across frames | ✅ |
| **Z.7a** | AnimationController hookup for Rule binning | Controller observes Rule rebinPerFrame flag, schedules rebin per frame when set | ✅ | ✅ |
| **Z.8** | Migration + deprecation — strip "sublayer" / "kind tree" from user-facing strings | `grep -r "sublayer" include/ui/ src/ui/ docs/manual/` returns zero user-facing matches (code-level API stays) | Grep is empty; user manual updated | ✅ |
| **Z.9** | Heatmap / Point Cluster / Point Displacement / Inverted Polygon renderers | Each renderer ships with a unit test + demo on snoopy lagoon | 4 renderers usable from Symbology tab; per-renderer unit tests green | ⏳ |
| **Z.9-Unclassed** | Continuous-color (unclassed) graduated renderer | Sub-renderer of Z.9 that ships independently | ✅ | ✅ |
| **Z.10** | 2.5D / Extrusion renderer | **DEFERRED** — needs 3D scene | Tracked; ships after 3D scene lands | 🚫 deferred |
| **Z.11** | Symbol Levels — cross-feature paint order | Reordering 3 Symbol Layers across 1000 conduits paints in new order; off by default | Conduit network intersections visually correct with Symbol Levels on | ✅ data |
| **Z.11a** | Symbol Levels paint integration | Paint host walks features in level-major order when Rule::symbolLevelsEnabled | 🚧 — blocked on plumbing SymbolStyle through SWMM1D paint host (~500 LOC prereq) | 🚧 |
| **Z.12** | Diagrams tab — embedded pie / bar / time-series per feature | Junction with embedded depth-vs-time chart animates per frame | Demo on snoopy lagoon: 50 junctions with charts hold 24 fps | 🟡 data+UI |
| **Z.12-data** | DiagramSpec value type + JSON round-trip | toJson/fromJson + equality | ✅ | ✅ |
| **Z.12-attach** | DiagramSpec stored on OpenSWMMVisLayer | accessor + setter + signal + .oswp persistence | ✅ | ✅ |
| **Z.12-ui** | Diagrams tab widget mounted in LayerStyleDialog | enable + type combo (Pie/Bar/TimeSeries/Histogram) + attributes/series + geometry + range + palette editor | ✅ — `Z.12-palette` color-stop editor shipped (QListWidget + Add/Remove/Edit via QColorDialog) | ✅ |
| **Z.12-paint** | Diagrams paint at feature anchors | Pie/bar drawn per feature at centroid | 🚧 — blocked on per-feature attribute accessor (~400 LOC prereq) before painter (~300 LOC) | 🚧 |
| **Z.13** | Temporal tab — time field / mode / frame rate / loop / pong | Status-bar animation widget reads from Temporal tab instead of hardcoded controller | Play/pause/loop/pong all functional from the tab | ✅ |
| **Z.13-data** | TemporalSpec value type | toJson/fromJson + equality | ✅ | ✅ |
| **Z.13-attach** | TemporalSpec stored on OpenSWMMVisLayer | accessor + setter + signal + .oswp persistence | ✅ | ✅ |
| **Z.13-ui** | Temporal tab widget mounted in LayerStyleDialog | enable + mode + fps + loop/pingPong + start/end + range-window | ✅ | ✅ |
| **Z.13-controller** | AnimationController honors per-layer TemporalSpec | frameRateFps + loop + pingPong + startTime/endTime range + 2D fallback driver | ✅ — `effectiveStepRange()` clamps seek/step/tick; 2D fallback drives playback when no 1D primary; only `Z.13-controller-2d-range` (datetime-keyed range on the 2D path) remains | ✅ |
| **Z.14** | Mask tab — clip layer to polygon | Subcatchment polygon clips a DEM raster | Pixels outside the polygon are NoData on a 1024×1024 raster | ✅ |
| **Z.14-data** | MaskSpec value type | toJson/fromJson + equality | ✅ | ✅ |
| **Z.14-attach** | MaskSpec stored on OpenSWMMVisLayer | accessor + setter + signal + .oswp persistence | ✅ | ✅ |
| **Z.14-ui** | Mask tab widget mounted in LayerStyleDialog | enable + source layer id + mode (ClipInside/ClipOutside) | ✅ | ✅ |
| **Z.14-paint** | MaskClipResolver + QPainter clip injection + cache | GISVectorLayer polygon source → setClipPath in 3 paint hosts; resolved path cached per source-layer id, auto-evicts via QPointer | ✅ — `Z.14-paint-cache` shipped; QSG mesh + GISVectorLayer items + non-vector source types still named follow-ups | ✅ |
| **Z.15** | Auxiliary Storage — per-feature manual overrides | Override color on one junction to red; persists across sessions | SQLite DB has the override; reload restores | 🟡 spec+UI |
| **Z.15-data** | AuxiliaryStorageSpec value type | toJson/fromJson + equality | ✅ | ✅ |
| **Z.15-attach** | AuxiliaryStorageSpec stored on OpenSWMMVisLayer | accessor + setter + signal + .oswp persistence | ✅ | ✅ |
| **Z.15-ui** | Auxiliary Storage tab widget mounted | enable + DB path + browse | ✅ | ✅ |
| **Z.15-db** | SQLite schema + read/write helpers | Tables for (feature_id, property_path, value); CRUD API | ⏳ | ⏳ |
| **Z.15-editor** | Row-level override editor dialog | Add/edit/remove per-feature overrides launched from the tab | ⏳ | ⏳ |
| **Z.15-paint** | Apply overrides at paint time (after DDOs, before paint) | Per-feature lookup hits DB; override values win over Rule output | ⏳ | ⏳ |
| **Z.16** | Joins tab — join external CSV/DBF | Observed-depth CSV joined to node layer; Graduated renderer can target the joined field | Join round-trips through `.oswp`; symbology drives off joined field | 🟡 spec+UI |
| **Z.16-data** | JoinSpec value type | toJson/fromJson + equality | ✅ | ✅ |
| **Z.16-attach** | JoinSpec list stored on OpenSWMMVisLayer | accessor + setter + signal + .oswp persistence | ✅ | ✅ |
| **Z.16-ui** | Joins tab widget mounted | list + add/remove + editor pane | ✅ | ✅ |
| **Z.16-engine** | Runtime join engine | CSV/DBF/SQLite readers + lazy column resolution + expression-evaluator hookup | ⏳ | ⏳ |
| **Z.17** | StyleIO + Styles ▸ submenu + Style Manager dialog | `.qml` round-trips to QGIS 3.34 for the vector subset; `.swmm-rule.json` full-fidelity; Style Manager imports & exports | QGIS opens our `.qml`; we open QGIS's `.qml`; Style Manager seed library appears | ✅ |
| **Z.17-files** | Native `.swmm-rule.json` save/load helpers | RuleListIoResult + envelope schema | ✅ | ✅ |
| **Z.17b** | QGIS `.qml` import/export for Rule Lists | singleSymbol + RuleRenderer mapping; SimpleMarker/SimpleLine/SimpleFill layer round-trip | ✅ (Graduated/Categorized fall back to single + warning; covers the common interop case) | ✅ |
| **Z.17c** | Style Manager dialog | Library list + preview + apply/save/import/export/delete; lives under `QStandardPaths::AppLocalDataLocation/styles` | ✅ — launched from Tools → Style Manager… | ✅ |
| **Z.18** | Layer Styling dock | Same `SymbologyTab` widget embedded in dock; drag color-ramp stop during playback re-renders live | 24 fps held while dragging stops on a 2D results layer | ✅ — launched from View → Layer Styling Dock |
| **Phase B** | Layer-side adoption of the Rule Model | All OpenSWMMVisLayer subclasses return non-null ruleList(); dialog mounts Rule-aware Symbology when present; renderer panels Rule-aware | ✅ (B.1–B.7 all shipped, including 2D mesh + 2D results ruleList() overrides) | ✅ |

---

## 17. Cross-slice ownership

| Concern | Owning slice | Consumers |
|---|---|---|
| `Rule` + `RuleList` data model | Z.1 | every other Z slice |
| `SymbologyTab` widget | Z.3 | modal `LayerStyleDialog` + non-modal `LayerStylingDock` |
| Marker / Line / Fill Symbol Layers | Z.4, Z.5 | every Rule with Marker/Line/Fill archetype |
| Raster / Hillshade / Contour / Edge / Node Symbol Layers | Z.6 | Raster + TIN + 2D results Rules |
| Data-defined overrides | Z.3 (`ε` button) | every property on every Symbol Layer + Renderer + Decoration |
| Symbol Levels | Z.11 | every Rule (cross-feature pass) |
| Animation binding | Z.7 | every dynamic Rule on 1D/2D results layers |
| Diagrams | Z.12 | every vector layer with `DiagramSpec` |
| Temporal | Z.13 | every result layer; status-bar widget |
| Mask | Z.14 | every layer with a `MaskSpec` |
| Auxiliary Storage | Z.15 | render path post-DDO, pre-paint |
| Joins | Z.16 | every layer with a `JoinSpec`; renderer attribute combos pick up joined fields |
| StyleIO + Style Manager | Z.17 | layer-tree Styles ▸ menu + `Style Manager` dialog |
| Layer Styling dock | Z.18 | reuses `SymbologyTab` widget |

---

## 18. Migration of existing planning docs

| Doc | Status after this plan |
|---|---|
| `RENDERING_OUTPUT_SUBLAYERS_PLAN.md` | Banner added; rendering-primitive slices (S1–S2.3c) survive intact; UI sections (§§4.1–4.4 + S3–S6 UI portions) strike-through with archaeology preserved |
| `RENDERING_STATIC_STYLING_PLAN.md` | Replaced by a 20-line redirect to this plan; `KindSublayer` proposal withdrawn (never shipped) |
| `GUI_IMPLEMENTATION_PLAN.md` §J | Keep; callout added pointing to §Z (this file) as the user-facing surface |
| `GUI_IMPLEMENTATION_PLAN.md §L` | BI per-kind dialog amendments trimmed (now covered by Z.3); BA / BB / BI.2 label amendments survive |
| `GUI_IMPLEMENTATION_PLAN.md §N` | §N.5–§N.9 (mesh edges / contours / velocity / HGL / labels) survive — they're decoration capabilities consumed by Rules. §N.2 / §N.2-bis / §N.4 rewritten to describe Rule + Rule List |
| `GUI_IMPLEMENTATION_PLAN.md §P` | WITHDRAWN/SUPERSEDED archaeology blocks trimmed to one-line footers; BI-MK.LT + BI-MK.4-OUT survive |
| `GUI_IMPLEMENTATION_PLAN.md §W` | Keep; "evolves via §Z" footer added |
| `GUI_IMPLEMENTATION_PLAN.md §X` | §X.3.2 (Symbology tab content) rewritten to show Active Rule combo + Rule List |
| `GUI_IMPLEMENTATION_PLAN.md §Z` (new stub) | One-page pointer to this file |

---

## 19. Open follow-ups (out of scope for first batch)

- **Z.10 — 2.5D / Extrusion** waits for 3D scene
- **Elevation tab** waits for 3D scene
- **3D View tab** waits for 3D scene
- **`.lyrx` ArcGIS Pro interop** — Z.17 stretch goal
- **Cross-layer Rule libraries** — share a Rule across multiple layers via reference rather than copy
- **Animated Symbol Layers** — pulsing markers, flowing arrows, animated dashes — possible but no user demand yet
- **Live histogram in Manual classifier** — Z.3 ships static histogram; live histogram tracking current viewport is a follow-up

---

## 20. Progress snapshot (handoff for next agent)

This section is the authoritative status read at the time of handoff. Keep it
fresh — agents picking up should update this before stopping. The slice table
in §16 reflects high-level status; this section captures the texture: what's
working end-to-end, what's data-only, what's blocked and why.

### 20.1 Working end-to-end

Open the app today and a user can:

1. **Open Layer Properties on any layer with a Rule List** (SWMMModelLayer,
   SWMMResultsLayer, GISVectorLayer, SWMM2DMeshLayer, SWMM2DResultsLayer). The
   Symbology tab presents the Active Rule combo + Rule List from §4. Cancel
   rollback works via the §W snapshot path.
2. **Edit any Rule's renderer** (Single / Graduated / Categorized / RuleBased
   / Unclassed) via the body's renderer-class combo + IRendererPanel. Edits
   propagate live to the canvas via `Rule::rendererReplaced` →
   `notifyRendererStateChanged()` → layer's repaint cascade.
3. **Edit 2D mesh decoration** (fill, hillshade, contour bands, contour lines,
   mesh edges, mesh nodes) via the 6 seed Rules on SWMM2DMeshLayer /
   SWMM2DResultsLayer. The QSG renderer reads from spec snapshots each frame;
   the Rule → legacy-style propagation handlers (Slice Z.6a-step3) write spec
   edits back to the legacy sublayer style fields the renderer samples.
4. **Configure five new tabs on each layer**:
   - **Temporal** (Z.13) — drives AnimationController frame rate / loop /
     ping-pong when enabled
   - **Mask** (Z.14) — clips paint to (or outside) another layer's polygons in
     the three QPainter hosts
   - **Auxiliary Storage** (Z.15) — toggle + DB path; runtime application + row
     editor not yet wired
   - **Joins** (Z.16) — manage a list of external-table joins; runtime engine
     not yet wired
   - **Diagrams** (Z.12) — pie / bar / time-series / histogram spec with size /
     offset / range; paint pass not yet wired
5. **Browse + share styles** via Tools → Style Manager…
   (Z.17c). Library at `QStandardPaths::AppLocalDataLocation/styles`. Both
   `.swmm-rule.json` (native, high-fidelity) and `.qml` (QGIS interop, lossy
   subset) supported. Export performs real format conversion when source/dest
   suffixes differ.
6. **Edit symbology without leaving the canvas** via View → Layer Styling Dock
   (Z.18). Always-open variant of the Symbology tab, follows the layer-tree
   selection.

### 20.2 Data-only / spec-shipped, integration deferred

These slices ship the value type + tab UI + persistence but no paint /
runtime effect. Editing the spec is harmless but invisible until the named
follow-up lands:

- **Z.12-paint** — Diagrams spec round-trips through .oswp but nothing
  paints the chart. Needs per-feature attribute accessor (~400 LOC prereq).
- **Z.15-paint / Z.15-db / Z.15-editor** — Auxiliary Storage spec saved
  but DB schema + CRUD + paint-time override application not wired.
- **Z.16-engine** — Joins list saved but no runtime join engine yet
  (CSV / DBF / SQLite readers + expression-evaluator hookup).
- **Z.11a paint** — Symbol Levels reorder helper shipped but the SWMM1D
  paint host doesn't walk `renderer()->symbolFor() → SymbolStyle.layers`
  yet so there's nothing to reorder.

### 20.3 Z.13-controller status

The per-layer temporal spec drives AnimationController for the active
driver with five behaviours: `frameRateFps`, `loop`, `pingPong`,
`startTime`/`endTime` range clamping, and 2D-fallback driving.

Status:

- ✅ **frameRateFps / loop / pingPong** — original Z.13-controller slice.
- ✅ **Range clamping** (`Z.13-controller-range`) — `seekToPeriod` /
  `stepForward` / `stepBackward` / `seekToFirst` / `seekToLast` /
  `onTimerTick` all clamp to the effective range derived from
  `spec.startTime` / `spec.endTime` via `periodIndexForDateTime`. The
  new private helper `effectiveStepRange(outMin, outMax)` is the single
  source of truth.
- ✅ **2D fallback driver** (`Z.13-controller-2d-fallback`) —
  `updateTimerInterval` and `onTimerTick` now read the 2D fallback's
  spec when no 1D primary is set. `connectFallback2D` observes
  `temporalSpecChanged` so 2D-layer edits take effect live.

Remaining follow-up:

- **Z.13-controller-2d-range** — `effectiveStepRange` returns the full
  `[0, total-1]` for the 2D fallback because `SWMM2DResultsLayer` has
  no public `periodIndexForDateTime` analogue. Add one (or expose
  `simTimeAt(i)` index search inside the controller) to enable
  datetime-keyed range clamping on the 2D path too.
- **Per-non-primary-layer scheduling** — current design uses one global
  timer driven by the active layer's spec; secondary 1D layers follow
  via the QDateTime fan-out. A multi-timer model would let each layer
  animate at its own rate independently.

### 20.4 Z.14-paint status

Mask clip is wired in three QPainter hosts with a cache for the
resolved clip path:

- ✅ **QPainter hosts** — SWMM1D (`SWMMLayerItem`), 2D mesh
  (`SWMM2DMeshGraphicsItem`), 2D results (`SWMM2DResultsGraphicsItem`).
- ✅ **Cache** (`Z.14-paint-cache`) — `MaskClipResolver` caches the
  resolved `QPainterPath` keyed by source-layer id. Entries auto-evict
  via `QPointer` when the source layer is destroyed. External callers
  can invalidate via `invalidateMaskClipCache(layerId)` after in-place
  geometry edits. Empty paths are cached too so the resolver doesn't
  re-walk a polygon-less OGR layer.

Remaining follow-ups:

- **Z.14-paint-qsg** — QSGClipNode requires triangulating the
  QPainterPath into a stencil mesh. Defer until needed.
- **Z.14-paint-vector** — GISVectorLayer's per-feature `QGraphicsItem`s
  need to become children of a parent container item that applies the
  clip in its own paint. Refactor of how `populateScene` constructs
  items.
- **Z.14-paint-source-types** — currently only `GISVectorLayer` polygon
  sources are supported. SWMMModelLayer subcatchments and raster
  outline polygons fall through to "no clip" + silent no-op. Each new
  source type is a small extension to `resolveMaskClip`.

### 20.5 Z.5b-paint status

Perpendicular polyline offset is wired end-to-end in SWMM1D's QPainter
host (`SWMMLayerItem`):

- ✅ **SingleSymbol fast path** — `lineOffsetForKindRenderer` reads the
  active kind's first line-SymbolLayer offset once per paint setup.
- ✅ **Graduated / Categorized per-feature offset**
  (`Z.5b-paint-graduated`) — `SWMMModelLayer` now caches per-feature
  offsets in `m_kindFeatureOffsets[NumCategories]`, populated by
  `rebuildKindFeatureColors` from each renderer's `symbolFor` output.
  The paint loop reads `featureOffset(category, idx)` per visible link
  when `kindHasAnyOffset(category)` is true; otherwise the single-symbol
  fast path runs.

Remaining follow-ups:

- **Z.5b-paint-qsg** — QSG vertex-buffer rebuild on offset change. The
  QSG renderer caches link geometry as flat arrays; rebuilding with
  offsets is doable but invasive (the cached vertex buffer would need
  to be regenerated per offset change).
- **Z.5b-paint-vector** — GISVectorLayer uses `QGraphicsLineItem` per
  feature. Offset requires converting to `QGraphicsPathItem` with an
  offset path during `populateScene`.

### 20.6 Phase B status

Phase B (layer adoption) is complete: B.1–B.7 all shipped. Every layer
class that styles geometry now returns a non-null `ruleList()`. The
LayerStyleDialog detects this and mounts the Rule-aware Symbology tab
when present. Renderer panels (Single / Graduated / Categorized /
RuleBased) are Rule-aware via `RendererPanelContext::rule`.

### 20.7 Architectural blockers for the remaining slices

Three remaining slices have prerequisites that themselves need to be
done before the slice can land. An agent picking up should expect to do
the prereq first OR explicitly skip the dependent slice:

| Slice | Prereq | Why |
|---|---|---|
| **Z.11a paint** | Plumb `SymbolStyle` through SWMM1D paint host (~500 LOC) | The paint loop currently goes direct to `QSGGeometryNode + QSGFlatColorMaterial` and hardcoded `drawNodeGlyph` branches. There's no `SymbolStyle.layers` sequence to reorder. |
| **Z.12-paint** | Per-feature attribute accessor on every layer type (~400 LOC) | `DiagramSpec.attributes` names fields like `"inflow"`, `"outflow"`. Different layer types source these differently (SWMM engine vs OGR fields vs 2D cell data). No unified accessor today. |
| **Z.15-paint** | Z.15-db (SQLite schema + CRUD helpers) | Override application reads from the DB; without the DB layer there's nothing to apply. |

### 20.8 Files touched in this rollout (representative)

For an agent navigating the change set:

- **Data model**: `include/render/{rule,rulelist,*spec,polylineoffset,maskclipresolver,qmlrulelistio,symbollevels,binsampler}.h` + matching `.cpp` files in `src/render/`
- **Layer plumbing**: `include/layers/openswmmvislayer.h` (TemporalSpec / MaskSpec / AuxiliaryStorageSpec / JoinSpec / DiagramSpec members + accessors + signals; `workspace()` accessor) and matching .cpp setters
- **Paint hosts**: `src/map/swmmlayeritem.cpp` (mask clip + line offset), `src/layers/swmm2dmeshlayer.cpp` (mask clip), `src/layers/swmm2dresultslayer.cpp` (mask clip), `src/map/swmm2dmeshqsgrenderer.cpp` (Z.6a spec-driven paint)
- **Dialog**: `include/ui/dialogs/{labelstab,temporaltab,masktab,auxiliarystoragetab,joinstab,diagramstab,rulesymbologytab,layerstyledialog,stylemanagerdialog}.h` + matching .cpp files
- **Dock**: `include/ui/panels/layerstylingdock.h` + .cpp
- **Project I/O**: `src/project/projectserializer.cpp` (5 new spec round-trips keyed under "temporal", "mask", "auxStorage", "joins", "diagram")
- **AnimationController**: `src/animation/animationcontroller.cpp` (per-primary-layer TemporalSpec)
- **Main window**: `forms/swmmvis.ui` (3 new actions: actionStyleManager, actionLayerStylingDock — the temporal/mask/etc tabs come from the layer dialog, not the main window menubar)
- **CMake**: `CMakeLists.txt` (new files registered), `tests/gui/CMakeLists.txt` (Z.*-data tests)

Tests live alongside in `tests/gui/test_*.cpp`. The Z.*-data slices each
have a focused JSON-round-trip + equality test. Tab widget tests are
intentionally not present — same pattern as the legacy `LabelsTab`
which also has no widget test (heavyweight layer construction outweighs
incremental signal).

### 20.9 How to pick up

1. **Read this section first**, then §16 for the slice list.
2. **Pick a thread** from one of the buckets below.
3. **For non-blocked threads**: the pattern established by Z.13 / Z.14 / Z.15
   / Z.16 / Z.12 is consistent — survey existing code first, build value
   type / helper, attach to layer, expose in UI, wire to consumer. ~200–
   400 LOC per slice when scope is held.
4. **For blocked threads**: do the prereq as its own slice first. Don't try
   to ram both in one session — both pieces suffer.
5. **Update §20** before stopping. Note what landed, what was deferred and
   why, and what the next agent should do.

#### 20.9.1 Big-ticket unfinished slices

These are substantial new code (~600–1,500 LOC each) and deserve their
own focused sessions:

- **Z.9b** — `IAggregateRenderer` interface + 4 concrete renderers
  (Heatmap / Point Cluster / Point Displacement / Inverted Polygon).
  Architectural decision: how do aggregate renderers integrate with
  the per-feature paint host? Start with Heatmap (the simplest) to
  validate the interface.
- **Z.16-engine** — Runtime join engine: CSV reader / DBF reader /
  SQLite reader, lazy column resolution, expression-evaluator hookup.
  Best done as 3–4 sub-sessions (`-csv`, `-dbf`, `-sqlite`, `-expr`).
- **Z.15-db** + **Z.15-editor** + **Z.15-paint** — SQLite schema, CRUD
  helpers, row-editor dialog, paint-time application of overrides
  after data-defined overrides resolve. The schema design (Z.15-db) is
  a one-way decision worth deliberating before writing CRUD.

#### 20.9.2 Blocked slices needing architectural prereqs

See §20.7 for full table.

- **Z.11a paint** — needs SymbolStyle plumbed through SWMM1D paint host.
- **Z.12-paint** — needs per-feature attribute accessor on every layer
  type.
- **Z.15-paint** — needs Z.15-db first.

#### 20.9.3 Remaining named follow-ups (small, well-bounded)

These are 100–300 LOC each and can be batched into a single session if
needed:

- **Z.5b-paint-qsg** — QSG vertex-buffer rebuild on offset change.
  Invasive in the QSG renderer's geometry cache.
- **Z.5b-paint-vector** — `QGraphicsLineItem` → `QGraphicsPathItem`
  refactor in GISVectorLayer.
- **Z.14-paint-qsg** — `QSGClipNode` path triangulation for the QSG
  mesh renderer.
- **Z.14-paint-vector** — Parent-container restructure in
  GISVectorLayer to apply clip across per-feature items.
- **Z.14-paint-source-types** — extend `resolveMaskClip` to accept
  SWMMModelLayer subcatchments and raster outline polygons as
  sources.
- **Z.13-controller-2d-range** — datetime-keyed range clamp on the 2D
  fallback path; needs a `periodIndexForDateTime` analogue on
  `SWMM2DResultsLayer`.
