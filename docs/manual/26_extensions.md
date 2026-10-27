@page manual_extensions 26 — GUI extensions

GUI extensions add ribbon commands, dock panels, document views and preferences
to SWMMVis. They are compiled Qt plugins built against the matching GUI SDK.

The SDK, extension loader and host services are maintained in `openswmm.gui`
and documented with the GUI. Individual plugins can be maintained in separate
repositories and built against the installed SDK. Use **GUI Extensions** in
the documentation navigation for the author guide and API references.

Open **Tools → Plugins…** to see engine I/O plugins, process components and GUI
extensions. GUI extension rows show version, vendor, license, build compatibility,
load status and failure details. **Add directory…** adds an existing plugin
directory; **Open plugin folder** opens the user plugin directory.

Enable or disable an extension with its checkbox, then restart SWMMVis. Rescan
updates the inventory; it does not execute newly discovered code. Extensions
whose Qt, compiler, architecture or SDK version is incompatible remain listed
with the reason they could not load.

If an extension prevents normal startup, launch with **--no-extensions** or hold
**Shift** while launching. An interrupted initialization or extension job disables
the affected extension on the next launch and displays a recovery explanation.
Native extensions execute inside the application and should come from a trusted
author.

Extension dock visibility and layout follow normal window settings. Extension
document state and project-specific configuration are stored with the project.
Unavailable document kinds are retained so a project can be reopened after the
extension is restored. The shared **Output** dock contains an output tab for each
extension that writes messages. The same extension messages also appear in the
application's **Logs** panel, identified by extension and severity. Background
jobs record their start, stage changes and completion, failure or cancellation.

Extension work uses the shared status bar for progress. Simulation and project
opening take precedence while they are active; extension progress remains
tracked and becomes visible again afterward. Several extension operations can
run together. Their summary shows the slowest known progress, or a busy
indicator while any operation has an unknown total. Finishing one operation
does not clear another operation's progress. Use **Logs** for individual stage
details and the extension's **Cancel** control to stop its work.

Each extension can add its own named ribbon tabs and command groups, including
multiple tabs when needed. Command icons appear on ribbon buttons and in
collapsed group menus. Hiding or disabling one tab leaves the other tabs available.

The separate `openswmm.gui.data` extension adds **USGS time series…** and
**Elevation raster…** on its **Data** tab, with gage
and terrain icons. Its area chooser uses the current map viewport
or a chosen polygon from a loaded feature layer. Preview results respect polygon
holes and multiple parts. USGS previews create a temporary feature layer with
labels and the normal attribute-table, query, selection and styling tools.
Data-table filtering narrows the displayed population; native layer queries and
selection remain independent of the Data table's selected rows. Save selected
Data rows to add a persistent GIS layer with the reviewed area and response
provenance. Panning retains the completed viewport preview for inspection while
requiring another preview before saving under the changed area.

Use **Clear preview** beside **Preview** to remove the elevation footprints or
the temporary USGS site layer and clear the catalogue results. It also cancels a
pending preview so a late response cannot put the graphics back. Downloaded
files, imported Time Series collections and loaded rasters remain available.
The button is unavailable during a payload download or native collection import;
use **Cancel** to stop that operation first.

The Data download workflow selects a USGS measurement, daily/continuous values
and an inclusive start/end period. A completed download adds a native **Time
Series** collection layer with station locations and saved source attributes.
Use its layer/series context menu to inspect, filter or plot observations, or
right-click a station on the map. Nonspatial series can be used from the layer
tree and table. These layers remain usable when the Data plugin is disabled.

**Use in model…** prepares an explicit model-owned copy with reviewed units,
model-clock interpretation and missing/qualified-data handling. Creating a layer
or linking it to a feature does not change model forcing. The Time-Series Plots
and Data Objects manual chapters describe plotting and assignment in detail.

Terrain downloads retrieve selected GeoTIFF tiles, crop/mask a derived DEM to
the reviewed area and add that derived DEM as a normal raster layer. Raw files
and provenance remain in the chosen download folder. Empty, failed or cancelled
runs do not silently create incomplete layers; a closed/reloaded target reports
saved assets separately from layer-import success. Advanced details, filters,
cached previews and metadata-layer exports remain available separately.
Data previews, downloads and imports report their stages in the shared status
bar and record outcomes in **Logs**. A saved download and its addition to the
map are separate outcomes: **Saved, not added** means the files are retained
even though the original model could not accept a new layer.
Precipitation design storms retain their own implementation package.
The SDK and its documentation remain part of `openswmm.gui`.

Extension authors should read @ref gui_extension_sdk for the public API,
standalone CMake example, compatibility, ownership, threading and undo contracts.
