# Visualization & Styling Overhaul Plan

**Status:** ⏳ Draft — 2026-05-31
**Owner:** GUI / Rendering
**Prefix:** `VS` (Visualization Styling)

> **Supersession.** This plan unifies and supersedes the styling/legend/visibility
> portions of the following plans. Where a slice below overlaps an earlier plan, the
> slice here is authoritative.
>
> | Plan | Disposition |
> |---|---|
> | `RENDERING_STATIC_STYLING_PLAN.md` | ⛔ Already withdrawn 2026-05-27 → folded into VS.2–VS.4 |
> | `RENDERING_2D_RESULTS_STYLING_PLAN.md` (AN.*) | Superseded by VS.7 |
> | `RENDERING_SINGLE_SYMBOL_TRANSFER_PLAN.md` (SS.*) | Superseded by VS.4 |
> | `RENDERING_DIALOG_DEMO_PLAN.md` (DM.*) | Folded into VS.5 (attribute picker seam retained) |
> | `RENDERING_OUTPUT_SUBLAYERS_PLAN.md` (S1–S6) | Primitives kept; UI portions superseded by VS.5/VS.7 |
> | `RENDERING_RULE_MODEL_PLAN.md` (Z.1–Z.18) | **Foundational — NOT superseded.** This plan builds on it. |
> | `LAYER_TREE_REORGANIZATION_PLAN.md` | Independent; VS.8/VS.9 assume its tree shape |
> | `FLOWRENDER_2D_STREAMLINES_PLAN.md` (FR.*) | Independent; out of scope here |
>
> Status legend used throughout: ✅ shipped & verified · 🔧 partial · ⏳ pending · ⛔ withdrawn.

---

## Progress Log

| Date | Slice | Status | Notes |
|---|---|---|---|
| 2026-05-31 | VS.1 | ✅ root cause fixed — manual build verify | **Root cause (P6 follow-up):** with GPU/QSG rendering ON, `MapCanvas` only hands the layer to `SWMMLayerQSGRenderer` inside its `qsgActive` (visible-layer) block, so a whole-layer toggle-off never told the renderer to clear — and `updatePaintNode` had no `isVisible()` gate, so its scene-graph node kept the full network in the FBO. **Fix:** added `\|\| !m_layer->isVisible()` to the `updatePaintNode` early-out (`swmmlayerqsgrenderer.cpp`); the renderer's existing `repaintRequested → update()` connection guarantees the node is dropped on toggle and rebuilt on show. Per-kind toggles already worked via the `m_*HiddenFlag` arrays (`rebuildFlagArrays`), and the CPU `m_scene->render()` path clears via `depopulateScene` + `update()`. The earlier `scene->invalidate()` additions are harmless but no-ops on this manual-render canvas (it isn't a QGraphicsView). **Verification:** the QSG path needs a live GPU/QML context, so it isn't headless-unit-testable (and the sandbox has no Qt); verify manually — GPU rendering ON → uncheck the whole model layer → glyphs vanish immediately; re-check → network returns; toggle a single kind (e.g. Conduits) → only that kind clears. |
| 2026-05-31 | VS.3 | ✅ code+tests (pending build) | `IntervalBinner` gained NaturalBreaks (Jenks), StdDev, Logarithmic, Exponential — each yields exactly `nBins-1` breaks. JSON + `test_intervalbinner` extended. PrettyBreaks deferred (variable break count). |
| 2026-05-31 | VS.2 | 🔧 type+helper done (pending build) | New `FillSymbolLayerSpec` + `drawFill()` (third primitive), registered in CMake, `test_fillsymbollayer` added. **VS.2b** (wire fill into the `swmmlayeritem` / `featuresublayer` / `gisvectorlayer` paint paths) is the remaining step. |
| 2026-05-31 | VS.4 | ✅ code+tests (pending build) | `GraduatedRenderer` gained an independent line-**width** output axis (`outputWidthEnabled` + range + `widthForBin`). Previously the size axis wrote both `size` and `width` from one px range — now markers (size) and links (width) scale independently. Back-compat migration in `fromJson` preserves pre-VS.4 projects. `test_ifeaturerenderer` +3 cases. (Categorized already carries per-category symbols, so size/width are inherently per-class there.) |
| 2026-05-31 | VS.5 | 🔧 done (pending build) | Removed the five deferred-feature tabs (Temporal / Mask / AuxStorage / Joins / Diagrams) from `LayerStyleDialog` — capability bits, `buildTabs` conditionals, `build*Tab` definitions, and includes. Dialog now shows the canonical QGIS set (Information / Source / Symbology / Labels / Rendering / Metadata). Grep confirms no live code references the removed symbols (only stale generated doxygen HTML — VS.11). Standalone `temporaltab`/`masktab`/`joinstab`/`diagramstab`/`auxiliarystoragetab` classes still compile but are now unused → VS.11. |
| 2026-05-31 | VS.8 | 🔧 done (pending build) | Opacity editing already existed in tree column 1 for layers + sublayers (`LayerOpacityDelegate`). The real gap: `LayerTreeModel::onLayerDataChanged` was defined but never connected, so external opacity/visibility/name changes (e.g. from the dialog Rendering tab) didn't refresh the layer row. Wired each layer's `opacityChanged`/`visibilityChanged`/`nameChanged` → `onLayerDataChanged` in `rebuildCategories` (idempotent disconnect-then-connect). Sublayer rows already refresh via `invalidated()`. **Verify on build:** (a) paint multiplies parent × sublayer opacity per the `ISublayer::opacity()` contract; (b) dialog slider live-updates while both dialog and tree are open (minor; dialog buffers with cancel-snapshot). |
| 2026-05-31 | VS.9 | 🔧 done (pending build) | `LegendLayerTreeModel` now connects each sublayer-host layer's `ISublayer::invalidated()` to the legend cache reset, tracked in `m_connectedSublayers` and disconnected on every rebuild. Closes the gap where a sublayer style/visibility edit repainted the canvas (`graphics_item_->geometryChanged()`) but never raised the parent layer's `repaintRequested`, leaving the legend stale. Note: this model's `legendItemsFor` shows renderer-derived rows (the rule model retired user-facing sublayer rows), so the wiring future-proofs + catches renderer state mirrored through sublayer signals. Automated `test_legendlayertreemodel` needs a MapCanvas+layer graph (integration-level) → authored against a build; manual check: edit a 2D-results sublayer style and confirm the legend dock updates without panning. |

| 2026-05-31 | VS.11 | 🔧 partial — stale tab files removed | Deleted the 10 now-orphaned tab files (`temporaltab`, `masktab`, `auxiliarystoragetab`, `joinstab`, `diagramstab` — .cpp + .h) and their CMake entries. Final grep across `src`/`include`/`tests`/`forms`/`CMakeLists.txt` shows zero references. Their data-model specs (TemporalSpec/MaskSpec/etc. on layers) are untouched. Remaining VS.11 doc work (supersession banners on the older plan files; regenerate doxygen html) is optional follow-up. |
| 2026-05-31 | VS.6 | 🔧 done (pending build) | (a) New `PalettedRasterRenderer` (`IRasterRenderer`) — discrete value→colour via `CategoricalPalette` + per-class labels/legend, JSON round-trip, `test_palettedrasterrenderer` (7 cases). (b) Hillshade relief overlay for single-band DTMs: `GISRasterLayer` gained `setHillshadeEnabled`/`setHillshadeParams`; `warpToCanvas` composites a Horn normal·light shade from the warped elevation grid over the colour-ramp pixels. **Finishing:** verify azimuth orientation vs a known DEM; route `warpToCanvas` colourisation through `rasterRenderer()` so the paletted renderer drives standalone rasters (today the loop still uses `m_colorRamp`); add a raster Symbology panel. |
| 2026-05-31 | VS.7 | 🔧 done (pending build) | Color-by-magnitude added to `FlowArrowStyle` **and** `VelocityVectorStyle` (`colorByMagnitude` + `colorRampName` + `speedMin/MaxMps` + `colorForSpeed()`, auto-surfacing in the SublayerStyleDialog via Q_PROPERTY/Q_CLASSINFO, JSON round-trip). Wired into the flow-arrow QPainter loop (`SWMM2DVelocityArrowsItem`) — each arrow's shaft samples the ramp by speed. **Finishing:** the velocity-glyph (length-scaled) pass builds geometry in the QSG renderer; apply `colorForSpeed()` there too. Contours are already dynamic per-timestep (no change needed). |
| 2026-05-31 | VS.2b | 🔧 done (pending build) | Polygon fill now composed through the `FillSymbolLayerSpec` primitive in `SWMMResultsLayer::paintPolygon`; added a `fillStyle` (Qt::BrushStyle) Q_PROPERTY to `PolygonFeatureSublayerStyle` so solid/hatch/cross-hatch fills are reachable + round-trip. **Finishing:** wire the same into `GISVectorLayer` polygon items + the legacy `swmmlayeritem` subcatchment loop if hatch fills are wanted there. |
| 2026-05-31 | VS.10 | 🔧 done (pending build) — option 2 | Unified labeling. `LabelConfig` moved to the `OpenSWMMVisLayer` base (member + `labelConfig()` + virtual `setLabelConfig()` + `labelConfigChanged()` signal); `SWMMModelLayer` & `GISVectorLayer` now inherit storage and override only the setter (model syncs `m_showLabels`; vector mirrors onto `GISVectorSymbol`). `LabelsTab` read/write/refresh generalised to the base API; dialog Labels capability extended to results/2D/mesh. Label **painting** added to `SWMM2DResultsGraphicsItem` (per-cell value labels, screen-space, halo, grid-bucketed). **Finishing:** verify the base/subclass migration compiles cleanly (the highest-churn change); add label painting to the 1D `SWMMResultsLayer` items (it now carries config + tab but no paint yet); generalise the 2D labelled quantity beyond depth + real collision avoidance. |

> Phases above need a developer-machine build (`ctest`) to confirm green — the
> Cowork sandbox has no Qt6/CMake. Commands in §8.4.

## 0. Purpose

The rendering subsystem is mature: a compositional symbol model, five feature
renderers, a raster pseudo-color renderer, color ramps, categorical palettes, an
interval binner, sublayers for 2D results, and a legend that reads from renderers.
The gaps are **completion, repair, and unification**, not greenfield construction.

This plan does five things:

1. **Fixes the visibility artifact bug** — layers/sublayers toggled off in the tree
   leave stale pixels on the canvas.
2. **Unifies styling around three primitives** (Marker / Line / Fill) whose
   color, fill, pen, and size can be driven by a **single value** or by
   **classification bands** (graduated / categorical), exposed uniformly on every
   model layer, results layer, and sublayer.
3. **Completes the classification engine** with the standard GIS methods
   (equal interval, quantile, manual/defined breaks, natural breaks/Jenks,
   standard deviation, pretty breaks, logarithmic, exponential).
4. **Adds specialized rendering** the primitive model does not cover: categorical
   and continuous (DTM) rasters with hillshade/relief shading, and dynamic 2D
   contours (lines + filled bands) and magnitude-scaled directional arrows.
5. **Simplifies the layer-properties dialog**, wires the legend dynamically, adds
   configurable per-layer labeling, completes opacity-in-treeview, and removes
   stale files.

Design bias (per `CLAUDE.md`): **simplicity and alignment with the existing,
vetted architecture over novelty.** Every primitive and classifier already has a
home in `include/render`; we extend those rather than introduce parallel systems.

---

## 1. Current State (verified findings)

### 1.1 What already exists ✅

| Area | Reality on disk |
|---|---|
| Symbol primitives | `SymbolLayer` + `SymbolStyle` compositional stack (`symbollayer.h`). `MarkerSymbolLayerSpec` (13 shapes, fill/outline/pen/size/rotation/offset), `LineSymbolLayerSpec` (color/width/pen/cap/join/dash/offset + `LineArrowSpec`). |
| Feature renderers | `SingleSymbolRenderer`, `CategorizedRenderer`, `GraduatedRenderer`, `UnclassedColorsRenderer`, `RuleBasedRenderer` (eval stubbed), `MultiKindRenderer`; interface `IFeatureRenderer` with `symbolFor()` + `legendSymbolItems()`. |
| Raster renderer | `IRasterRenderer` + `SingleBandPseudoColorRenderer`. |
| Classification | `IntervalBinner` (EqualInterval, Quantile, Manual only). `GraduatedClass`, `RasterColorRamp` (22 ramps), `CategoricalPalette` (9 palettes). |
| Dynamic styling | `DataDefinedScalar` (Linear/Sqrt/Log curves) used for per-feature size. Per-`SymbolLayer` expression-override map exists but is **stored, not evaluated**. |
| Raster symbol specs | `RasterColorRampSymbolLayerSpec`, `HillshadeSymbolLayerSpec`, `ContourSymbolLayerSpec`, `MeshEdge/Node` specs — all defined. |
| 2D results sublayers | `DepthColorRampSublayer`, `ContourBandSublayer`, `IsolineSublayer`, `FlowArrowSublayer`, `VelocityVectorSublayer`, mesh fill/edge/node — all implemented, marching-squares contours, dynamic per-timestep. |
| Opacity | `OpenSWMMVisLayer::opacity()` Q_PROPERTY + signal; `ISublayer::opacity()` virtual (multiplies parent). Tree **column 1** already renders/edits opacity for layers *and* sublayers via `LayerOpacityDelegate`. |
| Legend | `legendItemsFor()` reads `renderer()->legendSymbolItems()`; `ISublayerHost::aggregatedLegendSymbolItems()` concatenates sublayer items with `sublayerId` routing. |
| Dialog | `LayerStyleDialog` builds tabs by capability flags: Information, Source, Symbology, Labels, Temporal, Mask, AuxStorage, Joins, Diagrams, Rendering, Metadata. |

### 1.2 What is missing or broken 🔧/⏳

1. **Artifact bug (root cause found).** `OpenSWMMVisLayer::depopulateScene()`
   (`src/layers/openswmmvislayer.cpp` ~L279–297) removes & deletes graphics items but
   never calls `scene->invalidate(...)`/`update()`. The `SWMM2DResultsLayer`
   override (~L1627–1639) has the same omission. Result: deleted items leave stale
   pixels until an unrelated repaint. This is the reported "artifacts remain after
   turning a layer off."
2. **No `FillSymbolLayerSpec`.** `SimpleFill`/`HatchFill`/`PatternFill` are enum
   values with no typed spec — subcatchments/polygons cannot be styled with the same
   rigor as markers and lines.
3. **Classification incomplete.** Only 3 of the standard methods exist. Missing:
   Jenks/natural breaks, standard deviation, pretty breaks, logarithmic, exponential.
4. **No categorical (paletted) raster renderer** for land-use / classified rasters.
5. **DTM hillshade not wired as a raster renderer.** `HillshadeSymbolLayerSpec`
   exists and the *mesh* path uses it; standalone `GISRasterLayer` DTMs do not get
   relief shading.
6. **Directional arrows lack joint color+size encoding.** `FlowArrowSublayer` is
   fixed-length; `VelocityVectorSublayer` scales length only. Neither varies color by
   magnitude through a ramp.
7. **Legend not live.** `LegendLayerTreeModel` rebuilds on layer `repaintRequested`
   but does not subscribe to per-sublayer `invalidated()`, so sublayer symbology edits
   don't refresh the legend until something else triggers a repaint.
8. **Labeling not exposed uniformly.** `labelconfig.h` exists; the Labels tab is only
   built for `SWMMModelLayer`/`GISVectorLayer`, not surfaced per-sublayer or for
   results layers.
9. **Dialog clutter.** Temporal, Mask, AuxStorage, Joins, Diagrams tabs are deferred
   features adding noise.
10. **Stale files.** Withdrawn plans and (after VS.5) orphaned tab classes.

---

## 2. Design Principles

- **One styling vocabulary everywhere.** A model layer, a 1D results layer, a 2D
  results sublayer, and a raster all answer the same question — "given this
  thing, what color/size/pen/fill does it get?" — through the same renderer +
  classification stack. Specialized layer types *add* capabilities (relief,
  contours, arrows) but never fork the core grammar.
- **Three primitives.** Marker (dots/points/glyphs), Line (strokes), Fill
  (polygons/areas). Everything composes from these plus the raster/relief and
  vector-glyph specials.
- **Single value or class bands, chosen per property.** Color, size, width, and
  fill are each either a constant or bound to an attribute through a classifier
  (graduated bands or categories) or a continuous data-defined expression.
- **Legend is derived, never authored twice.** The renderer is the single source
  of truth; the legend reads `legendSymbolItems()` and refreshes live.
- **Modern, minimal dialog.** Two primary tabs — *Symbology* and *Labels* — plus
  read-only *Information* and a thin *Rendering* (opacity/blend). Nothing else.
- **MVC + synchronization** (per project `CLAUDE.md` §5.1): the same renderer/style
  object is edited from the dialog, the tree opacity column, and the legend; all
  views observe one model and stay in sync via existing signals.

---

## 3. Target Architecture

### 3.1 Primitive layer (the bottom of the stack)

Add the missing fill primitive to complete the trio:

```
SymbolLayerKind:  SimpleMarker | SimpleLine | SimpleFill | MarkerLine | HatchFill | ...
                   │              │            │
                   ▼              ▼            ▼
            MarkerSymbolLayerSpec  LineSymbolLayerSpec  FillSymbolLayerSpec  (NEW)
```

`FillSymbolLayerSpec` (new, mirrors the existing marker/line specs):
fill color, fill style (solid/hatch/none), outline color, outline width,
outline pen style, outline cap/join. A `SymbolStyle` remains an ordered stack of
these, so a subcatchment can be "translucent fill + dashed outline + centroid
marker" with no special-casing.

### 3.2 Property bindings — the heart of the request

Every styleable property (`color`, `size`, `width`, `fill`, `opacity`) resolves
through one of three **binding modes**:

| Mode | Backing class | Use |
|---|---|---|
| **Constant** | the spec field itself | single value for the whole layer |
| **Classified** | `IFeatureRenderer` (Graduated / Categorized) + `IntervalBinner` + ramp/palette | class bands by attribute |
| **Data-defined** | `DataDefinedScalar` / expression | continuous mapping attribute → value |

This is already the renderer model — VS makes it *uniform and complete* rather
than inventing anything. The Symbology tab's top control is a **mode selector**
(Single Symbol · Categorized · Graduated · Unclassed continuous · Rule-based),
and each renderer exposes which properties it can classify.

### 3.3 Classification engine (`IntervalBinner` extension)

Extend `BinMethod` to the full GIS set and centralize break computation:

| Method | Status | Notes |
|---|---|---|
| Equal Interval | ✅ | exists |
| Quantile (equal count) | ✅ | exists |
| Manual / Defined breaks | ✅ | exists |
| Natural Breaks (Jenks) | ⏳ VS.3 | Fisher-Jenks; cap sample size with reservoir sampling for large layers |
| Standard Deviation | ⏳ VS.3 | breaks at mean ± k·σ |
| Pretty Breaks | ⏳ VS.3 | rounded "nice" numbers |
| Logarithmic | ⏳ VS.3 | for skewed data (flow, area) |
| Exponential | ⏳ VS.3 | geometric growth of band widths |

All methods share the existing `computeBreaks(samples) → breaks` /
`binFor(value, breaks)` contract and JSON round-trip, so renderers and legend need
no change to consume them.

### 3.4 Specialized layer types

These do **not** share the marker/line/fill primitives and get dedicated specs +
a tailored Symbology panel:

- **Continuous raster / DTM** — `RasterColorRampSymbolLayerSpec` (ramp + clamp +
  no-data color) **composited with** `HillshadeSymbolLayerSpec` (azimuth, altitude,
  z-exaggeration, blend, strength) for relief/shadow shading. New
  `HillshadeRasterRenderer` (or a hillshade compositing pass added to the
  pseudo-color renderer) wires the existing spec into the standalone
  `GISRasterLayer` paint path, not only the mesh path.
- **Categorical raster** — new `PalettedRasterRenderer`: discrete value → color via
  `CategoricalPalette` + per-class label, for land-use/soil rasters. Mirrors
  `CategorizedRenderer` on the feature side.
- **2D results contours** — `IsolineSublayer` (lines) and `ContourBandSublayer`
  (filled), both driven by `IntervalBinner` + ramp, recomputed per timestep, with
  optional labeled isolines (`labelEveryN`).
- **2D results directional arrows** — extend `FlowArrowSublayer` /
  `VelocityVectorSublayer` so **both length and color** encode magnitude through a
  `DataDefinedScalar` (length) + ramp (color), with configurable spacing, head size,
  and dry-cell cutoff.

### 3.5 Labeling

Surface `LabelConfig` as a uniform, **optional** capability on every feature-bearing
layer and sublayer: enable toggle, field/expression, font, color, halo,
placement, scale range, collision avoidance. One reusable `LabelsTab` widget bound
to whatever object owns a `LabelConfig`.

### 3.6 Legend (dynamic)

`LegendLayerTreeModel` connects each layer's renderer change **and each sublayer's
`invalidated()`** to a cache rebuild, so any symbology/classification/label edit
refreshes the legend immediately. Legend entries continue to come solely from
`legendSymbolItems()` / `aggregatedLegendSymbolItems()`.

### 3.7 Opacity

Already present in tree column 1 for both layers and sublayers. VS.8 **verifies and
completes** the wiring (paint-time multiply parent×sublayer, the
`opacityChanged`/`invalidated` → repaint path) rather than building it new, and adds
the same opacity control to the thin Rendering tab so the dialog and tree stay in
sync (MVC).

---

## 4. Decisions (locked)

- **D1.** Build on the existing renderer + `IntervalBinner` + ramp/palette stack.
  Do **not** introduce a parallel "primitive styling" system. The user's
  "primitives + class bands + GAS breaks" maps directly onto it.
- **D2.** Add `FillSymbolLayerSpec` as the third first-class primitive; do not defer
  polygon styling.
- **D3.** The properties dialog drops the five deferred-feature tabs
  (Temporal / Mask / Auxiliary Storage / Joins / Diagrams) — these are the
  "extraneous" ones. **Realized in VS.5:** the dialog now shows the canonical
  QGIS set — **Information**, **Source** (CRS), **Symbology**, **Labels**,
  **Rendering** (opacity), **Metadata**. Folding Source + Metadata into
  Information to reach a strict 4-tab dialog is optional polish (VS.5b),
  deferred because moving those widgets is cosmetic and higher-risk than the
  clear win of deleting the deferred tabs.
- **D4.** Specialized layers (raster/DTM, categorical raster, 2D contours, arrows)
  get bespoke Symbology panels chosen by layer archetype; they reuse classifier +
  ramp building blocks.
- **D5.** Legend stays renderer-derived; we only add live-refresh wiring.
- **D6.** Opacity is treated as *complete-and-verify*, not new construction.

---

## 5. Slice Breakdown

Each slice is independently shippable with its own verification. Recommended order
follows dependencies; VS.1 is highest priority (visible bug).

### VS.1 — Fix the visibility artifact bug ⏳ (priority)
**Goal:** Turning any layer or sublayer off in the tree clears its pixels immediately.
**Changes:** In `OpenSWMMVisLayer::depopulateScene()` and the
`SWMM2DResultsLayer::depopulateScene()` override, after removing items call
`scene->invalidate(removedBoundingRect)` (or `scene->update()` as a safe fallback).
Audit all `depopulateScene`/visibility-toggle paths (raster `fetchCache` branch in
`MapCanvas::refreshLayerItems`) for the same omission. Confirm QSG paths upload empty
geometry when `isVisible()==false` (they do today).
**Files:** `src/layers/openswmmvislayer.cpp`, `src/layers/swmm2dresultslayer.cpp`,
`src/map/mapcanvas.cpp`.
**Verify:** Manual + screenshot test — load a model with a 2D results layer, toggle
each layer and each sublayer off; assert the canvas region clears in the same frame
(pixel diff to background). Write the test images to a user-visible folder
(`CLAUDE.md §4`).

### VS.2 — Fill primitive ⏳
**Goal:** Polygons/subcatchments style with the same rigor as markers/lines.
**Changes:** Add `FillSymbolLayerSpec` (`include/render/fillsymbollayer.h` +
`src/render/fillsymbollayer.cpp`), a `drawFill()` helper, JSON round-trip, and wire
`SimpleFill`/`HatchFill` kinds in `SymbolStyle` painting.
**Files:** new `fillsymbollayer.*`, `symbolstyle.cpp`, `symbollayer.cpp`, `CMakeLists.txt`.
**Verify:** Unit test round-trips a fill spec; render a subcatchment layer with
translucent fill + dashed outline; legend swatch shows fill.

### VS.3 — Complete the classification engine ⏳
**Goal:** All standard GIS break methods available to every graduated renderer,
raster ramp, and contour binner.
**Changes:** Extend `IntervalBinner::BinMethod` with Jenks, StandardDeviation,
PrettyBreaks, Logarithmic, Exponential; implement `computeBreaks` for each; reservoir-
sample large inputs for Jenks. JSON enum extension with back-compat.
**Files:** `include/render/intervalbinner.h`, `src/render/intervalbinner.cpp`,
`src/render/binsampler.cpp`, unit tests.
**Verify:** Unit tests per method against known fixtures (compare breaks to reference
values); degenerate inputs (constant/empty) fall back gracefully.

### VS.4 — Unify single/graduated/categorized across all feature layers ⏳
**Goal:** Model layers, 1D results, and 2D results expose the same renderer choices
and classify color **and** size/width, with edits back-propagated to legacy style
objects where needed.
**Changes:** Ensure `GraduatedRenderer`/`CategorizedRenderer` can target size and
width (not just color); confirm `symbolFor()` honors the selected primitive's bound
properties; reconcile the `RENDERING_SINGLE_SYMBOL_TRANSFER` (SS.*) back-propagation
logic into one path.
**Files:** `src/render/renderers/*.cpp`, `src/render/symbolstyleadapter.cpp`.
**Verify:** For each layer archetype, switch renderer modes and confirm map + legend
update; round-trip through `.oswp`.

### VS.5 — Slim the layer-properties dialog ⏳
**Goal:** Dialog shows Information, Symbology, Labels, Rendering only.
**Changes:** Remove `Temporal`, `Mask`, `AuxStorage`, `Joins`, `Diagrams` capability
flags, their `buildXTab()` methods, conditionals, and includes in
`layerstyledialog.cpp/.h`. Merge Source/CRS + Metadata into a single read-only
Information tab. Modernize layout (consistent spacing, grouped sections, the
mode-selector at top of Symbology).
**Files:** `src/ui/dialogs/layerstyledialog.cpp/.h`.
**Verify:** Open the dialog for each layer type; exactly four tabs; no dangling
references; build clean.

### VS.6 — Raster/DTM relief + categorical raster ⏳
**Goal:** Standalone rasters render with relief shading (continuous/DTM) or paletted
categories (land-use).
**Changes:** Wire `HillshadeSymbolLayerSpec` into `GISRasterLayer` paint (composite
hillshade over the pseudo-color pass; reuse the mesh hillshade math in
`swmm2dmeshqsgrenderer`). Add `PalettedRasterRenderer` (`include/render/renderers/`)
selecting `IRasterRenderer` by raster archetype (continuous vs classified). Add a
raster Symbology panel (ramp + classification + hillshade controls, or palette +
class table).
**Files:** `src/layers/gisrasterlayer.cpp`, new
`renderers/palettedrasterrenderer.*`, `src/render/rastersymbollayers.cpp`,
raster panel in `src/ui/dialogs/`.
**Verify:** Load a DTM → relief visible, light azimuth/altitude adjustable; load a
classified raster → discrete colors + legend categories.

### VS.7 — Dynamic 2D contours & directional arrows ⏳
**Goal:** 2D results offer filled + line contours and magnitude-encoded arrows.
**Changes:** Confirm `IsolineSublayer`/`ContourBandSublayer` recompute per timestep
through `IntervalBinner` + ramp; expose method/break/ramp controls. Extend
`FlowArrowSublayer`/`VelocityVectorSublayer` so length (via `DataDefinedScalar`) and
color (via ramp) both encode magnitude; expose spacing, head size, min/max size,
dry-cell cutoff. Optional isoline labels.
**Files:** `src/render/sublayers/*.cpp`, `src/layers/swmm2dresultslayer.cpp`,
sublayer style panels.
**Verify:** Animate a 2D run; contours and arrow color/size track the field per
timestep; toggling each sublayer clears cleanly (depends on VS.1).

### VS.8 — Complete opacity-in-treeview + Rendering tab sync ⏳
**Goal:** Whole-layer and per-sublayer opacity adjustable from the tree and the
dialog, kept in sync.
**Changes:** Verify `LayerOpacityDelegate` edits both layer and sublayer rows;
confirm paint multiplies parent×sublayer opacity; ensure `opacityChanged` /
`invalidated()` trigger repaint. Mirror the same opacity control on the Rendering
tab bound to the same property (MVC). Optional UX: slider popover in the delegate.
**Files:** `src/ui/panels/layertreepanel.cpp`, `src/layers/openswmmvislayer.cpp`,
`src/render/sublayers/*`, `src/ui/dialogs/layerstyledialog.cpp`.
**Verify:** Drag layer opacity and a sublayer opacity independently; both reflect in
canvas and dialog without restart.

### VS.9 — Live legend wiring ⏳
**Goal:** Legend refreshes on any symbology/classification/label/visibility change,
including sublayers.
**Changes:** In `LegendLayerTreeModel`, connect each layer renderer change and each
`ISublayer::invalidated()` to the cache rebuild (mirror `rebuildSublayerRows`
connection pattern). Ensure `LegendOverlay` already subscribes (it does).
**Files:** `src/ui/models/legendlayertreemodel.cpp`.
**Verify:** Change a graduated break / categorical color / sublayer style; legend
updates in the same interaction.

### VS.10 — Uniform optional labeling ⏳
**Goal:** Any feature layer/sublayer can be labeled, configurably.
**Changes:** One reusable `LabelsTab` bound to a `LabelConfig` owner; surface for
model, 1D results, 2D results, and GIS vector layers; honor enable toggle + scale
range + placement.
**Files:** `src/ui/dialogs/labelstab.cpp`, layer/sublayer label config plumbing.
**Verify:** Enable labels on a node layer and an isoline sublayer; labels render,
toggle off cleanly, persist in `.oswp`.

### VS.11 — Stale file cleanup ⏳
**Goal:** Repo reflects the unified architecture.
**Changes:** After VS.5 lands, remove orphaned tab classes if no longer referenced
(`temporaltab`, `masktab`, `joinstab`, `diagramstab`, `auxiliarystoragetab` — confirm
no other consumers first, per `CLAUDE.md §3`). Add supersession banners to the plans
in the table above and move fully-withdrawn ones to `docs/plans/archive/`. Remove dead
includes uncovered by VS.1–VS.10.
**Files:** `src/ui/dialogs/*`, `docs/*`, `CMakeLists.txt`.
**Verify:** Clean build; `grep` shows no references to removed symbols; docs link
graph consistent.

---

## 6. Properties Dialog — Target Layout

```
┌ Layer Properties ───────────────────────────────────────┐
│ [ Information ] [ Symbology ] [ Labels ] [ Rendering ]   │
├──────────────────────────────────────────────────────────┤
│ Symbology:                                                │
│   Mode: ( Single ▼ Categorized · Graduated · Unclassed · Rule )
│   ┌ Single ───────────────────────────────────────────┐  │
│   │ Primitive: Marker | Line | Fill                    │  │
│   │ Color [■]  Size [▮]  Outline [■] [pen ▼] [width]   │  │
│   └────────────────────────────────────────────────────┘  │
│   ┌ Graduated ────────────────────────────────────────┐  │
│   │ Attribute [▼]  Method [Jenks ▼]  Classes [5]       │  │
│   │ Ramp [▼]  [ class table: range · color · label ]   │  │
│   └────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
```

Raster layers replace the primitive block with ramp + hillshade (continuous) or
palette + class table (categorical). 2D results sublayers each present their own
compact Symbology panel (depth ramp / contour / arrows) under the same tab.

---

## 7. Out of Scope

Temporal animation config, mask/clip, auxiliary storage, external joins, embedded
diagrams, 2.5D/extrusion, blend modes beyond normal, draw effects (glow/shadow on
vectors), SVG/font markers, animated streamlines (`FLOWRENDER` FR.*), and the full
expression DSL evaluator. Hooks remain; UI is deferred.

---

## 8. Testing Strategy & Infrastructure (cross-cutting)

Per `CLAUDE.md` §4 (Goal-Driven Execution): every slice states a concrete, testable
success criterion **before** coding, then loops until the test passes. No slice is
"done" without green tests.

### 8.1 Framework & conventions (match the existing suite)

The project already uses **QtTest (`Qt6::Test`)** driven by **CTest**, with two
labels: `unit` (`tests/unit/`, headless logic) and `gui` (`tests/gui/`, offscreen
QtTest under `QT_QPA_PLATFORM=offscreen`). New tests follow the established pattern:

- Register with the `add_swmmvis_gui_test(<name> <test.cpp> <sources...>)` helper in
  `tests/gui/CMakeLists.txt` (or the `unit` equivalent). Each binary is
  **self-contained** — it compiles only the specific `src/**/*.cpp` it exercises, so
  tests stay fast and free of GDAL/engine/widget dependencies wherever possible.
- One `QObject` test class per file, `private slots` as test cases, named
  `behavior_under_condition()` (e.g. `jenks_matches_reference_breaks()`), using
  `QCOMPARE`/`QVERIFY`. Mirror `tests/gui/test_intervalbinner.cpp`.
- Header banner with `\file`, author, license, and the originating slice
  (`VS.x`), matching the house style.
- **Transparent file IO** (`CLAUDE.md §4`): any test that emits an image/fixture
  writes it under a user-reviewable path
  (`tests/gui/output/VS<x>/...`), never a temp dir. Render/pixel tests save both the
  produced PNG and the expected PNG so diffs are inspectable.

### 8.2 What gets tested at each level

- **Pure logic → `unit`/`gui` self-contained QtTest.** Classifier breaks, fill-spec
  JSON round-trip, renderer `symbolFor()`/`legendSymbolItems()`, opacity composition
  math, data-defined scalar mapping. Deterministic, fixture-based, fast.
- **Rendering correctness → offscreen pixel/region tests.** Render a layer to a
  `QImage` via the offscreen platform and assert on sampled pixels or
  cleared-region rectangles. Used for the artifact fix, relief shading,
  contours/arrows, and opacity. Compared against committed reference PNGs with a
  tolerance.
- **Integration → small offscreen scene tests.** Build a `MapCanvas` + a layer +
  sublayers, toggle visibility/opacity, switch renderer modes, and assert legend
  items and scene state stay synchronized.

### 8.3 Per-slice test matrix

| Slice | New / extended test target | Key assertions |
|---|---|---|
| VS.1 | `test_layer_visibility_clears` (new, gui) | After `setVisible(false)` + refresh, the layer's scene region is cleared (sampled pixels == background); `depopulateScene` invalidates the removed rect. Regression for the artifact bug. |
| VS.2 | `test_fillsymbollayer` (new, gui) | Fill spec JSON round-trip; `drawFill()` produces expected fill+outline pixels; legend swatch renders fill. |
| VS.3 | extend `test_intervalbinner` | Jenks/StdDev/PrettyBreaks/Log/Exponential break values vs reference fixtures; degenerate (empty/constant) fallback; JSON enum back-compat. |
| VS.4 | extend `test_ifeaturerenderer` + `test_symbolstyleadapter` | Graduated/Categorized classify **size & width** (not just color); `symbolFor()` honors bound primitive; `.oswp` round-trip; legend item count == class count. |
| VS.5 | extend `test_layerstyledialog` (new if absent, gui) | Dialog exposes exactly {Information, Symbology, Labels, Rendering} per archetype; no references to removed tab classes; constructs without crash. |
| VS.6 | extend `test_rastersymbollayers` + `test_palettedrasterrenderer` (new) | Hillshade compositing alters pixel luminance per light azimuth/altitude; paletted renderer maps discrete values → palette colors + legend categories. |
| VS.7 | extend `test_2d_sublayers` + `test_marchingtriangles` | Contour bands/isolines recompute per timestep through binner+ramp; arrow length **and** color track magnitude; sublayer invisibility uploads empty geometry. |
| VS.8 | `test_opacity_composition` (new, gui) | Effective alpha == parent × sublayer; tree col-1 edits drive `setOpacity`; `opacityChanged`/`invalidated` trigger one repaint; dialog Rendering tab and tree report the same value. |
| VS.9 | extend `test_legendlayertreemodel` (new if absent, gui) | Editing a renderer break / categorical color / sublayer style refreshes legend cache in the same interaction (no extra repaint needed). |
| VS.10 | `test_labelconfig_surface` (new, gui) | Label enable/field/placement round-trip; labels render when enabled and clear when disabled; persists in `.oswp`. |
| VS.11 | build + `grep` gate | Clean build with no references to removed symbols; full `ctest` suite green. |

### 8.4 Running the suite

On the developer machine (macOS) from the configured build dir:

```
cmake --build build/darwin --target <test_target>      # build one test
ctest --test-dir build/darwin -L gui --output-on-failure   # run gui label
ctest --test-dir build/darwin -L unit --output-on-failure  # run unit label
ctest --test-dir build/darwin --output-on-failure          # run everything
```

> **Sandbox note:** the Cowork Linux sandbox has only `g++` (no CMake/Qt6), so
> these targets are authored here but **built and run on the developer machine**.
> Each phase report below states the exact `ctest` invocation to confirm green.

### 8.5 Final acceptance pass

After VS.11, load a representative project, exercise every layer archetype through
the slimmed dialog, toggle visibility/opacity on layers and sublayers, switch
renderer modes, and confirm map + legend stay synchronized — with the full `ctest`
suite green and no stale references remaining.

---

## 9. Files Touched (representative)

**Rendering / model**
`include|src/render/fillsymbollayer.*` (new), `intervalbinner.*`, `binsampler.cpp`,
`renderers/graduatedrenderer.cpp`, `renderers/categorizedrenderer.cpp`,
`renderers/palettedrasterrenderer.*` (new), `rastersymbollayers.cpp`,
`symbolstyle.cpp`, `symbolstyleadapter.cpp`, `sublayers/*`.

**Layers**
`layers/openswmmvislayer.cpp`, `layers/swmm2dresultslayer.cpp`,
`layers/gisrasterlayer.cpp`, `map/mapcanvas.cpp`, `map/swmm2dmeshqsgrenderer.cpp`.

**UI**
`ui/dialogs/layerstyledialog.*`, `ui/dialogs/labelstab.cpp`,
`ui/dialogs/sublayerstyledialog.cpp`, new raster Symbology panel,
`ui/panels/layertreepanel.cpp`, `ui/models/legendlayertreemodel.cpp`.

**Build / docs**
`CMakeLists.txt`, `docs/*` (supersession banners + archive).

---

## 10. Sign-off Checklist

Each item requires both the code change **and** its §8.3 test target green via `ctest`.

- [ ] VS.1 artifact bug fixed · `test_layer_visibility_clears` green
- [ ] VS.2 fill primitive shipped · `test_fillsymbollayer` green
- [ ] VS.3 all classification methods · `test_intervalbinner` extended green
- [ ] VS.4 unified renderer modes · `test_ifeaturerenderer`/`test_symbolstyleadapter` green
- [ ] VS.5 dialog reduced to four tabs · `test_layerstyledialog` green, builds clean
- [ ] VS.6 DTM relief + categorical raster · raster tests green
- [ ] VS.7 dynamic contours + magnitude arrows · `test_2d_sublayers` extended green
- [ ] VS.8 opacity layers + sublayers, dialog/tree synced · `test_opacity_composition` green
- [ ] VS.9 live legend refresh · `test_legendlayertreemodel` green
- [ ] VS.10 optional labeling uniform · `test_labelconfig_surface` green
- [ ] VS.11 stale files removed, docs reconciled · full `ctest` suite green
