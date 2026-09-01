# HANDOFF — Verify & Test: Cell-Area Stats, Decoupled Mesh Generation, Remap 1D↔2D

**Date:** 2026-07-28 · **For:** verification/testing agent
**Implements:** `workplans/MESH_DECOUPLED_1D2D_REMAP_PLAN_2026-07-28.md` (all parts, one shot)
**Repos touched:** `openswmm.engine` AND `openswmm.gui` — **build the engine first**; the
GUI's `meshenginesync.cpp` calls four brand-new engine C-API symbols and will not link
against a stale engine install tree.

---

## 1. What changed (file-by-file)

### openswmm.engine
| File | Change |
|---|---|
| `src/engine/2d/data/MeshData.hpp` | New `TriCouplingRow {tri, node, node_name, cd, area}` + `std::vector<TriCouplingRow> tri_couplings` — source of truth for cell couplings. Legacy per-triangle arrays kept as last-row-wins mirror. |
| `src/engine/2d/input/SectionHandlers2D.cpp` | `parse2DTriangleNodeMapLine` now APPENDS a row per line (repeated-row form; several nodes per triangle) instead of overwriting per-triangle arrays. |
| `src/engine/2d/SurfaceRouter2D.cpp` | Resolve step: synthesises rows from legacy arrays when `tri_couplings` is empty (GeoPackage path), resolves row names → indices, mirrors last row into legacy arrays. Unit scaling (`f2`) applied to `row.area`. |
| `src/engine/2d/coupling/NodeCoupling.cpp` | `buildCouplingPoints()` triangle section iterates `tri_couplings` (one CouplingPoint per row) instead of per-triangle arrays. |
| `src/engine/core/InpWriter.cpp` | `[2D_TRIANGLE_NODE_MAP]` written from `tri_couplings` (one row per coupling) when non-empty; legacy fallback preserved. |
| `include/openswmm/engine/openswmm_2d.h` + `src/engine/2d/api/Api2D.cpp` | **New API:** `swmm_2d_add_triangle_coupling(engine, tri, node_name, cd, area)`, `swmm_2d_clear_triangle_couplings`, `swmm_2d_triangle_coupling_rows`, `swmm_2d_get_triangle_coupling_row`. |
| `plans/GUI_API_REQUEST_BLOCK_BA.md` | Item 9 (MESH-REMAP-01) logged as shipped. |

### openswmm.gui
| File | Change |
|---|---|
| `include/mesh/meshresult.h` | New `mesh::CellCoupling {tri, nodeId, cd=0.65, area=2.0}`; `MeshResult::cellCouplings`. |
| `include/mesh/meshcellstats.h` + `src/mesh/meshcellstats.cpp` | **Part A** — `computeCellAreaStats()` (min/max/mean/median, nth_element median, even-count average). |
| `src/layers/swmm2dmeshlayer.cpp` | `extendedMetadata()` gains 4 "Cell area" rows + "Coupled cells (rows)"; new `applyCellCouplings()` mutator (wholesale swap, returns previous rows); `cellCouplings()` accessor in header. |
| `include/mesh/meshnodemapper.h` + `src/mesh/meshnodemapper.cpp` | **Part C.1** — `mapNodesToMesh()`: coincident→vertex (nearest, claimed-once), interior→cell (spatial-grid point-in-triangle, edge-inclusive, lowest-index tie-break), outside→unmatched, preserve-existing mode. Defaults Cd 0.65 / area 2.0 m². |
| `src/mesh/inpmeshwriter.cpp` | `formatTriangleNodeMap` writes `cellCouplings` rows (INDEX NODE CD AREA) after legacy subcatch rows. |
| `src/mesh/inpmeshreader.cpp` | **Bug fix**: `[2D_VERTEX_NODE_MAP]` now resolves TAG-form first tokens (writer prefers tags; reader only took ints — GUI-written maps never round-tripped). **New**: `[2D_TRIANGLE_NODE_MAP]` parser → `cellCouplings`. |
| `src/mesh/meshenginesync.cpp` | Pushes cell couplings: `clear` + one `add` per row (skipped when both sides have none). |
| `src/ui/toolbars/mesheditingtoolbar.{h,cpp}` | **Part C.4** — "Remap 1D↔2D" action beside Auto-couple; Add-missing / Re-map-all / Cancel dialog; summary message (vertex-coupled / cell-coupled / shared cells / skipped / outside). |
| `src/ui/dialogs/meshgenerationdialog.{h,cpp}` | **Part B** — group renamed "1D geometry influence (optional)"; `m_includeJunctions` **default OFF** with explanatory tooltip; new "1D ↔ 2D coupling" group + `m_mapNodesAfterGen` (default ON); `PipelineInputs.couplingNodes` collected for ALL node categories; worker runs `mapNodesToMesh` post-Triangle (preserve-existing, so marker-coupled Steiner vertices are respected) and appends `cellCouplings` before the write. |
| `CMakeLists.txt`, `tests/unit/CMakeLists.txt` | `meshcellstats.cpp`, `meshnodemapper.cpp` added; new test targets `test_meshcellstats`, `test_meshnodemapper`. |
| `tests/unit/test_meshcellstats.cpp`, `tests/unit/test_meshnodemapper.cpp` | New unit suites (9 + 8 cases). |

---

## 2. Build order & commands

```bash
# 1. Engine first (GUI links new symbols)
cd openswmm.engine
cmake --preset=<platform>            # Darwin / Linux / Windows
cmake --build build/<subdir> --config Release
cmake --install build/<subdir> --prefix install/<preset>   # where the GUI expects it

# 2. GUI against the fresh engine install
cd ../openswmm.gui
cmake --preset=<platform> -DSWMMVIS_BUILD_TESTS=ON \
      -DOPENSWMMENGINE_INSTALL_DIR=<abs path to engine install>
cmake --build build/<subdir> --config Release
```

Expect ZERO new warnings in the touched files. If `meshenginesync.cpp` fails to link
(`swmm_2d_add_triangle_coupling` undefined), the engine install tree is stale — step 1
did not land.

## 3. Automated tests

```bash
# GUI unit + gui suites
ctest --test-dir openswmm.gui/build/<subdir> -L unit --output-on-failure
ctest --test-dir openswmm.gui/build/<subdir> -L gui  --output-on-failure
# Engine suite
ctest --test-dir openswmm.engine/build/<subdir> --output-on-failure
```

Must pass, with attention to: `test_meshcellstats` (NEW), `test_meshnodemapper` (NEW),
`test_meshautocouple`, `test_meshenginesync`, `test_inpmeshreader`/writer suites (names
may differ — anything touching `inpmesh*`), `test_meshgenerator`.

**Gaps you should fill (were not writable without a build environment):**
1. **Writer/reader round-trip test** for cellCouplings: build a MeshResult with 2 rows
   sharing one triangle → `InpMeshWriter::write` (inline) → `InpMeshReader::read` →
   expect both rows back (tri, node, cd=0.65, area=2.0). Add to the existing inpmesh
   test target.
2. **Vertex TAG-form regression test**: write a mesh whose coupled vertex has a tag,
   confirm the reader now restores `coupledNode` (this was the pre-existing bug).
3. **Engine gtest** for the new API: open a 2D model, `swmm_2d_add_triangle_coupling`
   twice on the same triangle (different nodes), `swmm_2d_triangle_coupling_rows` == 2,
   run a short sim (both nodes exchange), write the .inp, re-read, rows == 2. Follow the
   AA-3.1 test pattern referenced in `plans/GUI_API_REQUEST_BLOCK_BA.md` §Verification.
4. **Engine round-trip via .inp text**: `[2D_TRIANGLE_NODE_MAP]` with two rows for
   triangle 0 → InpWriter output contains both rows (previously last-line-wins).

## 4. Manual QA script (GUI, ~15 min)

Model: `examples/demo_road_culvert/` (or any model with a weir/orifice pair).

1. **Part A** — Generate any mesh → layer Properties → Metadata tab: four "Cell area"
   rows present, median ≤ mean plausible, values in CRS units². Refresh works.
2. **Part B** — Mesh generation dialog: "1D geometry influence (optional)" group;
   junctions checkbox UNCHECKED by default; rim/flatten controls disabled until checked;
   "1D ↔ 2D coupling" group with mapping checkbox CHECKED. Generate with junctions OFF
   on a model with two nodes < one cell-size apart: NO sliver cells at the pair
   (compare against junctions ON). Metadata shows "Coupled cells (rows)" > 0.
3. **Part C.4** — Mesh toolbar → "Remap 1D↔2D": Add-missing run reports skips (all
   nodes already coupled from generation); Re-map all works; summary counts consistent;
   close weir nodes land in the SAME cell (shared-cells line ≥ 1).
4. **Persistence chain** — Save project → open the .inp: `[2D_TRIANGLE_NODE_MAP]` has
   one row per coupling incl. repeated triangle indices with CD/AREA (0.65 / 2). Reload
   project: couplings intact (reader path). Run a simulation: no errors; flooded node
   coupled mid-cell shows exchange with the 2D surface.
5. **Regression** — Auto-couple still works standalone; vertex Cd/Area spinboxes still
   gate on coupled vertices; generating with junctions ON reproduces the old behaviour
   (marker coupling + mapper fills only the rest).

## 5. Known limitations / decisions (do not "fix" without checking)

- **GeoPackage** persists only the last coupling per triangle (schema is one row per
  triangle). Deliberate follow-up; noted in `GUI_API_REQUEST_BLOCK_BA.md` item 9.
- Cell-coupling defaults **Cd 0.65 / area 2.0 m²** (user decision 2026-07-28); vertex
  couplings keep 1.0 m². `mesh::kCellCouplingDefault*` in `meshnodemapper.h`.
- The Remap toolbar action applies vertex couplings via the per-vertex mutator (NOT yet
  one undo command — `applyCellCouplings` returns the previous rows to enable a proper
  QUndoCommand later; acceptable for this slice, flag if undo is required now).
- Toolbar has no per-cell coupling READOUT yet (rows visible via Metadata count and
  .inp). Plan C.2's adapter work was scoped out of this shot.
- `m_includeJunctions` default OFF changes generated meshes vs. previous releases —
  intentional (plan decision 3).

## 6. If something fails

- Mapper misclassifies edge-sitting nodes → see eps logic in
  `src/mesh/meshnodemapper.cpp::pointInTriangle` (scale-aware, edge-inclusive).
- Round-trip loses rows → check section constant `kSecTriangleNodeMap` in
  `inpmeshreader.cpp` and that `stripExistingMeshSections` still strips the section
  before rewrite (it owns `[2D_TRIANGLE_NODE_MAP]` — pre-existing).
- Engine double-counts couplings → `SurfaceRouter2D` synthesis must run ONLY when
  `tri_couplings` is empty; `buildCouplingPoints` must NOT also read legacy arrays.
- Sim result differences vs. old builds for single-coupling models should be ZERO —
  bitwise-identical coupling points. Any drift = bug in the row/mirror path.

---

## 7. Verification result — 2026-07-28 (macOS arm64, Darwin presets)

**Builds:** engine `Darwin` → `install/Darwin` (codesigned), GUI `build/` against it.
Zero new warnings in the touched files (residual warnings are pre-existing:
Kokkos GPU sources, `TableData.hpp` field init, `-Winconsistent-missing-override`
in `gisvectorlayer.h` / `swmm2d*layer.h`, `ld` duplicate-library notes).

**Suites:** engine `106/106` pass. GUI `unit` `45/45` pass. GUI `gui` `93/94` —
the one failure, `test_asyncload::parseErrorDiagnosticsPreserved`, is
**pre-existing and unrelated**: it asserts `openEngineForPath` returns null for
a model with an undefined `[DWF]` node, but `swmm_engine_set_lenient_open(1)`
(GUI commit `fe6577c`, 2026-07-19) deliberately keeps such models open. The test
predates that change (`349b2eb`, 2026-07-11) and needs updating separately.

### Defects found and fixed

1. **`mapNodesToMesh` matched nothing — the whole feature was inert.**
   `MeshSpatialGrid` drops zero-area bboxes on `rebuild` and rejects zero-area
   query rects, so the degenerate vertex boxes (`QRectF(v.xy, v.xy)`) and the
   point probe (`QRectF(p, p)`) indexed and matched nothing: every node fell
   through to `unmatched`. Fixed in `src/mesh/meshnodemapper.cpp` — boxes and
   probes are inflated by `tol`, the exact distance / point-in-triangle tests
   still do the filtering. All 8 shipped mapper tests were red before this
   (they had never been run); they are green now.
2. **Full re-map double-coupled a node onto its own vertex.** "Re-map all"
   clears cell rows but keeps vertex couplings; the vertex pass skipped ANY
   coupled vertex, so a node sitting on its own vertex also got a cell row —
   two engine coupling points for one node, exchanging twice. The vertex pass
   now recognises `coupledNode == id` and reports the node as
   `skippedExisting`. Regression tests:
   `FullRemapDoesNotDoubleCoupleANodeOnItsOwnVertex`,
   `VertexHeldByAnotherNodeFallsThroughToCell`.
3. **Parser stopped maintaining the legacy per-triangle mirror.**
   `parse2DTriangleNodeMapLine` only appended rows, leaving
   `tri_coupled_node_name/cd/area` empty until `SurfaceRouter2D::initialize`
   mirrored them back. The **GeoPackage writer reads those arrays** (3 sites)
   and the GUI saves from an OPENED-not-initialized engine, so cell couplings
   were silently dropped on a pre-initialize GeoPackage export. This is what
   turned `test_engine_2d_surface::InputParsing.Parse2DTriangleNodeMap` red.
   The parser now mirrors last-row-wins, same as `swmm_2d_add_triangle_coupling`.

### Tests added (the four listed gaps + three extra)

| Test | Covers |
|---|---|
| `MeshInpCellCouplingRoundtrip.SharedTriangleRowsSurviveWriteRead` | gap 1 — 2 rows on one triangle survive writer→reader |
| `MeshInpCellCouplingRoundtrip.NoRowsEmitsNoSection` / `.OmittedColumnsGetMapperDefaults` | empty-set + CD/AREA defaults (0.65 / 2.0) |
| `MeshInpCouplingRoundtrip.VertexTagFormResolvesOnRead` | gap 2 — tag-form vertex map round-trips (asserts the file really is tag form) |
| `TriangleCouplingRowsTest.*` (5 cases, engine) | gaps 3 + 4 — parse/write round-trip, add/clear/rows/get API, survival across a run, error paths, legacy single-row mirror |
| `MeshEngineSync.CellCouplingRowsPersistThroughEngine` | full GUI persistence chain: reader (tag form) → layer → `pushMeshEditsToEngine` → `swmm_model_write` → reader, shared cell intact |
| `TestMeshAsyncLoad::metadataCarriesCellAreaStatsAndCouplingRows` | Part A metadata rows on the real layer + `applyCellCouplings` drop/undo contract |
| `MeshNodeMapper.FullRemapDoesNotDoubleCouple…` / `.VertexHeldByAnotherNode…` | defect 2 |

### Still needs a human (UI-only, unchanged from §4)

Dialog layout / checkbox defaults and tooltips (Part B), the toolbar action and
its Add-missing / Re-map-all dialog and summary text (Part C.4), and the
sliver-cell comparison with junctions ON vs OFF. Code-level checks done:
`seedDefaults()` sets junctions OFF and mapping ON, `collectInputs` collects all
four node categories (`CatJunctions … CatDividers`), the toolbar action is
registered next to Auto-couple and applies vertex couplings through the existing
per-vertex mutator.

### Observations (not changed)

- The writer emits `coupling.triangleToNode` (subcatchment→region rows, node
  column = subcatchment name) into the same `[2D_TRIANGLE_NODE_MAP]` section
  the reader now parses into `cellCouplings`. On a GUI round-trip those rows
  come back as cell couplings and are re-emitted by index. Pre-existing
  section overloading; previously the GUI simply dropped the section on read.
- `applyCellCouplings` emits `repaintRequested` but not `attributeChanged` —
  consistent with the other mesh mutators, which also have no project-dirty hook.
