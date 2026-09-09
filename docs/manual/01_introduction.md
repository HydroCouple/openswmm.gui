@page manual_introduction 01 — Introduction and Getting Started

## What you'll do

Find out what SWMMVis is, how it relates to the OpenSWMM engine and to EPA SWMM 5,
install it, launch it for the first time, work through the Welcome page, and open
your first model.

## Where to find it

| Thing | Where |
|---|---|
| The application | macOS: `SWMMVis.app`; Windows: Start menu entry; Linux: desktop entry `org.openswmm.swmmvis.desktop` (categories *Education / Science*) |
| Welcome page | The first tab in the MDI workspace; reopen with **Help → Show Welcome Page** (ribbon **View → Start → Welcome**) |
| License agreement | Shown automatically at launch; re-enable it in **Tools → Preferences… → General** |
| Bundled examples | Welcome page, **Example Projects** |
| About and credits | **Help → About** |

\figtodo{01_main_window_annotated.png, The SWMMVis main window after opening a model — ribbon; map canvas; Layers; Object Browser; Properties and Message Logs}

## Step-by-step

### What SWMMVis is

**SWMMVis** (repository `openswmm.gui`) is a Qt 6 / C++20 desktop application for
building, running, and analysing storm-water models. It is a ground-up replacement
for the legacy Delphi EPA SWMM GUI (`epaswmm5.exe`), built directly on the
[OpenSWMM engine](https://github.com/HydroCouple/openswmm.engine) rather than
driving it as an external executable.

Two things distinguish it from the classic SWMM interface:

- **It is a GIS application.** The centre of every project window is a map canvas
  with a real coordinate reference system, on-the-fly reprojection, and layers:
  vector and raster GIS data, WMS/WMTS/WCS and WFS services, XYZ basemaps,
  delimited-text tables, SWMM model layers, SWMM results layers, 2D meshes, and 2D
  results. See \ref manual_layers and \ref manual_crs.
- **It exposes the whole SWMM 6 engine.** Everything the engine can do —
  finite-volume 1D routing, 2D overland flow on unstructured meshes, 1D–2D
  coupling, advection–reaction–dispersion transport, multispecies reaction
  systems, heat transport, water age, groundwater exchange, streets and inlet
  junctions — is reachable from dialogs, tables, plots, and animated maps rather
  than by hand-editing the `.inp` file.

The application is licensed GPL v3; the engine it links is Apache 2.0. Third-party
components and their licences are listed in **Help → About**.

### Relationship to the OpenSWMM engine

SWMMVis is *not* a standalone solver. It holds an in-process engine handle
(`SWMM_Engine`) per open project and drives it through the engine's C API — the
model you see in the Object Browser, the Properties panel, and the attribute tables
*is* the engine's live state, not a parsed copy of a text file. That is why
changing **Flow Units** in the status bar immediately re-labels every units suffix
in every open editor.

The build this manual documents ships engine **6.0.0-alpha.4**. The status bar's
**Engine:** picker offers two engines per project:

| Entry | What it runs |
|---|---|
| **OpenSWMM 6.0.0-alpha.4** | The refactored SWMM 6 engine — required for finite-volume 1D routing; 2D overland flow; 1D–2D coupling; transport, reaction and heat modules; semi-implicit node continuity; Anderson acceleration |
| **SWMM 5.3.0 (Legacy)** | The unmodified EPA SWMM 5.x solver preserved inside the engine repository — for regression comparison against classic SWMM results |

The default for new projects is set in **Preferences → General → Default engine
mode**; the status-bar picker overrides it per project. Options that only the
refactored engine understands are hidden or disabled when the legacy engine is
selected — see \ref manual_simulation_options.

> **Pre-release.** 6.0.0-alpha.4 is an alpha. The `.oswp` project format, the
> preference keys, and the window layout may still change before 6.0.0.

### EPA SWMM 5 compatibility

SWMMVis reads and writes standard SWMM input files. A `.inp` written by EPA SWMM
5.2 opens directly; a `.inp` written by SWMMVis is a normal SWMM input file that
EPA SWMM 5 will read, *except* for sections SWMM 5 does not define — for example
`[2D_OPTIONS]`, `[2D_MESH_FILE]`, `[REACTION_*]`, `[HEAT_SOURCES]`,
`[HEAT_FLUXES]`, `[RDII_DECAY]`, and `[USER_FLAGS]`. SWMM 5 ignores or rejects
those, so a model that uses SWMM 6 capabilities is not round-trippable to 5.x.

Two SWMM 5 conventions are preserved deliberately:

- **`LINK_OFFSETS`** — the status bar's **Offset Mode** toggle switches between
  `DEPTH` and `ELEVATION` and, exactly like EPA SWMM, offers to convert existing
  link offsets when you flip it (the prompt is skipped when the model has no
  links).
- **Flow units** drive the whole unit system. `CFS`, `GPM` and `MGD` select US
  customary; `CMS`, `LPS` and `MLD` select SI.

Results files are the standard SWMM binary `.out` (opened with **File → Import →
Open SWMM Output**) plus the SWMM 6 `.h5` 2D results file. See
\ref manual_file_formats.

### Capability overview

Each paragraph below names the chapter that covers it in full.

**Map, navigation and printing.** Pan, zoom, zoom-to-extent and zoom-to-selection,
a measure tool, live cursor coordinates, an editable map-scale box, and map image
export. Export currently writes **PNG**; SVG, DXF and EMF appear in the file-type
list but are not implemented yet and report so. See \ref manual_map_navigation.

**Coordinate reference systems.** Every layer and every canvas carries a CRS;
layers reproject on the fly. A model whose `.inp` has no CRS is prompted for one at
open time, with a one-click "local projected" fallback in the model's own linear
unit. See \ref manual_crs.

**Layers and data sources.** Vector and raster files through GDAL, WMS/WMTS/WCS,
WFS, XYZ tiles and stored basemaps, delimited text, SWMM model layers, SWMM
results, `.2dm` meshes and `.h5` 2D results — all in one ordered, groupable layer
tree with per-layer opacity, filters and scale-visibility. See \ref manual_layers.

**Symbology and legends.** Per-layer renderers, rule lists, colour ramps, labels
and label expressions, a per-user style library (**Tools → Style Manager…**), a
live-editing **Layer Styling** dock, a docked **Legend** and a draggable on-canvas
legend overlay. See \ref manual_styling.

**Selection and network tracing.** Click, rubber-band and polygon selection,
attribute search, invert selection, and upstream/downstream network tracing. See
\ref manual_selection.

**Browsing and editing model data.** The Object Browser groups every SWMM object by
category; the Properties panel edits the selection; compound editors handle
multi-part properties; the Section View draws cross-sections. See
\ref manual_object_browser and \ref manual_attribute_tables.

**Drawing the network.** Add junctions, virtual junctions, inlet junctions,
outfalls, flow dividers, storage units, conduits, pumps, orifices, weirs, outlets,
subcatchments, rain gages and text annotations directly on the map; move, reshape
and delete existing geometry inside an edit session; import features from a GIS
layer as SWMM objects. See \ref manual_map_editing.

**Hydrology.** Rain gages, subcatchments, infiltration, LID controls, snowpacks,
aquifers and groundwater, unit hydrographs and RDII. See \ref manual_hydrology.

**Hydraulics.** Nodes and links, cross-sections and transects, streets and HEC-22
inlets, control rules, and inflows. See \ref manual_hydraulics.

**Climatology.** Temperature, evaporation, wind, snowmelt and areal depletion, and
solar radiation, all in one tabbed dialog. See \ref manual_climate.

**Water quality and transport.** Pollutants, land uses, build-up/wash-off,
treatment, initial quality, multispecies reaction systems, water age and heat
transport. See \ref manual_water_quality.

**Data objects.** Time series, curves, time patterns, control rules, transects,
streets and inlets, each with its own MVC editor; observed calibration series are
loaded in the comparison plot. See \ref manual_data_objects.

**Simulation options.** Every `[OPTIONS]` key and the `[FILES]` external-file slots
on a paged dialog. See \ref manual_simulation_options.

**2D overland flow.** Mesh generation from terrain, mesh editing, per-cell
parameter assignment from rasters or shapefiles, edge boundary conditions, and
1D↔2D coupling. See \ref manual_2d_mesh.

**Running simulations.** Run, pause and stop with partial-result flushing; a
Simulation Status dock that tracks concurrent runs; live 1D and 2D results while
the run is still writing. See \ref manual_running.

**Analysing results.** Results layers and result-driven symbology, map animation
with a scrubber and look-back window, time-series and comparison plots, longitudinal
and 2D profile plots, result columns in the attribute table, flow balance, travel
time, mass balance and a statistics dashboard. See \ref manual_results,
\ref manual_time_series_plots, \ref manual_profile_plots,
\ref manual_tabular_results and \ref manual_analysis_tools.

### Supported platforms and installation

SWMMVis is built and packaged for macOS, Windows and Linux from the same CMake
project.

| Requirement | Notes |
|---|---|
| Qt | 6.5 or newer (Widgets, OpenGL, Network, Concurrent, Svg, Charts) |
| GDAL | 3.x — supplies GIS formats and the PROJ CRS database |
| Graphics | The map, 2D mesh and 2D results layers render through Qt's scene graph. On macOS the OpenGL RHI backend is forced; other platforms use Qt's native backend |
| OpenSWMM engine | 6.0.0-alpha.4 — shipped inside the application bundle |

To install a released build, download the installer or archive for your platform
and run it. On macOS, drag `SWMMVis.app` out of the disk image into
`/Applications`; if the bundle was downloaded from the web, right-click it and
choose **Open** the first time so Gatekeeper lets it through. On Linux the package
installs `org.openswmm.swmmvis.desktop` and an AppStream metainfo file, so the
application appears in the desktop menu under *Education / Science*.

To build from source, clone `openswmm.gui`, `openswmm.engine` (branch
`swmm6_rel`), `QPropertyModel` (branch `dev`) and `vcpkg` as siblings in one parent
directory, point `QT_ROOT_DIR` at your Qt kit, then configure and build with the
platform preset:

```
cmake --preset=Darwin        # or Linux / Windows
cmake --build --preset=Darwin
```

Full build instructions, the dependency table and the test invocation are in the
repository `README.md`; release-by-release changes are in `CHANGELOG.md` and
`docs/releases/`.

\figtodo{01_install_macos_dmg.png, The macOS disk image with SWMMVis.app being dragged into Applications}

### First launch

Launching the application runs three things in order.

1. **The splash screen.** A 600 × 400 splash appears on the primary screen and
   reports its progress — *Reading Application Settings*, then *Loading Component
   Libraries*. On a first run this stage is slower than usual because GDAL/PROJ
   build their CRS caches.
2. **The main window**, restored to the geometry, dock layout and ribbon tab you
   last used. A saved position on a monitor that is no longer attached is clamped
   back onto a connected screen automatically.
3. **The License Agreement dialog** (`licenseagreementdialog.h`), shown modally
   once the event loop starts.

The License Agreement dialog presents the GNU General Public License v3 notice for
SWMMVis with two buttons and one checkbox.

| Control | What it does |
|---|---|
| **Yes, I Agree** | Accepts and continues into the application |
| **No, Exit** | Quits immediately — the application does not start |
| **Show this agreement on startup** | Unchecked, suppresses the dialog on later launches. Stored in `QSettings` under `SWMMVis/LicenseAgreement/showOnStartup` |

You can bring the dialog back later with **Preferences → General → Show license
agreement on startup**.

\figtodo{01_license_agreement.png, The License Agreement dialog with the GPL v3 notice and the show-on-startup checkbox}

### The Welcome page

The Welcome page is an ordinary tab in the MDI workspace, titled **Welcome**. It
has four sections plus a startup checkbox.

| Section | Contents |
|---|---|
| **Start Modeling** | **New Project ...** and **Open ...** — the same commands as **File → New** and **File → Open** |
| **Open Recent Files** | One button per entry in the recent-files list — the same list as **File → Open Recent**, up to 20 entries — followed by **Clear Recent Files** |
| **Learn SWMM** | Three external links opened in your browser: **User Manual**; **Engine API Reference**; **Report an Issue** |
| **Example Projects** | One button per bundled example; the button's caption is the example's description |
| **Show welcome page on start up** | Unchecked, the Welcome tab is hidden at the next launch. Stored under `SWMMVis::ShowWelcomeOnStartup` |

Closing the Welcome tab with its **×** only hides it; **Help → Show Welcome Page**
brings it back with its content intact.

\figtodo{01_welcome_page.png, The Welcome page showing Start Modeling; Open Recent Files; Learn SWMM and Example Projects}

### Bundled examples and what happens when you open one

Examples ship as a read-only payload inside the installation
(`<bundle>/Resources/examples` on macOS, `<prefix>/share/openswmmgui/examples`
elsewhere). At startup the **examples seeder** (`src/project/examplesseeder.cpp`)
mirrors that payload once per application version into your per-user data folder
(`QStandardPaths::AppLocalDataLocation` + `/examples`), stamping a
`.seeded_version` marker so later launches skip the copy. If the per-user folder
cannot be written, the Welcome page falls back to scanning the read-only install
folder.

Two example shapes are recognised:

| Shape | Discovery rule |
|---|---|
| **Directory example** | A subdirectory containing at least one `.oswp` (preferred) or `.inp`. The whole directory is the unit that gets copied |
| **Flat example** | A single top-level `.inp` (legacy single-file bundles) |

A directory example may carry an `example.json` manifest with `name` and
`description` fields; those become the button's title and caption. Without a
manifest the folder or file name is prettified (`_` and `-` become spaces) and the
caption reads *Copy to a folder you choose — then open*.

Clicking an example never opens the bundled copy in place, so a run can never write
results into your installation. Instead:

1. A folder picker opens, defaulting to your **Documents** folder.
2. The example is copied into a subfolder of the folder you chose, named after the
   example.
3. If that destination already exists you are asked to **Open Existing**,
   **Replace** (delete and re-copy) or **Cancel**.
4. The copy's `.oswp` — or its `.inp` when there is no project file — is opened.

The models shipped with this build are `site_drainage`, `demo_capped_street`,
`demo_road_culvert`, `demo_snoopy_lagoon`, `demo_vfr_slope`, `demo_weir_culvert`
and `bellinge_2d`; several are used by the tutorials in Part V.

\figtodo{01_example_copy_prompt.png, Choosing the destination folder before an example is copied and opened}

\videotodo{First launch — accepting the licence; touring the Welcome page and opening a bundled example into a working folder}

### Opening your first model

Any of these open a model:

- **File → Open** (`Ctrl+O`), or **Open ...** on the Welcome page. The file dialog
  offers every readable input format (`.inp`, `.gpkg`, …) plus SWMMVis project
  files (`.oswp`).
- **File → Open Recent ▸** and pick an entry.
- Drag an `.inp` or `.oswp` file from your file manager and drop it on the main
  window. Dropping several files opens several projects; other file types are
  ignored.

There is no command-line argument for opening a model, and double-clicking a
`.inp` in your file manager will not hand it to SWMMVis — use one of the routes
above. (A `SWMMVIS_OPEN_ON_STARTUP` environment variable exists as a
development and testing hook; it is not a supported user feature.)

While the file loads, the status bar shows *Opening &lt;name&gt;…* with a progress
bar and the Message Logs dock records each stage; parsing and layer construction
run off the GUI thread, so the window stays responsive. When it finishes you get:

- a new tab in the MDI workspace named after the file;
- the network drawn on the map canvas — nodes as markers, links as polylines,
  subcatchments as polygons;
- the status bar's **Flow Units**, **Offset Mode**, **Engine** and **Coordinate
  Reference System** widgets bound to that project;
- the Layers, Object Browser, Properties and (if a results file was found) results
  panels populated.

**If the model has no CRS.** A `.inp` with no coordinate system loads with the
placeholder CRS *Untitled (Local)*, and SWMMVis asks you to choose one, because
reprojecting basemaps and GIS layers needs a real CRS. The prompt offers **Choose
CRS…**, **Use local projected (ft)** / **(m)** — which matches the model's flow-unit
system and is enough for 2D mesh generation — and **Abort Open**. Models whose
coordinates are already in an auto-generated local CRS (`Local (ft)` / `Local (m)`)
are not prompted. See \ref manual_crs.

\figtodo{01_first_model_open.png, A freshly opened example model on the map canvas with the status bar bound to the project}

### Where to get help

| Route | What you get |
|---|---|
| **Help → Show Welcome Page** | The Welcome tab, including the **Learn SWMM** links to this manual; the engine API reference; and the issue tracker |
| **Help → Keyboard Shortcuts…** | Preferences opened on the **Keyboard** page — browse and rebind every command (see \ref manual_shortcuts) |
| **Help → About** | The About dialog: a searchable list of every shipped component with its version; role; provenance; SPDX identifier and full licence text |
| **Command Palette** (`Ctrl+Shift+P`) | Search every command by name when you cannot remember where it lives |
| Tooltips and status tips | Every ribbon button and menu item carries a one-line description; long tooltips on the model-authoring actions explain what the tool does to the model |
| **Message Logs** dock | Every warning and error the GUI and the engine produce, timestamped |

**Help → Help** exists in the menu with the platform help shortcut bound to it, but
it is not connected to anything in this build and does nothing. Use the Welcome
page's **User Manual** link instead.

\figtodo{01_about_dialog.png, The About dialog listing shipped components with the licence text for the selected entry}

## Tips and gotchas

- **First launch is slower.** GDAL/PROJ build their CRS caches on first use; later
  launches are noticeably faster.
- **macOS Gatekeeper.** A downloaded `.app` may need right-click → **Open** once.
- **`File → New` does not ask any questions.** It creates a blank untitled project
  immediately from your **Simulation Defaults**, **Dynamic Wave Defaults**, **2D
  Defaults** and **Object Defaults** preferences. Set those up first if you build
  new models often — see \ref manual_preferences.
- **An untitled project lives only in memory.** Nothing touches disk until the
  first **Save As**, and closing one always prompts even if you changed nothing.
- **Examples are always copied.** If you want to keep working on an example, note
  where you copied it — re-clicking the Welcome button will offer to replace it.
- **`SWMM_LOG_FILE`.** Set this environment variable to a path (or to `1` for an
  automatic location under the application data folder) to tee every diagnostic
  message to a file. Useful on Windows, where the application has no console.
- **The 2D and results tabs appear only when they apply.** **Mesh 2D** shows up
  once a mesh or 2D-results layer is loaded, **Terrain** once any raster is
  loaded — they are contextual ribbon tabs, not missing features.

## Related

- \ref manual_interface — every menu; ribbon group; panel and status-bar widget
- \ref manual_projects — `.inp` versus `.oswp`; saving; recents; undo; portability
- \ref manual_preferences — defaults for new projects; appearance; shortcuts
- \ref manual_crs — assigning and changing coordinate reference systems
- \ref manual_file_formats — every file SWMMVis reads and writes
- \ref tutorial_site_drainage — the first end-to-end tutorial
- \ref manual_about — credits and licences
