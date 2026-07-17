# Very Large Meshes — Chunked (Tiled) + LOD-Pyramid Rendering Plan

> Created: **2026-07-13** · Status: **IN PROGRESS (2026-07-16)**
>
> - **Phase 0 ✅ DONE 2026-07-16** — generator (`tests/tools/meshperfgen.cpp` → `mesh_perf_generator` target), fixtures in `tests/perf-data/mesh/` (gitignored; README documents regeneration), `openswmm.load.mesh` logging category added, baseline harness `tests/gui/test_meshperf_baseline.cpp` (self-skips without fixtures), §6 table filled. Verdict: the 5M mesh breaks in three places — ① synchronous load = **36.4 s GUI freeze** (13.8 s parse + 22.7 s scene build), ② QPainter mid/near-zoom frame = **3.7–3.8 s** (native-tri fill; full-extent is fine at 320 ms thanks to the quad-bake overview), ③ scene caches = **~1.0 GB** resident. QSG full-extent first paint 309 ms (needs Phase 3 pyramid for the <33 ms target); QSG warm-grab times in the harness read 0 ms (grabWindow caching) — per-frame QSG costs to be re-measured through the MapCanvas pipeline in Phase 1/2.
> - **Phase 1 ✅ CODE + HEADLESS VERIFY DONE 2026-07-16 — awaiting interactive UI sign-off** (see `HANDOFF_MESH_LOD_UI_TESTING_2026-07-16.md`). P1.1: `SWMM2DMeshQSGRenderer` live in `swmmlayer.qml` (bottom of the QSG stack), MapCanvas single-owner handoff for the topmost visible mesh layer (mask ⇒ CPU; CPU-rendered 2D results on screen ⇒ mesh stays CPU to preserve stacking; 1D kinds forced into the QSG frame while the mesh owns it), `SWMM2DMeshLayer::qsgOwnsRendering()` gates the QPainter item, preference `Rendering/QsgMeshEnabled` (default ON, Preferences → GPU Rendering checkbox), env kill-switch `OPENSWMM_QSG_MESH=0`. P1.2: `SWMMVis::attachMesh2DLayersAsync` — parse + scene build on a QtConcurrent worker (layer built there, `moveToThread` to GUI), adoption + prior-run HDF5 attach in the completion handler, hidden-until-adopted, busy bar + `load.mesh` timing. Tests: `test_meshasyncload` (pref default/signal, CPU-gate + pick invariance, worker≡sync build, parity screenshots to `tests/output/mesh_qsg_parity/`); full suite **92/92 gui + 42/42 unit** (better than the 83/86 doc baseline — the 3 historical offscreen failures don't reproduce locally). Headless limitation: offscreen QPA can't read back QSG pixels, so QSG-vs-QPainter visual parity, identify/BC-editing feel, and the no-freeze 5M open must be confirmed in the live app before Phase 2 starts.
> - **2026-07-17 — evaluation-round fixes + progressive load + style plumbing.** ① GL >65k-vertex node truncation fixed (chunked uploads); QML-registration order fixed; premultiplied-alpha + LOD-threshold fixes (see git log `3ea1ad5`/`67c6cb7`/`4cc6964`). ② **Progressive load** (owner request): `SWMM2DMeshLayer(deferHeavyGeometry)` Phase A builds fill + LOD pyramid only → layer joins the canvas and renders coarse immediately; `finishSceneGeometryAsync()` builds wireframe edges/spatial grids/adjacency/BC slots on a worker and swaps them in (renderers force the pyramid until grids exist; z-edits fall back to a light rebuild mid-defer). Stage notes + busy bar bracket every phase. ③ **Style audit + fixes** (agent-audited, findings in this section): terrain-fill colour ramp + invert now surfaced in the mesh style editor (previously only the *band* ramp was — the reported "ramp doesn't change the relief"); QPainter fill now honours the fill scheme (parity with QSG's `schemeDrivesColor`); `hillshadeMinLit` was a `thread_local` global — GPU path never saw edits — now a per-layer member; inert `attribute` rows hidden for mesh sublayers (terrain always classifies by elevation).
> - **OWNER DIRECTION (2026-07-17):** async/progressive loading is the standard for ALL layer types — boundaries/elements drawn as they become available, stage logs before/after, status-bar progress, "seamlessness". The mesh progressive load is the first instance; generalising to model/results/raster/vector streaming render is a follow-on work item.
> - **Deferred style-parity gaps (audit 2026-07-17, QPainter-fallback-only + dead knobs):** QPainter ignores MeshEdgeStyle (color/width/slope-break/wide/scheme), MeshNodeStyle::shape, and isoline labels; dead-in-both knobs: edge/isoline `dashPattern`, node `outlineColor/WidthPx`, band `smoothBands`/`belowMinColor`/`aboveMaxColor`, isoline `labelDecimals/FontPt/Halo`, `indexEvery/indexWidthPx`. Fix or remove during Stage 2/3.
> - Phases 2–4 not started (stop point: owner UI testing).
>
> Predecessors (read before implementing): `MESH_CULLING_OCCLUSION_AND_LEGEND_PLAN.md` (5M-tri targets; C2/C4/C5 unbuilt), `QSG_2D_1M_RENDERING_AGENT_INSTRUCTIONS.md` (Phases 1–8 helpers all shipped), `RASTER_PYRAMID_RENDERING_PLAN_2026-07-10.md` (the raster analogue), `FILE_OPEN_PROFILING_AND_ASYNC_IO_PLAN_2026-07-10.md` (async-open pattern, `openswmm.load` logging — implemented).

## Does the raster tiling mechanism transfer to meshes?

**The concept transfers; the primitive changes.** Raster tiles are resampled pixel blocks; a mesh is indexed triangle topology on the QSG (GPU) path, so the correct analogues are:

| Raster mechanism | Mesh analogue |
|---|---|
| 256-px tile keyed `level/col/row` | **Spatial chunk** of triangles (`MeshRenderChunkIndex` — already exists, ~4096 cells/chunk) with its own index range / geometry node |
| GDAL overview pyramid (2,4,8,…) | **Decimation pyramid**: N pre-built simplified meshes via **normal-deviation (dot-product) thinning** — the `mesh::DTMThinner` criterion applied per level with a descending threshold ladder, so coarse levels still capture ridges/channels/breaklines |
| `selectTileMatrix` LOD pick from mapUnitsPerPixel | Same math picking a pyramid level (extend `Qsg2DLodPolicy` Far/Mid/Near buckets to level indices) |
| `QCache` of tile QImages | Per-chunk GPU vertex/index buffers, uploaded lazily for visible chunks, evicted LRU |
| Tile fallback from parent level | Draw coarser-level chunk until fine chunk's buffer is built |
| Literal raster tiles | **Optional far-zoom raster bake** (C5 in the culling plan) — at extreme zoom-out, render the mesh once into the `MapRenderJob` raster buffer and reuse the actual raster tile pipeline |

## 0. State of the world (verified 2026-07-13 — surprising, read carefully)

1. **The live static-mesh render path is QPainter**, `SWMM2DMeshGraphicsItem::paint()` (`src/layers/swmm2dmeshlayer.cpp:106`, comment at `:124`). `SWMM2DMeshQSGRenderer` is fully implemented — dirty-state, LOD policy, grid culling, fill-RGB cache, matrix-only pan (`src/map/swmm2dmeshqsgrenderer.cpp`) — but **dormant**: `resources/qml/swmmlayer.qml` instantiates only the results (`results2dRenderer`) and 1D renderers.
2. **Time-varying results already use the target architecture** (`SWMM2DResultsQSGRenderer`): `MeshStaticGeometryBuffers` (indexed, positions built once per geomRevision), color-bytes-only updates per timestep, `MeshRenderChunkIndex` visibility batching, async contours, optional shader color mode.
3. **What's missing for very large meshes:**
   - Mesh QSG fill is **expanded non-indexed** (3 verts/tri, `ColoredPoint2D`) — ~180 MB re-upload per dirty frame at 5M tris (culling plan §1.1). Indexed static buffers (E1) landed only on the results renderer.
   - **One geometry node per pass**, not per chunk — any rebuild rebuilds *everything visible*; no per-chunk upload/eviction.
   - **Single-level LOD**: `rebuildOverview()` (`swmm2dmeshlayer.cpp:893`, ≥200k tris, ~15k-cell quad bake). No pyramid, no edge-collapse (C4 unbuilt).
   - **Mesh load is synchronous on the GUI thread**: `InpMeshReader::read` + layer ctor `rebuildSceneGeometry()` at `src/swmmvis.cpp:4477`.
   - `SceneTri` AoS (`QPointF` a,b,c + 4 floats ≈ 64 B) duplicates vertices ×~6 vs indexed SoA — ~320 MB resident at 5M tris.
4. **Hard constraints on chunk/LOD reordering** (breakage risks — the implementing agent must respect these):
   - Per-edge BCs, boundary flags, and selection use **flat index `tri*3 + edgeLocal`** (`swmm2dmeshlayer.h:205-208, :547`).
   - HDF5 (`/Mesh2_face_depth [nTime,nFace]`) and live engine ticks are in **engine face order**; depth lookup, identify/pick (`pickCellsInRect/Polygon`), and 1D coupling all key on true face ids. **Never reorder the canonical arrays — chunks hold index remap tables only** (culling-plan Q5).
   - Vertex→triangle CSR adjacency (`m_vertTriPtr/m_vertTriIdx`) and `findEdgeNeighbour()` symmetry must stay valid.
5. **Instrumentation exists:** `Qsg2DRenderStats` via env `OPENSWMM_RENDER_PERF=1` — per-pass built vertices, uploaded bytes, repaint ms. Env toggles pattern: `OPENSWMM_QSG_INDEXED_FILL`, `OPENSWMM_QSG_SHADER_FILL`.

**Targets (inherited, unchanged):** 5M+ triangles; pan/zoom ≥30 fps; full-extent idle <33 ms; time-scrub <50 ms initially, 16–33 ms eventually; stable memory.

---

## Phase 0 — Baseline + test assets (gate for everything else)

1. Generate reviewable synthetic meshes at 0.5M / 1M / 5M triangles into a documented folder (e.g. `tests/perf-data/mesh/`, per CLAUDE.md Transparent File IO — no temp dirs). A small generator tool/test producing `.2dm` is acceptable and should be committed.
2. With `OPENSWMM_RENDER_PERF=1`, record for each size on **both** paths (QPainter live path; QSG mesh renderer temporarily instantiated in a test harness): full-extent first paint, pan frame, zoom frame, style change, memory (resident + uploaded bytes from stats). Also record mesh load time (`openswmm.load` category — extend to a `load.mesh` sub-category if absent).
3. Fill the table in §6. **Exit criteria:** numbers show where the 5M mesh actually breaks (upload volume vs per-frame color eval vs QPainter raster fill), so Phases 2–4 can be re-scoped.

## Phase 1 — Activate the QSG mesh path + async mesh load (foundations)

- **P1.1** Instantiate `SWMM2DMeshQSGRenderer` in `swmmlayer.qml` behind a preference (`PreferencesManager`, default ON like `qsgRenderEnabled()`, env kill-switch `OPENSWMM_QSG_MESH=0`). Gate the QPainter item with a `qsgOwnsRendering()` early-return exactly as `SWMM2DResultsLayer` does (`swmm2dresultslayer.h:402` pattern). Keep the QPainter path compiled as fallback.
- **P1.2** Move mesh load off the GUI thread using the now-established async-open split (worker: `InpMeshReader::read` + `rebuildSceneGeometry` data work → GUI: adopt + emits), with `beginFileOpen`/`endFileOpen` logging + busy bar, matching `FILE_OPEN_PROFILING_AND_ASYNC_IO_PLAN` P2.4's hidden-until-adopted guard.
- **Verify:** all mesh gui tests pass with QSG on and off; visual parity screenshots (fill/edges/contours/selection) at three zooms; identify/pick and BC editing still correct; large mesh loads without GUI freeze.

## Phase 2 — Indexed chunked geometry ("tiles")

The core tiling step. On the mesh QSG renderer (and shared with results where applicable):

- **P2.1** Adopt `MeshStaticGeometryBuffers` for the mesh fill/edge passes (indexed; positions anchor-relative, built once per geomRevision) — replaces the expanded 3-verts/tri path. Reuse the `OPENSWMM_QSG_INDEXED_FILL` toggle convention.
- **P2.2** Partition triangles by `MeshRenderChunkIndex` chunk id into **per-chunk index ranges** (remap tables `chunkTri → trueTri`; canonical arrays untouched, per constraint §0.4). One `QSGGeometryNode` per visible chunk per pass, children of the existing root transform node.
- **P2.3** Visibility: chunks fully outside the coverage rect are detached (buffers retained in an LRU pool with byte budget, proposed 256 MB, preference-tunable); newly visible chunks upload lazily. Dirty ticks (style/data) rewrite color bytes only for **visible** chunks — invisible chunks marked stale and refreshed on re-entry.
- **P2.4** Selection/BC overlays: translate flat `tri*3+e` ids through the chunk remap at draw time; add a unit test asserting round-trip id fidelity on a chunked mesh.
- **Verify:** stats show uploaded bytes per pan/zoom scale with *visible* chunks, not mesh size; 5M-tri pan ≥30 fps; identify returns identical ids to Phase 1; memory stays within budget while panning across the full mesh.

## Phase 3 — Terrain-adaptive LOD decimation pyramid ("overviews")

**Decimation criterion: the `mesh::DTMThinner` normal-deviation (dot-product) strategy** (`include/mesh/dtmthinner.h`, `src/mesh/dtmthinner.cpp`, Slice AU.5b), re-targeted from the DTM grid to the loaded triangle mesh. Rationale: score = min (or avg) `dot(area-weighted vertex normal, fan face normal)` keeps vertices exactly where terrain bends — channels, levees, ridges — so every pyramid level remains a *faithful terrain surface*, unlike resolution-uniform grid binning. Threshold ladder = pyramid levels.

- **P3.1** Extract the scoring/removal core of `DTMThinner` into a shared routine that operates on `MeshResult` topology instead of the raster 8-ring:
  - Vertex fan comes from the existing CSR adjacency (`m_vertTriPtr`/`m_vertTriIdx`); area-weighted vertex normal from incident triangle normals (flip downward normals into the upper half-space, as DTMThinner does).
  - `score >= threshold` → smooth → removable; boundary vertices (`boundaryEdges`), vertices with `coupledNode` set, and tagged/BC-carrying vertices are **never removed** (analogue of DTMThinner's "boundary pixels are never removed").
  - Batch-remove per pass with dirty-set rescoring (same optimization as DTMThinner); local hole retriangulation via fan closing; fall back to a constrained Delaunay re-triangulation of retained vertices per level if fan closing produces slivers (decide during implementation against quality metrics, min-angle > 20°).
- **P3.2** Pyramid levels = descending dot thresholds, proposed ladder **L1 0.90 (coarse) / L2 0.95 / L3 0.99 / native** (matching the documented threshold guide: ~26° / ~18° / ~8° bends), each level thinned from the next-finer level so vertex sets are nested (progressive refinement, cheaper builds). Optional per-level `maxPoints` cap and Poisson-disk min-spacing (both already `DTMThinnerOptions` concepts) to bound worst-case level size on flat terrain. Keep the existing quad-bake `rebuildOverview()` only as the emergency coarsest fallback for degenerate cases (e.g., all-flat meshes where thinning removes nearly everything). Build on a worker thread post-load with progress → status bar + `load.mesh` log lines (mirror the raster `.ovr` build UX). Skip levels the mesh is already smaller than.
- **P3.3** Level selection: extend `Qsg2DLodPolicy` from Far/Mid/Near to a level index chosen from `mapUnitsPerPixel` vs per-level mean triangle size — same shape as `WMTSLayer::selectTileMatrix`. Hysteresis (~15%) to avoid level thrash at boundaries.
- **P3.4** Chunk the pyramid levels too (Phase 2 machinery is level-agnostic); parent-level chunk drawn as fallback while a finer chunk uploads (XYZ parent-tile pattern).
- **P3.5** Semantics at coarse levels: retained vertices keep their **exact native z** (DTMThinner rule: "use values directly, do not re-sample"), so hillshade/ramps/contours at coarse levels are computed from real terrain, not averages. Edge/marker passes drawn only at native or near-native levels; contour bands may march on any level (they benefit most from terrain-faithful decimation). Identify/pick always resolves against **native** arrays regardless of drawn level.
- **P3.6 (decide at review):** persist the pyramid to a sidecar cache file next to the mesh (analogous to `.ovr`) for >1M-tri meshes so reopen skips the rebuild. Proposed: yes, versioned binary blob keyed by mesh content hash + threshold ladder, reviewable location beside the source file.
- **P3.7 Editing mode — native topology guarantee (hard requirement).** The pyramid is display-only; edit tools always see the true mesh:
  - **User-facing toggle — "True mesh (disable LOD pyramid)":** a per-layer checkable action forcing native geometry at every zoom, pyramid fully bypassed. Surfaced as a checkable `QAction` on the **Terrain toolbar** (`src/ui/toolbars/terraintoolbar.cpp`, wired to the active mesh layer the same way it tracks the active terrain via `activeTerrainChanged`), with the same action mirrored in the mesh layer's context menu. Persisted per layer via `ProjectSerializer` and defaulted from a `PreferencesManager` setting; env kill-switch `OPENSWMM_MESH_LOD=0` for the whole app. Status bar hint when active on a >1M-tri mesh ("True mesh rendering — LOD disabled"), since full-extent frame rates will drop by design. This toggle is the manual override for editing sessions; the automatic enforcement below applies even when the toggle is off.
  - When any mesh edit tool is active (vertex-Z, BC/boundary, tags, coupling, Manning's), the renderer automatically forces **native level for all visible chunks** (coarse levels may persist outside the coverage rect) — equivalent to the toggle scoped to the edit session. Snapping, hover, selection, and the attribute/property editors read and write only the canonical `MeshResult` arrays and flat `tri*3+e` edge ids — never pyramid geometry. This is structurally guaranteed by ground rule 3 (chunks/levels are remap-table views), but edit-tool activation is the enforcement point.
  - Vertex/edge handles, rubber bands, and identify results are generated from native arrays even if a coarse level is momentarily on screen (e.g. during the native-chunk upload after tool activation).
  - After an edit commits: bump `geomRevision`, patch native chunk buffers via the existing incremental vertex-Z path (no full rebuild), and mark affected pyramid levels/chunks **stale** — they keep drawing (staleness is visually acceptable at coarse zoom) while a background worker re-thins only the dirty region (dirty-set rescoring makes this local). Sidecar cache is invalidated by the content-hash key automatically.
  - Since mesh topology is immutable after load (`Q_PROPERTY ... CONSTANT`, `swmm2dmeshlayer.h:65-67`), edits never change XY connectivity — only z and attributes — so pyramid XY structure stays valid and re-thinning is only needed where z changes could cross the dot threshold.
- **Verify:** full-extent view of 5M-tri mesh <33 ms; visual check that channels/levees/ridges remain legible at every level (side-by-side screenshots per level over a real DEM-derived mesh); level transitions no worse than the current single-overview swap; per-level triangle counts and build ms logged under `load.mesh`; pyramid build never blocks GUI; reopen with sidecar skips build (log line proves it). **Editing:** with a coarse level on screen, activating an edit tool snaps the view to native chunks; the Terrain-toolbar "True mesh" toggle switches native↔pyramid rendering live, persists through project save/reload, and its state is reflected when switching the active mesh layer; a vertex-Z edit on a 5M-tri mesh round-trips (edit → save → reload) byte-identical to the same edit with the pyramid disabled; BC/selection ids unchanged with pyramid on/off (extend the P2.4 round-trip test).

## Phase 4 — Deferred / optional (gate on Phase 2–3 numbers)

- Far-zoom **raster bake** (culling plan C5): render coarsest level once into the `MapRenderJob` buffer and skip QSG mesh entirely below a scale threshold — reuses the raster stale-buffer blit for free pan.
- Quantized int16 positions per chunk (E2) if upload bandwidth still dominates.
- Edge-collapse / quadric-error decimation (C4 alternative) only if the dot-product thinning pyramid shows unacceptable shape error — explicitly out of scope until the normal-deviation pyramid is proven insufficient (it reuses vetted `DTMThinner` logic; QEM would be new unvetted code).
- Results-renderer adoption of the chunk-LOD pyramid for animated depth at extreme zoom-out.

## Implementation-agent ground rules

1. Work phase-by-phase in the order above; each phase's **Verify** block is the exit gate — do not start the next phase with a failing gate.
2. Follow CLAUDE.md: surgical diffs, no new abstractions beyond what this plan names, match existing style (`Qsg2D*`/`Mesh*` conventions, env-var toggles, revision-keyed caches).
3. Never reorder canonical mesh arrays (§0.4). All chunk/LOD structures are derived views with remap tables.
4. Every new knob gets: a `PreferencesManager` entry or env var, a `Qsg2DRenderStats` field if perf-relevant, and a unit test (`tests/unit/`) or gui test (`tests/gui/`) — extend `test_qsg2d_renderstats`-style coverage.
5. Record before/after numbers in §6 with the Phase 0 protocol; update this document's status header per phase (convention of `QSG_PAN_ZOOM_OPTIMIZATIONS.md`).
6. Update `CHANGELOG.md` on release-worthy milestones.

---

## 6. Measurement table

Protocol: `test_meshperf_baseline` (offscreen QPA, 1200×800 viewport), synthetic
meshes from `mesh_perf_generator`, Debug build (`build/darwin-debug`), Apple
Silicon. Zooms: full = whole extent, mid = 1/8 linear, near = 1/64 linear.
"Resident MB" = SceneTri/SceneEdge/SceneNode/bbox scene caches (analytic).
QSG "first" = build + upload + render + grab; warm grabs read 0 ms
(grabWindow caching) so per-frame QSG cost is re-measured in Phase 1/2 via
`OPENSWMM_RENDER_PERF=1` through the MapCanvas pipeline.

| Mesh (tris) | Path | Scenario | ms / fps | Uploaded MB | Resident MB | Phase |
|---|---|---|---|---|---|---|
| 0.5M | load | read + scene build | 1352 + 1807 ms | — | 103 | 0 (baseline) |
| 0.5M | QPainter | full / mid / near paint | 45 / 364 / 394 ms | — | 103 | 0 |
| 0.5M | QSG | full / mid / near first | 40 / 27 / 1 ms | — | 103 | 0 |
| 1M | load | read + scene build | 2643 + 3748 ms | — | 206 | 0 |
| 1M | QPainter | full / mid / near paint | 78 / 731 / 771 ms | — | 206 | 0 |
| 1M | QSG | full / mid / near first | 81 / 45 / 6 ms | — | 206 | 0 |
| 5M | load | read + scene build | 13756 + 22681 ms | — | 1031 | 0 |
| 5M | QPainter | full / mid / near paint | 320 / 3741 / 3846 ms | — | 1031 | 0 |
| 5M | QSG | full / mid / near first | 309 / 31 / 26 ms | — | 1031 | 0 |

## Decision checklist for review

1. Approve activating the dormant QSG mesh renderer (P1.1) as the prerequisite, with QPainter fallback retained?
2. Chunk buffer LRU budget default 256 MB — OK?
3. Pyramid = normal-deviation (dot-product) thinning per the `DTMThinner` criterion, threshold ladder 0.90/0.95/0.99, quad-bake retained only as degenerate-case fallback — agreed? And within P3.1: fan-closing retriangulation with CDT fallback acceptable as an implementation-time decision?
4. Persist pyramid sidecar cache next to the mesh file (P3.6) — acceptable file placement?
5. Synthetic 0.5M/1M/5M test meshes committed under `tests/perf-data/mesh/` — acceptable repo weight, or keep generator-only?
6. Editing mode (P3.7): automatic enforcement forces native for **visible chunks only** while an edit tool is active (proposed), or native everywhere? The manual Terrain-toolbar "True mesh" toggle always forces native everywhere.
