# Large 2D Mesh — Save → Reload Failure Plan (2026-08-18)

**Repo:** `openswmm.gui` (reader/writer also cross-checked against `openswmm.engine`)
**Reported by:** user, Windows, external `.2dm` sidecar, ~1.5 M cells
**Symptom:** after saving a project and reopening it, the mesh does not load. Files on
disk look intact (`[2D_MESH_FILE]` present, `.2dm` present and full size).
**Method:** static reading of the save and reload paths. No instrumentation was run.
**Status:** AWAITING APPROVAL — fix program in §5; do not implement until approved.

---

## 1. Why this plan exists

The mesh round-trips correctly at small scale and the on-disk artifacts survive the
save, which rules out the write path losing data. The failure is therefore in the
**re-read**, and it is size-dependent. Reading the load path end to end turns up one
algorithmic defect that is quadratic in mesh size, three constant-factor amplifiers
on the same code, and two independent silent-drop bugs that are worth fixing in the
same pass.

Critically, the quadratic is **latent until the first save** — which is exactly the
reported trigger. See §3.

---

## 2. Findings

Every claim below was checked against the current tree. `[C]` = confirmed by reading,
`[H]` = hypothesis consistent with the code but not yet measured.

| # | Finding | File:line | Status |
|---|---|---|---|
| F1 | `[2D_VERTEX_NODE_MAP]` re-read is O(nCoupled × nVertices) | `inpmeshreader.cpp:167-177` | **[C]** |
| F2 | Writer emits tag form, guaranteeing F1's fallback branch | `inpmeshwriter.cpp:113-126` | **[C]** |
| F3 | `vertexToNode` is a `QHash`; "vertex-index order" comment is wrong | `inpmeshwriter.h:35`, `inpmeshwriter.cpp:109` | **[C]** |
| F4 | `scanUnitsHeader` splits the whole `.2dm` to find two comment lines | `inpmeshreader.cpp:339`, called `:556` | **[C]** |
| F5 | `tokenize()` runs a `QRegularExpression` split per line | `inpmeshreader.cpp:69-75` | **[C]** |
| F6 | `applyConveyanceRows` builds ~4.5 M separately-allocated `QVector<int>` | `inpmeshreader.cpp:468-478` | **[C]** |
| F7 | `formatTrianglesPreserving` re-splits original rows up to 3× per triangle | `inpmeshwriter.cpp:264, 278, 301, 321` | **[C]** |
| F8 | One save = 4 full read+split passes over the `.2dm` | `inpmeshwriter.cpp:806, 815, 816, 836, 785, 791, 515, 522` | **[C]** |
| F9 | `.oswp` mesh display state can never be restored (async ordering) | `swmmvis.cpp:4691` vs `:4785`/`:4929`; `projectserializer.cpp:896-917` | **[C]** |
| F10 | Phase-B/save race silently skips `patchBCSections` | `swmmvisprojectwindow.cpp:1367-1369`, `swmm2dmeshlayer.cpp:1238-1239` | **[C]** |
| F11 | Layer build peak allocation may exceed the Windows commit limit | §2.6 | **[H]** |

### 2.1 F1/F2/F3 — the quadratic (headline)

`patchAttributeSections` rebuilds `[2D_VERTEX_NODE_MAP]` on every external save
(`inpmeshwriter.cpp:845`). `formatVertexNodeMap` writes the **vertex tag**, not the
index, whenever the vertex carries one:

```cpp
// inpmeshwriter.cpp:120-123
if (!mv.tag.isEmpty())
    s << mv.tag;        // ← tag form
else
    s << vIdx;
```

On re-read, a tag never parses as an integer, so **every row** falls into the linear
scan:

```cpp
// inpmeshreader.cpp:167-177
int v = tok[0].toInt(&okv);
if (okv && (v < 0 || v >= out.vertices.size())) okv = false;
if (!okv) {
    v = -1;
    for (int i = 0; i < out.vertices.size(); ++i)      // full scan per row
        if (out.vertices[i].tag == tok[0]) { v = i; break; }
    if (v < 0) continue;                               // miss ⇒ no early break
}
```

Cost is `nCoupledVertices × nVertices / 2` `QString` comparisons. Two aggravators:

- `CouplingMap::vertexToNode` is a `QHash<int, QString>` (`inpmeshwriter.h:35`), and
  Qt 6 randomizes QHash iteration per process. The comment at `inpmeshwriter.cpp:109`
  ("Walk in vertex-index order so output is deterministic") is therefore **wrong**:
  rows are emitted shuffled, so the scan gets no locality benefit, and the `.2dm`
  diff churns on every save even when nothing changed.
- A **miss** runs the full `nVertices` iterations with no break (`:176`).

Calibration against this repo's own measurement — `GUI_LOAD_PERF_REVIEW_2026-08-13.md`
§A clocked 1.26×10¹⁰ `QString` compares at **22 s**:

| Coupled vertices | Compares (760 k verts) | Extrapolated |
|---:|---:|---:|
| 10,000 | 3.8×10⁹ | ~7 s |
| 50,000 | 1.9×10¹⁰ | ~33 s |
| 100,000 | 3.8×10¹⁰ | ~66 s |

Single-threaded, inside the Phase-A worker, with **no progress output** between the
`note()` at `swmmvis.cpp:5079` and the next at `:5103`. To the user this is
indistinguishable from "the mesh never loaded".

`[2D_TRIANGLE_NODE_MAP]` has the same fallback (`inpmeshreader.cpp:213-215`) over
1.5 M triangles, but after a save `patchAttributeSections` populates only
`cm.vertexToNode` (`:832-834`), leaving `triangleToNode` empty — so
`formatTriangleNodeMap`'s first loop emits nothing and only the index-form
`cellCouplings` rows are written. The triangle scan is **latent, not currently hot**.
It must still be fixed, or it fires the moment triangle coupling is written.

### 2.2 F4/F5 — redundant work in the reader

One reload of an external mesh performs **four** full `text.split('\n')` passes (two
on the `.inp`, two on the `.2dm`). The `.2dm` pair is pure waste:
`scanUnitsHeader` (`:339`) materializes a complete ~2.3 M-element `QStringList`
solely to locate two `;;` comment lines, discards it, and `parseSectionsFromText:369`
immediately splits the identical text again.

Every data line then goes through `tokenize()`, which does a `QRegularExpression`
split allocating a fresh `QStringList` — ~2.3 M transient allocations per load.

### 2.3 F6 — allocation storm in conveyance resolution

```cpp
// inpmeshreader.cpp:468-478
QHash<QPair<int,int>, QVector<int>> pairToSlots;
pairToSlots.reserve(mesh.triangles.size() * 3);   // 4.5 M
...
pairToSlots[key].append(t * 3 + e);               // per-key heap alloc
```

~4.5 M hash nodes each owning a separately heap-allocated `QVector<int>` ≈ 9 M small
allocations, ~400 MB. Guarded by `if (rows.isEmpty()) return;`, so it only fires when
`[2D_EDGE_CONVEYANCE]` is present — which `patchBCSections` writes whenever any edge
conveyance ≠ 1.0. Millions of small allocations is where the Windows LFH diverges
most sharply from glibc/macOS malloc.

### 2.4 F7/F8 — the save side is also quadratic in constant factor

`patchAttributeSections` → `patchBCSections` → `writeMeshFileRef` each call `readInp`
and `stripSections` independently: **four full reads and four full splits** of a
100-200 MB file per save. Inside that, `formatTrianglesPreserving` defines
`origDepthTok` as `origRows[i].simplified().split(...)` (`:264`) and calls it in the
`anyDepth` detection loop (`:278`) *and* twice more in the write loop (`:301`, `:321`)
— up to **4.5 M transient `QStringList` allocations per save** at 1.5 M triangles.

### 2.5 F9 — `.oswp` mesh display state is always dropped

`projectserializer.cpp:887-889` asserts:

> *"openSingleINP has already auto-loaded the mesh layer ... **before** applySession runs."*

That is stale. In `swmmvis.cpp`, `ProjectSerializer::applyFromFile` is `:4691`;
`attachMesh2DLayersAsync` is `:4785` and is `QtConcurrent::run`, with the layer
joining the canvas only in the watcher's `finished` handler at `:4929`. So the
`bySource` lookup at `projectserializer.cpp:896-905` is **always empty**, and every
saved entry hits `:917`'s `continue  // skip silently`. Active-mesh flag, edge/node
visibility, hillshade and contour state are discarded on every open, at every mesh
size.

The 2D-**results** block eight lines below (`:944-948`) already documents and works
around this exact race by stashing entries for the async load to consume. The mesh
block never got the same treatment when the Tiled-LOD P1.2 async refactor landed.

### 2.6 F10/F11 — save-time race and the memory ceiling

`swmmvisprojectwindow.cpp:1367-1369` gates the BC re-emit on
`edgeBCs().size() == triangles.size() * 3`. With `deferHeavyGeometry=true`, `m_bc` is
sized only in the Phase-B completion handler (`swmm2dmeshlayer.cpp:1238-1239`).
Saving before Phase B finishes fails the check and **silently skips**
`patchBCSections` — after the snapshot restore at `:1345-1347` has already reverted
the file. The race window widens with mesh size.

`[H]` Peak allocation for 1.5 M cells is roughly: `.2dm` QString (~300 MB) +
parse `QStringList` (~300 MB) + `edgeBCs` 4.5 M × 64 B (~290 MB, allocated three
times across reader/layer/worker) + heavy geom (tri bboxes 48 MB, edge bboxes 72 MB,
spatial grids). Plausibly 2-3 GB. On a 64-bit build with a healthy pagefile this
survives; on a constrained commit limit it throws `std::bad_alloc`, which
`swmmvis.cpp:4886` **does** surface as a log warning. Whether this fires is the one
thing static reading cannot settle — see §6.

---

## 3. Why the failure is save-triggered

```
generate mesh in session        → mesh in memory; .2dm never re-read      → fast
save                            → patchAttributeSections writes
                                  [2D_VERTEX_NODE_MAP] in TAG form         → F1 armed
reopen                          → reader takes the linear-scan fallback
                                  for every coupled vertex                 → F1 fires
```

`formatVertexNodeMap` returns `{}` early when `coupling.vertexToNode.isEmpty()`
(`:103`). Before the first save the section may be absent entirely; after
`patchAttributeSections` rebuilds it from `mesh.vertices[i].coupledNode` (`:832-834`)
it is always present and always tag-form. This matches the report exactly, and it
explains why the on-disk artifacts look correct — **they are correct**.

---

## 4. Success criteria

1. A 1.5 M-cell external mesh with ≥50 k coupled vertices reopens in **≤5 s** for the
   parse stage (`readMs` in the `openswmm.load.mesh` line at `swmmvis.cpp:4979`).
2. Round-trip fidelity: save → reopen → save produces a **byte-identical** second
   `.2dm` (currently impossible because of F3's shuffled QHash order).
3. `.oswp` mesh display state (active / showEdges / showMeshNodes / hillshade /
   contours) survives save → reopen.
4. BC and conveyance edits survive a save issued while Phase B is still running.
5. Existing offscreen `ctest` suite green before and after each phase.

---

## 5. Proposed fix program

Each phase is its own commit, gated on a number. Phases 1-2 are the fix for the
reported bug; 3-4 are the silent-drop bugs found alongside; 5 is conditional on §6.

| Phase | Content | Gate |
|---|---|---|
| **1** | **Kill the quadratic.** In `parseSection`, build a `QHash<QString,int>` tag→index once per mesh, lazily on first tag-form row, for both `[2D_VERTEX_NODE_MAP]` (`inpmeshreader.cpp:167-177`) and `[2D_TRIANGLE_NODE_MAP]` (`:213-215`). Replaces both linear scans with a hash probe. No format change; old files still load. | parse stage of the repro `.2dm` **≤5 s**; tag→index resolution count unchanged (same mesh after load) |
| **2** | **Determinism + redundant passes.** (a) Emit `[2D_VERTEX_NODE_MAP]` rows in ascending vertex index — sort the `QHash` keys in `formatVertexNodeMap`, and fix the false comment at `:109`. (b) Fold `scanUnitsHeader` into `parseSectionsFromText`'s single pass, deleting the throwaway split at `:339`. (c) Replace `tokenize`'s `QRegularExpression` with a manual whitespace splitter. | two consecutive saves produce byte-identical `.2dm`; parse-stage wall clock down a further ≥30 % |
| **3** | **F9 — restore mesh display state.** Mirror the existing 2D-results pattern: stash the `kMeshLayers` entries (paths resolved absolute) on the project window in `applySession`, and consume them in `attachMesh2DLayersAsync`'s completion handler after `addLayer` (`swmmvis.cpp:4929`). Correct the stale comment at `projectserializer.cpp:888`. | new test: save with non-default mesh style → reopen → all five fields match |
| **4** | **F10 — close the Phase-B save race.** Have `saveModelAs` either await `sceneGeometryReady` or read BCs from the layer's file-loaded vector rather than the Phase-B-sized one. Surface `patchBCSections`/`patchAttributeSections`/`writeMeshFileRef` failures to the Message Log, matching the inline path at `swmmvisprojectwindow.cpp:1397-1408` instead of the bare `qWarning` at `:1360-1381`. | new test: save during Phase B → reopen → BC edits present |
| **5** | **Conditional (F6/F11).** Only if §6 shows a memory ceiling: replace `applyConveyanceRows`' `QHash<QPair,QVector<int>>` with a sorted flat array + binary search, and stream the `.2dm` parse instead of slurping. | peak RSS on the repro mesh under an agreed budget |

**Explicitly out of scope** (noted, not touched, per CLAUDE.md §3): the narrowing
`int nslots = triangles.size() * 3` casts at `inpmeshreader.cpp:591`/`:615`, the
`QIODevice::Text` LF↔CRLF asymmetry in `atomicWrite`, and the case-sensitive
`startsWith` relative-path test at `inpmeshwriter.cpp:487`/`:532` (wrong on Windows
when drive-letter case differs, but produces a valid absolute path, so benign today).

---

## 6. Open question that gates Phase 5

Static reading cannot distinguish **hang** (F1, quadratic, mesh eventually appears)
from **hard failure** (F11, `bad_alloc` in the layer build). The discriminator is
cheap: how long the reopen was left running before being judged failed, and whether
the Message Log ends at `"2D mesh parsed: N vertices, M triangles"` (⇒ F1) or at
`"Loading the 2D mesh failed: …"` (⇒ F11). If F1 alone, Phase 5 is unnecessary.

Also worth confirming: the Windows build is 64-bit. A 32-bit build fails
deterministically at this mesh size regardless of Phases 1-4.

---

## 7. Verification recipe

Per CLAUDE.md §4.1, all test artifacts land under a reviewable path — **no temp
directories**.

```
QT_LOGGING_RULES="openswmm.load.mesh=true" \
  build/SWMMVis <model>.inp 2>&1 | tee tests/output/mesh_reload_2026-08-18/<phase>.log
```

Fixtures (to be committed under `tests/data/mesh_reload_2026-08-18/`):

- **small** — existing 2D example, regression guard for format round-trip.
- **synthetic-1.5M** — generated `.2dm`, 1.5 M triangles / ~760 k vertices, with a
  `[2D_VERTEX_NODE_MAP]` of 50 k tag-form rows. Reproduces F1 without needing the
  user's proprietary model.
- **user repro** — if the reporter can share it.

Per phase: capture `readMs` / `sceneBuild` from the `mesh load timing (ms)` line
(`swmmvis.cpp:4979`), peak RSS, and a `diff` of two consecutive saved `.2dm` files.
Correctness gate: offscreen `ctest` green, plus the two new tests from Phases 3-4.
