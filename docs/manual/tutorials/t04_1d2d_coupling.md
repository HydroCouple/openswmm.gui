@page tutorial_1d2d_coupling T4 — Capped Street Inlets Coupled to a 2D Surface

## Goal

Work through the demo that exercises the whole 1D ↔ 2D coupling gate: two
capped street inlets that surcharge onto a 2D plaza, surface water that drains
back into the pipes, an outfall whose tailwater is taken from the 2D head, and
a mass balance that books both directions. You will find the vertex-to-node
map in the GUI, read the coupling coefficients, run the model, compare 1D
flooding against 2D depth at the same location, watch ponding drain back, cut
a profile with the 2D water surface on it, and read the coupled ledger.

\videotodo{Finding the vertex-node map — running the coupled model — watching J1 spill onto the plaza and drain back}

## Capabilities exercised

Inline 2D sections in an `.inp` · `[2D_VERTEX_NODE_MAP]` in the mesh tools,
the Properties panel and the attribute table · coupling `Cd` and area ·
Simulation Options → 2D Surface Routing · coupled runs · the Comparison Plot
across 1D and 2D series · results animation · profile plots with a 2D
inundation overlay · the coupled continuity ledger · retired `[2D_OPTIONS]`
keys.

## Files

| File | What it is |
| ---- | ---------- |
| `examples/demo_capped_street/capped_street.oswp` | The SWMMVis project |
| `examples/demo_capped_street/capped_street.inp` | Self-contained input — the 2D mesh is **inline**, there is no `.2dm` |
| `examples/demo_capped_street/capped_street.2d.h5` | Checked-in 2D results — 476 steps over 2 hours on 48 cells |
| `examples/demo_capped_street/capped_street.rpt` | Checked-in report |
| `examples/demo_capped_street/capped_street.out` | Checked-in 1D binary output |
| `examples/demo_capped_street/README.md` | The detailed engine-side notes this tutorial follows |

### The model

A 60 × 40 m urban street block. Underground:

```
SUB1 (0.30 ha) ──▶ J1 ──C1, 200 mm──▶ J2 ──C2, 200 mm──▶ J3 ──C3, 300 mm──▶ OUT1
SUB2 (0.20 ha) ─────────────────────▶ J2                                    (FIXED stage)
```

`J1` and `J2` are **capped**: each has a surcharge depth of 0.5 m on top of a
1.5 m full depth, so its spill threshold `z_top` sits 0.5 m above the rim.
`J3` is an ordinary interior junction and is not coupled at all.

On the surface, an inline mesh of **35 vertices** on a 7 × 5 grid at 10 m
spacing, split into **48 triangles**, Manning's *n* = 0.030, sloping uniformly
about 4 % from the north-east corner down to the south-west corner where
`OUT1` sits.

Three vertices carry the coupling:

```
[2D_VERTEX_NODE_MAP]
;;VERTEX NODE             CD         AREA
0        OUT1             0.65       1
17       J2               0.65       0.5
33       J1               0.65       0.5
```

Both subcatchments are 92 % impervious and drain to the pipes. The storm is a
symmetric triangular hyetograph peaking at **120 mm/hr at t = 15 min**, about
**30 mm** in total, followed by dry weather to t = 2 h. Flow units are `CMS`,
report step 15 s, routing step 5 s.

### Known state of this example

Read this before you interpret any number.

The checked-in `capped_street.inp` is **internally inconsistent about units**.
`FLOW_UNITS` is `CMS` and `[MAP] Units` is `METERS`, and the inline
`[2D_VERTICES]` elevations are metres (9.0 – 13.0 m, exactly as the README
describes). But the 1D inverts, depths and lengths carry foot-valued numbers
for the same physical quantities: `J1` is `36.4173` where the README says
11.1 m (11.1 × 3.28084 = 36.4173), `SurDepth` is `1.6404` where the README
says 0.5 m, `C1` is `98.4252` long where the README says 30 m, and `OUT1`'s
`FIXED` stage is `26.2467` where the README says 8.0 m.

The consequence is visible in the checked-in results:

- The 1D network sits about 26 m *above* the mesh, so neither gate ever opens.
  `Mesh2_face_coupling_flux` in `capped_street.2d.h5` is **identically zero at
  every cell and every time step**.
- The report's `2D Surface Routing Continuity` block shows
  `1D -> 2D Spill Inflow` = 0.000 and `2D -> 1D Drain Outflow` = 0.000, and a
  `Continuity Error (%)` of **29.199** — the plaza received 71.829 m³ of
  rainfall and still held 50.855 m³ at the end, with no outlet.
- There is **no `1D <-> 2D Exchange Reconcil.` block and no `2D Solver
  Statistics` block** in the checked-in `.rpt`.
- `J1` and `J2` still flood — 0.09 and 0.10 hours respectively, about 0.005 ×
  10⁶ L each — but that flooding is booked as ordinary 1D `Flooding Loss`, not
  as spill onto the surface.

Everything the README predicts about the *coupling* therefore describes what
the demo is meant to do, not what the shipped artefacts show. This tutorial
uses the shipped numbers where they exist and says plainly where the
behaviour you are looking for is absent. If you want to see the coupling work,
fix the units first: divide every `[JUNCTIONS]`, `[OUTFALLS]` and
`[CONDUITS]` elevation, depth and length by 3.28084 so the 1D network lands on
the same datum as the mesh.

## Steps

### 1. Open the project

**File → Open…** (`Ctrl+O`) → `examples/demo_capped_street/capped_street.oswp`.

The four 1D nodes draw diagonally across the block from `J1` at (50, 40) to
`OUT1` at (0, 0), and the inline mesh draws as a 7 × 5 grid of quads each
split along its south-west/north-east diagonal. Because a mesh layer is on the
canvas, the contextual **Mesh 2D** ribbon tab appears.

Unlike the Snoopy Lagoon example (\ref tutorial_2d_inundation), the mesh here
is **inline in the `.inp`** — `[2D_VERTICES]`, `[2D_TRIANGLES]` and
`[2D_VERTEX_NODE_MAP]` are sections of the input file, not an external
`.2dm`. **Simulation Options → Mesh** reflects that: the list entry reads
*(Inline mesh — embedded in project .inp)* and the summary line
*Active mesh: inline mesh embedded in project .inp*.

\figtodo{t04_project_open.png, The capped street project with the inline mesh and the four 1D nodes}

### 2. Find the vertex–node map

There is **no coupling property on a 1D node** — a junction's Properties dock
says nothing about the mesh. Coupling lives on the *mesh vertex*, and there
are four places to see it.

**a. The Mesh 2D ribbon tab.** Activate the vertex tool in the **Vertices**
group (an icon-only toggle; its tooltip reads *Select Vertex — click a mesh
vertex to select it, then edit its elevation in the Z spinbox to the right.
Esc clears.*). Click vertex 33, at the north-east corner where `J1` sits. The
info label changes from **Vertex: (none)** to **Vertex #33 (node)** and the
group reveals:

| Control | Writes | Value here |
| ------- | ------ | ---------- |
| **Z** spin | `[2D_VERTICES]` Z | 12.6 m |
| **Tag** line edit | `[2D_VERTICES]` TAG | empty |
| **Coupled SWMM node** combo | `[2D_VERTEX_NODE_MAP]` NODE — blank means uncoupled | `J1` |
| **Cd:** spin (0.001 – 1.0, default 0.65) | `[2D_VERTEX_NODE_MAP]` CD — scales the orifice-equation exchange | 0.65 |
| **Area:** spin | `[2D_VERTEX_NODE_MAP]` AREA — the orifice-throat area of the exchange, in the mesh's length units squared | 0.5 m² |

**Cd:** and **Area:** appear only while the selected vertices are coupled.
The **Mesh:** combo and the **Z:** hover readout sit in the **Mesh** group to
the left.

**b. The Properties dock.** With a mesh vertex selected, it shows
**Vertex #**, **X**, **Y**, **Elevation (Z)**, **Coupled SWMM node** and
**Tag**.

**c. The Vertices attribute table.** Open the **Attribute Table** dock
(`Ctrl+Alt+4`) and pick the *△ Mesh &lt;name&gt; — Vertices (35)* category.
The columns **Coupled Node**, **Coupling Cd** and **Coupling Area** hold the
map; sort or query on **Coupled Node** to isolate the three coupled rows —
vertex 0 → `OUT1` (area 1.0 m²), vertex 17 → `J2` (0.5 m²) and vertex 33 →
`J1` (0.5 m²).

**d. The mesh style panel.** Its **Coupled Nodes** tab has **Show
SWMM-coupled vertices** with a **Colour:** and **Marker size:**, and its note
names the section it renders — `[2D_VERTEX_NODE_MAP]`. Turn it on and three
markers appear.

\figtodo{t04_vertex_coupling_toolbar.png, The Mesh 2D Vertices group showing vertex 33 coupled to J1 with Cd and Area}

\figtodo{t04_vertices_attribute_table.png, The mesh Vertices table with the three coupled rows}

\figtodo{t04_coupled_nodes_style.png, The mesh with coupled vertices marked}

To build a map like this from scratch, use the **Coupling** group on the same
tab:

- **Auto-couple** — *Couple mesh vertices to coincident SWMM nodes. Applies to
  the selected vertices, or scans the whole mesh when nothing is selected.
  Already-coupled vertices are left unchanged.* It reports
  *Coupled N vertex(es) to coincident SWMM nodes.*
- **Remap 1D↔2D** — clears every existing coupling (with a **Clear & Re-map**
  confirmation) and rebuilds it: nodes that sit on a vertex get vertex
  coupling, nodes merely inside the mesh get cell coupling
  (`[2D_TRIANGLE_NODE_MAP]`, which has no direct editor of its own), and nodes
  outside the mesh are listed as skipped.

The **Generate 2D Mesh** dialog's *Map model nodes to the mesh after
generation (re-runnable from the Mesh toolbar)* checkbox runs the same remap.

### 3. Project-level coupling defaults

**Model → Simulation Options… → 2D Surface Routing** has a **1D ↔ 2D
coupling** group:

| Control | `[2D_OPTIONS]` key | This model |
| ------- | ------------------ | ---------- |
| **Coupling Cd:** | `COUPLING_CD` | 0.65 |
| **Exchange interval:** (s; **Every routing step** at 0) | `COUPLING_SYNC` | not written |
| **Derive exchange areas automatically (COUPLING_AREA AUTO)** | `COUPLING_AREA` | not written |

The per-vertex `CD` and `AREA` columns override these defaults for the
vertices that carry them, which is the case for all three here.

### 4. The retired `[2D_OPTIONS]` keys

The **2D Surface Routing** page will not show you everything that is in
`[2D_OPTIONS]` for this model, because eight of its keys no longer exist.
The section as written is:

```
[2D_OPTIONS]
MAX_TIMESTEP           2
MIN_TIMESTEP           0.001
REL_TOLERANCE          0.0001
ABS_TOLERANCE          1e-06
DRY_DEPTH              1e-05
LIMITER_EPSILON        1e-06
COUPLING_CD            0.65
MAX_KRYLOV_DIM         30
COUPLING_INTERVAL      0
MAX_CVODE_STEPS        500
LINEAR_SOLVER          GMRES
PRECONDITIONER         NONE
REPORT_2D              YES
OUTPUT_FILE            capped_street.2d.h5
```

`MIN_TIMESTEP`, `REL_TOLERANCE`, `ABS_TOLERANCE`, `MAX_KRYLOV_DIM`,
`COUPLING_INTERVAL`, `MAX_CVODE_STEPS`, `LINEAR_SOLVER` and `PRECONDITIONER`
configured the CVODE/ARKODE implicit 2D stack, which has been removed. The
explicit local-inertial marcher is now the only 2D integrator. On **file
load** the engine emits **WARNING 104** per key —
*[2D_OPTIONS] &lt;key&gt; was retired with the CVODE/ARKODE 2D solvers and was
ignored; the explicit local-inertial marcher is the only 2D integrator* — and
carries on, so legacy models still open. Set the same key **programmatically**
and it is a hard error instead.

Look for those eight warnings in the **Message Logs** dock (`Ctrl+Alt+7`) when
you open the model, and in the `Warnings & Errors` section of the Report
Viewer after a run. The marcher settings that *do* exist are `THETA`,
`CFL_NUMBER`, `LTS_TIERS`, `H_MOVE`, `FROUDE_MAX`, `ADVECTION`,
`MAX_TIMESTEP` and `COUPLING_AREA`, and the GUI exposes all of them on the
**2D Surface Routing** page.

Saving the model from SWMMVis rewrites `[2D_OPTIONS]` from the dialog, which
drops the retired keys for good.

\figtodo{t04_retired_key_warnings.png, The Message Logs dock with the WARNING 104 lines for the retired 2D options}

### 5. Run

Make sure **Simulation Options → Models / Processes → Modules → 2D Surface
Routing** is ticked — it gates the whole 2D page — then **Analysis → Execute**
(`Ctrl+R`).

Because the mesh is inline, the pre-flight *2D mesh not found* dialog will not
appear. `[2D_OPTIONS] OUTPUT_FILE` is already `capped_street.2d.h5`, so the
run overwrites the checked-in results file; copy it aside first if you want to
keep the shipped one.

Two initialisation warnings should appear at the top of the report, one for
each capped node:

```
WARNING: 2D-coupled node 'J1' has sur_depth > 0 — surcharge gate uses
invert + full_depth + sur_depth as the spill threshold (z_top). Below
z_top the orifice ramp is closed in both directions; above z_top it opens.
```

If only one appears, the initialisation loop is short-circuiting. Both are
present in the checked-in `capped_street.rpt`.

To work from the shipped results instead of running, load them with
**File → Import → Add SWMM Output** for `capped_street.out` and
**File → Import → Add 2D Results…** for `capped_street.2d.h5`.

### 6. Compare 1D flooding against 2D depth at `J1` and `J2`

This is the comparison the whole example exists for. It needs one plot holding
both a 1D series and a 2D series.

1. Select `J1` on the map and press `Ctrl+T`. In the **Plot Variables** picker
   tick **Overflow (m³/s)** and **Depth (node) (m)** — the picker labels every
   attribute in the run's own units, and this model is `CMS`. Repeat for `J2`.
   They land in the **Comparison Plot**.
2. Activate the cell picker in **Mesh 2D → 2D Results**, click the mesh cell
   containing vertex 33 (the `J1` corner), right-click, and choose **Depth (2D
   cell) (m)**. Do the same for the cell at vertex 17 (`J2`). They land in the
   *same* Comparison Plot window.
3. Use **Show Animation Cursor** (`Ctrl+Shift+C`) on the plot toolbar to tie
   the plot cursor to the map's animation time.

In a working coupled run the node overflow and the surface depth at the same
location rise together, the surface depth lags slightly, and after the peak
the surface depth falls while the node's total lateral inflow gains a small
tail — the drainback.

In the checked-in results the two curves are unrelated: `J1` and `J2` overflow
between roughly 00:15 and 00:25 (peaks of 0.022 and 0.020 CMS, 0.09 and 0.10
hours flooded, maximum ponded depth 1.640 m each), while the plaza fills only
from direct rainfall and its maximum depth of **0.434 m** occurs at
**t ≈ 1:59**, at the very end of the run, in the low south-west corner. The
coupling flux is zero throughout.

\figtodo{t04_comparison_1d_2d.png, Node overflow at J1 and J2 against 2D cell depth at the same locations}

### 7. Watch the surface in the animation

On the **Results** ribbon tab:

1. **Set Style** → **Cell Depth Fill** → tick **Show cell depth fill**, set
   **Attribute:** to **Depth** (the fill styler offers only **Depth** and
   **Elevation**), and fix the **Range:** to **Fixed over run** or a manual
   0 – 0.45 m.
2. **Show Legend**, then set **Speed:** and press **Play**.
3. The **Flow Velocity** tab's **Show velocity vectors** overlays arrows; set
   **Dry depth cutoff:** low. Peak velocity anywhere in this run is
   **0.37 m/s**.

In a working run you would see two spill plumes appear around vertices 33 and
17 at the storm peak and then spread downslope; as the 1D heads fall the
puddle near vertex 17 drains back through the same gate. In the checked-in
results you see only the uniform rainfall filling the block and pooling
against the low corner, because the gates never opened.

\figtodo{t04_animation_ponding.png, The plaza at maximum depth with the ramp and legend}

### 8. Outfall tailwater from the 2D head

`OUT1` is a `FIXED` outfall whose stage sits *below* the mesh bed at vertex 0.
The engine overrides that fixed stage with the 2D head whenever surface water
actually reaches vertex 0 — and only then. A dry-mesh guard keeps the override
suppressed until the depth at that vertex exceeds 0.1 mm, so a permanently dry
mesh whose bed elevation happens to exceed the outfall invert cannot
accidentally raise the tailwater every step.

To check it, plot **Head (m)** at `OUT1` in the Comparison Plot alongside
**Depth (2D cell) (m)** for the cell at vertex 0. In a working run `OUT1`'s
head is flat at the fixed stage until vertex 0 wets, then tracks
`bed + depth`, then releases back to the fixed stage as the corner drains.

In the checked-in report `OUT1`'s maximum HGL is 23.11 m against a `FIXED`
stage of 26.2467 — the outfall never rose to its own stage, let alone above
it, which is another symptom of the datum mismatch described above.

The outfall type itself is never mutated by the override: an outfall entered
as `FIXED` still reads `FIXED` afterwards.

\figtodo{t04_outfall_tailwater.png, Head at OUT1 against 2D depth at the coupled corner vertex}

### 9. Profile through `J1 → J3 → OUT1` with the 2D surface

**Analysis → Plot Profile** (`Ctrl+Shift+T`), click `J1`, then `Ctrl`-click
(`⌘`-click) `OUT1`. The route runs `J1 → C1 → J2 → C2 → J3 → C3 → OUT1`.

In the **Profile Plot** window:

- The **2D Inundation** toolbar button overlays the active 2D results layer's
  water surface on the pipe profile.
- **Display Options…** → **Profile Display Options** → **Display** tab has
  **Show 2D inundation (active 2D results)**, **2D water surface line pen**
  and **2D inundation fill brush** under a *2D overlay* heading, plus
  **Ground line source (Auto = 2D mesh if present, else node rims)** under
  *Ground* — set to **Auto** the ground line is taken from the mesh rather
  than interpolated between node rims, which is what you want here.
- The *Visibility* group toggles **Current HGL line**, **Current HGL fill**,
  **Current EGL**, **Max HGL band**, **Max HGL line** and **Max EGL line**;
  the *Flooding* group sets the flooding glyph's radius, sweep angle and
  colour, which is how spilling nodes are marked on the section.

Press **Play** and watch the pipe HGL and the surface water move together.
With this model's datum mismatch the two lines will be 26 m apart vertically;
set the Y axis range by hand from the axis-range editor if you want both in
one view.

\figtodo{t04_profile_2d_overlay.png, Profile from J1 to OUT1 with the 2D water surface overlaid}

### 10. The coupled mass-balance ledger

**Analysis → Show Mass Balance** (or **Analysis → Report**) opens the
**Report Viewer**. The coupling shows up in three places.

**In the 1D ledger**, `Flow Routing Continuity`. Spill from a coupled node
into the 2D domain is accumulated into **`Flooding Loss`**
(`routing_flooding`), and drainback from the surface into a coupled node is
accumulated into **`External Inflow`** (`routing_external`). Before that
accounting landed, a coupled model with `ALLOW_PONDING NO` showed a zero
flooding row while the 2D side silently absorbed the water.

Checked-in values: `Wet Weather Inflow` 0.014 hectare-m, `External Outflow`
0.013, `Flooding Loss` 0.001, `External Inflow` 0.000, continuity error
**0.003 %**. The runoff ledger reports 30.000 mm of precipitation, 1.636 mm
infiltrated, 27.346 mm of surface runoff and a −0.074 % error.

**In the 2D ledger**, `2D Surface Routing Continuity`, in cubic metres:
`Initial Stored Volume`, `Rainfall Inflow`, `1D -> 2D Spill Inflow`,
`Outfall Inflow`, `Boundary Inflow`, `2D -> 1D Drain Outflow`,
`Outfall Withdrawal`, `Boundary Outflow`, `Evaporation Loss`,
`Infiltration Loss`, `Final Stored Volume`, `Continuity Error (%)`.

Checked-in values: `Rainfall Inflow` **71.829 m³**, both exchange rows
**0.000**, `Final Stored Volume` **50.855 m³**, `Continuity Error (%)`
**29.199**. That error is the water that fell on the plaza, had no outlet, and
was neither stored nor accounted for — the signature of a coupling that never
engaged.

**In the reconciliation block**, `1D <-> 2D Exchange Reconcil.`, which lists
`1D -> 2D Spill`, `2D -> 1D Drain`, `Net 1D -> 2D` and
`Flow Continuity w/ Exchange Internal (%)` — the 1D continuity recomputed with
the exchange treated as an internal transfer rather than a loss and a gain.
A working coupled run should show a small net exchange and a continuity error
well under 1 % on both sides. This block is **absent** from the checked-in
report.

`2D Solver Statistics` — `Internal Steps`, `Face-Kernel Evals`,
`Avg Internal Step (s)`, `Last Internal Step (s)`, active-cell percentages and
per-tier LTS occupancy — is likewise absent here; it is where you look when
you want to know what a CFL change cost you.

\figtodo{t04_mass_balance_coupled.png, The Report Viewer on the 2D Surface Routing Continuity block}

## What to look for

- **Two C3a warnings, not one** — one per capped coupled node. Both are in the
  checked-in report.
- **`Flooding Loss` and `External Inflow` together.** In a working coupled
  run, spill and drainback are visible on the two 1D rows; a zero
  `External Inflow` with non-zero `Flooding Loss` means the gate is one-sided.
- **`1D -> 2D Spill Inflow` and `2D -> 1D Drain Outflow`** in the 2D ledger,
  and the reconciliation block. All three are the coupling's fingerprint. In
  the checked-in artefacts they are zero or missing.
- **The 2D continuity error.** 29 % here. On a healthy coupled run it should
  be a small fraction of a percent, and a large error with zero exchange rows
  means the two domains are not talking.
- **Pipe capacity.** `C2` runs at `Max/Full Flow` 1.64 — well over its nominal
  capacity, because a surcharged 200 mm pipe on a 5 % grade carries more than
  its Manning full-flow value. `C1` reaches exactly 1.00 and `C3`, the 300 mm
  outlet, only 0.45. `J3` never surcharges. That is the intended behaviour:
  the two 200 mm laterals are the bottleneck, not the outfall pipe.
- **Runoff coefficients** of 0.911 and 0.912 on 92 % impervious catchments —
  a useful sanity check that the storm and the subareas are wired correctly.

## Variations

Every variation below is worth running twice — once on the model as shipped,
once after correcting the datum — because on the shipped model most of them
change nothing at all.

### Remove one coupling

Select vertex 17 with the vertex tool and clear its **Coupled SWMM node**
combo (blank means uncoupled). `J2` now behaves like `J3`: it surcharges and
floods entirely in 1D, and nothing appears on the plaza above it. Re-run and
compare `J2`'s flooding volume and the 2D ledger's `1D -> 2D Spill Inflow`
against the base run.

Do it the other way too: couple `J3` to the vertex nearest it with
**Auto-couple** (or by typing into the combo) and watch a third plume appear.

### Change the discharge coefficient

`Cd` scales the orifice-equation exchange. Lower **Cd:** on vertex 33 from
0.65 towards 0.1 and `J1` holds more head before it can shed the same flow, so
it spills later, spills less, and drains back more slowly. Raise it to 1.0 for
the opposite. Change the **Area:** instead — 0.5 m² is the throat area of the
capped inlet — and you scale the same exchange geometrically.

The project-level defaults on **Simulation Options → 2D Surface Routing** only
apply to vertices that do not carry their own `CD`/`AREA` columns, so to test
the global knob you must first clear the per-vertex values.

### Capped versus uncapped

The cap is the surcharge depth. Set `J1`'s and `J2`'s **Surcharge Depth** to 0
in the Properties dock and `z_top` collapses onto the rim: the gate now opens
as soon as the head reaches the manhole rim instead of 0.5 m above it. Spill
starts earlier, peak head is lower, and the surface plume is broader and
shallower. Restore 0.5 m and the physical-surcharge-cap behaviour returns —
between rim and `z_top` the gate stays *closed* even though water has
technically reached the rim.

### Other knobs from the README

| Where | What it does |
| ----- | ------------ |
| `[XSECTIONS]` `C1`, `C2` | smaller pipes ⇒ stronger surcharge ⇒ more spill |
| `[TIMESERIES] STORM` | peak intensity and duration |
| `[2D_VERTICES]` vertex 0 | the bed elevation at `OUT1` — the dry-mesh-guard knob |
| `[OUTFALLS] OUT1` | switch between `FIXED`, `FREE`, `TIDAL` and `TIMESERIES` stage |
| `[2D_VERTEX_NODE_MAP]` | add or remove rows to couple more or fewer nodes |

To refine the mesh, regenerate the vertex grid at 5 m spacing (13 × 9 = 117
vertices, 192 triangles) instead of 10 m. The 10 m cells are coarse for
street-scale flow; they are sufficient for verifying the coupling logic but
are not a physical benchmark for curb hydraulics.

Two limitations of the example are worth remembering: a single rain gage is
applied uniformly to the 2D mesh, and the subcatchment areas were deliberately
made smaller than the block so the 0.24 ha of mesh receiving direct rainfall is
not double-counted; and there is no infiltration on the mesh, so every drop
that lands on it becomes surface runoff.

## Related

- \ref manual_2d_mesh — the mesh tools, cell parameters and 1D–2D coupling
- \ref manual_simulation_options — the 2D Surface Routing page key by key
- \ref manual_hydraulics — nodes, surcharge depth and outfalls
- \ref manual_running — running, warnings and the Report Viewer
- \ref manual_results — 2D results layers, styling and animation
- \ref manual_time_series_plots — the Comparison Plot and the animation cursor
- \ref manual_profile_plots — profile plots and the 2D inundation overlay
- \ref manual_troubleshooting — continuity errors and what they mean
- \ref tutorial_2d_inundation — the previous tutorial
- \ref tutorial_2d_boundaries — edge boundary conditions and edge conveyance
