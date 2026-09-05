@page manual_2d_mesh 19 — 2D Overland Flow: Meshes, Cell Parameters, Boundary Conditions and 1D–2D Coupling

## What you'll do

Build the unstructured triangular mesh the SWMM 6 engine routes surface water
on: prepare a DTM and the vector layers that shape it, generate the mesh (or
import one), edit vertices, edges and cells, assign per-cell roughness, initial
depth and infiltration, set boundary conditions edge by edge, and couple the
mesh to the 1D pipe network. Everything here writes the `[2D_*]` sections of
the `.inp` — inline, or into a sibling `.2dm` file referenced by
`[2D_MESH_FILE]`.

\figtodo{19_2d_overview.png, A generated 2D mesh over a DTM with the 1D network and coupled nodes}

## Where to find it

| What | Where |
| --- | --- |
| **Generate Mesh** | **Model → Generate Mesh**; ribbon **Model** and **Mesh 2D** tabs |
| **Add 2D Mesh…** (import a `.2dm`) | **File → Import → Add 2D Mesh…**; ribbon **Home** tab |
| Mesh picker, vertex / edge / cell editors | The ribbon **Mesh 2D** tab (the Mesh Editing toolbar) |
| **Select Vertices** / **Select Edges** | **Model → Mesh →**, and the **Mesh 2D** tab |
| **From Raster…** / **From Shapefile…** (Cell Data) | Ribbon **Mesh 2D** tab |
| **Assign Infiltration to Selection…** | **Model → Mesh →**, and the **Mesh 2D** tab while cells are selected |
| **Aquifer…** / **Initial Conditions…** (2D groundwater preview) | Ribbon **Mesh 2D** tab |
| Active terrain raster, vertical unit, invert offsets | The Terrain toolbar |
| **Terrain Profile** | **Model → Terrain Profile**; ribbon **Model** tab |
| Which mesh the engine reads; 2D solver options | **Model → Simulation Options…** → *Mesh* and *2D Surface Routing* |

The **Mesh 2D** ribbon tab and its actions are contextual — they appear when
the project has a 2D mesh. None of these commands carry a default shortcut.

\videotodo{From a DTM raster to a generated coupled mesh with boundary conditions}

## Step-by-step

### Workflow overview

1. Load the DTM raster and any vector layers that should shape the mesh —
   boundary polygon, breaklines, buildings, structures (\ref manual_layers).
2. Check the project CRS and the DTM's vertical unit (\ref manual_crs).
3. **Model → Generate Mesh** — set the domain, quality, minimum cell size and
   the initial cell values, then **Generate**.
4. Inspect and correct the mesh on the **Mesh 2D** tab: vertex elevations, cell
   parameters, edge boundary conditions.
5. Assign spatially varying cell data from a raster or a shapefile.
6. Couple the mesh to the 1D network — **Auto-couple** or **Remap 1D↔2D**.
7. Set the 2D solver options and turn on 2D reporting
   (\ref manual_simulation_options), then run (\ref manual_running).

### Preparing the terrain

The mesh takes its elevations from a DTM raster added like any other raster
layer (\ref manual_layers). Two things must be right before you generate:

- **Horizontal CRS.** The mesh is built in the model's CRS. When the raster's
  CRS differs, the pipeline builds a mesh→DTM transform and reprojects the
  sampled points, so a mismatch is handled — but a *missing* raster CRS is not.
- **Vertical unit.** The **Generate 2D Mesh** dialog reports the DTM vertical
  unit it detected from the raster's CRS metadata and derives a **Z conversion
  (×)** factor from it and the chosen mesh vertical unit. Check both; a
  metres-versus-feet mistake is invisible in the mesh and fatal in the run.

The **Terrain toolbar** carries the active terrain raster used for interactive
editing, in three groups:

\figtodo{19_terrain_toolbar.png, The Terrain toolbar with the active raster; vertical unit and invert offsets}

| Group | Control | What it does |
| --- | --- | --- |
| **Active Terrain** | Raster combo | The raster used for Z sampling and invert estimation; **(none)** disables it |
| **Vertical Units** | **DEM** combo | The DEM's vertical unit — **m (metres)** or **ft (feet)** |
| | **×** factor | Multiplier applied to raw DEM values to reach model vertical units; derived from the two units, editable |
| **Invert Offsets** | **Node Δ** | Signed offset added to the converted DEM elevation for a new node's invert |
| | **Link Δ** | Signed offset applied at each link endpoint, used to estimate conduit slope |
| **Profile** | **Terrain Profile** | Trace a polyline and plot the terrain elevation along it — see \ref manual_profile_plots |

### The Generate 2D Mesh dialog

**Model → Generate Mesh** opens a tabbed dialog — **Sources**, **Quality** and
**Hydraulics** — with the output destination in a fixed footer. Generation runs
on a worker thread with a progress bar and a live stage label; **Cancel
Generation** stops it cleanly at the next stage boundary.

\figtodo{19_generate_mesh_sources.png, The Generate 2D Mesh dialog on the Sources tab}

**Sources tab — Sources group**

| Control | What it does |
| --- | --- |
| **DTM raster** | The raster sampled for elevations; **(none)** falls back to interpolating from junction rim elevations |
| **DTM vertical unit** | Read-only, auto-detected from the raster CRS |
| **Mesh vertical unit** | **Match flow units (auto-convert)**, **Metres (m)** or **Feet (ft)** |
| **Z conversion (×)** | Multiplier applied to every raw DTM Z before it is written to the mesh; recomputed from the two units above, editable |
| **Domain** | Read-only summary of the meshing domain (the model extent when no boundary layer is chosen) |

**Auxiliary feature layers (optional)**

| Control | What it does |
| --- | --- |
| **Boundary polygon** | The polygon layer whose features define the meshing boundary. Interior rings become holes that Triangle leaves unmeshed. **(none)** falls back to the SWMM model bounding rectangle plus 5%; **Use SWMM subcatchment polygons** dissolves the subcatchments into the boundary |
| **Constraining points** | A checkable list of point layers; every feature becomes a Steiner point. Layers with 3D geometry also offer **use feature Z** |
| **Constraining lines** | A checkable list of line layers; every feature becomes a constraint segment, so the mesh has edges along it. Breaklines, walls, kerbs and road centrelines belong here |

**1D geometry influence (optional)** — these shape the PSLG only; the 1D–2D
coupling itself is authored after generation.

| Control | What it does |
| --- | --- |
| **Junctions / outfalls / storage → Steiner vertices (tag = node id)** | Pins a mesh vertex at every node. **Off by default** — forcing a vertex at every node distorts the mesh around close node clusters such as weir and orifice endpoints |
| **Conduits → constraint segments (marker = conduit id)** | On by default; mesh edges follow the pipe alignments |
| **Subcatchments → triangle regions (tag = subcatchment id)** | On by default; each subcatchment becomes a tagged mesh region, which is what gives the region-defaults table its rows |
| **Use node rim elevation (invert + max depth) instead of terrain** | Nodes take their rim elevation rather than the sampled terrain |
| **Flatten terrain within radius** | Forces every terrain or refinement vertex within this radius of a node to that node's rim elevation, removing the slivers that terrain/rim misalignment creates. **(off)** at 0 |
| **Enforce minimum node separation** | A node candidate closer than this to an already-kept node is not pinned as a vertex; it stays in the coupling list and the post-generation mapper couples it to its containing cell instead |

**1D ↔ 2D coupling**

| Control | What it does |
| --- | --- |
| **Map model nodes to the mesh after generation** | Runs the node→mesh mapper once Triangle finishes. Independent of the junctions-as-Steiner box, and re-runnable later from the toolbar's **Remap 1D↔2D** |

**Elevation interpolation (no DTM)** — the whole group is disabled while a DTM
is selected.

| Control | What it does |
| --- | --- |
| **Method** | **Inverse distance weighting (IDW)** or **Natural neighbour** |
| **NN variant** | **Sibson (area-stealing)** or **Laplace (edge-ratio)**; natural neighbour falls back to IDW outside the seed convex hull |
| **IDW power** | Shepard exponent |

\figtodo{19_generate_mesh_quality.png, The Quality tab with the Triangle quality and minimum cell size groups}

**Quality tab — Triangle quality**

| Control | What it does |
| --- | --- |
| **Max triangle area** | Upper bound on triangle area in project length units squared; **(no cap)** at 0 |
| **Min angle** | Minimum triangle angle. 0–33° is reliable; above 33° refinement may not terminate |
| **Size gradation** | Replaces the uniform area cap with a graded size field: the cap is kept *at* the constrained features and the permitted area grows with distance at this Lipschitz slope. Strictly fewer cells with smooth transitions. **(uniform)** at 0; needs a non-zero max area |
| **Max Steiner points** | Cap on the points Triangle may insert; **(unlimited)** at 0 |
| **Allow Steiner refinement on boundary** | Lets Triangle split boundary segments |

**PSLG Optimisation**

| Control | What it does |
| --- | --- |
| **Geometry simplification ε** | Ramer–Douglas–Peucker tolerance applied to every polygon ring and polyline before it enters Triangle; **(off)** at 0 |
| **Steiner snap radius** | Near-coincident Steiner points from different sources within this distance are merged to one; **(off)** at 0 |
| **Max boundary edge length** | Splits domain and hole ring edges longer than this into equal parts after simplification — pure vertex insertion; **(off)** at 0 |

**Minimum Cell Size** — Ruppert refinement emits cells at the scale the *input*
demands, so a minimum cell size means conditioning the input geometry. On the
explicit 2D marcher a single sliver sets the timestep for the whole domain, so
this group is usually worth the small geometry change it costs.

| Control | What it does |
| --- | --- |
| **Minimum cell size** | The target minimum feature size *h* in project length units; **(off)** at 0, which reproduces an unconditioned mesh exactly |
| **Suggest** | Fills the field with one third of the side of the equilateral triangle matching the **Max triangle area** |
| **Enforce — may move or merge SWMM coupling points** | Off (advisory): tagged nodes and conduit endpoints never move or merge — on real models nearly every crowded vertex *is* such an identity, so the minimum is rarely achieved. On (enforce): identities closer than *h* may merge, crowded nodes are not pinned as vertices, and the cleanup may absorb slivers into an identity vertex. **No coupling is ever lost** — a merged or demoted node couples through its containing cell instead, and the log says which |
| **Trim corners sharper than** | Corner-trim threshold in degrees; **(off)** at 0 |
| **Also trim corners at SWMM nodes** | Extends corner trimming to node-tagged corners |
| **Drop holes smaller than one cell** | Removes sub-scale hole rings that could only produce slivers |
| **Collapse leftover slivers after meshing** | Post-Triangle sliver collapse; on by default |

Under the group a read-only line reports what the current *h* implies: the
refinement floor per cell, the weld radius within which vertices merge, and the
statement that no vertex moves further than that radius. It also warns when the
minimum angle is above 28°, because a high angle bound multiplies cells around
sharp features.

**Terrain-Adaptive Thinning** — decimates the DTM grid to the points that
actually carry terrain shape, by iterative normal-deviation scoring.

| Control | What it does |
| --- | --- |
| **Enable normal-deviation terrain simplification** | Turns thinning on |
| **Normal dot threshold** | Above this score the neighbourhood is smooth and the vertex is dropped; below it the vertex is a terrain feature and is kept |
| **Thinning passes** | Iteration count; **(unlimited)** at 0 |
| **Max thinning points** | Cap on retained terrain points; **(unlimited)** at 0 |
| **Min point spacing (Poisson-disk)** | Poisson-disk filter over the retained points; **(auto)** at 0 |
| **Boundary buffer** | Terrain candidates outside the domain, inside a hole, or closer than this to any constrained segment or mandatory Steiner vertex are dropped so they cannot force boundary slivers; **(auto)** at 0 uses half the effective terrain point spacing |

\figtodo{19_generate_mesh_hydraulics.png, The Hydraulics tab with the initial cell values and the region defaults table}

**Hydraulics tab — Initial cell values**

| Control | What it does | Writes |
| --- | --- | --- |
| **Roughness (Manning's n)** | Written to every generated cell | `[2D_TRIANGLES]` MANNINGS_N |
| **Initial depth** | Standing water depth at the start of the run; 0 starts the cell dry | `[2D_TRIANGLES]` INIT_DEPTH |

**Region defaults** — one row per region tag the mesh will carry (subcatchment
tags, when **Subcatchments → triangle regions** is on), plus a `*` row that is
always present and always first. Columns: **Region**, **Manning's n**,
**Initial depth**, **Method**, **P1 … P5** and **Destination**.

- The `*` row's **Manning's n** and **Initial depth** cells are a read-only
  mirror of the two spin boxes above; a project where you never touch the table
  produces exactly the mesh it would have without it.
- **Blank means inherit.** A region row shows the inherited `*` value in italics
  until you type into it, and only rows you actually filled in are stamped onto
  their cells.
- **Method** is the per-region infiltration model: **None**, **Horton**,
  **Modified Horton**, **Green-Ampt**, **Modified Green-Ampt**, **Curve
  Number** or **Constant**. The P1…P5 headers retitle themselves from the
  method, and parameter cells the method does not use render `—` and refuse
  edits. Parameters are in **project units** — the same numbers you would type
  into `[INFILTRATION]`.
- **Destination** is the fate of infiltrated water. Only **Lost** is accepted by
  the engine in this release; the others are shown disabled.
- Infiltration follows the engine's inheritance rule — *per-cell override →
  region tag row → `*` row → none* — so region rows go to
  `[2D_INFILTRATION_DEFAULTS]` verbatim and are never flattened into per-cell
  rows. A table where every row is **None** emits no `[2D_INFILTRATION*]`
  section at all.
- A row that names a method but leaves one of that method's parameters blank is
  rejected with a message naming the region.

**Output (footer)**

| Control | What it does |
| --- | --- |
| **External .2dm** | Writes a standalone `.2dm` referenced by `[2D_MESH_FILE]` in the `.inp` |
| **Inline in .inp** | Embeds `[2D_VERTICES]`, `[2D_TRIANGLES]` and the rest directly in the `.inp` |
| **Mesh file** / **Browse…** | Destination path; defaults to `<project>.2dm` |

The generated file carries `;; SOURCE_CRS:` and `;; UNITS:` header lines so it
is self-describing. Freshly generated meshes are renumbered along a Hilbert
curve of their triangle centroids — the engine's cell index is the file line
order, so spatial neighbours being index-adjacent measurably helps both the
solver's cache behaviour and the map renderer. It is a pure permutation: the
geometry and every per-element attribute are unchanged.

### Importing an existing mesh

**File → Import → 2D Mesh** loads an OpenSWMM `.2dm` file and attaches it to the
active project as a mesh layer. The file dialog offers `*.2dm` (and *All
Files*); this build reads no other mesh format — convert externally if your
mesh comes from another tool. The import runs asynchronously with the progress
bar; the outcome is reported in the **Message Logs** panel.

The *Mesh* page of **Model → Simulation Options…** is where you choose which
mesh the engine actually reads when a project has several — see
\ref manual_simulation_options.

### The Mesh 2D ribbon tab

\figtodo{19_mesh2d_ribbon.png, The Mesh 2D ribbon tab with the Mesh; Vertices; Edges; 2D Results; Profile and Coupling groups}

The Mesh Editing toolbar is organised into captioned groups. Clusters appear
and disappear with the selection, so the tab shows the editors that apply right
now.

**Mesh group**

| Control | What it does |
| --- | --- |
| **Mesh** combo | Which mesh layer is active — this is the layer every other control on the tab acts on |
| **Z:** hover readout | Elevation under the cursor, interpolated linearly across the triangle it is inside; blank off-mesh. It is also mirrored into the status-bar coordinate readout |

**Vertices group**

| Control | What it does | Writes |
| --- | --- | --- |
| **Select Vertex** (toggle) | Click a vertex to select it; **Esc** clears | — |
| Vertex info label | The selected vertex index, its tag and its coupled node | — |
| **Z** spin | Elevation of the selected vertex in project vertical units | `[2D_VERTICES]` Z |
| Tag field | Descriptive vertex tag | `[2D_VERTICES]` TAG |
| Coupled node combo | The SWMM node this vertex couples to; blank = uncoupled | `[2D_VERTEX_NODE_MAP]` |
| **Cd** spin | Discharge coefficient for the selected coupled vertices | `[2D_VERTEX_NODE_MAP]` CD |
| **Area** spin | Exchange area (mesh length units squared) for the selected coupled vertices | `[2D_VERTEX_NODE_MAP]` AREA |

The Z spin coalesces edits — it applies on Enter, focus-out or an arrow step, so
a mesh recompute is not triggered per keystroke.

**Edges group**

| Control | What it does |
| --- | --- |
| **Select Edge** (toggle) | Click a boundary edge, or drag a box to select several. **Ctrl/⌘-click two boundary edges to add the whole run between them** — the shortest chain by geometric length. **Esc** clears |
| Edge info label | The selected edge, its tag, or the count and how many of them are boundary edges |
| **BC type** combo | The boundary condition; changes apply immediately to every selected edge |
| Parameter page | Follows the BC type (see the table below) |
| **Conveyance** spin | Per-edge flux attenuation in [0, 1]; 1.0 is unrestricted. Applies to **every** edge, interior and boundary, and is mirrored onto the neighbouring triangle's matching slot so mass conservation holds |

**Coupling group** — **Auto-couple** and **Remap 1D↔2D**, described below.

**2D Results and Profile groups** — **Pick 2D Cells** and the mesh profile
tracer, plus the per-cell editors that appear while cells are selected. See
\ref manual_analysis_tools and \ref manual_profile_plots.

### Cell parameters

Select cells with **Pick 2D Cells** — single-click selects (Shift adds, Ctrl
toggles), drag a box or press **L** to lasso, **Esc** clears — then use the cell
editor that appears beside the cell info label.

\figtodo{19_cell_editor.png, The per-cell parameter editor on the Mesh 2D tab with cells selected}

| Control | What it does |
| --- | --- |
| Parameter combo | Which per-cell attribute to prescribe |
| Value editor | A spin box, or a combo when the parameter is an enumeration (the infiltration method) |
| Cell tag field | Descriptive triangle tag (`[2D_TRIANGLES]` TAG) |
| **Assign Infiltration to Selection…** | The whole-row infiltration form for the selected cells — method, parameters and destination in one undo entry, applied to the cells or to the region tag they share. Enabled only while cells are selected |

The editable per-cell parameters are:

| Parameter | Meaning | Writes |
| --- | --- | --- |
| **Manning's n** | Surface roughness | `[2D_TRIANGLES]` MANNINGS_N |
| **Initial Depth** | Standing water depth at t = 0; 0 starts the cell dry | `[2D_TRIANGLES]` INIT_DEPTH |
| **Infiltration Method** | Per-cell infiltration model; inherited from the cell's region tag until it is set here | `[2D_INFILTRATION]` METHOD |
| The method's parameters | Positional, in project units; a parameter the resolved method does not use reads `—` and refuses edits | `[2D_INFILTRATION]` P1…P5 |

Values read back **resolved**, not raw: a cell with no override reads its region
tag's numbers (or the `*` row's), which is what the engine will run. Editing one
materialises a per-cell override; undoing that edit puts the cell back to
*inheriting*, not to a materialised override carrying the same numbers.

A further group of **2D groundwater** parameters — **Saturated Conductivity
(Ks)**, **Aquifer Thickness (zs)**, **Porosity (theta_s)**, **Initial
Unsaturated Depth (hu)** and **Initial Saturated Depth (hg)** — appears greyed
in every selector. The engine kernel is not implemented, and assignment refuses
them.

Every cell edit goes onto the project undo stack and refreshes the map, the
Properties panel and the attribute table through the layer's change signal, so
no view can drift.

### Assigning cell data from a raster or a shapefile

**From Raster…** and **From Shapefile…** on the **Mesh 2D** tab open the
**Assign 2D Cell Data** dialog. Where the toolbar's cell editor prescribes one
value to a hand-picked selection, this prescribes a spatially varying field to
the whole mesh or the current selection, in one undoable step.

\figtodo{19_assign_cell_data.png, The Assign 2D Cell Data dialog in classified infiltration lookup mode}

**Mapping mode**

| Mode | What it does |
| --- | --- |
| **Single numeric target** | One band or field → one cell parameter |
| **Multiple numeric targets** | N bands or fields → N parameters in one pass (Horton's f0, fmin and decay together), still one undo entry |
| **Classified infiltration lookup** | A categorical key field (land-use code, hydrologic soil group), optionally a second key, mapped through an editable lookup table to a complete infiltration row. Curve Number by land use × hydrologic soil group is the canonical two-key case |

**Source** — **Raster** (with **Band**, **Scale** and **Offset**) or **Vector
layer** (with **Layer**, **Field** and **Use selected features only**).

**Sampling** is orthogonal to the mode:

| Sampling | What it does |
| --- | --- |
| **Cell centroid (point sample)** | One sample at the cell centroid |
| **Overlay — automatic (by source type)** | Majority for a categorical source, area-weighted mean for a continuous one |
| **Overlay — area-weighted mean** | Explicit override |
| **Overlay — majority (largest share)** | Explicit override |
| **Natural neighbour** | For scattered point sources rather than coverages; **Sibson** or **Laplace** variant |

Offering the wrong overlay statistic for a source type produces plausible
nonsense, which is why the automatic choice is bound to the source's declared
type and the override is explicit.

**Write as** (infiltration modes) — **Per-cell overrides** writes one
`[2D_INFILTRATION]` row per cell, correct when the source really does vary
cell by cell; **Region defaults (by tag)** writes one
`[2D_INFILTRATION_DEFAULTS]` row per source key, preserving tag inheritance. A
**Leave cells that already inherit these values unchanged** option keeps
inheriting cells inheriting.

**Apply to** — **All cells** or **Selected cells** (the count is shown).

The lookup table has **Add Row**, **Remove Row**, **Load CSV…** and **Save
CSV…**, so an agency standard table can be reused across projects. A trailing
`(unmatched)` row applies to every key the table does not list.

**Preview** samples without writing and reports what it found; **Apply** writes.
Sampling runs on a worker thread and the mesh is untouched until it finishes
successfully, so cancelling mid-run leaves the mesh exactly as it was.

### Boundary conditions

Select boundary edges with **Select Edge**, then choose a type. Boundary edges
you never touch default to `WALL` (zero flux).

\figtodo{19_edge_bc.png, Assigning a boundary condition to a selected run of boundary edges}

| Type | Parameter | Writes (`[2D_BOUNDARY_CONDITIONS]` TYPE) |
| --- | --- | --- |
| **Wall** | none | `WALL` |
| **Normal Flow** | **Slope:** bed slope, dimensionless — must be > 0; the engine treats 0 as a wall | `NORMAL_FLOW` |
| **Specified Stage (Constant)** | **Stage:** water-surface elevation in project vertical units | `SPECIFIED_STAGE` |
| **Specified Stage (Timeseries)** | **TS:** a time series name | `TS_STAGE` |
| **Specified Flow (Constant)** | **Flow/m:** discharge per metre of edge, outward-positive | `SPECIFIED_FLOW` |
| **Specified Flow (Timeseries)** | **TS:** a time series name | `TS_FLOW` |
| **Rating Curve** | **Curve:** a curve name | `RATING_CURVE` |

The time-series and curve fields are editable combos over the project's data
objects; pick an existing one, or type a name and press the browse button to
create or edit it in the matching editor (\ref manual_data_objects). Edits apply
immediately to every selected edge — there is no separate Apply.

Edges can also carry a **group** name, which the `.inp` writes in the GROUP
column so a run of edges reads as one named boundary.

**Edge conveyance** (`[2D_EDGE_CONVEYANCE]`) is separate from the BC type and
applies to interior edges too. It is the way to model a leaky berm, a kerb or a
partially blocking wall: set the conveyance of the edges along the feature to
the fraction of the flux the geometry would otherwise allow. The engine mirrors
each row onto the neighbouring triangle's matching slot so antisymmetry — and
therefore mass conservation — is preserved.

### 1D–2D coupling

Coupling is authored *after* the mesh exists and can be redone at any time. Two
mechanisms exist, and both write the same two sections:

- `[2D_VERTEX_NODE_MAP]` — a mesh **vertex** exchanges with a SWMM node. This is
  what a node sitting exactly on a mesh vertex gets.
- `[2D_TRIANGLE_NODE_MAP]` — a mesh **cell** exchanges with a SWMM node, using
  the engine's orifice exchange law. Several nodes may share one cell (a weir or
  orifice pair, for instance).

Both carry a **CD** (discharge coefficient) and an **AREA** (exchange area). The
area is in the *mesh's* length units squared — on a US-units project without an
SI mesh tag, 2.0 means 2.0 ft², not 2.0 m². `[2D_OPTIONS] COUPLING_AREA AUTO`
overrides authored areas from the connected conduit area either way; set it on
the *2D Surface Routing* page of \ref manual_simulation_options.

\figtodo{19_coupling.png, Coupled vertices and cells highlighted with their SWMM node ids}

**Auto-couple** pairs mesh vertices with the SWMM node at the same map
coordinate, within a scale-free tolerance of 1e-6 × the mesh bounding-box
diagonal. It acts on the selected vertices, or scans the whole mesh when nothing
is selected, and never changes an already-coupled vertex. A summary reports how
many were coupled, how many were already coupled, and how many nodes had no
coincident vertex.

**Remap 1D↔2D** is the superset and the answer to *"the mesh changed, now
what?"*. It clears and re-authors the whole mapping: coincident nodes get vertex
coupling, non-coincident nodes inside the mesh get cell coupling, and nodes
outside the mesh are reported by name. It confirms first, because it discards
the existing mapping. This is what decouples the 1D–2D relationship from mesh
generation: regenerate the mesh, remap, and the coupling is correct again
without rebuilding the model.

Individual couplings can always be typed by hand: select a vertex, pick the node
in the coupled-node combo, and set its Cd and area. Blanking the combo removes
the coupling.

On save, the GUI's edits — vertex Z, per-edge conveyance, per-edge boundary
conditions — are pushed back into the live engine's in-memory 2D mesh before the
deck is serialised. The engine, not the GUI mesh model, is the source of truth
on save: it round-trips coupling maps, Manning's n, units and options that the
lightweight GUI model does not retain, so syncing only the editable fields back
into it is lossless for everything you did not touch.

### Rainfall on the mesh

Rain reaches the 2D surface according to `[2D_OPTIONS] RAINFALL_MODE`, set on
the *2D Surface Routing* page of **Model → Simulation Options…**:

| Mode | What it does |
| --- | --- |
| **Natural neighbour (all gages)** | Spatially interpolates every located rain gage onto each cell, falling back to inverse-distance outside the gage hull |
| **System (uniform gage mean)** | Applies one uniform value — the mean of all gages |
| **None (no direct rainfall)** | No direct rainfall on the mesh; the surface only receives water through coupling and boundaries |

Rain gages are placed and edited like any other model object
(\ref manual_hydrology); the 2D module reads their locations, so there is no
per-cell gage assignment to make. To check what a run actually applied, select
cells with **Pick 2D Cells** and right-click: the attribute menu offers
**Rainfall** (intensity) and **Rain Volume** (cumulative), alongside depth, HGL
and velocity, and plots the chosen series for the selected cells. Right-click
also works on selected edges and vertices with their own attribute lists.
Rainfall series require a run whose 2D output carries the per-cell rainfall
datasets — attributes the open results do not carry are greyed in the menu.

### 2D groundwater (preview)

**Aquifer…** and **Initial Conditions…** on the **Mesh 2D** tab open the
**2D Groundwater (Preview)** dialog. **Every input is disabled** and a banner
says why: the two-zone groundwater kernel is not in the engine yet. The dialog
exists so the planned parameter surface is visible.

\figtodo{19_gw2d_preview.png, The 2D Groundwater preview dialog with its disabled inputs and banner}

The **Aquifer Properties** page lays out **Saturated conductivity (Ks)**,
**Aquifer thickness (zs)**, **Porosity (theta_s)**, a **Soil model** with its
model-specific parameters, a **Closure** mode and a **Kinematic layers** count
used by the kinematic closure. **Initial Conditions** holds **Initial
unsaturated depth (hu)** and **Initial saturated depth (hg)**. An **Apply to**
scope combo offers **All cells**, **Selected cells** and **Cells with tag…**.

Nothing here writes to the model.

### Styling the mesh

A mesh layer's appearance is edited in its style panel, which has one tab per
sublayer: **Terrain Fill**, **Elevation Bands**, **Elevation Isolines**, **Mesh
Edges**, **Mesh Vertices**, **Boundary Conditions** and **Coupled Nodes**.
Right-clicking a sublayer in the Layers panel and choosing **Edit Sublayer
Style…** opens the matching tab directly. See \ref manual_styling.

\figtodo{19_mesh_style_panel.png, The 2D mesh style panel showing the terrain fill and isoline tabs}

### Saving

| Choice | What the `.inp` carries |
| --- | --- |
| **Inline in .inp** | `[2D_VERTICES]`, `[2D_TRIANGLES]`, `[2D_VERTEX_NODE_MAP]`, `[2D_TRIANGLE_NODE_MAP]`, `[2D_BOUNDARY_CONDITIONS]`, `[2D_EDGE_CONVEYANCE]` and the `[2D_INFILTRATION*]` family, written directly into the deck |
| **External .2dm** | The same sections in a sibling `.2dm`, referenced by `[2D_MESH_FILE] FILE <path>` |

The engine also reads `[2D_INITIAL_VELOCITY]`, `[2D_INITIAL_QUALITY]` and
`[2D_BOUNDARY_QUALITY]`. SWMMVis has no editor for those three — it round-trips
them unchanged, so hand-edited values survive a save.

Layer-level state that is not part of the SWMM model — styling, sublayer
visibility, panel layouts, the active mesh layer — lives in the project's
`.oswp` sidecar beside the `.inp`. See \ref manual_projects.

## Tips and gotchas

- **Set the vertical unit before you generate.** A metres/feet mistake produces
  a perfectly valid mesh at the wrong scale and is very hard to spot afterwards.
- **Constrain, don't just refine.** A breakline as a constraint line puts mesh
  edges *on* the feature. Refining everywhere until the feature happens to be
  resolved costs orders of magnitude more cells and still misses the crest.
- **Use size gradation instead of a smaller area cap.** It keeps the resolution
  at the features and coarsens away from them, always producing fewer cells than
  the uniform cap, never more.
- **One sliver sets the timestep for the whole domain.** The explicit marcher is
  CFL-limited on the smallest cell. If a run is inexplicably slow, set a
  minimum cell size and regenerate.
- **Advisory minimum cell size is often inert.** On a real SWMM model almost
  every crowded vertex is a coupling identity that advisory mode may not touch.
  Turn on **Enforce** when you actually need the floor — no coupling is lost,
  the affected nodes just couple through their containing cell.
- **A min angle above 28° is rarely worth it.** It multiplies cells around sharp
  input angles for no practical benefit; 26–28° with a minimum cell size is the
  better pairing.
- **Junctions as Steiner vertices is off for a reason.** Pinning a vertex at
  every node distorts the mesh where nodes cluster. Let the post-generation
  mapper author the coupling instead.
- **Regenerating the mesh invalidates the coupling.** Run **Remap 1D↔2D**
  afterwards; it reports every node it could not place.
- **`NORMAL_FLOW` with slope 0 is a wall.** The engine needs a positive bed
  slope for that BC to pass any water.
- **Conveyance is not a boundary condition.** Use it on interior edges to model
  berms, kerbs and partial blockages; the BC type still applies independently on
  boundary edges.
- **Preview before you apply cell data.** The Preview pass tells you how many
  cells matched and what the sampling produced, and nothing is written until you
  press Apply.
- **Large meshes:** prefer an external `.2dm` over inline (it keeps the `.inp`
  readable and lets several decks share one mesh); leave the Hilbert reordering
  alone, since it is what keeps the renderer and the solver cache-friendly; turn
  off elevation isolines and vertex rendering while editing; and see
  \ref manual_performance for redraw policy and solver threading.

## Related

- \ref manual_layers — adding the DTM raster and the vector layers that shape the mesh
- \ref manual_crs — the project CRS the mesh is built in
- \ref manual_styling — the mesh style panel and its sublayers
- \ref manual_selection — selection behaviour shared with the mesh tools
- \ref manual_hydrology — rain gages and subcatchments
- \ref manual_hydraulics — the 1D network the mesh couples to
- \ref manual_data_objects — the time series and curves boundary conditions reference
- \ref manual_water_quality — 2D initial and boundary quality sections
- \ref manual_simulation_options — the *Mesh* and *2D Surface Routing* pages
- \ref manual_running — running a coupled simulation
- \ref manual_results — 2D results layers and animation
- \ref manual_profile_plots — mesh and terrain profiles
- \ref manual_analysis_tools — 2D cell time series and the rainfall visualisation
- \ref manual_performance — large meshes and redraw policy
- \ref tutorial_2d_inundation — from a DTM raster to an animated inundation map
- \ref tutorial_1d2d_coupling — capped street inlets coupled to a 2D surface
- \ref tutorial_2d_boundaries — edge boundary conditions and edge conveyance
- \ref tutorial_pure_2d — a stand-alone 2D model with system rainfall
