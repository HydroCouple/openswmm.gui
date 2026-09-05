@page manual_performance A4 — Performance and Redraw Policy

## What you'll do

Understand why most gestures are cheap, what happens when you open a very large
model, which settings actually change run time, and how to diagnose a slowdown
with logging categories and environment variables.

## Where to find it

- **Preferences → Rendering** — GPU rendering and label level-of-detail
- **Preferences → Simulation** and **Simulation Defaults** — progress tick,
  default thread count
- **Preferences → 2D Defaults** — mesh generation tolerances
- **Simulation Options → System / Performance** — the per-model thread count
- Environment variables, for diagnosis (see below)

## Step-by-step

### How the canvas redraws

The canvas separates redraws into four independent **dirty channels**. Each
channel tracks its own pending state and has its own debounce timer, so a cheap
update never pays the cost of an expensive one.

| Channel | Cost | Debounce | Drives |
|---|---:|---:|---|
| **Raster** | high — a background render job recomposes basemaps and GIS rasters | 150 ms | basemap changes, raster layer add / remove / visibility / opacity, CRS change |
| **Scene** | medium — refresh the vector scene items for visible layers | 50 ms | network edits, theme changes, vector layer visibility |
| **Overlay** | low — repaint widget-coordinate decorations only | immediate | selection highlights, rubber band, measure line, cursor readout |
| **Extent** | very low — recompute the scale and emit signals | immediate | pan and zoom commit |

A single gesture usually touches one channel. Clicking to select an object
dirties only **Overlay** — the basemap is untouched and the vector items do not
repopulate.

#### Coalescing and debouncing

Multiple invalidations of the same channel inside its window fire **once**.
Toggling a vector layer five times quickly costs one scene refresh, not five.
The debounce is *deferred*, not delayed: the timer starts on the first
invalidation and fires at the end of the window; later invalidations in the
window only re-set the pending mask.

A scene refresh that arrives **during** a pan or zoom gesture is re-armed rather
than dropped, so an edit made mid-gesture still lands when the gesture ends.

#### Suspending and resuming

Batch operations — loading a project with many layers, applying a multi-step
undo command — bracket their work between a suspend and a resume. Every
invalidation inside the bracket accumulates and fires **once** on the matching
resume. The pair is reference-counted, so nesting is safe.

### Buffers and caches

| Cache | What it holds |
|---|---|
| Map buffer | the composed raster image plus its extent; blitted with a transform during pan and zoom so the preview is instant |
| Scene buffer | a rasterised copy of the vector scene, used during gestures and reused when idle if the extent, size and device pixel ratio all still match |
| QSG frame cache | the grabbed GPU framebuffer for the SWMM, 2D-results and terrain-mesh renderers |
| Drag render timer | while panning, re-warms tile caches at the drag extent roughly every 200 ms |
| XYZ tile cache | a 400-tile LRU per tile layer |

The GPU frame is re-grabbed only when the frame is explicitly dirty, the top
layer of a kind changed, the widget was resized, the renderers' content
revision changed, or the cache is empty. **Extent drift alone does not force a
re-grab** — during pan and zoom the cached frame is blitted into a transformed
destination rectangle. And when no layer kind is GPU-owned, the whole GPU render
and framebuffer readback is skipped, which is the single most expensive thing in
a paint on a large model.

### GPU rendering

Three scene-graph renderers are stacked in an offscreen quick widget, bottom to
top: the **2D terrain mesh**, the **2D results**, then the **SWMM network**. On
macOS the app forces the OpenGL RHI backend and the threaded render loop unless
those are already set in the environment; Windows and Linux keep Qt's native
backend (Direct3D 11 or Vulkan). Multisampling is requested on the default
surface format.

Two preferences control this, both in **Preferences → Rendering → GPU
Rendering**:

| Control | Effect |
|---|---|
| **Use GPU rendering for SWMM layers (recommended)** | the network renderer |
| **Use GPU rendering for 2D terrain mesh layers (recommended)** | the terrain mesh renderer; its tooltip names the `OPENSWMM_QSG_MESH=0` kill switch |

#### Level of detail

The 2D renderers apply a **Far / Mid / Near** bucket policy with hysteresis on
both the bucket and a quantised zoom octave, so a small zoom nudge does not
churn the geometry. The far bucket drops cells below about 8 px² (4 px² for the
terrain mesh). **Selection overlays are always drawn**, whatever the bucket.

Contour recomputation switches from synchronous to **asynchronous** above
50 000 cells, so contours trail the frame instead of blocking it.

### Label rendering

**Preferences → Rendering → Label Rendering → Label zoom-out threshold (m11)**
sets the zoom scale below which labels stop drawing. On a model with a thousand
junctions, raising this is the single cheapest win before you animate anything.

### Large models: what to expect on open

Opening a project reads the `.inp` up to four times (engine parse, map-units
scan, mesh scan, 2D-output scan), applies the `.oswp` sidecar, then loads the
mesh asynchronously.

Several open-path costs have been measured and fixed. On a large model, applying
the sidecar dropped from about 22.3 s to about 250 ms once the name lookups
became hash probes; CRS handling was deduplicated from five or six scene
rebuilds per open down to two; a quadratic combo fill in the mesh toolbar that
grew to 10.6 GB and pinned a core across 21 tab switches now costs 2.7 GB and
no measurable CPU; the hover path no longer allocates on every mouse move; and
the link spatial grid is outlier-proof, so a model whose links all landed in one
cell now indexes into a 144 × 123 grid at roughly 16 links per cell.

**What is still slow**, and honestly so: roughly 100 ms of deliberate zoom
retry timers before a model first appears, and the active-window change handler
running three times per open with dozens of whole-widget-tree searches each
time. Per-window GPU context sharing, an SRS memoisation cache and compressed
cell storage are all deferred work, not features.

### Large 2D meshes

A mesh with hundreds of thousands of triangles loads in two visible stages.
The **Message Logs** dock narrates them:

```
Scanning …inp for a 2D mesh …
2D mesh parsed: N vertices, M triangles (t s) — building scene geometry,
  spatial index and LOD pyramid …
2D mesh visible (coarse) — finishing wireframe and spatial index in the
  background …
2D mesh fully ready: wireframe, spatial index and editing structures built (t s).
```

The mesh is usable at the coarse stage. Two parse-side costs have been removed:
the coupled-vertex tag lookup is now a hash rather than a linear scan over every
vertex (and an index-form map pays nothing at all), and the tokeniser no longer
uses a regular expression — it was being called millions of times on a
million-triangle mesh.

Mesh display state — active flag, edge and node visibility, hillshade and
contours — is restored from the `.oswp` after the asynchronous load joins the
canvas.

Two limits to plan around:

- **A saved `.2dm` is not byte-stable.** The vertex-node map is written in hash
  order, so two consecutive saves differ. Expect version-control noise.
- Every vertex-Z edit forces a rebuild of the mesh position buffers. Bulk
  elevation editing on a very large mesh is therefore not cheap.

### Results caching

There is no central frame cache; caching lives in the results layers.

- The 1D results layer keeps per-step result buffers keyed by step, a per-kind
  and per-output-code min/max cache, a categorised-renderer string cache, a
  per-feature override cache and a per-category scene-item cache. Uncached
  whole-file summary statistics cost four passes over the file, so they are
  computed once.
- Opening an `.out` prefetches period 0 and logs a timing breakdown
  (`output_open`, `header`, `id_maps`, `prefetch_step0`, `total`) under the
  `openswmm.load.results` category.
- On the live-tail path, appending a period invalidates the whole-file
  aggregates but keeps the per-step buffers, which stay valid because they are
  keyed by step.
- 2D results are read **per frame, on demand** from the `.h5`. There is no
  prefetch, so animation speed on a large mesh is bound by HDF5 read throughput
  and by the contour and LOD work above.

### Threads

`[OPTIONS] THREADS` controls engine parallelism. The default is **1**.

| Value | Meaning |
|---|---|
| `0` | auto — the OpenMP maximum, lowered by `OMP_NUM_THREADS`, `OMP_THREAD_LIMIT` and CPU affinity, then by model-size gates, and on Apple Silicon by a dynamic-wave performance-core clamp |
| `1` | single-threaded; no OpenMP overhead |
| `N` | exactly N; above the machine's logical processors this is honoured with an oversubscription warning, and the active spin-wait policy is disabled |

Model-size gates apply to explicit values too, with a warning: the dynamic-wave
solver wants at least **100 conduits per thread**, and the 2D marcher at least
**4 triangles per thread**. Asking for more threads than the model can use just
produces warnings and no speed-up.

**Simulation Options → System / Performance**:

| Control | Notes |
|---|---|
| **Worker threads:** | 0–256, special value **auto**; the suffix shows `/ N logical`, or `/ N logical — oversubscribed` |
| *(effective label)* | computed by the engine, not by the GUI |
| *(limits summary)* | logical processors; performance cores, noting that *efficiency cores slow the solvers and are avoided in auto mode*; *Engine built without OpenMP — every run is serial.*; the OpenMP process limit when it is below the logical count; and *2D Kokkos backend already running with N threads (fixed until restart)* |
| **Apply fast preset** | sets `THREADS` to the fast-preset value and `MINIMUM_STEP` to 1.0 s; its tooltip claims roughly 2.6× on the Bellinge benchmark with mass balance as good or better, and warns that raising `MINIMUM_STEP` to 2.0 s gives about 4× at the cost of continuity |

The same knob has a default on **Preferences → Simulation Defaults → Worker
threads (THREADS)**.

Three environment variables the engine inherits are reported at start-up in the
Message Logs as warnings — `OMP_NUM_THREADS`, `OMP_THREAD_LIMIT`,
`OPENSWMM_2D_THREADS` and `SWMM_DW_THREADS`, each with its effect and the
machine's logical-processor count. SWMMVis never unsets them; it just tells you
they are there, which is usually enough to explain a run that will not go
parallel.

For choosing a compute backend (CPU, OpenMP, CUDA, HIP, SYCL) see
\ref manual_plugins.

### GUI worker threads

Loading and analysis run off the main thread on the global thread pool: the
layer composite render job, opening an `.out`, background attribute-range and
statistics sweeps, the heavy mesh geometry phase, raster tiling (on its own
dedicated pool), vector loading, XYZ reprojection (also on its own pool), the
asynchronous mesh attach, the whole mesh-generation pipeline, cell-data
assignment, profile computation, asynchronous contours, and the simulation run
itself.

**Preferences → Simulation → Progress-tick interval** (50–10000 ms, default
1000 ms) sets how often a running simulation reports progress. Lowering it makes
the status panel more responsive at a small cost.

### Environment variables

Set these before launching. All are diagnostic; none is needed for normal use.

#### Rendering and diagnostics

| Variable | Values | Effect |
|---|---|---|
| `SWMMVIS_LOG_REDRAW` | non-zero = on | logs every invalidation, fire, suspend and resume |
| `OPENSWMM_2D_RENDER_DEBUG` | set = on | prints the per-paint re-grab decision and warns on non-finite cell depths |
| `OPENSWMM_RENDER_PERF` | `1` | per-frame repaint and grab timing |
| `SWMMVIS_RENDER_PERF` | set = on | network renderer sampler — logs average rebuild and pan times every 60 frames |
| `OPENSWMM_QSG_MESH` | `0` = off | kill switch for the GPU terrain-mesh renderer; forces the painter fallback |
| `OPENSWMM_QSG_INDEXED_FILL` | `0` = opt out | falls back to the expanded per-corner smooth fill |
| `OPENSWMM_QSG_ASYNC_CONTOURS` | `0` sync, ≥ 1 async, unset = auto | overrides the 50 000-cell async contour threshold |
| `OPENSWMM_QSG_SHADER_FILL` | `1` = on | opt-in GPU scalar-fill shader path |
| `QSG_RHI_BACKEND`, `QSG_RENDER_LOOP` | Qt values | honoured if already set; forced to `opengl` / `threaded` on macOS otherwise |

#### Logging and startup

| Variable | Values | Effect |
|---|---|---|
| `SWMM_LOG_FILE` | a path, or `1`, or `auto` | tees all Qt log output to a file; `1`/`auto` writes `<app data>/logs/swmmvis-<timestamp>.log` and prints the path to stderr |
| `QT_LOGGING_RULES` | Qt rule syntax | enables the categories below |
| `OPENSWMM_DIALOG_STACKING` | `qt` or `native` | overrides dialog stacking; beats the `Window/DialogStacking` setting |
| `SWMMVIS_OPEN_ON_STARTUP` | path to an `.inp` | opens that model at start-up (testing hook) |
| `SWMMVIS_STARTUP_SNAPSHOT` | output PNG path | zooms to extent after mesh load and grabs the canvas |
| `SWMMVIS_STARTUP_SNAPSHOT_ZOOM` | a fraction in (0, 1) | zoom about the mesh centre before the grab |
| `SWMMVIS_SNAPSHOT_SIMOPTS` | output PNG path | grabs the Simulation Options dialog off-screen |
| `SWMMVIS_SNAPSHOT_MESHSTYLE` | output PNG path | grabs the mesh style dialog |
| `PROJ_DATA`, `GDAL_DATA` | directory paths | read at start-up, and set automatically when bundled `proj/` and `gdal/` directories are found beside the executable |

#### Engine-side

`OPENSWMM_PERF` (timers), `OPENSWMM_2D_THREADS`, `SWMM_DW_THREADS`,
`OPENSWMM_2D_RAINFALL_MODE`, `OPENSWMM_2D_FLUX_DH_EPS`,
`OPENSWMM_2D_HEAD_RAMP`, `OPENSWMM_2D_SYNC_SPAN`,
`OPENSWMM_2D_MARCHER_TELEMETRY`, `OPENSWMM_2D_MARCHER_CHECK`,
`OPENSWMM_FV_BACKEND`, `OPENSWMM_2D_BACKEND`,
`OPENSWMM_FV_MIN_PARALLEL_CELLS`, `OPENSWMM_2D_MIN_PARALLEL_CELLS`,
`OPENSWMM_2D_MIN_PARALLEL_CELLS_DEVICE`, `OPENSWMM_GPU_PLUGIN_PATH`, and a
family of `SWMM_TRACE_*` and `OPENSWMM_*_TRACE` hooks for solver debugging.

### Reading `SWMMVIS_LOG_REDRAW`

```sh
SWMMVIS_LOG_REDRAW=1 ./SWMMVis
```

Every invalidation and every channel firing goes to stderr:

```
[redraw:invalidate] Scene  reason=layer-properties-apply
[redraw:fire] Scene  reason=layer-properties-apply
[redraw:invalidate] Overlay  reason=map-tool-select
[redraw:fire] Overlay  reason=map-tool-select
[redraw:invalidate] Raster|Scene|Extent  reason=crs-reproject
[redraw:fire] Extent  reason=crs-reproject
[redraw:fire] Scene  reason=crs-reproject
[redraw:fire] Raster  reason=crs-reproject
[redraw:suspend] depth=1
[redraw:resume] depth=0 pending=Raster|Scene
```

What to look for:

- **Mismatched cause and channel** — a `selection-changed` reason dirtying
  **Raster** is a regression; selection should be Overlay-only.
- **A burst of identical invalidations** that the debounce coalesced into one
  fire (good), or did not (a loop is hitting the immediate Overlay or Extent
  path without a suspend bracket).
- **Raster firings during a pan** — a pan should commit Extent on release only,
  with intermediate moves previewing from the stale buffer.

The lookup is resolved once per process, so the unset case costs nothing.

\figtodo{a04_redraw_log.png, Terminal output from SWMMVIS_LOG_REDRAW during a pan and a selection}

### Logging categories

Enable with `QT_LOGGING_RULES`, for example
`QT_LOGGING_RULES="openswmm.load.*=true"`.

| Category | Covers |
|---|---|
| `openswmm.load.project` | project open |
| `openswmm.load.mesh` | mesh read and scene build timings |
| `openswmm.load.gui` | GUI construction |
| `openswmm.load.window` | project window setup |
| `openswmm.load.model` | model layer |
| `openswmm.load.results` | `.out` open breakdown |
| `openswmm.load.raster` / `openswmm.load.vector` | GIS layers |
| `openswmm.render.perf` | frame timing |
| `openswmm.mesh.perf` | mesh generation stages and the boundary cache |
| `openswmm.save.perf` | mesh–engine sync on save |
| `openswmm.selection.perf` | selection operations |
| `openswmm.bulkdelete` | bulk delete |
| `openswmm.attr-table` | the attribute table |
| `openswmm.ts-load.*` | time-series model, dialog, chart and provider |
| `openswmm.ui.actionregistry` | action registration and shortcuts |
| `openswmm.examples` | example seeding |

### Mesh generation performance

**Preferences → 2D Defaults** holds the generation tolerances that decide how
big a mesh you end up with: **Simplify tolerance**, **Snap tolerance**,
**Minimum triangle angle**, **Maximum triangle area (m²)** (special value
*unconstrained*), **Maximum Steiner points** (special value *unlimited*),
**IDW power** and **Node flatten radius**.

The two that most affect triangle count are maximum triangle area and minimum
cell size. Halving the maximum area roughly quadruples the triangle count, and
everything downstream — parse time, scene build, animation frame cost, run time
— scales with it. A boundary polygon that has been cached from a previous run is
reused, and the log says so.

## Tips and gotchas

- The most effective large-model wins, in order: raise the label zoom-out
  threshold; keep GPU rendering on; turn the mesh wireframe off while
  animating; hide layers you are not looking at.
- Turning GPU rendering **off** is a diagnostic step, not a performance
  improvement.
- Preferences has a **Reset to defaults** button, but its values are a
  hand-maintained mirror of the compiled-in defaults rather than the same
  constants — a rarely-exercised path.
- There is no preference for redraw debounce intervals, frame cache size, tile
  cache size or GUI thread count. Those are compiled constants: 150 ms, 50 ms,
  400 tiles and the global thread pool.
- Reported thread counts come from the engine, not from the GUI's own
  arithmetic. If the effective count is lower than you asked for, the summary
  line beneath the spin box says why.
- More threads is not always faster. Below 100 conduits per thread the
  dynamic-wave solver clamps and warns, and on Apple Silicon efficiency cores
  are deliberately avoided in auto mode.

## Related

- \ref manual_preferences — the full Preferences dialog
- \ref manual_simulation_options — System / Performance and 2D Surface Routing
- \ref manual_layers — layer visibility and scale limits
- \ref manual_styling — label configuration
- \ref manual_2d_mesh — mesh generation options and their cost
- \ref manual_plugins — compute backends
- \ref manual_troubleshooting — what to do when something is slow or stale
- \ref tutorial_bellinge — these settings applied to a real large model
