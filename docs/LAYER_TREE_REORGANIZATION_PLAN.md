# Layer Tree Reorganization Plan

**Author:** Caleb Buahin
**Date:** 2026-05-30
**Status:** PROPOSAL — pending review before implementation
**Scope:** `openswmm.gui` — layer-tree panel categorisation only. No paint /
renderer / persistence changes.

---

## 1. Goal

Re-bucket the layer tree so that:

1. **2D meshes** (`SWMM2DMeshLayer`) live in their own dedicated category,
   separated from generic feature layers. The category and per-layer icons
   reuse the **Generate Mesh** toolbar icon (`:/swmmvis/CreateMesh`,
   `images/create_mesh.svg`).
2. **SWMM outputs** are split into **two dedicated groups**:
   - **1D Outputs** — `SWMMResultsLayer` (existing `.out` results) — uses
     the existing `:/swmmvis/Chart` icon.
   - **2D Outputs** — `SWMM2DResultsLayer` (mesh-based depth / velocity
     heatmaps) — uses `:/swmmvis/CreateMesh`, matching the mesh layer.
3. **Visualizable sublayers are exposed in the tree** under both 1D and 2D
   output layers, each with an independent visibility checkbox, opacity
   spinner, and per-sublayer style entry point — mirroring how the SWMM
   model layer's per-category kind rows behave today.
   - **1D** (`SWMMResultsLayer`) — per-category result sublayers
     (Subcatchments, Conduits, Pumps, Orifices, Weirs, Outlets, Junctions,
     Outfalls, Storage, Dividers, RainGages).
   - **2D** (`SWMM2DResultsLayer`) — paint-pass sublayers (Mesh Fill,
     Depth Color Ramp, Filled Contours, Isolines, Velocity Vectors, and
     optionally Mesh Edges, Mesh Nodes, Flow Arrows).

The intent is to remove user confusion between (a) plain GIS vectors and
(b) the SWMM mesh / 2D simulation surface, to make the distinction between
1D pipe-network results and 2D overland results immediately legible from
the tree icons, and to give users a single, obvious place to toggle each
visualizable component of a result layer without opening a dialog.

---

## 2. Current State (verified)

`src/ui/panels/layertreepanel.cpp`, anonymous-namespace section at the top
of the file, defines:

```cpp
enum CategoryId {
    CatSwmm = 0,
    CatSwmmOutputs,   // .out results — 1D
    CatFeatureLayers,
    CatRasterLayers,
    CatBasemaps,
    CatTables,
    CatCount
};
```

`categoryFor()` has explicit cases for:
- `SWMMModelLayer` → `CatSwmm`
- `SWMMResultsLayer` → `CatSwmmOutputs`
- vector / GIS / sub-project → `CatFeatureLayers`
- raster → `CatRasterLayers`
- imagery / WMS / WMTS → `CatBasemaps`
- tabular / time-series → `CatTables`

**`SWMM2DMeshLayer`, `SWMM2DResultsLayer`, and `SWMMAnnotationLayer` are
NOT in the switch.** They hit the `default:` branch, log a
`qWarning("unclassified layer type")`, and fall into **Feature Layers**.
This is the root of the confusion the user is reporting.

Per-layer icons in the `Qt::DecorationRole` branch of `data()` (around
lines 542–569) also have no case for the 2D types — they fall through to
the generic `:/swmmvis/Layers` icon.

The Generate Mesh action (`actionGenerateMesh` in `forms/swmmvis.ui` line
1693) uses `:/swmmvis/CreateMesh` (alias of `images/create_mesh.svg` per
`resources/swmmvis.qrc` line 79). This is the icon to reuse.

---

## 3. Proposed Tree Layout (after change)

```
Root
├── SWMM                  (:/swmmvis/Layers)        — model network
├── Meshes                (:/swmmvis/CreateMesh)    — 2D mesh layers   ← NEW
├── SWMM 1D Outputs       (:/swmmvis/Chart)         — .out 1D results  ← RENAMED
├── SWMM 2D Outputs       (:/swmmvis/CreateMesh)    — mesh-based results ← NEW
├── Feature Layers        (:/swmmvis/AddVector)
├── Raster Layers         (:/swmmvis/AddRaster)
├── Basemaps              (:/swmmvis/Globe)
└── Tables                (:/swmmvis/TableView)
```

Display order is the new `m_categoryDisplayOrder` default. Empty categories
are still suppressed (existing behaviour preserved by `rebuildCategories`).

> **Open question for review:** the user wrote "dedicated layer grouping"
> for SWMM outputs. Two readings:
> - **(A)** two sibling top-level groups (above), or
> - **(B)** one "SWMM Outputs" parent with "1D" and "2D" subgroups.
>
> The current tree model is two-level (`Category → Layer`) with the third
> level reserved for kind- and sublayer-rows. Option (B) would require a
> 4-level model, which is a much larger change. **Recommendation: (A).** If
> (B) is required, that's a separate slice and this plan should be paused.

---

## 4. Implementation Steps

All edits are confined to **two files**:

- `src/ui/panels/layertreepanel.cpp` — category enum, mapping, icons.
- `resources/swmmvis.qrc` — no change needed (icons already registered).

### 4.1 Extend the `CategoryId` enum

In the anonymous namespace at the top of `layertreepanel.cpp`:

```cpp
enum CategoryId {
    CatSwmm = 0,
    CatMeshes,         // NEW — 2D triangular meshes
    CatSwmm1DOutputs,  // RENAMED from CatSwmmOutputs — 1D .out results
    CatSwmm2DOutputs,  // NEW — 2D mesh-based results
    CatFeatureLayers,
    CatRasterLayers,
    CatBasemaps,
    CatTables,
    CatCount
};
```

`CatSwmm1DOutputs` keeps the old ordinal value (1) so that any persisted
`m_categoryDisplayOrder` from a prior session continues to point at the
"1D outputs" bucket. New ordinals for `CatMeshes` and `CatSwmm2DOutputs`
are appended; absent ordinals from older session state fall through the
"not seen" branch in `rebuildCategories()` and append cleanly.

### 4.2 Update `categoryFor()`

```cpp
case L::SWMMModelLayer:            return CatSwmm;
case L::SWMMResultsLayer:          return CatSwmm1DOutputs;
case L::SWMM2DMeshLayer:           return CatMeshes;          // was default-warn
case L::SWMM2DResultsLayer:        return CatSwmm2DOutputs;   // was default-warn
case L::SWMMAnnotationLayer:       return CatFeatureLayers;   // explicit, removes the warn
```

Annotation handling is included to silence the unrelated `unclassified
layer type` warning while we're in the switch — no behaviour change.

### 4.3 Update `categoryInfo()`

```cpp
case CatSwmm:            return {"SWMM",             ":/swmmvis/Layers"};
case CatMeshes:          return {"Meshes",           ":/swmmvis/CreateMesh"};
case CatSwmm1DOutputs:   return {"SWMM 1D Outputs",  ":/swmmvis/Chart"};
case CatSwmm2DOutputs:   return {"SWMM 2D Outputs",  ":/swmmvis/CreateMesh"};
case CatFeatureLayers:   return {"Feature Layers",   ":/swmmvis/AddVector"};
case CatRasterLayers:    return {"Raster Layers",    ":/swmmvis/AddRaster"};
case CatBasemaps:        return {"Basemaps",         ":/swmmvis/Globe"};
case CatTables:          return {"Tables",           ":/swmmvis/TableView"};
```

### 4.4 Per-layer icons in `data()` (DecorationRole switch)

Add the two missing cases (around lines 542–569 of the existing file):

```cpp
case OpenSWMMVisLayer::SWMM2DMeshLayer:
    return QIcon(QStringLiteral(":/swmmvis/CreateMesh"));
case OpenSWMMVisLayer::SWMM2DResultsLayer:
    return QIcon(QStringLiteral(":/swmmvis/CreateMesh"));
```

This ensures both the category header *and* the individual mesh / 2D
result rows use the mesh glyph.

### 4.5 Default `m_categoryDisplayOrder` is enum-driven

`LayerTreeModel::LayerTreeModel()` already initialises the order from
`CatCount`, so no constructor change is needed — the new categories slot
into the default display order automatically.

### 4.6 Re-enable sublayer rows for 1D and 2D output layers

**Background.** The infrastructure for 3rd-level sublayer rows already
exists in `LayerTreeModel` (`SublayerRow` storage, `m_sublayerRowPtrSet`,
the `data() / setData() / flags()` branches around lines 438–676). It is
currently *empty* — `rebuildSublayerRows()` is a no-op per the
`RENDERING_RULE_MODEL_PLAN.md` decision to withdraw sublayers from the
user-facing surface in favour of the Rule List. The user direction
overrides that for output layers: the Rule List remains for styling, but
the tree once again exposes a per-sublayer checkbox row.

Both target layers already implement `ISublayerHost::sublayers()`:

| Layer | Sublayers returned (paint order, bottom-up) |
|---|---|
| `SWMMResultsLayer` | 11 × `FeatureSublayer`, one per `SWMMModelLayer::Category` (Subcatchments → RainGages, see `swmmresultslayer.h` §"ISublayerHost interface") |
| `SWMM2DResultsLayer` | `MeshFillSublayer`, `DepthColorRampSublayer`, `ContourBandSublayer`, `IsolineSublayer`, `VelocityVectorSublayer` (+ `MeshEdgeSublayer`, `MeshNodeSublayer`, `FlowArrowSublayer` available via accessors) |

Each sublayer exposes `displayName()`, `isVisible() / setVisible()`,
`opacity() / setOpacity()`, `isDynamic()`, and a `styleChanged()` /
`invalidated()` signal pair — exactly what the existing
`SublayerRow` data path already reads (`layertreepanel.cpp` lines
438–467). The wiring is already complete; only the storage population is
missing.

**Change.** Rewrite `rebuildSublayerRows()` to allow sublayer rows under
hosts of the two target output layer types, and only those:

```cpp
void LayerTreeModel::rebuildSublayerRows()
{
    m_sublayerRowStorage.clear();
    m_sublayerRowPtrSet.clear();

    for (const Category &cat : m_categories) {
        for (OpenSWMMVisLayer *layer : cat.layers) {
            // Skip layers that already carry kind sub-rows (SWMMModelLayer),
            // preserving the mutual-exclusion contract called out in the
            // existing rowCount() comment.
            if (m_kindRowStorage.contains(layer))
                continue;

            // Opt-in by layer type — keeps the user-facing surface narrow
            // and matches the explicit request: 1D + 2D output sublayers.
            auto *host = dynamic_cast<OpenSWMM::Render::ISublayerHost *>(layer);
            const auto t = layer->layerType();
            const bool eligible =
                host && (t == OpenSWMMVisLayer::SWMMResultsLayer ||
                         t == OpenSWMMVisLayer::SWMM2DResultsLayer);
            if (!eligible)
                continue;

            const auto subs = host->sublayers();   // paint order, bottom-up
            // Display order = TOP-of-paint-stack first, matching how the
            // tree shows top-of-canvas first for categories and layers.
            auto &storage = m_sublayerRowStorage[layer];
            storage.reserve(subs.size());
            for (auto it = subs.rbegin(); it != subs.rend(); ++it) {
                SublayerRow row{layer, *it};
                storage.push_back(row);
                m_sublayerRowPtrSet.insert(
                    static_cast<const void *>(&storage.back()));
            }

            // Live update: re-emit dataChanged when any sublayer of this
            // host invalidates (style edit, animation tick, opacity change).
            // Disconnect-then-connect keeps repeated rebuilds idempotent
            // (Qt 6 asserts on UniqueConnection with non-PMF slots).
            for (auto *sub : subs) {
                QObject::disconnect(sub, nullptr, this, nullptr);
                QObject::connect(sub, &OpenSWMM::Render::ISublayer::invalidated,
                                 this, [this, layer]() {
                    const int catIdx = m_layerToCategory.value(layer, -1);
                    if (catIdx < 0) return;
                    const int layerRow =
                        m_categories[catIdx].layers.indexOf(layer);
                    if (layerRow < 0) return;
                    const int rows = rowCount(
                        createIndex(layerRow, 0, layer));
                    if (rows <= 0) return;
                    const QModelIndex top    = createIndex(0, 0, layer);
                    const QModelIndex bottom = createIndex(rows - 1,
                        columnCount() - 1, layer);
                    emit dataChanged(top, bottom);
                });
            }
        }
    }
}
```

This relies entirely on the existing `SublayerRow` storage, the existing
`data() / setData() / flags()` branches, and the existing per-sublayer
icons (`:/swmmvis/Layers`). It does **not** add new model types or
modify the host layers' implementations.

### 4.7 Per-sublayer icons (small follow-on in `data()`)

The current sublayer DecorationRole returns `:/swmmvis/Layers` for all
sublayers (line 450). Replace that with a small dispatch so the new rows
are immediately recognisable:

| Sublayer class | Icon alias |
|---|---|
| `FeatureSublayer` (1D, per Category) | reuse the kind icons that `SWMMModelLayer::kindIconAlias()` already emits for the matching `Category` (Junction, Conduit, Pump, Weir, Subcatchment, …) |
| `MeshFillSublayer`, `MeshEdgeSublayer`, `MeshNodeSublayer` | `:/swmmvis/CreateMesh` |
| `DepthColorRampSublayer` | `:/swmmvis/Droplet` |
| `ContourBandSublayer`, `IsolineSublayer` | `:/swmmvis/Profile` |
| `VelocityVectorSublayer`, `FlowArrowSublayer` | `:/swmmvis/Move` |

All listed aliases are already in `resources/swmmvis.qrc`. No new SVGs
are added in this slice. Dispatch is by `qobject_cast<>` since each
sublayer is a `QObject`-derived concrete class.

### 4.8 Right-click "Edit Sublayer Style…" entry

The `SublayerStyleDialog` already exists (`ui/dialogs/sublayerstyledialog.h`,
imported by `layertreepanel.cpp` line 18). Wire one extra context-menu
item on sublayer-row right-click — `Edit Style…` — that opens the dialog
scoped to the clicked `ISublayer *`. No new dialog work, just plumbing
in the existing `onContextMenuRequested` slot.

The following are **not** part of this slice, to keep the diff surgical
(per `CLAUDE.md` §3):

- No changes to layer construction, paint, sublayers, or persistence.
- No new layer types or renderer wiring.
- No edits to `LegendLayerTreeModel`, `LayerStyleDialog`, or the
  Symbology dialog.
- No serialiser changes — category id is recomputed at load time from
  `layerType()`, not persisted (verified: `rebuildCategories()` is the
  single source of truth).
- No icon additions to `swmmvis.qrc` (both `CreateMesh` and `Chart` are
  already registered).
- The `SWMMResultsLayer` kind-row UX (OUT.3) is untouched in tree
  structure; the rebuildKindRows() branch for it was already removed in
  2026-05-25. With §4.6 the SWMMResultsLayer now exposes its 11
  Category-keyed sublayers as the 3rd-level rows it was originally
  designed to.
- The "(B) nested subgroups" tree shape is deferred.
- No changes to `SWMM2DResultsLayer`'s paint pipeline. The sublayer
  pointers were dormant per `RENDERING_OUTPUT_SUBLAYERS_PLAN.md` Slice
  S5.6; this plan only surfaces them in the tree, the actual paint
  replacement (Slice CF-final) remains a separate slice.
- No new `ISublayer` implementations.

---

## 6. Verification Plan

Per `CLAUDE.md` §4, each step has an explicit check:

| Step | Verification |
|------|---|
| 1. Enum + mapping edits compile | `cmake --build build` clean — no warnings about unused enumerators |
| 2. Tree shows new categories | Load a project with 1D `.out` results — confirm header reads "SWMM 1D Outputs (N)" with chart icon |
| 3. Mesh isolation | Add a `SWMM2DMeshLayer` — confirm it appears under "Meshes" with the create-mesh icon, **not** under Feature Layers |
| 4. 2D results isolation | Run 2D sim → confirm `SWMM2DResultsLayer` lands under "SWMM 2D Outputs" with the create-mesh icon |
| 5. No regressions | Drag-reorder a category, toggle visibility, edit opacity, open context menu on each new category — same UX as today |
| 6. Warning silenced | Tail Qt log on project load — no `unclassified layer type` for mesh / 2D-results / annotation layers |
| 7. Empty-category suppression | Open a project with no mesh and no 2D results — confirm those two categories are hidden (existing `rebuildCategories` behaviour) |
| 8. 1D sublayer rows | Expand a `SWMMResultsLayer` row — confirm 11 sublayer children (Subcatchments → RainGages) with per-row checkbox + opacity column |
| 9. 1D sublayer toggle | Uncheck "Conduits" sublayer — confirm conduits stop drawing in the result paint pass, model-layer conduits unaffected |
| 10. 2D sublayer rows | Expand a `SWMM2DResultsLayer` row — confirm Mesh Fill / Depth Color Ramp / Filled Contours / Isolines / Velocity Vectors children appear with correct icons |
| 11. 2D sublayer toggle | Toggle Velocity Vectors and Isolines on/off — confirm the paint pass updates and per-sublayer opacity edits propagate |
| 12. Live invalidation | Edit a sublayer style via SublayerStyleDialog — confirm the tree row's swatch/icon updates without a manual refresh |
| 13. Mutual exclusion | Confirm `SWMMModelLayer` still shows its 11 kind rows (not sublayer rows) — eligibility check in §4.6 holds |

Manual smoke test only — no unit test exists for `LayerTreeModel`
categorisation today. Adding one is a sensible follow-up but not in
scope here.

---

## 7. Risk & Rollback

**Risk:** very low. The change is additive on the category enum and a
4-line edit to the type switch. Rollback is a single commit revert.

**Backward compatibility:** existing `.swmmvisproj` files do not persist
category state — categories are recomputed from `layerType()` on load —
so no migration is required.

---

## 8. Files Touched

```
src/ui/panels/layertreepanel.cpp      ~120 lines edited
   §4.1 enum                          ~10 lines
   §4.2 categoryFor                   ~10 lines
   §4.3 categoryInfo                  ~6  lines
   §4.4 layer DecorationRole          ~4  lines
   §4.6 rebuildSublayerRows           ~60 lines (replaces 5-line stub)
   §4.7 sublayer DecorationRole       ~25 lines (dispatch by sublayer class)
   §4.8 context menu entry            ~10 lines
```

Header dependencies on existing classes only: `SWMMResultsLayer`,
`SWMM2DResultsLayer`, the concrete sublayer classes (already forward-
declared / included via `<render/sublayers/*.h>` from the two layer
headers). No new files. No build-system changes. No `.qrc` changes.

---

## 9. Sign-off Checklist

- [ ] Reviewer confirms tree-shape preference: **(A) flat siblings** or
      **(B) nested SWMM Outputs parent**.
- [ ] Reviewer confirms category labels: "Meshes", "SWMM 1D Outputs",
      "SWMM 2D Outputs" — or proposes alternatives.
- [ ] Reviewer confirms `:/swmmvis/CreateMesh` reuse for both Meshes and
      2D Outputs is acceptable (vs. introducing a separate icon for 2D
      results).
- [ ] Reviewer confirms 1D sublayer set (11 per-Category rows) — or
      requests a reduced default (e.g. only Conduits / Junctions /
      Subcatchments visible by default, others hidden).
- [ ] Reviewer confirms 2D sublayer set (Mesh Fill, Depth Color Ramp,
      Filled Contours, Isolines, Velocity Vectors — plus optional Mesh
      Edges / Mesh Nodes / Flow Arrows) — or requests trim.
- [ ] Reviewer confirms sublayer display order in the tree:
      **top-of-paint-stack first** (recommended, mirrors how the rest of
      the tree is rendered) vs. paint-order bottom-up.
- [ ] Reviewer confirms reactivating sublayer rows in the tree does not
      conflict with `RENDERING_RULE_MODEL_PLAN.md` (which previously
      withdrew them) — i.e. that the user direction supersedes that
      decision for output layers only.
- [ ] After sign-off: implement, verify per §6, commit as a single slice.
