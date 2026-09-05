@page manual_file_formats A2 — File Formats

## What you'll do

Understand every file SWMMVis reads or writes: what is in it, which part of the
application owns it, and what round-trips safely.

## Where to find it

**Tools → Plugins…** lists every file-format filter the running build can offer
in an Open or Save dialog, grouped by role — Input (read), Results (read /
write), Report (write), Hot-Start (read / write), Project (read / write),
Vector (read), Raster (read), Tabular (read), Map Export (write) and Component
Config (read). See \ref manual_plugins.

## Step-by-step

### Overview

| Extension | Role | Written by |
|---|---|---|
| `.inp` | SWMM input file — the model | engine |
| `.oswp` | SWMMVis project sidecar — layers, styles, CRS, display state | GUI |
| `.rpt` | status report | engine |
| `.out` | binary 1D results | engine |
| `.2d.h5` | 2D surface results, CF-1.11 / UGRID-1.0 HDF5 | engine |
| `.2dm` | external 2D mesh | GUI |
| `.gpkg` | GeoPackage model container | engine plugin |
| `.rxn` `.ard` `.age` `.heat` `.i2d` | process-component configs | engine components |
| `.hs` / `.hsf` | hot-start state | engine |
| `.dat` and friends | rain and climate records | third party |

### `.inp` — the SWMM input file

The `.inp` is the model. **SWMMVis has no `.inp` writer of its own**: Save and
Save As route to the engine's writer, and the GUI then writes its `.oswp`
sidecar alongside. (When you save through a plugin-backed format such as
`.gpkg`, the sidecar is skipped.)

#### Sections written, in emission order

`TITLE` · `OPTIONS` · `EVAPORATION` · `TEMPERATURE` · `SNOWPACKS` ·
`ADJUSTMENTS` · `EVENTS` · `RAINGAGES` · `SUBCATCHMENTS` · `SUBAREAS` ·
`INFILTRATION` · `AQUIFERS` · `GROUNDWATER` · `GWF` · `LID_CONTROLS` ·
`LID_USAGE` · `JUNCTIONS` · `VIRTUAL_JUNCTIONS` · `INLET_JUNCTIONS` ·
`OUTFALLS` · `DIVIDERS` · `STORAGE` · `CONDUITS` · `PUMPS` · `ORIFICES` ·
`WEIRS` · `OUTLETS` · `XSECTIONS` · `LOSSES` · `TRANSECTS` · `STREETS` ·
`INLETS` · `INLET_USAGE` · `CONTROLS` · `REPORT` · `POLLUTANTS` ·
`INITIAL_QUALITY` · `LANDUSES` · `COVERAGES` · `BUILDUP` · `WASHOFF` ·
`LOADINGS` · `TREATMENT` · `INFLOWS` · `DWF` · `RDII` · `HYDROGRAPHS` ·
`RDII_DECAY` · `PATTERNS` · `TIMESERIES` · `CURVES` · `MAP` · `COORDINATES` ·
`VERTICES` · `Polygons` · `SYMBOLS` · `TAGS` · `USER_FLAGS` ·
`USER_FLAG_VALUES` · `FILES` · `PLUGINS` · `PROCESS_COMPONENTS` · then the 2D
block.

The 2D block is `2D_INFILTRATION_OPTIONS`, `2D_INFILTRATION_DEFAULTS`,
`2D_INFILTRATION`, `2D_OPTIONS`, then **either** `2D_MESH_FILE` (external mesh)
**or** the inline mesh — `2D_VERTICES`, `2D_TRIANGLES`,
`2D_INITIAL_VELOCITY`, `2D_VERTEX_NODE_MAP`, `2D_TRIANGLE_NODE_MAP`,
`2D_BOUNDARY_CONDITIONS`, `2D_EDGE_CONVEYANCE`, `2D_INITIAL_QUALITY`,
`2D_BOUNDARY_QUALITY`.

#### New in SWMM 6

`VIRTUAL_JUNCTIONS`, `INLET_JUNCTIONS`, `RDII_DECAY`, `USER_FLAGS`,
`USER_FLAG_VALUES`, `PLUGINS`, `PROCESS_COMPONENTS`, and the whole `2D_*`
family. `STREETS`, `INLETS` and `INLET_USAGE` also exist but are SWMM 5.2
features, not 6.0 additions.

#### GUI-oriented sections

| Section | Read | Written | Notes |
|---|---|---|---|
| `[MAP]` | yes | yes | `DIMENSIONS x1 y1 x2 y2` plus `Units` — `None`, `Feet`, `Meters` or `Degrees` |
| `[COORDINATES]` | yes | yes | node positions |
| `[VERTICES]` | yes | yes | link polyline vertices |
| `[Polygons]` | yes | yes | written with that exact mixed case; normalised to `POLYGONS` on read |
| `[SYMBOLS]` | yes | yes | rain-gage positions |
| `[TAGS]` | yes | yes | |
| `[USER_FLAGS]` / `[USER_FLAG_VALUES]` | yes | yes | |
| `[LABELS]` | parsed and discarded | **no** | |
| `[BACKDROP]` | parsed and discarded | **no** | |
| `[PROFILE]` | parsed and discarded | **no** | registered singular, not `PROFILES` |

**`[LABELS]`, `[BACKDROP]` and `[PROFILE]` are lost on save.** They parse
without error and are then dropped, and nothing writes them back. If you need
map annotations or a backdrop to survive, keep them in the `.oswp` (annotations
are stored there) or in a separate GIS layer.

The model CRS is written as `[OPTIONS] CRS`; the 2D HDF5 writer reads it.

### `.oswp` — the SWMMVis project

Pretty-printed JSON, UTF-8, current schema version **4**. It sits beside the
`.inp` with the same basename and is written on every successful built-in
`.inp` save.

Every path inside is stored **relative** to the `.oswp`'s own directory, so the
folder is portable; an absolute path is only written when a relative one is
impossible (a different Windows drive, for example). Symlinks are deliberately
not resolved. Unknown keys are skipped silently, so a newer file opens in an
older build without complaint.

#### Root keys

| Key | Contents |
|---|---|
| `schemaVersion` | integer |
| `sessions` | one object per project window |
| `canvas` | `crsAuthority`, `crsCode`, `extent` — `[xmin, ymin, xmax, ymax]` |
| `basemaps` | basemap layers (omitted when empty) |
| `gisLayers` | non-basemap GDAL/OGR layers (omitted when empty) |

#### Session keys

`id`, `title`, `notesHtml`, `inpPath`, `engineVersion`, `layer`,
`resultLayers`, `resultLayerSublayers`, `resultLayerKindRenderers`,
`resultLayerReports`, `terrain`, `meshLayers`, `results2DLayers`,
`legendOverlay`, `annotations`.

- **`layer`** carries the model layer's `crsAuthority` / `crsCode`, category and
  object ordering, hidden objects, the `renderer`, per-kind `kindRenderers`,
  rule metadata, label configuration, temporal settings, mask, joins and
  diagram. Per-layer symbology sub-keys include `isVisible`,
  `filterExpression`, `minScale`, `maxScale`, `blendMode`, `rebinPerFrame` and
  `symbolLevelsEnabled`. Renderer kinds are `single`, `graduated`,
  `categorized`, `rule` and `multikind`.
- **`resultLayers`** is a list of relative `.out` paths;
  `resultLayerReports` maps each to its `.rpt`, and the sublayer and per-kind
  renderer maps are keyed by the same path.
- **`terrain`** is `activeLayerPath`, `nodeOffsetM`, `linkOffsetM`,
  `verticalUnit`.
- **`meshLayers`** carries **display state only** — `sourcePath`, `active`,
  `showMeshNodes`, `showEdges`, a `hillshade` block (`azimuth`, `altitude`,
  `zExag`, `minLit`), a `contours` block (`show`, `intervals`, `color`,
  `width`, `filled`, `filledOpacity`) and sublayers. The mesh itself is loaded
  from `[2D_MESH_FILE]` or the inline sections in the `.inp` and matched back by
  resolved `sourcePath`.
- **`results2DLayers`** is `path`, `name`, `visible`, `opacity`, `dryDepth`,
  `maxDepth`, `maxVelocity` and sublayers.
- **`basemaps`** entries carry `type` (`xyz`, `wmts`, `wms`, `localraster`),
  `name`, `url`, `headers`, `tilePixelRatio`, `axisOrder`, `layerName`,
  `style`, `imageFormat`, `crs`, `tileMatrixSet`, `dpiMode` and — for
  `localraster` only — `path`.
- **`gisLayers`** entries carry `type` (`raster` or `vector`), `path`, `name`,
  `visible`, `opacity` and the OGR `layerName`.

#### What the `.oswp` does *not* hold

Dock and toolbar layout, window geometry and the recent-files list live in
`QSettings` (group `SWMMVis::MainWindow`), not in the project. Animation state
is not written to the project either. Profile-plot paths are not stored anywhere
— see the `[PROFILE]` note above.

\figtodo{a02_oswp_structure.png, An .oswp opened in a text editor showing the sessions and meshLayers blocks}

### `.rpt` — the status report

Plain text, written by the engine's report plugin. Its content is controlled by
the `[REPORT]` section: `DISABLED`, `INPUT`, `CONTINUITY`, `FLOWSTATS`,
`CONTROLS`, `AVERAGES`, and the `SUBCATCHMENTS` / `NODES` / `LINKS` object
lists. **Analysis → Report** and **Analysis → Show Mass Balance** both open it —
mass balance lives in the report's continuity blocks, not in a dialog of its
own. The path of the `.rpt` matching a loaded `.out` is recorded on the results
layer and persisted in the `.oswp`.

### `.out` — binary 1D results

Little-endian binary, magic `516114522`, version `60000`. Layout, as documented
by the reader:

```
[Header]         magic, version, flow_units, counts (7 x int32)
[ID Section]     subcatchment / node / link names (length-prefixed strings)
[Input Section]  static properties (subcatchment areas, node and link geometry)
[Variable Codes] per-type variable code arrays and report metadata
[Output Section] per period: date (8 bytes) + subcatchment + node + link + system results
[Footer]         id_start, input_start, output_start, n_periods, error_code, magic
```

Flow-unit codes: `0 = CFS`, `1 = GPM`, `2 = MGD`, `3 = CMS`, `4 = LPS`,
`5 = MLD`.

**Live reading.** SWMMVis can open an `.out` **while the engine is still
writing it**. The reader derives the readable period count from the file size
and the output offset, so a partially flushed record is never counted. As
periods land, the layer emits a "periods now readable" signal, and when the
footer arrives it emits a "file is final" signal — so plots and the animation
controller keep their binding across the transition instead of being torn down
and rebuilt. Before the header exists the open simply fails quietly and is
retried on the next progress tick.

A cancelled run still leaves a usable `.out`: the engine flushes partial output
either way, and SWMMVis loads it.

### `.2d.h5` — 2D surface results

HDF5 written by the engine's built-in `Default2DOutputPlugin`, following the
**CF-1.11** and **UGRID-1.0** conventions, so ParaView and QGIS can read it
directly. The path comes from `[2D_OPTIONS] OUTPUT_FILE`; an empty value means
no 2D output. It does **not** need a `[PLUGINS]` row.

Root attributes: `Conventions = "CF-1.11 UGRID-1.0"`, `title`, `institution`,
`source`.

| Group / dataset | Contents |
|---|---|
| `/crs` | scalar variable carrying `model_crs`, `metres_per_model_unit`, `units` and a comment |
| `/Mesh2` | topology variable — `cf_role = mesh_topology`, `topology_dimension`, `node_coordinates`, `face_node_connectivity`, `face_coordinates` |
| `Mesh2_node_x` / `_y` / `_z` | vertex coordinates and bed elevation |
| `Mesh2_face_nodes` | face–node connectivity with `start_index` |
| `Mesh2_face_x` / `_y` / `_z` | cell centroids and bed |
| `Mesh2_face_mannings_n`, `Mesh2_face_area` | static cell properties |
| `Mesh2_edge_length`, `Mesh2_edge_nx`, `Mesh2_edge_ny` | static edge properties |
| `/time` | `units = "days since simulation start"`, `calendar = "standard"` |

Time-varying face datasets, each tagged `mesh = "Mesh2"` and
`location = "face"`: `Mesh2_face_depth`, `Mesh2_face_head`,
`Mesh2_face_grad_hx`, `Mesh2_face_grad_hy`, `Mesh2_face_grad_hx_lim`,
`Mesh2_face_grad_hy_lim`, `Mesh2_face_rainfall`,
`Mesh2_face_coupling_flux`, `Mesh2_face_net_source`,
`Mesh2_face_infil_rate`, `Mesh2_face_infil_cum`, `Mesh2_face_rain_cum`,
`Mesh2_face_vx`, `Mesh2_face_vy`, `Mesh2_face_continuity_err`. Edge and node
series: `Mesh2_edge_flux`, `Mesh2_node_head`, `Mesh2_node_depth`.

Envelopes, tagged `cell_methods = "time: maximum"`:
`Mesh2_face_max_depth`, `Mesh2_face_max_velocity`,
`Mesh2_face_max_continuity_err`.

Quality: `Mesh2_face_species_conc` plus a `species_names` dataset.

`/mass_balance_2d` holds scalar doubles in m³ — `init_storage`,
`final_storage`, `rainfall_in`, `coupling_1d_to_2d_in`,
`coupling_2d_to_1d_out`, `outfall_in`, `outfall_out`, `boundary_in`,
`boundary_out`, `evap_out`, `infil_out` — with a `continuity_error` group
attribute.

**Coordinate caveat.** `Mesh2` x and y are stored in **SI metres regardless of
the model's units**. Divide by `metres_per_model_unit` to recover model-CRS
coordinates before reprojecting. The CRS is carried as a `model_crs` attribute
on `/crs` rather than as a CF `grid_mapping`, deliberately.

A quick sanity check from a shell:

```sh
h5dump -n model.2d.h5   # should list /Mesh2_face_depth, /time, /mass_balance_2d, ...
```

### `.2dm` — external 2D mesh

Despite the extension, this is **not** SMS/ADCIRC 2DM. It is an
OpenSWMM-native bracketed-section text file, parsed by the same grammar as the
`.inp`, and it is the only format SWMMVis writes directly rather than through
the engine.

Header written by the GUI:

```
;; OpenSWMM 2D Mesh File
;; Source project: <name>.inp
;; Generated by openswmm.gui

;; UNITS: metre
;; SOURCE_CRS: EPSG:25832
```

The `;; UNITS:` line is read by a prescan before parsing; with it the engine
treats the coordinates as already-SI metres and skips its `FLOW_UNITS`-based
foot-to-metre scaling. Write it into every new mesh.

Accepted sections: `2D_VERTICES`, `2D_TRIANGLES`, `2D_INITIAL_VELOCITY`,
`2D_VERTEX_NODE_MAP`, `2D_TRIANGLE_NODE_MAP`, `2D_BOUNDARY_CONDITIONS`,
`2D_INITIAL_QUALITY`, `2D_BOUNDARY_QUALITY`, `2D_EDGE_CONVEYANCE`,
`2D_INFILTRATION_OPTIONS`, `2D_INFILTRATION_DEFAULTS`, `2D_INFILTRATION`.
`[2D_OPTIONS]` stays in the `.inp` and is never written to the `.2dm`.

**Precedence:** a section present in the `.2dm` **replaces** the `.inp`'s inline
version; a section the `.2dm` omits keeps whatever the `.inp` supplied.

Writing an external mesh also patches the `.inp`: any inline 2D data sections
and any prior reference are stripped and replaced with

```
[2D_MESH_FILE]
FILE  <path relative to this .inp>
```

Choose inline or external in the **Generate 2D Mesh** dialog's footer —
**External .2dm** or **Inline in .inp** — with the file filter
`SWMMVis 2D Mesh (*.2dm)`.

**Gotcha:** two consecutive saves of the same mesh are **not byte-identical**,
because the vertex-node map is written in hash order. Expect noise if the
`.2dm` is under version control.

### `.gpkg` — GeoPackage model container

A GeoPackage is an **alternative container for the whole model**, provided by
an engine plugin, not a GUI layer bundle. Saving to `.gpkg` is a standalone
write: no `.oswp` sidecar is produced.

One `.gpkg` can carry the network (`nodes`, `storages`, `outfalls`,
`dividers`, `links`, `conduits`, `pumps`, `orifices`, `weirs`, `outlets`,
`subcatchments`, `rain_gages`, …), hydrology and quality tables
(`lid_controls`, `lid_usage`, `pollutants`, `landuses`, `buildup`, `washoff`,
`treatment`, `rdii_assignments`, `unit_hydrographs`, …), the 2D mesh
(`mesh_2d_vertices`, `mesh_2d_triangles`,
`mesh_2d_boundary_conditions`, `mesh_2d_edge_conveyance`,
`mesh_2d_vertex_coupling`, `mesh_2d_triangle_coupling`), results
(`result_timeseries`, `result_summary`), observed data (`observed_series`,
`observed_values`), hot-start slots, and embedded raingage, climate and
routing-interface data — alongside the standard `gpkg_contents`,
`gpkg_geometry_columns` and `gpkg_spatial_ref_sys` tables.

**It does not carry layer styles.** SWMMVis styles are exported separately as
`*.swmm-style.json` or QGIS `*.qml`.

A `.gpkg` is also a readable **vector** data source, so the same file can be
added as a GIS layer; multi-layer sources prompt for a sublayer.

### Process-component config files

Referenced from `[PROCESS_COMPONENTS]` as `config="…"`, resolved relative to
the `.inp`'s directory. All are bracketed-section text.

| Extension | Component | Sections |
|---|---|---|
| `.rxn` | Reaction System | `REACTION_OPTIONS`, `REACTION_SPECIES`, `REACTION_COEFFICIENTS`, `REACTION_TERMS`, `REACTION_PIPES`, `REACTION_TANKS`, `REACTION_QUALITY` |
| `.ard` | Eulerian ARD Transport | `TRANSPORT_OPTIONS`, `CONDUIT_DISPERSION`, `TRANSPORT_BOUNDARIES`, `TRANSPORT_SOURCES` |
| `.age` | Water Age | `WATER_AGE_SOURCES` |
| `.heat` | Heat Transport | `HEAT_SOURCES`, `HEAT_FLUXES`, `RADIATIVE_FLUXES`, `SOLAR_RADIATION`, `CLOUD_COVER`, `SEDIMENT_EXCHANGE` |
| `.lard` | Lagrangian Transport | *(filter is registered, but no such component or file format exists yet — LARD is selected with `[OPTIONS] QUALITY_SOLVER LAGRANGIAN` alone)* |
| `.i2d` | Integrated 2D | *(filter registered; component not implemented)* |

`.rxn` details, since it is the one with a GUI editor:

- `[REACTION_OPTIONS]` — `SOLVER` (`EUL`/`RK5`/`ROS2`/`BDF2`), `COUPLING`
  (`NONE`/`FULL`), `RATE_UNITS` (`SEC`/`MIN`/`HR`/`DAY`), `AREA_UNITS`
  (`FT2`/`M2`/`CM2`), `TIMESTEP`, `ATOL`, `RTOL`, `TEMPERATURE`.
- `[REACTION_SPECIES]` — `BULK|WALL <name> <units> [atol] [rtol]`. At least one
  species is required, and a species may not share a name with a pollutant.
- `[REACTION_COEFFICIENTS]` — `PARAMETER|CONSTANT <name> <value>`.
- `[REACTION_TERMS]` — `<name> <expression>`.
- `[REACTION_PIPES]` / `[REACTION_TANKS]` — `RATE|EQUIL|FORMULA <species>
  <expression>`.
- `[REACTION_QUALITY]` — `GLOBAL <species> <value>` or
  `NODE|LINK <element> <species> <value>`.

Expressions may use the hydraulic variables `D`, `Q`, `U`, `RE`, `US`, `FF`,
`AV`, `HRT`, `DT` and `TEMP`, the functions `EXP`, `LOG`, `LOG10`, `SQRT`,
`ABS`, `SGN`, `STEP`, `SIN`, `COS`, `TAN`, `MIN`, `MAX`, `POW`, and the
operators `+ - * / ^`. Parse order is forced, so section order in the file does
not matter.

`[REACTION_SOURCES]`, `[REACTION_PARAMETERS]`, `[REACTION_PATTERNS]`,
`[REACTION_REPORT]` and `[REACTION_SUBCATCHMENTS]` are recognised but
**rejected** by the current engine — do not use them.

The `[REACTION_*]` sections may also be written **inline in the `.inp`** as a
fallback. That produces a style warning, and an external `.rxn` wins on
conflict.

### Rain files

`[RAINGAGES]` takes `Name Format Intvl SCF Source`, with `Format` one of
`INTENSITY`, `VOLUME` or `CUMULATIVE`, and `Source` one of `TIMESERIES <name>`
or a `FILE` form. Two file grammars:

```
FILE "path" Station Units [StartDate] [ScaleFactor]     ;; legacy
FILE "path:column"                                      ;; OpenSWMM user CSV
```

`Units` is `MM` or `IN`; `*` is the placeholder for "no start date"; a trailing
numeric token is a **rainfall scale factor** applied to every value (default
1.0). The legacy form requires a station id; an empty one is written as `*` with
a warning.

Recognised file formats: NWS 15-minute, NWS hourly, NCDC DSI-3240 hourly, NCDC
DSI-3260 15-minute, HLY-PRCP, the standard SWMM rain file, and — new in
6.0 — a **user CSV**: any multi-column text file (CSV, TSV or PCSWMM TSF),
auto-detected by content, with the column named after a colon in the path.

Unreadable rows in a CSV are skipped with a warning rather than aborting the
run.

### Climate files

`[TEMPERATURE] FILE <path> [start] [C10|C|F]` — `C10` is tenths of a degree
Celsius. `[EVAPORATION] FILE [pc1 … pc12]` reads pan evaporation with optional
monthly coefficients. `WINDSPEED` takes `MONTHLY` or `FILE`; `HUMIDITY` takes
`MONTHLY` with twelve values or a single constant.

Formats are auto-detected: **user-prepared**
(`StationID YYYY MM DD TMAX TMIN EVAP WIND`), **NCDC GHCND**, **NCDC TD3200 /
NWS cooperative observer**, and **Canadian DLY02/DLY04**.

### `[FILES]` — interface and hot-start files

Grammar: `SAVE|USE <KIND> "path" [date time]`.

| Kind | Direction | Notes |
|---|---|---|
| `RAINFALL` | SAVE / USE | processed rainfall interface |
| `RUNOFF` | SAVE / USE | binary; header stamp `SWMM5-RUNOFF` |
| `RDII` | SAVE / USE | binary (`SWMM5-RDII`) or legacy text; both readable, writing is always binary |
| `INFLOWS` | **USE only** | `SAVE INFLOWS` is a fatal error |
| `OUTFLOWS` | **SAVE only** | `USE OUTFLOWS` is a fatal error |
| `HOTSTART` | SAVE / USE | `USE` takes one path; `SAVE` may appear **several times**, each with an optional `MM/DD/YYYY HH:MM:SS` |

Unknown kinds are ignored (with warning 103). All paths are rebased relative to
the destination directory unless absolute paths are requested.

**Hot-start files.** The native format is `*.hs` — a 16-byte magic
`OPENSWMM_HS_V1`, a version word, a timestamp and a simulation time, then
state. Versions 1–3 are readable; version 3 is written when water age is on.
Legacy SWMM 5 `*.hsf` files are **read-only**.

**Routing interface file.** A text file in which an upstream model writes
outfall results and a downstream model reads them as inflow boundary
conditions, interpolated between periods.

**RDII interface file.** Records are **step-aligned, not interpolated** — a
record applies over `[date, date + step)`.

### Delimited text

**File → Import → Delimited** takes `Delimited text (*.csv *.tsv *.txt)`.
`.csv` is split on commas, `.tsv` and `.tab` on tabs, and an unknown suffix
falls back to CSV. `.xlsx` is recognised but not enabled in the shipped build —
it reports *Excel (.xlsx) import requires QXlsx — not yet enabled in this
build*. PCSWMM `.tsf` files are handled as tab-delimited with an IDs row naming
the columns.

### GIS formats via GDAL / OGR

The Open dialogs offer only the drivers the running build actually has, so your
list may be shorter than this one.

**Raster:** GeoTIFF / COG (`tif` `tiff`), Arc/Info ASCII Grid (`asc`), Erdas
Imagine (`img`), SRTMHGT (`hgt`), USGS DEM (`dem`), DTED (`dt0` `dt1` `dt2`),
ESRI/ENVI binary (`bil` `bsq` `flt`), ENVI (`dat` `raw`), Surfer
(`grd`), Terragen (`ter`), PNG, JPEG, VRT, netCDF (`nc`), HDF5 (`h5` `hdf5`),
HDF4 (`hdf`).

**Vector:** ESRI Shapefile (`shp`), GeoPackage (`gpkg`), GeoJSON (`geojson`
`json`), Esri File Geodatabase (`gdb`), SQLite / SpatiaLite (`sqlite` `db`),
GML, KML / KMZ, MapInfo (`tab` `mif`), CSV point data.

The minimal build ships OpenFileGDB, GeoPackage, Shapefile, GeoJSON and
SQLite; GML, KML and MapInfo appear only when their optional module was
compiled in. Multi-layer sources (GeoPackage, File GDB, multi-layer GML/KML)
prompt with a sublayer-selection dialog.

### Exports

| From | Format |
|---|---|
| **File → Map Image…** | filters offer PNG, SVG, DXF, EMF and EPA SWMM Map (`.map`), but **only PNG is implemented today** — any other suffix reports that it is not implemented yet |
| **File → Print** | a system print dialog over a canvas grab (a PDF printer therefore works, but there is no PDF filter) |
| Copy map view | pixmap to the clipboard |
| Tabular results dialog *(compiled but unreachable — see \ref manual_tabular_results)* | `CSV (*.csv)` or `TSV (*.tsv)` |
| Attribute table | `CSV (*.csv);;TSV (*.tsv)` |
| Custom report builder *(compiled but unreachable)* | `CSV (*.csv)` |
| Statistics dashboard | `CSV (*.csv)` |
| Mesh cell data | `CSV files (*.csv)` |
| Comparison plot | `CSV file (*.csv);;SWMM time series (*.dat)`, plus `PNG image (*.png)` |
| Profile plot, transect editor, inlet editor | `PNG image (*.png)` |
| Pattern editor | `PNG Image (*.png);;Scalable Vector Graphics (*.svg)` — the only working SVG export |
| Layer style | `SWMMVis style (*.swmm-style.json *.json)` |
| Style manager | `Style files (*.qml)` and SWMM rule files |

Plot image export is **PNG only** except in the pattern editor. Data export is
CSV, with TSV in two places.

\figtodo{a02_export_filters.png, The Export Map dialog showing the available filters}

## Tips and gotchas

- `[LABELS]`, `[BACKDROP]` and `[PROFILE]` do not survive a save. Keep anything
  you care about elsewhere.
- The `.oswp` is small JSON — a 995-node, 155k-triangle project's sidecar is
  about 16 KB. It is safe to keep under version control; the `.2dm` is not,
  because it is not byte-stable.
- An `.inp` is read up to four times when a project opens (engine parse, map
  units scan, mesh scan, 2D output scan). That is expected.
- Deleting the `.oswp` loses styling, layers and CRS but never the model — the
  `.inp` is complete on its own.
- 2D mesh coordinates in the `.h5` are metres even in a US-units project.
  Always divide by `metres_per_model_unit` before reprojecting.
- Set `;; UNITS: SI (m)` (or `metre`) in every new `.2dm` and in inline 2D
  sections. Without it, a US-units project silently scales the mesh by 0.3048.

## Related

- \ref manual_projects — opening, saving and portability
- \ref manual_layers — every data source the GUI can add
- \ref manual_results — reading `.out` and `.2d.h5` results
- \ref manual_2d_mesh — inline versus external meshes
- \ref manual_water_quality — the `.rxn`, `.ard`, `.age` and `.heat` configs
- \ref manual_plugins — the format registry behind the Open and Save dialogs
- \ref manual_troubleshooting — parse errors and missing files
