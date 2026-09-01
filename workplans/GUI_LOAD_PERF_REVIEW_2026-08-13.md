# GUI Model-Load & Interaction Performance Review

**Date:** 2026-08-13
**Repo:** `openswmm.gui`, branch `swmm6_gui`
**Build measured:** Release (`-O2`, `build/`), engine post-load-optimization (`4f8cc560..6c78d863`)
**Evidence log:** `tests/output/gui_perf_2026-08-13/after3.log` (one session: two model
opens + one simulation run + hover/pan/select interaction)
**Status:** review complete; fix program approved (Phases 1-5 below), nothing implemented yet.

---

## 1. Why this review exists

User report: *"GUI model loading is slow — even for small models."* The engine-side
load-optimization program had just delivered 2.56× on open+init, yet the app still felt
slow. The instrumented session settles the question of where the time goes:

| Model | nodes / links | `loadModel` total | engine_open | sidecar apply |
|---|---|---:|---:|---:|
| West%20Whiteland.inp | 42,809 / 121,902 | 676 ms | 488 ms | **22,267 ms** |
| East Boston submodel | 179 / 183 | **14 ms** | 13 ms | 37 ms |

The measured core load path (engine open → SoA copy → CRS → geometry cache) is
**innocent**. Everything the user feels is in the orchestration around it: the sidecar
apply, redundant scene rebuilds, per-mouse-move full scans, and fixed UI overheads.

The engine run path is also clean: `[PERF-LOAD] open=0.0065 init=0.0163` on the small
model. The 36.6 s that followed the Run click was 1D simulation compute.

---

## 2. Root causes, ranked (all confirmed with file:line evidence)

### A. The 22-second sidecar apply — O(N²) name lookups  ★ headline

The `.oswp` sidecar is **13 KB of JSON**. Applying it took 22.3 s. Chain:

```
SWMMVis::finalizeSingleINPOpen                      src/swmmvis.cpp:4587-4599   (UI thread, blocking)
 └─ ProjectSerializer::applyFromFile → applySession  src/project/projectserializer.cpp:1104, :644
     └─ per category: layer->setKindRenderer(...)    projectserializer.cpp:719-728
         └─ rebuildKindFeatureColors(c)              src/layers/swmmmodellayer.cpp:2770
             ├─ classifyGraduatedIfNeeded: per object identifyByName(objectNameAt(c,i))  :3038
             └─ pass 2: per object identifyByName(name)                                  :3127-3133
                 └─ identifyByName = FOUR sequential linear scans                        :3324-3348
                    (nodes → links → catchments → gages; a link pays a full failed
                     node scan first)
```

Cost model: conduits pass ≈ 121,902 × (42,809 + ~60,951) ≈ 1.26×10¹⁰ QString compares,
×2 passes → matches 22.3 s. Small model: ~7.6×10⁴ compares → matches 37 ms.

**The fix already exists in the class:** `m_nameToSoa` —
`QHash<QString, SoaLocation>` (`include/layers/swmmmodellayer.h:2179-2180`) — built in
`rebuildCategoryIndex` (`swmmmodellayer.cpp:2242-2306`) over all four kinds, kept
coherent on add/rename (:7066, :7102, :6980-6991), and already used correctly by
`elementPosition` (:3918). `identifyByName` never consults it. Worse, the caller
computes `soaIndexFor(i)` (:3129) *immediately before* converting index→name→scan→index.

Sibling instances of the same bug:
- `nodeIndex`/`linkIndex` (:3893/:3900) — linear scans; make
  `SWMMResultsLayer::populateScene` O(N²) too (`swmmresultslayer.cpp:2932-2935`).
- Sidecar apply also calls synchronous `openResults()` (`projectserializer.cpp:844`)
  though `openResultsAsync()` exists (`swmmresultslayer.cpp:820`); comment at
  `swmmresultslayer.cpp:2624` even names "West Whiteland's 427 MB .out".

### B. ~6 full scene rebuilds per model open (should be 1)

Each rebuild = O(N) `rebuildSceneCoords` walking all links **twice** with a heap
allocation per link, then `LinkSpatialGrid::rebuild`. Observed: `[LinkSpatialGrid::rebuild]`
×13 for two opens; churn pattern identical on the 179-node model → structural.

No CRS setter compares values:
- `OpenSWMMVisLayer::setSRS` always emits (`src/layers/openswmmvislayer.cpp:203-211`).
- `MapCanvas::applyCRSInternal` guards on **pointer** identity; every caller passes a
  fresh `new SpatialReferenceSystem` so it never fires (`src/map/mapcanvas.cpp:293-315`).
- One `srsChanged` emission feeds **two** listeners that each call `onCanvasCRSChanged`
  on the same layer: `swmmvisprojectwindow.cpp:245-267` and the per-layer lambda at
  `mapcanvas.cpp:531-546` (which also refreshScene + zoomToFullExtent + invalidate).
- `finishModelLoad` re-sets the same CRS (`swmmvisprojectwindow.cpp:800-804`).
- Sidecar apply sets it twice more (`projectserializer.cpp:669`, `:1172`).

Rebuild inventory for one open: worker `buildGeometryCache` (#1, legitimate) +
adoptOpenEngine setSRS → PW lambda (#2) + MapCanvas lambda (#3) + finishModelLoad
setCanvasSRS (#4) + sidecar layer SRS (#5) + sidecar canvas SRS (#6).

Also: `rebuildSceneCoords` does an offset pre-pass then a write pass — two full link
walks (:6685-6698), each materializing polylines via `cachedLinkPolyline`.

### C. Hover = full linear scan of all links, per mouse move

`identifyAt` tier 2 (`swmmmodellayer.cpp:3547-3763`):
- `for (li = 0; li < m_links.size(); ++li)` at :3687 — ignores `m_linkGrid`, ignores
  `m_linkBboxes`, no bbox reject.
- Per link calls `cachedLinkPolyline(li)` — **not a cache**: heap-allocates a fresh
  `QVector<QPointF>` every call (:3956-3981).
- Builds + destroys an OGR inverse transform per call (:3569-3577) despite the cached
  `m_inverseTransform` member used by `transformCanvasToLayer` (:7688).
- Tier 1 nodes: KD-tree hit path is fine, but a radius miss falls back to a full linear
  scan of nodes+gages (:3649-3669) — i.e. every hover over empty space.
- Tier 3 subcatchments: full scan point-in-polygon, no bbox pre-reject (:3739).
- Hover trigger: `maptoolselect.cpp:527` (mouseMoveEvent → pickAt → identifyAt); also
  snapengine.cpp:53 for every add-tool mouse move.
- `pickAt` then re-derives indices via `linkIndex(name)`/`nodeIndex(name)` — another
  linear scan per hit (:3900/:3893).

Session evidence: 144 full scans of 121,902 links (consecutive 1-2 unit coordinate
deltas prove hover, not clicks). Paint p50=1 ms but p99=315 ms, max=462 ms (n=376).

### D. Fixed overheads that bury small models (~0.3-0.5 s on a 14 ms load)

| Item | Where | Cost |
|---|---|---|
| 50 ms zoom retry-timer (up to 20×) + 50 ms diagnostic timer that walks `mapScene()->items()` just to log `[postZoom]` | `swmmvisprojectwindow.cpp:836-881` | ≥100 ms guaranteed blank canvas |
| `onActiveSubWindowChanged` runs **3×/open** (addSubWindow, show/setActiveSubWindow, forced `mActiveProjectWindow=nullptr` rebind at `swmmvis.cpp:4700-4701`); each does ~42 whole-tree `findChild<QAction*>` traversals (26 toolActionKeys :5586-5601, 13 applyProjectOpenToActions :837-856, 3 applyEditSessionToActions) | `swmmvis.cpp:5363-5960` | ~126 QObject-tree walks/open, grows with open windows |
| Mesh toolbar rebind materializes the **entire** Timeseries + Curve registries (every point → QDateTime) + full node enum + sort — unconditionally, every open AND every tab switch, gated on nothing | `mesheditingtoolbar.cpp:555-579` ← `swmmvis.cpp:5913`; listers `swmmvis.cpp:982-1006` | the whole `setAllPoints` burst |
| Attribute table `refresh()` ×3/open: #1 against an engine-less layer (pure waste), #2 modelLoaded (legit), #3 results `layerAdded`; each rebuilds combo, 3× layer sweeps, fresh delegate per column (leaked until panel death, `attributetablepanel.cpp:1159`), QSettings ctor + 2 reads (:1098-1112) | `attributetablepanel.cpp:757, 720, 713` | medium × 3 |
| `.inp` read **4× per open**: engine parse; `readMapUnitsFromInp` full-file scan **on the UI thread** inside `adoptOpenEngine` (`swmmmodellayer.cpp:1180`, impl :658-677); mesh scan (`swmmvis.cpp:4960`); 2D-output scan (`parseTwoDOutputFile`) | | blocking IO |
| Sidecar auto-**write** on first open (`AutoCreateOswpOnOpen` default true, full session serialize + disk write, UI thread) + `saveSettings()` (`saveState(2)` + QSettings flush) on every open | `swmmvis.cpp:4614-4632`, `:4579` | blocking disk writes |
| `attachMesh2DLayersAsync` runs for every model: spinner + "Scanning … for a 2D mesh" + worker + 2 extra file reads even when the engine already knows there are no 2D sections | `swmmvis.cpp:4685, 4729-5024` | flicker + wasted thread |
| Recent-files: menu rebuild + ≤20 `QCommandLinkButton` destroy/rebuild on welcome page, `QFileInfo` each | `swmmvis.cpp:6052-6148` | ~20 stats + widget churn |

Also latent: MSAA default-format block runs **after** `QGuiApplication` construction
(`main.cpp:87` vs `:101-111`), contradicting its own comment; the per-widget override in
`mapcanvas.cpp:198-203` is what actually works.

### E. The 22 s ran with no progress indication

`endFileOpen` hides the progress bar at `swmmvis.cpp:4571`; the sidecar apply starts at
:4587. All of its cost is unattributed dead time with a cleared status bar.

### F. Unconditional qDebug in hot paths

| Site | Fires |
|---|---|
| `swmmmodellayer.cpp:3724` `[identifyAt tier2]` (12 stream operands) | per mouse move |
| `swmmmodellayer.cpp:3972` `[cachedLinkPolyline LAST]` | per full link pass AND per identify |
| `swmmmodellayer.cpp:3292` `[setSelectedElements]` | per selection change |
| `swmmmodellayer.cpp:3773` `[populateScene]` — builds 11-arg QString **before** the isVisible early-out at :3782 | per populate |
| `swmmmodellayer.cpp:6727` `[LinkSpatialGrid::rebuild]` | per rebuild |
| `maptoolselect.cpp:1164` `[selectInRect]` | per rubber-band |
| `swmmvisprojectwindow.cpp:824, :873` `[loadModel]`, `[postZoom]` | per open (cheap) |

Properly gated already (leave alone): `swmmlayeritem.cpp:1077` (lcRenderPerf), mapcanvas
redraw logs (`SWMMVIS_LOG_REDRAW`), 2D renderer logs, attr-table (lcAttrTbl).

### G. Spatial-grid fragility and per-paint allocation

- WW-2024's **10 corrupt junction coordinates** (J45194/95, J45202-05, J45231/32,
  J45269/70; |x|,|y| up to 4.1×10⁷ vs a 27,937 × 20,172 real span) stretch the grid
  extent 900× × 3,200× → all 281k links land in ~1 cell → every query degrades to a
  linear scan. Cell sizing: `median diagonal × 16`, cap 1024×1024
  (`swmmmodellayer.cpp:6503-6560`). Inserts already `std::clamp`, so only *sizing* is
  broken by outliers.
- `LinkSpatialGrid::query` (:6565-6595) allocates a `QVector<bool> seen(maxIdx+1)` and a
  growing `out` **per paint**, and traverses cells twice.
- At the 1024×1024 cap, `cells.resize` creates ~1 M `QVector` objects (~24 MB of empty
  containers) per rebuild — ×6 rebuilds per open (see B).
- `SWMMLayerItem::refreshBoundingRect()` (`swmmlayeritem.cpp:216-264`) re-transforms
  every vertex through OGR, duplicating what `rebuildSceneCoords` just produced — runs
  in the item ctor, i.e. once per populateScene (7×/session).
- Small-model degeneracy: 179-node model gets a 1×2 grid → full scan + dedupe overhead.

### Registry read-back: NOT a problem (verified)

All 13 `ensure*Registry()` accessors are lazy and none run during open. The only
registry-ish work at open is `ensureUserFlagsModel` (cheap). The `setAllPoints` burst
comes from the Mesh toolbar path (D), not from model load itself.

### Session restore: does not exist (correction)

No QSettings-driven reopen in the codebase. The only auto-open path is the
`SWMMVIS_OPEN_ON_STARTUP` env hook (`swmmvis.cpp:322-326`) / macOS AppKit window
restoration. Earlier suspicion of an in-app session restore was wrong.

### Run path (context)

Second engine open in `SimulationRunner` confirmed (`simulationrunner.cpp:272-287`,
off-thread, strict) — every parse win lands twice; sharing the layer's engine is a
deferred item. Pre-run UI work includes `parseTwoDOutputFile` (full .inp read per Run
click), an MDI×layers collision scan with per-layer stats, and the dirty-flag is
fragile: any post-open registry mutation → `dataObjectsChanged` → `setHasChanges(true)`
→ next Run rewrites the .inp.

---

## 3. Approved fix program

Phases, each its own commit(s), each gated on a measured number from existing log lines.

| Phase | Content | Gate |
|---|---|---|
| **1** | `identifyByName` → `m_nameToSoa`; index-based attribute fetch in `rebuildKindFeatureColors`/`classifyGraduatedIfNeeded`; `nodeIndex`/`linkIndex` → hash; sidecar `openResults` → `openResultsAsync` | WW sidecar apply 22,267 → **≤300 ms**; small ≤10 ms |
| **2** | CRS value-equality guards (`setSRS`, `applyCRSInternal`); single `srsChanged`→`onCanvasCRSChanged` owner; drop redundant `setCanvasSRS` in finishModelLoad; `rebuildTransform` WKT-pair early-out; fuse the two link passes in `rebuildSceneCoords` | `[LinkSpatialGrid::rebuild]`/open ~6 → **1-2** |
| **3** | identifyAt via grid query + bbox reject + `m_linkSceneFlat` slices; reuse `m_inverseTransform`; tier-1 miss = miss; tier-3 bbox pre-reject; gate hot-path qDebug (new `openswmm.pick` category); query scratch-buffer reuse; rect-select via grid/KD | no hover lag on WW; paint p99 ≪ 315 ms |
| **4** | event-driven first zoom (kill 50+50 ms timers); gate mesh-toolbar registry materialization on mesh/visibility; `readMapUnitsFromInp` → worker; progress bar covers sidecar+results; cache tool-action pointers (kill findChild storms + 3rd rebind); attr-table skip engine-less/hidden refresh; defer sidecar auto-write + saveSettings; mesh scan asks engine instead of re-reading .inp | East Boston open **≤250 ms** end-to-end; WW ≤1.5 s |
| **5** | percentile-clipped grid extent + occupancy-target cell size + cols/rows floor (LinkSpatialGrid + MeshSpatialGrid) | WW-2024 grid usable (not 1×1), hover/pan fluid |

Deferred (measure-first, separate program): per-window QQuickWidget/QML/RHI sharing;
SRS memo-cache + CRSManager warmup; CSR cell storage; `refreshBoundingRect` OGR-pass
elimination; SimulationRunner engine sharing.

## 4. Verification recipe

```
OPENSWMM_PERF=1 QT_LOGGING_RULES="openswmm.load.*=true" \
  build/SWMMVis.app/Contents/MacOS/SWMMVis 2>&1 | tee tests/output/gui_perf_2026-08-13/<phase>.log
```

Models: East Boston submodel (small) · West%20Whiteland.inp (42k clean) ·
WW-2024 (103k + 10 corrupt coords) · `grid_500k_geo.inp` (synthetic ceiling).
Correctness: offscreen `ctest` green before and after each phase; new tests for
name↔index coherence, setSRS equality guard (QSignalSpy), grid sizing under outliers.
Memory: `watch_rss.sh` (exact-name pgrep, 10 GB kill) during large-model passes.
Invariants: edit-time dirty tracking (`test_dirtytracking_run.cpp`) must stay green.

## 5. Data quality note

WW-2024 and WW-Infrastructure carry the same 10 corrupt-coordinate junctions
(projection failure on one imported group). Fixing the model data makes those two files
fast immediately; Phase 5 makes the app robust to the next such file.

---

# RESULTS (appended 2026-08-13, after execution)

All measured on the user's own models; suite 184/184 throughout.

| Phase | Commit | Gate | Result |
|---|---|---|---|
| 1 — O(N²) name lookups | `1b13148` | sidecar ≤300 ms | **22,267 → 250 ms** |
| 2 — CRS/scene rebuild dedup | `f1858a7` | rebuilds → 1-2 | **5-6 → 2** per open |
| — a11y measurement | `a523521` | — | located the source; see below |
| ★ quadratic combo fill | `6c8770e` | — | **10.6 GB + 100% CPU → 2.7 GB, 0%** over 21 tab switches |
| 3 — hover path | `6e9c016` | no hover lag | 121,902 allocs/mouse-move → bbox reject; traces gone |
| 5 — outlier-proof grid | `9953791` | WW-2024 usable | **all 281k links in 1 cell → 144×123, ~16/cell** |
| 4 — open-path overheads | — | small model ≤250 ms | **not done** |

## The finding that was not in this review

The 9.6-10.6 GB / 100% CPU burst was **not** any of the causes hypothesised
above. `MeshEditingToolbar::refreshNodeList()` filled a QComboBox with
`addItem()` in a loop — one call per node, 42,809 on West Whiteland. Each
insert emits `rowsInserted`, and Qt's macOS accessibility bridge answers by
rebuilding that combo's ENTIRE element array
(`-[QMacAccessibilityElement updateTableModel]` → `populateTableArray`). That
is O(N²) allocation, and it ran on window activation — every model open and
every tab switch. `addItems()` collapses it to one insertion.

The `QSignalBlocker` already present never helped: it silences the combo, not
its internal model, and it is the model's signal that reaches accessibility.

## Hypotheses this review got wrong

1. **Map scene items flood the accessibility tree** — false. Qt does not
   enumerate `QGraphicsScene` contents as accessible children (5,000 items →
   <100 children). `test_a11y_scene_exposure` pins it.
2. **The Attribute Table is the source** — it does expose 879,879 accessible
   children, but is not what the stack sample caught. A fix deferring
   `refresh()` while hidden was reverted: it must populate synchronously even
   when never shown, because callers read the model programmatically.
3. **`QT_ACCESSIBILITY=0` is a valid control** — false on macOS; that variable
   is honored by the Linux/AT-SPI backend only. The "experiment" tested
   nothing.

What settled it was `sample <pid>` on the live spinning process. The heap
dumps said *what* was allocated and sent the investigation to the wrong widget
twice; only the stack said *who* allocated it.

## Also corrected during execution

- Plan item 1.2 (index-based attribute fetch) dropped: `objectNameAt` honors
  `m_objectOrderOverrides` while the adjacent `soaIndexFor` does not, so it
  would have changed behaviour under a perf banner. Pre-existing bug, left
  documented.
- Plan item 2.3 (delete the duplicate `srsChanged` listener) rejected as
  unsafe: it is the only path when the project-window lambda bails out
  (geographic layer, matching authority). Made `rebuildTransform` idempotent
  instead.
- Percentile clipping rejected for the grid extent — 10 bad rows in 281,902 is
  0.0035%, so even a 0.5% clip discards 1,405 legitimate links. Tukey fences
  (k=10) instead.
- `LinkSpatialGrid::query` had to switch from intersect-and-bail to clamping,
  or links clamped into edge cells by `rebuild` become unhittable.

## Remaining

Phase 4 only: the ~100 ms of deliberate zoom timers before a model appears,
and `onActiveSubWindowChanged` running 3× per open with ~42 whole-tree
`findChild` traversals each. Worth re-measuring first — `6c8770e` removed the
dominant cost from that same path.
