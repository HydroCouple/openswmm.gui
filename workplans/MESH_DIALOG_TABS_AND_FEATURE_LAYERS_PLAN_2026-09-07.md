# Mesh Dialog Quality Tabs and Editable Feature Layers — Plan

| | |
|---|---|
| **Date** | 2026-09-07 |
| **Repo** | `openswmm.gui` only. No engine changes. |
| **Status** | **PLAN FOR REVIEW — nothing implemented.** Decisions already taken are in §0; decisions still open are in §9. |
| **Verified against source** | 2026-09-07, HEAD `b47809d` plus the uncommitted quad-everywhere work in `meshgenerationdialog.*` (see §1.3). All `file:line` refs are to that state. |
| **Builds on** | `QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md` (§3.1 region attributes, §6.3 dialog MVC, and its explicit deferral of "drawn on the map"), `QUAD_EVERYWHERE_PLAN_2026-09-07.md` §3.5, `MIN_CELL_SIZE_ENFORCEMENT_PLAN_2026-08-17.md` Phase 6, `MESH_BOUNDARY_PATH_SELECT_PLAN_2026-08-04.md` (the house MVC template), `LOCAL_RASTER_BASEMAP_PLAN_2026-08-09.md` (the reuse-inventory format), `LAYER_STYLING_LABELING_PLAN_2026-08-16.md`, `ATTRIBUTE_CALCULATOR_PLAN_2026-08-14.md` (draft), `OPTIONS_DIALOG_TABBED_RESTRUCTURE_PLAN_2026-09-07.md` (page-class + tab-gate precedent) |

---

## 0. The two asks and the decisions already taken

> "The mesh quality tab is also entirely too long. I want to group and tabify as
> appropriate. I want to design a feature layer creation tab as it is needed to
> parameterize and delineate model domain. It can be a new tab. It should allow
> definition of attributes whether 2D/3D where 3D z values may be interpolated
> from a raster and mesh. Allow creation of polygons with holes etc."

| # | Decision | Answer (2026-09-07) |
|---|---|---|
| D1 | Where feature-layer creation lives | **A main-window compact-toolbar tab ("Features") plus a dock panel.** Drawing happens on the map canvas; the Mesh Generation dialog is modal and stays a *consumer* — it picks drawn layers from its existing layer combos and lists. |
| D2 | Storage of drawn features | **GeoPackage on disk next to the project**, one `.gpkg` per project, one table per feature layer. Persists in `.oswp` by path (the existing `gisLayers` shape), is re-openable by worker threads, and is consumed *unchanged* by mesh generation, attribute assignment, and Import Feature Layer. Cost: the repo's first OGR write path. |
| D3 | Mesh dialog Quality tab | **Nested `QTabWidget` inside the Quality tab.** Three top-level tabs stay (Sources · Quality · Hydraulics). |
| D4 | Roles the feature layer must serve this round | **All four:** (a) mesh domain boundary with holes/islands; (b) 3D breaklines and hard points; (c) refinement / quad / min-cell-size regions; (d) parameter zones (roughness, land use, infiltration) and SWMM delineation via Import Feature Layer, plus boundary-condition lines. |

Part A (§2) is small and self-contained. Part B (§3–§7) is the substantial piece.

---

## 1. Findings — what exists, what does not

### 1.1 Reuse inventory (exists — do not reimplement)

| Need | Where | Notes |
|---|---|---|
| Layer base with style / label / rule / mask / join hooks | `include/layers/openswmmvislayer.h:115`; hooks `:413-526` | All default-off, all inherited for free |
| File-backed OGR vector layer with rendering, identify, selection sync, sublayer picker | `GISVectorLayer` — `include/layers/gisvectorlayer.h:101`; `populateScene` `src/layers/gisvectorlayer.cpp:808`; `addPolygonWithHoles` `:885`; FID-tagged scene items `:779-788`; `identifyAt → QList<QVariantMap>` `gisvectorlayer.h:334`; `IAttributeProvider` `:102` | **Read-only:** opens with `GDAL_OF_READONLY` (`gisvectorlayer.cpp:1088`, `:1170`). Polygons with interior rings already render correctly. |
| Layer-tree category for feature layers | `CatFeatureLayers` — `include/ui/panels/layertreecategories.h:34-44`; unknown ordinals fall back to it (`layertreecategories.cpp:54`) | New `OpenSWMMVisLayerType` ordinal would be `15` (`openswmmvislayer.h:135-152`) |
| `.oswp` persistence of GIS layers by path | `src/project/projectserializer.cpp:1371-1450` (`{type, path, name, visible, opacity, layerName}` `:1384-1392`); relative paths via `toRelativePath` `include/project/projectserializer.h:96` | Schema version `kCurrentSchemaVersion = 4` `:44` |
| Compact-toolbar tabs, contextual tabs, ribbon groups | `CompactToolbarController::addTab(id, title, toolbars, contextual)` `include/ui/toolbars/compacttoolbarcontroller.h`; registration `src/swmmvisactions.cpp:383-404`; contextual reveal `:462-470`; tab ids `include/ui/actioncatalog.h:216-219`; `RibbonGroup` `include/ui/toolbars/ribbongroup.h:50` | Row schema `ActionCatalogEntry` `actioncatalog.h:50-59`; the constexpr table `kActionCatalog[]` `:61-212`; tests `tests/gui/test_action_registry.cpp`, `test_compact_toolbar.cpp` |
| Docks | Properties panel `src/swmmvis.cpp:3358-3360` (right); Attribute Table `:3372-3377` (bottom); Legend / Layer Styling `:2185`, `:2195` (right) | |
| Polygon draw tool (SWMM subcatchment) | `OpenSWMMVisMapToolAddSubcatchment` — `include/map/tools/maptooladdsubcatchment.h:31`; interaction contract `:23-29`; `m_vertices` `:60`, `m_snap` `:63` | Click = vertex, double-click = close, right-click = undo vertex, Enter = commit, Esc = cancel |
| Polyline draw tool | `OpenSWMMVisMapToolAddLink` — `include/map/tools/maptooladdlink.h` | |
| Vertex edit tool | `OpenSWMMVisMapToolEditVertex` — `include/map/tools/maptooleditvertex.h:41`; contract `:29-39` | Drag handle, right-click segment → insert, right-click handle → delete |
| Lasso select | `OpenSWMMVisMapToolSelectPolygon` `include/map/tools/maptoolselectpolygon.h:34` | |
| Snapping | `SnapEngine` `include/map/snapengine.h:33`; prefs `include/core/preferencesmanager.h:146-158` (`snapEnabled`, `snapTolerancePx` = 12, `snapToVertices`) | **Bound to `SWMMModelLayer*`** (`snapengine.h:57`) — needs a provider interface (§3.4) |
| Undo: layer-owned authored object idiom | `AddAnnotationCommand` `include/map/mapundostack.h:972` (doc `:964-970`); `EditVertexCommand` `:395`; `EditSubcatchCommand` `:452`; `BulkEditCommand` `:726-747`; `MapCommand` `:171`; `MapUndoStack` `:116` | Stable ids across redo/undo; keyed by name, never index (`:628-630`) |
| Polygon-with-holes semantics | `mesh::QuadRegion { ring, holes, … }` `include/mesh/meshquadregion.h:38-61`; `pointInRegion` `:74`, `validateQuadRegion` `:82`, `normalizeRingCCW` `:64`, `ringIsSimple` `:77`, `ringSignedArea` `:67`, `resampleRing` `:108` | Ring validity rules already written: `QUAD_MESHING_REDESIGN_PLAN:112-122` |
| Mesh-domain holes as seeds | `MeshGenerator::addHole(QPointF)` `include/mesh/meshgenerator.h:158`; ring→seed conversion documented `include/ui/dialogs/meshgenerationdialog.h:18-20`, `holeRings` `:86` | Interior rings of the boundary polygon become holes automatically — a drawn polygon with holes needs **no** new generator code |
| Z from raster | `GISRasterLayer::valueAt(mapX, mapY, canvasSRS, band, ok)` `include/layers/gisrasterlayer.h:165-168` (canvas CRS in, reprojects internally); thread-safe low-level `mesh::DTMSampler` `include/mesh/dtmsampler.h:22` (bilinear, raster CRS); `detectVerticalUnit()` `gisrasterlayer.h:100` | |
| Z from mesh | `SWMM2DMeshLayer::sampleZAt(sx, sy)` `include/layers/swmm2dmeshlayer.h:352-354` (barycentric, NaN outside) | |
| Scattered-point Z interpolation | `mesh::NaturalNeighbourInterpolator` `include/mesh/naturalnbinterpolator.h:46`; IDW inside the mesh pipeline | For "Z from points" (optional, §9 Q4) |
| Mesh generator inputs from files | `BoundaryKind::VectorFile` + `boundaryPath/LayerName/CRSWkt` `meshgenerationdialog.h:95-99`; point/line layer lists `:372-373`, `AuxLayerRow::useZ/is3D` `:378-383`; `QuadRegionLayerSpec` `:187-188` with per-feature `quad_mode/quad_spacing/quad_aspect/quad_angle/tag` (`QUAD_MESHING_REDESIGN_PLAN:103-105`); worker re-opens by path `meshgenerationdialog.h:89-92`, `:184-186` | A GeoPackage table is just another OGR source to all of these |
| Cell attributes from a vector layer | `MeshAttributeAssignDialog` `include/ui/dialogs/meshattributeassigndialog.h:82` (file doc `:6-48`); `Source::Vector`, `vectorPath` `:146`; sampling modes `:98-104`; classified infiltration lookup `:21-24` | Roughness / land-use / infiltration zones — consumes by path |
| Vector → SWMM objects | `ImportFeatureLayerDialog` `include/ui/dialogs/import/importfeaturelayerdialog.h:42`; `setSourceLayer(GISVectorLayer*)` `:52`; undo `include/map/importcommands.h` | Subcatchment / conduit delineation — consumes a `GISVectorLayer*` |
| Edge BC assignment on a mesh | `MeshEditingToolbar` `include/ui/toolbars/mesheditingtoolbar.h`; `MapToolMeshSelectEdge` + Ctrl-click shortest path (`MESH_BOUNDARY_PATH_SELECT_PLAN`); `MeshSetEdgeAttributeCommand` `include/map/meshcommands.h:159`; `mesh::MeshEdgeBC` `include/mesh/meshedgebc.h:23-46` | BC *lines* → edges is new (§6.4) |
| CRS | `SpatialReferenceSystem` `include/map/spatialreferencesystem.h`; `CRSSelectionDialog`; canvas CRS `MapCanvas::setCanvasSRS` `src/map/mapcanvas.cpp:365`; model CRS from `.inp` `src/layers/swmmmodellayer.cpp:1269-1279` | |
| Attribute table plumbing | `AttributeTablePanel::showLayerSource(OpenSWMMVisLayer*)` `include/ui/panels/attributetablepanel.h:86`; `GISVectorAttributeTableModel` (read-only, defined in the `.cpp`) `:42`, `:278`; `SWMMAttributeTableModel::ColumnSpec` `include/ui/panels/swmmattributetablemodel.h:93` | |
| Mesh dialog scroll idiom, worker-thread contract, `MeshRegionDefaultsWidget` | `meshgenerationdialog.cpp:2846-3952` (`buildUi`), `:3806` (`wrapInScrollArea`); worker-thread contract `meshgenerationdialog.h:10-19`; `MeshRegionDefaultsWidget` fwd-decl `:41`, member `m_regionDefaults` `:467` | |

### 1.2 Gaps (must be built)

| Gap | Nearest precedent |
|---|---|
| Editable, file-backed vector layer (OGR **write**: `CreateLayer`, `CreateFeature`, `SetFeature`, `DeleteFeature`, `CreateField`) — none exists anywhere in the tree | `GISVectorLayer` (read side) |
| A `Feature { fid; geometry; QVariantMap attrs }` value type and a `FieldDef { name; type; default }` schema | `identifyAt` output; `ColumnSpec` |
| Polygon / polyline / point **digitising into a non-SWMM layer**, "add hole", "add part", move feature | `OpenSWMMVisMapToolAddSubcatchment`, `OpenSWMMVisMapToolAddLink`, `OpenSWMMVisMapToolEditVertex` |
| Snapping to a non-SWMM layer | `SnapEngine` (signature bound to `SWMMModelLayer*`) |
| Schema-driven property adapter (runtime field list) | 32 hand-written `*PropertyAdapter` classes across 21 headers in `include/ui/properties/` |
| Editable GIS attribute table | `GISVectorAttributeTableModel` (read-only) |
| Vector export (`.shp` / `.geojson`) | none — `GDALVectorTranslate` unused |
| Per-region min-cell-size (role (c)) — today a single global spin (`m_minCellSizeSpin` `meshgenerationdialog.h:418`) | `RegionMarker { maxArea }` `meshgenerator.h:58-64` |
| BC **lines** → mesh edges (role (d)) | Ctrl-click boundary path selection |

### 1.3 The Mesh Generation dialog today

`include/ui/dialogs/meshgenerationdialog.h` (483 lines) · `src/ui/dialogs/meshgenerationdialog.cpp` (5,269 lines). `QTabWidget` at `:2853`; **no per-tab builder** — one 1,107-line `buildUi()` `:2846-3952`. Tabs: `S&ources` `:2859-3140`, `Quality` `:3148-3805`, `Hydraulics` `:3815-3880`; footer `:3887-3951`. Dialog is `540×560` (`:2825`).

**The Quality tab** — eight group boxes, 37 form rows plus a 120 px table, roughly 1,650–1,800 px tall in a 560 px dialog:

| Group | Line | Rows | Members |
|---|---|---|---|
| Triangle quality | `:3154` | 5 | `m_maxAreaSpin`, `m_minAngleSpin`, `m_gradationSpin`, `m_maxSteinerSpin`, `m_allowSteiner` |
| PSLG Optimisation | `:3211` | 3 | `m_simplifyEpsSpin`, `m_snapEpsSpin`, `m_maxBoundaryEdgeBox/Spin` |
| Minimum Cell Size | `:3271` | 8 | `m_minCellSizeSpin`, `m_minCellSuggestBtn`, `m_minSizeEnforceBox`, `m_trimAngleSpin`, `m_trimAtNodesBox`, `m_dropSubScaleHolesBox`, `m_cleanupBox`, `m_minCellDerivedLabel` (+ `syncMinCell` `:3374-3388`) |
| Terrain-Adaptive Thinning | `:3395` | 6 | `m_thinningBox`, `m_thinningToleranceSpin`, `m_thinningIterationsSpin`, `m_thinningMaxPointsSpin`, `m_minSpacingBox/Spin`, `m_boundaryBufferSpin` (+ `syncThinning` `:3487-3497`) |
| Quadrilateral cells **(uncommitted)** | `:3507` | 2 | `m_quadEverywhereCheck`, `m_quadEverywhereSpacingSpin` |
| Quad regions (PSLG) — optional overrides | `:3542` | 7 | hint + `m_quadRegionLayerCombo`, `m_quadRegionSubcatchEdit`, `m_quadRegionModeCombo`, `…SpacingSpin`, `…AspectSpin`, `…AngleSpin` |
| Quad quality | `:3635` | 6 | `m_quadMergeBox`, `m_quadMinAngleSpin`, `m_quadMaxAngleSpin`, `m_quadMinSjSpin`, `m_quadMaxAspectSpin`, `m_quadPlanaritySpin` (+ `syncQuad` `:3712-3735`) |
| Structured quad patches | `:3743` | table | hint + `m_patchTable` + three buttons |

**Constraints on Part A:**
- `meshgenerationdialog.h/.cpp` carry **uncommitted** quad-everywhere work (`git diff --stat`: 106 insertions). Part A sequences **after** that lands.
- `tests/gui/test_meshmincelldialog.cpp:98-105` finds the *Minimum Cell Size* group **by title** and asserts its widget census (2 spins, 4 checkboxes). Re-parenting the group into a tab is fine; renaming or splitting it is not.
- Persistence is one-way: `seedDefaults()` `:4061-4160` reads `PreferencesManager::twoDDefaults()` (`preferencesmanager.h:497-538`); min-cell, gradation and all quad settings are **not** persisted (`:4104-4134`, comment `:4116-4120`). Out of scope here; noted for `QUAD_MESHING_REDESIGN_PLAN §6.3`.

---

## 2. Part A — Quality tab → nested tabs

### 2.1 Layout

The `Quality` page keeps its sidebar/tab position and becomes a `QTabWidget` (`objectName` `meshQualityTabs`) with four pages, **groups moved whole, no widget renamed**:

```
Quality
  ├ Sizing          Triangle quality · PSLG Optimisation                         (8 rows)
  ├ Cell Size       Minimum Cell Size                                            (8 rows)
  ├ Terrain         Terrain-Adaptive Thinning                                    (6 rows)
  └ Quads           Quadrilateral cells · Quad regions (PSLG) — optional overrides ·
                    Quad quality · Structured quad patches                       (15 rows + table)
```

Each inner page is wrapped by `wrapInScrollArea` exactly as the outer pages are, so `Quads` — still the tallest at ~15 rows plus a table — scrolls gracefully if a platform's fonts push it past the dialog height, while the other three fit without scrolling at the dialog's default `540×560`.

**Why this split and not another:** it follows the pipeline order the user reasons in (size the triangles → enforce a floor → thin the terrain → decide on quads), it keeps the three enable-sync lambdas (`syncMinCell`, `syncThinning`, `syncQuad`) each entirely inside one page, and it puts every quad control — including the uncommitted everywhere-toggle — on one page so the `QUAD_MESHING_REDESIGN_PLAN §6.3` MVC rework has a single home to land in.

### 2.2 Gating

None new. `m_gradationSpin` stays enabled-by-`maxArea` (`:3191-3196`) — both on *Sizing*. The everywhere-toggle's effect on the region group (`syncQuad` `:3719-3730`, uncommitted) stays intra-page.

### 2.3 Code shape

Minimal-diff, per `UI_REDESIGN_ITER2_WORKPLAN:55` ("mesh dialog minimal-diff only"): extract `buildQualityTab()` from `buildUi()` `:3148-3805` as a private member returning the `QTabWidget`; inside it the eight group constructions are unchanged, only their `addWidget` targets move from one `QVBoxLayout` to four. **No page-class split** of this dialog in this round — that belongs to the `QUAD_MESHING_REDESIGN` §6.3 rework and is out of scope here.

### 2.4 Tests

- `test_meshmincelldialog` stays green unchanged (group found by title, census unchanged).
- New slot in the same file: `qualityTabsStructure()` — `findChild<QTabWidget*>("meshQualityTabs")` has the four titles in order; the *Minimum Cell Size* group's ancestor chain reaches the `Cell Size` tab page.
- New slot: `qualityTabsNoScrollAt540x560()` — for *Sizing*, *Cell Size*, *Terrain*: the inner `QScrollArea::verticalScrollBar()->maximum() == 0` at the dialog's default size. *Quads* is exempt (table).

**Estimate: 0.5–1 day**, after the quad-everywhere commit.

---

## 3. Part B — Editable feature layers: architecture

### 3.1 Model — `FeatureLayer`

```
OpenSWMMVisLayer
  └ GISVectorLayer                     (read path, rendering, identify, selection — unchanged)
       └ FeatureLayer   ★ NEW          (write path, schema, editing session, Z policy, role)
```

`FeatureLayer : public GISVectorLayer` (`include/layers/featurelayer.h`, type ordinal `15` → `CatFeatureLayers`). It opens its GeoPackage table with `GDAL_OF_UPDATE` on the GUI thread and adds:

| API | Semantics |
|---|---|
| `static FeatureLayer *create(gpkgPath, tableName, GeomType, const Schema&, const SpatialReferenceSystem *crs, bool hasZ)` | `CreateLayer` + `CreateField`s in the project `.gpkg`; returns the open layer |
| `Schema schema() const`; `addField(FieldDef)`; `removeField(name)` | `OGRLayer::CreateField` / `DeleteField`; emits `schemaChanged()` |
| `Feature feature(fid) const`; `QVector<qint64> fids() const` | reads through OGR, returns `Feature { fid; FeatureGeometry; QVariantMap attrs }` |
| `qint64 addFeature(const Feature&)`; `setGeometry(fid, geom)`; `setAttributes(fid, QVariantMap)`; `removeFeature(fid)` | Each is one OGR write, autocommit, followed by `refreshScene()` for the affected items and `featuresChanged({fids})` |
| `ZPolicy zPolicy() const` / `setZPolicy(ZPolicy)` | §4 |
| `Role role() const` / `setRole(Role)` | §6 — a label plus a schema template, not a behaviour switch |
| `QString gpkgPath() const`; `QString tableName() const` | for `.oswp` and for consumers that re-open by path |

`FeatureGeometry` (`include/geometry/featuregeometry.h`) is a small value type — `Point / LineString / Polygon (exterior + interior rings) / Multi*`, all rings `QVector<QPointF>` with an optional parallel `QVector<double> z` — with `toOGR()` / `fromOGR()`. It is the *only* place OGR geometry is converted; everything else in the GUI handles `FeatureGeometry`. Ring validation reuses `mesh::ringIsSimple`, `normalizeRingCCW`, `ringSignedArea`, `pointInRing` (§1.1).

**Threading contract (unchanged house rule):** the layer's `GDALDataset` never leaves the GUI thread. Every edit is a short autocommitted write. Workers (mesh generation, attribute assign) re-open the `.gpkg` read-only by path, exactly as they re-open shapefiles today. SQLite/GPKG permits a read-only open while the GUI holds an update handle with no transaction in flight — the plan does **not** use `StartTransaction`/`CommitTransaction` for that reason (undo granularity comes from the command stack, not from SQL transactions).

### 3.2 Views

| View | What changes |
|---|---|
| Map scene | Nothing new — `GISVectorLayer::populateScene`/`refreshScene` already draw polygons with holes and tag items by FID. `FeatureLayer` calls `refreshScene()` after each write. Selected-feature highlight exists (`:779-788`, `test_gisselectionsync.cpp`). |
| **Features dock** ★ NEW (`FeatureLayerPanel`, right dock) | Three stacked sections: **Layers** (list of `FeatureLayer`s in the active canvas, New / Duplicate / Remove / Export…); **Schema** (field table: name, type, default; Add / Remove; role template picker); **Z** (policy editor, §4). Selecting a layer makes it the *edit target* for the tools. |
| Properties panel | New `FeaturePropertyAdapter` (`include/ui/properties/featurepropertyadapter.h`) driven by the layer's `Schema` at runtime — one row per field, plus geometry summary (type, vertex count, area/length, Z range) |
| Attribute Table | `GISVectorAttributeTableModel` gains `setData` when the bound layer `qobject_cast<FeatureLayer*>` — edits go through `EditFeatureAttributesCommand` |
| Layer tree / style dialog | Free — `GISVectorLayer` already provides `styleSubjects()`, symbol JSON, labels |

### 3.3 Controllers — map tools and commands

New tools in `include/map/tools/`, each copying the interaction contract of its SWMM precedent and targeting `canvas->activeFeatureLayer()` (set by the dock):

| Tool | Precedent | Contract |
|---|---|---|
| `MapToolDrawPoint` | `OpenSWMMVisMapToolAddNode` (`maptooladdnode.h:32`) | click = feature |
| `MapToolDrawLine` | `OpenSWMMVisMapToolAddLink` (`maptooladdlink.h:36`) | click = vertex, double-click/Enter = commit, right-click = undo vertex, Esc = cancel |
| `MapToolDrawPolygon` | `OpenSWMMVisMapToolAddSubcatchment` (`maptooladdsubcatchment.h:23-29`) | same; closes the ring on commit; rejects self-intersecting rings with a status message |
| `MapToolAddHole` | `MapToolDrawPolygon` | draw a ring **inside** a selected polygon → becomes an interior ring; rejected if it crosses the exterior or another hole (`pointInRing` + `ringIsSimple`) |
| `MapToolAddPart` | `MapToolDrawPolygon`/`Line` | draw a new part appended to the selected multi-geometry (promotes single → multi) |
| `MapToolEditFeatureVertex` | `OpenSWMMVisMapToolEditVertex` (`maptooleditvertex.h:29-39`) | drag handle; right-click segment → insert; right-click handle → delete; works on every ring and part |
| `MapToolMoveFeature` | `OpenSWMMVisMapToolMoveNode` (`maptoolmovenode.h:38`) | drag whole feature |
| Select / lasso | existing `OpenSWMMVisMapToolSelect` (`maptoolselect.h:39`), `OpenSWMMVisMapToolSelectPolygon` | already sync GIS selection |

Commands in `include/map/featurecommands.h`, all `MapCommand` subclasses mirroring `AddAnnotationCommand` (`mapundostack.h:964-1000`) — the command owns a detached `Feature` while undone, the layer owns it while applied, the FID is stable across redo/undo:

`AddFeatureCommand`, `DeleteFeaturesCommand`, `EditFeatureGeometryCommand` (old/new `FeatureGeometry`, `mergeWith` for drag sequences as `MoveNodeCommand:339`), `EditFeatureAttributesCommand` (old/new `QVariantMap`), `AddFieldCommand` / `RemoveFieldCommand` (schema; `RemoveField` snapshots the column), `ResampleZCommand` (old/new Z arrays for N features; one Ctrl+Z per resample), `ImportToFeatureLayerCommand` (§6.5).

Every tool commits through a command; nothing writes to the layer directly. This is the CLAUDE.md §5.1 MVC split: the layer is the model, the dock/table/properties/scene are views on `featuresChanged`/`schemaChanged`, the tools and commands are the controller.

### 3.4 Snapping

`SnapEngine::snap(tool, SWMMModelLayer*, x, y)` (`snapengine.h:56-58`) becomes `snap(tool, const QList<ISnapProvider*>&, x, y)` with

```cpp
class ISnapProvider {  // include/map/isnapprovider.h
public:
    virtual ~ISnapProvider() = default;
    virtual bool snapCandidates(const QPointF &map, double tolMap,
                                QVector<SnapEngine::Result> &out) const = 0;
};
```

`SWMMModelLayer` implements it by delegating to today's body (behaviour identical; `Kind` gains `FeatureVertex`, `FeatureEdge`). `FeatureLayer` implements it over its FID-tagged scene items. The canvas assembles the provider list from visible layers; the existing `snapEnabled` / `snapTolerancePx` / `snapToVertices` preferences apply unchanged, plus one new preference `snapToFeatureLayers` (default on).

### 3.5 Persistence

- **Project GeoPackage:** `<project>.features.gpkg` beside the `.inp` (path from `ProjectSerializer::sidecarPathFor` `projectserializer.h:80` with a different suffix). Created lazily on the first *New feature layer*. One OGR layer (table) per `FeatureLayer`; the table name is the layer name sanitised, unique within the file.
- **`.oswp`:** a `gisLayers` entry `{type:"feature", path, layerName, name, visible, opacity, role, zPolicy{…}, symbol}` — the existing `serializeGisLayer` shape (`projectserializer.cpp:1384-1392`) plus three fields. `kCurrentSchemaVersion` → 5; loaders of 4 ignore the new type gracefully (unknown `type` already falls through). *Save As* moves the `.gpkg` with the project via `saveaspathnormalizer` (`include/project/saveaspathnormalizer.h`).
- **Export:** `Export layer…` on the dock runs `GDALVectorTranslate` to Shapefile / GeoJSON / standalone GeoPackage. Import of an existing OGR layer *into* a feature layer (to make it editable) is `ImportToFeatureLayerCommand` (§6.5).

---

## 4. Z: 2D and 3D features, Z from raster and from mesh

### 4.1 Policy, per layer

```cpp
struct ZPolicy {
    enum class Source { None, Constant, Raster, Mesh };
    Source  source     = Source::None;    // None ⇒ 2D layer (wkbPolygon, not wkbPolygon25D)
    QString sourceLayerId;                // GISRasterLayer or SWMM2DMeshLayer layerId()
    int     rasterBand = 1;
    double  constant   = 0.0;
    double  densifySpacing = 0.0;         // > 0 ⇒ insert vertices every d map units before sampling
    double  zScale     = 1.0;             // vertical unit conversion (raster ft → model m, etc.)
    bool    resampleOnEdit = true;        // re-sample a vertex's Z when it is moved/inserted
};
```

A layer is **2D** (`Source::None`) or **3D** (any other source). The dimension is fixed at creation because it decides the OGR geometry type (`wkbPolygon` vs `wkbPolygon25D`); the *source* may be changed afterwards and followed by *Resample Z*.

### 4.2 Sampling

| Source | Call | Notes |
|---|---|---|
| Raster | `GISRasterLayer::valueAt(x, y, canvasSRS, band, &ok)` (`gisrasterlayer.h:165`) | Canvas-CRS in; reprojects internally; `ok=false` → NaN. `zScale` defaults from `detectVerticalUnit()` vs the model's unit system. |
| Mesh | `SWMM2DMeshLayer::sampleZAt(sx, sy)` (`swmm2dmeshlayer.h:352`) | Barycentric; NaN outside every cell |
| Constant | — | |

A NaN sample leaves the vertex's Z at NaN and marks the feature in the dock (count of un-sampled vertices) — never silently zero. The mesh pipeline already treats `hasZ=false` seeds correctly (`meshgenerator.h:50-51`), so a partially-sampled breakline degrades to its 2D constraint rather than failing.

### 4.3 Densification

A 3D breakline drawn with three clicks across a valley must follow the terrain, not chord it. With `densifySpacing > 0`, `Resample Z` (and commit, when `resampleOnEdit`) inserts intermediate vertices along each segment at that spacing **before** sampling. The inserted vertices are real (stored) vertices so the mesh generator sees them as constraint-segment endpoints — which is also why the spacing must respect the mesh minimum cell size: the dock shows the active mesh `minCellSize` next to the spacing spin and warns when spacing < *h* (the `MIN_CELL_SIZE_ENFORCEMENT_PLAN:40` hazard, "GIS-digitised … 3D breaklines routinely carry vertices centimetres apart").

### 4.4 Interaction

- Drawing on a 3D layer samples Z per committed vertex (tool status bar shows the live sampled Z under the cursor, reusing the hover-Z probe idiom `include/mesh/meshhoverprobe.h`).
- `Resample Z` (ribbon button; selected features or whole layer) is one `ResampleZCommand`.
- The property adapter shows per-vertex Z read-only and a per-feature "Z: min / max / n unsampled".

> **Amendment 2026-09-14 — per-vertex X, Y and Z are editable.** At the user's
> request, coordinates are no longer read-only. The Features dock gains a
> **Vertices** section: one row per vertex of the single selected feature,
> addressed by (part, ring, index) so interior rings and multi-part geometries
> are reachable, with X, Y and Z all typeable inside an edit session. Each edit
> is one non-mergeable `EditFeatureGeometryCommand`, so a typed coordinate is
> the same undo step as the same move made by dragging — the §3.3 rule that
> nothing writes to the layer outside a command is unchanged.
>
> Three consequences worth recording:
>
> - An X/Y edit is gated on `FeatureGeometry::validate` against the layer's
>   geometry type, exactly as the draw tools gate a commit. A move that
>   self-intersects a ring or pushes a hole out of its exterior is refused and
>   the cell reverts.
> - When the layer is 3D and `zPolicy.resampleOnEdit` is set, changing X or Y
>   re-samples that vertex's Z, matching `maptoolfeatureedit.cpp`'s vertex-drag
>   path. A hand-typed Z is therefore kept only until the next resample; the
>   dock says so rather than pretending otherwise (the alternative — a
>   per-vertex "pinned" flag in `Ring` — was rejected as a model change with
>   GeoPackage persistence implications for a UI convenience).
> - The grid refuses to populate above 10 000 vertices; those geometries are
>   edited on the map.
>
> This does **not** revive `FeaturePropertyAdapter`, which is still unbuilt —
> see the B6 note in §7.

---

## 5. UI — the "Features" tab and the dock

### 5.1 Compact-toolbar tab

`mCompactToolbar->addTab("features", tr("Features"), {mToolBarFeatures})` in `src/swmmvisactions.cpp` next to `:383-404`; **non-contextual** (always visible — the tab is how you *start*). Tab id added to `kActionCatalogTabs` (`actioncatalog.h:216-219`); every action below is a `kActionCatalog` row with `tab = "features"` so `test_action_registry` covers them. Ribbon groups (`RibbonGroup`):

| Group | Actions |
|---|---|
| **Layer** | New feature layer… · Import to feature layer… · Export layer… · Remove layer |
| **Draw** | Point · Line · Polygon · Add hole · Add part (checkable, mutually exclusive, like the Model tab's add-node/link/subcatchment) |
| **Edit** | Edit vertices · Move · Delete selected · Snapping (toggle) |
| **Z** | Resample Z · (label) current Z source |
| **Use as** | Mesh boundary · Breaklines · Regions · Assign cell attributes… · Import to SWMM… · Assign BC to edges… (§6) |

The *Draw* and *Edit* actions are enabled only while a `FeatureLayer` is the active edit target; *Add hole* / *Add part* additionally require a selected polygon/line. The mesh2d/terrain contextual-tab reveal (`swmmvisactions.cpp:462-470`) is left alone.

### 5.2 New-layer dialog

`NewFeatureLayerDialog` (modal, small): name · geometry type (Point / Line / Polygon; multi allowed) · **role template** (§6) which pre-fills the schema · schema table (editable) · Z (2D / 3D + source picker listing the canvas's raster and mesh layers) · CRS (read-only: the canvas CRS; the `.gpkg` table is created in it).

### 5.3 Features dock

`FeatureLayerPanel` (right dock, `objectName` `dockWidgetFeatures`, tabbed with Properties by default). Sections: Layers list (edit target) · Schema editor · Z policy · a one-line status ("12 features · 3 with unsampled Z"). It is a *view*: it rebuilds from `featuresChanged` / `schemaChanged` / `layerAdded` / `layerRemoved` and never touches OGR itself.

---

## 6. Roles — how a drawn layer parameterises and delineates the domain

A **role** is a schema template plus the label shown in the dock; the layer stays a plain OGR table, so any consumer that takes "a vector layer" already works. The *Use as* ribbon group is a set of shortcuts that open the existing consumer pre-selected.

| Role | Geometry | Template fields | Consumer (existing unless ★) |
|---|---|---|---|
| **Domain boundary** | Polygon (+holes, +multipart) | `name` | Mesh dialog › Sources › *Domain* combo (`m_boundaryLayerCombo` `meshgenerationdialog.h:371`, `BoundaryKind::VectorFile`). Interior rings → holes automatically (`meshgenerationdialog.h:18-20`). Multipart → `setDomains`. |
| **Breaklines / hard points** | Line, Point (3D) | `tag`, `marker` | Mesh dialog › Sources › line/point layer lists (`m_lineLayersList` / `m_pointLayersList`, `AuxLayerRow::useZ`). The `is3D` flag comes from the OGR geometry type, so a 3D feature layer is offered "Use Z" automatically. |
| **Regions** | Polygon (+holes) | `max_area`, `min_cell` ★, `quad_mode`, `quad_spacing`, `quad_aspect`, `quad_angle`, `tag` | Quad: `m_quadRegionLayerCombo` (per-feature overrides per `QUAD_MESHING_REDESIGN_PLAN:103-105`). Refinement: ★ new — the worker reads `max_area` → `RegionMarker`. Min cell: ★ new — per-region `MinSizePolicy` (`pslgminsize.h`) keyed by `tag`; the global spin becomes the default. |
| **Parameter zones** | Polygon | `mannings_n`, `init_depth`, `landuse`, `hsg`, `infil_method`… | `MeshAttributeAssignDialog` with `Source::Vector`, `vectorPath` = the `.gpkg` (`meshattributeassigndialog.h:146`); classified lookup for `landuse × hsg` (`:21-24`) |
| **SWMM delineation** | Polygon / Line / Point | per target (subcatchment: `name`, `outlet`, `area`…; conduit: `name`, `from`, `to`…) | `ImportFeatureLayerDialog::setSourceLayer(FeatureLayer*)` (`importfeaturelayerdialog.h:52`) — a `FeatureLayer` *is a* `GISVectorLayer`, no change |
| **Boundary-condition lines** | Line | `bc_type`, `head`, `flow`, `tseries`, `curve`, `group`, `conveyance` (mirrors `mesh::MeshEdgeBC` `meshedgebc.h:23-46`) | ★ new `AssignBCFromLinesCommand`: for each line, the boundary edges within a tolerance band (default: half the local edge length) get the row's BC, as one undo macro of `MeshSetEdgeAttributeCommand`s (`meshcommands.h:159`). Uses the boundary graph (`meshboundarygraph.h`) to prefer a connected run. |

### 6.1 Mesh dialog changes (consumer side, all small)

- Layer combos/lists already enumerate `GISVectorLayer`s (`populateLayerCombos()` `meshgenerationdialog.cpp:4196`); `FeatureLayer` instances appear automatically. Add a role glyph in the combo text ("⬚ domain", "⟋ breakline") when the layer is a `FeatureLayer` with a role.
- Worker: `PipelineInputs` gains `refinementRegionLayers` (path, layerName, crsWkt — same shape as `QuadRegionLayerSpec` `:187-188`) and `minSizeRegionLayers`; two new loops in `runMeshPipeline` map `max_area` → `addRegion` and `min_cell` → a per-region policy. This is the only generator-facing code in the plan.

### 6.2 Refinement and min-cell regions (★)

The worker (`runMeshPipeline`) gains two loops next to the quad-region loop: for each polygon in a *Regions* layer, `max_area > 0` → `MeshGenerator::addRegion(RegionMarker{centroid, attribute, maxArea, tag})` (`meshgenerator.h:58-64`, `:159`); `min_cell > 0` → a per-region `mesh::pslg::MinSizePolicy` keyed by `tag`, with the global `m_minCellSizeSpin` as the default (subject to §9 Q3). Validation reuses the quad-region rules (`QUAD_MESHING_REDESIGN_PLAN:112-122`): simple, CCW, inside a domain, not crossing a hole, pairwise non-overlapping; failures are logged with the feature's `tag`/FID and skipped, never fatal.

### 6.3 Parameter zones

No new code beyond the *Use as → Assign cell attributes…* shortcut, which opens `MeshAttributeAssignDialog` with `Source::Vector` and `vectorPath` pre-set to the layer's `.gpkg` and its table. The role template's field names (`mannings_n`, `init_depth`, `landuse`, `hsg`) match what the dialog's classified lookup expects, so a freshly drawn zone layer works with the dialog's defaults.

### 6.4 Boundary-condition lines (★)

`AssignBCFromLinesCommand` (`featurecommands.h`): input = a *BC lines* feature layer and the active `SWMM2DMeshLayer`. For each line feature: collect boundary edges (`SWMM2DMeshLayer::isBoundaryEdge` `swmm2dmeshlayer.h:360`) whose midpoint lies within `tolerance` of the line (default: half the median boundary-edge length; overridable in a small confirm dialog that previews the edge count per line); if the collected edges are not one connected run on the boundary graph (`meshboundarygraph.h:36-39`), bridge gaps of ≤ 2 edges and report the rest. Each line's attributes map onto `mesh::MeshEdgeBC` (`meshedgebc.h:23-46`) — `bc_type`, `head`, `slope`, `flow`, `tseries`, `curve`, `group`, `conveyance` — and are applied as one undo macro of `MeshSetEdgeAttributeCommand`s (`meshcommands.h:159`), so one Ctrl+Z reverts the whole assignment. Re-running after a re-mesh is the intended workflow: the lines persist, the edges are recomputed.

### 6.5 Import to feature layer

`ImportToFeatureLayerCommand`: copy an OGR layer (any `GISVectorLayer`) or a SWMM object class (subcatchment polygons, conduit polylines, junction/outfall points, with their key attributes) into a **new** editable feature layer in the project `.gpkg`, so users start from what they already have and then edit. Geometry is reprojected to the canvas CRS on the way in. One undo step removes the created layer. The SWMM direction (`FeatureLayer` → SWMM objects) is the existing `ImportFeatureLayerDialog` and needs no change.

---

## 7. Phasing

Part A is independent of Part B and can go first. Part B phases each end green on §8 and are committed separately (hunk-wise where files are shared).

| Phase | Deliverable | Est. |
|---|---|---|
| **A** | Quality tab → 4 nested tabs; `buildQualityTab()`; 2 test slots. **After** the quad-everywhere commit. | 0.5–1 d |
| **B0** | `FeatureGeometry` + ring validation wrappers; **leaf unit test** (`tests/unit/test_featuregeometry.cpp`): holes, multipart, Z arrays, OGR round trip. | 1 d |
| **B1** | `FeatureStore` — the OGR write path (create `.gpkg`, create table with schema + CRS + Z, add/set/delete feature, add/delete field); leaf unit test with a real `.gpkg` under `tests/unit/data/`. | 1.5 d |
| **B2** | `FeatureLayer` (subclass, type 15, `featuresChanged`/`schemaChanged`), `.oswp` `type:"feature"` round trip (schema v5), Save-As path move; GUI test: create → save → reopen → same features. | 1.5 d |
| **B3** | Commands (`featurecommands.h`) + `MapToolDrawPoint/Line/Polygon` + `NewFeatureLayerDialog` + the *Features* tab (Layer, Draw groups) + `FeatureLayerPanel` (Layers + Schema sections). GUI test drives the polygon tool with `QTest::mouseClick` on the canvas viewport (the only existing precedent is `tests/gui/test_asyncload.cpp`; `test_gisselectionsync.cpp` is the pattern for asserting the resulting GIS selection state) and asserts one polygon in the `.gpkg` and one undo step. | 3 d |
| **B4** | `MapToolAddHole`, `MapToolAddPart`, `MapToolEditFeatureVertex`, `MapToolMoveFeature`; Edit group; `ISnapProvider` refactor of `SnapEngine` (+ `snapToFeatureLayers` pref). Tests: hole inside/outside rejection; vertex drag undo; SWMM snapping behaviour unchanged (`test_gisselectionsync`/existing snap tests green). | 2.5 d |
| **B5** | Z: `ZPolicy`, raster/mesh sampling, densification, `ResampleZCommand`, Z section of the dock, live Z readout. Test: 3D line over a synthetic raster (`tests/gui/data/`) → Z within 1e-6 of the analytic surface; NaN outside coverage reported not zeroed. | 2 d |
| **B6** | Views: `FeaturePropertyAdapter` (schema-driven), editable `GISVectorAttributeTableModel` for `FeatureLayer`. Tests: edit a cell → `EditFeatureAttributesCommand` → undo restores. | 1.5 d |

> **B6 status, 2026-09-14 — partially landed.** The editable
> `GISVectorAttributeTableModel` shipped (`attributetablepanel.cpp`, `setData`
> → `EditFeatureAttributesCommand`), and the Features dock's own feature grid
> covers the same ground for the active layer. **`FeaturePropertyAdapter` was
> never written** — no adapter in `include/ui/properties/` references
> `FeatureLayer`. The editable **Vertices** grid (§4.4 amendment) landed in the
> dock instead, because coordinates are a table and a property sheet is a poor
> shape for a 200-vertex ring. A schema-driven property adapter remains open.
>
> Gate 7 (`test_featureviews`) is also still unbuilt, and no test anywhere
> constructs a `FeatureLayer` or a `FeatureLayerPanel` — the dock's edit paths
> are covered only by the model-level invariants in
> `tests/unit/test_featuregeometry.cpp`. Building that rig is the next thing
> worth doing in B6.
| **B7** | Roles: templates in `NewFeatureLayerDialog`; *Use as* group; mesh-dialog role glyphs; ★ `max_area` / `min_cell` region loops in the worker; ★ `AssignBCFromLinesCommand`. Tests: a drawn boundary with one hole → mesh with one hole (`test_meshautocouple` fixture style); region `max_area` honoured (cell-area stats via `mesh::computeCellAreaStats`); BC line → N edges tagged, one undo. | 3 d |
| **B8** | `ImportToFeatureLayerCommand` (OGR layer / SWMM class → feature layer); `Export layer…` (`GDALVectorTranslate`). Tests: shapefile → feature layer → export → identical geometry count/type. | 1.5 d |
| **B9** | Docs (`docs/manual/` new chapter *Feature Layers*; `19_2d_mesh.md` Sources/Quality sections; `images/TODO.md`), `CHANGELOG.md`. | 1 d |

**≈ 19–20 days** total; Part A 1 d, Part B 18–19 d. B0–B2 are leaf-testable and carry no UI; if the plan is cut short after B3, the tree has a working "draw polygons with attributes into a GeoPackage and mesh from them" feature and nothing dangling.

---

## 8. Verification gate

| # | Check | Phase |
|---|---|---|
| 1 | `test_meshmincelldialog` unchanged and green; new `qualityTabsStructure`, `qualityTabsNoScrollAt540x560` | A |
| 2 | `test_featuregeometry` (leaf): ring validity, hole containment, multipart, Z round trip through OGR | B0+ |
| 3 | `test_featurestore` (leaf): `.gpkg` create/write/read/delete; schema add/remove; CRS written; 25D geometry type for 3D | B1+ |
| 4 | `test_featurelayer_roundtrip` (full-app): create → `.oswp` save → reopen → features, schema, role, zPolicy identical; Save-As moves the `.gpkg` | B2+ |
| 5 | `test_featuretools` (full-app): draw polygon; add hole inside (accepted) / crossing (rejected); vertex drag + undo; delete + undo; snapping to a SWMM node and to a feature vertex | B3–B4 |
| 6 | `test_featurez` (full-app): raster and mesh sampling accuracy; densification vertex count; NaN handling; `ResampleZCommand` undo | B5 |
| 7 | `test_featureviews`: attribute-table edit → command → undo; property adapter reflects schema change | B6 |
| 8 | `test_featureroles`: boundary-with-hole meshes; `max_area` region honoured; BC line assigns edges; Import Feature Layer accepts a `FeatureLayer` unchanged | B7 |
| 9 | Existing: `test_gisvectorpopulate`, `test_gisselectionsync`, `test_action_registry`, `test_compact_toolbar`, `test_meshautocouple`, `test_meshinpbcroundtrip`, full `-L gui` | every phase |
| 10 | Manual: draw a domain with an island, two 3D breaklines over a DEM, one refinement region, one roughness zone, one BC line; generate; inspect in the mesh layer's metadata (`SWMM2DMeshLayer::extendedMetadata` `swmm2dmeshlayer.cpp:846-916`) | B7 |

All test fixtures and generated `.gpkg` files live under `tests/unit/data/` or `tests/gui/data/` (CLAUDE.md §4.1).

---

## 9. Decisions still open

| # | Question | Recommendation |
|---|---|---|
| Q1 | **Dock placement:** tab the Features dock with Properties (right) or with Attribute Table (bottom)? | Right, tabbed with Properties — schema editing is property-like and the bottom dock is wide-and-short. |
| Q2 | **Multipart in this round:** ship `MapToolAddPart` in B4, or defer multipart to storage/rendering only (draw single-part, import multi)? | Ship in B4 — it is ~150 lines once *Add hole* exists and the domain-with-islands case wants it. |
| Q3 | **Per-region min cell size (★ role (c))** is the only item that touches `pslgminsize.h` semantics (a policy per region rather than one global). Ship in B7, or defer to the `MESH_MINSIZE_ENFORCEMENT_V2` track and keep `min_cell` in the template as "reserved"? | Defer to that track; keep the field so layers made now carry it. Removes the one risky generator change from B7. |
| Q4 | **Z from scattered points** (a fourth `ZPolicy::Source::Points` via `NaturalNeighbourInterpolator`)? | Not this round — raster and mesh cover the stated need. |
| Q5 | **Ownership of the project `.gpkg` on *File → New* and on models without an `.oswp`:** create beside the `.inp` regardless, or refuse to create feature layers until the project is saved? | Refuse until saved, with a one-line prompt to save — avoids orphaned `.gpkg` files in temp directories. |
| Q6 | **`SnapEngine` refactor scope:** provider interface (recommended, B4) or a minimal second overload for `FeatureLayer*`? | Provider interface — it also unblocks snapping to plain `GISVectorLayer`s later at no cost. |

---

## 10. Risks

| Risk | Mitigation |
|---|---|
| First OGR write path in the repo; GPKG/SQLite locking between the GUI's update handle and a worker's read-only handle | Autocommit per command, no long transactions; B1 test opens a second read-only handle mid-session and reads; worker re-open by path is the existing rule |
| `meshgenerationdialog.*` is dirty with another session's work | Part A waits for that commit; Part B's dialog edits (§6.1) are ~40 lines in `populateLayerCombos` and the worker, staged hunk-wise |
| `GISVectorLayer::populateScene` rebuilds all items on `refreshScene()`; per-edit rebuild is O(N) | Acceptable for authored layers (10²–10⁴ features); if profiling in B3 shows lag on drag, add `refreshFeature(fid)` that replaces one item — the FID tagging already exists |
| Snapping refactor regresses SWMM tool behaviour | `SWMMModelLayer`'s provider is a pure delegation of today's body; existing snap tests must stay green before feature-layer snapping is added |
| Densified 3D breaklines create sub-cell constraint segments | Dock warns when `densifySpacing < minCellSize`; `MIN_CELL_SIZE` enforcement already handles the residual |
| `.oswp` schema bump (4 → 5) | Additive only; unknown `type` is skipped by the current loader |
| Scope creep toward a GIS editor | Non-goals: topology editing across features, attribute joins, expression-based field calculation (that is `ATTRIBUTE_CALCULATOR_PLAN`), raster digitising, curve/arc geometry |
