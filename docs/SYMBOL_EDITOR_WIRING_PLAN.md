# Symbol Editor Wiring Plan

**Status:** ⏳ Draft — for review (no code changes yet)
**Date:** 2026-05-31
**Prefix:** `SE` (Symbol Editors)
**Goal:** Make "edit a symbol in Layer Properties → Symbology → Apply" actually
repaint the map, for **every** feature type, with the correct controls
(including a marker-shape picker for point kinds like junctions).

> This plan is scoped narrowly to the symbology *editing* loop. It complements
> (does not replace) `VISUALIZATION_STYLING_OVERHAUL_PLAN.md`; the VS work added
> styling *capabilities*, this plan makes the *editor UI* drive them.

---

## 1. Confirmed Root Cause

For a `SWMMModelLayer`, `LayerStyleDialog::buildSymbologyTab()` mounts a
**`RuleSymbologyTab`** because `ruleList()` is non-null
(`layerstyledialog.cpp:471–475`). Selecting a kind (e.g. Junctions) and the
"Single Symbol" class mounts `SingleSymbolPanel`
(`singlesymbolrendererpanel.cpp`), which on the **rule path** builds an
archetype adapter via `SymbolStyleAdapter::createFor(rule)`
(`symbolstyleadapter.cpp:353`) — e.g. a `PointSymbolStyleAdapter` for nodes.

The panel then asks `StyleEditorRegistry::createEditorFor(adapter)` for a widget
(`singlesymbolrendererpanel.cpp:121`). **No `IStyleEditorWidget` is registered
for any member of the `*SymbolStyleAdapter` family.** The registered editors bind
to *different* types:

| Registered editor | Bound type | Used by |
|---|---|---|
| `PointFeatureStyleEditor` | `PointFeatureSublayerStyle` | 1D results sublayers |
| `LineFeatureStyleEditor` | `LineFeatureSublayerStyle` | 1D results sublayers |
| `PolygonFeatureStyleEditor` | `PolygonFeatureSublayerStyle` | 1D results sublayers |
| `SwmmElementSymbolEditor` | `SwmmElementSymbolAdapter` | legacy per-kind subjects (not the rule path) |
| `GisVectorSymbolEditor` | `GisVectorSymbolAdapter` | GIS vector layer |
| `MeshHillshadeEditor` | `SWMM2DMeshLayer` | whole mesh layer |
| `RasterColorRampEditor` | `GISRasterLayer` | whole raster layer |

Because nothing matches a `PointSymbolStyleAdapter`, `createEditorFor` returns
`nullptr`. The only fallback — a `QPropertyModel` property grid (which *would*
surface every adapter property, grouped by `Q_CLASSINFO`, with a
`MarkerShapeEditor` combo for the shape row) — is compiled **out**: it lives
behind `#ifdef HAVE_QPROPERTYMODEL` (`singlesymbolrendererpanel.cpp:126`), and
**`HAVE_QPROPERTYMODEL` is not defined anywhere in the build**
(`grep` of `CMakeLists.txt` returns nothing; the QPropertyModel library itself
*is* available — `layerstyledialog.cpp` includes `<qpropertymodel.h>`
unconditionally).

**Net effect:** the Single Symbol panel renders the bare label *"No
single-symbol editor registered for this layer kind."* — no controls, nothing to
apply, no marker picker. This matches both reported symptoms exactly, and it
applies to **all** feature types that take the rule path, not just junctions.

### Why the rest of the chain is fine

Once an editor exists, the downstream loop is already wired and correct:

```
editor → adapter setter → SingleSymbolRenderer::setSymbol() (in place)
       → Rule::notifyRendererStateChanged()  [emits rendererReplaced]
       → SWMMModelLayer per-kind bridge (swmmmodellayer.cpp:2022)
       → setKindRenderer() → elementSymbolFromStyle() writes color/size/SHAPE
         back onto the legacy m_<kind>Sym struct (swmmmodellayer.cpp:290–315)
       → setJunctionSymbol() … → emit repaintRequested()
       → SWMMLayerItem::paint() reads b.sym->markerShape (swmmlayeritem.cpp)
```

`elementSymbolFromStyle` already extracts the `"shape"` prop into
`markerShape`, and the paint loop already honours it. So the **only** missing
link is the editor widget. (The adapters — `PointSymbolStyleAdapter` etc. —
already declare full `Q_PROPERTY` + `Q_CLASSINFO` groups incl. `markerShape`,
`symbolstyleadapter.h:177–196`.)

---

## 2. Scope — feature types & their adapters

`SymbolStyleAdapter::createFor()` already returns the right archetype adapter
per the rule's first `SymbolLayer` kind. This plan must ensure **each** has a
working editor:

| Archetype | Adapter | Feature kinds | Key controls needed |
|---|---|---|---|
| Point | `PointSymbolStyleAdapter` | junctions, outfalls, storage, dividers, rain gages | marker shape, size, fill, outline color/width, opacity, labels |
| Line | `LineSymbolStyleAdapter` | conduits, pumps, orifices, weirs, outlets | color, width, pen/dash, cap/join, flow arrows, labels |
| Polygon | `PolygonSymbolStyleAdapter` | subcatchments | fill color, **fill style** (VS.2b), outline, opacity, labels |
| Raster ramp | `RasterColorRampSymbolStyleAdapter` | DTM / depth raster | ramp, clamp, no-data color, opacity |
| Hillshade | `HillshadeSymbolStyleAdapter` | DTM relief | azimuth, altitude, z-factor, strength |
| Contour band | `ContourBandSymbolStyleAdapter` | 2D filled contours | band count, ramp, opacity |
| Isoline | `IsolineSymbolStyleAdapter` | 2D line contours | iso count, color, width, dash, labels |
| Mesh edge | `MeshEdgeSymbolStyleAdapter` | mesh wireframe | color, width, dash |
| Mesh node | `MeshNodeSymbolStyleAdapter` | mesh vertices | shape, size, fill, outline |
| Vector glyph | `VelocityVectorSymbolStyleAdapter` | velocity arrows | length, spacing, head, color, **color-by-magnitude** (VS.7) |
| Generic | `SymbolStyleAdapter` | fallback | opacity + raw props |

Renderer **classes** also need their panels confirmed working end-to-end (not
just Single Symbol): **Graduated** and **Categorized** edit through
`GraduatedRendererPanel` / `CategorizedRendererPanel`; their output reaches the
painter via the per-feature override caches (`rebuildKindFeatureColors`), a
different path than single-symbol back-prop — to be verified per SE.5.

---

## 3. Design Decision (for approval)

Two complementary approaches; the plan proposes **both, staged**:

**A — Generic grid fallback (SE.0, immediate un-break).** Enable the
`QPropertyModel` property grid in `SingleSymbolPanel` (define
`HAVE_QPROPERTYMODEL`, or drop the guard since the library is already linked).
Because every adapter exposes grouped `Q_PROPERTY`s and the panel already
registers `MarkerShapeEditor` for the shape row, this single change makes **all**
feature types editable — including the junction marker picker — with edits
flowing through the existing bridge to repaint. Low risk, high coverage, modest
UX (a grouped property tree rather than bespoke widgets).

**B — Dedicated editors (SE.1–SE.4, polish).** Register purpose-built
`IStyleEditorWidget`s for the `*SymbolStyleAdapter` family, mirroring the
existing `FeatureStyleEditor` widgets (swatches, shape combo with previews,
pen-style dropdowns, live preview). These *displace* the generic grid for the
common types and give the modern look the broader overhaul targets.

Rationale for staging: A restores correctness for every feature type in one
small, reviewable change; B then upgrades UX without re-introducing risk. If you
prefer to skip the generic grid and go straight to dedicated editors, we drop
SE.0 and the common types are simply broken until SE.1–SE.3 land — so A is
recommended as the safety net.

> **Decision needed at review:** (1) Approve A+B staged, or B-only? (2) Should the
> dedicated editors reuse/extend the existing `FeatureStyleEditor` widgets, or be
> new classes parallel to them?

---

## 4. Phase Breakdown

Each phase is independently shippable and ends with a manual verification on a
real model plus, where logic is involved, a headless QtTest.

### SE.0 — Generic grid fallback (immediate un-break) ⏳
**Goal:** Every feature type's Single Symbol panel shows editable, grouped
controls (incl. marker shape); edits repaint the map.
**Changes:** Enable the `QPropertyModel` path in `SingleSymbolPanel` — add
`HAVE_QPROPERTYMODEL` to the target's compile definitions (guarded by the
QPropertyModel package being found) or remove the `#ifdef` since
`<qpropertymodel.h>` is already an unconditional dependency. Confirm
`MarkerShapeEditor` is registered for the shape row (it is, in that block).
**Files:** `CMakeLists.txt` (compile definition) and/or
`src/ui/dialogs/editors/singlesymbolrendererpanel.cpp`.
**Verify:** Open Junctions → Single Symbol → a grouped grid appears with a
shape combo; change shape + color + size, Apply → nodes repaint. Repeat for a
conduit (line) and a subcatchment (polygon).

### SE.1 — Point symbol editor ⏳
**Goal:** Polished editor for `PointSymbolStyleAdapter` (junctions et al.).
**Changes:** New `PointSymbolStyleEditor : IStyleEditorWidget` (or extend
`PointFeatureStyleEditor` to also bind the adapter) with: marker-shape combo
(icon previews), size spin, fill/outline color swatches, outline width,
opacity, optional label group. `REGISTER_STYLE_EDITOR(PointSymbolStyleAdapter, …)`.
**Files:** `src/ui/dialogs/editors/pointsymbolstyleeditor.{h,cpp}` (or extend
`featurestyleeditor.cpp`), CMake.
**Verify:** Junction shape/size/color edits reflect on Apply; round-trip via
`.oswp`; live preview matches map.

### SE.2 — Line symbol editor ⏳
**Goal:** Editor for `LineSymbolStyleAdapter` (conduits, pumps, orifices, weirs,
outlets): color, width, pen/dash style, cap/join, flow-arrow group, labels.
**Files:** new editor + `REGISTER_STYLE_EDITOR(LineSymbolStyleAdapter, …)`.
**Verify:** Conduit width/dash/arrow edits reflect on Apply.

### SE.3 — Polygon symbol editor ⏳
**Goal:** Editor for `PolygonSymbolStyleAdapter` (subcatchments): fill color,
**fill style** (solid/hatch — VS.2b), outline color/width/pen, opacity, labels.
**Files:** new editor + `REGISTER_STYLE_EDITOR(PolygonSymbolStyleAdapter, …)`.
**Verify:** Subcatchment fill + hatch + outline edits reflect on Apply.

### SE.4 — Raster / mesh / 2D archetype editors ⏳
**Goal:** Editors for `RasterColorRampSymbolStyleAdapter`,
`HillshadeSymbolStyleAdapter`, `ContourBandSymbolStyleAdapter`,
`IsolineSymbolStyleAdapter`, `MeshEdgeSymbolStyleAdapter`,
`MeshNodeSymbolStyleAdapter`, `VelocityVectorSymbolStyleAdapter`. Several whole-
layer editors already exist (`MeshHillshadeEditor`, `RasterColorRampEditor`);
reuse their widgets where possible. The generic grid (SE.0) covers any not yet
given a bespoke editor, so this phase is incremental polish.
**Verify:** Each 2D/mesh sublayer's controls edit + repaint; arrow
color-by-magnitude (VS.7) and DTM relief (VS.6) are reachable from the dialog.

### SE.5 — Verify Graduated / Categorized end-to-end ⏳
**Goal:** Confirm renderer-class changes (Single↔Graduated↔Categorized) and
graduated/categorized edits repaint correctly for every layer type, via the
per-feature override caches. Fix any gaps where the class swap doesn't
`rebuildKindFeatureColors` or doesn't repaint.
**Files:** `swmmmodellayer.cpp` (`setKindRenderer`/`rebuildKindFeatureColors`),
`graduatedrendererpanel.cpp`, `categorizedrendererpanel.cpp` as needed.
**Verify:** Switch a node kind to Graduated by an attribute → map shows binned
colors; switch back to Single → reverts.

### SE.6 — GIS vector + results-layer parity ⏳
**Goal:** Confirm the same Apply→repaint loop for `GISVectorLayer` (uses
`GisVectorSymbolAdapter` — already has an editor) and the 1D/2D results layers
(FeatureSublayerStyle editors). Close any remaining type that still shows the
empty-panel label.
**Verify:** Edit each non-SWMM-model layer type → reflects on Apply.

### SE.7 — Tests + final acceptance ⏳
**Goal:** Lock the behaviour against regression.
**Changes:** Headless QtTests: (a) `createEditorFor` returns a non-null editor
for every `*SymbolStyleAdapter` type; (b) a `PointSymbolStyleAdapter`
`setMarkerShape`/`setFillColor` edit propagates through
`Rule::notifyRendererStateChanged` → `SWMMModelLayer` legacy struct
(`m_junctionSym.markerShape` / `.fillColor`); (c) `.oswp` round-trip of an edited
symbol. Plus a final manual pass across all feature types.
**Verify:** `ctest -L gui` green; manual matrix complete.

---

## 5. Progress Tracking

Updated after each phase during implementation.

| Phase | Status | Notes |
|---|---|---|
| SE.0 `HAVE_QPROPERTYMODEL` define | 🔧 done — pending build | Added `target_compile_definitions(SWMMVis PRIVATE $<$<TARGET_EXISTS:QPropertyModel>:HAVE_QPROPERTYMODEL>)`. **Retained** per the user's direction to keep QPropertyModel where it's used elsewhere — this enables the QProperty paths in the attribute panel, profile-options and mesh-profile dialogs (all gated on the same macro, previously compiled out). The symbology grid it briefly enabled has since been **removed** (see "Grid removal" row). |
| Grid removal (symbology) | 🔧 done — pending build | Removed the QPropertyModel generic-grid fallback from `SingleSymbolPanel` (`singlesymbolrendererpanel.cpp`) — symbology now uses only dedicated `IStyleEditorWidget`s. The `createEditorFor` tail-match fix (SE.1) + the SE.1–SE.4 editors mean every archetype resolves to a real editor, so the panel never falls back to a grid. QPropertyModel remains in use elsewhere (attribute panel, preferences, sublayer style dialog, transect/chart/legend editors). |
| SE.1 Point editor | 🔧 done — pending build | New `PointSymbolStyleEditor` (shape combo, size, fill, outline, opacity) + registration. **Also fixed `StyleEditorRegistry::createEditorFor`** to match the unqualified class-name tail — namespaced editors (incl. the existing results-layer `FeatureSublayerStyle` editors) never matched the fully-qualified `metaObject` name and silently fell back to the grid. This is the change that lets *any* dedicated editor load. |
| SE.2 Line editor | 🔧 done — pending build | `LineSymbolStyleEditor` (colour, width, dash, opacity) + registration. Arrow/label groups deferred (labels live in the Labels tab). |
| SE.3 Polygon editor | 🔧 done — pending build | `PolygonSymbolStyleEditor` (fill, outline, width, opacity) + registration. Hatch fill-style control deferred (the VS.2b `fillStyle` prop lives on the legacy struct, not yet on the adapter). |
| SE.4 Raster/mesh/2D editors | 🔧 done — pending build | Dedicated editors for all 7 archetype adapters (RasterColorRamp, Hillshade, ContourBand, Isoline, MeshEdge, MeshNode, VelocityVector) in `symbolstyleeditors2d.{h,cpp}`, covering every Q_PROPERTY so nothing is lost with the grid gone. Registered + added to CMake. **Required precondition for the grid removal above.** |
| SE.5 Graduated/Categorized verify | ⏳ | — |
| SE.6 GIS vector + results parity | 🔧 partially unblocked | The SE.1 registry fix revives the previously-dead `Point/Line/PolygonFeatureSublayerStyle` and `GisVectorSymbolAdapter` editors. Needs build verification. |
| SE.7 Tests + acceptance | ⏳ | — |

Legend: ⏳ pending · 🔧 in progress / done-pending-build · ✅ shipped & verified.

---

## 6. Verification Strategy

Per-phase: a concrete manual check on a representative model (named above) plus,
for logic, a self-contained QtTest registered via `add_swmmvis_gui_test`
following the existing pattern. The decisive cross-cutting test (SE.7b) asserts
the editor→adapter→bridge→legacy-struct propagation without a window, so the
core loop is regression-locked. Build/run on the developer machine
(`cmake --build … && ctest -L gui`); the Cowork sandbox has no Qt6/CMake.

---

## 7. Out of Scope

Refactoring the SWMM paint loop to read directly from `symbolFor()` (a larger
"renderer-drives-paint" change); the expression/data-defined editor UI; legend
authoring. These remain in the broader `VISUALIZATION_STYLING_OVERHAUL_PLAN.md`.

---

## 8. Open Questions for Review

1. **A+B staged, or B-only?** (Recommend A+B: SE.0 restores correctness fast.)
2. **Reuse `FeatureStyleEditor` widgets for the adapter editors, or new classes?**
3. **Priority order** — is the point/line/polygon trio (SE.1–SE.3) the priority,
   with 2D/mesh (SE.4) after, as drafted?
4. Any feature type where you'd accept the generic grid permanently (skipping a
   bespoke editor)?
