# 2D Mesh — Boundary-Condition Edge Colouring & Cell-Attribute Colouring

**Date:** 2026-08-19
**Status:** IMPLEMENTED (uncompiled) — see
`HANDOFF_MESH_BC_CELL_ATTR_STYLING_2026-08-19.md` for the build/verify pass
and for **four deviations from this plan** (§4 of the handoff). Where the two
documents disagree, the handoff describes the code.
**Scope:** `SWMM2DMeshLayer` / `SWMM2DMeshQSGRenderer` (model-edit view only)

---

## 0. Goal

Two related, user-configurable additions to 2D mesh symbology:

1. **Edge BC colouring** — colour every mesh edge by its boundary-condition
   type, configurable per type through the layer style dialog.
2. **Cell attribute colouring** — let the terrain fill be driven by a
   selectable per-cell attribute (Manning's *n*, initial depth, and the
   pending groundwater/soil keys) instead of only bed elevation.

### Decisions taken (2026-08-19 review)

| Question | Decision |
|---|---|
| Cell attributes exposed | Elevation + **all** `cellParamSpecs()` keys, **including the disabled `gw.*` placeholders** |
| Edge scope | **All edges coloured by BC type**; interior slots resolve to `Wall` |
| Results layer | **Model view only** — `SWMM2DResultsLayer` unchanged (deliberate divergence) |
| Process | Work plan first, then implement |

### Known constraint — soil attributes are not real yet

The engine has **no soil column and no infiltration on the 2D mesh**
(`openswmm.engine/docs/manuals/reference/hydraulics/sections/Chapter9-TwoDimensional.md`
§9.12: *"There is no infiltration on the mesh"*). `MeshTriangle`
(`include/mesh/meshresult.h`) carries only `mannings`, `initDepth`, `tag`.
The five `gw.*` keys in `src/mesh/meshcellparams.cpp:39-52` are registered
with `enabled = false` and `cellParamValue()` returns NaN for them.

Per the decision above they **will** appear in the colour-by selector, greyed,
and will render as the no-data colour with a legend row that says so. This is
plumbing-ahead for `INTEGRATED2D_GW_GUI_PLAN_2026-08-15.md`; it must not be
mistaken for working data. **Acceptance requires that a user who picks `gw.Ks`
sees an unambiguous "no data — engine support pending" state, not a blank or
misleadingly uniform mesh.**

### Non-goals

- No new engine API. No changes to `openswmm.engine`.
- No change to the `IFeatureRenderer` stack — the mesh layer styles through
  `ISublayerHost` + `SublayerStyle` bags, and this work stays on that path.
- No BC *editing* changes. `MeshEditingToolbar` and `meshcommands.h` are
  read-only inputs here.
- No `[2D_*]` INP or GeoPackage schema changes. Styling is GUI-side only and
  persists in the `.swmm-style.json` / project style blob.

---

## 1. Data the renderer needs, and what's missing

**BC storage today** (`include/layers/swmm2dmeshlayer.h:271`):

```cpp
QVector<mesh::MeshEdgeBC> m_bc;   // sized nTri*3, flat-indexed tri*3 + edgeLocal
```

`mesh::MeshBCTypes::Type` (`include/mesh/meshbctype.h`) — **order is locked**
(participates in `.oswp` + `[2D_BOUNDARY_CONDITIONS]` encoding):

| # | Enum | GUI label |
|---|---|---|
| 0 | `Wall` | Wall |
| 1 | `NormalFlow` | Normal Flow |
| 2 | `SpecifiedStageConst` | Specified Stage (constant) |
| 3 | `SpecifiedStageTS` | Specified Stage (time series) |
| 4 | `SpecifiedFlowConst` | Specified Flow (constant) |
| 5 | `SpecifiedFlowTS` | Specified Flow (time series) |
| 6 | `RatingCurve` | Rating Curve |

**The gap:** `SWMM2DMeshLayer::SceneEdge` (`swmm2dmeshlayer.h:545`) is

```cpp
struct SceneEdge { QLineF line; float zAvg; float slope; };
```

`m_sceneEdges` is **deduplicated** — one entry per unique undirected edge — so
there is no way to get from a scene edge back to a `tri*3 + edgeLocal` BC slot.
This must be fixed before any BC colouring is possible.

**Resolution:** add a parallel `QVector<qint32> m_sceneEdgeSlot`, not a field on
`SceneEdge`. `SceneEdge` is currently 40 bytes; adding an `int` pushes it to 44
and pads to 48 (+8 B/edge). A parallel vector costs exactly 4 B/edge and leaves
the hot struct's cache behaviour untouched. On a 1.5 M-edge mesh that is 6 MB.

**Dedup tie-break:** an interior edge has two candidate slots. Prefer the slot
whose BC type is not `Wall`; if both are `Wall` (the normal case) use the lower
`tri` index. The non-Wall preference is a cheap safety net — BCs are only
meaningful on boundary edges, and `resizeBCsToMesh()` defaults interior slots to
`Wall` — but it keeps the render honest if a stale/imported project has a BC on
an interior slot.

---

## 2. Part A — Edge colouring by BC type

### A.1 Style bag — `MeshEdgeStyle`

`include/render/sublayers/meshedgesublayer.h`. Add eight properties in a new
`Q_CLASSINFO` group so `LayerStyleDialog` picks them up with no dialog code:

```cpp
Q_PROPERTY(bool   colorByBC   READ colorByBC   WRITE setColorByBC   NOTIFY styleChanged)
Q_PROPERTY(double bcWidthPx   READ bcWidthPx   WRITE setBcWidthPx   NOTIFY styleChanged)
Q_PROPERTY(QColor bcWallColor            ...)
Q_PROPERTY(QColor bcNormalFlowColor      ...)
Q_PROPERTY(QColor bcStageConstColor      ...)
Q_PROPERTY(QColor bcStageTSColor         ...)
Q_PROPERTY(QColor bcFlowConstColor       ...)
Q_PROPERTY(QColor bcFlowTSColor          ...)
Q_PROPERTY(QColor bcRatingCurveColor     ...)

Q_CLASSINFO("group:colorByBC",           "Boundary conditions")
Q_CLASSINFO("group:bcWidthPx",           "Boundary conditions")
Q_CLASSINFO("group:bcWallColor",         "Boundary conditions")
// ... one per colour
```

**Why seven explicit `QColor` properties rather than a `CategoricalPalette` or a
second `ClassificationScheme`:** `ClassificationScheme` has only `Continuous`
and `Classified` modes (`classificationscheme.h:66`) — both numeric. Forcing a
7-value enum through manual breaks at 0.5/1.5/… is a lie about the data and
would leak into the legend labels. Seven declarative properties are
self-documenting, get a dialog and JSON round-trip for free from the existing
`SublayerStyle` machinery, and the enum is frozen at seven so there is no
open-ended growth to design for.

**Defaults** — seeded from `CategoricalPalette::byName("Tab10")` in the
`MeshEdgeStyle` constructor, except:

- `bcWallColor` defaults to `m_color` (`QColor(0,0,0,130)`) so that turning
  `colorByBC` on does **not** change how the mesh interior looks. Only the
  boundary lights up.
- `bcWidthPx` defaults to `1.6` (vs `lineWidthPx` `0.35`) so non-Wall BC edges
  read as a distinct boundary ribbon rather than a hairline.
- `colorByBC` defaults to **`false`** — existing projects render byte-identically
  until the user opts in.

`toJson()` / `fromJson()` in `src/render/sublayers/meshedgesublayer.cpp:70-110`
gain the eight keys, with `fromJson` falling back to the constructor defaults
for absent keys (old style files load cleanly).

### A.2 Renderer — Pass 2 bucketing

`src/map/swmm2dmeshqsgrenderer.cpp:1051-1121`.

Today Pass 2 emits **two** `QSGFlatColorMaterial` nodes, `edgeThinNode` and
`edgeWideNode`, split by slope. The plan adds **six** more flat nodes — one per
non-Wall BC type — and leaves the thin/wide pair to carry everything Wall:

```
edgeThinNode          slope-thin, BC == Wall            → bcWallColor (or legacy thinColor)
edgeWideNode          slope-wide, BC == Wall            → bcWallColor-derived (or legacy wideColor)
edgeBcNode[0..5]      BC == NormalFlow .. RatingCurve   → bc*Color, width bcWidthPx
```

**Why this and not `QSGVertexColorMaterial`:** switching the edge nodes to
per-vertex colour would be more general, but it grows every edge vertex from
`Point2D` (8 B) to `ColoredPoint2D` (12 B) — +36 MB on a 1.5 M-edge mesh at
6 verts/edge — for a payload where the colour is *constant across ~99.9 % of
edges*. Interior edges are all `Wall`; only the boundary ring is non-Wall, so
the six extra buckets are tiny (typically 10³–10⁴ segments) and total geometry
memory is unchanged. Flat nodes also keep the existing `setFlatColor` +
`withOp()` opacity path intact.

Trade-off accepted: six extra draw calls. Negligible against the existing
10-node tree, and five of them upload zero vertices on a mesh with no BCs.

Concretely:

1. **Node-tree order.** Insert the six BC nodes immediately after
   `edgeWideNode` (so they paint above the wireframe, below contours). Both the
   construction block (`:399-408`) **and** the sibling-walk block (`:411-420`)
   must be updated in lockstep — the walk is positional and silently
   mis-casts if they drift. Add a `static_assert`-style comment naming the
   count.
2. **Off-screen early-out** (`:457-471`) must `uploadFlatVerts(..., empty_p)`
   the six new nodes too.
3. **Bucketing loop** (`:1089-1099`) becomes:

```cpp
const bool byBC = edgeStyle && edgeStyle->colorByBC();
const auto &slots = m_layer->m_sceneEdgeSlot;
const auto &bcs   = m_layer->edgeBCs();

for (int ii = 0; ii < edgeCount; ++ii) {
    const int  ei = useEdgeIdx ? visibleEdges[ii] : ii;
    const auto &e = edges[ei];
    const float ax = ..., ay = ..., bx = ..., by = ...;

    if (byBC) {
        const int slot = (ei < slots.size()) ? slots[ei] : -1;
        const int t    = (slot >= 0 && slot < bcs.size())
                       ? int(bcs[slot].type) : 0;   // 0 == Wall
        if (t != 0) { appendThickSeg(bcSegs[t-1], ax,ay,bx,by, kBcHW); continue; }
    }
    if (useSlopeWidth && hasElev && (e.slope*invSlope > kSplit))
        appendThickSeg(wideSegs, ax,ay,bx,by, kWideHW);
    else
        appendThickSeg(thinSegs, ax,ay,bx,by, kThinHW);
}
```

4. **Colours.** When `byBC`, `thinColor`/`wideColor` are overridden by
   `bcWallColor` (wide keeps the historic alpha bump). When `!byBC`, the
   existing slope-`ClassificationScheme` path at `:1068-1083` is untouched.
5. **Opacity.** The six BC nodes go through the same `withOp()` lambda.
6. **Stats.** `stats.addPass("edges", ...)` sums the six buckets too.

### A.3 Invalidation

BC edits funnel through `SWMM2DMeshLayer::applyMeshEdgeBC()`
(`src/layers/swmm2dmeshlayer.cpp:1802`), which emits
`attributeChanged(refName)`. The renderer only listens to `repaintRequested`
(`swmm2dmeshqsgrenderer.cpp:320`).

**Two things must be verified before writing code, and fixed if absent:**

- (a) `attributeChanged` reaches `repaintRequested`. If it does not, connect it
  (or emit alongside) in the layer, not in the renderer.
- (b) A BC-only edit must not be swallowed by the `insideCoverage` /
  `lodKeyChanged` short-circuit at `:539-548`. Pass 2 has no per-edge cache, so
  it recomputes whenever it runs — but it only runs when the resolved dirty bits
  say so. Add a `m_bcRevision` counter on the layer, bumped in `applyMeshEdgeBC`
  / `resizeBCsToMesh`, snapshotted by the renderer as `m_lastBcRev`, and folded
  into the `geomChanged`-style diff so a BC edit forces the edge pass.

`m_sceneEdgeSlot` is rebuilt in `rebuildSceneGeometry()`
(`swmm2dmeshlayer.cpp:1004`) **and** `rebuildSceneGeometryLight()` (`:1092`) —
both, or a light rebuild leaves a stale/short slot vector and the
`ei < slots.size()` guard silently degrades every edge to Wall.

---

## 3. Part B — Cell fill colouring by attribute

### B.1 Style bag — `MeshFillStyle`

`include/render/sublayers/meshfillsublayer.h`:

```cpp
Q_PROPERTY(QString colorByAttribute READ colorByAttribute WRITE setColorByAttribute NOTIFY styleChanged)
Q_PROPERTY(QColor  noDataColor      READ noDataColor      WRITE setNoDataColor      NOTIFY styleChanged)
Q_CLASSINFO("group:colorByAttribute", "Fill")
Q_CLASSINFO("group:noDataColor",      "Fill")
```

- `colorByAttribute` default `"elevation"` — the sentinel for today's bed-Z
  behaviour. Other legal values are exactly the `key`s from
  `mesh::cellParamSpecs()`: `mannings`, `initDepth`, `gw.Ks`, `gw.zs`,
  `gw.thetaS`, `gw.hu0`, `gw.hg0`.
- `noDataColor` default `QColor(200, 200, 200, 120)` — flat grey, used for any
  cell whose attribute value is NaN.
- `useElevationRamp` keeps its meaning as the on/off gate (false → flat
  `fillColor`). Its *name* is now slightly wrong; **not** renaming it, because
  the key is in shipped `.swmm-style.json` files and in
  `projectserializer.cpp`. A doc comment will record the widened meaning.
- `classification` (existing `ClassificationScheme`) applies to whichever
  attribute is selected. **Switching `colorByAttribute` must reset the scheme's
  cached range** — a scheme classified over elevation (10–90 m) applied to
  Manning's *n* (0.02–0.10) collapses to one class.

### B.2 Layer — attribute value supply

`SWMM2DMeshLayer` gains, mirroring the existing `elevationSamples()`
(`swmm2dmeshlayer.h:498`):

```cpp
/*! Per-triangle value of \p key, parallel to m_sceneTris. NaN = unset.
 *  Cached; invalidated by geomRevision / attributeChanged. */
[[nodiscard]] const QVector<float> &cellAttributeValues(const QByteArray &key) const;

/*! Strided sample of \p key for quantile / natural-breaks classification.
 *  NaNs excluded. Empty when the attribute has no data at all. */
[[nodiscard]] QVector<double> cellAttributeSamples(const QByteArray &key,
                                                   int maxSamples = 200000) const;

/*! Observed [min,max] over non-NaN values; invalid QPair when all-NaN. */
[[nodiscard]] std::optional<QPair<double,double>> cellAttributeRange(const QByteArray &key) const;
```

Backed by `mesh::cellParamValue(mesh(), tri, key)` — the single source of truth
from `meshcellparams.h`. **Do not** read `MeshTriangle::mannings` directly; the
registry is the contract, and it is what will start returning real numbers for
`gw.*` when the engine lands them.

Strided sampling matters: `elevationSamples()` already caps at 200 k for
responsiveness on multi-million-cell meshes, and the classification editor calls
this on every keystroke.

### B.3 Renderer — Pass 1

`src/map/swmm2dmeshqsgrenderer.cpp:706-830`. Today the classify input is
`t.zAvg` and the range is `[zMin, zMax]`. Changes:

1. Resolve `attrKey = fillStyle->colorByAttribute()`. When `"elevation"`, the
   code path is **byte-identical to today** — same `t.zAvg`, same
   `legacyElevationRamp()` fast path, same cache.
2. Otherwise take `const QVector<float> &vals = m_layer->cellAttributeValues(attrKey)`
   and `[vMin,vMax]` from `cellAttributeRange`. `emitShadedTri` classifies
   `vals[triIdx]` instead of `t.zAvg`.
3. **NaN → `noDataColor`**, bypassing the ramp. Every `gw.*` key is 100 % NaN
   today, so the whole mesh renders `noDataColor` — the intended, honest,
   visible signal that the attribute has no data.
4. **Hillshade stays on geometry.** `lit` continues to come from the triangle's
   z-normal (`:808-823`). Shading is a terrain-relief cue; detaching it from the
   colour source is correct and keeps the relief readable under any attribute.
5. **Fill cache key.** `m_fillCacheRev` (`:755`) is
   `geomRevision() ^ scheme.revision()`. It must additionally fold in
   `qHash(attrKey)` and an attribute revision, or switching Manning's → initial
   depth will show stale colours. Extend the `fillCacheHit` predicate at
   `:756-767` with `m_fillCacheAttrKey == attrKey`.
6. `emitShadedTri` currently receives `const SceneTri &`; it needs the triangle
   index to index `vals`. The overview-quad and far-zoom top-up call sites pass
   `cacheSlot == nullptr` and are outside the native index space — they must
   pass `triIdx == -1` and fall back to `noDataColor`/flat, **not** index `vals`
   out of range.

### B.4 Attribute selector UI

`include/render/attributecandidates.h` gains:

```cpp
namespace OpenSWMM::Render::AttributeCandidates {
/*! Colour-by candidates for 2D mesh cells: "elevation" plus every
 *  mesh::cellParamSpecs() key. Entries whose spec has enabled == false are
 *  returned with a disabled marker so the combo greys them. */
QList<AttributeCandidate> meshCellNumeric();   // {key, label, enabled, tooltip}
}
```

This introduces a small `AttributeCandidate` struct because the existing
functions return bare `QStringList` and cannot express "greyed, engine support
pending". Existing callers are untouched.

The `LayerStyleDialog` symbology tab binds `colorByAttribute` to a combo built
from `meshCellNumeric()`, disabling rows via `Qt::ItemIsEnabled` on the model —
the same treatment `MeshEditingToolbar` already gives the disabled cell params.
Tooltip on a disabled row: the spec's `tooltip` plus *"Engine support pending
(2D groundwater)."*

---

## 4. Part C — Legend

- `MeshEdgeSublayer::legendSymbolItems()` (`src/render/sublayers/meshedgesublayer.cpp:~131`):
  when `colorByBC`, emit one row per BC type **that is actually present in the
  mesh**, labelled with `mesh::MeshBCTypes::label(t)`. Always include Wall.
  Emitting all seven regardless would put five dead rows in the dock on a
  typical model. Requires the sublayer to reach the layer's BC vector — pass the
  present-type set down through `SublayerContext`, or cache a
  `QSet<int> m_bcTypesPresent` on the layer refreshed alongside `m_bcRevision`.
  (Prefer the cached set — `legendSymbolItems()` is `const` and context-free.)
- `MeshFillSublayer::legendSymbolItems()`: title the ramp with
  `mesh::cellParamLabel(key, depthUnitLabel)` instead of a hard-coded
  "Elevation". When the attribute is all-NaN, emit a single `noDataColor` swatch
  labelled *"No data — engine support pending"*.

---

## 5. Part D — Persistence

- `SublayerStyle::toJson`/`fromJson` overrides on both bags cover
  `StyleFileIO` (`swmmvis-style/v1`) and the project blob automatically —
  **verify, don't assume**, by round-tripping through
  `StyleFileIO::exportStyle` / `importStyle`.
- Backwards compatibility: absent keys → constructor defaults →
  `colorByBC == false`, `colorByAttribute == "elevation"` → identical render.
  A v1 style file written before this change must load without warnings.
- Forward compatibility: a style file written *after* this change loaded by an
  older build ignores the unknown keys. Acceptable, no version bump.

---

## 6. Phases and verification

Each phase is independently buildable and independently verifiable. Per
`CLAUDE.md` §4.1, every test artefact (screenshots, exported style JSON, sample
`.inp`) is written to `workplans/artifacts/mesh-bc-styling/`, **not** to
`/tmp` or a temp dir.

| # | Step | Verify |
|---|---|---|
| 1 | `m_sceneEdgeSlot` + `m_bcRevision` + `m_bcTypesPresent` on `SWMM2DMeshLayer`; populate in **both** `rebuildSceneGeometry()` and `rebuildSceneGeometryLight()` | Unit test: on a fixture mesh, every boundary `SceneEdge` maps to a slot with `isBoundaryEdge(slot/3, slot%3) == true`; `m_sceneEdgeSlot.size() == m_sceneEdges.size()` after **both** rebuild paths |
| 2 | `MeshEdgeStyle` BC properties + JSON round-trip | Unit test: default-constructed → `toJson` → `fromJson` → equal; a pre-change v1 JSON blob loads with `colorByBC == false` |
| 3 | Renderer Pass 2 bucketing + node-tree wiring | Load a fixture with ≥1 of each BC type; `colorByBC` on → screenshot shows 7 distinct colours on the boundary ring, interior unchanged. `colorByBC` off → pixel-identical to a pre-change screenshot of the same view |
| 4 | BC-edit invalidation (`m_bcRevision` → dirty bits) | Change one edge's BC via `MeshEditingToolbar` **without panning or zooming**; the map recolours on the next frame. This is the case the `insideCoverage` short-circuit would eat |
| 5 | `cellAttributeValues` / `Samples` / `Range` on the layer | Unit test: `mannings` range matches a hand-computed min/max on the fixture; `gw.Ks` returns an empty sample vector and a null range; sampling a 1 M-cell mesh returns ≤ 200 k values |
| 6 | `MeshFillStyle::colorByAttribute` + Pass 1 rewire + cache key | Switch elevation → Manning's → initial depth → elevation; each switch recolours immediately (no stale frame). Elevation render is pixel-identical to pre-change |
| 7 | `gw.*` no-data path | Select `gw.Ks`: entire mesh renders `noDataColor`, legend reads "No data — engine support pending", combo row is greyed with the explanatory tooltip |
| 8 | Legend rows (edge BC + fill attribute) | LegendDock shows one row per **present** BC type; fill ramp title tracks the selected attribute and its unit |
| 9 | Style file + project round-trip | `StyleFileIO::exportStyle` → edit unrelated props → `importStyle` → BC colours and `colorByAttribute` survive. Save/reopen project: same |
| 10 | Perf regression on a large mesh | ≥1 M-cell fixture: frame time and peak RSS within 5 % of pre-change with `colorByBC` off; with it on, the six BC buckets add < 5 % |
| 11 | Confirm `SWMM2DResultsLayer` untouched | Results-view mesh edges render exactly as before; grep confirms no BC branch reached from `swmm2dresultsqsgrenderer.cpp` |

---

## 7. Files touched

**Modified**

```
include/layers/swmm2dmeshlayer.h              m_sceneEdgeSlot, m_bcRevision, m_bcTypesPresent,
                                              cellAttributeValues/Samples/Range
src/layers/swmm2dmeshlayer.cpp                both rebuild paths, applyMeshEdgeBC bump, attr cache
include/render/sublayers/meshedgesublayer.h    8 BC properties
src/render/sublayers/meshedgesublayer.cpp      ctor defaults, toJson/fromJson, legendSymbolItems
include/render/sublayers/meshfillsublayer.h    colorByAttribute, noDataColor
src/render/sublayers/meshfillsublayer.cpp      ctor defaults, toJson/fromJson, legendSymbolItems
src/map/swmm2dmeshqsgrenderer.cpp              node tree, early-out, Pass 1 attr, Pass 2 buckets
include/map/swmm2dmeshqsgrenderer.h            m_lastBcRev, m_fillCacheAttrKey
include/render/attributecandidates.h           AttributeCandidate, meshCellNumeric()
src/render/attributecandidates.cpp             meshCellNumeric()
src/ui/dialogs/symbologytab.cpp (or editors/)  colour-by combo binding w/ disabled rows
```

**Unchanged, deliberately**

```
src/map/swmm2dresultsqsgrenderer.cpp           results view keeps current edge styling
include/mesh/meshbctype.h                      enum order is locked
include/mesh/meshcellparams.h                  registry is already the right shape
src/mesh/inpmeshwriter.cpp / inpmeshreader.cpp no INP change — styling is GUI-side
openswmm.engine/**                             no engine change
```

**New**

```
tests/…/tst_meshbcstyling.cpp                  phases 1, 2, 5
workplans/artifacts/mesh-bc-styling/           screenshots, exported styles, fixture .inp
```

---

## 8. Open risks

1. **Positional sibling walk.** `updatePaintNode`'s `firstChild()`/`nextSibling()`
   chain (`:409-421`) is the single most likely place to introduce a
   silent memory-corrupting bug — a missed `nextSibling()` `static_cast`s a
   `QSGNode*` to `QSGGeometryNode*`. Both blocks change together, reviewed
   together.
2. **Light rebuild path.** If `rebuildSceneGeometryLight()` is missed,
   BC colouring degrades to all-Wall only after certain reloads — an
   intermittent bug. Phase 1's test covers both paths explicitly.
3. **`gw.*` placeholders reading as real.** Mitigated by phase 7's acceptance
   criteria, but the risk is a user screenshotting a grey mesh as "soil data".
   The legend text must say *engine support pending*, not just *no data*.
4. **Classification range carry-over** between attributes with wildly different
   magnitudes (elevation ~10¹, Manning's ~10⁻²). Phase 6 must confirm the reset.
5. **`useElevationRamp` name drift** — now gates a non-elevation ramp. Documented,
   not renamed, to avoid breaking shipped style files.

---

## 9. Follow-ups (out of scope, worth filing)

- Mirror BC colouring into `SWMM2DResultsLayer` if the divergence proves
  annoying in practice.
- `MeshEdgeBC::group` (the `[2D_BOUNDARY_CONDITIONS]` `GROUP` column) is a
  natural second thematic axis — colour by BC *group* rather than type. Note
  that the engine C API does **not** expose `GROUP`; it survives only in the
  GUI model and the GeoPackage `bc_group` column.
- The >2-class limitation in the slope classification path
  (`swmm2dmeshqsgrenderer.cpp:1075`) is untouched here and still stands.
- Wire `gw.*` to real values under `INTEGRATED2D_GW_GUI_PLAN_2026-08-15.md`;
  this plan's plumbing should then light up with no renderer change.
