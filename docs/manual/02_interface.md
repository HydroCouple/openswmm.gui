@page manual_interface 02 — The Interface

## What you'll do

Learn what every part of the SWMMVis window is and what every command does. This is
a reference chapter — use the tables to find a command, then follow the `\ref` link
to the chapter that explains the workflow behind it.

## Where to find it

The whole main window. Commands live in three places at once:

- the **menu bar**, which is almost complete — every capability has a menu home
  except a handful of Mesh 2D tools (**Pick 2D Cells**, **From Raster…**, **From
  Shapefile…**, **Aquifer…**, **Initial Conditions…**), which live only on the
  ribbon;
- the **ribbon**, a tabbed compact toolbar with captioned groups (Home, Model,
  Terrain, Mesh 2D, Analysis, Results, View);
- the **Command Palette** (`Ctrl+Shift+P`), which searches all of them by name.

Keyboard shortcuts listed here are the defaults; rebind any of them in
**Help → Keyboard Shortcuts…** (see \ref manual_shortcuts).

\figtodo{02_window_regions.png, The main window with the ribbon; tab strip; MDI workspace; docks and status bar called out}

## Step-by-step

### Window layout

```
┌───────────────────────────────────────────────────────────────────────┐
│ Menu bar  File Edit View Model Analysis Results Tools Window Help     │
├───────────────────────────────────────────────────────────────────────┤
│ Tab strip  [Home][Model][Terrain*][Mesh 2D*][Analysis][Results][View] │
│ Ribbon row  ┌ Project ┐┌ History ┐┌ Navigate ┐┌ Select ┐┌ Import ┐ …  │
├──────────────┬─────────────────────────────────────┬──────────────────┤
│  Layers      │  MDI workspace                      │  Object Browser  │
│  Layer       │  ┌ Welcome ┐┌ model_a ┐┌ model_b ┐  │  Properties      │
│  Styling     │                                     │  Section View    │
│              │        map canvas of the            │  Legend          │
│              │        active project               │                  │
├──────────────┴─────────────────────────────────────┴──────────────────┤
│  Attribute Table · Simulation Status · Message Logs                   │
├───────────────────────────────────────────────────────────────────────┤
│ Status bar  Engine | Flow Units | progress | Offset Mode | Auto-Length │
│             | Coordinates | Map Scale | Coordinate Reference System   │
└───────────────────────────────────────────────────────────────────────┘
```

`*` marks contextual ribbon tabs, which appear only when the active project
contains the data they act on.

Docks may be nested, tabbed together, dragged as a group, or floated; the layout is
saved on exit and restored at the next launch. Right-clicking an empty part of the
ribbon row opens a short menu of the eight dock toggles — the ribbon rows themselves
are managed by the tab strip and cannot be hidden individually.

### The ribbon

The ribbon is one row of buttons under a tab strip. Only the current tab's toolbar
is visible; the tab you were last on is restored at the next launch
(`SWMMVis::MainWindow/CompactToolbarTab`).

Buttons are grouped into captioned **ribbon groups** — a caption under a row of
buttons, closed by a vertical rule, in the ArcGIS Pro idiom.

#### Ribbon tabs and groups

| Tab | Groups (left to right) |
|---|---|
| **Home** | **Project** (New; Open; Save) · **History** (Undo; Redo) · **Navigate** (Pan; Zoom In; Zoom Out; Full Extent; Zoom to Selection) · **Select** (Select ▾ split button; Invert; Upstream; Downstream) · **Inspect** (Measure; Search; Copy) · **Import** (SWMM Output; 2D Results; Vector Data; Raster Data; Web Layers; Delimited; Basemap; 2D Mesh) · **Run** (Execute; Pause; Cancel) |
| **Model** | **Select** · **Edit** (Edit Existing) · **Nodes** (Junction; Virtual Junction; Inlet Junction; Outfall; Flow Divider; Storage) · **Links** (Pipe; Pump; Orifice; Weir; Outlet) · **Subcatchments** · **Rain Gages** · **Annotation** (Text) · **Climate** (split button over Temperature; Wind; Snow; Evaporation; Solar Radiation) · **Data Objects** (Time Series; Curve; Pattern; Control Rule; Transect; Street; Inlet; Aquifer; LID Control; Pollutant; Land Use; Reaction System; Heat) · **Setup** (Simulation Options; User Flags; Import Feature Layer) · **Tools** (Assign Rain Gages) · **Mesh 2D** (Generate Mesh) |
| **Terrain** *(contextual)* | **Active Terrain** · **Vertical Units** · **Invert Offsets** · **Profile** |
| **Mesh 2D** *(contextual)* | **Mesh** (Generate Mesh) · **Cell Data** (From Raster; From Shapefile) · **Groundwater (2D)** (Aquifer Parameters; Initial Conditions) — plus the Mesh Editing toolbar's own groups: **Mesh** · **Vertices** · **Edges** · **2D Results** · **Profile** · **Coupling** |
| **Analysis** | **Select** · **Results Layers** (the 1D and 2D results pickers) · **Report** (Summarize; Report; Tabular View) · **Plots** (Time Series; Profile; 2D Profile) · **Network Analysis** (Flow Balance Downstream/Upstream; Travel Time Downstream/Upstream; Mass Balance) |
| **Results** | **Playback** (Skip Back; Skip Forward; Play; Pause; Stop) · **Timeline** (scrubber; **Window:** look-back; time cursor; **Speed:**; **Cycle**) · **Display** (Show Legend; Set Style; **Live 2D**; **Live 1D**) |
| **View** | **Panels** (the eight dock toggles) · **Styling** (Layer Styling; Styles) · **Start** (Welcome) |

\figtodo{02_ribbon_home_tab.png, The Home tab with its Project; History; Navigate; Select; Inspect; Import and Run groups}

\figtodo{02_ribbon_model_tab.png, The Model tab showing the Nodes; Links and Data Objects groups}

#### Contextual tabs

| Tab | Appears when |
|---|---|
| **Terrain** | The active project's canvas holds at least one raster layer |
| **Mesh 2D** | The active project's canvas holds a 2D mesh layer or a 2D results layer |

Both are hidden again as soon as the last qualifying layer is removed, and they
follow the active project tab.

#### Split buttons

Two groups use **last-used split buttons**: **Select** on the Home tab (Select /
Select by Polygon) and **Climate** on the Model tab. The button's face mirrors the
member you used last — including its checked state, so pressing `Esc` to fall back
to the Select tool is visible on the ribbon — and the arrow opens the family menu.
The remembered member persists under `SWMMVis::Ribbon/LastUsed/<family>`.

#### Compact mode

Each ribbon row watches its own width. As the window narrows, groups shed detail
from the trailing end first, in three steps:

| Mode | Appearance |
|---|---|
| **Full** | Large icons with the label under each icon; caption shown |
| **Compact** | Small icon-only buttons; caption still shown |
| **Collapsed** | The whole group becomes one popup button titled by its caption |

Groups are promoted back only after the row has 32 px of slack, so dragging the
window edge across a threshold does not make the ribbon flicker. Groups that host
widgets rather than buttons — **Results Layers**, **Timeline**, the Terrain groups —
never collapse. The **Command Palette** launcher sits at the right end of the tab
strip.

\figtodo{02_ribbon_compact_modes.png, The same ribbon row at three window widths showing Full; Compact and Collapsed groups}

#### Mesh Editing toolbar

Shown on the **Mesh 2D** tab beside the Mesh 2D groups.

| Group | Contents |
|---|---|
| **Mesh** | **Mesh:** active-mesh picker · **Z:** live elevation readout interpolated inside the triangle under the cursor |
| **Vertices** | Select Vertex toggle; vertex info label; Z spin box for retyping the picked vertex's elevation |
| **Edges** | Select Edge toggle; edge info label; the boundary-condition editor pages; **Browse Object** |
| **2D Results** | Cell picking; the cell info label; the per-cell parameter page; **Assign Infiltration to Selection…** |
| **Profile** | The mesh profile-plot tool |
| **Coupling** | **Auto-couple** and **Re-map** 1D nodes onto the active mesh |

See \ref manual_2d_mesh.

#### Terrain toolbar

Shown on the **Terrain** tab.

| Group | Controls | What it does |
|---|---|---|
| **Active Terrain** | Raster picker | Chooses the DEM used for Z sampling and invert estimation |
| **Vertical Units** | **DEM:** unit combo (m / ft); conversion label; **×** factor spin box | `ModelZ = DEM_Z × factor` — auto-detected from the raster CRS and the project flow units; override when the detection is wrong |
| **Invert Offsets** | **Node Δ:** and **Link Δ:** spin boxes with unit labels | `InvertElev = DEM_Z × factor + Δ`, signed, in model vertical units |
| **Profile** | Terrain profile tool | Traces a terrain profile — see \ref manual_profile_plots |

### The menu bar

Every table below lists the menu's items in the order they appear, including the
items that are added at run time.

#### File

| Item | Shortcut | What it does |
|---|---|---|
| **New** | `Ctrl+N` | Creates a blank untitled project immediately from your preference defaults — no dialog. See \ref manual_projects |
| **Open** | `Ctrl+O` | Opens a `.inp`; `.gpkg` or `.oswp` |
| **Open Recent ▸** | | Up to 20 previously opened files by short name — full path on the tooltip — followed by **Clear Recent Files** |
| **Save** | `Ctrl+S` | Writes the model back to its own file |
| **Save As…** | `Ctrl+Shift+S` | Writes to a new path; the format comes from the file-type dropdown |
| **Import ▸** | | See the sub-table below |
| **Map Image…** | | Exports the active map view. PNG is written; SVG / DXF / EMF are offered but not implemented and report so |
| **Print** | `Ctrl+P` | Prints the active view |
| **Exit** | `Ctrl+Q` | Quits — each dirty or untitled project gets its close prompt first. On macOS Qt moves this to the application menu |

**File → Import ▸**

| Item | What it adds | Chapter |
|---|---|---|
| **Open SWMM Output** | A SWMM binary `.out` results layer | \ref manual_results |
| **Add 2D Results…** | A SWMM 6 `.h5` 2D results file | \ref manual_results |
| **Add Vector Data** | Any GDAL/OGR vector source | \ref manual_layers |
| **Add Raster Data** | Any GDAL raster source | \ref manual_layers |
| **Add WMS Data** | A WMS / WMTS / WCS service layer | \ref manual_layers |
| **Add WFS Data** | Features from a Web Feature Service | \ref manual_layers |
| **Add Basemap** | A stored XYZ / tile basemap; its **Local File** tab also adds local raster basemaps | \ref manual_layers |
| **Add Delimited Data** | A delimited-text table — as points or as a tabular layer | \ref manual_attribute_tables |
| **Add 2D Mesh…** | An existing `.2dm` mesh | \ref manual_2d_mesh |

#### Edit

| Item | Shortcut | What it does |
|---|---|---|
| **Undo** | `Ctrl+Z` | Undoes the last map/model edit on the **active project's** stack |
| **Redo** | `Ctrl+Shift+Z` / `Ctrl+Y` | Redoes it |
| **Copy** | `Ctrl+C` | Copies the active view to the clipboard |
| **Select** | | Activates the click / rubber-band selection tool |
| **Select by Polygon** | | Selects everything inside a polygon you draw |
| **Invert Selection** | | Selects what was not selected |
| **Select Upstream** | | Traces the network upstream of the selection |
| **Select Downstream** | | Traces the network downstream of the selection |
| **Search** | `Ctrl+F` | Opens the attribute/name search |
| **Edit Existing** | `Ctrl+E` | Toggles the edit session. Must be on to move; reshape or delete existing geometry; adding new features does not need it |

See \ref manual_selection and \ref manual_map_editing.

#### View

| Item | Shortcut | What it does |
|---|---|---|
| **Zoom In** | `Ctrl++` | Zoom-in tool (checkable) |
| **Zoom Out** | `Ctrl+-` | Zoom-out tool (checkable) |
| **Zoom Extent** | `Ctrl+Shift+F` | Fits every visible layer |
| **Zoom To Selection** | `Ctrl+Shift+J` | Fits the union of the selected objects |
| **Pan** | | Pan tool (checkable) |
| **Measure** | `Ctrl+Shift+M` | Measures distances and areas on the canvas |
| **Show Legend** | | Toggles the draggable legend overlay on the active canvas |
| **Panels ▸** | | The eight dock toggles — see the table below |
| **Layer Styling Dock** | `Ctrl+Alt+8` | Shows or hides the live layer-styling editor; opening it loads the layer selected in **Layers** |
| **Appearance ▸** | | **System** / **Light** / **Dark**; mirrors the Preferences Appearance page |
| **Command Palette…** | `Ctrl+Shift+P` | Opens the palette |

#### Model

| Item | What it does | Chapter |
|---|---|---|
| **Add Node ▸** | Add Junction · Add Virtual Junction (splits a conduit at the pick) · Add Inlet Junction (splits a street conduit) · Add Outfall · Add Flow Divider · Add Storage | \ref manual_map_editing |
| **Add Link ▸** | Add Pipe · Add Pump · Add Orifice · Add Weir · Add Outlet | \ref manual_map_editing |
| **Add Subcatchment** | Draws a subcatchment polygon | \ref manual_hydrology |
| **Add Rain Gauge** | Places a rain gage | \ref manual_hydrology |
| **Climate ▸** | Temperature · Evaporation · Wind · Snow · Solar Radiation — all five open the tabbed Climatology dialog on the matching tab (Solar Radiation opens the Evaporation tab, since solar feeds Hargreaves ET) | \ref manual_climate |
| **Add Text** | Places a text annotation | \ref manual_map_editing |
| **Assign Rain Gages…** | Binds gages to subcatchments spatially — Thiessen area majority or natural-neighbour weights — previewed first and applied as one undo step | \ref manual_hydrology |
| **Data Objects ▸** | See the sub-table below | \ref manual_data_objects |
| **Import Feature Layer…** | Converts GIS features into SWMM objects with column mapping and update/skip handling | \ref manual_map_editing |
| **Simulation Options…** | The paged `[OPTIONS]` editor | \ref manual_simulation_options |
| **Water Age Sources...** | Initial age of water entering by each source pathway (`WATER_AGE_SOURCES`) | \ref manual_water_quality |
| **Initial Quality...** | Per-node and per-link initial concentrations (`INITIAL_QUALITY`) | \ref manual_water_quality |
| **Reaction System...** | Species; kinetics and the `.rxn` file (`REACTION_*`) | \ref manual_water_quality |
| **Heat Configuration...** | `[HEAT_SOURCES]`; `[HEAT_FLUXES]` and radiative / solar / cloud forcing | \ref manual_water_quality |
| **User Flags…** | Defines the `[USER_FLAGS]` schema | \ref manual_projects |
| **Generate Mesh…** | Opens the 2D mesh generator. A DTM raster is optional — without one; vertex elevations come from junction rim elevations | \ref manual_2d_mesh |
| **Set Project CRS…** | Chooses or changes the project's coordinate reference system | \ref manual_crs |
| **Mesh ▸** | Select Vertex · Select Edge · Assign Infiltration to Selection… — menu mirrors of the Mesh Editing toolbar tools | \ref manual_2d_mesh |
| **Terrain Profile** | Traces a terrain profile | \ref manual_profile_plots |

**Model → Data Objects ▸** — every entry opens that category's editor in *browse*
mode: nothing is created automatically; use the editor's own **Add**/**New**
button. Categories without an editor in this build are shown disabled with a
tooltip naming what is missing.

| Item | Editor |
|---|---|
| **Time Series…** | `[TIMESERIES]` — inline records or an external file |
| **Curves…** | `[CURVES]` — storage; diversion; rating; shape; control; tidal and the five pump types |
| **Time Patterns…** | `[PATTERNS]` |
| **LID Controls…** | `[LID_CONTROLS]` |
| **Pollutants…** | `[POLLUTANTS]` |
| **Land Uses…** | `[LANDUSES]`; `[BUILDUP]`; `[WASHOFF]` |
| **Aquifers…** | `[AQUIFERS]` |
| **Snowpacks…** | `[SNOWPACKS]` |
| **Control Rules…** | `[CONTROLS]` |
| **Transects…** | `[TRANSECTS]` |
| **Streets…** | `[STREETS]` |
| **Inlets…** | `[INLETS]` |
| **Unit Hydrographs…** | `[HYDROGRAPHS]` |

#### Analysis

| Item | Shortcut | What it does | Chapter |
|---|---|---|---|
| **Execute** | `Ctrl+R` | Runs the active project | \ref manual_running |
| **Pause Execution** | | Pauses the selected run — checkable; toggle again to resume | \ref manual_running |
| **Cancel Execution** | `Ctrl+.` | Pauses; then asks whether to stop. Stopping flushes whatever the engine produced to `.out` / `.rpt` and loads the partial results | \ref manual_running |
| **Summarize Results** | | The run summary | \ref manual_analysis_tools |
| **Report** | | The two-panel report viewer over the project's `.rpt` | \ref manual_running |
| **Tabular View** | `Ctrl+Shift+A` | Raises the **Attribute Table** dock and focuses it — it is not a separate window | \ref manual_tabular_results |
| **Plot Time Series** | `Ctrl+T` | Plots the selection; or arms a one-shot map pick | \ref manual_time_series_plots |
| **Plot Profile** | `Ctrl+Shift+T` | Picks a path of nodes and links and plots the HGL profile | \ref manual_profile_plots |
| **Plot 2D Profile** | | Draws a polyline across the 2D mesh: terrain plus the active 2D results layer's animated depth and maximum-depth envelope | \ref manual_profile_plots |
| **Rainfall Visualization…** | | Compares every rain gage's series on one chart | \ref manual_analysis_tools |
| **Flow Balance Downstream** | | Flow balance over the downstream sub-network | \ref manual_analysis_tools |
| **Flow Balance Upstream** | | Flow balance over the upstream sub-network | \ref manual_analysis_tools |
| **Travel Time Downstream** | | Travel time downstream of the selection | \ref manual_analysis_tools |
| **Travel Time Upstream** | | Travel time upstream of the selection | \ref manual_analysis_tools |
| **Show Mass Balance** | | Wired to the same handler as **Report** — opens the same report viewer; the continuity ledgers are sections of the `.rpt`. There is no separate mass-balance dialog | \ref manual_running |

#### Results

| Item | What it does |
|---|---|
| **Play** | Starts map animation (checkable) |
| **Pause** | Pauses it (checkable) |
| **Stop** | Stops and rewinds (checkable) |
| **Backward** | Steps one frame back |
| **Forward** | Steps one frame forward |
| **Set Style** | Opens the layer style dialog for the layer selected in **Layers**; falls back to the topmost visible layer |

See \ref manual_results.

#### Tools

| Item | Shortcut | What it does |
|---|---|---|
| **Plugins…** | | Read-only listing of every file-format filter — built-in and engine-discovered. See \ref manual_plugins |
| **Style Manager…** | | Browse; apply; save; import and export saved Rule Lists from the per-user style library. See \ref manual_styling |
| **Preferences…** | `Ctrl+,` | The Preferences dialog. On macOS Qt moves this into the application menu. See \ref manual_preferences |

#### Window

Built at run time, so the middle of the menu changes as you work.

| Item | Shortcut | What it does |
|---|---|---|
| **Minimize** | `Ctrl+M` | Minimizes the main window. MDI sub-windows are deliberately not minimizable |
| **Zoom** | | Toggles the main window between maximized and normal |
| *(project list)* | | One checkable entry per open project sub-window; the active one is checked. Selecting one activates and focuses it |
| *(open dialog list)* | | One entry per open modeless dialog; most recently used first. Selecting one first clamps the dialog back onto a connected screen; then raises it — this is the recovery path for a dialog left on a monitor you have since unplugged |
| **Bring All to Front** | | Raises every project window; then the main window; then the dialogs |
| **Reset Window Positions** | | Discards saved geometry for the main window and every named dialog and gathers all open windows back onto the current screen. Dock layout; splitter sizes and header state are left alone |

#### Help

| Item | Shortcut | What it does |
|---|---|---|
| **Show Welcome Page** | | Re-opens the Welcome tab |
| **Help** | `F1` (platform help key) | Present in the menu but **not connected to anything** in this build — it does nothing |
| **Keyboard Shortcuts…** | | Opens Preferences on the **Keyboard** page |
| **About** | | The About dialog with the component and licence browser. On macOS Qt moves this to the application menu |

\figtodo{02_menu_model_expanded.png, The Model menu expanded with the Add Node; Add Link; Climate; Data Objects and Mesh submenus}

### The MDI workspace and its tabs

The centre of the window is a tabbed MDI area. Each tab is one document:

- the **Welcome** tab (see \ref manual_introduction);
- one **project window** per open model, titled by the file name, with a trailing
  `*` while it has unsaved changes.

Tabs are movable and closable, and the close **×** is forced to the right of the
tab label on every platform. Closing the Welcome tab only hides it. Closing a
project tab runs that project's save prompt.

Each project window owns its own map canvas, model layer, selection manager, undo
stack, results layers and engine handle. Switching tabs re-binds the ribbon, every
dock, and the status-bar widgets to the newly active project — including its flow
units, offset mode, engine version and CRS. Two projects with different unit
systems can therefore be open side by side without interfering.

The MDI backdrop and the Welcome tab track the application theme
(`mdiworkspacechrome`), so the workspace does not flash a light panel in dark mode.

### Dock panels

Eight docks have toggle actions in **View → Panels ▸** and on the **View** ribbon
tab; the Layer Styling dock has its own View-menu entry.

| Panel | Shortcut | What it shows | Chapter |
|---|---|---|---|
| **Layers** | `Ctrl+Alt+1` | The layer tree — ordered; groupable; per-layer visibility and opacity; filter box. Clicking a SWMM kind row focuses that category in the Object Browser | \ref manual_layers |
| **Object Browser** | `Ctrl+Alt+2` | Every SWMM object grouped by category; with per-category ordering and the **Add New…** entry points | \ref manual_object_browser |
| **Properties** | `Ctrl+Alt+3` | The property grid for the current selection. Its title tracks what is selected — *Properties — &lt;name&gt;* or *Properties — &lt;kind&gt; (N features)* | \ref manual_object_browser |
| **Attribute Table** | `Ctrl+Alt+4` | The tabular view of a layer's features; with the attribute calculator and selection sync | \ref manual_attribute_tables |
| **Legend** | `Ctrl+Alt+5` | The docked legend for the active canvas's layers | \ref manual_styling |
| **Simulation Status** | `Ctrl+Alt+6` | One row per running or finished simulation with progress; selecting a row targets Pause / Stop at that job | \ref manual_running |
| **Message Logs** | `Ctrl+Alt+7` | Timestamped Information / Warning / Error messages from the GUI and the engine; auto-scrolled to the newest row | \ref manual_running |
| **Layer Styling** | `Ctrl+Alt+8` | The always-open variant of the Symbology tab; edits apply live to the canvas and follow the layer selected in **Layers** | \ref manual_styling |
| **Section View** | `Ctrl+Alt+9` | The cross-section drawing for the selected link or transect | \ref manual_object_browser |

\figtodo{02_docks_default_layout.png, The default dock arrangement with Layers on the left; Object Browser and Properties on the right and Message Logs at the bottom}

Two further items exist in the code but are **not reachable from the UI in this
build** and are listed here only so you do not go looking for them: an **Overview
Map** navigator panel (`overviewmappanel.h`), which is compiled but never created;
and an **Analysis Toolbox** dock declared in the main-window form, which is never
populated and has no toggle action. Use \ref manual_map_navigation for the
navigation workflows the overview map would have served.

### Status bar

Left to right, all of these are permanent widgets on the right-hand side of the
bar; transient messages (*Opening &lt;file&gt;…*, tool hints) appear on the left.

| Widget | What it does | Writes |
|---|---|---|
| **Engine:** | Picks the engine used to run the **active project**: *OpenSWMM 6.0.0-alpha.4* or *SWMM 5.3.0 (Legacy)*. Disabled until a project is open | Per-project engine selection |
| **Flow Units:** | `CFS` · `GPM` · `MGD` · `CMS` · `LPS` · `MLD`. Changing it re-labels every unit suffix in the whole UI | `[OPTIONS] FLOW_UNITS` |
| *progress* | A stage label plus a bar; arbitrated between simulation runs; project opens and the plain busy spinner |  |
| **Offset Mode:** | `Depth [toggle] Elevation` — the active side is bold. Flipping it offers to convert existing link offsets, exactly as EPA SWMM does; the prompt is skipped for a model with no links | `[OPTIONS] LINK_OFFSETS` |
| **Auto-Length:** | `On` / `Off`. When on; conduit lengths recompute from the polyline on every node move or vertex edit | `[CONDUITS]` length |
| **Coordinates:** | Live cursor position in canvas CRS units; with a `Z:` suffix when a terrain raster can be sampled |  |
| **Map Scale:** | Editable combo — pick a preset or type `1:N` |  |
| **Coordinate Reference System:** | A button showing the canvas CRS authority code; click to open the CRS picker | Project CRS — see \ref manual_crs |

\figtodo{02_status_bar.png, The status bar with the Engine; Flow Units; Offset Mode; Auto-Length; Coordinates; Map Scale and CRS widgets}

### Command Palette

`Ctrl+Shift+P`, or the launcher at the right end of the ribbon tab strip.

A frameless popup with a filter box and a relevance-sorted list of every registered
command — dock toggles and dialog launchers included. Type to filter; `↑`/`↓` to
move; `Enter` to trigger or toggle the selected command; `Esc`, or clicking away,
to dismiss. Commands that are currently disabled are greyed and cannot be
triggered, which makes the palette a quick way to find out *why* something is
unavailable.

\figtodo{02_command_palette.png, The Command Palette filtered to a few matching commands}

\videotodo{A tour of the window — switching ribbon tabs; revealing the contextual Mesh 2D tab; rearranging docks and finding a command with the Command Palette}

### Keyboard shortcuts

**Help → Keyboard Shortcuts…** opens Preferences on the **Keyboard** page, which
hosts the shortcut editor.

| Control | What it does |
|---|---|
| Filter box | Narrows the command tree |
| Command tree | Every registered command; grouped by category; showing its current binding |
| Key-sequence editor | Records the new binding you press |
| **Assign** | Applies the recorded sequence — live and persisted immediately; there is no separate Apply |
| **Clear** | Removes the binding |
| **Reset** | Restores the selected command's default |
| **Reset All** | Restores every default |
| Conflict label | Explains why an assignment was blocked or warned about |

Conflict policy: an exact duplicate of another command's binding **blocks**
Assign and names the command that holds it; a sequence on the platform-reserved
list only produces a warning. Overrides are stored under `SWMMVis::Shortcuts`,
keyed by the command's stable id, so they survive upgrades. The full default list
is in \ref manual_shortcuts.

\figtodo{02_shortcut_editor.png, The Keyboard page of Preferences with a command selected and its key-sequence editor}

### Theme

**View → Appearance ▸** and **Preferences → Appearance** are two views of one
setting.

| Mode | Behaviour |
|---|---|
| **System** | Follows the operating system's light/dark appearance; live |
| **Light** | Always light |
| **Dark** | Always dark |

The theme is a token-driven palette plus a small style-sheet overlay on top of Qt's
Fusion style, and action icons are re-tinted through a theme-aware icon factory, so
glyphs stay legible in both schemes. Changing the mode in either place updates the
other immediately.

### Drag and drop

Drop `.inp` or `.oswp` files from your file manager anywhere on the main window and
they open as projects — several at once if you drop several. Files of other types
are rejected by the drag itself, so the cursor tells you before you let go. To add
GIS data to an open project, use **File → Import ▸** rather than dropping.

### Context menus

**On the map canvas.** There is no general canvas context menu. Right-click is
handled by the active tool: with **Select** or **Edit Vertex** active, right-clicking
a link offers **Delete vertex** / **Delete this vertex** on a vertex and **Insert
vertex here** on a segment; with the mesh vertex tool active it reports the picked
vertex's **Elevation Z**. See \ref manual_map_editing.

**On a layer row in the Layers panel.**

| Group | Items |
|---|---|
| Navigation | **Zoom to Layer** · **Show in Overview** |
| Data | **Open Attribute Table** · **Show Feature Count** · **Plot Time Series…** · **Set as Active Results Layer** (results layers only) |
| Order | **Move to Top** · **Move Up** · **Move Down** · **Move to Bottom** |
| Layer | **Rename Layer** · **Duplicate Layer** · **Remove Layer** |
| Display | **Filter…** · **Set Layer Scale Visibility…** · **Hide Layer** / **Show Layer** |
| Styling | **Properties…** · **Styles ▸** (**Edit Symbology…** · **Copy Style** · **Paste Style**) |

On a **category header** row: **Move Category Up** · **Move Category Down**. On a
SWMM **kind** sub-row: **Show**/**Hide &lt;kind&gt;** · **Properties…** · a list of
that kind's features with **Plot timeseries…**, truncated with a *… +N more (use
Object Browser)* entry.

**On the Object Browser.**

| Row type | Items |
|---|---|
| Category header | **Add New…** · **Sort Category A→Z** · **Reset Category to Default Order** |
| Object | **Edit…** · **Properties…** · **Delete…** · **Move to Top** / **Move Up** / **Move Down** / **Move to Bottom** · **Reset to Default Order** |

Object types whose editor is not yet implemented show **Delete** disabled with an
explanatory tooltip. See \ref manual_object_browser.

## Tips and gotchas

- **The menu bar is very nearly the complete surface.** Several commands are
  menu-only by design; the only ribbon-only ones are the Mesh 2D tools listed
  under *Where to find it* above.
- **A greyed command usually means "no project" or "no edit session."** Add-feature
  tools need an open project; move, reshape and delete need **Edit Existing** on.
- **`Esc` returns to the Select tool** and the ribbon's Select split button shows
  it, because the split button mirrors its member's checked state.
- **Pause and Stop act on the run selected in Simulation Status.** With several
  runs in flight and no selection, they refuse and tell you to pick one.
- **Dock layout is saved with a version stamp.** After an upgrade that changes the
  toolbar structure, an incompatible saved layout is discarded and the default
  layout is used — that is expected, not a fault.
- **If a dialog disappears**, look in **Window** — every open modeless dialog is
  listed there and selecting it pulls it back onto a connected screen. **Reset
  Window Positions** is the blunt instrument for the same problem.

## Related

- \ref manual_introduction — installation and first launch
- \ref manual_projects — projects; saving; undo and multiple open models
- \ref manual_preferences — appearance; shortcuts and defaults
- \ref manual_shortcuts — the default shortcut list
- \ref manual_map_navigation — the canvas tools in depth
- \ref manual_layers — the Layers panel and every data source
- \ref manual_object_browser — the Object Browser; Properties and Section View
- \ref manual_running — the Simulation Status and Message Logs docks in use
- \ref manual_results — the Results tab and map animation
