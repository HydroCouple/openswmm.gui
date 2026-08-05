@page manual_mesh_attribute_tables 09 — 2D Mesh Attribute Tables

## What you'll do

List and edit a 2D mesh's **vertices**, **edges** and **cells** as tables, so
you can query them, sort them, bulk-edit a column, and move between the table
and the map in either direction.

## Where to find it

The **Attribute Table** dock. Its *Category* combo lists the SWMM object
categories first, then any loaded CSV/TSV tables and GIS feature layers, and
finally three entries per loaded 2D mesh:

```
△ Mesh <name> — Vertices (12 843)
△ Mesh <name> — Edges    (37 512)
△ Mesh <name> — Cells    (24 906)
```

Right-clicking a mesh layer in the layer tree and choosing **Open Attribute
Table** jumps straight to its Vertices table.

On a large mesh opened progressively, the three entries appear immediately but
stay greyed until the background load finishes — the edge pairing and the
boundary flags are part of that load. They enable themselves when it lands.

## What each table shows

| Table | Read-only columns | Editable columns |
|-------|-------------------|------------------|
| **Vertices** | Index, X, Y, Marker | Elevation, Tag, Coupled Node, Coupling Cd, Coupling Area |
| **Edges** | Edge (`triangle:edge`), Boundary, Length | Conveyance; and on boundary edges: BC Type, Stage, Bed Slope, Flow, Time Series, Rating Curve, Group |
| **Cells** | Index, Area, Centroid X, Centroid Y | Tag, plus one column per per-cell parameter (Manning's n, Initial Depth, …) |

Coordinates are **not** editable here — move a vertex on the map instead. The
same goes for the other derived columns (edge length, cell area, centroid): they
are computed from the geometry, so editing them would have nowhere to write.

A cell showing **—** does not apply to that row:

- the boundary-condition columns on an **interior** edge (a BC only means
  something where the mesh ends);
- the BC parameters a row's **current type doesn't read** — see below;
- Coupling Cd / Coupling Area on an **uncoupled** vertex — they appear the
  moment you give the vertex a coupled node;
- per-cell parameters that are still waiting on engine support (the 2D
  groundwater set), which are listed so the roadmap is visible.

**Conveyance** is the exception among the edge columns: it applies to interior
edges too, and writing it mirrors the value onto the other half of the edge —
the engine requires both halves to agree.

### References point at objects that exist

Three columns name something else in the model, and each is a **closed
picker** — a dropdown of what is actually defined, plus a "…" button onto that
family's editor. You cannot type a name into them, so a mesh can never cite an
object that isn't there:

| Column | Offers |
|---|---|
| Vertices → **Coupled Node** | every SWMM node; blank removes the coupling |
| Edges → **Time Series** | the project's time series ("…" opens the Time Series editor) |
| Edges → **Rating Curve** | the project's curves ("…" opens the Curve editor) |

Open a mesh without its model and these fall back to plain text — there is
nothing to pick from, and an empty dropdown would just be a dead cell.

### One boundary condition, one parameter

Each BC type reads exactly one parameter. Set **BC Type** first; the column it
reads becomes editable and the rest show **—**:

| BC Type | Live parameter |
|---|---|
| Wall | *(none)* |
| Normal Flow | Bed Slope |
| Specified Stage (constant) | Stage |
| Specified Stage (time series) | Time Series |
| Specified Flow (constant) | Flow |
| Specified Flow (time series) | Time Series |
| Rating Curve | Rating Curve |

**Group** is independent of the type and stays available on any boundary edge.
This mirrors the mesh toolbar, which shows one parameter widget per type and
hides the others, and it means a boundary can't be saved carrying a value the
engine will never read. A value you typed under a previous type is kept but
hidden, so flipping back brings it into view again — and only the live
parameter is ever written to the `.inp`.

### One row per edge

The mesh stores each edge against a triangle corner, so an interior edge is
held twice — once from each side. The Edges table lists it **once**, under the
lower of the two slots. Selecting that edge on the map, from either triangle,
highlights the same single row.

## Editing

Edits behave exactly like edits made on the mesh-editing toolbar, because they
go through the same write path:

- **Ctrl+Z undoes them**, on the same stack as map edits.
- A **bulk apply** is one undo step. Select several rows, right-click an
  editable column, and choose *Apply this value to N selected rows* (copies the
  clicked cell) or *Apply … to N selected rows…* (prompts for one). Rows whose
  cell does not apply are skipped — so bulk-applying a Stage touches only the
  rows already set to Specified Stage. The picker columns offer only the
  copy-the-clicked-cell flavour, since their value has to come from the
  dropdown; that also makes "couple these twelve vertices to J1" one step.
- The mesh toolbar, the property browser and the map all refresh immediately —
  and an edit made in any of them refreshes the table.

Mesh elements cannot be added or deleted from the table; **Delete** is inactive
for these categories, and there is no *Change Type*.

## Finding bad cells

The **Area** column exists for mesh diagnostics. Tiny cells are what make a 2D
run slow — a handful of sub-square-metre cells forced in around a coupling
point can dominate the timestep — so the usual hunt is:

1. Open the **Cells** table and click the *Area* header to sort ascending.
   (Areas sort numerically, not as text, so the smallest cell really is the
   first row.)
2. Or query for them directly: `Area < 0.5`, and read the match count in the
   status label to see how many there are.
3. Set the selection radios to **Replace** and hit *Apply* to select the
   matches, then **Zoom to selected** to see where on the mesh they cluster.

This is the same measurement the layer-properties **Metadata** tab summarises
as min / max / mean / median — both read `mesh::triangleArea`, so the smallest
row here is exactly the minimum reported there.

## Query, selection, export

Everything the SWMM categories offer works here:

- the **WHERE** query bar filters rows by any column
  (`Boundary = 'Yes' AND Length > 12`);
- the **Replace / Add / Subtract / Intersect / Invert** radios apply the query
  result to the selection;
- **Show selected only**, **Zoom to selected**, **Copy** (Ctrl+C, TSV) and
  **Export CSV…** behave as they do elsewhere.

Selection is two-way through the same per-project bus described in
[08 — Selection](08_selection.md): picking rows highlights the elements on the
map, and picking on the map with the mesh vertex / edge / cell tools reveals and
highlights the rows.

## Tips and gotchas

- Selecting rows **replaces** the whole selection, as it does for a SWMM
  category — switching between the Vertices and Cells tables and selecting rows
  will not accumulate a mixed set.
- **Coupling Area is metres²** regardless of the project's flow units, which is
  why the header says so explicitly rather than following the unit system.
- Edge length and cell area are in **map units** (the project CRS), not the
  vertical unit used by Elevation.
- Column widths are remembered per element kind, so your Cells layout survives
  switching to Edges and back — and across sessions.

## Related

- [08 — Selection](08_selection.md) — the selection bus these tables share.
- [12 — Map Editing](12_map_editing.md) — moving vertices, and the mesh-editing
  toolbar these tables mirror.
