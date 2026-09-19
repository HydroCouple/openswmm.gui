@page manual_property_index A9 — Object Property Index

## What you'll do

Find which chapter documents a given object's properties, which adapter drives
them, and which `.inp` sections they are written to.

This is a router, not a second copy of the property tables. Every property row
SWMMVis shows is documented once, in the chapter that owns the object; duplicating
those rows here would give you two descriptions to keep in step. What this page
adds is the single place to start looking, plus the `.inp` mapping.

The completeness of the chapters it points at is enforced, not asserted:
`scripts/manual_docs_audit.py property-labels` extracts every user-visible label
from the property adapters and fails if one appears nowhere in the manual.

## Where to find it

Properties appear in three places, and all three are views of the same adapter:

- the **Properties** panel, for the object selected on the map or in the Object Browser
- the **Attribute Table**, one row per object and one column per property
- the **compound editors** reached from a property row's `…` button

Because they share one adapter, an edit in any of them is the same edit — the
other two update immediately, and one undo step reverses it. See
\ref manual_object_browser and \ref manual_attribute_tables.

## Step-by-step

### Visual objects

| Object | Adapter | Documented in | Writes |
|---|---|---|---|
| Rain Gage | `SWMMRainGagePropertyAdapter` | \ref manual_hydrology → *Rain gages* | `[RAINGAGES]` `[SYMBOLS]` |
| Subcatchment | `SWMMSubcatchPropertyAdapter` | \ref manual_hydrology → *Subcatchment properties*; *Infiltration models* | `[SUBCATCHMENTS]` `[SUBAREAS]` `[INFILTRATION]` `[POLYGONS]` |
| Junction | `SWMMNodePropertyAdapter` | \ref manual_hydraulics → *Node types and their properties* | `[JUNCTIONS]` `[COORDINATES]` |
| Virtual Junction | `SWMMNodePropertyAdapter` | \ref manual_hydraulics → *Node types and their properties* | `[VIRTUAL_JUNCTIONS]` `[COORDINATES]` |
| Inlet Junction | `SWMMNodePropertyAdapter` | \ref manual_hydraulics → *Node types and their properties* | `[INLET_JUNCTIONS]` `[COORDINATES]` |
| Outfall | `SWMMNodePropertyAdapter` | \ref manual_hydraulics → *Node types and their properties* | `[OUTFALLS]` `[COORDINATES]` |
| Flow Divider | `SWMMNodePropertyAdapter` | \ref manual_hydraulics → *Node types and their properties* | `[DIVIDERS]` `[COORDINATES]` |
| Storage Unit | `SWMMNodePropertyAdapter` | \ref manual_hydraulics → *Node types and their properties* | `[STORAGE]` `[COORDINATES]` |
| Conduit | `SWMMLinkPropertyAdapter` | \ref manual_hydraulics → *Link types and their properties* | `[CONDUITS]` `[XSECTIONS]` `[LOSSES]` `[VERTICES]` |
| Pump | `SWMMLinkPropertyAdapter` | \ref manual_hydraulics → *Link types and their properties* | `[PUMPS]` `[VERTICES]` |
| Orifice | `SWMMLinkPropertyAdapter` | \ref manual_hydraulics → *Link types and their properties* | `[ORIFICES]` `[XSECTIONS]` `[VERTICES]` |
| Weir | `SWMMLinkPropertyAdapter` | \ref manual_hydraulics → *Link types and their properties* | `[WEIRS]` `[XSECTIONS]` `[VERTICES]` |
| Outlet | `SWMMLinkPropertyAdapter` | \ref manual_hydraulics → *Link types and their properties* | `[OUTLETS]` `[VERTICES]` |
| Map Label | *retired* | — | `[LABELS]` is parsed and discarded |

Two entries from the legacy manual's catalogue no longer exist as objects:

- **Map Label.** SWMMVis reads `[LABELS]` and drops it (see \ref manual_file_formats).
  Text on the map is an annotation stored in the `.oswp` project, not a model
  object — see \ref manual_map_editing.
- **Backdrop.** A backdrop was an unreferenced bitmap stretched to fit. Its
  replacement is a georeferenced raster, WMS/WMTS or XYZ basemap layer, which is
  a layer rather than a property of the model — see \ref manual_layers.

### Node and link compound editors

Some properties are edited through a dialog rather than a single cell. They are
still the same adapter and the same undo step.

| Editor | Reached from | Documented in | Writes |
|---|---|---|---|
| Inflows — Direct; Dry Weather; RDII | a node's **Inflows** row | \ref manual_hydraulics → *Node compound editors* | `[INFLOWS]` `[DWF]` `[RDII]` |
| Treatment | a node's **Treatment** row | \ref manual_hydraulics → *Node compound editors* | `[TREATMENT]` |
| Cross-Section | a link's **Cross-Section** row | \ref manual_hydraulics → *Cross-section editor and the Section View* | `[XSECTIONS]` |
| Inlet usage | a conduit's **Inlet** row | \ref manual_hydraulics → *Inlet usage on a conduit* | `[INLET_USAGE]` |
| Infiltration | a subcatchment's **Infiltration** row | \ref manual_hydrology → *Infiltration models* | `[INFILTRATION]` |
| Groundwater exchange | a subcatchment's **Groundwater** row | \ref manual_hydrology → *Groundwater Exchange editor* | `[GROUNDWATER]` `[GWF]` |
| LID usage | a subcatchment's **LID** row | \ref manual_hydrology → *LID Control editor* | `[LID_USAGE]` |
| Land use assignment | a subcatchment's **Land Uses** row | \ref manual_water_quality | `[COVERAGES]` `[LOADINGS]` |

### Non-visual objects

These have no map geometry; they are edited from the Object Browser and the
Attribute Table.

| Object | Adapter | Documented in | Writes |
|---|---|---|---|
| Aquifer | `SWMMAquiferPropertyAdapter` | \ref manual_hydrology → *Aquifer editor* | `[AQUIFERS]` |
| Snow Pack | `SWMMSnowpackPropertyAdapter` | \ref manual_hydrology → *Snow pack editor* | `[SNOWPACKS]` |
| LID Control | `SWMMLIDControlPropertyAdapter` | \ref manual_hydrology → *LID Control editor* | `[LID_CONTROLS]` |
| Unit Hydrograph | `SWMMHydrographPropertyAdapter` | \ref manual_hydrology → *Unit hydrographs and RDII* | `[HYDROGRAPHS]` |
| Pollutant | `SWMMPollutantPropertyAdapter` | \ref manual_water_quality | `[POLLUTANTS]` |
| Land Use | `SWMMLandUsePropertyAdapter` | \ref manual_water_quality | `[LANDUSES]` `[BUILDUP]` `[WASHOFF]` |
| Transect | `SWMMTransectPropertyAdapter` | \ref manual_hydraulics → *Transect editor* | `[TRANSECTS]` |
| Street | `SWMMStreetPropertyAdapter` | \ref manual_hydraulics → *Street editor* | `[STREETS]` |
| Inlet | `SWMMInletPropertyAdapter` | \ref manual_hydraulics → *Inlet editor* | `[INLETS]` |
| Control Rule | `SWMMControlRulePropertyAdapter` | \ref manual_hydraulics → *Control rules editor* | `[CONTROLS]` |
| Time Series | `SWMMTimeSeriesPropertyAdapter` | \ref manual_data_objects → *The time series editor* | `[TIMESERIES]` |
| Curve | `SWMMCurvePropertyAdapter` | \ref manual_data_objects → *The curve editor* | `[CURVES]` |
| Time Pattern | `SWMMPatternPropertyAdapter` | \ref manual_data_objects → *The time pattern editor* | `[PATTERNS]` |

### 2D mesh objects

| Object | Adapter | Documented in | Writes |
|---|---|---|---|
| Mesh triangle or quad | `MeshTrianglePropertyAdapter` | \ref manual_2d_mesh → *Cell parameters* | inline 2D sections or `.2dm` |
| Mesh edge | `MeshEdgePropertyAdapter` | \ref manual_2d_mesh → *Boundary conditions* | inline 2D sections or `.2dm` |
| Mesh vertex | `MeshVertexPropertyAdapter` | \ref manual_2d_mesh → *Preparing the terrain* | inline 2D sections or `.2dm` |

## Tips and gotchas

- **One adapter, three views.** The Properties panel, the Attribute Table and the
  compound editors are views of the same model. An edit in any of them is the
  same edit and one undo reverses it — they never hold divergent values.
- **Labels carry live unit suffixes.** A row reads **Invert Elev. (ft)** or
  **Invert Elev. (m)** depending on `[OPTIONS] FLOW_UNITS`. Only the suffix
  changes; the property is the same. See \ref manual_reference_tables.
- **Read-only rows are of two kinds.** Geometry derived from the map (a conduit's
  length when it follows its vertices) and post-run statistics (peak flow, total
  volume) are both shown greyed. The first becomes editable if you detach it from
  the geometry; the second never does.
- **A node's type is a property, not a separate object.** Converting a junction
  to a storage unit rewrites which `.inp` section it lands in. See
  \ref manual_map_editing.
- **The `.inp` sections named here are where the property is written**, not
  necessarily the only place it is read. The engine's own documentation describes
  each section's fields and their numerical meaning.

## Related

- \ref manual_object_browser — the Object Browser, Properties panel and Section View
- \ref manual_attribute_tables — one row per object; bulk edits
- \ref manual_hydraulics — node and link properties in full
- \ref manual_hydrology — rain gage, subcatchment, LID, snow and groundwater properties
- \ref manual_water_quality — pollutants, land uses and treatment
- \ref manual_data_objects — time series, curves and patterns
- \ref manual_2d_mesh — mesh cell, edge and vertex parameters
- \ref manual_reference_tables — starting values for the numbers these rows take
- \ref manual_file_formats — what each `.inp` section holds
