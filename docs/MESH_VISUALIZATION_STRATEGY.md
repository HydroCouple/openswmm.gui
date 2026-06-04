# Mesh Visualization Strategy

Two parallel workstreams are documented here:
1. **Mesh Generation Dialog — Tab Redesign** (UX)
2. **Default Mesh Rendering** (visual quality, pre-theming)

Both are implemented in the current slice.  
Full visual customization is deferred to **Slice AC.4 — Layer Theming**.

---

## 1. Mesh Generation Dialog Tab Redesign

### Problem

The original dialog was a single `QScrollArea` containing six stacked `QGroupBox`
widgets (~640 px of scrollable content) plus a fixed footer.  It was dense,
required constant scrolling, and mixed "what data?" concerns with "how to mesh?"
concerns on the same visual plane.

### Solution

Replace the scroll area with a `QTabWidget` (3 tabs) and move the output
destination outside the tab widget so it is always visible — a user must know
where the file is going before clicking Generate.

### Tab structure

```
┌────────────────────────────────────────────────────────┐
│  Mesh Generation                                       │
│  ──────────────────────────────────────────────────    │
│  [ Sources ]  [ Quality ]  [ Hydraulics ]  ← QTabWidget│
│  ┌──────────────────────────────────────────────────┐  │
│  │  (tab content — no scroll needed, ~300 px each)  │  │
│  └──────────────────────────────────────────────────┘  │
│  ──────────────────────────────────────────────────    │
│  Output  ◉ External .2dm  ○ Inline in .inp   [Browse]  │
│          Path: ___________________________________      │
│  ──────────────────────────────────────────────────    │
│  [progress label / bar — hidden until running]         │
│                               [Close]  [Generate ▶]    │
└────────────────────────────────────────────────────────┘
```

#### Tab 1 — Sources

All "what data feeds in" controls:

| Group | Controls |
|---|---|
| **Sources** | DTM raster combo · Domain extent label |
| **Auxiliary feature layers** | Boundary polygon combo · Constraining-point layer list · Constraining-line layer list |
| **1D–2D Coupling** | Include junctions/outfalls/storage · Include conduits · Include subcatchments |

#### Tab 2 — Quality

All "how to triangulate" controls:

| Group | Controls |
|---|---|
| **Triangle quality** | Max triangle area · Min angle · Max Steiner points · Allow Steiner on boundary |
| **PSLG optimisation** | Geometry simplification ε (Ramer-Douglas-Peucker) · Steiner snap radius |
| **Terrain-adaptive thinning** | Enable checkbox · Elevation tolerance · Refinement passes · Max thinning points |

#### Tab 3 — Hydraulics

| Group | Controls |
|---|---|
| **Roughness (Manning's n)** | Constant value spinner (active) · Categorical raster · Shapefile field (both deferred — tooltip says "Full support coming in a future release" instead of plain disabled) |

The tab is intentionally thin now; it will grow as `ManningsSampler` support
lands (shapefile-field and categorical-raster modes).

### Footer (outside tabs — always visible)

A plain `QFormLayout` beneath a `QFrame::HLine` separator (no group-box border)
so it reads as "infrastructure, not content":

- **Output mode** row: `◉ External .2dm` / `○ Inline in .inp`
- **Mesh file** row: path edit + Browse button
  (path edit and Browse are disabled when Inline mode is selected)

Below the footer: the progress label + bar (hidden until Generate is pressed)
and the standard button box.

### Additional design choices

- **Window size**: 520 × 510 (down from 560 × 640).  No tab needs more than
  ~300 px; footer + buttons add ~150 px.
- **`addStretch()`** at the bottom of each tab so controls pin to the top on
  resize.
- **No icons on tabs** — the current project has no tab-bar icon assets.
- **All widget pointers, signals, and logic unchanged** — only `buildUi()` is
  rewritten.

---

## 2. Default Mesh Rendering (Pre-Theming)

### Goal

When a 2D mesh layer is active, the canvas should convey terrain shape clearly
using a filled elevation color scale modulated by a shaded-relief hillshade, with
black triangle-edge outlines to make individual elements legible.  This is the
*default* visual until Slice AC.4 introduces per-layer style overrides.

### Render passes (in `MeshGraphicsItem::paint()`)

```
Pass 1  Filled triangles   Terrain color ramp × hillshade factor  α = 200 (active) / 180 (inactive)
Pass 2  Edges              Solid black outlines                    α = 140 thin / 200 steep
Pass 3  Nodes (optional)   Elevation-colored dots                  plain 2.5 px / tagged 5 px halo
```

#### Pass 1 — Terrain fill with shaded relief

**Elevation color ramp** (5-stop, deep-water to snow):

| t    | Color        | Hex     |
|------|--------------|---------|
| 0.00 | Deep blue    | #1a3d6b |
| 0.20 | Sea green    | #2e8b57 |
| 0.50 | Yellow-green | #c8d94e |
| 0.75 | Golden       | #c8a000 |
| 1.00 | Near-white   | #f0f0e8 |

**Hillshade** uses a NW light at 35° elevation (standard cartographic convention):

```
light direction: L = (-0.5774, -0.5774, +0.5774)  (unit vector)
face normal N   : cross-product of the two edge vectors using (scene_x, scene_y, z)
illumination    : lit = clamp(dot(N, L), 0.15, 1.0)
final color     : R' = R × lit,  G' = G × lit,  B' = B × lit
```

The multiplicative blend preserves hue better than Qt's `QColor::darker()` and
produces the characteristic dark ravines / bright ridgelines of standard shaded
relief maps.

The `lit` floor of 0.15 (rather than 0.0) prevents completely black faces on
overhanging surfaces, which can appear as rendering artefacts.

**Alpha**: 200 when the layer is the active mesh; 180 otherwise.  The fill is
intentionally opaque — the mesh is the primary data layer when it is present.
Underlying basemap tiles and SWMM network elements are still visible at the
edges and through semi-transparent areas near the outline.

#### Pass 2 — Black edge outlines

Edges are split into two width classes based on normalised slope:

| Slope vs. max | Pen width (active) | Alpha | Intent |
|---|---|---|---|
| ≤ 35% of max | 0.8 px | 140 | Gentle / flat terrain |
| > 35% of max | 2.0 px | 200 | Steep breaks, levees, channels |

Both use `Qt::SolidLine`, cosmetic (screen-pixel), black.

The thicker steep-edge class makes channel walls and embankments stand out
against the shaded-relief background.

#### Pass 3 — Vertex dots

Unchanged from the prior slice.  Plain vertices: 2.5 px dots colored by
elevation.  SWMM-tagged (coupled) vertices: 5 px white halo + 3.5 px
elevation-colored dot.

### Theming hook (Slice AC.4)

When the layer-style system lands, `MeshGraphicsItem` will accept a
`MeshLayerStyle` value passed through `SWMM2DMeshLayer::setStyle()`.  All
hardcoded values above — color ramp stops, alpha, light direction, edge widths,
slope threshold — become fields of `MeshLayerStyle` with the current values as
defaults.  The hook site is marked with a comment in `MeshGraphicsItem::paint()`
so it is easy to find.

```cpp
// ── Theming hook (Slice AC.4) ─────────────────────────────────────
// Replace hardcoded constants below with m_style fields once
// SWMM2DMeshLayer::setStyle(MeshLayerStyle) is wired up.
// ─────────────────────────────────────────────────────────────────
```

### Files touched

| File | Change |
|---|---|
| `src/layers/swmm2dmeshlayer.cpp` | `MeshGraphicsItem::paint()` — fill alpha, hillshade blend, edge pass |
| `src/ui/dialogs/meshgenerationdialog.cpp` | `buildUi()` — tab layout, footer |
| `docs/MESH_VISUALIZATION_STRATEGY.md` | this document |

### Not in scope (deferred to Slice AC.4)

- `MeshLayerStyle` struct and `SWMM2DMeshLayer::setStyle()` API
- Layer Properties panel "Style" tab for mesh layers
- Result-animation color scale (time-varying depth / velocity)
- Legend / color-bar overlay widget
