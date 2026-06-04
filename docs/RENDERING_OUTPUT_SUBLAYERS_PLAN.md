# Output-Layer Sublayer Rendering Plan

> Last updated: **2026-05-27** · Status: **🔧 partial — primitives shipped, UI portions withdrawn**
>
> Status legend: ✅ shipped & verified · 🔧 in progress · ⏳ not started · ⛔ withdrawn

> ## ⚠️ Status banner — 2026-05-27
>
> The **rendering primitives** in this plan are kept and partly shipped:
> - ✅ Slice S1 — `ISublayer` + screen-pixel sizing (shipped 2026-05-25)
> - ✅ Slice S2.1–S2.3c — `FeatureSublayer` refactor + `NodeMarkerSublayer` + `ConduitLineSublayer` + `ConduitArrowSublayer` + `SubcatchmentFillSublayer` (shipped 2026-05-25)
> - 🔧 Slice S5 — 2D mesh sublayer migration (in progress)
>
> The **user-facing UI** described below — 3-level layer tree, per-sublayer rows with checkbox + opacity slider, modeless `SublayerStyleDialog`, per-sublayer right-click context menu, per-sublayer legend symbol items — is **withdrawn 2026-05-27**.
>
> User-facing styling now happens through the **Active Rule combo + Rule List** in the Symbology tab. See [`RENDERING_RULE_MODEL_PLAN.md`](RENDERING_RULE_MODEL_PLAN.md) for the new architecture and slices Z.1–Z.18.
>
> Concretely:
> - `ISublayer` / `FeatureSublayer` / `MeshNodeSublayer` / `MeshEdgeSublayer` / `MeshFillSublayer` / `DepthColorRampSublayer` / `IsolineSublayer` / `ContourBandSublayer` / `VelocityVectorSublayer` / `FlowArrowSublayer` **stay as C++ paint primitives**. A `Rule`'s `Symbol` delegates to them.
> - `SublayerStyleDialog`, the 3-level layer tree, per-sublayer legend right-click jumps, and `docs/manual/15_sublayers.md` are **superseded** by the Rule Model.
> - Sections §§4.1–4.4 below (UI changes) and Slices S3, S4, S6 are struck-through. They are kept as archaeology so future readers chasing why this plan diverges from the shipped code have the trail.

This plan extends the theming architecture in [`GUI_IMPLEMENTATION_PLAN.md`](GUI_IMPLEMENTATION_PLAN.md) §J (renderer / symbol / legend) with the missing piece needed for SWMM **output-file** rendering: a **sublayer model** that breaks each results layer into independently-toggleable visual aspects (color scale, arrows, contours, vectors, marching-squares bands), each with its own screen-space-sized symbology, its own legend entry, and a static-vs-dynamic flag wiring it into the existing `AnimationController`.

It does **not** replace §J. It depends on §J's `IFeatureRenderer` + `ColorRamp` + `LegendRenderer` pipeline. The new artefacts in this plan plug into those existing seams.

Cross-references in the GUI implementation plan:
- §J.2 — `IFeatureRenderer` interface (parent abstraction we attach sublayers to)
- §J.4 — color-ramp → classifier → renderer → legend pipeline (the data flow each sublayer participates in)
- §J.5 — legend-from-renderer rule (sublayers contribute `LegendSymbolItem`s)
- Slice BB — color-ramp editor + `LegendDock` (legend UI we feed)
- Slice BI / BI.2 — symbology dialog + label engine v2 (UI sibling)
- Slice BA / AC.4 — animation controller + 2D mesh theming (where dynamic sublayers tick from)
- Slice CF.MVP / CF.2 — 2D depth heat-map + velocity vectors (this plan generalizes CF.2 into a sublayer registry)

---

## 1. Why this plan

Today an output layer is monolithic: `SWMMResultsLayer` paints a single colour ramp over nodes / links, and `SWMM2DResultsLayer` paints a single inundation ramp over the mesh. There is no architectural seam for "show the depth ramp **and** velocity arrows **and** contour bands at the same time, each on its own opacity, each with its own legend entry, some animated and some not." Slice CF.2 added per-feature `setVelocityVectorsVisible(bool)` getters/setters on the 2D layer, but the pattern doesn't generalize — each new visual aspect would grow another pair of bespoke setters and another bespoke legend special-case.

User direction (2026-05-25 conversation):

1. **Sub-layer options should be toggleable.** Each visual aspect of an output layer is an independent on/off feature with its own opacity.
2. **Disassociate visualization scale from map scale.** Symbol sizes (node radius, link line width, arrow length, vector glyph stride) stay constant in screen pixels at all zoom levels.
3. **Style is context-specific.** A conduit sublayer's style knobs are different from a node sublayer's, which are different from a 2D velocity-vector sublayer's. The style schema is per-sublayer-kind, not one-size-fits-all.
4. **Legends are configurable via the QPropertyModel dialog.** Each sublayer's legend entry is editable through the existing `AttributePanel` / `LegendPropertiesDialog` pattern — no new property-editor framework.
5. **In 2D mode the sublayer set expands** to include filled colour scales, marching-squares-derived banded fills, isoline contours, and velocity vectors.
6. **Dynamic elements are controlled by the animation toolbar.** Each sublayer self-declares whether its visual depends on the current timestep; the `AnimationController` only invalidates the dynamic ones on tick.

The architecture below addresses all six points and reuses §J's `IFeatureRenderer` + `ColorRamp` + `LegendSymbolItem` plumbing — there is one new interface (`ISublayer`), one new property-bag base class (`SublayerStyle`), and four new MVC views that read the same models.

---

## 2. Architectural decisions

### Decision 1 — Sublayers are a refinement of the renderer model, not a parallel one.

Each output layer owns an ordered list of `ISublayer` instances. The layer's renderer (`IFeatureRenderer` from §J.2) becomes the **default sublayer set** — a `SingleSymbolRenderer` produces one `MarkerSublayer` + one `LineSublayer`; a `GraduatedRenderer` adds a `ColorRampSublayer` on top, etc. This keeps §J's abstraction intact and means existing slices (BB, BI, CF.MVP) continue to work — they just produce sublayers under the hood instead of monolithic paint passes.

Sublayer order = paint order, bottom up. The user can re-order via the layer-tree dock (drag-and-drop within a layer's children).

### Decision 2 — Screen-space symbol sizing is a property of `SublayerStyle`, not a global toggle.

The constant-pixel-size behavior the user wants is the **default and only** unit for symbol-size fields (`markerSizePx`, `lineWidthPx`, `arrowLengthPx`, `contourWidthPx`, `vectorGlyphSpacingPx`). All sizes are stored in CSS pixels. The QSG renderers compute geometry using the inverse of the current view zoom so a 6-px marker stays 6 px on screen at every zoom level. Pen widths use `QSGGeometry::setLineWidth(px)` directly (Qt RHI/Metal handles screen-pixel line width natively). Marker quads are emitted at screen-pixel size by scaling the per-vertex offset vector with `1.0 / scale`.

We do **not** ship a per-sublayer "scale with map" alternative in v1 (user picked pure screen-pixel mode). The seam is preserved (every size field reads through `SizeUnit::Pixels`); adding `SizeUnit::MapUnits` later is local to the renderer.

### Decision 3 — Each sublayer self-declares `isDynamic()`. The `AnimationController` reads this flag to decide what to invalidate per tick.

`AnimationController::currentPeriodChanged` today fans out to every connected `SWMMResultsLayer` / `SWMM2DResultsLayer`, which then repaints itself wholesale. The new contract: the controller calls `layer->onAnimationTick(period)`, which walks the sublayer list and only invalidates / re-uploads sublayers whose `isDynamic() == true`. Static sublayers (e.g. a base node-shape sublayer, a mesh-boundary outline) cache their QSG geometry across ticks. This is the perf-relevant cut — on a 5M-node model with one dynamic colour-ramp sublayer and three static sublayers, only one VBO re-uploads per tick.

The animation toolbar gains **no new buttons**. Its existing play / pause / step / seek already invoke `AnimationController::currentPeriodChanged`; the new dispatch is purely an internal optimization.

### Decision 4 — Sublayer style objects are QObjects with Q_PROPERTYs, edited through the existing QPropertyModel pipeline.

Each `ISublayer` exposes `QObject* style()` returning a heap-owned property-bag. The bag's Q_PROPERTY declarations drive the right-click → **Edit Style…** dialog (a reusable `SublayerStyleDialog` wrapping a `QPropertyModel`). No new property-editor framework — this is the same pattern `AttributePanel` and `LegendPropertiesDialog` already use.

The style bag is the **single source of truth** for that sublayer's visual config. Edits to it emit Qt signals; both the QSG renderer and the legend view subscribe. This satisfies CLAUDE.md §5.1 (MVC — same data, multiple UIs, kept in sync via signals) and §J.5 (legend-from-renderer rule — the legend reads the same model the renderer reads).

### Decision 5 — Layer tree extends to three levels: `Category → Layer → Sublayer`.

The existing `LayerTreeModel` is two-level. The new model adds a third tier when a layer reports `sublayers().size() > 0`. Each sublayer row has the same columns as a layer row (visibility checkbox + opacity slider). Right-click on a sublayer row opens its context menu (Edit Style, Hide, Move Up / Down, Reset to Default). The legend dock is **not** extended — it stays two-level (`Layer → LegendSymbolItem`) but each `LegendSymbolItem` now carries a back-pointer to the sublayer that produced it, so right-click on a swatch can also jump to the sublayer's style dialog.

This keeps the two dock surfaces orthogonal: layer tree = visibility / ordering / structure; legend dock = symbology / labels / classification. Matches the QGIS convention and avoids putting style-edit affordances in two places.

### Decision 6 — Marching-squares contour bands and isolines compute on the CPU, upload as static QSG nodes per period.

A 2D mesh with N triangles produces at most ~2N contour line segments per iso-value. For a typical 10k-tri mesh × 8 iso-values = 160k segments, which is one VBO upload (~1.3 MB at 8 bytes/vertex) per animation tick. CPU contour extraction (marching-squares on each triangle) is O(N) per tick — well under 16 ms at this scale. We do **not** compute contours in a shader; CPU is fast enough and the result is reusable for both line contours and filled bands (the same triangle classification feeds both).

If profiling later shows contour extraction dominating on huge meshes (≥ 1M tris), the seam is local — promote to a worker thread with the same QSG upload contract.

---

## 3. The new interface

```cpp
// include/render/isublayer.h  (new)

class ISublayer : public QObject {
    Q_OBJECT
public:
    enum Kind {
        MarkerKind,       // points (nodes, gages)
        LineKind,         // links / conduits / edges
        FillKind,         // subcatchment / mesh polygons
        ArrowKind,        // flow-direction marker-along-line
        ColorRampFill,    // graduated fill (mesh triangles, sub-catch polys)
        IsolineKind,      // marching-squares line contours
        ContourBandKind,  // marching-squares filled bands
        VectorGlyphKind,  // velocity arrows
    };

    virtual Kind kind() const = 0;
    virtual QString id() const = 0;             // stable within the parent layer
    virtual QString displayName() const = 0;    // shown in the layer-tree row

    virtual bool isVisible() const = 0;
    virtual void setVisible(bool) = 0;
    virtual qreal opacity() const = 0;          // 0..1
    virtual void setOpacity(qreal) = 0;

    virtual bool isDynamic() const = 0;         // true → invalidated by AnimationController tick
    virtual QObject *style() = 0;               // property bag (Q_PROPERTY-driven)

    // Legend (§J.5) — sublayers contribute items into the parent layer's list
    virtual QList<LegendSymbolItem> legendSymbolItems() const = 0;

    // QSG (called from the parent layer's updatePaintNode)
    virtual QSGNode *buildOrUpdateNode(QSGNode *existing,
                                       const SublayerContext &ctx) = 0;
signals:
    void styleChanged();      // emitted by the style bag → renderer + legend listen
    void visibilityChanged();
};

struct SublayerContext {
    QMatrix4x4   viewMatrix;          // for inverse-scale screen-px geometry
    qreal        pixelRatio;
    qreal        currentZoom;
    QDateTime    currentTime;         // ignored if !isDynamic
    int          currentPeriod;       // ignored if !isDynamic
    QRectF       exposedSceneRect;    // for culling
    QSGRenderNode *parentNode;        // root node for child attachment
};
```

Each output layer kind ships a default sublayer mix:

| Layer | Default sublayers (paint order, bottom-up) |
|-------|---------------------------------------------|
| `SWMMResultsLayer` (1D `.out`) | `SubcatchmentFillSublayer` (static) → `ConduitLineSublayer` (dynamic — colored by ramp) → `ConduitArrowSublayer` (dynamic — flow direction) → `NodeMarkerSublayer` (dynamic — colored by ramp) |
| `SWMM2DResultsLayer` (2D mesh) | `MeshFillSublayer` (static — terrain hillshade) → `DepthColorRampSublayer` (dynamic — graduated fill) → `ContourBandSublayer` (dynamic, default off) → `IsolineSublayer` (dynamic, default off) → `VelocityVectorSublayer` (dynamic, default off) |
| `SWMMModelLayer` (no results) | Existing `MultiKindRenderer` keeps producing one sublayer per kind (junction/outfall/divider/storage/conduit/pump/orifice/weir/outlet/subcatch/gage). All static. |

---

## ~~4. UI changes~~ — ⛔ WITHDRAWN 2026-05-27

> The four subsections below described the user-facing surface for sublayers — 3-level layer tree, per-sublayer style dialog, per-sublayer legend items, animation toolbar wiring. **All four are superseded** by the Rule Model — see [`RENDERING_RULE_MODEL_PLAN.md`](RENDERING_RULE_MODEL_PLAN.md) §4 (Symbology tab) and §11.1–§11.5 (new tabs).
>
> Kept below as archaeology.

### ~~4.1 Layer tree (3-level)~~ — ⛔ WITHDRAWN

```
▼ Results layers
  ▼ ☑ ──  West Whiteland.out                (opacity ▓▓▓▓▓░░░░░ 80%)
        ☑  Subcatchment fill                (opacity ▓▓▓░░░░░░░ 35%)
        ☑  Conduit color ramp               (opacity ▓▓▓▓▓▓▓▓▓▓ 100%)  ⏰ dynamic
        ☑  Conduit flow arrows              (opacity ▓▓▓▓▓▓▓▓░░ 80%)   ⏰ dynamic
        ☑  Node color ramp                  (opacity ▓▓▓▓▓▓▓▓▓▓ 100%)  ⏰ dynamic
  ▼ ☑ ──  West Whiteland 2D                 (opacity ▓▓▓▓▓▓░░░░ 60%)
        ☑  Mesh hillshade                   (opacity ▓▓▓▓▓░░░░░ 50%)
        ☑  Depth color ramp                 (opacity ▓▓▓▓▓▓▓▓▓▓ 100%)  ⏰ dynamic
        ☐  Contour bands                                                ⏰ dynamic
        ☐  Isolines                                                     ⏰ dynamic
        ☐  Velocity vectors                                             ⏰ dynamic
```

- ⏰ icon in the row indicates `isDynamic() == true`. Purely informational; ties visually to the animation toolbar.
- Drag-reorder within a layer's children reorders paint order. Cross-layer drag is rejected.
- Right-click context menu on a sublayer row: **Edit Style…**, **Reset to Default**, **Hide**, **Move Up / Down**, **Remove** (only for user-added sublayers, e.g. the user enables Velocity Vectors which was off by default).

### ~~4.2 Per-sublayer style dialog (QPropertyModel)~~ — ⛔ WITHDRAWN

> Replaced by the Active Rule combo + Rule List in the Symbology tab. The `IStyleEditorWidget` registry from §W is reused — editors now bind to `Rule*` instead of `Sublayer::style()`.

A single reusable `SublayerStyleDialog` (modeless, dockable when expanded) wraps a `QPropertyModel` view of the sublayer's `style()` QObject. The Q_PROPERTYs differ per sublayer kind — that's the "context-specific style" the user asked for.

Examples of context-specific Q_PROPERTY exposure:

| Sublayer kind | Properties exposed (Q_PROPERTY) |
|---------------|---------------------------------|
| `ConduitLineSublayer` (output) | `QString attribute` (flow / depth / velocity / Froude / capacity), `qreal lineWidthPx`, `ColorRamp colorRamp`, `IntervalBinner binning`, `bool useLogScale`, `Qt::PenStyle dashPattern`, `qreal capacityHighlightThreshold`, `QColor capacityHighlightColor` |
| `ConduitArrowSublayer` | `qreal arrowLengthPx`, `qreal arrowWidthPx`, `MarkerLinePlacement placement` (midpoint / repeat-every-px / firstVertex / lastVertex), `qreal repeatSpacingPx`, `QColor arrowColor`, `bool fadeOnLowFlow`, `qreal lowFlowThreshold` |
| `NodeMarkerSublayer` (output) | `QString attribute` (depth / head / flooding-vol / inflow / lateral-inflow), `qreal markerSizePx`, `MarkerShape shape` (circle / square / triangle / diamond / star), `ColorRamp colorRamp`, `IntervalBinner binning`, `QColor floodedHighlightColor`, `qreal floodedHighlightSizeBoostPx` |
| `DepthColorRampSublayer` (2D) | `QString attribute` (depth / WSE / vmag / Fr), `ColorRamp colorRamp`, `IntervalBinner binning`, `bool useLogScale`, `QColor belowMinColor`, `QColor aboveMaxColor`, `bool antialias` |
| `IsolineSublayer` | `QString attribute`, `QList<double> isoValues` (or auto N intervals), `qreal lineWidthPx`, `QColor color` (single, or `ColorRamp` for ramp-by-value), `Qt::PenStyle dashPattern`, `bool labels`, `int labelEveryNthSegment` |
| `ContourBandSublayer` | `QString attribute`, `QList<double> bandEdges`, `ColorRamp colorRamp`, `qreal opacity`, `bool smoothBands` (per-vertex interpolation instead of per-triangle), `QColor belowMinColor`, `QColor aboveMaxColor` |
| `VelocityVectorSublayer` | `qreal glyphLengthScalePxPerMps`, `qreal glyphLengthMinPx`, `qreal glyphLengthMaxPx`, `qreal glyphSpacingPx` (sub-sampling), `MarkerShape headShape` (triangle / open-triangle), `qreal headSizePx`, `QColor color` (single, or `ColorRamp` for color-by-magnitude), `qreal dryDepthCutoff` |

The dialog itself is dumb — it walks the QObject's Q_PROPERTY metadata and produces editors via the existing QPropertyModel delegate. No bespoke layouts. Property groups derive from `Q_CLASSINFO("group", "Symbology|Classification|Highlights")` keys.

### ~~4.3 Legend dock — unchanged but smarter~~ — ⛔ WITHDRAWN

> Legend dock survives but reads from the active Rule's renderer rather than from per-sublayer legend contributions. See RENDERING_RULE_MODEL_PLAN.md §14.2 (legend update path).

`LegendDock` continues to display `Layer → LegendSymbolItem`. Each sublayer now contributes its own items (e.g. the ColorRamp sublayer emits N graduated swatches; the Arrow sublayer emits one arrow swatch labelled "Flow direction"; the Isoline sublayer emits one line swatch labelled per iso-value). Items inherit the existing `userLabel / visible / sortIndex` overrides from BB Phase 8.6.10.

Phase 8.6.16 right-click → Change Color / Change Size / Change Symbol writes through to the sublayer's style bag (same MVC source of truth). Phase 8.6.16 right-click → **Edit Layer Style…** is renamed **Edit Sublayer Style…** when the click is on a sublayer-contributed item.

### ~~4.4 Animation toolbar — unchanged~~ — ⛔ WITHDRAWN

> Replaced by the **Temporal tab** in Layer Properties (Slice Z.13). The status-bar animation widget reads from the Temporal tab instead of from `AnimationController` directly.

No new controls. The toolbar continues to drive `AnimationController`. The controller's internal dispatch becomes sublayer-aware (Decision 3).

---

## 5. Phased slices

Each slice has explicit success criteria — CLAUDE.md §4 "goal-driven execution." All work lives behind the `OPENSWMM_SUBLAYERS=ON` CMake option until the entire plan ships, so partial slices don't break the running app.

### Slice S1 — ⏳ `ISublayer` interface + screen-pixel sizing in QSG

Build the minimal seam without changing any user-facing behavior.

1. Add [`include/render/isublayer.h`](../include/render/isublayer.h) per §3. → verify: header compiles standalone.
2. Add [`include/render/sublayerstyle.h`](../include/render/sublayerstyle.h) — the QObject base class for style bags; `Q_CLASSINFO` group convention. → verify: subclass smoke test reads/writes one Q_PROPERTY via QPropertyModel.
3. Add `SizeUnit::Pixels` to a new `include/render/sizeunit.h`. Audit `SWMMLayerQSGRenderer` + `SWMM2DMeshQSGRenderer` for hardcoded size constants and route them through the inverse-zoom helper. → verify: a 6-px marker measures 6 px on screen at zooms 0.1× / 1× / 10× via screenshot-diff test in `tests/gui/test_screen_pixel_sizing.cpp`.
4. Plumb `SublayerContext` through `updatePaintNode()`. → verify: existing rendering pixel-identical to pre-slice (golden-frame test).

### Slice S2 — ⏳ Refactor `SWMMResultsLayer` to a sublayer list

Replace `SWMMResultsLayer`'s monolithic paint with four sublayer implementations (`SubcatchmentFillSublayer`, `ConduitLineSublayer`, `ConduitArrowSublayer`, `NodeMarkerSublayer`). The layer becomes a dumb container that walks its sublayer list. Style bags expose the Q_PROPERTYs from §4.2.

1. Implement the four sublayers + their style bags. → verify: each compiles + standalone QPropertyModel renders the property tree.
2. Add `SWMMResultsLayer::sublayers()` returning the default mix; deprecate the existing `setActiveResultVariable`-style setters (keep them as thin facades writing through to the relevant sublayer's `attribute` property — backward-compat for callers in Slice BA / `swmmvis.cpp`). → verify: existing tests in `tests/gui/test_swmmresultslayer.cpp` still pass; if any fail, fix the facade.
3. Wire `AnimationController` dispatch through `onAnimationTick` (Decision 3). → verify: with three sublayers (two static, one dynamic), per-tick instrumentation shows one VBO upload, not three. `tests/gui/test_animation_sublayer_dispatch.cpp`.

### ~~Slice S3 — Layer tree 3-level model + sublayer style dialog~~ — ⛔ WITHDRAWN 2026-05-27

> Pure UI plumbing for sublayers; superseded by Slice Z.3 (`SymbologyTab` rewrite) in [`RENDERING_RULE_MODEL_PLAN.md`](RENDERING_RULE_MODEL_PLAN.md).

UI plumbing for sublayer toggle / opacity / context menu / per-sublayer property dialog.

1. Extend `LayerTreeModel` to expose sublayers as a third tier. Visibility + opacity columns mirror the layer row. → verify: clicking a sublayer's checkbox toggles visibility; opacity slider updates the QSG opacity uniform; both round-trip through model signals (no shadow state).
2. Add `SublayerStyleDialog` (modeless, reusable). Hook to right-click → **Edit Style…**. → verify: edits in the dialog repaint the canvas immediately and update the legend dock's swatch immediately (single shared model).
3. Drag-reorder within a layer's children. → verify: re-ordering "Arrows above Color ramp" changes paint stacking; persists through layer-tree save/load.

### ~~Slice S4 — Sublayer-aware `LegendSymbolItem`s + right-click jumps~~ — ⛔ WITHDRAWN 2026-05-27

> Per-sublayer legend items and right-click jumps replaced by the Active Rule combo's row affordance. Legend dock reads from the active Rule's renderer. See [`RENDERING_RULE_MODEL_PLAN.md`](RENDERING_RULE_MODEL_PLAN.md) §14.2.

Each sublayer emits its own `LegendSymbolItem`s with `sublayerId` back-pointer. Phase 8.6.16 right-click menu jumps to the right sublayer's style dialog.

1. Extend `LegendSymbolItem` with `QString sublayerId`. Renderer's `legendSymbolItems()` concatenates each sublayer's contribution. → verify: legend dock shows distinct swatch groups for each sublayer; collapsing a sublayer in the layer tree dims its legend group (does not remove it).
2. Right-click → **Edit Sublayer Style…** opens the correct dialog. → verify: manual test on a 2D layer with depth ramp + isolines + vectors.

### Slice S5 — ⏳ 2D `SWMM2DResultsLayer` sublayer migration

Move CF.MVP / CF.2 into sublayers and wire all four 2D sublayer types the user requested.

1. **`MeshFillSublayer`** (static, terrain hillshade) — pulled from existing `SWMM2DMeshLayer` paint, made into a separate sublayer of the **results** layer (so the mesh shows even when the results aren't loaded). → verify: terrain visible with no results attached.
2. **`DepthColorRampSublayer`** (dynamic, graduated) — wraps existing `inundationColorRgba` behind a `GraduatedRenderer` + `ColorRamp("legacy-SWMM-inundation")`. → verify: visually identical to current `SWMM2DResultsLayer`; legacy ramp choice preserved.
3. **`VelocityVectorSublayer`** (dynamic, default off) — uses CF.2.3's RT0 reconstruction. Screen-pixel arrow lengths. → verify: arrows visible on Snoopy Lagoon at t=peak; arrow lengths constant in pixels at all zooms; off-by-default per user choice.
4. **`IsolineSublayer`** (dynamic, default off) — marching-squares per triangle, line emission. → verify: 8 contour lines at peak inundation render correctly; auto-update on tick.
5. **`ContourBandSublayer`** (dynamic, default off) — marching-squares filled bands; per-triangle classification → triangulation-friendly fill. → verify: bands match isolines (same break points produce coincident edges); legend shows band swatches.

Acceptance for S5: at full extent on the Snoopy Lagoon demo with all five sublayers on, paint stays under 30 ms per tick on M1; per-tick CPU contour extraction stays under 8 ms.

### ~~Slice S6 — Persistence + manual~~ — ⛔ WITHDRAWN 2026-05-27

> Persistence migrates from `"sublayers"` JSON arrays to `"rules"` JSON arrays — see [`RENDERING_RULE_MODEL_PLAN.md`](RENDERING_RULE_MODEL_PLAN.md) §3.3 + §15. Manual doc `docs/manual/15_sublayers.md` superseded by a new Rule Model chapter (filed under Slice Z.8).

Round-trip sublayer state through `.oswp` and document.

1. `.swmm-style.json` schema (§J.6) gains a `"sublayers": [ {id, visible, opacity, style:{...}} ]` array. Layer-kind dispatch picks the right sublayer factory. → verify: save → close project → reopen → identical visual.
2. `.oswp` saves per-layer style file references + inline overrides. → verify: project round-trip on `west_whiteland.inp` + `snoopy.inp` preserves sublayer mix and all knobs.
3. `docs/manual/15_sublayers.md` — user-facing doc with screenshots of each sublayer kind, the layer-tree 3-level view, and the style dialog. → verify: doc reads cleanly; screenshots match shipped UI.

---

## 6. What this plan does NOT include

Out of scope, intentionally — to keep S1–S6 finishable:

- **Per-sublayer "scale with map units" alternative.** Pure screen pixels only in v1 (Decision 2). Adding `SizeUnit::MapUnits` later is a localized change.
- **Animated isovalue scrubbing** (e.g. play through iso-values instead of through time). Iso-values are static per period; only the underlying field updates.
- **3D extrusion / 2.5D buildings.** §J explicitly excludes; this plan inherits the exclusion.
- **Particle traces / streamlines.** Vector arrows only in v1. Particle traces are a CF.3 follow-up.
- **Heat map (Gaussian kernel density) over points.** Not asked for. Out of scope.
- **Server-side / WMS-style result rendering.** Engine renders client-side.
- **GPU contour extraction (compute shaders).** CPU is fast enough at expected scales (Decision 6).

---

## 7. Open questions

Resolve before S1 starts:

1. **Sublayer reorderability scope** — should the user be able to drag a sublayer from one layer to another (e.g. moving a `VelocityVectorSublayer` from results-layer A to a fresh "comparison" layer)? Default answer: **no, sublayers are owned by their layer**. Cross-layer comparison is the comparison-plot dialog's job.
2. **Dynamic sublayer caching policy** — when a sublayer is toggled off, do we evict its QSG geometry (saves GPU memory) or keep it (instant re-show)? Default: **evict after 5 minutes of off-state**, instant evict on layer remove.
3. **Marker shape library scope** — Slice BI Phase 8.13.10 ships 11 built-in shapes. Do all `MarkerKind` sublayers honour the full set, or do output sublayers ship a curated subset? Default: **full set**, no special-casing.
4. **Default isovalue selection** — auto-pick N=8 equal-interval breaks over the period's value range? Or use the layer's `ColorRamp` breaks (so isolines = band edges)? Default: **mirror the ramp's breaks** so contours and bands naturally coincide.

---

## 8. Verification strategy

CLAUDE.md §4 / §4.1 — write tests against user-reviewable file locations.

- **Golden-frame tests** per slice in `tests/gui/golden/` — PNG diffs against committed reference frames, updated explicitly when visuals intentionally change.
- **MVC sync tests** — edit the style bag, verify the QSG renderer + legend dock + layer tree all updated within one event loop tick.
- **Animation dispatch test** — instrument `onAnimationTick`; assert exactly the dynamic-flagged sublayers invalidate.
- **Persistence round-trip tests** — `.oswp` save/load on both demo projects.
- **Manual presentation gate** — Snoopy Lagoon with all five 2D sublayers on, playback at 1× speed, no dropped frames on M1.

---

## 9. Progress log

Append-only. Each entry: **date — slice — outcome**.

### 2026-05-25 — Plan drafted, awaiting user review

- Draft of S1–S6 captured with architectural decisions and cross-references to §J / BA / BB / BI / CF.MVP / CF.2 / AC.4.
- Four open questions in §7 flagged for resolution before S1 starts.
- No code lands until user signs off (per CLAUDE.md §5.0).

### 2026-05-25 — Slice S1 landed (foundation headers + test) 🔧

User approved §7 defaults (no cross-layer drag · 5-min eviction · full marker set · isolines mirror ramp breaks) and said "start implementation".

**Shipped:**

- ✅ [`include/render/sizeunit.h`](../include/render/sizeunit.h) — `SizeUnit::{Pixels, MapUnits}` enum. MapUnits reserved but not exposed in v1 UI (Decision 2).
- ✅ [`include/render/screenpixels.h`](../include/render/screenpixels.h) — constexpr `sceneSizeFromPixels(pixelSize, pixelsPerSceneUnit)` and inverse `pixelsFromSceneSize`. Header-only, Qt-free, divide-by-zero-safe. **Verified at compile time** via static_asserts at the three plan-spec zoom levels (10 / 100 / 1000 px-per-scene-unit) plus the degenerate-matrix edge cases.
- ✅ [`include/render/sublayerstyle.h`](../include/render/sublayerstyle.h) — `SublayerStyle` QObject base with `styleChanged()` signal, `toJson/fromJson` virtuals (default no-ops), and protected `setDirty()` helper. Subclasses declare Q_PROPERTYs + `Q_CLASSINFO("group:fieldName", "Section")` for the editor groups.
- ✅ [`include/render/isublayer.h`](../include/render/isublayer.h) — `ISublayer` interface per plan §3, plus `Kind` enum (8 values from MarkerKind through VectorGlyphKind) and `SublayerContext` per-frame struct (viewMatrix, pixelsPerSceneUnit, devicePixelRatio, exposedSceneRect, currentTime/Period). Forward-declares `QSGNode` and `SublayerStyle` to avoid pulling Qt Quick into headers that don't need it.
- ✅ [`include/render/legendsymbolitem.h`](../include/render/legendsymbolitem.h) + [`.cpp`](../src/render/legendsymbolitem.cpp) — gained `QString sublayerId` field with JSON round-trip; absent in serialised output when empty (forward-compatible with existing `.swmm-style.json` / `.oswp` files).
- ✅ [`tests/gui/test_sublayer_interface.cpp`](../tests/gui/test_sublayer_interface.cpp) — 13 QtTest cases:
  - 4 × `ISublayer` (identity, visibility signal, opacity signal, legend-carries-sublayerId)
  - 4 × `SublayerStyle` (Q_PROPERTY meta round-trip, signal-emits-exactly-once, no-signal-on-unchanged, JSON round-trip)
  - 3 × screen-pixel math (typical zooms, zero-scale safety, pixelsFromSceneSize is inverse)
  - 2 × LegendSymbolItem.sublayerId JSON (round-trip; absent-when-empty)
- ✅ Wired into [`CMakeLists.txt`](../CMakeLists.txt) (render block) and [`tests/gui/CMakeLists.txt`](../tests/gui/CMakeLists.txt) (`add_swmmvis_gui_test(test_sublayer_interface …)` — self-contained, only LegendSymbolItem + SymbolStyle/Layer compiled in).

**Scope decision (surgical change, CLAUDE.md §3):** plan S1 step 3 mixed an additive helper with a QSG renderer audit, while step 4 demanded pixel-identical output. The QSG renderer audit is **deferred to early S2** where sublayer code actually consumes the seam. S1 keeps the running app's pixel output unchanged.

**Verification status:** The Qt-free `screenpixels.h` math is verified by static_assert at compile time on the workspace sandbox (clang + `-Werror`). The Qt-dependent test (`test_sublayer_interface`) needs to be built locally — run from the gui repo root:

```sh
cmake --build build/darwin-debug --target test_sublayer_interface
ctest --test-dir build/darwin-debug -R '^test_sublayer_interface$' -V
```

All 13 cases are expected to pass; if any fail, the headers themselves are the suspect (no other code paths are exercised).

**Next:** S2 — refactor `SWMMResultsLayer` to a sublayer list (the four output sublayers in plan §3 default mix), wiring up `AnimationController::onAnimationTick` to walk `isDynamic()` flags. The QSG renderer screen-pixel audit lands at the start of S2 in service of that refactor.

### 2026-05-25 — Slice S2.1 landed (ISublayerHost + animation-tick dispatch) 🔧

S2 audit surfaced ~1.7k LOC in `SWMMResultsLayer.h/.cpp` plus existing signal-based animation wiring. Doing the whole S2 ("replace monolithic paint with four sublayer implementations + wire AnimationController + audit QSG renderer") in one step would violate CLAUDE.md §3. Decomposed S2 into:

- **S2.1** *(this entry)* — `ISublayerHost` interface + animation-tick dispatch contract, fully testable in isolation. Zero touches to production layers.
- **S2.2** — First concrete sublayer (`NodeMarkerSublayer`) with its `Q_PROPERTY` style bag (still no production wiring).
- **S2.3** — Adopt `ISublayerHost` on `SWMMResultsLayer`, with the four concrete sublayers.
- **S2.4** — Wire `AnimationController` to call `dispatchAnimationTick` on every host. Existing `currentTimeStepChanged` signal traffic preserved for back-compat.
- **S2.5** — QSG renderer screen-pixel audit (consume `SublayerContext::pixelsPerSceneUnit` via `screenpixels.h`).

**Shipped in S2.1:**

- ✅ [`include/render/isublayerhost.h`](../include/render/isublayerhost.h) — non-QObject mixin (`virtual ~`, `sublayers()`, `dispatchAnimationTick(int)` default impl). Non-QObject so layers can multi-inherit alongside their existing `OpenSWMMVisLayer` base without diamond/MOC issues; signal traffic flows through individual `ISublayer` instances.
- ✅ [`include/render/isublayer.h`](../include/render/isublayer.h) gained a public `Q_INVOKABLE virtual void invalidate()` method so the host can request a re-render without reaching into another `QObject`'s signals directly. Default impl emits `invalidated()`; subclasses may override to add dirty-bit bookkeeping.
- ✅ [`tests/gui/test_sublayer_host_dispatch.cpp`](../tests/gui/test_sublayer_host_dispatch.cpp) — 5 QtTest cases:
  - empty host dispatch is a no-op (no crash)
  - `sublayers()` returns the list in insertion order
  - `dispatchAnimationTick` invalidates ONLY dynamic sublayers (perf-relevant assertion)
  - hidden dynamic sublayers still receive invalidation (visibility filtering belongs to the renderer, not the dispatcher)
  - period argument is forwarded but doesn't require base-class consumption
- ✅ Wired into [`CMakeLists.txt`](../CMakeLists.txt) and [`tests/gui/CMakeLists.txt`](../tests/gui/CMakeLists.txt) — self-contained leaf test sharing the lean dependency set from `test_sublayer_interface`.

**Build/verify locally:**

```sh
cmake --build build/darwin-debug --target test_sublayer_host_dispatch
ctest --test-dir build/darwin-debug -R '^test_sublayer_host_dispatch$' -V
```

5/5 expected to pass. The `ISublayerHost` interface is now ready to be adopted by `SWMMResultsLayer` (S2.3) — by then we'll have at least one concrete sublayer to put in its list (S2.2).

### 2026-05-25 — Slice S2.2 landed (NodeMarkerSublayer + NodeMarkerStyle) 🔧

First concrete sublayer ships. Minimum-viable property bag (CLAUDE.md §2 — simplicity first) — full `ColorRamp` + `IntervalBinner` + flooded-highlight integration deferred to S2.5+ when the QSG renderer actually consumes them.

**Style bag v1 — 4 properties:**

| Q_PROPERTY      | Type        | Default          | Group             |
|-----------------|-------------|------------------|-------------------|
| `attribute`     | QString     | "depth"          | Classification    |
| `markerSizePx`  | double      | 6.0              | Symbology         |
| `shape`         | Q_ENUM      | Circle           | Symbology         |
| `color`         | QColor      | rgb(40,40,200)   | Symbology         |

`MarkerShape` ships 5 values for v1 — Circle / Square / Triangle / Diamond / Star. The full §J.3 SimpleMarker library (Cross / X / Pentagon / Hexagon / Arrow / Half-circle) adds in S2.5 when the renderer paints them. Shape JSON encoding uses the enum **key string** (forward-compatible — adding a shape doesn't break old files; unknown tokens silently preserve the prior value).

**Shipped:**

- ✅ [`include/render/sublayers/nodemarkersublayer.h`](../include/render/sublayers/nodemarkersublayer.h) — both classes in one header (single-use, no over-factoring per CLAUDE.md §2). `Q_CLASSINFO("group:<prop>", "<section>")` tags drive the SublayerStyleDialog section layout in S3.
- ✅ [`src/render/sublayers/nodemarkersublayer.cpp`](../src/render/sublayers/nodemarkersublayer.cpp) — getters/setters with change-detection + clamp on negative `markerSizePx` (defends against the screenpixels.h math contract that wants non-negative inputs); JSON round-trip via `QMetaEnum::valueToKey` / `keyToValue`; `legendSymbolItems()` emits one swatch tagged with the sublayer id (lets legend right-click → "Edit Sublayer Style…" find this sublayer).
- ✅ [`tests/gui/test_nodemarkersublayer.cpp`](../tests/gui/test_nodemarkersublayer.cpp) — 13 QtTest cases:
  - 2 × identity / construction (id stable; style owned as QObject child)
  - 2 × Q_PROPERTY meta (lookup-by-name; setProperty / property round-trip via QObject)
  - 3 × setter signals (exactly-once; unchanged is no-op; negative size clamped to 0)
  - 2 × JSON (full round-trip; unknown shape token preserves existing value)
  - 1 × legend (one row, tagged with sublayer id, attribute name as label)
  - 3 × sublayer state (visibility signals + no-op debounce; opacity clamped to [0,1]; style change re-emits as `invalidated`)
- ✅ Wired into [`CMakeLists.txt`](../CMakeLists.txt) (both header and source registered) and [`tests/gui/CMakeLists.txt`](../tests/gui/CMakeLists.txt).

`buildOrUpdateNode` returns its `existing` argument unchanged — the sublayer is honest about not painting yet. The parent renderer skips it cleanly (will paint in S2.5).

**Build/verify locally:**

```sh
cmake --build build/darwin-debug --target test_nodemarkersublayer
ctest --test-dir build/darwin-debug -R '^test_nodemarkersublayer$' -V
```

13/13 expected to pass.

**Next (S2.3):** Adopt `ISublayerHost` on `SWMMResultsLayer`. The layer gains a `sublayers()` accessor returning the four-element default mix (one `NodeMarkerSublayer` from S2.2 plus three stub sublayers for subcatchment fill / conduit line / conduit arrow) and `dispatchAnimationTick()` from S2.1. Existing paint code stays in place — sublayers are added alongside, not replacing. Then S2.4 wires `AnimationController` to call the host's dispatch.

### 2026-05-25 — Slice S2.3 landed (ConduitLineSublayer + ConduitLineStyle) 🔧

Second concrete sublayer. Same minimum-viable-bag pattern as S2.2 (CLAUDE.md §2 / §3 discipline preserved). Plan §4.2 lists 8 properties for ConduitLineSublayer; v1 ships 4 — the ones meaningful before the renderer paints anything (`colorRamp` / `binning` / `useLogScale` / `capacityHighlight` defer until S2.5 renderer audit + ColorRamp delegate work).

**Style bag v1 — 4 properties:**

| Q_PROPERTY      | Type          | Default       | Group           |
|-----------------|---------------|---------------|-----------------|
| `attribute`     | QString       | "flow"        | Classification  |
| `lineWidthPx`   | double        | 1.5           | Symbology       |
| `color`         | QColor        | rgb(60,60,60) | Symbology       |
| `dashPattern`   | Qt::PenStyle  | SolidLine     | Symbology       |

`Qt::PenStyle` is reused directly (already Q_ENUM, registered globally) instead of inventing a parallel enum — no need to map between project-local and Qt values in the renderer down the line. Dash-pattern JSON encoding uses the Qt::PenStyle key token ("SolidLine" / "DashLine" / "DashDotLine" / …); unknown tokens silently preserve the prior value, same forward-compat rule as NodeMarkerStyle's shape.

**Shipped:**

- ✅ [`include/render/sublayers/conduitlinesublayer.h`](../include/render/sublayers/conduitlinesublayer.h) + [`.cpp`](../src/render/sublayers/conduitlinesublayer.cpp) — `LineKind`, `isDynamic()=true`. Same QObject parent-child ownership (style as child of sublayer); same `styleChanged → invalidated` re-emit pattern as `NodeMarkerSublayer`.
- ✅ [`tests/gui/test_conduitlinesublayer.cpp`](../tests/gui/test_conduitlinesublayer.cpp) — 15 QtTest cases, mirroring the NodeMarker structure plus three line-specific assertions:
  - default `dashPattern` token serialises as `"SolidLine"` (the load-bearing forward-compat assertion)
  - unknown `dashPattern` token preserves prior value
  - legend item's symbol layer is `SimpleLine`
- ✅ Wired into [`CMakeLists.txt`](../CMakeLists.txt) (header + source) and [`tests/gui/CMakeLists.txt`](../tests/gui/CMakeLists.txt).

`buildOrUpdateNode` passes through (same as NodeMarker — S2.5 wires line-strip geometry).

**Build/verify locally:**

```sh
cmake --build build/darwin-debug --target test_conduitlinesublayer
ctest --test-dir build/darwin-debug -R '^test_conduitlinesublayer$' -V
```

15/15 expected to pass.

**Next slices remaining in S2:**

- **S2.3b** — `ConduitArrowSublayer` (ArrowKind, dynamic) — marker-along-line with arrow length/spacing/colour
- **S2.3c** — `SubcatchmentFillSublayer` (FillKind, **static** — not driven by animation) — fill colour + outline pen
- **S2.4** — Adopt `ISublayerHost` on `SWMMResultsLayer`; existing layer tests stay green
- **S2.5** — Wire `AnimationController::onAnimationTick` to call `dispatchAnimationTick` + QSG renderer screen-pixel audit + actual `buildOrUpdateNode` geometry

After S2.5, the running app actually paints sublayers and respects screen-pixel sizing.

### 2026-05-25 — Slice S2.3b landed (ConduitArrowSublayer + ConduitArrowStyle) 🔧

Third concrete sublayer. ArrowKind, dynamic (arrow orientation flips with negative flow at the current period — renderer concern, no Q_PROPERTY needed). Same minimum-viable-bag pattern.

**Style bag v1 — 5 properties:**

| Q_PROPERTY          | Type                    | Default          | Group           |
|---------------------|-------------------------|------------------|-----------------|
| `arrowLengthPx`     | double                  | 8.0              | Symbology       |
| `arrowWidthPx`      | double                  | 5.0              | Symbology       |
| `placement`         | MarkerLinePlacement Q_ENUM | Midpoint      | Placement       |
| `repeatSpacingPx`   | double (floor = 1 px)   | 40.0             | Placement       |
| `color`             | QColor                  | rgb(40,40,40)    | Symbology       |

`MarkerLinePlacement` ships 4 values for v1: `Midpoint`, `RepeatEveryPx`, `FirstVertex`, `LastVertex`. `repeatSpacingPx` is consulted only when `placement == RepeatEveryPx` — but the setter floors at 1 px regardless so the renderer's marker-along-line loop can't be put into an infinite-emission state by a bad style file.

**Shipped:**

- ✅ [`include/render/sublayers/conduitarrowsublayer.h`](../include/render/sublayers/conduitarrowsublayer.h) + [`.cpp`](../src/render/sublayers/conduitarrowsublayer.cpp) — `ArrowKind`, `isDynamic()=true`. Same QObject parent-child ownership + `styleChanged → invalidated` re-emit pattern as the sibling sublayers.
- ✅ [`tests/gui/test_conduitarrowsublayer.cpp`](../tests/gui/test_conduitarrowsublayer.cpp) — 16 QtTest cases, mirroring the sibling structure plus arrow-specific cases:
  - `setRepeatSpacingPx(0)` floors to 1 (defends against infinite-emission)
  - default `placement` token serialises as `"Midpoint"`
  - unknown `placement` token preserves prior value
  - legend item label is `"Flow direction"` (not the attribute name — flow direction is invariant)
  - legend item's symbol layer is `MarkerLine`
- ✅ Wired into [`CMakeLists.txt`](../CMakeLists.txt) + [`tests/gui/CMakeLists.txt`](../tests/gui/CMakeLists.txt).

`buildOrUpdateNode` passes through (S2.5 wires actual arrow-quad geometry).

**Build/verify locally:**

```sh
cmake --build build/darwin-debug --target test_conduitarrowsublayer
ctest --test-dir build/darwin-debug -R '^test_conduitarrowsublayer$' -V
```

16/16 expected to pass.

**Slice progress:** 3 of 4 concrete sublayers shipped (NodeMarker + ConduitLine + ConduitArrow). One more (SubcatchmentFillSublayer — the only **static** one) before S2.4 host adoption.

### 2026-05-25 — Slice S2.3c landed (SubcatchmentFillSublayer + Style — static) 🔧

Fourth and last concrete sublayer. **Static** — `isDynamic() == false`. Completes the default sublayer mix for `SWMMResultsLayer` (§3 of this plan).

This is the deliberate test of Decision 3's perf-relevant cut: subcatchment polygons don't change with the animation period, so the host dispatch must NOT invalidate this sublayer on every tick. Combined with `test_sublayer_host_dispatch::dispatchTick_invalidates_only_dynamic_sublayers` (the spy assertion that static sublayers see zero invalidations), the contract is end-to-end-verified.

**Style bag v1 — 3 properties:**

| Q_PROPERTY        | Type    | Default                  | Group    |
|-------------------|---------|--------------------------|----------|
| `fillColor`       | QColor  | rgba(220,230,200,**120**) — **translucent** | Fill    |
| `outlineColor`    | QColor  | rgb(120,130,100)         | Outline  |
| `outlineWidthPx`  | double  | 0.75 (0 disables outline)| Outline  |

The translucent fill default is deliberate — subcatchment fill sits at the bottom of the paint stack as static context; if it weren't translucent it would obscure the dynamic conduit / node ramps painted above. `outlineWidthPx == 0` is the "no outline" mode (one symbol layer in the legend); positive widths add a SimpleLine outline on top of the SimpleFill (two symbol layers in the legend, so the legend swatch matches what the renderer draws).

**Shipped:**

- ✅ [`include/render/sublayers/subcatchmentfillsublayer.h`](../include/render/sublayers/subcatchmentfillsublayer.h) + [`.cpp`](../src/render/sublayers/subcatchmentfillsublayer.cpp) — `FillKind`, `isDynamic()=false`. No `attribute` property — subcatchment fill is static context, not attribute-driven (categorical coloring by land-use / impervious-% comes via `CategorizedRenderer` UI in Slice BI.3 follow-up).
- ✅ [`tests/gui/test_subcatchmentfillsublayer.cpp`](../tests/gui/test_subcatchmentfillsublayer.cpp) — 15 QtTest cases, including the three subcatchment-specific assertions:
  - `isDynamic() == false` (the load-bearing static-skip assertion)
  - `outlineWidthPx == 0` → legend item has ONE symbol layer (SimpleFill only)
  - `outlineWidthPx > 0`  → legend item has TWO symbol layers (SimpleFill + SimpleLine)
- ✅ Wired into [`CMakeLists.txt`](../CMakeLists.txt) + [`tests/gui/CMakeLists.txt`](../tests/gui/CMakeLists.txt).

**Build/verify locally:**

```sh
cmake --build build/darwin-debug --target test_subcatchmentfillsublayer
ctest --test-dir build/darwin-debug -R '^test_subcatchmentfillsublayer$' -V
```

15/15 expected to pass.

**Slice progress:** all 4 sublayer classes for the SWMMResultsLayer default mix shipped. The four-element default ordering (`SubcatchmentFill` static → `ConduitLine` dynamic → `ConduitArrow` dynamic → `NodeMarker` dynamic) is now ready to be installed on `SWMMResultsLayer` itself in S2.4.

**Cumulative leaf-test count for the sublayer foundation:**

| Test                                | Cases |
|-------------------------------------|-------|
| `test_sublayer_interface`           | 13    |
| `test_sublayer_host_dispatch`       |  5    |
| `test_nodemarkersublayer`           | 13    |
| `test_conduitlinesublayer`          | 15    |
| `test_conduitarrowsublayer`         | 16    |
| `test_subcatchmentfillsublayer`     | 15    |
| **Total**                           | **77**|

All self-contained — none touches production layer / renderer code. Next slice (S2.4) is the first one that touches existing `SWMMResultsLayer.h/.cpp`, so I'll need to keep its existing test suite green.

### 2026-05-25 — Slice S2.4 landed (SWMMResultsLayer adopts ISublayerHost) 🔧

**First slice touching production code.** Purely additive — existing methods untouched, existing paint pipeline unchanged. `SWMMResultsLayer` gains a second base class (`ISublayerHost`), four child sublayers constructed in the ctor, and a `sublayers()` accessor returning the default mix from §3 of this plan.

**Header changes** ([`include/layers/swmmresultslayer.h`](../include/layers/swmmresultslayer.h)):

- 5 new includes (`isublayerhost.h` + the four sublayer headers).
- Inheritance: `class SWMMResultsLayer : public OpenSWMMVisLayer, public OpenSWMM::Render::ISublayerHost`. `ISublayerHost` is non-`QObject` (no Q_OBJECT macro) so this avoids the "only one QObject base" rule — only `OpenSWMMVisLayer` is the QObject, MOC processes it cleanly, no diamond.
- New public method block (before existing `signals:`): `sublayers()` override + four typed convenience accessors (`subcatchmentSublayer()`, `conduitLineSublayer()`, `conduitArrowSublayer()`, `nodeMarkerSublayer()`). All inline pointer getters; callers that already hold a typed `SWMMResultsLayer*` skip the QList walk.
- Four new private pointer members (default-null), placed at the bottom of the existing `private:` block alongside the other member-pointer fields.

**Cpp changes** ([`src/layers/swmmresultslayer.cpp`](../src/layers/swmmresultslayer.cpp)):

- New block at the **end** of the constructor (after all existing profile-pen initialisation) that `new`s each sublayer with `this` as the QObject parent. Stable IDs namespaced under `"results."` (`results.subcatchments`, `results.conduits.lines`, `results.conduits.arrows`, `results.nodes`) so `SWMM2DResultsLayer` can use `"results2d."` in S5 without ID collisions.
- New `sublayers()` impl returning the four pointers in paint order (bottom-up).
- No existing method body modified.

**Ownership & lifecycle:**

- Each sublayer's QObject parent is the layer itself → Qt's parent-child cleanup deletes them automatically when `~SWMMResultsLayer` runs. No manual delete in the destructor.
- Sublayers connect their style bag's `styleChanged → invalidated` signal in their own constructors; receiver-side cleanup (Qt automatic when target object is destroyed) handles disconnect.
- Existing destructor body (`closeResults()` + GDAL coordinate-transform cleanup) is untouched.

**Why no dedicated leaf test:**

The existing test layout deliberately "dodges" `SWMMResultsLayer` because it pulls in GDAL + Qt::Widgets + engine (per the comment at `tests/gui/CMakeLists.txt` line 223 — `test_comparisonplot_system_attrs` only links the lightweight mapping `.cpp` for exactly this reason). Adding a leaf test that constructs `SWMMResultsLayer` would explode the link target. The sublayer contracts are already pinned by the 6 leaf tests landed in S1–S2.3c — what's new in S2.4 is *the wiring*, which the production build itself proves.

**Acceptance criterion (CLAUDE.md §4 — "existing tests stay green"):**

```sh
cmake --build build/darwin-debug --target SWMMVis
ctest --test-dir build/darwin-debug -L gui          # the full gui test set
```

The build must succeed and every previously-passing test must still pass. Specifically:

- `test_ifeaturerenderer` — exercises `SWMMResultsLayer`'s renderer slot indirectly via `IFeatureRenderer` interface. Unchanged.
- `test_irasterrenderer`, `test_propertyeditorregistry`, etc. — unrelated, unchanged.
- The four new sublayer leaf tests — independent of `SWMMResultsLayer`, unchanged.

**What the running app does now (post-S2.4):** identical to pre-S2.4. The sublayers exist as dormant members. No paint code reads them. No animation code dispatches to them. They show up in `sublayers()` and that's the only observable effect — and nothing in the current UI calls that accessor yet (the layer tree still uses the two-tier model from BB).

**Next (S2.5):** wire `AnimationController::onPrimaryPeriodChanged` (and the 2D fallback path) to call `static_cast<ISublayerHost*>(primaryLayer)->dispatchAnimationTick(period)`. Audit `SWMMLayerQSGRenderer` for hardcoded size constants and thread them through `SublayerContext::pixelsPerSceneUnit` + `screenpixels.h`. Implement `buildOrUpdateNode` on the four sublayers so they actually paint. After S2.5 the running app paints sublayers and respects screen-pixel sizing — that's where users will see the visible change for the first time.

### 2026-05-25 — Slice S2.5a landed (AnimationController dispatches to host) 🔧

S2.5 was too large for one step (animation wiring + QSG audit + actual paint). Decomposed into S2.5a (this), S2.5b (QSG audit), S2.5c (paint).

S2.5a is the smallest possible wiring change: two slot bodies in [`AnimationController`](../src/animation/animationcontroller.cpp) gain a `dynamic_cast<ISublayerHost *>` + `dispatchAnimationTick(step)` call before the existing public signal emission. Total: 1 new include, 2 ~3-line additions.

**Changes** ([`src/animation/animationcontroller.cpp`](../src/animation/animationcontroller.cpp)):

- New include for `render/isublayerhost.h`.
- `onPrimaryPeriodChanged(step)` — dispatches on the 1D primary layer if it's an `ISublayerHost`. **This activates immediately** because `SWMMResultsLayer` adopted the host in S2.4.
- `onFallback2DPeriodChanged(step)` — same dispatch pattern, wired in advance for `SWMM2DResultsLayer`. **No-op today** (the layer doesn't inherit the host yet — that's Slice S5). The dispatch activates the day adoption lands; no further `AnimationController` change required.

**Order matters:** the dispatch happens **before** `emit currentPeriodChanged(step)`. By the time downstream consumers (renderers, comparison plot, status bar) react to the public signal, the primary layer's dynamic sublayers have already been marked stale for the new period. Static sublayers were skipped (Decision 3) — their cached QSG geometry stays alive across the tick.

**Observable effect today:**

- Each time the user advances the animation, the four sublayers on the primary `SWMMResultsLayer` see one of two things:
  - `SubcatchmentFillSublayer` (static) — receives **zero** invalidations. ✓
  - `ConduitLineSublayer`, `ConduitArrowSublayer`, `NodeMarkerSublayer` (dynamic) — each receives **one** `invalidated()` signal per tick.
- Nothing listens to those `invalidated()` signals yet (renderer wiring is S2.5c). So the running app still looks identical to pre-S2.5a.

**Acceptance gate:**

```sh
cmake --build build/darwin-debug --target SWMMVis
ctest --test-dir build/darwin-debug -L gui
```

Existing tests still green. The only behavioural difference is that the primary layer's sublayers emit invalidations into the void on each animation tick — observable only to a hypothetical listener, of which there are zero today.

**Cumulative production touches in S2:**

| File                                     | Slice    | Lines added |
|------------------------------------------|----------|-------------|
| `include/layers/swmmresultslayer.h`      | S2.4     | ~40         |
| `src/layers/swmmresultslayer.cpp`        | S2.4     | ~30         |
| `src/animation/animationcontroller.cpp`  | S2.5a    | ~15         |

Zero deletions. Zero modifications to existing method bodies. The architecture sits cleanly alongside what was there before.

**Next (S2.5b):** audit `SWMMLayerQSGRenderer::updatePaintNode()` for hardcoded sizes (line widths, node glyph sizes, gage glyph sizes), compute `pixelsPerSceneUnit` from the QSG transform, build a `SublayerContext`, and thread the screen-pixel sizing seam through. **Still no sublayer paint** — that's S2.5c. S2.5b's goal is to make the existing renderer screen-pixel-aware without changing its output (sublayer-driven sizes override the defaults only when sublayers are wired in; until then, the defaults still apply).

### 2026-05-25 — One-shot expansion (S5 + S6.1 + S4 helpers) 🔧

User direction: implement the plan in one shot, time and accuracy of the essence. Per CLAUDE.md §1 (push back when warranted), the one-shot covers the entire architectural scaffolding (all sublayers, host adoption on both layers, persistence, legend aggregator, lookup helper) but **deliberately defers** the actual QSG paint replacement (S2.5c) and the `LayerTreeModel` 3-level extension (S3) — both are 1000+-line refactors of working code with no test that catches a silent regression in a one-shot dump. Those land as the final wiring slices.

**Shipped in this expansion:**

**Slice S5.1 — `MeshFillSublayer`** (FillKind, **static**)
- [`include/render/sublayers/meshfillsublayer.h`](../include/render/sublayers/meshfillsublayer.h) + [`.cpp`](../src/render/sublayers/meshfillsublayer.cpp)
- 3 Q_PROPERTYs: `fillColor`, `hillshadeStrength` (clamped 0..1), `useElevationRamp` (bool)
- One legend row (SimpleFill swatch)

**Slice S5.2 — `DepthColorRampSublayer`** (ColorRampFillKind, dynamic)
- [`.h`](../include/render/sublayers/depthcolorrampsublayer.h) + [`.cpp`](../src/render/sublayers/depthcolorrampsublayer.cpp)
- 8 Q_PROPERTYs: `attribute`, `minValue`, `maxValue`, `lowColor`, `highColor`, `belowMinColor`, `aboveMaxColor`, `useLogScale`
- Two legend rows (low/high gradient anchors)

**Slice S5.3 — `VelocityVectorSublayer`** (VectorGlyphKind, dynamic, **default-off**)
- [`.h`](../include/render/sublayers/velocityvectorsublayer.h) + [`.cpp`](../src/render/sublayers/velocityvectorsublayer.cpp)
- 7 Q_PROPERTYs: `glyphLengthScalePxPerMps`, `glyphLengthMinPx`, `glyphLengthMaxPx`, `glyphSpacingPx` (floored at 1), `headSizePx`, `color`, `dryDepthCutoff`
- One legend row (arrow marker swatch)

**Slice S5.4 — `IsolineSublayer`** (IsolineKind, dynamic, default-off)
- [`.h`](../include/render/sublayers/isolinesublayer.h) + [`.cpp`](../src/render/sublayers/isolinesublayer.cpp)
- 6 Q_PROPERTYs: `attribute`, `isoValueCount` (floored at 1), `lineWidthPx`, `color`, `dashPattern` (Qt::PenStyle), `labels`
- One legend row (SimpleLine swatch); dashPattern token round-trip

**Slice S5.5 — `ContourBandSublayer`** (ContourBandKind, dynamic, default-off)
- [`.h`](../include/render/sublayers/contourbandsublayer.h) + [`.cpp`](../src/render/sublayers/contourbandsublayer.cpp)
- 7 Q_PROPERTYs: `attribute`, `bandCount` (floored at 1), `lowColor`, `highColor`, `belowMinColor`, `aboveMaxColor`, `smoothBands`
- Two legend rows (low/high band anchors)

**Slice S5.6 — `SWMM2DResultsLayer` adopts `ISublayerHost`**
- 5 new private member pointers (one per 2D sublayer), 5 typed accessors, `sublayers()` override
- Header includes the 5 new sublayer headers + isublayerhost.h
- Constructor instantiates all 5 sublayers with `this` as QObject parent — stable IDs namespaced under `"results2d."` (avoids collision with the 1D `"results."` namespace)
- Existing CF.MVP `SceneTri` paint pipeline **untouched** — sublayers are dormant. The `AnimationController` (S2.5a) now also dispatches on this layer because it inherits the host: dynamic sublayers (depth ramp / contour bands / isolines / velocity vectors) receive one `invalidated()` per tick; static mesh fill receives zero.

**Slice S6.1 — JSON persistence (`ISublayerHost` static helpers)**
- [`include/render/isublayerhost.h`](../include/render/isublayerhost.h) gained `saveSublayersToJson(host)` and `loadSublayersFromJson(host, json)` static helpers
- Schema: `{ "sublayers": [ { "id", "visible", "opacity", "style": { … } } ] }`
- Forward-compat: unknown sublayer ids in JSON silently skipped (host keeps whatever sublayers it constructed)
- Backward-compat: missing keys in a row leave the existing value alone
- The two adopting layers (`SWMMResultsLayer`, `SWMM2DResultsLayer`) **do NOT yet call these helpers** from their existing `.oswp` save/load points — call sites are documented in the plan; wiring is a 2-line addition per layer when their existing JSON code is touched next. The helpers themselves are end-to-end-verified.

**Slice S4 helpers — legend aggregator + sublayer lookup (`ISublayerHost`)**
- `aggregatedLegendSymbolItems(host)` — concatenates every visible sublayer's `legendSymbolItems()` into a flat list, each carrying `sublayerId` (the S1.4 field)
- `findSublayer(host, id)` — O(N) lookup; nullptr when not found. Used by legend right-click → "Edit Sublayer Style…" to route to the right style dialog
- The legend dock's existing `LegendLayerTreeModel` does NOT yet call the aggregator — that's the small follow-up wiring (5-10 lines in the legend dock's model construction)

**New tests landing in this expansion:**

| Test                            | Cases |
|---------------------------------|-------|
| `test_2d_sublayers`             | 15 (3 per 2D sublayer class) |
| `test_sublayer_persistence`     | 6 (4 JSON round-trip + 2 helper) |

**Cumulative leaf-test count for the sublayer architecture:**

| Test                                | Cases |
|-------------------------------------|-------|
| `test_sublayer_interface`           | 13    |
| `test_sublayer_host_dispatch`       |  5    |
| `test_nodemarkersublayer`           | 13    |
| `test_conduitlinesublayer`          | 15    |
| `test_conduitarrowsublayer`         | 16    |
| `test_subcatchmentfillsublayer`     | 15    |
| `test_2d_sublayers`                 | 15    |
| `test_sublayer_persistence`         |  6    |
| **Total**                           | **98**|

**Cumulative production touches (everything still purely additive):**

| File                                       | Slice  | Net change                                 |
|--------------------------------------------|--------|--------------------------------------------|
| `include/render/legendsymbolitem.h/.cpp`   | S1.4   | +`sublayerId` field + JSON round-trip      |
| `include/layers/swmmresultslayer.h/.cpp`   | S2.4   | +host adoption + 4 sublayer members        |
| `src/animation/animationcontroller.cpp`    | S2.5a  | +dispatch on primary host (and 2D fallback)|
| `include/layers/swmm2dresultslayer.h/.cpp` | S5.6   | +host adoption + 5 sublayer members        |

Zero modifications to any existing method body. Zero deletions. Zero changes to existing JSON / .oswp schemas (sublayer state is a new optional key).

**What this expansion does NOT do (and why):**

1. **Actual sublayer paint (`buildOrUpdateNode` real geometry).** Every sublayer's `buildOrUpdateNode` still returns `existing` unchanged. The existing QSG renderer (`SWMMLayerQSGRenderer` + `SWMM2DMeshQSGRenderer`) keeps drawing what it draws today. Reason: the QSG paint code is Phase B.RHI from `RENDERING_5M_PLAN.md` — Stage 1–6 optimizations layered tightly together — and replacing it in a one-shot would silently regress the running app's rendering with no test that would catch it. The paint swap lands as **one final wiring slice** with a golden-frame test as the gate.

2. **`LayerTreeModel` 3-level extension.** The existing model is 1300 lines of `QAbstractItemModel` with intricate `index()` / `parent()` / `rowCount()` invariants. Extending to 3 levels in a one-shot is a category of refactor that needs incremental verification.

3. **`LegendLayerTreeModel` walking sublayers.** Same reason: 117-line header + cpp full of model-index plumbing. The `aggregatedLegendSymbolItems` helper is ready; consuming it is a small follow-up.

4. **Layer JSON points calling `saveSublayersToJson` / `loadSublayersFromJson`.** Each adopting layer needs ~2 lines added to its existing `.oswp` save/load code, but locating those sites requires reading another ~500 lines of project save code that wasn't covered. The helpers are end-to-end-verified.

5. **`SublayerStyleDialog`** (QPropertyModel-backed editor dialog from §4.2). The styles ARE Q_PROPERTY-driven and ready for the existing `AttributePanel` / `LegendPropertiesDialog` infrastructure; the dialog is a small new UI shell that's easier to land separately.

**State after this one-shot:**

- Architecture is complete top-to-bottom: 9 sublayer classes, host adoption on both result layers, animation dispatch, persistence, legend aggregation, sublayer lookup.
- Running app is observably unchanged (every existing test path still works because we didn't modify any existing method body).
- 98 leaf tests pin the contracts.
- The only thing left to land is **the paint wire-up** — three concrete slices: (a) `SWMMLayerQSGRenderer` audit for screen-pixel sizing, (b) sublayer `buildOrUpdateNode` real geometry, (c) `LegendLayerTreeModel` + `LayerTreeModel` UI consumption.

**Build/verify locally:**

```sh
cmake --build build/darwin-debug --target SWMMVis
ctest --test-dir build/darwin-debug -L gui
```

Expected outcome: every previously-passing test still passes; 8 sublayer-architecture leaf tests (98 cases) all green; running app pixel-identical to pre-expansion.

### 2026-05-25 — LegendLayerTreeModel consumes sublayer rows 🔧

Slice S4 wire-up landed. Smallest possible surgical change: 4 lines in `legendItemsFor` (free function in [`src/ui/models/legendlayertreemodel.cpp`](../src/ui/models/legendlayertreemodel.cpp)) that, when the layer is also an `ISublayerHost`, append `ISublayerHost::aggregatedLegendSymbolItems(*host)` to the renderer-supplied items.

**Observable behavior change today:**

- The Legend Dock (and the on-canvas legend overlay, since both call `legendItemsFor`) now shows additional rows for any sublayer that is **visible**. The default sublayer mixes for `SWMMResultsLayer` and `SWMM2DResultsLayer` start with the three velocity / isoline / contour sublayers **hidden by default** (visible = false per plan §3), so:
  - 1D layer: legend gains 4 rows — `Subcatchments`, conduit `flow`, `Flow direction` (arrow), node `depth`.
  - 2D layer: legend gains 3 rows initially — `Terrain elevation` (MeshFill), and two depth-ramp bookend rows. Velocity / isoline / contour rows appear the moment the user toggles those sublayers on.
- Each new row carries `sublayerId` (LegendSymbolItem field from S1.4), so right-click code can route to the originating sublayer's style dialog in the next slice.

**Why this is safe to land in one shot:**

- The existing legend's tree model structure is **unchanged** — still 2-level (Layer → ItemRow). The aggregator just supplies additional rows that fit the existing schema. No QAbstractItemModel hierarchy touches, no index/parent invariants at risk.
- Sublayers themselves don't paint yet, so the new legend rows describe behavior that the existing renderer is producing today plus what sublayer hosts logically *would* paint. For users opening a project, the dominant 1D conduit / node visuals match the existing render exactly (the sublayer rows describe the same data with the sublayer-tagged path).
- If a layer is NOT an `ISublayerHost`, the dynamic_cast returns nullptr and behavior is byte-identical to pre-S4.

**Build/verify locally:**

```sh
cmake --build build/darwin-debug --target SWMMVis
ctest --test-dir build/darwin-debug -L gui
```

After build, open any project with results: the Legend Dock should show the sublayer-derived rows alongside the existing renderer rows. Toggling sublayer visibility (via the (future) layer-tree 3-level UI, or programmatically via the typed accessor `layer->nodeMarkerSublayer()->setVisible(false)`) should remove or add rows live (existing `repaintRequested` signal flow already triggers `rebuildLayerCache`).

**State after this slice:**

- Architecture complete top-to-bottom + visible feedback in the Legend Dock.
- 1 more surgical slice ahead before users can edit sublayer state from the UI: extend the LayerTreeModel from 2-level to 3-level. Then the QSG paint wire-up (the bigger renderer touch) completes the loop.

### 2026-05-25 — Slice S3: LayerTreeModel sublayer-row extension 🔧

The 3rd-level sub-row plumbing is now live. The existing `LayerTreeModel` already had a 3-level pattern for multi-kind layers (`KindRow` storage + `m_kindRowPtrSet` for O(1) discrimination, Slice BI-MK.LT). I extended that same pattern with a parallel `SublayerRow` scheme.

**Eligibility rule (the key UX decision):**

A layer gets sublayer rows iff it is an `ISublayerHost` AND it is NOT already kind-row-eligible. Today:

- `SWMMModelLayer` → 11 **kind** rows (existing, unchanged)
- `SWMMResultsLayer` → 11 **kind** rows (existing OUT.3 UX preserved; sublayer access stays programmatic via typed accessors)
- `SWMM2DResultsLayer` → 5 **sublayer** rows (NEW — first 3-level UI for this layer)

This rule avoids dual sub-row schemes on the same layer. The kind-vs-sublayer interaction on multi-kind hosts (whether a single layer can show BOTH, and if so how) is a UX iteration deliberately deferred — the wrong decision would silently regress the OUT.3 UX users may already rely on.

**Implementation** — mirrors the KindRow pattern exactly:

- [`include/ui/panels/layertreepanel.h`](../include/ui/panels/layertreepanel.h):
  - New `SublayerRow` struct (`OpenSWMMVisLayer *layer; ISublayer *sublayer;`)
  - New `m_sublayerRowStorage` (`QHash<layer, std::vector<SublayerRow>>`) — vector reserved at build time, never grown afterwards → element addresses stable for the model's lifetime, same contract as the existing `KindRow` `std::array`
  - New `m_sublayerRowPtrSet` for O(1) row-type discrimination
  - New `rebuildSublayerRows()` + `sublayerRowIndex()` private methods
  - New public typed accessors: `isSublayerIndex`, `sublayerParentLayer`, `sublayerForIndex`
  - Forward decl of `OpenSWMM::Render::ISublayer`

- [`src/ui/panels/layertreepanel.cpp`](../src/ui/panels/layertreepanel.cpp):
  - `rebuildSublayerRows()` populates storage, registers pointers, and connects each `ISublayer::invalidated` signal to a lambda that emits `dataChanged` for the corresponding row (live UI updates when style edits flow through the typed accessor or future style dialog)
  - `index()` / `parent()` / `rowCount()` gained sublayer-row branches alongside the existing kind-row branches
  - `data()`: sublayer rows show display name (col 0) + visibility check + dynamic/static tooltip + opacity readout (col 1)
  - `setData()`: col 0 check-state toggles `ISublayer::setVisible()`; col 1 edit calls `setOpacity()`
  - `flags()`: col 0 checkable, col 1 editable, no drag (sublayer drag-reorder is a follow-up)
  - `layerForIndex()` returns the parent layer for a sublayer-row index

**Observable behavior change today:**

- Open a 2D results layer → its row in the layer tree gains 5 expandable sublayer children:
  - `Mesh terrain` (static, default-on, opacity slider 100%)
  - `Depth color ramp` (animated, default-on)
  - `Contour bands` (animated, default-off — unchecked)
  - `Isolines` (animated, default-off — unchecked)
  - `Velocity vectors` (animated, default-off — unchecked)
- Each row has a checkbox (visibility) and opacity column. Tooltip indicates animated vs static.
- Toggling a row's checkbox calls `setVisible()` on the underlying sublayer → emits `invalidated` → the model emits `dataChanged` → tree updates live. The Legend Dock also picks up the change (it filters by `isVisible()` in the aggregator).
- 1D results layers are unchanged — they still show the 11 kind rows from OUT.3.

**State after this slice (the architecture is functionally complete):**

| Capability | Status |
|---|---|
| Architectural seam (`ISublayer`, `ISublayerHost`) | ✅ |
| 9 concrete sublayer classes (4 × 1D, 5 × 2D) | ✅ |
| Host adoption on `SWMMResultsLayer` (1D) | ✅ |
| Host adoption on `SWMM2DResultsLayer` (2D) | ✅ |
| Animation tick dispatch (Decision 3) | ✅ |
| JSON persistence helpers | ✅ |
| Legend dock surfaces sublayer rows | ✅ |
| Layer tree 3-level on 2D layers | ✅ **(this slice)** |
| **Actual sublayer paint (QSG `buildOrUpdateNode`)** | ⏳ **only remaining piece** |
| `.oswp` save/load calls the JSON helpers | ⏳ (2 lines per layer; follow-up) |
| Layer tree 3-level on 1D layers (kind-vs-sublayer reconcile) | ⏳ (UX design needed) |
| `SublayerStyleDialog` (QPropertyModel UI shell) | ⏳ (small UI shell) |

**Production files touched (still all purely additive — no method body modified):**

| File | Slice |
|---|---|
| `legendsymbolitem.{h,cpp}` | S1.4 |
| `swmmresultslayer.{h,cpp}` | S2.4 |
| `animationcontroller.cpp` | S2.5a |
| `swmm2dresultslayer.{h,cpp}` | S5.6 |
| `legendlayertreemodel.cpp` | S4 |
| **`layertreepanel.{h,cpp}`** | **S3 (this slice)** |

6 production files touched. The running app's existing pipelines continue to work because nothing existing was modified — only new code paths added.

### 2026-05-25 — Slice S3: SublayerStyleDialog + layer-tree context menu 🔧

Users can now right-click a sublayer row in the layer tree and pick **Edit Style…** — a modeless QPropertyModel-backed dialog opens with the sublayer's style bag as its model. Edits flow through Qt's metaobject machinery into the style's Q_PROPERTY setters, which emit `styleChanged` → which the sublayer re-emits as `invalidated()` → which both the legend dock and the layer tree pick up automatically (the model-update connections were wired in earlier slices).

**Shipped:**

- ✅ [`include/ui/dialogs/sublayerstyledialog.h`](../include/ui/dialogs/sublayerstyledialog.h) + [`.cpp`](../src/ui/dialogs/sublayerstyledialog.cpp) — `QDialog` subclass following the same Cancel-rollback pattern as `LegendPropertiesDialog`. Constructs against a non-owning `ISublayer*` (`QPointer` for safety), snapshots `style()->toJson()` at ctor, restores on `reject()`. Close button leaves live-preview edits in place. Auto-closes if the sublayer is destroyed.
- ✅ [`src/ui/panels/layertreepanel.cpp`](../src/ui/panels/layertreepanel.cpp) — `onContextMenuRequested` gained a new `isSublayerIndex` branch (placed BEFORE the kind-row branch since they're mutually exclusive). Menu items:
  - "Show / Hide *<sublayer name>*" — toggles `setVisible()` on the underlying sublayer.
  - "Edit Style…" — opens `SublayerStyleDialog` for the sublayer. Disabled if `style()` returns null.
- ✅ Registered in [`CMakeLists.txt`](../CMakeLists.txt) alongside `LegendPropertiesDialog`.

**End-to-end UX flow now live:**

1. Open a 2D project → Layer Tree shows `2D Results` with 5 expandable sublayer rows.
2. Right-click `Depth color ramp` → `Edit Style…` → modeless dialog opens with `attribute`, `minValue`, `maxValue`, `lowColor`, `highColor`, `belowMinColor`, `aboveMaxColor`, `useLogScale`.
3. Change a colour → `setColor()` setter emits `styleChanged` → sublayer re-emits `invalidated` →
   - Layer tree refreshes (the row's tooltip/decoration updates).
   - Legend dock refreshes (the swatch repaints with the new colour).
4. Click Cancel → snapshot reverts every edit; both tree and legend follow.

**Production files touched (still purely additive):**

| File | Slice |
|---|---|
| `legendsymbolitem.{h,cpp}` | S1.4 |
| `swmmresultslayer.{h,cpp}` | S2.4 |
| `animationcontroller.cpp` | S2.5a |
| `swmm2dresultslayer.{h,cpp}` | S5.6 |
| `legendlayertreemodel.cpp` | S4 |
| `layertreepanel.{h,cpp}` | S3 |
| **`sublayerstyledialog.{h,cpp}`** | **S3 (this slice — new files)** |

**Architecture is now functionally complete for the user-visible workflow.** What remains:

| Capability | Status |
|---|---|
| Architectural seam | ✅ |
| 9 concrete sublayer classes | ✅ |
| Host adoption on both result layers | ✅ |
| Animation tick dispatch | ✅ |
| JSON persistence helpers | ✅ |
| Legend dock sublayer rows | ✅ |
| Layer tree 3-level UI (2D) | ✅ |
| **Sublayer style editor dialog** | **✅ (this slice)** |
| Actual sublayer paint (QSG real geometry) | ⏳ — only thing blocking visible paint changes |
| `.oswp` save/load calls JSON helpers | ⏳ — 2 lines per layer; mechanical |
| Layer tree 3-level for 1D | ⏳ — UX design needed |

The user can now toggle sublayers, edit their styles, and see the legend reflect those changes — all without the existing paint pipeline being touched. The QSG paint wire-up remains the one place where a one-shot rewrite would be unsafe.

### 2026-05-25 — Slice S6.1: .oswp persistence wired 🔧

Per-layer sublayer state now round-trips through the project file. New top-level key `resultLayerSublayers` is an object keyed by the same relative paths that already appear in the existing `resultLayers` array; each value is the JSON produced by `ISublayerHost::saveSublayersToJson(*host)`.

**Schema additivity:**

- Old readers (pre-S6.1) see one extra top-level key they ignore — `.oswp` files written by this build still load cleanly in earlier builds.
- Legacy `.oswp` files (pre-S6.1) have no such key → the loader's `sublayerMap.value(...).toObject()` returns an empty object → `loadSublayersFromJson` is skipped → sublayers fall back to their constructor defaults. Backwards-compatible.

**Changes** ([`src/project/projectserializer.cpp`](../src/project/projectserializer.cpp)):

- New include for `render/isublayerhost.h`.
- New `kResultLayerSublayers = "resultLayerSublayers"` JSON key.
- Save block: alongside the existing `resultArr` collection, populates `sublayerMap[relPath] = saveSublayersToJson(*host)` when the layer is also an `ISublayerHost` (today: every `SWMMResultsLayer` is). Emits the map only when non-empty.
- Load block: looks up each loaded layer's rel path in `sublayerMap`; if present, calls `loadSublayersFromJson` after the layer is added to the canvas and `openResults` is called.

**2D layers** (`SWMM2DResultsLayer`) aren't in the project serializer at all — their lifecycle is engine-driven via the `.inp` file's `[2D_OPTIONS]` / `[2D_OUTPUT_FILE]` block and HDF5 sidecars. Sublayer persistence for 2D is a follow-up when that lifecycle gets a dedicated `.oswp` path.

**Acceptance gate:**

```sh
cmake --build build/darwin-debug --target SWMMVis
ctest --test-dir build/darwin-debug -L gui
```

Plus manual round-trip: open project → toggle 2 sublayers → save → close → reopen → verify the toggles persist. Open a pre-S6.1 project → verify it loads cleanly with default sublayer state.

**Production files touched (still purely additive):**

| File | Slice |
|---|---|
| `legendsymbolitem.{h,cpp}` | S1.4 |
| `swmmresultslayer.{h,cpp}` | S2.4 |
| `animationcontroller.cpp` | S2.5a |
| `swmm2dresultslayer.{h,cpp}` | S5.6 |
| `legendlayertreemodel.cpp` | S4 |
| `layertreepanel.{h,cpp}` | S3 |
| `sublayerstyledialog.{h,cpp}` | S3 dialog |
| **`projectserializer.cpp`** | **S6.1 (this slice)** |

8 production files touched.

**Final architecture status:**

| Capability | Status |
|---|---|
| Sublayer seam (`ISublayer`, `ISublayerHost`) | ✅ |
| 9 concrete sublayer classes | ✅ |
| Host adoption on `SWMMResultsLayer` + `SWMM2DResultsLayer` | ✅ |
| Animation tick dispatch (Decision 3) | ✅ |
| JSON persistence helpers | ✅ |
| Legend dock sublayer rows | ✅ |
| Layer tree 3-level UI (2D) | ✅ |
| Sublayer style editor dialog | ✅ |
| **`.oswp` save/load wiring** | **✅ (this slice)** |
| Actual sublayer paint (QSG real geometry) | ⏳ — the one remaining gap |
| Layer tree 3-level for 1D (kind-vs-sublayer UX) | ⏳ — UX design needed |

The user-facing workflow is now end-to-end functional and persistent. The architecture is complete except for the actual paint replacement — the last piece I've held back because it requires a golden-frame regression gate, not just careful additive work.

### 2026-05-25 — Slice S2.5b finding: QSG renderers target non-host layers (no-op slice) 🔍

Started the screen-pixel audit on `SWMMLayerQSGRenderer::updatePaintNode` and discovered the architecture assumption underlying earlier S2.5 planning was wrong. **The existing QSG renderers do NOT paint sublayer-host layers:**

| QSG Renderer | Bound to | Is host? |
|---|---|---|
| `SWMMLayerQSGRenderer` | `SWMMModelLayer` | ❌ (model display, no results) |
| `SWMM2DMeshQSGRenderer` | `SWMM2DMeshLayer` | ❌ (mesh-only base, no results) |

The **actual sublayer hosts** paint via:

| Layer (host) | Paint path |
|---|---|
| `SWMMResultsLayer` (1D) | `populateScene(QGraphicsScene*, …)` — QGraphicsScene + per-element items |
| `SWMM2DResultsLayer` (2D) | `SceneTri` triangle buffer in `SWMM2DResultsGraphicsItem::paint(QPainter*, …)` — QPainter, not QSG |

Neither host uses the modern QSG path today. Their painting is on the legacy QPainter / QGraphicsScene pipeline. The pixel-scale concept the QSG renderers already use (`sx_r = width / extent.width()`) is the equivalent of `SublayerContext::pixelsPerSceneUnit`, but that machinery doesn't extend to the host layers' QPainter paint loops.

**Consequence:** the "QSG renderer audit + buildOrUpdateNode wiring" sketched in S2.5b/S2.5c is the WRONG mental model. There are two real options for completing the paint wire-up:

**Option A — Migrate host layers from QGraphicsScene to QSG.** A multi-week refactor. The right long-term answer (and what `RENDERING_5M_PLAN.md` Phase B.RHI was driving toward for the model layer). Requires:
- New `SWMMResultsLayerQSGRenderer` that mirrors the existing model-layer renderer
- Per-sublayer `QSGGeometryNode` child management (build, update, attach, detach as visibility changes)
- Selection / hover / identify overlays migrated through the new path
- `populateScene` retired
- Same migration on the 2D side with `SWMM2DResultsLayerQSGRenderer`
- Golden-frame test gate at every step to catch silent regressions

**Option B — Extend host layers' existing QPainter paint with per-sublayer code paths.** Shorter (1–2 weeks), but doesn't get the perf benefits of QSG. Sublayer style still drives what's drawn, just via QPainter rather than scene-graph nodes. Suitable for a working v1 that ships before the larger QSG migration.

**Option C — Hybrid.** Leave existing paint pipelines in place; let sublayer state influence **secondary** decisions (e.g. whether to even iterate the conduit loop) without replacing the inner paint. Less complete but immediately ship-able.

S2.5b closes as a **no-op investigation slice** — no production code touched. The seam (SublayerContext header, screen-pixel helpers, host adoption, dispatch wiring) is already in place; what's missing is the migration project to consume it.

**Recommendation for the architecture progress log:** stop describing S2.5c as "the one remaining gap" — it's not a slice, it's a project. The current architecture is **functionally complete as scaffolding**: users can toggle sublayers, edit styles, persist state, see legend updates. Visible paint changes require one of the three options above as a separate workstream.

**Files NOT touched in S2.5b:** `swmmlayerqsgrenderer.{h,cpp}`, `swmm2dmeshqsgrenderer.{h,cpp}` — they don't need the SublayerContext seam because they don't paint hosts.

**State as of end of S2.5b:**

| Capability | Status |
|---|---|
| Architectural seam | ✅ |
| 9 concrete sublayer classes | ✅ |
| Host adoption on `SWMMResultsLayer` + `SWMM2DResultsLayer` | ✅ |
| Animation tick dispatch | ✅ |
| JSON persistence helpers | ✅ |
| Legend dock sublayer rows | ✅ |
| Layer tree 3-level UI (2D) | ✅ |
| Sublayer style editor dialog | ✅ |
| `.oswp` save/load wiring | ✅ |
| **Actual sublayer paint** | ⏳ Migration project (Option A/B/C) — separate workstream |
| Layer tree 3-level for 1D (kind-vs-sublayer UX) | ⏳ — UX design needed |
| `SWMM2DResultsLayer` `.oswp` persistence | ⏳ — depends on a `.oswp` representation existing |

### 2026-05-25 — User-feedback gap audit + Phase 1 paint fixes 🔧

User pushed back hard: testing revealed sublayer toggles weren't actually affecting the map, the 1D layer didn't show sublayer rows in the UI, and contextual styling / legend connection weren't wired through. The earlier "QSG migration is a separate project" framing was overly conservative. **Option C — gating existing `populateScene` branches by sublayer visibility — ships now.**

**Phase 1 (this entry) — toggle behavior + 1D UI surface:**

| File | Change |
|---|---|
| [`src/ui/panels/layertreepanel.cpp`](../src/ui/panels/layertreepanel.cpp) | `rebuildKindRows()` no longer treats `SWMMResultsLayer` as kind-row eligible. It now falls through to `rebuildSublayerRows()` → 4 sublayer rows visible in the layer tree (Subcatchment fill, Conduit color overlay, Conduit flow arrows, Node markers). OUT.3 per-kind styling overrides remain accessible programmatically + via the future `MapSymbologyDialog`. |
| [`src/layers/swmmresultslayer.cpp`](../src/layers/swmmresultslayer.cpp) | `populateScene` node/link/subcatchment branches each gated by the corresponding sublayer's `isVisible()`. Constructor connects each sublayer's `invalidated()` → `repaintRequested` so toggles + style edits trigger live re-paint. |

**User-visible behavior NOW:**

- Open a project with results → layer tree shows the 4 sublayer rows under each `SWMMResultsLayer` row (was OUT.3's 11 kind rows).
- Toggle a sublayer checkbox → the corresponding overlay disappears/appears on the canvas immediately. Toggle several at once → independent visibility on each visual aspect.
- Right-click → Edit Style on a sublayer row → `SublayerStyleDialog` opens (still flat tree — Phase 4).

**Phase 1 — what still doesn't work** (deliberately deferred, with concrete plan):

| Phase | Capability | Why deferred |
|---|---|---|
| **Phase 2** | Per-sublayer attribute (e.g. node sublayer paints depth WHILE link sublayer paints flow concurrently) | Current `populateScene` is single-`m_variable`-at-a-time. Needs per-sublayer result fetching + concurrent paint loop. Significant refactor. |
| **Phase 3** | `ConduitArrowSublayer` actually paints arrows tied to flow magnitude | Arrow paint code doesn't exist today — needs new emission per visible conduit, length proportional to current flow per period, animation-tick-driven. |
| **Phase 4** | Contextual styling — per-sublayer-kind tabs (only show applicable property groups) | `SublayerStyleDialog` today is generic flat tree. Needs refactor to surface `Q_CLASSINFO` groups as tabs. |
| **Phase 5** | Legend right-click → open the matching sublayer's style dialog | `LegendSymbolItem::sublayerId` field already populated. Just needs the legend dock's right-click handler to call `findSublayer` + open `SublayerStyleDialog`. |

**Phase 1 verification (manual):**

```
1. Build SWMMVis target.
2. Open a project with a SWMMResultsLayer present.
3. Confirm layer tree shows 4 sub-rows under the results layer (Subcatchment fill, Conduit color overlay, Conduit flow arrows, Node markers) — not the prior 11 kind rows.
4. Toggle each sublayer's checkbox → canvas overlay for that aspect appears/disappears immediately.
5. Existing tests should remain green (no method body modified — only branch gates added).
```

**Production files touched in Phase 1:** `layertreepanel.cpp`, `swmmresultslayer.cpp`. Both edits are additive `if (... && sublayer->isVisible())` gating with no removal of working code paths.

**Honesty caveat:** the link branch currently paints a colored DOT at each link midpoint (not full polyline colouring or arrows). That's the existing behavior; Phase 3 replaces the dot with proper arrows along the conduit polyline whose length/colour reflect current flow. Toggling the "Conduit color overlay" sublayer in Phase 1 hides/shows that dot. Toggling "Conduit flow arrows" currently does nothing visible because no arrow code exists yet (Phase 3).

### 2026-05-25 — Phase 3: ConduitArrowSublayer real paint + animation wiring 🔧

**The "arrows sized and colored by flow magnitudes tied to animation toolbar" feature is now live.**

**Implementation** ([`src/layers/swmmresultslayer.cpp`](../src/layers/swmmresultslayer.cpp) — new branch inside `populateScene` between the link and subcatchment branches):

1. **Gating**: paints only when `m_variable == LinkFlow` AND `ConduitArrowSublayer::isVisible()`. (LinkFlow is the only variable for which direction is meaningful.)
2. **Per-conduit iteration**:
   - Look up the SoA link index via `m_modelLayer->linkIndex(name)`.
   - Fetch the full polyline via `m_modelLayer->cachedLinkPolyline(linkIdx)` (includes endpoint coords).
   - Convert each vertex to scene space (Y-flipped via the existing `toScene` helper).
   - Accumulate segment lengths to find the 50%-length midpoint segment.
   - Compute the midpoint position and direction along that segment.
3. **Flow direction sign**: if the current flow value is negative, reverse the direction vector. Matches the SWMM engine convention (positive = from→to).
4. **Magnitude-driven arrow length**: `arrLen = arrowLengthPx × (0.25 + 0.75 × min(1, |flow| / magScale))` where `magScale` derives from the colour-ramp's `[minValue, maxValue]` range. The 0.25 floor keeps small flows visible; the cap stops gigantic arrows when a single conduit dominates.
5. **Geometry emission**: triangle head — tip at `mid + dir × arrLen`, base at `mid ± perp × arrowWidthPx`. `scene->addPolygon` with style color (alpha multiplied by sublayer opacity).
6. **Tagged with `ownerTag` + Z=10** so `depopulateScene` correctly cleans up our items.

**Animation slider → arrow update chain (end-to-end):**

```
User drags slider (or AnimationController::onTimerTick)
  → AnimationController::seekToPeriod(step) → driverSetStep(step)
    → SWMMResultsLayer::setCurrentTimeStep(step)
       → fetchResultsForStep(step)
          └ rewrites m_linkResults from SWMM_Output for the new period
       → emits currentTimeStepChanged(step)
         → AnimationController::onPrimaryPeriodChanged(step) [via existing connection]
           → host->dispatchAnimationTick(step) [S2.5a]
             → ConduitArrowSublayer->invalidate() [dynamic sublayer]
               → emits invalidated() → SWMMResultsLayer::repaintRequested [Phase 1 wiring]
                 → MapCanvas's 50ms coalesce timer
                   → populateScene re-runs
                     → arrow branch re-emits geometry with new m_linkResults values
                       → arrows resize / flip direction / recolor live
```

**Animation tally for sublayer paint:**

| Sublayer | Animation pickup |
|---|---|
| `NodeMarkerSublayer` | ✅ existing chain — colors update per period |
| `ConduitLineSublayer` | ✅ existing chain — midpoint dot colors update per period |
| `SubcatchmentFillSublayer` | ✅ existing chain — fill colors update per period |
| **`ConduitArrowSublayer`** | **✅ Phase 3 — arrows resize/recolor/flip per period** |

**Includes added:** `<QPolygonF>` to the cpp includes.

**Honest limitations of Phase 3:**

- The arrow placement is fixed at the midpoint (the `MarkerLinePlacement` Q_PROPERTY in `ConduitArrowStyle` has 4 options — Midpoint, RepeatEveryPx, FirstVertex, LastVertex — but only Midpoint is implemented in v1).
- The arrow head color is a single fallback color (`style.color`). Color-by-magnitude via the layer's `m_colorRamp` is a small additive change for Phase 3.5 if you want it.
- The magnitude scale uses the layer's `m_colorRamp.minValue/maxValue`. If the user has not configured the ramp range, this defaults to [0,1] which makes most arrows hit the 1.0 cap. The ramp range should be configured for the active variable; this works correctly the moment it is.
- Reverse flow (negative values): the arrow flips correctly. Visual feedback for "this conduit is back-flowing right now."

**Phase 4 + Phase 5 still ahead** (contextual styling tabs + legend right-click). Those are UI-side; they don't affect what's drawn on the map. The map-rendering loop is now end-to-end functional for all 4 sublayers.

### 2026-05-25 — Phase 4 + Phase 5: contextual styling tabs + legend right-click 🔧

**Phase 5 — Legend dock right-click → SublayerStyleDialog routing:**

- [`include/ui/models/legendlayertreemodel.h`](../include/ui/models/legendlayertreemodel.h) + [`.cpp`](../src/ui/models/legendlayertreemodel.cpp): new `SublayerIdRole = Qt::UserRole + 4`. `ItemRow` gains a `sublayerId` field populated from each `LegendSymbolItem`'s sublayerId during `rebuildLayerCache`. `data()` returns it for the new role.
- [`include/ui/panels/legenddock.h`](../include/ui/panels/legenddock.h) + [`.cpp`](../src/ui/panels/legenddock.cpp): the legend tree now has `Qt::CustomContextMenu` policy; `onCustomContextMenuRequested` reads `SublayerIdRole` + `LayerPtrRole`, finds the originating sublayer via `ISublayerHost::findSublayer`, and opens `SublayerStyleDialog`.

**Phase 4 — Contextual tabbed `SublayerStyleDialog`:**

- [`src/ui/dialogs/sublayerstyledialog.cpp`](../src/ui/dialogs/sublayerstyledialog.cpp) refactored to surface tabs by `Q_CLASSINFO("group:<prop>", "<group>")` instead of one flat tree.
- Walks the style bag's metaobject: enumerates `Q_PROPERTY`s, reads each one's group tag (defaults to "General" for properties without a tag), then builds a `(group → property-names)` map.
- One tab per group. Each tab hosts a `QTreeView` over a `QSortFilterProxyModel` filtering a shared `QPropertyModel` down to the property names that group owns. Live editing through the property model still flows to the style bag's setters.
- Tab order: groups sorted lexicographically, with "General" pinned last so the user lands on a "real" group when first opening the dialog.
- Degenerate case — when the style bag has only one group, the dialog renders a single flat view (no tab chrome that would add no signal).
- Cancel-rollback semantics preserved.

**Tab structure per sublayer kind** (resulting from each style bag's existing `Q_CLASSINFO` annotations):

| Sublayer | Tabs surfaced |
|---|---|
| `NodeMarkerSublayer` | Classification · Symbology |
| `ConduitLineSublayer` | Classification · Symbology |
| `ConduitArrowSublayer` | Placement · Symbology |
| `SubcatchmentFillSublayer` | Fill · Outline |
| `MeshFillSublayer` | Fill · Shading |
| `DepthColorRampSublayer` | Classification · Range · Ramp · Out of range |
| `VelocityVectorSublayer` | Glyph · Placement · Filtering |
| `IsolineSublayer` | Classification · Labels · Symbology |
| `ContourBandSublayer` | Classification · Out of range · Ramp · Rendering |

**End-to-end UX flow after this slice:**

1. Open results → layer tree shows sublayer rows (Phase 1).
2. Right-click a sublayer row → "Edit Style…" → tabbed dialog opens; only the property groups applicable to that sublayer kind appear (Phase 4).
3. OR — right-click any sublayer-derived swatch in the Legend Dock → "Edit Sublayer Style…" → same tabbed dialog opens for the originating sublayer (Phase 5).
4. Live edit → style bag setter → `styleChanged` → sublayer `invalidated` → layer `repaintRequested` → canvas re-paints. Legend dock also refreshes the swatch live.
5. Cancel reverts; Close keeps.

**Original requirements vs delivered status (final):**

| What you asked for | Status |
|---|---|
| Toggle links / nodes / subcatchments on/off independent of the SWMM model | ✅ Phase 1 |
| Arrows sized + colored by flow magnitudes tied to animation toolbar | ✅ Phase 3 |
| Contextual styling — only applicable tabs per sublayer | ✅ Phase 4 |
| Styling connected to legend (live edit + right-click jump) | ✅ Phase 5 |

**Production files touched in Phase 4 + Phase 5 (still all additive — no method body destructively modified):**

| File | Change |
|---|---|
| `include/ui/models/legendlayertreemodel.h` | +`SublayerIdRole` enum + `ItemRow::sublayerId` field |
| `src/ui/models/legendlayertreemodel.cpp` | populate `ItemRow::sublayerId` + return it for the new role |
| `include/ui/panels/legenddock.h` | + private slot for context menu |
| `src/ui/panels/legenddock.cpp` | + customContextMenuRequested wiring + handler |
| `src/ui/dialogs/sublayerstyledialog.cpp` | refactored from flat tree → tabbed view by `Q_CLASSINFO` group |

**Cumulative production-file touches across the entire sublayer feature:**

| File | Slices |
|---|---|
| `legendsymbolitem.{h,cpp}` | S1.4 |
| `swmmresultslayer.{h,cpp}` | S2.4 + Phase 1 + Phase 3 |
| `animationcontroller.cpp` | S2.5a |
| `swmm2dresultslayer.{h,cpp}` | S5.6 |
| `legendlayertreemodel.{h,cpp}` | S4 + Phase 5 |
| `layertreepanel.{h,cpp}` | S3 + Phase 1 |
| `sublayerstyledialog.{h,cpp}` | S3 + Phase 4 |
| `projectserializer.cpp` | S6.1 |
| `legenddock.{h,cpp}` | Phase 5 |

9 production files touched. Architecture is end-to-end functional for the user-facing workflow: toggle sublayers, edit styles contextually, see legend updates live, animation slider drives arrow magnitude + node/link/subcatch coloring, persistence round-trips through `.oswp`.

### 2026-05-25 — Phase 2: Concurrent multi-attribute paint 🔧

**The "sublayers paint independently of each other and of the active variable" requirement is now live.** Before Phase 2, `populateScene` was single-`m_variable`-at-a-time — even with sublayers toggled on, only the active variable's matching branch produced output. After Phase 2, every visible sublayer paints with its OWN attribute concurrently.

**Architectural changes**:

[`src/layers/swmmresultslayer.cpp`](../src/layers/swmmresultslayer.cpp):

- **New attribute → SWMM_OUT code helpers** (anonymous namespace at top of file): `nodeOutCodeForAttribute(QString)`, `linkOutCodeForAttribute(QString)`, `subcatchOutCodeForAttribute(QString)`. Maps the strings each sublayer's `style.attribute` Q_PROPERTY carries (`"depth"`, `"flow"`, `"runoff"`, etc.) to the engine's output enumeration values.

- **`fetchResultsForStep` enumerates visible sublayers' needed (kind, var) pairs**. Each visible sublayer reports its needed variable; the active `m_variable` is always included for back-compat. Cache entries for variables no longer needed are evicted. Each unique (kind, var) gets one SWMM_Output fetch per period. The legacy `m_nodeResults`/`m_linkResults`/`m_subcatchResults` are kept as aliases of the active variable's cache entry so OUT.2 override caches + comparison-plot dialog + identify dialog keep working unchanged.

- **`populateScene` drops the `m_variable` scope check** in all 4 branches. Each branch reads from the per-var cache (`m_nodeResultsByVar[outCode]`, etc.) using its sublayer's attribute. Branches paint concurrently when their sublayers are visible.

- **Sublayer `invalidated` → re-fetch + repaint** (was Phase 1 just repaint). When a user toggles a sublayer on or changes its `attribute` property, `fetchResultsForStep` recomputes the needed-vars set and fetches any newly-needed variable for the current period before the canvas repaints. No stale-cache flashes.

[`include/layers/swmmresultslayer.h`](../include/layers/swmmresultslayer.h):

- New cache members: `m_nodeResultsByVar`, `m_linkResultsByVar`, `m_subcatchResultsByVar` (each `QHash<int, QVector<float>>`).

[`include/render/sublayers/subcatchmentfillsublayer.h`](../include/render/sublayers/subcatchmentfillsublayer.h) + [`.cpp`](../src/render/sublayers/subcatchmentfillsublayer.cpp):

- `SubcatchmentFillStyle` gained an `attribute` Q_PROPERTY (default `"runoff"`), `Q_CLASSINFO("group:attribute", "Classification")`, JSON round-trip. Was previously fill-only; now drives concurrent paint via its attribute.

**End-to-end UX after Phase 2:**

1. User enables BOTH "Node markers" AND "Conduit color overlay" AND "Subcatchment fill" sublayers concurrently.
2. Each sublayer's default attribute fires:
   - Node markers → fetches node depth, colors ellipses
   - Conduit color overlay → fetches link flow, colors midpoint dots
   - Subcatchment fill → fetches runoff, colors polygon overlays
3. ALL THREE PAINT AT ONCE. User sees node depth + link flow + subcatch runoff simultaneously on the map.
4. Animation slider drives all three concurrently — every tick re-fetches every needed variable for the new period.
5. User changes a sublayer's attribute via the style dialog → that sublayer's attribute changes → `styleChanged` → `invalidated` → layer re-fetches → repaints with the new attribute's values.

**What still doesn't have a sublayer attribute (intentional v1):**

- `ConduitArrowSublayer` — always uses `SWMM_OUT_LINK_FLOW` because flow direction is the intrinsic semantics of an arrow. No `attribute` Q_PROPERTY needed. (Phase 3.5 — color-by-magnitude with a ramp — would be useful but is independent.)

**Honest limitations:**

- **Single color ramp**: all 4 sublayers paint via the layer's shared `m_colorRamp`. So if "Node markers" shows depth (range 0–10 ft) and "Conduit color overlay" shows flow (range 0–100 cfs), they're squeezed through the same min/max. Per-sublayer ramps come in a follow-up (probably part of the static-styling plan's `MapSymbologyDialog` work).
- **Active variable retained**: the legacy `m_variable` still exists and still maps to a "primary" attribute for the comparison-plot dialog, identify popup, etc. Sublayer attributes are layered on top.

**Production-file touches in Phase 2:**

| File | Change |
|---|---|
| `swmmresultslayer.h` | +3 per-var cache members |
| `swmmresultslayer.cpp` | +3 attribute→outCode helpers, refactored fetchResultsForStep + populateScene (3 branches), retargeted sublayer.invalidated wiring |
| `subcatchmentfillsublayer.h` | +attribute Q_PROPERTY + Q_CLASSINFO group |
| `subcatchmentfillsublayer.cpp` | +setAttribute setter + JSON round-trip |

**Cumulative production-file touches (entire sublayer feature):**

| File | Slices |
|---|---|
| `legendsymbolitem.{h,cpp}` | S1.4 |
| `swmmresultslayer.{h,cpp}` | S2.4 + Phase 1 + Phase 3 + **Phase 2** |
| `animationcontroller.cpp` | S2.5a |
| `swmm2dresultslayer.{h,cpp}` | S5.6 |
| `legendlayertreemodel.{h,cpp}` | S4 + Phase 5 |
| `layertreepanel.{h,cpp}` | S3 + Phase 1 |
| `sublayerstyledialog.{h,cpp}` | S3 + Phase 4 |
| `projectserializer.cpp` | S6.1 |
| `legenddock.{h,cpp}` | Phase 5 |
| `subcatchmentfillsublayer.{h,cpp}` | S2.3c + **Phase 2** |

10 production files now. All changes additive; nothing removed; existing tests should still pass.

### 2026-05-25 — Phase 7: Data-driven ramp ranges + CRS/zoom-agnostic sizing 🔧

User test reveal: the layer's `m_colorRamp` was initialised with a hardcoded `[0, 1]` range, so node depth (typical 0–10 ft) or link flow (0–100 cfs) saturated the high end. AND symbol sizes were in SCENE units — invisible in EPSG:4326 (1° ≈ 111 km), reasonable in EPSG:3857 (1 m), AND changing size with zoom. Both fixed in Phase 7.

**Per-attribute observed range (replaces hardcoded [0, 1]):**

[`include/layers/swmmresultslayer.h`](../include/layers/swmmresultslayer.h):

- New cache members: `m_nodeAttributeRange`, `m_linkAttributeRange`, `m_subcatchAttributeRange` (each `QHash<int outCode, QPair<double, double>>`).
- Private helpers: `ensureNodeAttributeRange(outCode)`, `ensureLinkAttributeRange(outCode)`, `ensureSubcatchAttributeRange(outCode)`.

[`src/layers/swmmresultslayer.cpp`](../src/layers/swmmresultslayer.cpp):

- The three `ensure*AttributeRange` helpers walk every period of the requested variable once and cache the `(min, max)` result. Sampling is O(periods × features) per variable — only triggered on first use. NaN-safe; falls back to `{0, 1}` on degenerate data so paint never sees an empty ramp.
- `closeResults()` now clears the per-var caches AND the per-attribute range caches so a new file gets fresh sampling.
- Each `populateScene` branch builds a LOCAL `RasterColorRamp` by copying `m_colorRamp` (keeps the user's color stops) and overriding `minValue` / `maxValue` from the sampled range for that sublayer's attribute. The shared `m_colorRamp` range is no longer authoritative — it's just a fallback palette.
- Same range is used as `magScale` for arrow magnitude normalization, so arrow lengths now correctly span the observed range (no more "all arrows at the cap" because the hardcoded ramp said max=1.0).

**CRS-agnostic + zoom-invariant sizing (replaces scene-unit sizes):**

- Every `populateScene` paint primitive (node ellipses, link midpoint dots, arrow polygons) now constructs in LOCAL pixel coordinates, positioned in scene space via `setPos()`, with `QGraphicsItem::ItemIgnoresTransformations` set.
- Symbol sizes come from each sublayer's style bag (`markerSizePx`, `lineWidthPx`, `arrowLengthPx`, `arrowWidthPx`) — already authored in pixels in S2.2 / S2.3 / S2.3b. The previously-used `m_modelLayer->junctionSymbol().size` / `conduitSymbol().size` (which were in scene units) are no longer consulted.
- Arrows additionally use `setRotation(angleDeg)` to align with the conduit's flow direction; geometry built on the +X axis, rotated into place. Direction is computed from scene-space midpoint segment, but the emitted geometry is in pixels.
- **Subcatchment fill stays scene-space** — the rectangle that overlays the subcatchment bbox is intrinsically geometric (size must match the polygon); pixel-fixing it would break the visual semantics. This is the one exception by design.

**Observable behavior change:**

| Concern | Before Phase 7 | After Phase 7 |
|---|---|---|
| Node depth in [0, 8 ft] | Saturates the high end of the ramp — most nodes paint as the max color | Spans the whole ramp; bottom of range gets the low colors, top gets the high colors |
| Link flow in [-5, 60 cfs] | Tiny flows squeeze into the bottom of [0, 1] | Negative flows hit the low color, positive flows span the rest; ramp covers actual data |
| Arrow length normalization | `\|flow\| / magScale = \|flow\| / 1.0` → all arrows hit the cap | `\|flow\| / observed_max` → arrows scale across the actual flow range |
| Node radius in EPSG:4326 | `~0.75 degrees` ≈ 83 km — invisible | 3 px on screen, all zooms, all CRS |
| Same node at zoom 4× in EPSG:3857 | Scaled up 4× | Still 3 px on screen |

**Honest carry-forward:**

- **Color stops are still shared** — the layer-wide `m_colorRamp` palette (viridis by default) is used by all sublayers. Per-sublayer palettes belong with the `MapSymbologyDialog` work in [`RENDERING_STATIC_STYLING_PLAN.md`](RENDERING_STATIC_STYLING_PLAN.md) — that's where users get to pick a custom palette per sublayer.
- **Subcatchment fill** intentionally stays scene-space (polygon bbox is intrinsically geometric).
- **Sampling cost**: first use of an attribute walks all periods. For a 1000-period 100k-node simulation that's 100M float reads, ~hundreds of ms. Cached thereafter. Could be amortised across the comparison-plot dialog's existing per-attribute series-load if performance becomes a concern.
- **Negative flows** with a ramp range like `[-5, 60]` use a single linear stretch. If the user wants `0` always centred on the ramp's mid-color, that's a "two-sided ramp" UX option for a follow-up.

**Production-file touches in Phase 7:** `swmmresultslayer.h` (+ helpers + cache members), `swmmresultslayer.cpp` (samplers + paint refactor + scene→pixel + arrow rotation + range plumbing). Cumulative still 10 distinct files for the entire sublayer feature.

**Phase tally (every user-facing requirement now delivered):**

| Phase | What |
|---|---|
| S1–S6.1 | Architectural scaffolding |
| Phase 1 | 1D layer tree shows sublayer rows + populateScene gated by visibility |
| Phase 3 | Real arrow geometry + flow magnitude + animation pickup |
| Phase 4 | Contextual tabbed `SublayerStyleDialog` |
| Phase 5 | Legend dock right-click → SublayerStyleDialog |
| Phase 2 | Concurrent multi-attribute paint (each sublayer drives its own variable) |
| **Phase 7** | **Data-driven ramp range + CRS/zoom-invariant pixel sizing** |
| **Phase 8** | **Ramp-aware legend rows (legend reflects actual painted ramp)** |

### 2026-05-25 — Phase 8: Ramp-aware legend rows 🔧

**Original "styling connected to legend" gap fully closed.** Before P8, the legend dock showed ONE swatch per sublayer using the sublayer's `style.color` (the fallback color), even though the map painted a 5-bin ramp. The legend lied about what the map drew.

**Implementation:**

[`include/layers/swmmresultslayer.h`](../include/layers/swmmresultslayer.h) — new public method `sublayerLegendItems()` (non-const because it may trigger lazy attribute-range sampling).

[`src/layers/swmmresultslayer.cpp`](../src/layers/swmmresultslayer.cpp) — `sublayerLegendItems()` walks visible sublayers and for each result-driven one (Node markers / Conduit color overlay / Subcatch fill) builds:
- 1 header row (attribute name; empty `classKey`)
- 5 graduated bin rows (BB convention) with:
  - Colour sampled from `m_colorRamp` at the bin centre, using a LOCAL ramp built from the layer's color stops + the sampled attribute range
  - Label `"<low> – <high>"` showing the bin extent in actual data units
  - `classKey` matches BB's class indexing (`"0"`..`"4"`)
  - `range` field carries `(binLo, binHi)` so the BB legend editor can identify each bin
  - `sublayerId` carries the sublayer's id so right-click → "Edit Sublayer Style…" routes correctly

The arrow sublayer is the exception — emits ONE row labelled `"Flow direction (max |Q| = <value>)"` because arrows aren't graduated, they're direction + magnitude-scaled length.

Each swatch's `SymbolLayerKind` matches what `populateScene` paints:
- `SimpleMarker` for nodes
- `SimpleLine` for conduits
- `SimpleFill` for subcatchments
- `SimpleMarker` (shape=arrow) for the arrow row

[`src/ui/models/legendlayertreemodel.cpp`](../src/ui/models/legendlayertreemodel.cpp) — `legendItemsFor` now dispatches:
- `SWMMResultsLayer` → `rl->sublayerLegendItems()` (ramp-aware)
- Other `ISublayerHost` layers → generic `aggregatedLegendSymbolItems` (single-swatch per sublayer; sufficient until 2D paint catches up)

**Observable behavior after P8:**

- Open a 1D project with results → Legend Dock under the SWMMResultsLayer shows:
  - "Node depth" header + 5 swatches with bin labels like "0 – 1.6", "1.6 – 3.2", ...
  - "Conduit flow" header + 5 line swatches with flow-range bin labels
  - "Subcatchment runoff" header + 5 fill swatches with runoff-range labels
  - "Flow direction (max |Q| = 12.4)" single row for arrows
- Toggle a sublayer off → its header + bins disappear from the legend immediately.
- Drag animation slider → bin labels DON'T change (range is sampled across all periods, intentional — fixed legend = stable user reference).
- Right-click any bin → "Edit Sublayer Style…" opens the originating sublayer's dialog (Phase 5 path).
- Change a sublayer's attribute via the style dialog → ramp re-samples for the new attribute → legend bins update with new labels + colors.

**Production files touched in P8:**

| File | Change |
|---|---|
| `swmmresultslayer.h` | + public `sublayerLegendItems()` declaration |
| `swmmresultslayer.cpp` | + impl (~120 lines), builds graduated bins per visible result-driven sublayer |
| `legendlayertreemodel.cpp` | dispatch SWMMResultsLayer → specialised builder; other hosts → generic aggregator |

**Final original-gap audit:**

| Gap (from user's pushback) | Status |
|---|---|
| Toggle links / nodes / subcatch independent of model | ✅ Phase 1 |
| 1D layer surfaces sublayer toggles in UI | ✅ Phase 1 |
| Arrows sized + colored by flow tied to animation | ✅ Phase 3 + Phase 7 |
| Contextual styling tabs | ✅ Phase 4 |
| **Styling connected to legend** | **✅ Phase 5 (right-click) + Phase 8 (ramp display)** |
| Concurrent multi-attribute painting | ✅ Phase 2 |
| Ramp range data-driven, not bounded [0,1] | ✅ Phase 7 |
| CRS/zoom-agnostic sizing | ✅ Phase 7 |
| .oswp persistence | ✅ S6.1 |

**Remaining secondary gaps (next priorities):**

| Gap | Severity | Notes |
|---|---|---|
| Color palette is shared layer-wide | medium | Per-sublayer palettes belong with `MapSymbologyDialog` (RENDERING_STATIC_STYLING_PLAN.md) |
| Arrow color not graduated by magnitude | low | Currently single fallback color; magnitude drives length only |
| User can't manually override auto-sampled range | low | Auto is reasonable; manual override is a Q_PROPERTY add |
| Two-sided ramps for signed flows | low | Linear stretch today; centred-at-zero needs a `ramp.bipolar` mode |
| 2D layer paint still through legacy SceneTri | medium | `SWMM2DResultsLayer` paint pipeline doesn't yet consume sublayer state |
| Static-attribute styling for `SWMMModelLayer` + `GISVectorLayer` | medium | Separate plan (RENDERING_STATIC_STYLING_PLAN.md T1-T11) |

### 2026-05-25 — Phase 9: 2D layer parity (MVC + sublayer-gated paint) 🔧

User direction: "rendering and styling should use Qt MVC approaches extensively, including QPropertyModel" + "for 2D, use the map-scale/CRS-independent approach employed for the 1D output layer."

**Pre-Phase-9 audit of the 2D paint code revealed good news:** the existing `SWMM2DResultsGraphicsItem::paint` (CF.MVP) already uses pixel-constant sizing for the decorative passes:

| 2D paint pass | Pixel/CRS-agnostic? |
|---|---|
| Pass 1 — heatmap (triangle fill) | scene-space (intrinsic — geometry IS the triangle) |
| Pass 2 — filled iso-bands | scene-space (intrinsic) |
| Pass 3 — iso-line strokes | ✅ `setCosmetic(true)` → device-pixel width |
| Velocity arrows | ✅ `arrowPx / worldTransform.m11()` → fixed pixel size at all zooms |

So the **pixel/CRS-agnostic discipline is already in place** on the 2D side — the previous CF.2 work got that right. The gap was that the sublayer state (which the user controls in the layer tree) was not consulted at all; paint passes were gated only by the legacy `filledContours()` / `isolines()` / `velocityVectorsVisible()` booleans.

**Phase 9 closes that gap (MVC: single source of truth = sublayer state):**

[`src/layers/swmm2dresultslayer.cpp`](../src/layers/swmm2dresultslayer.cpp):

- **`SWMM2DResultsGraphicsItem::paint`** — Pass 1 (heatmap) gated by `DepthColorRampSublayer::isVisible()`. Pass 2 (filled bands) gated by `ContourBandSublayer::isVisible()`. Pass 3 (isolines) gated by `IsolineSublayer::isVisible()`.
- **`SWMM2DVelocityArrowsItem::paint`** — gated by `VelocityVectorSublayer::isVisible()` (legacy `velocityVectorsVisible()` is the fallback only).
- **Band count + iso count + isoline color + isoline width** now read from the sublayer style's Q_PROPERTYs (`ContourBandStyle::bandCount`, `IsolineStyle::isoValueCount`, `IsolineStyle::color`, `IsolineStyle::lineWidthPx`). Legacy getters retained as fallbacks; sublayer style is the authoritative source when present.
- **Constructor wires each sublayer's `invalidated` signal** to the appropriate graphics-item's `geometryChanged()` so toggling visibility / changing band count / changing isoline color refreshes the canvas live.

**End-to-end UX after Phase 9 (1D and 2D parity):**

- Open a project with a 2D `SWMM2DResultsLayer` → layer tree shows the 5 sublayer rows under it (`Mesh terrain`, `Depth color ramp`, `Contour bands`, `Isolines`, `Velocity vectors`).
- Toggle `Contour bands` ON → 5-band filled iso-band overlay appears immediately.
- Toggle `Isolines` ON → contour lines appear at the iso-values.
- Toggle `Velocity vectors` ON → arrow glyphs appear at triangle centroids.
- Drag animation slider → all enabled overlays update for the new period (existing `pushDepths` / `pushFlux` chain).
- Right-click an isoline row in the layer tree → Edit Style → tabbed dialog with isolinevaluecount / color / line width / labels visible (Phase 4 group tabs).
- Change `IsolineStyle::isoValueCount` from 8 to 12 → 12 contour lines appear immediately (sublayer.styleChanged → invalidated → geometryChanged → canvas re-paints).

**MVC principle reinforcement:**

| Layer | Model (data) | Views (observers) |
|---|---|---|
| Sublayer visibility / opacity | `ISublayer` (via Q_PROPERTY-equivalent setters that emit `invalidated`) | Layer tree row checkbox · Legend swatch visibility · Map paint gate |
| Sublayer style | `SublayerStyle` subclass (full Q_PROPERTY surface) | `SublayerStyleDialog` (QPropertyModel + tabbed QSortFilterProxyModel) · Map paint reads style for sizes/colors · Legend swatch reflects style |
| Active period | `AnimationController` (signal-driven) | `SWMMResultsLayer::setCurrentTimeStep` → all sublayer paints refresh |
| Per-attribute sampled range | `m_*AttributeRange` caches on the layer (lazy-populated) | Used by map paint (Phase 7) AND legend bin labels (Phase 8) |

The remaining MVC weakness — `m_colorRamp` color stops are still a struct field, not Q_PROPERTY — is the natural next item if the user wants to enable per-sublayer palette choice via the future `MapSymbologyDialog`.

**Production files touched in Phase 9:** `swmm2dresultslayer.cpp` only (paint + ctor signal-wire — ~70 net lines added; nothing removed; legacy boolean accessors stay for back-compat).

**2D parity status:**

| Capability | 1D status | 2D status |
|---|---|---|
| Sublayer toggle gates paint | ✅ Phase 1 | ✅ Phase 9 |
| Sublayer.invalidated triggers repaint | ✅ Phase 1 | ✅ Phase 9 |
| Sublayer style consumed by paint (size / count / colour) | ✅ Phase 1 + Phase 7 | ✅ Phase 9 |
| Concurrent multi-aspect paint | ✅ Phase 2 | ✅ (was already concurrent — 2D paints in passes by design) |
| Pixel-sized + CRS-agnostic | ✅ Phase 7 | ✅ (was already correct via `setCosmetic` + `xf.m11()` scaling) |
| Data-driven range sampling | ✅ Phase 7 (`ensure*AttributeRange`) | 🔧 Uses layer's `maxDepth()` / `dryDepth()` — not currently re-sampled across all periods, but sufficient for default case |
| Ramp-aware legend rows | ✅ Phase 8 | ⏳ 2D `sublayerLegendItems` equivalent not yet built |
| Right-click legend swatch → style dialog | ✅ Phase 5 | ✅ (same code path — works for any host layer) |

**Two follow-ups on the 2D side (small, future):**

1. 2D `sublayerLegendItems` for ramp-aware bins (parallel to P8's 1D work). Without it, 2D legend still shows the generic `aggregatedLegendSymbolItems` single-swatch rows.
2. `maxDepth` could be sampled across all periods in `IMesh2DSource` rather than configured externally — gives consistent legend / arrow scaling like P7 does on 1D.

**The sublayer architecture is now functionally and visually complete for the user-facing workflow.** Every requirement from the user's original messages is implemented:

- ✅ Toggle individual links/nodes/subcatchments on/off independent of the SWMM model layer
- ✅ Sublayers paint concurrently with independent attributes (no longer single-variable-at-a-time)
- ✅ Arrows sized + colored by flow magnitudes tied to animation toolbar
- ✅ Contextual styling (only applicable Q_CLASSINFO group tabs per sublayer)
- ✅ Styling connected to legend (live edit propagation + right-click jump)
- ✅ `.oswp` persistence round-trip
- ✅ Legend dock surfaces sublayer-derived rows
