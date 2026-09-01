# Mesh Boundary Shortest-Path Edge Selection — GUI Plan (2026-08-04)

**Status:** IMPLEMENTED 2026-08-04 (uncommitted). Phases 1–4 all done; see §4 for what each verified.
**Decisions (user-approved 2026-08-04):** path edges are **Added** to the current selection; "shortest" = **geometric length** (Dijkstra); Ctrl/⌘+click is repurposed for path picking in this tool.

## 1. User-facing behavior

With the mesh edge-select tool (`mesh-select-edge`) active:

- **Ctrl/⌘ + click** on a **boundary** edge sets the path **anchor** (drawn distinctly, see §4).
- **Ctrl/⌘ + click** on a second boundary edge computes the shortest path along boundary edges from anchor to target (both terminal edges included) and **Adds** all path edges to the selection. The anchor then clears, so paths can be chained.
- Ctrl/⌘ + click on a non-boundary edge or empty space: no-op + status-bar message ("Path selection requires boundary edges"). It does **not** fall back to Toggle — Ctrl-as-Toggle is superseded in this tool (Shift=Add and plain click=Replace are unchanged; box-select unchanged).
- **Esc** clears the anchor first; a second Esc clears the selection (existing behavior).
- No path exists (disconnected boundary loops): status-bar message, anchor retained.
- Anchor = target edge: path is that single edge.
- Anchor is invalidated on tool deactivate, layer change, and `sceneGeometryReady()` (mesh rebuilt).

Note `Qt::ControlModifier` already maps to ⌘ on macOS (no `AA_MacDontSwapCtrlAndMeta` set), so one code path covers both platforms.

## 2. Architecture (MVC per CLAUDE.md §5.1)

- **Model:** `SWMM2DMeshLayer` gains a lazily built, cached boundary-edge graph (invalidated with scene geometry). Selection state stays in `SelectionManager`; highlight rendering is untouched (existing `MeshEditingToolbar::onSelectionChanged` bridge → `setHighlightedEdges` → QSG selection pass).
- **Controller:** `MapToolMeshSelectEdge` owns only the transient anchor and drives the graph query + `SelectionManager::select(refs, Add)`.
- **Views:** unchanged. Committed path edges render through the existing selection pass (`swmm2dmeshqsgrenderer.cpp:1371-1470`); only the anchor/preview is tool `paint()` overlay.

## 3. New/changed files

### New: `include/mesh/meshboundarygraph.h` + `src/mesh/meshboundarygraph.cpp`

`mesh::MeshBoundaryGraph` — pure value type, no Qt-GUI deps (unit-testable headless):

```cpp
class MeshBoundaryGraph {
public:
    // isBoundary: flat tri*3+e flags (from buildBoundaryFlags)
    static MeshBoundaryGraph build(const MeshResult &mesh, const QVector<bool> &isBoundary);
    // Returns flat edge slots (tri*3+e) forming the shortest path, inclusive of both
    // terminal edges. Empty if either input is not boundary or no path exists.
    QVector<int> shortestPath(int startSlot, int endSlot) const;
    bool isEmpty() const;
private:
    // CSR: vertex -> incident boundary edge slots; per-slot cached endpoints + length.
};
```

- Build: one O(3T) pass over `m_isBoundary`; boundary edges have exactly one slot (no interior dedupe problem). Endpoints from the e-opposite-vertex convention (e0=(v1,v2), e1=(v2,v0), e2=(v0,v1)) — same as `pickEdgeAt`/`buildBoundaryFlags`.
- Path: multi-source Dijkstra seeded from both endpoints of the start edge, terminating at either endpoint of the end edge; weights = 2D segment length from `mesh.vertices[].xy`. Boundary graphs are near-degree-2 chains/loops (marker-tagged edges can raise degree — Dijkstra handles it), and boundary edge count is O(√T), so this is interactive even on large meshes. Priority queue over `std::priority_queue`; predecessor stored per (vertex, arriving-slot) to reconstruct the slot sequence.

### Changed: `include/layers/swmm2dmeshlayer.h` / `src/layers/swmm2dmeshlayer.cpp`

- `const mesh::MeshBoundaryGraph &boundaryGraph();` — lazy build (requires `sceneGeometryComplete()`; returns empty graph otherwise). Cache member `m_boundaryGraph` + `m_boundaryGraphValid`, cleared wherever `m_isBoundary` is rebuilt (`rebuildSceneGeometry` / `finishSceneGeometryAsync` completion).
- No other layer changes. (`findEdgeNeighbour` stays private — not needed here.)

### Changed: `include/map/tools/maptoolmeshselectedge.h` / `src/map/tools/maptoolmeshselectedge.cpp`

- Members: `int m_pathAnchorSlot = -1;` (+ the layer it belongs to).
- `mousePressEvent`: if `ControlModifier` → `pickEdgeAt(..., /*boundaryOnly=*/true)`; first hit stores anchor, second hit runs `boundaryGraph().shortestPath(anchor, hit)`, maps slots → `mesh::MeshObjectRef::edge(sourcePath, slot/3, slot%3)`, commits via `SelectionManager::Add`, clears anchor, invalidates `Scene | Overlay`. Remove the old `toggleMode` branch (Ctrl no longer reaches the Toggle path); Shift/plain logic untouched.
- `keyPressEvent`: Esc clears anchor before falling through to existing clear-selection.
- `paint()`: draw the anchor edge (thick, distinct color per theme) on top of the existing rubber-band drawing.
- `deactivate()` / mesh-changed: reset anchor.

### Tests: `tests/unit/test_meshboundarygraph.cpp`

Synthetic meshes (built inline as `MeshResult`, written under `tests/unit/data/` if file-based, per CLAUDE.md §4.1 — no temp dirs):

1. Square of 8 triangles → outer boundary loop; path between opposite edges takes the shorter arc.
2. Two disconnected loops → `shortestPath` returns empty.
3. Start == end → single-edge path.
4. Marker-tagged internal boundary edge (degree-3 vertex) → Dijkstra picks the geometrically shorter route.
5. Non-boundary input slot → empty.

## 4. Phased checklist

```
Phase 1: MeshBoundaryGraph + unit tests            → DONE. tests/unit/test_meshboundarygraph 9/9 headless.
Phase 2: Layer accessor + cache invalidation        → DONE. tests/gui/test_meshboundarypath 4/4 — slots agree with
                                                       isBoundaryEdge(), short-arc routing, cache stable, and a
                                                       deferHeavyGeometry load reports empty until sceneGeometryReady().
Phase 3: Tool wiring (anchor, commit, Esc, paint)   → DONE (code); MANUAL RETEST STILL OWED — anchor render, path Add,
                                                       chaining, no-path message, Shift/plain/box select unchanged,
                                                       graceful no-op during progressive load.
Phase 4: docs/manual/08_selection.md update          → DONE (new "2D mesh edges" section + 4 gotchas).
```

**Deviations from the plan as written**

- Ctrl-click is handled in `mousePressEvent` (as specified) with a
  `m_pathClickHandled` latch so the matching `mouseReleaseEvent` does not also
  run a plain single-click select. Side effect: Ctrl+box-drag no longer
  box-selects (Ctrl is path-only in this tool); Shift/plain box-select unchanged.
- Path picking always picks boundary-only, independent of the A/B interior
  toggle — the graph has no interior arcs to route along.
- Status messages required new plumbing: `MapToolMeshSelectEdge::statusMessageChanged`
  → `SWMMVisProjectWindow::meshEdgeStatusMessage` → `SWMMVis` status bar (the tool
  is created lazily, so it cannot be wired directly from `SWMMVis`).
- Graph CSR is over *compacted* boundary vertices, not all mesh vertices, so the
  arrays stay proportional to the boundary on a million-vertex mesh.
- `boundaryGraph()` deliberately does NOT cache while `sceneGeometryComplete()`
  is false — otherwise a progressive load would pin an empty graph forever.

Non-goals: hover live-preview of the path (can be a follow-up — Dijkstra is cheap enough), Alt=Subtract, path selection for interior edges, undo of selection (selection is not on the undo stack anywhere in the app).
