# Mesh Overhaul — Phase 7 (Compile, Test, Validate) Handoff — 2026-09-30, revision 4 (2026-10-01)

Instructions for the agent that compiles, tests and validates the mesh overhaul. Revision 3 moves
the validation onto the **triangle engine** (`workplans/MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md`,
Phase 8, decisions D10–D14 — read its §1, §3, §6 and §9 first). The vetted plans are, in order:
`workplans/MESH_OVERHAUL_PLAN_2026-09-29.md` (size field, terrain tolerance, trimming, corridors —
still in force), `workplans/MESH_OVERHAUL_PHASE6B_FEATURE_CAPTURE_2026-09-30.md` (terrain break
lines — in force; its mixed cell shape is superseded), and the triangle-engine plan (replaces the
quadtree core, the CDT fringe and the cell-shape option). Follow them (CLAUDE.md §5.0); do not
invent a new strategy. Everything below is written for a fresh session with no memory of the work.

Revision 4 (owner: "Commit the work and generate instructions for another agent to compile and
verify"): the overhaul is now **committed** on `swmm6_gui` in two commits (§1); Step 0 brings the
working tree up to them; everything else is revision 3 unchanged.

Revision 3 superseded revision 2. What changed then: the generator is now a constrained Delaunay
refinement with a guaranteed minimum angle and quads only in aligned feature strips; the cell-shape
and grid-orientation options and the quadtree are gone; coarsen default 20; constraint lines that
cross or nearly touch are joined (Bellinge's crossing pipes now mesh); the Bellinge runs and gates
are rewritten for triangles (Step 3); the lake at rest runs triangles and a quad region (Step 4).

## 0. Ground rules (from CLAUDE.md, restated because they bite here)

- Surgical changes only. The implementation is done; this phase is build, test, measure, document.
  Fix what breaks; do not "improve" adjacent code. Every changed line must trace to a step below.
- Transparent file IO (§4.1): every test input/output you create goes under
  `tests/output/mesh_overhaul_2026-09/` in the repo, never under `/tmp` or `$TMPDIR`.
- Plans, handoffs and instructions you write go in `workplans/` (CLAUDE.md §5.0).
- The overhaul is committed (§1); your own fixes are not: do not `git commit` or `git push` on the
  owner's Mac unless the owner asks. Report them with `git diff HEAD --stat`.
- If a gate cannot be met, say so with numbers. Do not weaken a gate silently — propose the revision
  in the triangle-engine plan (§6 / §9) with the reason.
- `CHANGELOG.md` carries the overhaul, 6b and triangle-engine entries under *[Unreleased]*; touch it
  only for user-visible changes you make.

## 1. Where things stand (2026-10-01 02:15 UTC)

| Item | State |
| --- | --- |
| Commits | `~/Documents/Projects/cbuahin_github/openswmm.gui`, branch `swmm6_gui`, on top of the owner's `e7e91c6e`: (1) **"Mesh overhaul Phases 0–7 as built on the Mac (quadtree engine)"** — patches 0001–0003 plus the Phase 7 session's edits, the state that built (`BUILD_STATUS=0`) and ran the revision-1/2 gates; (2) **"Mesh overhaul Phase 6b + Phase 8: the triangle engine"** — patches 0004 and 0005 plus the triangle-engine plan and this handoff. `git log --oneline -3` shows them. Neither commit has been compiled on the Mac in its final form; commit 2 is what Steps 1–4 verify. Not pushed. |
| Working tree | The commits were made without touching the working tree (a full build was running at 02:00 UTC), so its overhaul files are still commit 1's content: until Step 0, `git status` lists the 0004/0005 files as modified (the reverse of commit 2) — expected, not lost work. |
| Not committed, on purpose | `test_artifacts/` (patches, scripts, container harness) and `tests/output/` (review probes and renders, Phase 7 results, 30 MB meshes) — both ignored, kept on disk for review. The owner's own unrelated edits stay uncommitted: `src/plot/profileplotwidget.cpp`, `tests/gui/test_profileplot_crown_visibility.cpp` and its registration block in `tests/gui/CMakeLists.txt`, `.gitignore`, `CLAUDE.md`, `.claude/`, the `docs/LEGACY_MANUAL_CROSSWALK_2026-09-19.md` deletion, test data and output folders. Leave them alone. |
| Patch 0005 | `test_artifacts/mesh_overhaul/patches/0005-triangle-engine.patch`, made against the Mac tree as of 01:54 UTC **with 0004 applied**; `apply_0005.sh` applies 0004 first when it is not applied, then 0005, refuses while a build or test is writing files, and refuses if the tree moved (dry run; on failure it reverses 0004 so nothing is half-applied). `0005-base.md5` lists the base checksums (`md5sum -c`; on macOS compare with `md5 -r`). Applying it to the working tree is Step 0. |
| Phase 7 session edits | In commit 1 as they were; commit 2 carries over the region-flood fix (one visited set across floods — 713 subcatchment seeds made run C take 213 s) and its test `manyUnboundedRegionSeedsTagFirstWinsInLinearTime` into the new `meshgenerator.cpp`, and keeps the engine-harness edits (6 h engine wait, degenerate-cell regex, ctest timeout 25 200 s). An edit made after 01:54 UTC to a file 0005 touches is in neither commit — Step 0 catches it. |
| Results from revisions 1–2 | `tests/output/mesh_overhaul_2026-09/bellinge/report.md` (21:21, quadtree, tolerance 0.5 m): kept as history. Generator-dependent gates must be re-measured on the triangle engine. Build/ctest triage of non-mesh failures (§1a of rev 2: `test_subcatch_coverage_loadings`, `test_xsectsampler`, `test_profile_street_xsect`, `test_savewarnings`, …) stays valid. |
| Never compiled on the Mac | Everything in 0004 and 0005. GUI-linked files were syntax-checked in the container (`g++ -fsyntax-only`, Qt 6.4 + shim): `meshgenerationdialog.cpp`, `preferencesdialog.cpp`, `preferencesmanager.cpp`, `test_mesh_overhaul_bellinge.cpp`, `test_mesh_overhaul_engine.cpp`, `test_meshterrainpipeline.cpp`, `test_meshdialog_seeds.cpp`, `test_meshqualitydialog.cpp`, `test_twod_defaults_prefs.cpp`. CMake files were edited, never configured on the Mac. |
| Build | `build/` (Ninja, `SWMMVIS_BUILD_TESTS=ON`). `./build-gui.sh` runs `cmake --build build -j 8` into `build/build-gui.log` and prints `BUILD_STATUS=<n>`. |
| Review aids | `tests/output/mesh_triangle_engine_2026-09-30/`: `feature_capture/*.png` (street strip, city), `review/` probes (own `CMakeLists.txt`) with before/after images of the Bellinge strip defects and `bellinge_probe_output.txt`, `debug/` probes. Container harness: `test_artifacts/mesh_overhaul/harness/`. |

Working from a cloud session: the desktop bridge shell is a Linux VM with the Mac folder mounted at
`$HOME/mnt/openswmm.gui` — no Qt/CMake toolchain there. Builds and tests run on the Mac itself (the
owner runs them, or computer use through the desktop app). `ps` in the VM does not see Mac
processes; "is something running" means "were files written under `build*/` or `tests/output/`
in the last minutes" (what `apply_0005.sh` checks).

### 1a. Step 0 — bring the working tree up to the commits → verify: overhaul paths match `HEAD`

If a build, test or engine run from revision 2 is still running, let it finish or stop it at a clean
point (its results are superseded) and note where it stopped. Then:

```sh
cd ~/Documents/Projects/cbuahin_github/openswmm.gui
git log --oneline -3                                       # the two overhaul commits on e7e91c6e
test_artifacts/mesh_overhaul/patches/apply_0005.sh        # QUIET_MIN=5 to shorten the idle check
git diff HEAD --stat -- include src tests/gui CMakeLists.txt CHANGELOG.md workplans resources vendor
```

`apply_0005.sh` exit codes: 0 applied (or already applied); 2 something wrote files in the last
`QUIET_MIN` minutes (wait, or `FORCE=1` if you know nothing runs); 3 a dry run failed — it names
the files, and the tree is left as it was. The final `git diff` must list only the owner's unrelated
edits: `src/plot/profileplotwidget.cpp` and `tests/gui/CMakeLists.txt` (the
`test_profileplot_crown_visibility` block, 29 lines). Anything else under `include/mesh`,
`src/mesh`, the mesh dialog or the mesh tests is an edit made after 01:54 UTC that is in neither
commit: save it (`git diff HEAD -- <file> > tests/output/mesh_overhaul_2026-09/<name>.patch`),
report it, and decide with the owner before restoring the file to `HEAD`
(`git restore --source=HEAD -- <file>`). On exit code 3, do the same for the files it names, then
re-run the script.

## 2. Code map (what to look at when something fails)

| Module | Role |
| --- | --- |
| `mesh/meshcdt` + `vendor/predicates` | Constrained Delaunay kernel. `refineQuality(QualityOptions)`: Ruppert/Shewchuk refinement — encroached subsegments split (concentric shells at acute input corners), bad triangles get Üngör off-centres, a point that would encroach a subsegment splits it instead, fixed (strip) subsegments are never split, Shewchuk's small-angle exemption, `minEdge` floor against cascades, insertion cap, cancellation. `QualityReport` counts. |
| `mesh/meshgenerator` | `generate()`: size function → constraint polylines → **joining** (lines closer than 0.25 h_min share vertices, crossings split) → strips (user patches, four-sided quad regions, conduit strips cut into runs at crossings and sharp bends) → terrain lines (cut back from everything) → facing-pair street strips → CDT → `refineQuality` (θ, mean edge = h via factor 1.22) → assembly (strips stitched by coordinate, region tags by one shared flood). `GenerationStats` via `stats()`; `previewTerrainBreaklines()`. |
| `mesh/breaklinestrips` (new) | `findFacingPairs`: two DEM break lines facing each other (normal rays, parallel within 20°, steady width, lower ground between — `RefineHook::elevationAt`) → street/ditch strip. |
| `mesh/meshpatch` | Structured patches; `PatchLine` = the conduit along a strip's middle row (keeps marker/tag). |
| `mesh/sizefield` | h(x) as before; **D13:** inside the cone of a DEM step the generator keeps as an edge, the terrain term is ignored (`SizeFieldOptions::steps`). `terrainBoundAt` removed. |
| `mesh/terrainsizefield`, `mesh/terrainbreaklines` | Unchanged from 6b. |
| `mesh/pslgprep` | + `unfoldPolyline` (conduit alignments stored backwards / stray vertices — used by the worker). |
| `mesh/meshquadtree` | **Deleted** with its test. `meshquadmatch` / `meshquadcleanup` remain, unused by the generator. |
| `ui/dialogs/meshgenerationdialog` | Quality tab: Minimum angle `meshMinAngleSpin` (20–33°, default 30), Quads between facing break lines `meshStreetQuadsBox` (on), Conduit quad strip width `meshConduitStripSpin` ("(off)" = 0); cell-shape combo and grid angle removed. Worker: conduits unfolded then resampled, `cs.stripWidth`, `sizeOptions.steps = g.previewTerrainBreaklines()`, `hook.elevationAt`, logs `[Mesh] quad strips: …` and `PipelineResult::generationStats`. |
| `core/preferencesmanager` | `TwoDDefaults`: `meshCoarsenFactor = 20`, `meshMinAngleDeg = 30` (key `MeshMinAngleDeg`, clamped 20–33), `meshQuadsBetweenBreaklines = true`; `meshCellShape` removed (key no longer read). |
| `mesh/meshcellstats` → `computeGradingStats` | Longest-edge ratio across shared faces (+ histogram), orthogonality, min angle, `cellsBelow10Deg`. |

Expected container results (22 suites green; case counts exclude init/cleanup, data rows count):

| Suite | Cases | Notes |
| --- | --- | --- |
| test_meshcdt | 16 | quality bounds, coarse, graded, small angles, fixed segments, overlap chains, the floor, 1.6 M triangles ≈ 2 s |
| test_meshgenerator_v2 | 10 | min angle ≥ 30 everywhere, crossings share a vertex, near-touching inputs joined, 1000 region seeds (≈ 1.2 s), 2.4 M cells ≈ 4 s |
| test_meshstrips (new) | 9 | facing pairs, street, conduit middle row, networks, crossings keep strips, sharp bends, manholes |
| test_meshfeaturecapture | 8 | street strip, triangles without streets, city 1 km² (36 k cells); CSVs to `tests/output/mesh_triangle_engine_2026-09-30/feature_capture/` |
| test_pslgprep | 13 | + `unfoldPolyline_reversedListsAndHairpins` |
| test_sizefield_v2 | 11 | + `stepConesAreIgnoredButRoughGroundIsNot` |
| test_meshgenerator / test_meshpatch / test_meshpatchplacement | 11 / 177 / 26 | |
| test_terrainbreaklines, test_meshsizefield, test_meshquadquality, test_meshquadmatch, test_meshquadcleanup, test_meshcellstats, test_pslgtrim, test_meshquadregion, test_meshstagecache, test_dtmraster_samplemany, test_corridorsource, test_nninterp* | all pass | |
| test_meshterrainpipeline, test_meshdialog_seeds, test_meshqualitydialog, test_twod_defaults_prefs | — | GUI-linked: **never run**. Street-curbs case now has a streets-on run; dialog tests use the new object names. |

## 3. Steps

Each step has a verification. Do them in order; stop and report at the first gate you cannot meet.

### Step 1 — Reconfigure and build on the Mac → verify: `BUILD_STATUS=0`

```sh
cd ~/Documents/Projects/cbuahin_github/openswmm.gui
cmake -S . -B build            # new breaklinestrips sources, test_meshstrips; meshquadtree gone
./build-gui.sh                 # log: build/build-gui.log
grep -n "error:" build/build-gui.log | head -40
```

Likely first-compile issues: a file still including `mesh/meshquadtree.h` (drop it); removed fields
`GenerationOptions::{trianglesOnly, trianglesAtTerrain, frameAngleDeg}`,
`RefineHook::terrainBoundAt`, `TwoDDefaults::meshCellShape`, the dialog's `m_cellShapeCombo` /
`m_gridAngleSpin` — adapt like the container versions of the tests; missing transitive includes
(GCC is laxer — add the include where the error points). Apple clang warnings from
`vendor/predicates/predicates.c` are suppressed in `CMakeLists.txt`; never edit `predicates.c`.
`build/tests/gui/` keeps stale executables of deleted tests (`test_meshquadtree`, …); re-running
CMake drops them from ctest.

### Step 2 — ctest → verify: 0 failures, or every failure triaged in the report

```sh
ctest --test-dir build -R 'mesh|pslg|sizefield|terrain|nninterp|dtmraster|corridor|twod_defaults' \
  --output-on-failure -j 4 2>&1 | tee tests/output/mesh_overhaul_2026-09/ctest_mesh.log
ctest --test-dir build -C Release --output-on-failure -j 4 2>&1 | tee tests/output/mesh_overhaul_2026-09/ctest_full.log
```

Compare counts with §2. Triage each failure as (1) a test asserting the old algorithm (quadtree
counts, quad shares, cell-shape combo) — adapt keeping its intent; (2) a real regression — fix it in
the module, add or extend a unit test, rerun all mesh suites; (3) unrelated to the overhaul (touches
nothing in `include/mesh src/mesh src/ui/dialogs src/core/preferencesmanager.cpp`) — report, don't fix.
Timing asserts were set on a container core; if one fails only on time, record the time and flag it.

Visual check: regenerate and open `tests/output/mesh_triangle_engine_2026-09-30/feature_capture/`
(`python3 …/feature_capture/plot_mesh.py street_strip`): quads between the curbs, triangles
elsewhere, smooth grading away from the street.

### Step 3 — Bellinge metrics → verify: `tests/output/mesh_overhaul_2026-09/bellinge/report.md`

Inputs as in revision 2: model `examples/bellinge_2d/BellingeSWMM_v021_nopervious.inp`, DEM
`examples/bellinge_2d/output_SRTMGL1.tif` (SRTM, whole metres — tolerances under ~3 m trace noise),
domain `tests/output/mesh_overhaul_2026-09/bellinge/domain.geojson` (exists).
Harness `tests/gui/test_mesh_overhaul_bellinge.cpp` (0005 rewrites its run table and gates; env
`SWMMVIS_MESH_OVERHAUL_BELLINGE=1`). Runs A–D constrain nodes only; E/F add the 1 015 conduits.

| Run | Cell | Tolerance | Min angle | Conduits | Purpose |
| --- | --- | --- | --- | --- | --- |
| A | 10 | 0 | 30 | — | grading and quality |
| B | 10 | 3 | 30 | — | terrain term + DEM break lines on SRTM |
| C | 3 | 3 | 30 | — | timing, memory, determinism (1/4/8 threads) |
| D | 10 | 3 | 33 | — | highest minimum angle |
| E | 10 | 3 | 30 | as lines | crossing pipes joined (13 in plan) |
| F | 10 | 3 | 30 | 4 m strips | conduit strips |

All runs: ratio 1.5, coarsen 20, min cell = cell/4, trim 5° / 0.1·cell, node mapping on.

Gates (triangle-engine plan §6; written into the harness):

| Metric | Gate |
| --- | --- |
| Grading | P50 ≤ 1.3; faces with neighbour ratio > 2 ≤ 0.05 %; max and P95 reported |
| Angles | triangles under θ (all causes) ≤ 0.5 %; under θ not at a small input angle (`generationStats.trianglesBelowAngle`) ≤ 0.1 % of cells; cells < 10° ≤ 0.05 %; min angle reported |
| Quads (when any) | all convex, min SJ ≥ 0.5 |
| Size conformity | ≥ 75 % of cells with mean edge in [0.7 h, 1.4 h] (reference field rebuilt from nodes, + conduits on E/F); report only on F |
| Terrain (tolerance > 0) | RMS ≤ tolerance, max ≤ 3 × tolerance at centroids |
| Topology | interior edges in exactly 2 cells, no zero-area cells, no duplicate vertices |
| Boundary trim | ≥ 50 % of ring vertices removed |
| Coupling | 0 nodes lost (vertex, cell coupling or node map) |
| C | byte-identical `.2dm` at 1/4/8 threads; ≤ 10 s single-thread; ≤ 200 B/cell peak-RSS growth |
| Orthogonality, strips placed/dropped | reported only |

Container expectations for E/F (probe on the same network without the DEM, bounding-box domain —
`review/bellinge_probe_output.txt`): as lines ≈ 69 k cells, ratio max 2.17, 11 cells < 10° (two
pipes leaving a manhole a few degrees apart), 0.026 % non-exempt under θ; 4 m strips ≈ 270 k cells,
≈ 835 strips placed / ≈ 200 dropped, SJ ≥ 0.93. The log line
`[Mesh] constraint lines joined within … : … merged, … put on a line beside them, … crossings split`
and `[Mesh] … conduit alignment(s) unfolded` confirm the repair ran.

If a gate fails: check the options reached the generator (log `openswmm.mesh.perf`:
`[Mesh][grading] size field …`, `[Mesh] quad strips: …`), then the metric definition, then the
algorithm. Record the numbers either way.

### Step 3b — Urban lidar case → verify: `tests/output/mesh_overhaul_2026-09/urban/report.md`

As revision 2 (ask the owner for a lidar tile; otherwise write "not run — no lidar"): tolerance 0.1
and 0.15 m, cell 4 m, min cell 0.5 m, streets on. Visual gate: kept lines follow curbs and building
outlines; streets between curbs become quad strips; triangles elsewhere grade smoothly; no
spaghetti on lawns (if there is, the tolerance is below the lidar noise — say so).

### Step 4 — Engine round trip → verify: lake at rest exact, storm continuity ≈ 0

`tests/gui/test_mesh_overhaul_engine.cpp` (env `SWMMVIS_MESH_OVERHAUL_ENGINE=1`).

1. **Lake at rest** — rows `triangles` and `quad-region` (a four-sided region 2 m in from each end,
   ±0.6 m across, spacing 0.4: ≈ 160 quads among ≈ 230 triangles in the container). Gate: max
   |h − h₀| = 0 and volume to 1e-9. Outputs under `tests/output/mesh_overhaul_2026-09/engine/lake_at_rest/`.
2. **Sloping storm** — `bellinge_T8.inp` with run B's mesh substituted (copy under
   `tests/output/mesh_overhaul_2026-09/engine/bellinge/`). Gate: `.rpt` 2D continuity 0.000 % (compare
   the shipped mesh's report), no degenerate-cell warnings. The engine harness waits up to 6 h
   (the Phase 7 session's quadtree run-B mesh, 97.5 k cells, took 4 h 19 min); the triangle
   engine's run B should have fewer cells at coarsen 20 — record its count and the wall time.
   Run F's mesh (≈ 4 × the cells) only if the owner asks.

### Step 5 — Close out → verify: documents updated, owner summary delivered

- Triangle-engine plan §9: add the Mac numbers next to the container ones; any gate revision with its
  reason. Main plan Progress line: "Phase 7 complete <date>" or what remains.
- `CHANGELOG.md`: only for user-visible changes you make.
- Owner summary: gates table with pass/fail, every test adaptation with its reason, open items (§5 +
  new), and `git status --short` so they can commit.

## 4. Verification checklist (copy into the report)

- [ ] Step 0: `apply_0005.sh` ran; `git diff HEAD` lists only the owner's unrelated edits
- [ ] `BUILD_STATUS=0` in `build/build-gui.log`
- [ ] Mesh suites match §2 counts; full ctest N passed / 0 failed (logs in `tests/output/mesh_overhaul_2026-09/`)
- [ ] GUI-linked cases pass: terrain pipeline (streets on), dialog seeds, quality dialog, 2D defaults
- [ ] Bellinge A–F: `metrics.csv`, `report.md`, meshes written; joining/unfold log lines present on E/F
- [ ] Grading, angle, quad, conformity, terrain, topology, trim and coupling gates
- [ ] Run C ≤ 10 s, ≤ 200 B/cell; determinism across thread counts
- [ ] Urban lidar case run (or recorded as not run)
- [ ] Lake at rest exact (triangles, quad region); Bellinge continuity 0.000 %
- [ ] Plans updated; fixes left uncommitted for the owner (list them)

## 5. Known open items (decide with the owner, don't resolve silently)

1. **Region-layer leniency** (revision 2 item 1): a missing region layer logs and continues.
2. **Tolerance vs DEM noise** (revision 2 item 4): one tolerance drives terrain size and break lines;
   below the DEM noise both follow noise (SRTM < 3 m). Only a log warning exists.
3. **Quad regions touching the domain or another region** get triangles (the strip checker needs
   h_min of room). Owner to confirm, or split such regions.
4. **Strip width vs cell size.** Strips narrower than the cell size make smaller cells beside them
   (Bellinge F ≈ 4 × the cells of E). A strip-width default tied to the cell size is an owner call.
5. **Small input angles** (pipes leaving a manhole a few degrees apart) keep a few cells under θ in
   any mesher; they are counted, not hidden.
6. **Unused modules**: `meshquadmatch` / `meshquadcleanup` (D11) — delete in a follow-up if pairing
   never returns.
7. **Coupling policy**: joining moves line vertices by < 0.25 h_min but never a pinned node; a conduit
   end at a node demoted by the min-separation rule can move onto a nearby vertex (coupling then via
   the node map). Confirm with E/F's coupling gate.
8. **`docs/gui-baseline/PHASE21_TERRAIN_SIZE_HANDOFF.md`** still describes the retired
   `TerrainSizeInput` — historical; flag if the docs site builds it.

## 6. Sync note

The container that produced patches 0001–0005 is not reachable from a new session; the Mac tree is
the source of truth. The overhaul is the two commits on `swmm6_gui` (§1): review them with
`git show --stat HEAD~1` and `git show --stat HEAD` (once Step 0 has run, `git diff HEAD` holds only
the owner's unrelated edits and any fixes you make). 0005 was made against a Mac-equivalent tree (the
Mac's files at 01:54 UTC + 0004); `apply_0005.sh` verifies that by dry run.
