# Plan: Cell-Area Stats, Decoupled Mesh Generation, and 1D↔2D Remap Tool

**Date:** 2026-07-28 · **Status:** APPROVED (decisions resolved 2026-07-28, see end)
**Repos:** `openswmm.gui` (primary), `openswmm.engine` (one small API + parser change, Part C)

---

## Problem statement

1. The mesh layer's Metadata tab shows counts and elevation ranges but no cell-area
   statistics (min / max / mean / median).
2. Mesh generation force-inserts every junction as a Steiner vertex
   (`meshgenerationdialog.cpp:2351-2399`). Where nodes sit close together for
   non-physical reasons (weir/orifice/pump endpoints), Triangle's quality pass
   refines razor-thin cells around them. Mesh quality is hostage to 1D topology.
3. Coupling is a *side effect* of generation: the vertex→node map is a marker
   lookup built inside `runMeshPipeline` (`meshgenerationdialog.cpp:1196-1208`).
   There is no way to (re)build the 1D↔2D mapping for an existing mesh — only the
   coincidence-based Auto-couple on the toolbar.

**Goal:** mesh generation produces geometry; a separate, re-runnable **Remap 1D↔2D**
operation maps model nodes onto that geometry — coincident nodes to vertices
(existing coupling), non-coincident nodes to their containing cell via the engine's
existing orifice exchange law (`NodeCoupling.cpp:23-45`), with **multiple nodes
allowed in one cell**.

---

## Part A — Cell-area statistics in mesh metadata

**File:** `src/layers/swmm2dmeshlayer.cpp` — `extendedMetadata()` (`:727-772`).

- Compute signed-area magnitude per triangle (cross product over `m_mesh.vertices`
  / `m_mesh.triangles`), then min, max, mean, and median (`std::nth_element`; even
  count = average of the two middle values).
- Insert four rows after "Cells (triangles)": `Cell area (min)`, `(max)`, `(mean)`,
  `(median)` — 3 significant decimals, project CRS units².
- O(T) on each Metadata-tab open/Refresh; no caching (tab already has a Refresh
  button and 1M-cell scan is milliseconds).
- **Test:** extend `tests/` with a fixture mesh of known areas; verify all four
  stats incl. even/odd median. (Unit-level: factor the stats computation into a
  small free function in `src/mesh/` so it is testable without the layer.)

*Verify:* new unit test passes; dialog shows rows for a generated mesh.

---

## Part B — Decouple generation from 1D mapping

**File:** `src/ui/dialogs/meshgenerationdialog.cpp` (+ header).

1. **Split the "1D – 2D Coupling (SWMM objects)" group (`:1396-1456`) into two:**
   - **"1D geometry influence (optional)"** — the existing three checkboxes
     (junctions → Steiner, conduits → constraints, subcatchments → regions), with
     `m_includeJunctions` **default OFF** (`seedDefaults()` `:1881-1883`). Tooltip
     explains the close-node/tiny-cell tradeoff. Rim-elevation / flatten-radius
     controls stay gated on it (`syncCoupling` `:1446-1453`).
   - **"1D ↔ 2D coupling"** — one new checkbox: *"Map model nodes to the mesh after
     generation (can be re-run anytime from the Mesh toolbar)"*, default ON.
2. **Remove the in-pipeline marker→coupling build** (`:1196-1208` and the
   mirror-back at `:2809-2815`). Instead, when the new checkbox is ON, run the
   Part-C mapper on the finished mesh (GUI thread, after the watcher fires).
   Generation itself no longer authors coupling.
3. Subcatchment→region tagging is *not* coupling — leave `triangleToNode`
   (subcatch runoff targets) untouched in this pass.

Behavioural change to call out in review: with junctions OFF, node locations no
longer appear as mesh vertices; coupling of non-coincident nodes relies on Part C.
Users wanting node-conforming meshes re-enable the checkbox — both paths remain.

*Verify:* generate with junctions OFF on a model with adjacent weir nodes — no
sliver cells at node clusters; coupling summary reported by the auto-run mapper.

---

## Part C — `MeshNodeMapper` + toolbar Remap button

### C.1 Mapper (new, pure logic — unit-testable)

**New files:** `src/mesh/meshnodemapper.{h,cpp}` (peer of `meshautocouple.{h,cpp}`).

Inputs: `MeshResult`, list of `(nodeId, x, y)` (same node lister the toolbar already
injects, `swmmvis.cpp:901`), coincidence tolerance
(`MeshAutoCouple::defaultCoincidenceTol`).

Per in-domain node, first match wins:
1. **Coincident with a vertex** (within tol) → vertex coupling — identical outcome
   to today's Auto-couple (`meshautocouple.cpp`), Cd 0.65 / area 1.0 defaults kept.
2. **Inside a cell** → **cell coupling**: node → containing triangle (point-in-
   triangle walk over a small spatial grid — reuse `meshspatialgrid`). Multiple
   nodes may map to the same cell.
3. **Outside the mesh** → reported unmatched.

Output struct: `{ QHash<int,QString> vertexMatches; QVector<CellMatch> cellMatches;
QStringList unmatched; }` where `CellMatch = { int tri; QString node; double cd, area; }`.

Existing manual couplings are preserved unless the user opts into "re-map all"
(confirmation dialog, same convention as Auto-couple's selection-else-whole-mesh
behaviour at `mesheditingtoolbar.cpp:1059-1060`).

### C.2 Storage: multiple nodes per cell — requires a small engine change

Engine today keys cell coupling **by triangle**: `tri_coupled_node[t]` is a single
`int` (`MeshData.hpp:105`), `[2D_TRIANGLE_NODE_MAP]` last-line-wins
(`SectionHandlers2D.cpp:404`), and there is a getter but **no setter** in the C API
(`Api2D.cpp:282`). The runtime is already general — `buildCouplingPoints()` returns
an unbounded `std::vector<CouplingPoint>` and the orifice law doesn't care how many
points share a cell (`NodeCoupling.cpp:304-360`).

**Proposal (engine, ~4 focused edits):** key authored cell couplings **by node**.
- Parser: accept repeated triangles in `[2D_TRIANGLE_NODE_MAP]` by storing rows in
  a `std::vector<TriCoupling>{tri, node_name, cd, area}` alongside (not replacing)
  the legacy per-triangle arrays; `buildCouplingPoints()` iterates the vector.
  Legacy single-node files behave identically.
- C API: add `swmm_2d_add_triangle_coupling(engine, tri_idx, node_idx, cd, area)`
  \+ `swmm_2d_clear_triangle_couplings(engine)` (mirroring
  `swmm_2d_set_vertex_coupled_node`, `openswmm_2d.h:238`).
- Log the request in `plans/GUI_API_REQUEST_BLOCK_BA.md` per convention.

**GUI storage (MVC):** add to `MeshResult` (`meshresult.h`) a
`QVector<CellCoupling> cellCouplings` (`{tri, nodeId, cd, area}`) — *not* fields on
`MeshTriangle`, since a triangle may carry several. Layer mutators on
`SWMM2DMeshLayer` (`applyCellCoupling…`, undoable, peers of
`applyMeshVertexCoupledNode` at `swmm2dmeshlayer.cpp:1828`); property adapters +
toolbar cell readout show the list for a picked cell.

### C.3 Writer / reader / engine sync

- `InpMeshWriter::formatTriangleNodeMap` (`inpmeshwriter.cpp:115-139`): emit one
  row per `CellCoupling` with `CD` and `AREA` columns (engine already parses
  tokens 2–3, `SectionHandlers2D.cpp:408-413`; engine's own writer already emits
  them, `InpWriter.cpp:441`).
- `InpMeshReader`: add a `[2D_TRIANGLE_NODE_MAP]` parser (none exists today,
  `inpmeshreader.cpp:167-168`), **and fix the pre-existing round-trip bug**: the
  vertex-map reader accepts only integer indices (`:149-151`) while the writer
  prefers tags (`inpmeshwriter.cpp:104-107`) — resolve tags against vertex tags.
- `MeshEngineSync` (`meshenginesync.cpp:112-115`): push cell couplings via the new
  API after vertex couplings.

### C.4 Toolbar button

**File:** `src/ui/toolbars/mesheditingtoolbar.cpp` + wiring in `swmmvis.cpp`.

- New `QAction` **"Remap 1D↔2D"** next to Auto-couple (`:149-156`); enabled when an
  active mesh + model layer exist. Runs the mapper, applies results as **one undo
  command**, then shows a summary: *N vertex-coupled, M cell-coupled
  (K cells shared), U unmatched*.
- Auto-couple stays (strict-coincidence subset); Remap is the superset action.

### C.5 Links

Engine coupling is **node-based only** — links have no 1D↔2D exchange mechanism.
Conduits' role stays geometric (optional constraint segments in Part B). If link
crest coupling is wanted later it is an engine feature first; out of scope here.

*Verify (Part C):* unit tests for the mapper (coincident node, interior node, two
nodes sharing one cell, node outside mesh, preserve-existing); writer↔reader↔engine
round-trip test incl. the tag-form fix; manual: Remap on the demo model, inspect
shared-cell couplings in the toolbar readout, run a simulation.

---

## Sequencing & risk

| Step | Depends on | Risk |
|---|---|---|
| A (metadata stats) | — | none |
| B (dialog split) | C mapper for the auto-run | low — both generation modes preserved |
| C.2 engine change | — | low — additive; legacy files unchanged |
| C.1/C.3/C.4 GUI | C.2 API merged into the engine install the GUI builds against | medium — cross-repo ordering |

Suggested order: **A → C.2 (engine) → C.1/C.3 (mapper+IO) → C.4 (toolbar) → B (dialog)**,
so the dialog's "map after generation" lands only once the mapper exists.

## Decisions (resolved 2026-07-28)

1. **Engine change approved** — implement C.2 (node-keyed coupling rows + new
   `swmm_2d_add_triangle_coupling` / `swmm_2d_clear_triangle_couplings` C API).
2. **Cell-coupling defaults: Cd = 0.65, exchange area = 2.0 m²** (vertex coupling
   keeps its existing 1.0 m² default; the 2.0 m² applies to cell-coupled nodes
   authored by the mapper — editable per coupling afterward).
3. **`m_includeJunctions` defaults OFF** in the generation dialog.
