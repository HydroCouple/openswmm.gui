# 2D Results — FlowRender Streamline Visualization Plan

**Status:** ⏳ draft 2026-05-31 — awaiting approval.

Sibling plan to [`RENDERING_2D_RESULTS_STYLING_PLAN.md`](RENDERING_2D_RESULTS_STYLING_PLAN.md) and [`MESH_VISUALIZATION_STRATEGY.md`](MESH_VISUALIZATION_STRATEGY.md). Builds on the velocity reconstruction already shipped in `swmm2dresultslayer.cpp` (the `SWMM2DVelocityArrowsItem` static-glyph pass, `:470–720`) and the offscreen GL path established by [`RENDERING_5M_PLAN.md`](RENDERING_5M_PLAN.md) (`swmmlayerglrenderer.{h,cpp}`).

The goal is a new **animated-streamline** visualization layer for 2D overland-flow results, reproducing the visual language of Esri's [`FlowRenderer`](https://developers.arcgis.com/javascript/latest/references/core/renderers/FlowRenderer/) (animated trails travelling along precomputed streamlines, colored by an attribute — velocity magnitude by default) **natively in the Qt6/OpenGL canvas**. It does not depend on or export to ArcGIS; the FlowRenderer reference defines the *target behaviour and knob surface only*.

---

## 0. Decisions locked (2026-05-31)

| # | Decision | Choice |
|---|---|---|
| D1 | Render target | **Native Qt/OpenGL** in `openswmm.gui` — no ArcGIS runtime dependency. |
| D2 | Velocity field storage | **Both** — native cell-centered U/V on the triangular mesh *and* a derived regular UV grid, selectable at render time. |
| D3 | HDF5 location | **Separate derived sidecar `.h5`** produced by a GUI-side processing step. The engine's CF-1.11/UGRID output (`Default2DOutputPlugin`) is read-only input; the GUI owns the new schema. |
| D4 | Scope | **End-to-end** — data model + processing + renderer + MVC/UI integration, with verification milestones. |

### Why a regular grid at all (the FlowRenderer constraint, translated)

Esri's FlowRenderer is only defined over a *raster* whose source type is `Vector-UV` / `Vector-MagDir` — i.e. a regular grid of (u, v) samples. The animated streamlines are integral curves of that gridded field. Our results live on an **unstructured triangular mesh**, so to honour the FlowRenderer model faithfully we resample to a regular UV grid for the streamline *integration* pass. We keep the **native** cell-centered field as well (D2) because (a) it is the ground-truth conveyance-consistent reconstruction, (b) it lets us color/seed streamlines from native cell attributes without grid interpolation error, and (c) the existing arrow overlay already consumes it. The grid is a derived acceleration/compatibility structure, not a replacement.

---

## 1. Current state

`SWMM2DResultsLayer` already:

- Reads the engine's CF-1.11 / UGRID-1.0 HDF5 via `Mesh2DH5Reader` (`/Mesh2_node_x|y|z`, `/Mesh2_face_nodes`, `/time`, `/Mesh2_face_depth`, `/Mesh2_edge_flux`, `/Mesh2_edge_length|nx|ny`).
- Reconstructs a **cell-centered velocity** `(vx, vy, vmag)` per wet triangle and caches it on the scene-triangle struct (`m_sceneTris`, consumed at `swmm2dresultslayer.cpp:556–574`).
- Paints **static, log-scaled magnitude arrows** (`SWMM2DVelocityArrowsItem`) and **direction-only flow arrows** through the sublayer/Rule/SymbolLayer MVC stack, with a `VectorGlyph` `SymbolLayerKind` (`symbollayer.h:70`).
- Drives a time slider (`currentTimeStepChanged → restyleScene → repaint`).

What is missing: there is **no streamline geometry**, **no animation of trails along paths**, **no gridded field**, and **no persisted derived flow-field artifact**. The arrow pass is per-frame, instantaneous, and stateless.

---

## 2. Success criteria

The slice is done when, on an existing 2D run:

1. A "Build Flow Field…" action produces a sidecar `*.flowfield.h5` next to the engine results, written to a **user-visible location** (never a temp dir — per repo CLAUDE.md §4 Transparent File IO), validating against the schema in §3.
2. A new **Flow Streamlines** sublayer renders animated trails travelling along precomputed streamlines over the canvas, colored by velocity magnitude by default.
3. The Symbology tab exposes the FlowRenderer-equivalent knob surface (§6.3) and edits propagate live via the existing `rendererReplaced` cascade.
4. Switching the color attribute (magnitude → depth → custom) and toggling native-vs-grid field source re-renders correctly.
5. Scrubbing the time slider re-seeds/re-integrates streamlines for the selected timestep; play advances both simulation time and trail animation.
6. Unit + visual-diff tests pass (§8); a golden `*.flowfield.h5` round-trips through reader/writer with bit-stable geometry.

---

## 3. The HDF5 flow-field data model (`*.flowfield.h5`)

Self-contained, CF-flavoured, **derived** sidecar. Group layout below. All datasets chunked + gzip-6; floats `float32` unless noted. The file embeds *both* the native cell-centered field (`/native`) and the resampled grid (`/grid`), plus an attribute registry (`/attributes`) so color can be driven by velocity magnitude (default), depth, or any future cell scalar without schema changes.

```
/                                   root
  @Conventions      = "CF-1.11"
  @data_model       = "openswmm.flowfield/1.0"
  @source_file      = <path to engine results .h5>
  @source_sha256    = <hash of source for staleness detection>
  @created_utc, @engine_version, @gui_version
  @crs_wkt          = <WKT2 copied from the model CRS>
  @length_units     = "m"   @velocity_units = "m s-1"   @time_units = "seconds since <t0>"

  /time              [nTime]  float64   — mirror of source /time

  /native/                              — D2 ground-truth, on the source mesh
    node_x           [nNode]   float64
    node_y           [nNode]   float64
    node_z           [nNode]   float32
    face_nodes       [nFace,3] int32    — triangle connectivity (mirror)
    face_cx          [nFace]   float64  — cell centroid x (cached)
    face_cy          [nFace]   float64  — cell centroid y
    u                [nTime,nFace] float32  — cell-centered velocity x  (m/s)
    v                [nTime,nFace] float32  — cell-centered velocity y  (m/s)
    magnitude        [nTime,nFace] float32  — |v| (stored, not recomputed)
    depth            [nTime,nFace] float32  — mirror of face_depth (wet mask + default ramp)
    @reconstruction  = "edge-flux RT0 / least-squares"   — provenance of u,v

  /grid/                                — D2 resampled regular field (FlowRenderer-native)
    @nx, @ny                            — column / row counts
    @x0, @y0, @dx, @dy                  — origin + cell size (CRS units)
    @resample_method = "idw|barycentric|nearest"
    @fill_value      = NaN
    x                [nx]      float64  — cell-center coords (convenience)
    y                [ny]      float64
    u                [nTime,ny,nx] float32   — gridded velocity x
    v                [nTime,ny,nx] float32   — gridded velocity y
    magnitude        [nTime,ny,nx] float32
    mask             [nTime,ny,nx] uint8     — 1 = wet/valid, 0 = dry/outside domain

  /attributes/                          — color-driver registry (default = magnitude)
    @default = "magnitude"
    <name>/  per scalar attribute available to the color visual-variable:
       @units, @long_name, @domain ("native"|"grid"|"both")
       @vmin, @vmax  (cached stats for ramp autoscaling)
       — values themselves live under /native/<name> or /grid/<name>
```

**Robustness requirements baked into the schema**

- `@source_sha256` lets the GUI detect a stale flow-field when the engine results change and offer to rebuild.
- `@fill_value = NaN` + `/grid/mask` cleanly separate "dry cell" from "outside domain" so the integrator stops streamlines at the wet boundary.
- Stored (not recomputed) `magnitude` and cached `@vmin/@vmax` keep ramp autoscaling and the legend O(1).
- Both `/native` and `/grid` carry the *same* `/time` axis; no resampling in the time dimension.
- WKT2 CRS string is embedded so the artifact is self-describing and reprojectable.

A new `Mesh2DFlowFieldH5Writer` (mirrors the Qt-light style of `Mesh2DH5Reader`: `std::vector` + `std::array`, `QString` only for paths) writes the file; a `Mesh2DFlowFieldH5Reader` reads it. Both live in `io/` next to `mesh2dh5reader.{h,cpp}`.

---

## 4. Processing pipeline (results → `*.flowfield.h5`)

Implemented as a `QtConcurrent`-friendly worker (`FlowFieldBuilder`, new `io/flowfieldbuilder.{h,cpp}`) so a large multi-timestep build stays off the UI thread, reporting progress to the existing run/progress UI.

```
1. Open engine results via Mesh2DH5Reader.            → verify: datasets present, nTime>0
2. For each timestep t:
   a. Read depth[t], edge_flux[t], edge geometry.
   b. Reconstruct cell-centered (u,v) per wet triangle  → reuse the SAME
      reconstruction already used by SWMM2DResultsLayer (edge-flux → cell
      velocity). Factor it OUT of the layer into a shared free function
      so the builder and the live layer cannot diverge.   ← legacy alignment (CLAUDE.md §4.01)
   c. Compute magnitude; flag wet mask via dryDepth cutoff.
   d. Resample (u,v) onto the regular grid (method per §4.1).
3. Stream-write /native and /grid timestep-by-timestep    → verify: writer flushes,
   (append along the time axis; do not hold all nTime        peak RAM ≈ one slice
   slices in RAM).                                            not whole series
4. Compute & store per-attribute @vmin/@vmax, @default.
5. Finalize root attributes (sha256 of source, CRS, versions).
```

### 4.1 Resampling to the regular grid (D2 grid branch)

Default grid resolution = the median triangle "diameter" of the mesh (so the grid neither under- nor over-samples the source), clamped to a configurable `[min,max]` cell count to bound memory; user-overridable in the build dialog. Default method = **barycentric** interpolation (point-in-triangle, exact at cell centers, continuous), falling back to **inverse-distance weighting** outside triangle coverage but inside the domain hull; cells outside the domain get `fill=NaN, mask=0`. The method choice is persisted in `/grid/@resample_method`.

### 4.2 Where the velocity reconstruction lives

The reconstruction `(edge_flux, edge_normals, edge_lengths) → (u, v)` is **already implemented** inside `SWMM2DResultsLayer`. To satisfy the legacy-alignment objective and prevent drift, this plan **extracts it verbatim** into `mesh/cellvelocity.{h,cpp}` as a pure function and has the layer call the extracted function (surgical change: move, don't rewrite). The builder then calls the identical function. No numerical behaviour change to the existing arrow pass — covered by a characterization test (§8).

---

## 5. Native streamline renderer (FlowRenderer behaviour, GL)

### 5.1 Rendering approach

Follow the offscreen-GL precedent of `SWMMLayerGLRenderer` rather than the QPainter `QGraphicsItem` path: animated streamlines for a dense field are thousands of moving primitives per frame, which is a poor fit for `QPainter`/`QGraphicsScene` but ideal for a VBO + a single instanced/`GL_LINES` draw. A new `FlowStreamlineGLRenderer` (`map/flowstreamlineglrenderer.{h,cpp}`) renders into an offscreen FBO and hands a premultiplied-ARGB32 `QImage` to the MapCanvas compositor — exactly the integration contract `SWMMLayerGLRenderer::render(extent, viewportSize)` already uses, so the canvas paint pipeline is untouched.

### 5.2 Streamline lifecycle (mirrors FlowRenderer semantics)

| FlowRenderer concept | Native implementation |
|---|---|
| Precomputed streamline path | Seed points (Poisson-disk by `density`) → RK4 integrate the (u,v) field for up to `maxPathLength` points; cache polyline paths per timestep. Integrate on `/grid` (default) or `/native` (barycentric walk) per the field-source toggle (D2). |
| Animated trail along the path | A trail of `trailLength` visible points advances along the cached path each frame; phase offset per streamline so they don't pulse in unison. Loops back at `maxPathLength`. |
| `flowSpeed` | Per-frame advance step = base (∝ local magnitude) × `flowSpeed`. 0 ⇒ static. |
| `flowRepresentation` (`flow-from`/`flow-to`) | Sign of the integration direction. Default `flow-to` for hydraulic flow (water moves *toward* the direction vector); expose both. |
| `trailWidth`, `trailCap` ("butt"/"round") | GL line width / cap geometry; round cap only when width > 4px (matches Esri's note). |
| `color` (solid) | Default white-ish solid when no color visual-variable is set. |
| `visualVariables` color stops on `Magnitude` | Color visual-variable bound to a `/attributes` scalar (default `magnitude`); per-vertex color sampled along the trail from the active attribute via the existing `ColorRamp`. Opacity visual-variable → per-vertex alpha. Size visual-variable → `trailWidth`. |
| `density` | Seed count multiplier; >1 increases seeds, capped by viewport + `trailLength` to avoid clutter (same guard Esri documents). |

### 5.3 Time + animation coupling

Two independent clocks, matching FlowRenderer's "relative to simulation time":

- **Simulation time** — the existing time slider selects timestep `t`; changing `t` swaps the cached streamline paths (rebuild paths lazily, cache last N timesteps).
- **Trail animation** — a `QTimer`/`requestAnimationFrame`-style repaint drives the trail phase at `flowSpeed`, independent of whether sim time is playing.

---

## 6. MVC / UI integration

Reuse the existing sublayer + Rule + SymbolLayer + adapter architecture (same pattern as `RENDERING_2D_RESULTS_STYLING_PLAN.md` §3) so the new style is editable through the Symbology tab and synchronizes across any UI that edits it (CLAUDE.md §5.1 MVC).

### 6.1 New sublayer + style

- `FlowStreamlineSublayer` added to `SWMM2DResultsLayer`'s sublayer list (8th sublayer), gated by `isVisible()` like the others.
- `FlowStreamlineStyle` (new, `render/` or alongside the other 2D styles) — a `QObject` with the full `Q_PROPERTY` surface in §6.3, grouped via `Q_CLASSINFO("group:…")`.

### 6.2 New SymbolLayerKind + adapter

- Add `StreamlineFlow` to `SymbolLayerKind` (`symbollayer.h`, 15th kind) with JSON token `"streamlineFlow"` round-tripping through `symbolLayerKindToString`/`…FromString`.
- New `StreamlineFlowSymbolLayerSpec` value type (mirrors `FlowStreamlineStyle`, same idea as `VelocityVectorSymbolLayerSpec`) — the persisted/JSON-serialized knob payload the renderer re-samples each frame.
- New `FlowStreamlineSymbolStyleAdapter` registered in `SymbolStyleAdapter::createFor(rule)` for the new kind, declaring only this style's properties (concrete metaobject ⇒ no incompatible knobs surface).
- Wire the Rule + spec + Rule→style back-prop in `swmm2dresultslayer.cpp buildRuleListLazy` (the same place the other 7 rules are built).

### 6.3 Exposed property surface (FlowRenderer parity)

| Group | Property | Type | Default | Maps to FlowRenderer |
|---|---|---|---|---|
| Field | `fieldSource` | enum {Grid, Native} | Grid | (our D2 extension) |
| Field | `flowRepresentation` | enum {FlowTo, FlowFrom} | FlowTo | `flowRepresentation` |
| Animation | `flowSpeed` | double | 10 | `flowSpeed` |
| Animation | `trailLength` | double | 100 | `trailLength` |
| Animation | `maxPathLength` | int/px | 200 | `maxPathLength` |
| Streamlines | `density` | double | 0.8 | `density` |
| Trail | `trailWidthPx` | double | 1.5 | `trailWidth` |
| Trail | `trailCap` | enum {Butt, Round} | Butt | `trailCap` |
| Color | `color` | QColor | (255,255,255) | `color` |
| Color | `colorAttribute` | enum from `/attributes` | magnitude | `visualVariables[field]` |
| Color | `colorRamp` + stops | ColorRamp | velocity ramp | `ColorVisualVariable.stops` |
| Color | `opacityAttribute` + stops | optional | none | `OpacityVisualVariable` |
| Mask | `dryDepthCutoff` | double | 0.01 | (domain mask) |

Defaults intentionally match Esri's documented defaults so the visual reads as "FlowRenderer" out of the box; the flood-simulation guidance (low magnitudes ⇒ raise `trailLength` ≈ 1500) is surfaced as a one-click "Flood preset".

### 6.4 Build dialog

A small "Build / Rebuild Flow Field" dialog (consistent with the Mesh Generation dialog redesign in `MESH_VISUALIZATION_STRATEGY.md`): source results path (auto-filled), output sidecar path (user-visible, defaults next to the source), grid resolution + resample method, timestep range. Progress + cancel. On completion, auto-adds/refreshes the Flow Streamlines sublayer. If a sidecar exists but its `@source_sha256` is stale, the dialog warns and offers rebuild.

---

## 7. File-touch map

| Area | New | Modified |
|---|---|---|
| Data model | `io/flowfieldbuilder.{h,cpp}`, `io/mesh2dflowfieldh5writer.{h,cpp}`, `io/mesh2dflowfieldh5reader.{h,cpp}` | `CMakeLists.txt` |
| Reconstruction | `mesh/cellvelocity.{h,cpp}` (extracted) | `src/layers/swmm2dresultslayer.cpp` (call extracted fn) |
| Renderer | `map/flowstreamlineglrenderer.{h,cpp}` | MapCanvas compositor hook (mirror `SWMMLayerGLRenderer` wiring) |
| Style/MVC | `render/flowstreamlinestyle.{h,cpp}`, `render/streamlineflowsymbollayer.{h,cpp}` | `include/render/symbollayer.h` (new kind), `render/symbolstyleadapter.cpp` (factory), `src/layers/swmm2dresultslayer.cpp` (sublayer + rule + back-prop) |
| UI | `ui/flowfieldbuilddialog.{h,cpp,ui}` | results layer context menu / build action |
| Tests | `tests/unit/test_flowfield_roundtrip.cpp`, `tests/unit/test_cellvelocity.cpp`, visual-diff fixture | `tests/CMakeLists.txt` |

---

## 8. Verification milestones

Following CLAUDE.md §4 Goal-Driven Execution — each slice has a concrete check, and all test IO is written to a **user-visible** fixtures directory, not temp (§4.01/§4.1).

| Slice | Deliverable | Verify |
|---|---|---|
| FR.1 | Extract `cellvelocity` shared fn | Characterization test: extracted fn reproduces current `m_sceneTris` `(vx,vy,vmag)` bit-for-bit on a golden run. |
| FR.2 | `*.flowfield.h5` schema + writer/reader | Round-trip test: write → read → assert geometry/time/native fields equal; `h5dump` structural check against §3; `@source_sha256` populated. |
| FR.3 | `FlowFieldBuilder` + resampling | Grid `magnitude` matches native at cell centers within tol; `mask` matches wet set; peak RAM ≈ one slice (assert via instrumentation). |
| FR.4 | `FlowStreamlineGLRenderer` (static, no anim) | Render one timestep to FBO; visual-diff vs. golden PNG within threshold; streamlines stop at wet boundary. |
| FR.5 | Trail animation + clocks | Trails advance at `flowSpeed`; `flowSpeed=0` ⇒ static; sim-time scrub swaps paths. (Frame-capture diff at fixed phases.) |
| FR.6 | MVC/Symbology integration | Editing each property in the dialog repaints live via `rendererReplaced`; JSON round-trips the new kind/spec; color attribute switch works. |
| FR.7 | Build dialog + stale detection | Dialog builds to user-visible path; stale `@source_sha256` triggers rebuild prompt. |
| FR.8 | Full-task acceptance | All §2 success criteria demonstrated on a real run; subagent review of the diff for surgical-change compliance. |

---

## 9. Open questions / assumptions to confirm before FR.1

1. **Reconstruction provenance.** I assume the existing edge-flux→cell-velocity reconstruction in `swmm2dresultslayer.cpp` is the canonical one to extract. If a more rigorous scheme (e.g. RT0 vector reconstruction vs. the current least-squares/centroid approach) is desired *in the flow field*, that is a separate numerical change — flag it and it becomes its own slice rather than a silent rewrite (CLAUDE.md §1, §3).
2. **Grid memory ceiling.** Default grid sizing is median-triangle-diameter clamped to a cell-count cap. Confirm the cap (drives default RAM/disk for big domains) — proposed default 2048×2048 max.
3. **Sidecar lifecycle.** Assume the sidecar is a cache the user can delete and rebuild, not a project-tracked artifact. Confirm whether it should be referenced from the project file.
4. **3D.** FlowRenderer supports a 3D scene variant; this plan targets the 2D map canvas only. 3D is out of scope unless requested.

---

## 10. Reference

- Esri FlowRenderer (target behaviour + knob surface): https://developers.arcgis.com/javascript/latest/references/core/renderers/FlowRenderer/
- Engine output schema consumed: `Mesh2DH5Reader` header (`include/io/mesh2dh5reader.h`), `Default2DOutputPlugin` (openswmm.engine `src/engine/2d/output/`).
- Existing styling MVC pattern: [`RENDERING_2D_RESULTS_STYLING_PLAN.md`](RENDERING_2D_RESULTS_STYLING_PLAN.md).
- Offscreen GL integration contract: [`RENDERING_5M_PLAN.md`](RENDERING_5M_PLAN.md), `map/swmmlayerglrenderer.{h,cpp}`.
