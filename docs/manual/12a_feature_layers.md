@page manual_feature_layers 12A — Editable GIS Feature Layers

## What you'll do

Create and edit GIS points, lines and polygons inside SWMMVis. Use them as mesh
domains, breaklines, refinement regions, corridors and parameter zones, or as
source features for importing SWMM objects. Feature layers keep their geometry
and attributes in a GeoPackage beside the model; the project remembers their
roles, styling and elevation settings.

Drawing a feature does not by itself create a SWMM node, conduit or subcatchment.
Use **Model → Import Feature Layer…** when you want to convert features into
model objects (\ref manual_map_editing).

## Where to find it

- **Features** ribbon: Select; New Layer; Edit Mode; Delete; Point; Line;
  Polygon; Add Part; Add Hole; Edit Vertices; Move.
- **View → Panels → Features** (`Ctrl+Alt+0`): layers, fields, attributes,
  vertex coordinates and elevation controls.
- **Model → Feature Layers**: the corresponding layer and editing commands.

\fig{12a_features_ribbon.png, The Features ribbon with layer creation; drawing and geometry editing commands}

## Step-by-step

### Create a layer

1. Open a project and save it to a named location. SWMMVis needs that location
   to create the companion `<model>.features.gpkg` file.
2. Choose **Features → New Layer**, or **New…** in the Features panel.
3. Choose **What is it for?** and enter a **Name**. The role supplies appropriate
   geometry types and fields; read the role's explanation before continuing.
4. Choose the geometry type. Choose a multipart type now if one feature may have
   several separate parts. The geometry type cannot be changed after creation.
5. Review the **Fields** table. Required fields are retained. Select the optional
   fields you need, edit their defaults, or use **Add custom field…**.
6. Expand **Elevation (Z)** if needed and choose a source. The choice of 2D or
   3D storage is also fixed when the layer is created.
7. Choose **Create**. Select the new layer in the Features panel and enable
   **Edit Mode** before drawing or changing its data.

The new layer uses the canvas CRS. When none is set, its coordinates are stored
as drawn; assign the correct CRS before combining it with other spatial data.
Horizontal CRS units and elevation units are separate choices; see \ref manual_crs.

\fig{12a_new_feature_layer.png, New Feature Layer with the role; geometry; fields and elevation settings}

### Choose the role

| Role | Geometry and fields | How it is used |
|---|---|---|
| **General** | Any supported geometry; `name` plus custom fields | An editable GIS layer for your own data |
| **Mesh domain boundary** | Polygon or multipolygon; required `name` | The area to mesh; interior rings become holes |
| **Breaklines / hard points** | Lines or points; optional `name`; elevation source | Lines mesh edges follow and points the mesh must include; the mesher reads geometry and Z |
| **Mesh regions** | Polygons; cell size `h`; cell mode `cells`; optional `tag` | Local sizing and alignment; `h = 0` uses the model cell size. **Quads if four-sided** allows aligned quads for four-sided regions; **Triangles** keeps triangles |
| **Corridors** | Line or multiline; total `width`; optional `tag` | Road or river centerlines used by mesh corridor sources |
| **Parameter zones** | Polygons; optional `mannings_n`; `init_depth`; `landuse`; `hsg` | Sources for assigning cell attributes; select and map them in the assignment workflow |
| **SWMM delineation** | Geometry appropriate for import; object naming/type fields | Source geometry for **Import Feature Layer…** |
| **Boundary-condition lines** | Lines; boundary type; head; slope; flow; time-series/curve references; group; conveyance | Stores proposed boundary data. Automatic assignment from these lines is not implemented; assign active boundary conditions with Mesh Editing |

A role helps construct the schema; it does not automatically apply its values to
an existing mesh. Choose the layer in the relevant mesh-generation or assignment
dialog, inspect its preview and complete that operation separately.

\fig{12a_mesh_region_fields.png, A Mesh regions layer showing cell-size and cell-mode defaults}

### Draw and modify geometry

The selected layer in the **Features** panel is the target of drawing and editing.
Check its name before using a tool, especially when several similar layers are
visible. Tools for incompatible geometry types are unavailable.

| Tool or gesture | Use |
|---|---|
| **Point** | Place a feature at the clicked map position |
| **Line** / **Polygon** | Click vertices in sequence; double-click or press **Enter** to finish |
| Right-click while drawing | Remove the most recent vertex; cancel when there is no earlier segment |
| **Esc** while drawing | Cancel the unfinished geometry |
| **Add Part** | Add another part to a selected feature in a compatible multipart layer |
| **Add Hole** | Draw an interior ring in a selected polygon |
| **Edit Vertices** | Select and move existing vertices; use the vertex context menu for available edits |
| **Move** | Move the selected feature on the map |
| **Delete** | Delete selected features while Edit Mode is active |
| **Select** | Return to selection when you finish drawing or editing |

Geometry edits participate in the project's undo history. Use **Undo** immediately
if the wrong layer or feature was changed. Unfinished geometry must satisfy its
type's minimum vertex count and validity rules before it can be committed.

\figtodo{12a_feature_vertices.png, A selected feature with its part; ring and vertex coordinates in the Features panel}

### Edit attributes and coordinates

The **Attributes** section describes the layer's columns; the **Features** table
contains the values of individual features. With Edit Mode enabled, edit cells
in the Features table. Its selection is linked to the map. **Add column…** and
**Remove column** change the schema; removing a column also removes its values
from every feature, so review the confirmation carefully.

Select exactly one feature to inspect the **Vertices** table. Its part and ring
columns distinguish multipart geometry and holes. X and Y are in the layer's CRS.
Editable Z values depend on the layer's elevation policy. A geometry with too
many vertices to list directs you to the map vertex tool.

**Update fields to role…** previews schema changes for an older layer: additions,
renames, defaults and value lists. Optional retired columns are explicitly listed
for removal. Review the preview, then choose **Update**; the update is one undo
operation. It does not implicitly reinterpret every existing feature value.

### Assign elevations

| Elevation setting | Meaning |
|---|---|
| **None — 2D layer** | Store horizontal geometry without Z |
| Constant source and **Value** | Store a fixed elevation |
| Raster source; **From layer**; **Raster band** | Sample a loaded raster at the feature vertices |
| 2D mesh source and **From layer** | Sample elevations from a loaded mesh |
| **Z conversion (×)** | Multiply sampled elevations to convert vertical units |
| **Densify before sampling** | Insert intermediate vertices at the specified spacing before sampling; useful where terrain changes between drawn vertices |
| **Re-sample when a vertex moves** | Refresh a moved vertex's elevation from its configured source |
| **Resample Z now** | Refresh the layer's features as one undo operation |

Read the coverage warning when sampling: a feature can extend beyond the raster
or mesh. Check the resulting Z values and the source's units before using the
layer as a breakline or channel input. A 2D layer cannot acquire 3D storage merely
by changing a display setting; create an appropriate 3D layer when needed.

### Import export and persistence

Use **Import…** in the Features panel to copy compatible vector data or a SWMM
object class into the selected editable layer. Review the source, geometry and
field choices before importing. Use **Export…** to write a selected feature layer
for use elsewhere. See \ref manual_layers for loading external GIS data and
\ref manual_map_editing for the separate GIS-to-SWMM conversion workflow.

Feature edits are written to the companion GeoPackage as they are applied.
**Save** retains the project's layer references and display settings; it is not a
rollback boundary for the GeoPackage. Use **Undo** to reverse feature edits
within the session. Keep the GeoPackage with the project when transferring it.
**Remove** removes a feature layer from the map; its table remains in the
GeoPackage. Removing a map layer and deleting its features are different actions.

## Tips and gotchas

- Save the project before creating its first feature layer.
- Check the selected target layer and Edit Mode before modifying geometry.
- Plan multipart and 3D storage before creating the layer.
- Creating or editing a source layer does not regenerate the mesh or reapply
  cell properties automatically.
- Boundary-condition line attributes are preparatory data; use the mesh edge
  editor to assign working boundary conditions in this build.
- Check sampled Z values and conversion factors before generating a mesh.

## Related

- \ref manual_map_editing — network editing and GIS-to-SWMM import
- \ref manual_layers — GIS sources and layer management
- \ref manual_crs — coordinate systems and transformations
- \ref manual_2d_mesh — mesh domains; breaklines; regions; corridors and assignments
- \ref manual_projects — project storage and portability
