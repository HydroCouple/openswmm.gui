# Changelog

All notable changes to the OpenSWMM GUI are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The `6.0.0-alpha.3` section below is **unreleased / pending** — only one
git tag exists in this repository (`v5.2.4`, the imported legacy Delphi
GUI baseline). `6.0.0-alpha.3` is the version set in `CMakeLists.txt`'s
`PROJECT_VERSION_SUFFIX` and in `vcpkg.json` for the new Qt/SWMMVis GUI
described below. It is used here instead of a generic
"Unreleased" heading. Generated with support from
[`git-cliff`](https://git-cliff.org) (config: `cliff.toml`) run against
the full commit history — the only tagged boundary is `v5.2.4`.

## [6.0.0-alpha.3] — SWMMVis: ground-up Qt6/C++ rewrite

The legacy Delphi `epaswmm5.exe` GUI (last shipped as `v5.2.4`, see
below) is being replaced by **SWMMVis**, a new Qt6/C++ application built
directly on `openswmm.engine`. Work started 2025-04-03 as a bare project
skeleton; substantive features begin with commit `9df607d` (2026-04-28).
An informal "Alpha 1" milestone was reached 2026-05-22 (commit
`55098b6`); everything below through the current `alpha.3` version
string has landed since.

### Added

- **Toolbar selection ops.** *Invert Selection* now scopes to a single
  SWMM category when the current selection is homogeneous (selecting
  three junctions and inverting yields every *other* junction) and falls
  back to inverting across all eleven categories when the selection is
  mixed; 2D mesh elements and non-spatial data objects pass through
  untouched. *Select Upstream* / *Select Downstream* now propagate
  through subcatchments — an upstream trace pulls in every subcatchment
  draining to a traced node, transitively through subcatchment →
  subcatchment outlets, and a subcatchment seed traces downstream along
  its outlet chain into the pipe network — and replace the selection
  rather than extending it. The selection algebra moved out of
  `SWMMVis` into a headless-testable `SelectionOps` module
  (`src/selection/selectionops.cpp`).
- **Copy in the Attribute Table.** `Ctrl+C` over the Attribute Table
  copies the selected rows as tab-separated text (header line included,
  current sort/column order, hidden columns skipped, query and
  show-selected-only filters honoured), pasting straight into Excel or
  Sheets; falls back to every visible row when nothing is selected. The
  same action is on the panel toolbar and the right-click menu. The
  existing `Ctrl+C` binding still copies the map view as an image when
  focus is anywhere else.
- **2D mesh/results rendering: 1M-cell QSG architecture.** An 8-phase
  rearchitecture of the 2D mesh and results renderers for meshes up to
  ~1M cells: per-sync dirty-domain classification (Geometry/Style/Data/
  Selection/Lod/Transform, so pans are matrix-only and time ticks skip
  static geometry); a hysteresis-damped Far/Mid/Near LOD policy with
  viewport culling; chunk-batched render culling; shared static geometry
  buffers with in-place indexed-fill color/index rewrites per tick;
  `QtConcurrent`-based async band/isoline contouring with a
  generation-guarded double buffer; and an opt-in GPU path
  (`ScalarFillMaterial`, a 256×1 ramp LUT texture, qsb-compiled shaders).
  New `OPENSWMM_RENDER_PERF=1` per-sync/per-pass instrumentation.
- **Tile-pyramid raster rendering.** Large-raster display is now
  O(viewport) instead of O(raster size): background overview-pyramid
  (`.ovr`) generation, a windowed/overview-aware `warpToCanvas` with an
  approximate (gdalwarp-style) reprojection transformer, a fixed-grid
  256px canvas-CRS tile cache with a coarser-tile fallback for missing
  tiles, and parallel tile production (N = min(4, cores) concurrent warps
  on read-only GDAL handles).
- **Non-blocking file I/O across every open path**, with opt-in
  load-phase telemetry (`openswmm.load.*` logging categories) and
  consistent "Opened/Failed to open" status messages: single-`.inp` open
  moves engine-open to a worker thread; `.out` results, raster, and
  vector layers gain the same async-open pattern (vector is two-stage:
  async sublayer enumeration → picker → async per-layer open). West
  Whiteland (103k nodes, Debug): post-parse GUI-thread freeze
  ~1877 ms → ~250 ms.
- **Symbology MVC overhaul.** Single-source-of-truth `SymbolStyle`
  round-tripping (labels, flow arrows), archetype-seeded
  `RendererFactory` as the sole renderer-construction path, graduated
  classification driven from the data layer (Jenks/StdDev/Log/Exp
  binners), contextual symbology (capability-aware, greyed dropdown rows
  with reasons), a canonical `LegendContent` MVC shared by the legend
  overlay/dock/per-class edit commands, and a `PalettedRasterRenderer` +
  hillshade. Removed the abandoned OpenGL renderer.
- **Engine editors, Phase 3:** rain gage, outlet (node/subcatchment),
  infiltration model + per-model parameters, and land-use/groundwater/LID
  compound editors in the Property Browser and Attribute Table; inline
  cross-section `geom1`–`geom4` fields (Property Browser + Attribute
  Table) for conduits/orifices/weirs, with shape-aware
  enable/disable and a right-click "apply to all selected rows" bulk-edit
  action.
- **2D mesh editing.** Per-vertex 1D↔2D coupling Cd/Area editors on the
  mesh toolbar (contextual, multi-select) plus an Auto-couple action that
  matches vertices to SWMM nodes at coincident coordinates; per-triangle
  Manning's n, descriptive vertex/triangle tags, and coupling edits now
  round-trip through the engine into the `.inp`/sidecar (previously
  silently dropped).
- **Climatology dialog** (6 legacy-parity tabs: Temperature, Evaporation,
  Wind, Snow Melt, Areal Depletion, Adjustments) and wiring for
  previously-dead toolbar buttons: Search, Tabular View, Add Delimited
  Data, Summarize Results, Copy/Print map, Invert Selection, Show Mass
  Balance, and real graph-BFS Select Upstream/Downstream with Flow
  Balance/Travel Time over the resulting subnetwork.
- **Polygon (lasso) selection tool** on the map toolbar — draw an
  arbitrary polygon to select every node/link/subcatchment/gage it
  encloses (Shift adds, Ctrl removes).
- **Configurable chart axis number format** (decimals/significant figures
  + a validated custom `printf`-style override), exposed from a per-chart
  "Chart Properties…" entry across the scatter/curve/pattern/time-series/
  hydrograph editors.
- **Terrain-raster longitudinal profile tool**, mirroring the existing
  mesh bed-profile trace but sampling ground elevation from the active
  DEM.
- **Sim-options parity:** process-model checkboxes (Rainfall/Runoff,
  Snowmelt, Groundwater, RDII, Water Quality, Flow Routing) grey out when
  the model has no objects of the class they control, matching the
  legacy Delphi `Doptions.pas` behavior.
- Coupled 1D-2D road-culvert example (`examples/demo_road_culvert`),
  tuned to produce a genuine overtopping event with an alternating-diagonal
  mesh (removes spurious cross-slope drift on a symmetric problem).

### Changed

- **License changed from MIT to GNU GPL v3.**
- **Engine consumption switched to a prebuilt `openswmm.engine` package**
  (`find_package`, replacing the in-tree `FetchContent` build); the GPU
  plugin (OpenMP/BoomerAMG) is now bundled beside the engine on all three
  platforms, with a serial CVODE+AMG fallback when it's absent.
- **Canonical SWMM DateTime converter** (`include/core/swmmdatetime.h`,
  delegating to the engine's own encode/decode primitives): a 3-phase
  consolidation migrated every call site (time-series editors/registry,
  results-layer clocks, the `[EVENTS]` editor, the climatology date
  field, the 2D auto-load anchor) off at least four independent hand-rolled
  OLE-Automation-epoch implementations, deleting the old
  `swmmjuliandatetime.h` shim. Fixes a minute-truncation bug (`GH #1`,
  e.g. 00:15 decoding as 00:14) and a `QTimeZone::LocalTime` vs. UTC
  inconsistency (`GH #2`). See `workplans/SWMM_DATETIME_CONSOLIDATION_PLAN_2026-07-10.md`.
- **Typed, per-kind object selection.** Selection, visibility, and rename
  were flat name-keyed sets, so a rain gage and a subcatchment (or any
  two objects) sharing a name bled selection/hidden state and rename
  targeting into each other. Selection is now `{name, kind}`-scoped
  end-to-end (click/rect/polygon select, hit-testing, delete, rename,
  Object Browser check state).
- 2D results selector now drops the "(live)" label and re-arms the
  animation controller against the full HDF5 source when a simulation
  finishes, so play/scrub operate on complete results.

### Fixed

- `.oswp`-relative layer paths (rain `FILE` gages, mesh sidecars,
  hotstart files) now resolve against the project directory instead of
  the process working directory — previously loaded silently empty
  (e.g. a zero-rainfall Bellinge run with an otherwise normal-looking
  report).
- Live QSG 2D result rendering (velocity glyphs, smooth fills, contours,
  mesh-profile samples) now re-applies the current frame when edge
  geometry/flux/vertex-head packets arrive after the depth packet.
- 2D profile plot: interpolated water surface is now clamped to the
  driving HGL among only the wet vertices of a cell, so it no longer
  climbs an adverse slope above the head available to drive it on
  dry/adverse cells.
- 2D mesh edits (roughness, tags, coupling) are now drained for editing
  before being pushed to the engine, so they persist from the GUI's
  normal OPENED-but-not-initialized state.
- WMS/WMTS `AddBasemapDialog` service-info lifetime converted to RAII
  (`std::unique_ptr`), fixing a CodeQL-flagged use-after-free.
- Real engine load-failure diagnostics surfaced (`swmm_get_last_error_msg`
  instead of a generic numeric-code lookup) — a 2D parse failure
  previously showed as "Out of memory".
- Outlet-connector selection ring (dashed circle at a selected
  subcatchment's outlet) now also draws on the GPU/QSG rendering path,
  not just the CPU painter, so it no longer disappears once 2D results
  are loaded.
- The GUI now bundles the `openswmm-legacy-worker` subprocess (and, on
  macOS, the legacy engine dylib) in its packaged output on all three
  platforms — legacy-engine runs previously failed after the switch to
  the prebuilt-engine package because only the modern engine was
  deployed.
- Windows/macOS/Linux CI and packaging: Ninja generator + `QT_ROOT_DIR` +
  vcpkg baseline for the Windows build (VS 2022→18 runner upgrade broke
  the hard-coded generator); `qtshadertools` install and a hardened
  `FindOpenMP.cmake` for macOS/Kokkos; `libcups2-dev` for Qt6
  PrintSupport on Linux; Doxygen working-directory and AppImage
  icon-filename fixes; CUDA/any-GPU-plugin + SUNDIALS DLL + GDAL/PROJ
  layout fixes for the Windows CPack package; committed several GUI
  source files that were referenced by `CMakeLists.txt` but never
  actually tracked (broke clean CI checkouts).

### Performance

- **Map canvas pan lag.** Profiling showed the canvas pipeline, not tile
  fetching, dominated: an EPSG:3857 canvas fast path replaces the
  per-pixel basemap reprojection with a single affine `drawImage`
  (projected canvases fall back to a thread-pooled per-pixel warp); the
  rendered scene buffer is reused when nothing scene-affecting changed;
  and a mid-drag render tick fills blank margins during a pan instead of
  only after mouse-up. Also fixed a pre-existing tile-cache data race
  (unlocked GUI-thread writes vs. a render-worker read).

### Testing

- New coverage for typed selection/visibility and async model load,
  driving the real (previously untestable, ~6750-line) `SWMMModelLayer`
  for the first time by linking `PROJECT_SOURCES` directly rather than
  cherry-picking dependencies.

## [5.2.4] — 2023-08-03

Last EPA-maintained release of the legacy Delphi GUI (`epaswmm5.exe`),
imported into this repository as the starting baseline. GUI-only notes
below, reconstructed from EPA's official update history
(`epaswmm5_updates.txt`); engine-side changes for the same builds are in
`openswmm.engine/CHANGELOG.md`.

### Fixed

- Flickering of the Study Area Map when panning.
- An Access Violation error editing a Storage Unit polygon's vertices.
- Possibility of the centroid symbol for subcatchments/storage nodes
  landing outside their bounding polygon.
- Max/Full Depth value for orifices/weirs appearing in the wrong column
  of the Summary Results Link Flow table.
- A "Scrollbar property out of range" error after an extremely
  long-duration run.

### Changed

- Re-opening the Welcome Screen from the Help menu and selecting a new
  file now prompts to save the current project first.

## Build 5.2.3 — 2023-02-12

### Fixed

- Failure to load a project's backdrop image when first opened in
  v5.2.2.
- Failure in v5.2.2 to update a Storage Unit's centroid symbol position
  after its vertices were edited.

## Build 5.2.2 — 2022-12-01

### Added

- Storage units can now be drawn as polygons, the same way subcatchments
  are.
- Option to view a backdrop image in grayscale (View | Backdrop menu).

### Changed

- Street Flow Summary table display updated for its additional results.
- The dashed line between an inlet conduit and its receptor node is
  always drawn from the conduit node at the lower invert elevation.

### Removed

- Copy As Data option on the Curves/Time Series/Transects preview plots
  (it didn't work).
- Progress indicator in the program's taskbar icon (caused problems in
  some environments, e.g. Citrix).

## Build 5.2.1 — 2022-08-01

### Added

- "NONE" choice for Normal Flow Criterion on the Dynamic Wave page of the
  Simulation Options dialog.
- Taskbar icon progress indicator while a simulation is running.
- Subcatchment outlines now included when the Study Area Map is exported
  to DXF.

### Changed

- More accurate detection of the number of available cores on the
  Dynamic Wave options dialog.
- Some refactoring for Delphi 10.4 compatibility.

### Fixed

- Welcome Screen behavior when there were no recent projects to display.
- A node-symbol sizing problem on DXF export.
- Issues restoring program state after an Add-In tool returned control.
- An Access Violation message in the Storage Shape editor.

## Build 5.2.0 — 2021-11-01

### Added

- Optional Welcome page.
- Keyboard shortcuts for common menu commands (with a Help menu listing).
- A subset of Summary Results table values viewable as Study Area Map
  themes.
- Relative path name support for files referenced in an input file
  (project portability).
- New dialog forms for Street cross-sections, Inlet structures, an Inlet
  Usage assignment (inlet → street conduit → receiving node), storage
  unit shape data, and Culvert Code selection.
- Type5 pump type in the Pump Curve Editor.
- Pump startup/shutoff depths now copyable to another pump.
- Input file reader strips a leading BOM character.
- Warning message when no data can be read from an opened file.

### Changed

- Map-related speed buttons moved from the main toolbar into a separate
  Map toolbar.
- F1 context-sensitive Help extended to all dialog forms.
- LID Control Editor now accepts void ratios greater than 1 for LID
  storage layers.

### Fixed

- Property Editor not appearing after a multi-monitor session.
- Move Up/Move Down buttons on the Data Browser.
- Problems determining map extents from a SWMM input file.

## Build 5.1.15 — 2020-05-01

### Added

- Mouse-wheel zoom on the Study Area Map.
- Better support for 4K ultra HD monitors (the three main toolbars were
  combined into one to avoid the resulting resizing issues).

### Changed

- Subcatchment Infiltration Dialog and Group Editor dialog updated to
  accept a choice of infiltration method (and its parameters, for
  group assignment).

### Fixed

- Problems with the Graph Options dialog for Statistics Report plots.

## Build 5.1.13 — 2018-05-10

Build 5.1.14 (2020-03-01) had engine-only changes; no GUI Updates were
listed for it.

### Changed

- Property editors and dialog forms updated for the new engine features
  (monthly time patterns for subcatchment properties, LID underdrain
  parameters, LID pollutant removal, surcharge method choice, storage
  unit surcharge depth, weir coefficient curve, control rule time step,
  average-value reporting).

### Fixed

- Cross-Section Editor not recording the chosen number of barrels for
  rectangular conduits.
- A bug in the `GetLinkOutVal` function of `Uoutput.pas`.

## Build 5.1.12 — 2017-03-14

### Added

- `OnChange` event handler on each LID Control Editor data field, to
  record when a value changes.

### Changed

- LID Control Editor now sets Storage Layer Thickness to 0 when a Rain
  Garden is selected.

### Fixed

- Profile Plot Options dialog not updating the main/axis title text
  correctly; downstream offset height of non-conduit links now set to 0
  on the plot.

## Build 5.1.11 — 2016-08-22

### Added

- Events sub-category of simulation options + Event Editor dialog
  (restricts detailed flow routing to specific time periods).
- Run Status dialog now indicates if any warning messages were issued.
- Profile Plot: ground-surface-line visibility toggle (default visible)
  and a thick-line outline option for conduits/ground surface.

### Fixed

- LID Control Editor now shows the Storage layer tab (bottom Seepage
  Rate) when a Rain Garden is selected, fixing a no-infiltration bug.
- A previously uninitialized elapsed-simulation-time variable passed
  between the GUI and engine.

## Build 5.1.10 — 2015-08-05

### Added

- Potential evapotranspiration (PET) added as a system-wide variable
  viewable via graph or table.

### Changed

- Accommodated the new Modified Green-Ampt infiltration option and
  Roadway weir type.
- Improved automatic scaling of plots with constant y-values and of
  profile plots (ground surface line removed from profile plots for
  clarity).
- Link depth-offset-to-elevation-offset conversion now uses the node
  invert elevation rather than `*` for a zero offset.

### Fixed

- Number of Threads dropdown on the Dynamic Wave Options dialog.
- Additional Cross Section Editor bugs remaining from 5.1.008.
- Macro List (Add-In Tools Properties form) insertion into the Working
  Directory edit box.

## Build 5.1.9 — 2015-04-30

### Changed

- Nicer default axis scaling for time series and scatter plots.

### Fixed

- A 5.1.008 regression hiding a conduit's Transect/Shape Curve name in
  the Cross Section Editor.
- User-supplied custom vertical-axis scaling for profile plots not being
  recognized.

## Build 5.1.8 — 2015-04-02

### Added

- Groundwater Summary table in the summary results form.
- Groundwater upper-zone soil moisture and node lateral inflow included
  in the abridged Hot Start file export.
- "Route To" field on the Outfall Node property editor (send outflow to
  a subcatchment).
- Minimum Routing Time Step and Number of Threads options on the Dynamic
  Wave Simulation Options page.
- LID Control Editor updated for Rooftop Disconnection and the Permeable
  Pavement soil layer; Drain Outlet field added to the LID Usage Editor.
- Cross Section Editor: selectable standard size codes/dimensions for
  elliptical and arch pipes.
- Custom Map Legend changes now saved with the project `.ini`.

### Changed

- Restored missing July–December column labels on the Climatology
  Editor's evaporation/wind speed tables.
- Renamed "Surface Water Height (Hsw)" → "Surface Water Depth" and
  "Channel Bottom Height (Hcb)" → "Threshold Water Table Elev." in the
  Groundwater Flow Editor.
- Input file column labels realigned with Users Manual Appendix D.
- Modal dialog message windows now center over their generating form;
  dropdown list box styling updated.

## Build 5.1.7 — 2014-09-15

### Added

- Object Toolbar restored.
- Climatology Editor page for monthly temperature/evaporation/rainfall
  adjustments.
- Weir surcharge-option field on the Weir Property editor.

### Changed

- Default LID storage-layer seepage rate changed.
- Infiltration Editor restored (Green-Ampt storage-unit seepage
  parameters); Groundwater Flow Equation Editor extended for deep
  groundwater flow.

### Fixed

- Project closing without a save prompt when a new style theme was
  selected in Preferences.

## Build 5.1.6 — 2014-05-19

### Fixed

- Options/Climatology dialog components not recording the changed-data
  flag after edits (a Delphi XE2-switch regression).

## Build 5.1.5 — 2014-04-23

### Changed

- Improved Open File Dialog preview-panel appearance.
- Storage-node Ponded Area property made read-only (storage nodes cannot
  pond).

### Fixed

- Help file pop-up topic windows obscured by the main Help window.

## Build 5.1.4 — 2014-04-14

### Fixed

- A refactoring bug ignoring Program Preferences numerical-precision
  changes.
- A 5.1.003 refactoring bug preventing groundwater-aquifer projects from
  running.

## Build 5.1.3 — 2014-04-08

### Fixed

- SWMM not working correctly under non-US Windows regional settings (a
  refactoring bug).
- Group Delete feature not working (a refactoring bug).
- Stay-on-top forms obscuring modal dialogs, a too-narrow Browser panel
  disappearing, and the Help system being unreachable with a modal form
  focused (all Delphi 7 → XE2 switch fallout).

### Changed

- Aquifer Editor form updated for the new upper evaporation pattern
  property.

## Build 5.1.2 — 2014-03-31

### Fixed

- A memory leak copying cells from the grid editor used in various
  dialogs.

### Changed

- Auxiliary-form/map-form creation moved to more appropriate startup
  events; main-form position/size save-and-restore routines revised.

## Build 5.1.1 — 2014-03-24

Large release: ported the entire GUI from Delphi 7 to Delphi XE2.

### Added

- Selectable UI color themes (Program Preferences dialog).
- Time Series Plot selection dialog can plot more than one object/
  variable pair; Graph Options dialog can invert a vertical axis.

### Changed

- "Data" Browser panel renamed to "Project" Browser.
- Object Toolbar eliminated — visual objects are now added the same way
  as non-visual ones, via the Project Browser.
- LID Control and LID Usage editors redesigned for the new LID control
  options.
- Summary results tables moved out of the Status Report into a separate,
  sortable Summary Report.
- `[XSECTIONS]` columns in a saved project file gained a "Culvert Code"
  heading label.

### Fixed

- Hargreaves-equation evaporation option not being saved with the
  project.
- Pollutants no longer listed as theme/graph/table/statistics variables
  when Water Quality analysis is off but pollutants are defined.

## Build 5.0.22 — 2011-04-21

### Added

- New Study Area Map subcatchment theme: percent of area occupied by LID
  controls.

### Changed

- LID Control Editor and LID Group Editor now validate fraction fields
  and total LID area/impervious-area-treated limits.

### Fixed

- Comments on Time Patterns being lost on save/reopen.
- File | Export | Hotstart option not saving groundwater state correctly.

## Build 5.0.21 — 2010-09-30

### Fixed

- Data Browser splitter-bar component anchoring on first launch.
- Incorrect display of link slopes on the Study Area Map under the
  Elevation Offsets option.

## Build 5.0.19 — 2010-07-30

### Added

- Data Browser support for the new LID objects, with new dialog forms
  for LID design data and subcatchment placement.
- Drag-and-drop project file opening from Windows Explorer/Desktop.
- "Evaporate only in dry periods" checkbox on the Climatology Editor.
- External time series (EXT) buildup function choice on the Land Use
  Editor.

### Changed

- Decimal separator now always "." regardless of Windows Regional
  Settings changes mid-run.
- New installer places example data sets under
  My Documents\EPA SWMM Projects.

### Fixed

- Data Browser splitter behavior under Windows 7 (isolated into its own
  panel component).

## Build 5.0.18 — 2009-11-18

### Fixed

- Status Report temp files not being deletable from the user's TEMP
  folder once no longer in use.

### Changed

- Add-On Tools project input file now includes all project data (map
  coordinates, element tags).

## Build 5.0.17 — 2009-10-07

### Changed

- Pollutant property editor updated for the new default dry-weather-flow
  concentration property.
- Default dry-weather runoff time step reduced 15 → 5 minutes; default
  total duration changed 0 → 6 hours.

### Fixed

- Ruler tool now marks the starting point, easing closed-polygon area
  measurement.

## Build 5.0.16 — 2009-06-22

### Added

- "Evaporate from daily temperatures" (Hargreaves) checkbox on the
  Climatology Editor's Evaporation page; Evaporation Rate added to the
  viewable System variables.

### Changed

- "Shape Curve" renamed to "Storage Curve" in the Storage Unit Property
  Editor (to avoid confusion with the cross-section Shape Curve).

## Build 5.0.15 — 2009-04-10

### Changed

- Data entry forms updated for storage-unit infiltration, per-hydrograph
  Initial Abstraction parameters, and the Transect Meander Modifier.

### Fixed

- Conduits with elevation offsets displaying incorrectly on pre-run
  profile plots.

## Build 5.0.14 — 2009-01-21

### Added

- Support for minimum conduit slope, per-conduit culvert designation,
  monthly infiltration recovery pattern, Baseline Time Pattern for
  external inflows, updated Modified Baskethandle cross-section,
  depth-/head-based Outlet rating curves, options to ignore selected
  process models, and external-file time series data — reflected across
  the Time Series Editor and related dialogs.
- New Reporting category of Simulation Options (limit which objects'
  results are saved/reported).
- Group Editing extended to Snow Packs and Groundwater Flow parameters.

### Changed

- Hotstart file export now converts metric results to internal US units.
- Commas no longer recognized as input-file item separators (allows
  commas in object ID names).
- Default natural areal depletion curve coordinates for snow packs
  aligned with NWS publications.
- "Rainfall" theme variable renamed to "Precipitation".

### Fixed

- Startup input file not loading when launched from the command line or
  an Explorer shortcut.
- Simulation progress meter's elapsed-days count on long-term runs.
- Profile Plots not updating correctly after certain display-option
  changes; vertical axis scaling now settable from Profile Plot Options;
  filled junction water level now capped at the ground surface.
- Copying a single Tabular Report column to clipboard/file.
- Time Series/Tabular Report Selection dialog buttons getting stuck
  disabled.
- Subcatchment/groundwater-node outlet-name loss on right-click type
  conversion.
- Statistics Report analyzer omitting the last event for some data sets.

## Build 5.0.13 — 2008-03-11

### Fixed

- Erroneous INI-file values that could prevent a graph from displaying
  properly.

## Build 5.0.12 — 2008-02-04

### Added

- Status Bar drop-down buttons for Link Offsets convention and flow
  units; a Bookmarks panel on the Status Report window; a Measurement
  Tool on the Map Toolbar; a "View Conduits Only" Profile Plot option.
- Storage Units added to the Group Editor's editable object types.

### Changed

- Non-conduit profile-plot object length reduced from 100 ft to 10 ft.
- Number-format decimal-place preferences now persist between sessions.
- Simulation results now always retained between sessions when
  requested, even if inputs changed after the last run.

### Fixed

- Profile Plot dialog not always finding the fewest-links path between
  two nodes.
- `[REPORT]` section entries for the command-line version being dropped
  by the GUI.
- Conduit lengths/areas being recomputed unconditionally after Map
  Dimensions changes.
- Backdrop map not panning correctly on Edit | Find Object.
- Subcatchment outlet/groundwater-node name loss on type conversion.
- Statistics Report analyzer omitting the last event.

## Build 5.0.11 — 2007-07-16

### Fixed

- A 5.0.010 regression that dropped quotation marks around Map Labels
  and backdrop file names containing spaces, breaking project reopen.

## Build 5.0.10 — 2007-06-19

### Added

- New Tools menu (Program Preferences, Map Display Options, Configure
  Tools/Add-Ins).
- "None" routing-method choice (for the new No Routing option).
- Custom cross-section and Circular Force Main support in the
  Cross-Section/Curve editors; Time To Close/Open field for orifices;
  Initial Abstraction parameters in the Unit Hydrograph Editor.
- Export current results to a Hotstart file (File | Export).
- Map Query support for filtering nodes by external-inflow type.

### Changed

- Pump Curve field can be left blank/`*` for the new Ideal pump type
  (with startup/shutoff depths).
- Auto-Length checkbox moved to the Status Panel; conduit slopes no
  longer shown as absolute values (negative slopes now visible on
  thematic map displays).
- Zoom Out now relative to the current map center.

### Fixed

- Path-finding between two nodes on the Profile Plot dialog now prefers
  the fewest links.

## Build 5.0.9 — 2006-09-19

### Fixed

- Profile plot display when all elevations are below zero.

## Build 5.0.8 — 2006-07-05

### Added

- Inflows Editor updated for the new baseline/scaling parameters on
  direct external inflows.
- Select All extended to the Status Report display.

### Changed

- Program-preferences `.INI` file relocated to the user's home directory
  (from the SWMM install directory).
- New text-file viewer speeds up Status Report display.
- Type 3 pump curves now plot head on the vertical axis, flow on the
  horizontal, in the Curve Editor's View option.

### Fixed

- A Graph Options Horizontal Axis formatting error.

## Build 5.0.7 — 2006-03-10

### Added

- Link Flow Depth and Link Velocity as calibration variables.

### Fixed

- Property Editor Maximum Depth field for irregular-shape conduits.
- Non-conduit link display on profile plots (weirs/orifices with crest
  heights above the node invert).
- Group Editing handling of irregular cross sections.

## Build 5.0.6a — 2005-10-19

### Fixed

- Numerical-precision problems computing centroids for subcatchments
  with very small vertex spacing.
- Calibration data outside a time series graph's range not displaying;
  date-based (vs. elapsed-time) calibration data shifting by one
  reporting period on elapsed-time graphs.

## Build 5.0.6 — 2005-09-05

### Added

- "Prompt to Save Results" and "Report Elapsed Time by Default"
  preferences.
- Additional Calibration File parameters (groundwater elevation, node
  flooding, etc.); Percent Impervious subcatchment theme.
- Exceedance Frequency plot panel on Statistics reports.
- Profile Plot link-list add/delete/reorder buttons; Profile Plots can
  now be generated before any simulation results exist.

### Changed

- File | Reopen now lists up to 10 recent files.
- Edit | Find split into map-object-find and Status-Report-text-find.

### Fixed

- Interface File Combine utility (broken since an interface-file format
  change).
- Subcatchment-polygon centroids now true centroids (not vertex
  averages).
- Storage-unit Maximum Depth lost on conversion to a junction.
- Zoom-in display problems on Transect/Curve/Time Series preview plots.

## Build 5.0.5b — 2005-06-15

### Fixed

- The "WEIR" keyword not being recognized as a valid Flow Divider type by
  the input-file parser.
- Profile Plot hydraulic grade lines dropping below a conduit's invert.

## Build 5.0.5a — 2005-05-25

### Fixed

- Profile Plot drawing when negative elevation values occur.

## Build 5.0.5 — 2005-05-20

### Added

- Startup input file via command line (`/f filename`).
- Support for output results files greater than 2 GB.
- Copy/print support for curve, time series, and transect graphical
  views.

### Changed

- Routing Time Step now entered as fractional seconds (legacy
  hrs:min:sec format still imports correctly).

### Fixed

- Weir flapgate-parameter reading from an input file.
- Property Editor appearing off-screen after a resolution change.
- The Convert To node-type-change option.

## Build 5.0.4 — 2004-11-24

### Added

- Negative temperature-value entry on input forms.

### Fixed

- Input file reader validation of time-of-day option values.
- Tabular Report date copying to clipboard/file.
- Graph Options Style field not showing "Solid" for Size > 1.

## Build 5.0.3 — 2004-11-10

### Changed

- Progress meter now refreshes daily (not every minute) on long-term
  runs of smaller projects, considerably speeding execution.
- Time series graph drawing and statistical analysis sped up
  considerably for large data sets.

### Fixed

- Group Editing updating the wrong conduit parameter.

## Build 5.0.1 — 2004-10-29

First official release of SWMM 5. No GUI-specific update notes were
issued for this or the immediately following 5.0.002 build.
