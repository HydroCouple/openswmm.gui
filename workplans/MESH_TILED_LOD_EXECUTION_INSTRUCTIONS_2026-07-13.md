# Mesh Tiled LOD — One-Shot Execution Instructions

Created: 2026-07-13 · Status: **READY TO EXECUTE**

## Mission

Implement `workplans/MESH_TILED_LOD_RENDERING_PLAN_2026-07-13.md` end-to-end in one session: activate the QSG mesh path, chunked indexed geometry, the dot-product (normal-deviation) LOD pyramid, the editing-mode native-topology guarantee, and the Terrain-toolbar "True mesh" toggle. Compile and test after every stage. You own the work through final validation — do not hand back a partially-verified result without an honest status record.

**The plan document is the specification.** Read it fully first, then the predecessor briefs it cites (`QSG_2D_1M_RENDERING_AGENT_INSTRUCTIONS.md` for conventions, `MESH_CULLING_OCCLUSION_AND_LEGEND_PLAN.md` §1.1 for the memory arithmetic). Use live code as source of truth where docs disagree.

## Resolved decisions (owner-approved — do not re-litigate)

1. **QSG mesh activation:** yes — preference default ON, env kill-switch `OPENSWMM_QSG_MESH=0`, QPainter path retained as fallback.
2. **Chunk GPU buffer LRU budget:** 256 MB default, `PreferencesManager`-tunable.
3. **Pyramid:** normal-deviation dot-product thinning (`DTMThinner` criterion re-targeted to `MeshResult` topology), threshold ladder 0.90 / 0.95 / 0.99 / native, nested vertex sets. Quad-bake `rebuildOverview()` kept only as degenerate-case fallback. Fan-closing retriangulation first; CDT-per-level fallback if slivers (min interior angle < 20°) — decide by measuring, record which you chose and why.
4. **Sidecar pyramid cache:** yes — versioned binary blob beside the mesh source file, keyed by mesh content hash + threshold ladder.
5. **Test meshes:** generator-only in the repo. Commit a small generator (test utility producing synthetic `.2dm` terrain with channels/ridges at 0.5M/1M/5M tris); generated files go to `tests/perf-data/mesh/` (gitignored, but a documented, user-reviewable folder — never temp dirs).
6. **Editing:** automatic native-forcing covers **visible chunks only** while an edit tool is active. The manual Terrain-toolbar toggle forces native **everywhere**.

## Environment / build / test commands

- Configure preset exists; build tree is `build/darwin-debug` (see `CMakePresets.json` / `CMakeUserPresets.json`; `Darwin-debug`, user preset `Darwin-local`). If `build/darwin-debug` is stale, reconfigure with the same preset previously used — check `gui-rebuild*.log` for the exact invocation before inventing one.
- Incremental target build: `cmake --build build/darwin-debug --target <target> -j2`
- Tests: `ctest --test-dir build/darwin-debug -R '<regex>' --output-on-failure`
- Full gui suite gate: same 83/86 baseline as the file-open plan — the 3 documented pre-existing failures (offscreen-GL + userflags engine-open) are the only acceptable failures. Any new failure is yours.
- Perf logging: `OPENSWMM_RENDER_PERF=1` (renderer stats), `QT_LOGGING_RULES="openswmm.load.*=true"` (load telemetry).

## Ground rules (from CLAUDE.md + plan — binding)

- Never reorder canonical mesh arrays; chunks/levels are remap-table views. Flat `tri*3+edgeLocal` ids and engine face order are load-bearing (plan §0.4).
- Surgical diffs; match existing conventions (`Qsg2D*`/`Mesh*` naming, env toggles, revision-keyed caches, `Qsg2DRenderStats` fields).
- Every stage ends with: build clean → stage tests pass → **commit** with a descriptive message (`mesh-lod: stage N — <summary>`). One commit per stage minimum; do not batch everything into one commit.
- If a stage gate cannot be met, STOP advancing. Record actual state in the plan's status header (what shipped, what failed, numbers), commit, and leave a `HANDOFF_MESH_LOD_<date>.md` describing the blocker. Never mark a gate passed that didn't pass.

## Stages (compressed from plan Phases 0–4; each Verify block in the plan is the exit gate)

### Stage 0 — Baseline + assets
- Mesh generator utility (`tests/perf-data/mesh/` output; committed generator, e.g. under `tests/tools/` or a test executable) producing 0.5M / 1M / 5M-tri synthetic terrain (analytic surface with channels + ridges so the pyramid has features to keep).
- Extend load telemetry with `openswmm.load.mesh` if absent.
- Record baseline numbers (QPainter live path; QSG mesh renderer via offscreen test harness): first paint, pan, zoom, style change, memory, uploaded bytes → fill plan §6 table.
- Gate: table filled; generator committed; all existing tests still pass.

### Stage 1 — Activate QSG mesh path + async mesh load (plan P1.1–P1.2)
- Instantiate `SWMM2DMeshQSGRenderer` in `resources/qml/swmmlayer.qml`; `qsgOwnsRendering()` gate on `SWMM2DMeshGraphicsItem::paint()` mirroring the results-layer pattern; preference + `OPENSWMM_QSG_MESH` env.
- Async mesh load: worker does `InpMeshReader::read` + scene-geometry data build; GUI adopts; `beginFileOpen`/`endFileOpen` plumbing (pattern: `FILE_OPEN_PROFILING_AND_ASYNC_IO_PLAN` P2.4, including the hidden-until-adopted guard).
- Gate: plan Phase 1 Verify block — parity screenshots at 3 zooms (QSG on/off), identify/pick + BC editing correct, no GUI freeze on 5M-tri load, gui tests at baseline.

### Stage 2 — Indexed chunked geometry (plan P2.1–P2.4)
- `MeshStaticGeometryBuffers` for mesh fill/edges; per-chunk `QSGGeometryNode`s from `MeshRenderChunkIndex` partitions with `chunkTri → trueTri` remap tables; LRU buffer pool (256 MB); lazy upload + stale-on-invisible color refresh; selection/BC id round-trip unit test.
- Gate: plan Phase 2 Verify block — uploaded bytes scale with visible chunks (stats prove it), 5M-tri pan ≥30 fps, ids identical to Stage 1, memory within budget.

### Stage 3 — Dot-product LOD pyramid + editing guarantee + toggle (plan P3.1–P3.7)
- Extract `DTMThinner` scoring core to operate on `MeshResult` via CSR adjacency; protected vertices (boundary/coupled/tagged/BC) never removed; threshold-ladder nested levels; per-level `maxPoints` + Poisson-disk options; worker-thread build with progress + `load.mesh` logging; sidecar cache (content-hash keyed, versioned).
- `Qsg2DLodPolicy` → level index from mapUnitsPerPixel with ~15% hysteresis; chunked levels with parent-level fallback; native-z preservation; edges/markers native-only; identify always native.
- **Editing (P3.7):** edit-tool activation forces native on visible chunks; all snapping/handles/editors read/write canonical arrays only; post-edit incremental patch + background dirty-region re-thin.
- **Toggle:** checkable `QAction` "True mesh (disable LOD pyramid)" on `TerrainToolbar` (`src/ui/toolbars/terraintoolbar.cpp`), wired to the active mesh layer (mirror `activeTerrainChanged` tracking); context-menu mirror; per-layer persistence via `ProjectSerializer`; `PreferencesManager` default; `OPENSWMM_MESH_LOD=0` app-wide kill-switch; status-bar hint on >1M-tri meshes.
- Gate: plan Phase 3 Verify block in full, including: per-level screenshots showing channels/levees/ridges legible; toggle live-switch + persistence test; vertex-Z edit round-trip byte-identical with pyramid on vs off; BC/selection ids unchanged.

### Stage 4 — Finalize
- Full gui + unit ctest run; record counts vs the 83/86 baseline.
- Fill all measurement tables in the plan; update the plan's status header phase-by-phase (convention: `QSG_PAN_ZOOM_OPTIMIZATIONS.md` style with ✅ per stage and real numbers).
- Update `CHANGELOG.md` (release-worthy: tiled LOD mesh rendering, true-mesh toggle, async mesh load).
- Final commit. If anything is deferred (e.g., Phase 4 optional items — raster bake, int16 quantization, results-renderer adoption — which are OUT of scope for this run), list it explicitly in the plan status.

## Out of scope for this run

Plan Phase 4 items (far-zoom raster bake, quantized positions, edge-collapse/QEM, results-renderer pyramid adoption). Do not start them.
