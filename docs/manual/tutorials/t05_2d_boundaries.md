@page tutorial_2d_boundaries T5 — Culverts, Embankments, Boundary Conditions and Edge Conveyance

## Goal

Build an intuition for what happens at the **edge** of a 2D domain and at the
**seam** between a 1D pipe and the surface. You will open a flat plain crossed
by a road embankment with a culvert through it, inspect and restyle its
`NORMAL_FLOW` outflow edges, look at how the buried culvert is stitched to the
mesh, animate the moment the road overtops, and cut a profile across the
embankment. Then you will compare a second, older variant of the same idea, and
finally walk a purpose-built reference model that contains **one row of every
boundary-condition type the engine accepts**, plus an `[2D_EDGE_CONVEYANCE]`
"leaky berm".

## Capabilities exercised

- `[2D_BOUNDARY_CONDITIONS]` — `WALL`, `NORMAL_FLOW`, `SPECIFIED_STAGE`,
  `TS_STAGE`, `SPECIFIED_FLOW`, `TS_FLOW`, `RATING_CURVE`
- `[2D_EDGE_CONVEYANCE]` — sub-grid berms, walls and partial blockages
- `[2D_VERTEX_NODE_MAP]` and `[2D_TRIANGLE_NODE_MAP]` — 1D↔2D coupling
- `[2D_OPTIONS]` — `CELL_CLOSURE`, `FACE_RECONSTRUCTION`, `COUPLING_*`,
  `RAINFALL_MODE`, and the CVODE-era keys the engine has retired
- **Select Mesh Edges** tool, mesh attribute table, mesh layer styling
- 2D mesh profile plot, map animation, flow balance, the status report

## Files

| File | What it is |
|---|---|
| `examples/demo_road_culvert/road_culvert.oswp` | The main model — open this one |
| `examples/demo_road_culvert/road_culvert.inp` | Flat plain; embankment; 3-conduit culvert at grade |
| `examples/demo_road_culvert/road_culvert.2d.h5` | Pre-computed 2D results |
| `examples/demo_road_culvert/road_culvert.out` / `.rpt` | Pre-computed 1D results and report |
| `examples/demo_weir_culvert/weir_culvert.oswp` | The older, sloped, 50-conduit variant |
| `docs/manual/tutorials/models/2d_complete_example.inp` | The boundary-type reference model |

`demo_weir_culvert/` also contains `_t/` and `diag_gui_perf/` scratch
directories and a `weir_culvert.inp.orig` backup. Ignore all three — they are
regeneration artefacts, not part of the example.

\figtodo{t05_road_culvert_overview.png, The road culvert model on the map canvas — flat mesh; transverse embankment; three coupled junctions along the culvert centreline}

## Steps

### 1. Open the road-culvert model

**File → Open…** and pick `examples/demo_road_culvert/road_culvert.oswp`. The
project file restores the mesh layer, its hillshade and contour settings, the
per-kind symbology and the 2D results layer, so the map comes up styled.

What is in the model:

| Item | Value |
|---|---|
| Domain | 1000 m (X) × 100 m (Y), **flat**, z = 1.0 m everywhere |
| Mesh | 1111 vertices, 2000 triangles, 10 m cells, alternating diagonal |
| Embankment | 0.5 m high plateau across the full width at X = 500 ± 10 m |
| Culvert | 3 conduits `C_P00`/`C_P01`/`C_P02`; `CIRCULAR 0.6` m; crown at grade |
| Nodes | Junctions `J_P00`, `J_P01`, `J_P02`; free outfall `J_P03` |
| Rain gage | `GAGE1`, `INTENSITY`, 15 min, `TIMESERIES STORM` (SCS Type II, 900 mm) |
| Boundaries | 10 `NORMAL_FLOW` rows on the X = 1000 face, slope 0.001 |

There are no subcatchments: rainfall lands directly on the mesh, because
`[2D_OPTIONS] RAINFALL_MODE` is `NATURAL_NEIGHBOUR` and `GAGE1` has a map
location in `[SYMBOLS]`.

### 2. Look at the boundary-condition edges

The whole east face of the domain is an outflow boundary. Every other boundary
edge is left out of the section, and **an unlisted boundary edge defaults to
`WALL`** — so the plain is walled on three sides.

1. Make the mesh layer active on the **Mesh 2D** ribbon tab (pick it in the
   **Mesh:** combo of the mesh toolbar).
2. Activate **Select Mesh Edges** (**Mesh 2D** tab, or
   **Model → Mesh → Select Mesh Edges**). By default it picks **boundary edges
   only**; press **A** to include interior edges and **B** to go back.
3. Click one edge on the X = 1000 face, then **Ctrl-click** (⌘-click on macOS)
   another edge further along that face. Every boundary edge on the shortest
   run between the two is added to the selection. Keep Ctrl-clicking to walk
   the whole face in a few clicks. **Esc** drops a pending anchor; **Esc**
   again clears the selection. (\ref manual_selection has the full rules.)
4. Read the **Edges** group on the mesh toolbar. With a run selected it shows
   `Edges: 10 (10 boundary)`, and the **BC type** combo reads **Normal Flow**
   with **Slope:** `0.00100`.

\figtodo{t05_select_mesh_edges.png, The Select Mesh Edges tool with the whole east outflow face selected and the Edges group showing Normal Flow}

The seven type entries in that combo map one-to-one onto the engine's
`[2D_BOUNDARY_CONDITIONS]` `TYPE` tokens:

| Combo entry | `.inp` token | `PARAM_1` |
|---|---|---|
| **Wall** | `WALL` | — (no flux) |
| **Normal Flow** | `NORMAL_FLOW` | bed slope, dimensionless |
| **Specified Stage (Constant)** | `SPECIFIED_STAGE` | water-surface elevation |
| **Specified Stage (Timeseries)** | `TS_STAGE` | a `[TIMESERIES]` name |
| **Specified Flow (Constant)** | `SPECIFIED_FLOW` | discharge per metre of edge |
| **Specified Flow (Timeseries)** | `TS_FLOW` | a `[TIMESERIES]` name |
| **Rating Curve** | `RATING_CURVE` | a `RATING` `[CURVES]` name |

Changing the combo applies immediately to **every** selected edge, so a run
selection is the fast way to build an outflow face.

### 3. The same thing in the attribute table

Open the attribute table (**Ctrl+Alt+4**, or **View → Panels → Attribute
Table**) and switch it to the mesh layer's **Edge** rows. The edge columns are:

| Column | Meaning | Writes |
|---|---|---|
| **Edge** | owning triangle and local edge, as `triangle:edge` | — |
| **Boundary** | Yes / No — BCs apply to boundary edges only | — |
| **Length (map units)** | edge length | — |
| **Conveyance** | flux multiplier in [0, 1]; 1 = unrestricted | `[2D_EDGE_CONVEYANCE]` |
| **BC Type** | the seven entries above | `[2D_BOUNDARY_CONDITIONS]` `TYPE` |
| **Stage** | constant water-surface elevation | `PARAM_1` |
| **Bed Slope** | must be > 0 | `PARAM_1` |
| **Flow** | discharge per metre of edge | `PARAM_1` |
| **Time Series** | series name for `TS_STAGE` / `TS_FLOW` | `PARAM_1` |
| **Rating Curve** | curve name for `RATING_CURVE` | `PARAM_1` |
| **Group** | optional named boundary group | `GROUP` |

Columns that do not apply to the row's BC type render as `—` and refuse edits.
An interior edge is listed once, under its lower triangle slot.

\figtodo{t05_edge_attribute_table.png, Mesh edge rows in the attribute table with the BC Type; Bed Slope and Conveyance columns visible}

### 4. Style the boundary edges so you can see them

Open the mesh layer's style dialog (double-click the layer in **Layers**, or
right-click → **Properties…**) and go to the **Boundary Conditions** tab.

- The header checkbox **Show boundary-condition indicators** turns the ring on.
- **Per-type styling** is a grid of **Type / Colour / Width**, one row per BC
  type. Types that do not occur in the mesh get ` (none in mesh)` appended.
  The **Wall** row is labelled `… / interior edges` and shows
  `(wireframe width)` instead of a width spin, because walls are drawn with the
  wireframe.
- The boundary ring stays visible at every zoom level, so give **Normal Flow**
  a strong colour and a wide stroke — the ten outflow edges then read clearly
  against the three walled faces.

The default colour and width for these indicators come from the
**2D Mesh Boundary-Condition Edges** group in **Preferences**.

\figtodo{t05_bc_style_tab.png, The Boundary Conditions tab of the mesh layer style dialog with per-type colour and width}

### 5. Coupling at the culvert junctions

The buried pipe is stitched to the surface through `[2D_VERTEX_NODE_MAP]`:

```
;;VERTEX NODE             CD         AREA
552      J_P00            0.65       0.283
553      J_P01            0.65       0.283
557      J_P02            0.65       0.283
558      J_P03            0.65       0.283
```

Four vertices, four nodes, `CD` 0.65 and an exchange **AREA** of 0.283 m² —
the full cross-section of a 0.6 m barrel.

Switch to **Select Mesh Vertices** on the **Mesh 2D** tab and click vertex 552.
The **Vertices** group on the mesh toolbar now shows:

| Control | What it does | Writes |
|---|---|---|
| **Z:** | vertex bed elevation | `[2D_VERTICES]` Z |
| tag field | descriptive vertex tag | `[2D_VERTICES]` TAG |
| coupled-node combo | the SWMM node this vertex exchanges with; blank = uncoupled | `[2D_VERTEX_NODE_MAP]` node |
| **Cd:** | coupling discharge coefficient | `[2D_VERTEX_NODE_MAP]` CD |
| **Area:** (m²) | exchange area | `[2D_VERTEX_NODE_MAP]` AREA |

The same four fields are the **Coupled Node**, **Coupling Cd** and
**Coupling Area (m²)** columns of the vertex rows in the attribute table.

Two buttons in the **Coupling** group automate the tedious part:
**Auto-couple** links every vertex that is coincident with a SWMM node
(`Coupled %1 vertex(es) to coincident SWMM nodes.`), and **Remap 1D↔2D**
clears the maps and re-derives them for the whole model — nodes on a vertex get
vertex coupling, nodes inside a cell get cell coupling.

Why the culvert engages at all is worth understanding: the pipe **crown** sits
at the ground surface (invert one diameter below), and `MaxDepth` equals the
diameter, so the coupling spill threshold `invert + MaxDepth` lands exactly on
grade. Ponded water enters the barrel as soon as it reaches the surface, and a
surcharged pipe spills back onto the mesh. With a literal invert-at-grade the
threshold would sit a full diameter *above* the surface and the culvert would
never engage.

\figtodo{t05_coupled_vertex.png, A coupled culvert vertex selected — the mesh toolbar Vertices group showing node J_P00; Cd 0.65 and Area 0.283 m2}

### 6. Load the results and animate the overtopping

The bundled results are already referenced by the project. If they are not
loaded, use **File → Import → Add SWMM Output…** for `road_culvert.out` and
**Add 2D Results…** for `road_culvert.2d.h5` (filter
`OpenSWMM 2D Results (*.h5)`).

To re-run instead, press **Ctrl+R**. The shipped report records
`Total elapsed time: 00:00:25` for this model.

Use the **Results** ribbon tab's transport (**Play**, **Pause**, **Stop**,
**Skip Back**, **Skip Forward**) to animate. Style the 2D results layer by
depth and watch:

1. Rain ponds **symmetrically across Y**. That symmetry is the point of the
   alternating ("union-jack") mesh diagonal — a mesh that splits every quad the
   same way is not N–S symmetric, and the numerical solution drifts water
   against one edge even though the terrain and the rainfall are uniform.
2. Water sheet-flows east and leaves through the `NORMAL_FLOW` face.
3. The upstream pond rises against the embankment, and at the SCS Type II peak
   (about hour 12) it reaches the 1.5 m road crest and **overtops** for the rest
   of the run, sheeting a few centimetres deep across the crown.

\videotodo{Animating the road-culvert storm — ponding; culvert engagement and road overtopping}

### 7. Cut a 2D profile across the embankment

Use **Analysis → Profile 2D** (`actionPlotProfile2D`) to trace a line across
the domain, from the ponded upstream side, over the crest, to the lee side.
Finish the trace with **Return**; **Esc** cancels it. The plot opens as
**2D Mesh Profile** and shows ground, animated depth and the depth envelope
along the trace; its **Profile Display Options** sub-dialog exposes the cell
boundary colour. Press **Home** to fit.

The mesh toolbar's own *Trace Profile* draws a **bed-only** profile — useful for
checking that the embankment really is a cell-wide plateau. That matters: a
single raised vertex column leaves the flanking cell beds much lower, and the
dam then leaks numerically from a lower effective crest and can never hold the
nominal 0.5 m head.

\figtodo{t05_profile_across_embankment.png, A 2D mesh profile across the road embankment showing the ponded upstream water surface and the crest}

### 8. Check the flow balance and the continuity blocks

Select the culvert conduits and use **Analysis → Flow Balance Upstream** /
**Flow Balance Downstream**. The result is an information box:

```
Subnetwork: N node(s), M boundary link(s)

Inflow:  …
Outflow: …
Net:     …

(final time-step flows, in project flow units)
```

Note the caveat printed in the box itself: this is a **final-time-step**
snapshot in project flow units, not a volume integral. For volumes, open the
status report (**Analysis → Report**; **Analysis → Show Mass Balance** opens the
same report, which is where continuity lives).

The shipped `road_culvert.rpt` gives you concrete targets:

| Quantity | Value |
|---|---|
| Flow routing continuity error | 0.008 % |
| 2D surface routing continuity error | −0.000 % |
| Rainfall inflow | 90 000.4 m³ |
| 1D → 2D spill inflow | 17 735.5 m³ |
| 2D → 1D drain outflow | 40 096.0 m³ |
| Boundary outflow | 88 480.4 m³ |
| Max depth `J_P00` / `J_P01` / `J_P02` | 1.14 / 1.13 / 0.96 m |
| Max flow `C_P00` / `C_P01` / `C_P02` | 0.237 / 0.553 / 0.285 m³/s |

Read the report's warnings too — this model emits `WARNING 04: minimum
elevation drop used for Conduit …` for all three barrels (they are laid at zero
slope), a note that 2D outfall withdrawal was capped by the water available on
the surface in twelve sync batches, and a `1D <-> 2D Exchange Reconcil.` block
whose internal percentage is a divide-by-near-zero artefact on the tiny
pipe-fill volume. None of these invalidates the run, but do not present the
model as a clean-continuity showcase without saying so.

\figtodo{t05_report_continuity.png, The status report's flow routing and 2D surface routing continuity blocks}

### 9. Compare with `demo_weir_culvert`

Open `examples/demo_weir_culvert/weir_culvert.oswp` in a second window. Same
idea, older build:

| | `demo_road_culvert` | `demo_weir_culvert` |
|---|---|---|
| Terrain | flat, z = 1.0 | 0.001 slope toward +X |
| Mesh diagonal | alternating | single (SW→NE) |
| Culvert | 3 conduits, Ø 0.6 m, 60 m | 50 conduits, Ø 0.15 m, 500 m |
| Coupled nodes | 4 (`J_P00`–`J_P03`) | 2 (`J_P00`, `J_P50`) |
| Exchange AREA | 0.283 m² (barrel area) | 0.4 m² |
| Rainfall | 900 mm SCS Type II | 100 mm |
| Routing continuity | 0.008 % | 0.334 % |

Two teaching points fall out of the comparison.

**Mesh orientation is a discretisation choice with physical consequences.**
Water in `demo_weir_culvert` drifts toward the north edge even though the
terrain is flat in Y and the rainfall is uniform. The 2D flux depends only on
cell state and edge geometry, so this is not physics: the legacy mesh splits
every quad along the same SW→NE diagonal, and a north↔south reflection turns
each of those into an NW→SE diagonal — a *different* mesh. The discretisation is
not N–S symmetric even though the problem is. The alternating diagonal cancels
the two orientations and restores symmetry. (The east–west ponding against the
embankment *is* physical: the domain is walled on three sides.)

**Undersized conveyance produces a dead culvert.** At Ø 0.15 m and zero slope
the older barrel conveys of the order of 0.003 m³/s. Water drains into the
coupled junction, cannot pass, and floods straight back onto the mesh.

Both example folders describe the embankment as a "weir" or a "road", but
neither model contains a `[WEIRS]` section. The obstruction is **terrain** —
raised Z values in `[2D_VERTICES]` — not a 1D weir link. That is usually the
right choice for something a vehicle drives over, and it is what makes
`FACE_RECONSTRUCTION` matter (step 11).

### 10. Retired 2D options

`weir_culvert.inp` still carries the CVODE-era `[2D_OPTIONS]` keys:

```
MIN_TIMESTEP           0.001
REL_TOLERANCE          0.001
ABS_TOLERANCE          1e-05
MAX_KRYLOV_DIM         30
COUPLING_INTERVAL      0
MAX_CVODE_STEPS        500
LINEAR_SOLVER          GMRES
PRECONDITIONER         AMG
```

Every one of those is **retired**. On load the engine warns and ignores them:

> `WARNING 104: [2D_OPTIONS] <key> was retired with the CVODE/ARKODE 2D
> solvers and was ignored; the explicit local-inertial marcher is the only 2D
> integrator.`

The full retired list is `MIN_TIMESTEP`, `REL_TOLERANCE`, `ABS_TOLERANCE`,
`MAX_CVODE_STEPS`, `MAX_KRYLOV_DIM`, `LINEAR_SOLVER`, `PRECONDITIONER`,
`JACOBIAN`, `ATOL_AREA_REF`, `COUPLING_INTERVAL`, `COUPLING_WINDOW`,
`ACTIVE_SET`, `ACTIVE_SET_HALO`, `MOMENTUM`, plus any `INTEGRATOR` value other
than `EXPLICIT`. The replacements are `THETA`, `CFL_NUMBER`, `LTS_TIERS`,
`H_MOVE`, `FROUDE_MAX`, `ADVECTION`, `MAX_TIMESTEP` and `COUPLING_AREA`.

Warn-and-ignore applies on the **file-load** path. Setting a retired key
through the API — which is what the **Simulation Options** dialog does — is a
hard error instead, so the dialog will never write one back. Saving the model
from SWMMVis is the simplest way to clean an old deck.

### 11. Walk the reference model, section by section

Open `docs/manual/tutorials/models/2d_complete_example.inp`
(**File → Open…**). It is a 20 m × 20 m parking lot on a 3 × 3 vertex grid —
9 vertices, 8 triangles, Manning's *n* = 0.018 — sloping from z = 101.0 in the
north to z = 100.0 in the south. It exists to carry one row of every 2D
section, so it is small enough to read end to end.

Two comment lines above `[TITLE]` are read by the engine's prescan:

```
;; UNITS: SI (m)
;; SOURCE_CRS: EPSG:32616
```

With `;; UNITS: SI (m)` the engine treats the mesh coordinates as metres and
skips the `FLOW_UNITS`-driven ft→m scaling. Write it into every new mesh.

#### `[2D_OPTIONS]`

\snippet 2d_complete_example.inp twod_options

| Key | Meaning | Default |
|---|---|---|
| `MAX_TIMESTEP` | caps the marcher's CFL substeps and the coupling sync batch, s | 10.0 |
| `DRY_DEPTH` | wet/dry threshold, m | 0.001 |
| `LIMITER_EPSILON` | slope-limiter ε for the output gradients | 1.0e-6 |
| `FLUX_DH_EPS` | head-difference regularisation for the √\|Δη\| flux, m | 0.004 |
| `COUPLING_CD` | default coupling discharge coefficient | 0.65 |
| `COUPLING_SYNC` | s; 0 couples every routing step, > 0 batches the 2D advance | 0.0 |
| `COUPLING_AREA` | `DEFAULT` or `AUTO` — derive missing exchange areas from conduits | `DEFAULT` |
| `RAINFALL_MODE` | `NATURAL_NEIGHBOUR`, `SYSTEM` or `NONE` | `NATURAL_NEIGHBOUR` |
| `CELL_CLOSURE` | `FLAT` or `VFR` — volume→free-surface closure | `FLAT` |
| `FACE_RECONSTRUCTION` | `MEAN` or `VFR_FACE` — conveyance depth at a face | `MEAN` |
| `VFR_MIN_WET_FRAC` | wetted-fraction floor for the VFR closure, (0, 0.5] | 0.01 |
| `INTEGRATOR` | `EXPLICIT` — the only accepted value | `EXPLICIT` |
| `THETA` | momentum θ-weighting, (0, 1]; 1 = pure Bates 2010 | 0.8 |
| `CFL_NUMBER` | Courant safety factor α, (0, 1] | 0.7 |
| `H_MOVE` | flux-activation depth, m; below it a cell is source-only | 0.003 |
| `LTS_TIERS` | local-timestepping tiers, 1–8; 1 = global dt | 4 |
| `FROUDE_MAX` | supercritical face velocity clamp | 1.5 |
| `ADVECTION` | convective momentum flux at interior faces; opt-in | `NO` |
| `BACKEND` | `AUTO`, `CPU`, `OMP`, `CUDA`, `HIP`, `SYCL` | `AUTO` |
| `REPORT_2D` | write 2D results | `YES` |
| `OUTPUT_FILE` | HDF5 output path, relative to the `.inp`; empty = no 2D output | *(empty)* |

Two of these earn special attention in this tutorial:

- **`CELL_CLOSURE VFR`** replaces the flat `η = z̄ + V/A` reconstruction with
  the exact stage–storage relation of the planar cell bed. `FLAT` overstates
  the free surface on a partially wet, slope-spanning cell by up to two-thirds
  of the cell relief — the "water climbs uphill" artefact. VFR costs roughly
  three to eight times more marcher substeps, so it is opt-in and pays off most
  on shallow water over gentle slopes.
- **`FACE_RECONSTRUCTION VFR_FACE`** governs the conveyance depth at boundary
  *and* interior faces. It is what makes a thin crest — an embankment, a levee,
  a road crown resolved as a line of high vertices — block until water genuinely
  reaches the crest, instead of the centroid-diluted face depth letting it
  overtop at roughly a third of the height. `road_culvert.inp` sets both
  `CELL_CLOSURE VFR` and `FACE_RECONSTRUCTION VFR_FACE` for exactly that reason.

Set all of these on the **2D Surface Routing** page of the Simulation Options
dialog (\ref manual_simulation_options), across its five tabs:

| Tab | Groups |
| --- | --- |
| **Hydrodynamics** | Time stepping · Explicit marcher |
| **Mesh & Closure** | Mesh · Cell closure (wetting / drying) |
| **Coupling** | 1D ↔ 2D coupling |
| **Processes** | Processes (rainfall, infiltration, evaporation, transport) · Groundwater (subsurface) |
| **Rainfall & Output** | Performance · Output |

\figtodo{t05_sim_options_2d_page.png, The 2D Surface Routing page of the Simulation Options dialog}

#### `[2D_BOUNDARY_CONDITIONS]`

\snippet 2d_complete_example.inp boundary_conditions

The grammar is `TRI EDGE TYPE [PARAM_1 [PARAM_2 [GROUP]]]`.

- An edge is identified by a **triangle index plus a local edge index**, not by
  a vertex pair. Edge *e* is the edge **opposite vertex *e*** of the triangle:
  for `T0 = (v0, v1, v4)`, `e0 = (v1, v4)`, `e1 = (v4, v0)`, `e2 = (v0, v1)`.
- `PARAM_2` is reserved and ignored — but it is read positionally, so you must
  write `*` in that slot if you want to supply a `GROUP`.
- `GROUP` is an optional name; `*` means none.
- Any boundary edge not listed defaults to `WALL`.
- `SPECIFIED_FLOW` is **outward-positive**, so an inflow is a negative value,
  and its units are project flow units **per metre of edge**.
- A `NORMAL_FLOW` row with slope 0 zeroes the Manning outflow and therefore
  behaves as a wall. The toolbar tooltip says so; the engine warns at
  validation. There is no auto-compute from bed geometry.

Row by row, this is what the reference model does:

| Row | Edge | Type | Effect |
|---|---|---|---|
| `0 2` | T0 south | `NORMAL_FLOW 0.0050` | Manning outflow on a 0.5 % pseudo-slope |
| `2 2` | T2 south | `NORMAL_FLOW 0.0050` | the rest of the south face |
| `2 0` | T2 east | `TS_STAGE TIDAL_TS` | stage driven by a `[TIMESERIES]`, group `east_tide` |
| `6 0` | T6 east | `RATING_CURVE DOWN_RC` | stage → flow per metre from a `RATING` curve, group `east_rc` |
| `7 0` | T7 north | `SPECIFIED_FLOW 0.0500` | prescribed per-metre discharge, group `upstream` |
| `5 0` | T5 north | `SPECIFIED_STAGE 101.50` | fixed water-surface elevation |
| — | west face | *(unlisted)* | defaults to `WALL` |

To reproduce any of these in the GUI: select the edge with **Select Mesh
Edges**, pick the type in the toolbar's BC combo, and fill the one parameter
that appears on the stacked page beside it — **Slope:**, **Stage:**, **Flow/m:**,
**TS:** or **Curve:**. The **…** button next to the combo browses, creates or
edits the referenced time series or rating curve. Named groups are set in the
attribute table's **Group** column.

\figtodo{t05_bc_types_reference.png, The reference model's six boundary rows highlighted on the map with per-type colours}

#### `[2D_VERTEX_NODE_MAP]` and `[2D_TRIANGLE_NODE_MAP]`

\snippet 2d_complete_example.inp coupling_maps

Both take `<index-or-tag> <SWMM node> [CD] [AREA]`. The first token is tried as
an integer index; if it parses as a number but is out of range, it falls back
to a tag lookup, so a mesh with numeric tags like `5001` still works.

The two differ in one important way. The **vertex** map is an assignment — one
row per vertex. The **triangle** map **appends**, so several nodes may couple to
the same triangle. That is what lets the two endpoints of a weir or orifice
share a single cell.

Here vertex 4 (the centre of the lot, bed 100.50 m) couples to the drain inlet
`J1`, whose rim is 99.0 + 1.5 = 100.5 m — flush with the surface. The triangle
tagged `vault` couples to the storage node `ST1` with a larger 10 m² exchange
area and a slightly lower `CD` of 0.60.

`AREA` is in the mesh's length units squared. With
`[2D_OPTIONS] COUPLING_AREA AUTO`, rows that do not author an explicit area get
one derived from the largest connected conduit, clamped to 0.05–2.0 m².

#### `[2D_EDGE_CONVEYANCE]`

\snippet 2d_complete_example.inp edge_conveyance

Grammar: `FROM_VERTEX TO_VERTEX CONVEYANCE`, with the edge identified by a
**vertex pair** this time, and conveyance in [0, 1] (values outside are
rejected). Every edge defaults to 1.0.

Conveyance is a multiplicative attenuation applied to the edge flux — the edge
transmissivity ψ of the integral-porosity shallow-water literature. It applies
to **interior** edges as well as boundary edges, and the engine mirrors each
row onto the partner slot of the neighbouring triangle so that antisymmetry, and
therefore mass conservation, is preserved. That makes it the right tool for
sub-grid features you do not want to mesh: garden walls, hedges, fences, kerbs,
a partially blocked road crossing — or, as here, a berm that leaks 30 % of the
flux the bare geometry would allow.

In the GUI it is the **ψ:** spin box in the mesh toolbar's **Edges** group
(0–1, three decimals, default 1.000) and the **Conveyance** column in the edge
attribute table. The spin deliberately sits outside the BC parameter stack so
it stays available for interior edges, which have no boundary condition at all.

\figtodo{t05_edge_conveyance.png, Two interior berm edges selected with the mesh toolbar psi spin set to 0.300}

## What to look for

- Ponding on the road-culvert plain is **symmetric across Y**. If it is not,
  the mesh diagonal — not the physics — is the suspect.
- The culvert only carries large net flow **after** the water surfaces on both
  sides differ. Before overtopping it mostly equalises the two sides, because a
  flat plain gives almost no driving head early in the storm.
- The 2D continuity error in `road_culvert.rpt` is essentially zero; the 1D
  routing figure is small but non-zero and its per-node breakdown is dominated
  by the small pipe-fill volume.
- Boundary edges you never listed really are walls. If water disappears off an
  edge you thought was closed, check for a stray `NORMAL_FLOW` row.
- A `NORMAL_FLOW` edge with slope 0 silently behaves as a wall.

## Variations

- **Turn the road into a dam.** Set every crest edge's **Conveyance** to 0.000
  and re-run. The culvert then carries everything until overtopping.
- **Swap the outflow face for a tidal boundary.** Select the whole X = 1000 run,
  set **Specified Stage (Timeseries)**, and point it at a new series built in
  the time-series editor. Watch the pond fail to drain at high stage.
- **Flip `CELL_CLOSURE` to `FLAT`** on the 2D Surface Routing page and re-run.
  Compare the shoreline position on the shallow upstream pond against the VFR
  run in the comparison plot.
- **Flip `FACE_RECONSTRUCTION` to `MEAN`** and watch the road overtop early —
  the classic centroid-diluted crest.
- **Turn `ADVECTION` on** and compare the overtopping jet on the lee side.
- **Break the culvert on purpose**: set `PIPE_BURIAL = 0` in
  `demo_road_culvert/generate_model.py` and regenerate. Invert-at-grade puts
  the spill threshold a full diameter above the surface and the culvert goes
  inert.

## Related

- \ref manual_2d_mesh — mesh generation, cell parameters and coupling in full
- \ref manual_simulation_options — every page of the Simulation Options dialog
- \ref manual_selection — mesh edge and vertex selection, including path picking
- \ref manual_styling — layer styling and colour ramps
- \ref manual_profile_plots — 2D mesh profiles and terrain profiles
- \ref manual_analysis_tools — flow balance and 2D cell time series
- \ref tutorial_1d2d_coupling — capped street inlets coupled to a 2D surface
- \ref tutorial_pure_2d — a mesh with no 1D network at all
- \ref manual_troubleshooting — retired-option warnings and mesh failures
