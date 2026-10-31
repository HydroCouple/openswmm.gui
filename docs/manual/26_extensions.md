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
Output messages include the full date, time and timezone. Progress completion
messages also report the operation's start time, finish time and duration in
seconds.

Extension work uses the shared status bar for progress. Simulation and project
opening take precedence while they are active; extension progress remains
tracked and becomes visible again afterward. Several extension operations can
run together. Their summary shows the slowest known progress, or a busy
indicator while any operation has an unknown total. Finishing one operation
does not clear another operation's progress. Use **Logs** for individual stage
details and the extension's **Cancel** control to stop its work.

Extensions choose whether to add ribbon controls. A toolbox tool can be used
without a custom ribbon tab. An extension can create its own named tabs or
contribute its own groups to a shared plugin tab. Plugins using the same shared
tab identity and caption appear together. Removing one contribution leaves the
other plugins' groups available. Command icons appear on ribbon buttons and in
collapsed group menus.

Data starts in **Toolbox only** mode. In **Preferences → Extensions — Data sources → Ribbon
placement**, choose **Shared Data tab** to place Data alongside other plugins
contributing to that tab, or **Separate Data tab** for its own tab. Apply changes
the placement immediately. The registered tools and their open panels remain
available through the Analysis Toolbox in every mode.

## Finding and opening tools

Use **View → Tools → Analysis Toolbox** on the ribbon, or **View → Panels →
Analysis Toolbox** in the menu, to browse tools grouped by extension
and toolset. Type in the search box to match tool names, descriptions and tags.
Use **Tags** to check categories graphically. A tool must match every checked
tag, so selecting **Download** and **Raster** narrows the list to matching raster
download tools. Remove a selected tag or use **Clear filters** to broaden the
list again. Matching tools retain their folder path.

Select a tool to inspect its description. Double-click it or press Enter to open
its panel. The toolbox, ribbon and command palette use the same command and
share the same panel instance. Dock, tabify or float the panel wherever it is
convenient. Closing its title bar hides it while preserving its controls and
active operation. Use **Cancel** to stop an operation. Opening a panel starts
no processing.

Tools become available when their extension loads. A plugin can contribute
tools to the toolbox while also providing its own ribbon tabs. Each registered
tool has at least one tag. Tool tags describe individual operations and may
be shared by tools from different plugins.

## Reviewing results and messages

The **History** tab in Analysis Toolbox lists runs accepted during the current
application session. A run begins when you explicitly start a preview,
download or analysis. Select a run to inspect its status, outputs and messages.
The corresponding tool panel offers the same information for that tool.
Each run shows **Started**, **Finished** and **Duration**. An active run displays
its elapsed time, which updates while the panel is visible. Timestamps include
the date and timezone so runs crossing midnight remain clear.
Messages carry their full timestamp and severity and also appear in the extension's
**Output** tab and the application's **Message Logs**.

Results identify the outputs that were actually produced. A saved file and a
layer added to a project have separate outcomes. **Saved, not added** reports a
retained file when the original project could not accept the layer. Output
actions become unavailable when a file has moved or its layer is detached.
History retains the explanation so you can inspect the result.

Use a running entry's **Cancel** control to stop that operation. Cancellation
is cooperative and may leave completed files listed in Results. Finishing or
cancelling one run preserves other active operations and their status-bar
progress. Session history retains up to 100 finished runs and 1,000 messages per
run, with a notice when messages are truncated. Active runs remain available
until settled. Downloaded files and their manifests remain available after
restarting the application.

## USGS observations and elevation

The separate `openswmm.gui.data` extension adds **USGS time series…** and
**Elevation raster…** under **Data sources → Download data** in Analysis Toolbox,
with gage and terrain icons. Its optional ribbon placement exposes the same
commands. Each tool has its own dockable panel, inputs and
preview, allowing gages and elevation footprints to be reviewed side by side.
Clearing one tool's preview leaves the other tool's preview and selection
available.

Drag a tool's title bar to move, tab or float its dock. Right-click the title bar
for explicit docking positions and a floating option. Tool content and
expanded **Results and messages** scroll within the available space, keeping
neighboring panels accessible.

Each tool's area chooser uses the current map viewport
or a chosen polygon from a loaded feature layer. Preview results respect polygon
holes and multiple parts. USGS site and elevation tile previews create a temporary
feature layer with the normal attribute-table, query, selection and styling tools;
USGS sites are labelled. Selection is shared: picking sites or tiles with the map
**Select** tool or in the attribute table selects the matching Data rows, and
selecting Data rows selects them on the map and in the attribute table. An
empty-space click with the **Select** tool clears both. Data-table filtering
narrows the displayed population; a native layer query does not narrow the Data
table. Save selected
Data rows to add a persistent GIS layer with the reviewed area and response
provenance. The area is frozen at **Preview**: panning, zooming or resizing the
map afterwards keeps the previewed rows available to download and save; preview
again to search the new view.

Use **Clear preview** beside **Preview** to remove the temporary site or tile
layer and clear the catalogue results. It also cancels a
pending preview so a late response cannot put the graphics back. Downloaded
files, imported Time Series collections and loaded rasters remain available.
The button is unavailable during a payload download or native collection import;
use **Cancel** to stop that operation first.

The Data download workflow selects a USGS measurement, daily/continuous values
and an inclusive start/end period, limited to the published record of the
selected sites. A completed download adds a native **Time
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
Under **Data sources → Precipitation**, **NOAA precipitation frequency…**
retrieves live Atlas 14 point estimates into memory. Enter a location or select
a georeferenced model gage, choose PDS/AMS and display units, then Fetch/Refresh.
The table and depth/intensity-duration-frequency charts show the accepted
estimate or its lower/upper bound. The accepted coordinate and retrieval time
remain visible. Clear releases the response; no NOAA response or disk cache is
created by retrieval or curve evaluation.

**Design storm builder…** shares that accepted table and compares generated
rainfall distributions as intensity, cumulative-depth or normalized cumulative
curves. Select a qualified method, frequency/depth, duration and recording
interval. NOAA temporal patterns are fetched live for an explicitly selected
region/area/duration. Manual-depth and custom cumulative patterns are also
available. Evaluation changes no model forcing. A regular rain-gage interval
must divide the storm duration evenly.

Both precipitation tools have a horizontal divider between the controls and
the chart/value tabs. Drag the divider vertically to resize these sections.
The controls scroll independently, including expanded source and model-target
sections; resizing keeps the accepted response and comparison candidates.

Expand the model target section, review the selected candidate, wall-clock start,
new time-series name and new/existing rain gage, then choose **Apply to model…**.
The host supplies the engine's actual rainfall units, independently of display
preferences. Applying creates the series, gage association and compact source/
method metadata in one undoable edit, directly from memory. Normal project save
retains the applied storm; reopening it needs no NOAA connection. A refresh
does not overwrite applied forcing. Raw NOAA frequency matrices and temporal
responses are omitted from project/settings/recovery persistence.
The SDK and its documentation remain part of `openswmm.gui`.

Extension authors should read @ref gui_extension_sdk for the public API,
standalone CMake example, compatibility, ownership, threading and undo contracts.
