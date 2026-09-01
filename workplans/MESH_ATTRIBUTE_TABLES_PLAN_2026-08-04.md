# Mesh Attribute Tables (Vertices / Edges / Cells) — GUI Plan (2026-08-04)

**Status:** Proposal — for review. No implementation yet.
**Decisions (user-approved 2026-08-04):** extend the existing `AttributeTablePanel` (not a new dock); x/y are **read-only**, everything else editable; edge/vertex edits become **undoable** via new `MapCommand`s (ids ≥ 45), and the toolbar is rewired through the same helpers.

## 1. User-facing behavior

- The Attribute Table dock's category combo gains, per loaded `SWMM2DMeshLayer`, three entries: `Mesh <name> — Vertices`, `— Edges`, `— Cells`.
- Standard panel features work: WHERE query bar, selection ops (Replace/Add/Subtract/Intersect/Invert), show-selected-only, zoom-to-selected, CSV export, TSV copy, right-click bulk "apply value to selected rows" (single undo macro).
- **Two-way selection sync:** selecting rows selects the elements on the map (highlighted via the existing QSG selection pass); selecting on the map (edge/vertex/cell tools) highlights and reveals the rows.
- Edits are undoable (Ctrl+Z), including bulk column applies. x/y (and other derived columns) are read-only.
- Delete-selected is disabled for mesh categories (mesh elements can't be individually deleted).
- Tables are unavailable (category grayed) until `sceneGeometryComplete()`.

## 2. Architecture (MVC per CLAUDE.md §5.1)

- **Model:** `SWMM2DMeshLayer` remains the single source of truth. All table writes go through new undoable push helpers → existing `apply*` mutators → `attributeChanged(refName)` / `meshEditsChanged()`. Table rows are virtual; no shadow copies of mesh data.
- **Views:** the new table model + existing `MeshEditingToolbar` + property-browser adapters all observe the same layer signals, so simultaneous edits stay synchronized. Map highlight reuses the existing `MeshEditingToolbar::onSelectionChanged` bridge unchanged (noting for a future refactor that this bridge belongs in `SWMMVisProjectWindow`; not moved now per CLAUDE.md §3).
- **Controller:** `AttributeTablePanel` routes selection to/from `SelectionManager` with the same guard-flag pattern as `SWMMAttributeTableModel`.

## 3. New/changed files

### New: `include/ui/panels/meshattributetablemodel.h` + `src/ui/panels/meshattributetablemodel.cpp`

`MeshAttributeTableModel : QAbstractTableModel`, bound by `setSource(SWMM2DMeshLayer*, Kind)` with `enum class Kind { Vertex, Edge, Cell }`. Reuses the panel's `ColumnSpec` / `EditorKind` / `UnitKind` schema and existing delegates (`attributedelegates.h`) so `installColumnDelegates()` works as-is.

**Row identity** (needed for `rowForRef` / selection sync):

- Vertices: row = vertex index. Ref: `MeshObjectRef::vertex(sourcePath, row)`.
- Cells: row = triangle index. Ref: `MeshObjectRef::cell(sourcePath, row)`.
- Edges: one row per **unique** edge — boundary edges have one slot; interior edges are canonicalized to the lower flat slot of the pair. Requires making `SWMM2DMeshLayer::findEdgeNeighbour` **public** (no behavior change) and a one-time O(3T) row-index build (`QVector<int> m_edgeRows` slot list + hash slot→row, rebuilt on `sceneGeometryReady`). Ref uses the canonical slot; `rowForRef` maps either slot of an interior pair to the same row.

**Columns:**

| Kind | Read-only | Editable |
|---|---|---|
| Vertex | id, x, y, marker | z (Numeric, Length), tag (Text), coupledNode (Text), couplingCd (Numeric), couplingArea (Numeric, Area) |
| Edge | id (`tri:e`), boundary, length | conveyance (Numeric); BC fields on boundary edges only: bcType (Enum), head, slope, flow (Numeric), tseries, curve, group (Text) |
| Cell | id, area, centroid x, centroid y | tag (Text) + one column per `mesh::cellParamSpecs()` entry (mannings, initDepth, …) driven by the spec's min/max/decimals/unit |

BC cells on non-boundary edge rows: `flags()` drops `ItemIsEditable` and `data()` renders "—" (mirrors the toolbar's boundary gating).

**Refresh:** connect `attributeChanged(refName)` → parse via `MeshObjectRef::parse*` → `dataChanged` for that row; `meshEditsChanged()` → coalesced full-column refresh; `sceneGeometryReady()` → `beginResetModel` + rebuild row index. Row data lazily computed in `data()` (no `m_rowCache` needed — reads are cheap index lookups); emit `objectEdited(refName)` on user edits only, matching the existing anti-feedback-loop convention.

**Scalability note:** rowCount can be ~1.5T for edges on multi-million-cell meshes. The model is fully virtual, so cost is the one-time row-index build; the WHERE query bar and show-selected-only proxy already handle filtering. Acceptable; no pagination.

### Changed: `include/map/meshcommands.h` / `src/map/meshcommands.cpp`

Mirror the existing cell-command pattern (`MeshSetTriangleAttributeCommand`, id 43):

- `MeshSetVertexAttributeCommand` (**id 45**): key ∈ {z, tag, coupledNode, couplingCd, couplingArea}; parallel `vertices/newValues/oldValues` (QVariant to cover text keys). Redo/undo dispatch to `applyMeshVertexZ` / `applyMeshVertexTag` / `applyMeshVertexCoupledNode` / `applyMeshVertexCouplingCd/Area`.
- `MeshSetEdgeAttributeCommand` (**id 46**): key ∈ {conveyance, bcType, head, slope, flow, tseries, curve, group}; slots stored as flat `tri*3+e`; dispatch to `applyMeshEdgeConveyance` / `applyMeshEdgeBC` (snapshotting the full `MeshEdgeBC` slot for undo — BC fields are interdependent through `bcType`).
- Push helpers, same contract as `pushCellParamEdit(s)`: `mesh::pushVertexParamEdit(s)`, `mesh::pushEdgeParamEdit(s)` — snapshot old values, drop no-ops, fall back to direct write when no canvas/undo stack.

### Changed: `src/ui/toolbars/mesheditingtoolbar.cpp`

Rewire `onZSpinChanged`, `commitBCParam`, `commitConveyance` (and vertex tag/coupling commits) through the new push helpers so toolbar edits become undoable and consistent with the table. No UI changes.

### Changed: `include/ui/panels/attributetablepanel.h` / `src/ui/panels/attributetablepanel.cpp`

- Category population: enumerate `SWMM2DMeshLayer`s, add the three entries per layer; rebuild on layer add/remove and gray until `sceneGeometryComplete()` (enable on `sceneGeometryReady`).
- `setSource` branch for `MeshAttributeTableModel` (fourth source-model type alongside SWMM/tabular/GIS); pass `m_canvas->undoStack()`.
- Selection sync: table→bus builds `MeshObjectRef` refs (per selection-ops radio mode); bus→table filters refs by `MeshObjectRef::layerKey(mesh->sourcePath())` + kind, uses `rowForRef`, guarded by the existing `m_applyingFromBus` flag. Map highlight then flows through the untouched toolbar bridge.
- Disable delete-selected for mesh categories; zoom-to-selected computes extent from element geometry (vertex pt / edge endpoints / triangle bbox).
- Per-category column-width persistence keys: reuse existing QSettings scheme with the mesh category id.

### Tests

- `tests/unit/test_meshcommands_vertex_edge.cpp`: redo/undo round-trips for both new commands incl. BC-slot snapshot restore and no-op dropping; interior-edge conveyance mirroring preserved under undo.
- `tests/gui/test_meshattributetable.cpp`: row identity (interior edge dedupe maps both slots to one row), x/y non-editable flags, edit → layer signal → single-row `dataChanged`, selection round-trip table↔`SelectionManager` without feedback loops.
- Any file-based fixtures under `tests/.../data/` (CLAUDE.md §4.1 — reviewable locations, no temp dirs).

## 4. Phased checklist

```
Phase 1: Undo commands + push helpers + rewire toolbar     → verify: unit tests pass; Ctrl+Z works from toolbar edits
Phase 2: MeshAttributeTableModel (read-only first)          → verify: gui test — rows/columns correct on sample mesh, edge dedupe
Phase 3: Editing via delegates + undo, bulk column apply    → verify: gui test + manual — edits undo as one command per apply
Phase 4: Panel integration + two-way selection sync         → verify: manual — map tools ↔ table selection, highlight, zoom-to
Phase 5: docs/manual update + CHANGELOG on release (§5.2)   → verify: doc build
```

Non-goals: editing x/y (explicitly excluded), adding/deleting mesh elements from the table, moving the selection→highlight bridge out of the toolbar (follow-up refactor), persisting mesh edits (existing `pushMeshEditsToEngine` save path is unchanged and already covers every field exposed here).
