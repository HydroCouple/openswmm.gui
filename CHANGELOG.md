# Changelog

All notable changes to the OpenSWMM GUI are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Only one pre-6.x tag exists in this repository (`v5.2.4`, the imported
legacy Delphi GUI baseline), so the `6.0.0-alpha.2` and `6.0.0-alpha.3`
headings below are delimited by the version strings in `CMakeLists.txt`
(`PROJECT_VERSION_SUFFIX`) and `vcpkg.json` rather than by tags:
`6.0.0-alpha.2` covers the SWMMVis rewrite up to and including
2026-07-12, and `6.0.0-alpha.3` covers everything from the
`6.0.0-alpha.3` version bump onward. No `v6.0.0-alpha.2` tag was ever
cut. Generated with support from [`git-cliff`](https://git-cliff.org)
(config: `cliff.toml`).

## [Unreleased]

### Added

- **Separate "Profile" and "2D Profile" on the Analysis tab.** The 2D
  surface profile is now its own action (`actionPlotProfile2D`, Analysis
  menu ▸ Plot 2D Profile, ribbon Plots group) instead of a dropdown
  override on Plot Profile, which is network-only again. New themed
  `Profile2D` glyph — the Profile sectional block with a triangulated
  surface — shared with the Mesh 2D tab's profile-trace tool, which was
  using an unthemed raw resource icon.

- **Profile ground-line source option.** `ProfilePlotOptions::groundSource`
  (Auto / NodeRims / Mesh2D / TerrainDEM) replaces the "Use terrain DEM"
  checkbox. Auto — the default — samples the 2D mesh vertex elevations
  (barycentric) at the path stations whenever the project has a mesh
  layer, else falls back to the rim-to-rim line; explicit choices override.
  The line re-samples when a mesh layer is added/removed or its vertices
  are edited. The 2D inundation band fills up from whichever ground is
  drawn. Sampled ground (mesh or DEM) only shapes the soil BETWEEN nodes:
  every node keeps its 1D rim (invert + max depth, crown-clamped) and
  manhole notch exactly as in the pure-1D profile.

- **More axis number-format styles.** The axis format enumerator (profile
  Display Options, chart properties, Preferences ▸ Plots) gains
  Scientific (2/3/4 decimals), Engineering (12.35e+03; 2/3 decimals) and
  Thousands (locale group separators; integer/1/2 decimals) presets on top
  of the decimals / significant-figure ones. `NumberFormat::format`
  renders every style exactly; `printfSpec()` (Qt `QValueAxis` labels)
  degrades Engineering to `%e` and Thousands to `%f`, since Qt axis labels
  are printf-only.

- **2D inundation overlay on the 1D profile plot.** When the project has
  an active 2D results layer, the profile samples its water surface (mesh
  bed + barycentric depth) at the same densified stations the terrain
  ground line uses. While the overlay is on, the drawn ground line is the
  mesh bed interpolated at those stations (not the rim-to-rim line; an
  enabled terrain DEM still takes precedence), and a translucent band
  fills from that ground line up to the 2D WSE with a line on top — only
  where the water surface stands above the ground, and beneath every 1D
  element so the network's own HGL stays intact. Animates with the profile cursor (the dialog steps
  the 2D layer itself, so a hidden layer still updates). Toggle via the
  toolbar's "2D Inundation" button or Display Options ▸ "Show 2D
  inundation" (`ProfilePlotOptions::show2DInundation`, plus pen/brush
  properties); default on. Dry / off-mesh / no-data stations leave gaps.

- **2D right-click plotting uses the 1D-style context menu.** Right-clicking
  a 2D cell selection, a mesh edge, or a mesh vertex now pops the same
  `AttributePickerMenu` used for nodes/links (one entry per attribute plus
  "All attributes") instead of the cell checkbox dialog / the fixed two-item
  edge menu / the depth+HGL-only vertex entry. Entries the results source
  can't serve are greyed out with a tooltip.

- **2D cell rainfall series.** New plot attributes `Rainfall (2D cell)`
  (mm/hr, from `/Mesh2_face_rainfall`) and `Rainfall volume (2D cell)`
  (cumulative m³, from the engine's new `/Mesh2_face_rain_cum`).
  `Mesh2DH5Reader::readFaceFieldAt` / `hasFaceField` read any named
  per-face dataset with a probe-once presence cache; older files grey the
  entries out. Live runs serve the same two fields from
  `EngineMesh2DSource` (per-tick `SimulationRunner::twoDRainfallAvailable`
  via the engine's `swmm_2d_get_rainfall_bulk` /
  `swmm_2d_get_rain_volume_bulk`), so the entries are enabled during
  rendering, not only after the HDF5 swap-in.

- **Heat Configuration editor (G4g)** — Model ▸ Heat Configuration… edits
  `[HEAT_SOURCES]` inlet temperatures (with per-node DWF/external-inflow
  overrides), `[HEAT_FLUXES]` module toggles, and the H6a
  `[RADIATIVE_FLUXES]` / `[SOLAR_RADIATION]` / `[CLOUD_COVER]` forcing
  across five tabs. Dependency-light against `openswmm_heat.h`, the
  writeIfChanged discipline throughout (an untouched OK writes nothing and
  invents neither `[HEAT_SOURCES]` rows nor cloud cover), authoring limits
  mirror the engine parser, and the COMPUTED-shortwave option is gated on
  an explicit site. Unblocked by the engine's IO3a–IO3c save chain: edits
  survive `swmm_model_write` on every model. ~~Known gap recorded: the
  engine exposes no getter for a bound shortwave/cloud timeseries NAME, so
  those combos rebind behind a "(keep current series)" placeholder.~~
  **Gap closed (2026-09-01):** the engine gained the name getters
  (`d868b2c3`), and the shortwave/cloud combos now display and preselect
  the bound series; OK rebinds only when the selection actually moves.

- **Water quality and transport reach the GUI.** Simulation Options gains a
  **Quality & Transport** page exposing the quality solver choice (legacy,
  Eulerian ARD, Lagrangian) and the transport keys — water age, quality
  substepping, dispersion — that the engine grew alongside it. Species become
  first-class result attributes: themeable on the map like any built-in
  quantity, and present in every plotting surface (the variable pickers, the
  time-series charts, the comparison plots), with a saved species selection
  that warns and degrades gracefully when the run it reloads against no
  longer carries that species. Water age gets its editors: a **Water Age
  Sources** dialog for the initial state and boundary ages, reachable from
  the model menus, and an age constituent in the node inflow editor —
  hours-labelled, with the MASS units choice gated off and a CONCEN fallback,
  since an age inflow is a concentration statement. (`ebf28ae`, `dcc20e6`,
  `f5e0d9b`, `bc4e07c`, `dae4bad`, `7a5f732`, `9e63357`, `94ff3b5`.)

- **Minimum cell size for mesh generation.** Constraining lines and polylines
  force Triangle to emit cells at whatever scale the input geometry contains —
  GIS vertices centimetres apart, two alignments passing within a hair, conduits
  meeting at a sharp angle — and on the 2D solver one sliver sets the CFL
  timestep for the whole domain. The Generate Mesh dialog gains a **Minimum Cell
  Size** group (Quality tab) that enforces a floor by conditioning the input
  PSLG before triangulation: constraint polylines are resampled to a minimum
  segment length, vertices closer together than the minimum are merged, endpoints
  that nearly touch a line are welded onto it, sharp corners are blunted, and
  sub-scale hole rings are dropped. Tagged SWMM nodes never move and are never
  merged into each other, so coupling identity is preserved; conduits shorter
  than one cell are demoted to point constraints and couple through the existing
  post-generation node mapper. A second, optional post-meshing pass collapses
  leftover slivers that Triangle inserted on its own, protecting every
  constrained edge and coupled vertex. Geometry changes, domain area before/after,
  and any location that still cannot hold a cell of the requested size are
  reported in the generation log. **Defaults to off**, so existing projects
  reproduce their current mesh exactly.

### Fixed

- **Offset Mode toggle and ELEVATION-mode round-trip.** The status-bar
  toggle read `Elevation [ ] Depth` while its checked state meant ELEVATION,
  so the knob sat beside the wrong label; it now reads `Depth [ ] Elevation`
  (DEPTH = default = left/off, matching legacy `DefOptions`). Offsets and
  crests in the Attribute Table and Properties panel are now shown and edited
  in the file's convention (`ui/linkoffsetdisplay.h` adds/removes the node
  invert; the engine store stays in depths), the convert prompt maps onto
  that store the way legacy `UpdateOffsets` does (Yes = same physics, No =
  same numbers reinterpreted) and covers weir/outlet crests, and the model
  layer restores authored From/To on adverse-slope conduits after open so a
  save no longer flips them. New `test_offsetmode_roundtrip` on
  `offset_authored_fixture.inp`.

- **2D results land on top of the 2D mesh in a foot-based CRS.** Two faults
  stacked. The engine writes 2D result coordinates in SI metres whatever the
  model's unit, and the results layer treated them as model units — so a model
  in EPSG:2249 (US survey foot) drew its inundation at ~0.3048x scale, pulled
  toward the CRS origin, while the `.2dm`-backed mesh layer sat correctly. And
  the results layer never reprojected at all: `onCanvasCRSChanged` discarded
  its argument and the geometry rebuild applied only a Y-flip, so terrain and
  inundation also separated whenever the canvas CRS differed from the model's.
  The layer now divides by the `metres_per_model_unit` factor the engine 6.0+
  `/crs` variable declares, then reprojects model CRS → canvas CRS through the
  same batched OGR path `SWMM2DMeshLayer` uses. Results written by an older
  engine (and the live in-process source, which has no metadata channel) carry
  no declaration; for those the factor is derived the way the engine derives
  it — `FLOW_UNITS`, suppressed by a `;; UNITS: SI (m)` mesh header — and one
  warning is logged, so existing `.2d.h5` files render correctly without
  re-running. (Issue #155.)

- **External file references are now relative for every kind of file.** Saving a
  model already rebased rainfall, timeseries, climate, interface and hot-start
  paths against the destination directory, but four slots were written verbatim:
  the external `.2dm` mesh, the 2D `OUTPUT_FILE`, per-unit LID report files, and
  the 2D mesh reference emitted by the GUI's own mesh writer. The mesh writer
  computed its token by prefix match, so a mesh kept in a *sibling* folder — the
  usual reason to keep it out of the model directory — was written as a
  machine-specific absolute path instead of `../shared/terrain.2dm`. All four now
  go through the same relativisation as everything else, `../` forms included.
  `[PLUGINS]` paths stay absolute by design: they name installed libraries, not
  model data.

- **A dangling 2D mesh reference survives Save As.** When the external `.2dm`
  could not be loaded there was no mesh in memory to write alongside the new
  `.inp`, yet the reference was copied through unchanged — so it silently
  re-resolved against the *destination* folder, found nothing, and the saved
  model opened 1D-only with no diagnostic. It is now re-anchored to keep naming
  the file it originally pointed at. When the mesh *is* loaded the existing
  behaviour is unchanged and now covered by tests: the mesh travels with the
  `.inp` and the original `.2dm` is never overwritten.

- **Climate file was opened against the working directory.** `[TEMPERATURE] FILE`
  was opened using the token as authored rather than its resolved path, so a
  relative reference resolved against the process CWD instead of the model
  directory. `ClimateFileReader::open` returns `false` in that case and nothing
  checked it, so the run proceeded with no climate data at all — a clean-looking
  result with silently missing evaporation and temperature. It now opens the
  resolved path and warns when the file cannot be read.

- **`WRITE_ABSOLUTE_PATHS` set through the API had no effect on the save.**
  `swmm_options_set_ext(engine, "WRITE_ABSOLUTE_PATHS", "YES")` fell through to
  the generic extension-option map instead of the real project option, so the
  save still wrote relative paths while the emitted deck declared `YES` — and the
  *next* open honoured it. The key now sets the option it names.

- **Portability warnings only appeared on Save As.** A plain Save skipped the
  pre-save portability check entirely, so cross-volume or missing references went
  unreported unless the user happened to use Save As. Both routes now run it.

- **Time Series editor showed absolute paths for file-backed series.**
  `setProjectAnchor` had no caller anywhere in the application, leaving the
  relative-display path unreachable — the editor showed a raw absolute path even
  though the token written to the `.inp` was relative. The anchor is now carried
  on the timeseries registry and follows the project through a Save As.

- **Per-region mesh area bounds were silently ignored.** Triangle honours
  `[2D] REGION` area bounds only when its `vararea` flag is set, and that flag
  is set only by a bare `a` switch — but the generator emitted the numeric
  `a<maxArea>` form whenever a uniform cap was configured, which suppressed it.
  Region bounds now take effect when the refinement size function is installed
  (minimum cell size enabled), and are clamped up to the refinement floor so a
  region cannot request cells below the configured minimum. Projects that set
  both a uniform max area and per-subcatchment region areas may see a higher
  cell count than before, because the region bounds are now applied.

- **Attribute tracks under the profile plot.** The profile plot dialog gains a
  collapsible pane of stacked mini-charts ("tracks"), one per selected result
  attribute — node depth, head, volume, lateral/total inflow, overflow; link
  flow, depth, velocity, volume, capacity — each with its own y-axis, plotted
  along the path and sharing the profile's x-axis: zooming or panning either
  pane moves both, column-for-column. Tracks animate with the simulation clock
  and show a min/max envelope band for the primary source; overlaid comparison
  sources are tinted with their scenario color. Pick attributes from the new
  "Tracks" toolbar menu; style pens, track height, titles and envelopes from
  the new Attribute Tracks tab in Display Options. The pane collapses by
  dragging the splitter, via the toolbar toggle, or automatically when no
  attribute is selected — and remembers its state.

- **Window menu lists open dialogs, plus "Reset Window Positions".** Modeless
  dialogs (plots, editors, options panels) are ordinary top-level windows that
  can be dragged onto any monitor, so on macOS they could become genuinely
  unreachable — a natively-stacked dialog does not appear in Mission Control.
  The Window menu now lists every open dialog, most recently used first, and
  selecting one moves it back onto a connected screen *before* raising it.
  **Reset Window Positions** discards all saved window geometry and gathers the
  main window and every open dialog onto the current screen; layout state that
  is not position data (splitter sizes, header widths, dock/toolbar
  arrangement) is preserved.
- **Opt-in pure-Qt dialog stacking.** Set `OPENSWMM_DIALOG_STACKING=qt` (or the
  `Window/DialogStacking` preference) to keep dialogs above the main window by
  re-raising them on activation instead of attaching them as native macOS child
  windows. Dialogs are then fully independent windows that cannot be dragged
  off-screen by the window they were attached to. Defaults to the previous
  native behaviour on macOS.

### Fixed

- **Moving one plot dialog no longer moves another.** Opening a time-series
  plot from a profile plot glued the two windows together: on macOS every
  modeless dialog was attached as an AppKit child window of its Qt parent, and
  child windows move rigidly with their parent. Dialogs are now attached to the
  nearest ordinary window instead, skipping any dialog in the parent chain, so
  windows that are parented to each other for lifetime reasons — the profile
  overlay plot, and the Display Options dialogs of the profile, mesh-profile and
  raster-profile plots — move independently again.
- **Windows can no longer be restored off-screen.** Saved geometry was accepted
  whenever its center point landed on any connected screen, which still allowed
  a window restored from a larger display to be too big for the current one, or
  to have its title bar — the only drag handle — tucked under the menu bar. The
  restore now picks the screen the window most overlaps, shrinks it to fit, and
  guarantees a grabbable portion stays visible. The main window is clamped this
  way too; previously its geometry was restored with no validation at all.
- **Time-series plots opened from the map no longer hijack the profile plot's
  overlay.** A recursive child lookup matched the profile-parented overlay
  dialog, because the profile plot is itself a descendant of the main window.
  The two dialogs also shared one saved-geometry key and so fought over their
  position; the overlay now stores its own (existing saved positions for it are
  reset once).

- **Zoomable, pannable section diagrams.** Every section/profile/LID diagram
  now supports scroll-to-zoom about the cursor, middle-button drag to pan, and
  middle double-click to zoom to extents. Zoom scales the geometry only —
  labels keep their point size, so zooming spreads the drawing out from under
  crowded dimension text rather than magnifying the crowding. The view is
  preserved while values are edited and reset when the subject changes.
- **Illustrated LID layer diagrams.** LID controls are drawn with material
  patterns (stippled planting media, open gravel outlines, angular base-course
  aggregate, paver joints, drainage-mat lattice, hatched native soil), planting
  appropriate to the type (shrubs for bioretention and rain gardens, turf for
  green roofs and swales), ponded water to the berm, a perforated underdrain
  drawn in section at its offset, and inflow/infiltration arrows. Each of the
  eight types also gets its own illustration — a rain barrel with lid and roof
  leader, a green roof with deck and parapets, a paved surface for permeable
  pavement, a trapezoidal channel for a vegetative swale, a geotextile wrap for
  an infiltration trench, and a disconnected downspout for rooftop
  disconnection — so the type is legible at a glance rather than only from the
  combo box.

- **Section View dock + engine-accurate cross-section drawings (Slice SP).**
  New dockable **Section View** panel (View ▸ Panels ▸ Section View, tabbed
  behind the Property Browser) draws the selected object as a vector diagram
  with labelled dimensions and leader lines: links get a true-shape
  cross-section (full depth, max width, invert/crown elevations, A/R/W
  readout) and a longitudinal profile (both structures, ground line, barrel at
  its real offsets, length + slope); nodes get a manhole profile with every
  connecting pipe drawn at its own invert offset, plus a plan-view compass
  inset of link headings. The same drawing now backs a live preview pane in
  the **cross-section editor** and a per-type layer-stack diagram in the
  **LID control editor**. Geometry is sampled from the engine's
  `swmm_xsect_*` API (`swmm_xsect_width_of_depth_array`), which the GUI had
  never consumed, so the drawing is the geometry the solver uses rather than
  an approximation — and the 26 hand-drawn `*_xsect.svg` shape thumbnails are
  replaced by procedurally rendered, theme-aware icons from that same code.
  Street cross-sections are drawn at full fidelity from the engine's
  `[STREETS]` table, so they render while the model is still being edited;
  irregular (transect) and custom sections render in the cross-section editor
  at any time and in the dock once the model has been validated or run.
- **Five cross-section shapes surfaced.** `BASKETHANDLE`, `SEMICIRCULAR`,
  `FORCE_MAIN`, `CUSTOM` and `DUMMY` are now in the shared shape/geom table
  (`xsectshapegeom.h`), so they appear in the shape picker, the Property
  Browser and the Attribute Table. `CUSTOM` is withheld from the picker until
  a shape-curve picker exists (its geom2 is a curve index).

### Changed

- **Profiles no longer exaggerate slope to fill the pane.** The link profile
  used independent axis scaling stretched to fill the drawing area, which tied
  the vertical exaggeration to the pane's aspect ratio — the same 0.25 % pipe
  read as ~1.3 % in a short dock and ~2.7 % in a tall one, with nothing on the
  drawing to say so. The exaggeration is now derived from the model's own
  proportions — a reach naturally 27:1 long-to-deep, drawn towards a 6:1
  target, asks for 4.4x and snaps down to 4x — so it is identical at every
  dock size. It caps at 10:1 and the achieved V:H is stated on the drawing. A
  short or steep reach comes out at true scale with no distortion at all. The Section View dock gains a **V:H** selector (Auto, 1:1,
  2:1, 5:1, 10:1, 20:1, 50:1) for an explicit choice.
- **Section outlines are sampled on a cosine-spaced depth ladder** instead of a
  uniform one, at a higher default density (240 intervals). Width changes
  fastest at the invert and the crown, so a uniform ladder put the fewest
  points where curvature is highest and left circular and egg-shaped pipes
  visibly faceted top and bottom.

### Fixed

- **Welcome screen painted in the wrong theme, and model tabs floated over
  it as small framed windows.** Three separate causes, all landing on the
  welcome tab.

  `QMdiArea` snapshots its backdrop brush once in its constructor and has
  no `PaletteChange` handling, so the theme installed after the main window
  was built never reached it — and `welcomeWidget`, a plain `QWidget` that
  paints no background of its own, showed that stale brush across the whole
  tab.

  That same transparency is why a model tab appeared *over* the welcome
  screen after welcome → model → welcome. In `TabbedView` `QMdiArea` never
  hides the outgoing sub-window: `_q_deactivateAllWindows` only calls
  `showNormal()` on it and counts on the incoming maximized window to cover
  the viewport. The model therefore stays in the viewport as a 200×150
  framed child, correctly z-ordered *underneath* the welcome — but a
  welcome that paints nothing is not a cover, so the user saw a detached,
  undocked model window through it. `welcomeWidget` now fills with
  `QPalette::Window` (already the `surfaceWindow` token the backdrop uses,
  so the screen looks unchanged, and re-read at every paint so the
  Appearance switch still tracks).

  Finally, Qt hands the maximized state over only from a predecessor that
  is both maximized *and* visible. Hiding the welcome tab in place (startup
  toggle off, or the tab's X) broke that chain and left the next tab in
  Normal state. The two document-open boundaries and the tab-close path now
  maximize the incoming window themselves, which repairs that lifecycle
  edge without making ordinary tab switching anything but a plain switch —
  so a welcome tab the user dismissed stays dismissed.

- **Cross-section editor never showed the transect picker for IRREGULAR
  conduits.** `LinkCompoundEditDialog` compared the shape against a stale
  literal `19`, which is `VERT_ELLIPSE` under the 6.0 `SWMM_XSectShape`
  numbering — `SWMM_XSECT_IRREGULAR` is `21`. Opening the dialog on an
  irregular conduit therefore hid the transect name picker and left the raw
  geom1 index visible. All shape comparisons in that file now use the named
  `SWMM_XSECT_*` / `openswmmvis::kXsect*Id` constants.

- **Bundled Bellinge 2D example with copy-on-open.** The Welcome page's
  Example Projects panel now understands directory-per-example bundles (an
  `.inp` + `.oswp` + data sidecars + optional `example.json` manifest for the
  display name/description) alongside the legacy flat `.inp` examples, and
  ships the Bellinge coupled 1D/2D urban flood model (mesh, 11-year rain
  record, SRTM DEM) under `examples/bellinge_2d/`. At startup the bundled
  payload is seeded into the per-user data dir
  (`QStandardPaths::AppLocalDataLocation/examples`, version-marker fast
  path), and clicking an example always copies it to a user-chosen folder
  before opening — the baseline is never opened in place, so simulation
  results can't overwrite it.
- **Add Basemap ▸ Local File**: local image basemaps (GeoTIFF/PNG/JPEG/BMP)
  with world-file + CRS assignment via GDAL PAM, rendered through the
  existing raster tile pyramid.

### Fixed

- **Object Browser now refreshes when data objects are added, removed, or
  renamed** (time series, curves, patterns, aquifers, …), including staged
  edits not yet flushed to the engine; the mesh-editing toolbar's BC
  time-series/curve combos and coupled-node dropdown refresh likewise.
- **Application settings identity is now set before the main window is
  constructed**, so all settings land in one file. One-time effect: saved
  window layout and recent-file lists from earlier builds reset.

## [6.0.0-alpha.3] — 2026-07-29

Continues the SWMMVis rewrite from `6.0.0-alpha.2`. The bulk of this
cycle is the **Mesh Tiled LOD** effort — moving the 2D mesh layer onto
the scene-graph renderer with a progressive, off-thread load — plus a run
of mesh-generation robustness fixes found on real regional datasets, mesh
persistence across save/reopen, and the first GIS-layer round-tripping.

### Added

- **2D mesh layer on the QSG path, with a progressive off-thread load.** The
  mesh renderer (`SWMM2DMeshQSGRenderer`) is now instantiated at the bottom of
  the scene-graph stack (mesh → 2D results → 1D network), owning the frame
  under the same single-owner rules as the 2D results renderer; a masked mesh,
  or a visible 2D results layer forced onto the CPU, keeps the mesh on the
  `QPainter` fallback so stacking order can never invert.
  `SWMM2DMeshLayer::qsgOwnsRendering()` gates
  `SWMM2DMeshGraphicsItem::paint()` so the two pipelines never double-paint.
  Controlled by preference `Rendering/QsgMeshEnabled` (default on), a
  Preferences → GPU Rendering checkbox, and the `OPENSWMM_QSG_MESH=0` env
  kill-switch. `attachMesh2DLayersAsync` replaces the synchronous 2D-mesh +
  prior-run HDF5 auto-load in `finalizeSingleINPOpen`, moving the `.inp`/`.2dm`
  parse and the scene-geometry build onto a `QtConcurrent` worker (36 s
  combined on the 5M-triangle baseline). A `deferHeavyGeometry` mode then
  splits the build itself: the constructor produces only scene triangles,
  bbox, z-range, vertex dots and the LOD pyramid so the layer joins the canvas
  and renders its coarse levels at once, and
  `finishSceneGeometryAsync()` builds the deduplicated wireframe edges,
  spatial culling grids, vertex adjacency and boundary-condition slots on a
  worker, swapping them in on the GUI thread under a revision check against
  mid-build CRS reprojection. Both renderers force the pyramid until the
  spatial grids exist, then switch on `sceneGeometryReady`. On the
  9.4M-triangle Virginia mesh the terrain now appears at ~43 s (31 s parse +
  12 s display build) instead of ~88 s, with the remaining ~44 s in the
  background.
- **Stage-by-stage load narration in the Message Log.** A long open previously
  showed "Opening …" and then silence. The open chain now reports every stage
  to the Message Log and status bar — loading the 1D network on a background
  thread, "Applied project settings from `<oswp>` (N ms)", "Scanning `<inp>`
  for a 2D mesh …", worker-thread parse counts and timings, scene-geometry /
  spatial-index / LOD-pyramid build times, layer adoption, and a "No 2D mesh
  sections in this model." closure for 1D-only models — mirrored to the
  `openswmm.load.mesh` logging category.
- **Per-layer zoom thresholds for the mesh wireframe and vertex dots.** The
  wireframe and vertex markers now appear automatically once cells project
  large enough on screen and stay hidden at far zoom, where they are sub-pixel
  noise. The vertex sublayer defaults visible and the LOD gate — not a hidden
  flag — keeps the far view clean; the `QPainter` fallback gained the same
  projected-cell-area gates the QSG path already applied. Both are per-layer
  configurable via `SWMM2DMeshLayer::edgeZoomMinCellPx` /
  `vertexZoomMinCellPx` (minimum on-screen cell size in pixels, `0` = always
  draw), surfaced as "Show edges at:" / "Show vertices at:" spins in the mesh
  style editor's Display group (range 0–40 px/cell in 0.5 steps) and read by
  the QSG path through `Qsg2DLodInputs::edgeMinCellAreaPx` /
  `markerMinCellAreaPx`. The defaults are edge 3.0 and vertex 6.0 px/cell
  (9 px² and 36 px² of projected cell area), and all three LOD buckets — Far
  included — apply the same threshold gate, so the documented "0 = always show
  when enabled" contract holds across the whole zoom range while the defaults
  still suppress dense million-scale wireframe when zoomed out. The 2D results
  renderers keep their 200 px² marker default, above the Mid bucket's 160 px²
  ceiling, so the gate is a no-op for them. Verified on a dense 80k-triangle
  grid: zero dark ink at full extent, a clear wireframe at ~10 visible cells,
  a raised threshold suppressing it and a `0` threshold forcing it on.
- **Classification editor for mesh terrain fill and elevation bands.** The mesh
  style editor edits both the terrain fill and the elevation contour bands
  through the shared `ClassificationEditor`, so instead of a high/low colour
  pair there is a ramp/band selector with invert, Continuous vs. Classified
  mode, a method choice (equal interval / quantile / natural breaks (Jenks) /
  std-dev / logarithmic / exponential / manual), class count, custom min/max
  range, an "Auto-classify from data" resample button, and an editable
  Lower/Upper/Colour/Label bin table. Each editor binds through a
  `SublayerSchemeBinding` around the fill/band style's
  `scheme()`/`setScheme()`, mirroring `swmm2dresultsstylepanel`; the sample
  provider feeds the new `SWMM2DMeshLayer::elevationSamples()` (per-vertex z,
  strided to ≤200k so resample stays responsive on multi-million-vertex
  meshes) and the range provider feeds `zMin()`/`zMax()`. The editor is
  regrouped into Terrain fill (classification), Hillshade, Elevation contour
  lines, and Filled elevation bands; terrain defaults to Continuous, bands to
  Classified.
- **Re-runnable "Remap 1D↔2D" operation, decoupled from mesh generation.**
  Mesh generation now produces geometry only; a separate mapper assigns model
  nodes to it and can be re-run from a toolbar action beside Auto-couple, with
  Add-missing / Re-map-all modes and a summary of vertex/cell/shared/skipped/
  outside counts. `mesh::mapNodesToMesh` claims the nearest coincident vertex
  once, else resolves point-in-triangle over a spatial grid (edge-inclusive,
  lowest index wins), else reports the node unmatched; interior nodes couple to
  their containing cell and several nodes may share one cell, defaulting to
  Cd 0.65 / area 2.0 m². A node already coupled to its own vertex is reported
  skipped rather than also cell-coupled, since two coupling points for one node
  exchange twice. Grid boxes and probes are inflated by the tolerance —
  `MeshSpatialGrid` drops zero-area bboxes on rebuild and rejects zero-area
  query rects, so degenerate point rects index and match nothing. Cell
  couplings round-trip through `MeshResult::cellCouplings`,
  `SWMM2DMeshLayer::applyCellCouplings` (a wholesale swap returning the
  previous rows so an undo command can restore them), `InpMeshWriter`'s CD/AREA
  rows, `InpMeshReader`, and `MeshEngineSync`. Also adds
  `mesh::computeCellAreaStats` (min/max/mean/median) as four rows on the layer
  Metadata tab.
- **Boundary-aware DTM terrain seeding.** Terrain Steiner candidates were
  sampled over the domain bounding box with no awareness of the PSLG, so pixel
  centres landing arbitrarily close to constrained boundary/hole/conduit
  segments (or to mandatory SWMM node vertices) forced slivers that the `-q`
  quality pass then split into clusters of tiny cells hugging the boundary. The
  worker now rejects candidates outside the domain rings, inside a hole ring,
  within a buffer of any constrained segment (segment spatial hash + clamped
  point-to-segment distance), or within the buffer of a mandatory Steiner
  vertex, logging per-category reject counts. The buffer defaults to auto =
  0.5 × effective terrain spacing, exposed as a "Boundary buffer" spin with an
  `(auto)` special value. An optional "Max boundary edge length" control
  splits domain/hole ring edges into equal parts after RDP simplification
  (pure vertex insertion), off by default. The segment hash bins each
  constrained segment in ≤ `bufferDist` chunks, so total insertions are
  O(len/buffer) rather than O((len/buffer)²) for a diagonal segment, with the
  3×3 query unchanged and still exact; the point-in-ring test is a y-banded
  odd-even crossing over all rings, so each candidate visits only edges near
  its scanline (both validated against brute force, 0 mismatches over 50k
  random queries), and a 20M-chunk ceiling makes any future unit slip warn and
  skip only the near-segment rejection instead of running unbounded, with chunk
  counting in `double`/`qint64`. Generation logs `stageMark()` wall times for
  every heavy step (thinning, reprojection, Poisson, each boundary-filter
  phase, Triangle) with chunk/cell/insertion counters, names the running
  sub-stage in the progress text rather than resting on one percentage through
  the heavy stretch, and polls Stop every 64k candidates in the rejection
  loop. On the Bellinge
  subcatchment-union domain the filter takes 62,701 candidates to 37,630
  (24,546 outside, 469 near-segment, 56 near-node).
- **Cancellable, gradable Triangle refinement.** A refinement hook is supplied
  through Triangle's `EXTERNAL_TEST` / `triunsuitable()` extension point, so
  `vendor/triangle/` stays untouched (the symbol is left undefined in
  `triangle_lib` and resolved at the final link). Three capabilities ride on
  it: **cancellation** — refinement was previously uninterruptible, leaving the
  Stop button dead for its whole duration; the hook polls a caller predicate
  and, once cancelled, reports every triangle suitable so the bad-triangle
  queue drains and `triangulate()` unwinds through `triangledeinit()`
  (deliberately not a `longjmp`, which would leak the mesh pools — the
  dominant allocation in the pipeline at ~200 B per output vertex), with
  latency bounded by one poll stride; **progress**, which Triangle otherwise
  gives no way to observe; and **a graded size function** — a single global
  `-a<area>` forces one element size everywhere and is the main driver of
  output vertex count on large domains, so `targetAreaAt` lets the caller
  refine only where it matters, superseding `GenerationOptions::maxArea` and
  omitting the global `-a` so the two cannot fight (per-region
  `RegionMarker::maxArea` is unaffected). The hook is stored thread-locally
  behind an RAII guard, since `triunsuitable()` takes no user-data pointer and
  concurrent triangulation must stay correct.
- **Polygons with holes.** A shared `RingPolygon` model in `EditGeometry` with
  a robust interior-point hole seed; GIS vector polygon interior rings render
  as holes through a new `QGraphicsPathItem` (odd-even fill); and 2D mesh
  generation seeds hole regions with a guaranteed-interior point instead of the
  vertex centroid, which could fall outside a non-convex ring and fail to carve
  the hole.
- **Loaded GIS raster and vector layers persist in the project.** Saving now
  records GDAL raster and OGR vector (shapefile, GeoPackage, …) data layers in
  the `.oswp` under a new `gisLayers` array, keyed by source path relative to
  the `.oswp` plus name/visibility/opacity (and the OGR sublayer name for
  vectors); on load they reopen through the same async GDAL/OGR path as
  File → Add Layer and re-add themselves to the canvas on completion,
  mirroring the existing basemap serialize/deserialize. Known gaps: GIS-layer
  symbology is not yet persisted, z-order among GIS layers may differ after an
  async reopen, and WCS/tile-pyramid layers are not covered.
- **Attribute Table support for externally loaded GIS feature layers.** The
  Attribute Table was bound only to the SWMM model plus CSV/TSV tabular
  layers, so imported OGR feature layers (Shapefile/GeoPackage/GeoJSON) had no
  attribute view. A read-only `GISVectorAttributeTableModel` caches a layer's
  fields and features, feature layers are listed in the category dropdown as
  "Features: `<name>`", and the existing source-multiplexing path is reused;
  the delete action no-ops on a non-SWMM source.
- **Feature layer → SWMM objects import dialog.** Tools → Import Feature
  Layer… converts GIS vector features into SWMM objects: points →
  junctions/outfalls/storage/dividers/rain gages, polylines →
  conduits/pumps/orifices/weirs/outlets. Column→attribute mapping with a
  required unique-ID column and JSON presets, three combinable link-endpoint
  strategies (from/to columns, spatial snap, auto-create junctions), a
  skip-or-update conflict policy with independent attribute and geometry
  toggles, a worker-thread dry-run preview, and the whole import as a single
  undo macro (`SetAdapterPropertiesCommand` routes attribute writes through the
  `SWMM*PropertyAdapter` `Q_PROPERTY` system). The planning core
  (`importplanning`) is Qt-Core-only.
- **Delete from the Object Browser and the Attribute Table.** The Object
  Browser context menu gains "Delete…": spatial leaves (junction/outfall/
  storage/divider, conduit/pump/orifice/weir/outlet, subcatchment, rain gage)
  route through the existing `DeleteObjectCommand`, so deletion is undoable and
  node deletion cascades its links exactly as on the map; data-object leaves
  (curve, time series, transect) route through a new undoable
  `DeleteDataObjectCommand` that snapshots the provider's full state, removes
  it via its registry, flushes the engine, and restores it on undo. Data types
  with no engine delete API yet (patterns, pollutants, aquifers, snowpacks, LID
  controls, streets, inlets, land uses, hydrograph groups, control rules) show
  a disabled action. The Attribute Table gains the matching path: Delete /
  Backspace or the right-click action over selected rows, behind a
  confirmation dialog, over a dialog-free `AttributeTablePanel::deleteObjects()`
  core plus `categoryIsDeletable()` so the delete path is unit-testable
  without a modal — mirroring how `selectionAsTsv()` is structured.
- **`MINIMUM_STEP` field and a fast-run preset in the Simulation Options
  dialog.** Adds the previously-missing `MINIMUM_STEP` spin box under
  Hydraulics → Solver, plus a one-click "Apply fast preset" button on the
  Performance tab that sets `THREADS=8` and `MINIMUM_STEP=1.0 s` — the
  conservative fast recipe for 1D/2D-coupled models, ~2.6× faster than
  as-shipped with mass balance as good as or better, benchmarked on the
  Bellinge coupled model. The recipe is factored into a testable static
  `fastPresetValues()` locked by a unit test.
- **VFR cell-closure controls on the 2D simulation-options page.** A "Cell
  closure (wetting / drying)" group exposes `CELL_CLOSURE` (Flat | VFR) and
  `FACE_RECONSTRUCTION` (Mean | VFR face) combos plus a `VFR_MIN_WET_FRAC`
  spin box, wired through the existing `swmm_options_get_ext` /
  `set_ext` path so the engine remains the source of truth and save/run
  round-trips through the engine writer. The engine's shipped default is
  FLAT/MEAN — VFR is measurably slower (1.64–3.3× on the engine's inundation
  benchmarks with the analytic Jacobian active) and therefore opt-in for
  shallow-water cases — so the controls hydrate to Flat/Mean on open.
- **Editable rainfall scale factors.** The gage rainfall scale factor and the
  two new per-subcatchment rain/snow scale factors are surfaced in both edit
  surfaces: the Property Browser (adapter `Q_PROPERTY`s with 1.0-safe
  hand-written getters and `> 0` write guards) and the Attribute Table
  (`ColumnSpec` columns with function-pointer setter dispatch). Refs #95.
- **Degree-day snow columns in the RDII decay editor.** Companion to the
  engine's `[RDII_DECAY]` degree-day snow model: the hydrograph editor's
  exponential-decay table grows from 8 to 11 columns with a per-response Snow
  checkbox plus `snow_T` and `snow_ddf` cells that stay greyed until both
  Active and Snow are checked (the existence-is-active convention).
  `applyRdiiDecaySet()` gains defaulted `snowOn`/`snow_T`/`snow_ddf`
  arguments forwarded to the extended `swmm_rdii_decay_set` C API, and readers
  are updated for the extended `swmm_rdii_decay_get` signature.
- **Friendly default name for new untitled projects.** File → New named the
  instance from a temp file called `swmmvis-untitled-<GUID>.inp`, so the layer
  tree showed a long GUID. The temp `.inp` is now `untitled_swmm_instance.inp`
  (its base name becomes the instance's display name), with a numeric `_N`
  qualifier added only when that name is already taken by an open instance or a
  lingering temp file.
- **Startup and snapshot environment hooks.** `SWMMVIS_OPEN_ON_STARTUP=<path.inp>`
  opens a model at launch with no UI interaction;
  `SWMMVIS_STARTUP_SNAPSHOT=<path.png>` zooms to full extent once the mesh lands
  and grabs the main window (canvas plus Message Log dock), with
  `SWMMVIS_STARTUP_SNAPSHOT_ZOOM=<factor>` to zoom into the mesh centre first;
  and `SWMMVIS_SNAPSHOT_MESHSTYLE` / `SWMMVIS_SNAPSHOT_SIMOPTS` grab the
  mesh-style and Simulation Options dialogs. Added to make scene-graph rendering
  verifiable without screen-recording permission, since the offscreen QPA cannot
  read back scene-graph pixels — but usable for any scripted capture.

### Changed

- **Mesh Symbology tabs de-duplicated.** The mesh Symbology editor showed two
  overlapping surfaces: the "Mesh / TIN" tab (`MeshHillshadeEditor`, with the
  `ClassificationScheme`-based colour-band editors) and a "Sublayers" tab whose
  `SymbolStyleAdapter` grids re-exposed the same fill, contour bands and
  contour lines through a flat low/high colour model. That model cannot
  represent a scheme, so it duplicated fill/line editing and — for contour
  bands, sharing one `ContourBandStyle` scheme — fought the Mesh/TIN editor
  last-writer-wins. `SWMM2DMeshLayer::styleSubjects()` no longer exposes the
  fill, contour-band or contour-line sublayers; those are edited solely by the
  Mesh/TIN classification editors, so the colour band, scale and sampling are
  applied one way across the QSG and `QPainter` paths. The Sublayers section
  now carries only Mesh edges and Mesh vertices, and the three controls that
  lived only in the removed adapters — "Show elevation labels" (isolines),
  "Smooth band boundaries" (bands), and a terrain "Fill opacity" spin — move
  into `MeshHillshadeEditor`, wired through `IsolineStyle::setLabels`,
  `ContourBandStyle::setSmoothBands` and `MeshFillSublayer::setOpacity`.
- **Simulation Options uses a list sidebar; Labels tab and mesh dialog
  scroll.** The wide `QTabWidget` in Simulation Options is replaced by a
  settings-style `QListWidget` sidebar plus `QStackedWidget`, mirroring
  `PreferencesDialog`: each `build*Tab()` now returns its page and
  `addCategory()` registers the sidebar row and wraps the page in a scroll
  area. The 2D-module toggle greys the "2D Surface Routing" list row rather
  than a tab, since `QStackedWidget` has no per-page enabled state; the Files
  page keeps its inner sub-tabs, and `readFromEngine`/`writeToEngine`/
  `onApply`/`onAccept` and all validation are unchanged. Layer Properties →
  Labels wraps its `LabelsTab` in the shared
  `OpenSWMM::Ui::wrapInScrollArea` helper (as the Symbology tab already did),
  so the stacked group boxes scroll vertically instead of squeezing, with a
  readable minimum width so combos and spins keep their size. The Generate 2D
  Mesh dialog wraps each tab page (Sources/Quality/Hydraulics) in a scroll
  area and lowers its default to `resize(540, 560)` with a 420 px minimum
  height, keeping the footer (output path, progress, buttons) outside the tabs
  and always visible, so the dialog no longer opens taller than the screen to
  fit its longest page.
- **Bed-elevation contours render at every zoom level.** The QSG mesh renderer
  gated the isoline pass on `lod.drawContours`, which the LOD policy sets false
  in the Far bucket, so contours vanished when zoomed out; the `QPainter`
  fallback never gated them. The marching-triangles output is zoom-invariant,
  cached and culled to the coverage rect, so drawing it at Far is cheap. The
  LOD gate is removed — contours follow the user's `showContours` toggle
  alone, matching the `QPainter` path.
- **2D depth now defaults to the smooth per-vertex fill** instead of
  marching-squares contour bands, which read as discrete steps with thin
  seam/gap artifacts between band polygons. Bands and flat cell fill remain
  available from the layer tree.
- **Mesh-generation defaults retuned.** Triangle's default minimum angle drops
  from 33° to 26°: 33° sat at the very top of the range the tooltip itself
  calls reliable, immediately below the non-termination cliff, refinement cost
  climbs steeply past ~28°, and 33° routinely yielded several times the
  vertices of 26° with no practical gain for 2D routing.
  Junctions-as-Steiner-points now defaults **off** — forcing a vertex at every
  node shreds mesh quality around node clusters that are close for
  non-physical reasons (weir/orifice/pump endpoints) — and the PSLG
  checkboxes are regrouped under "1D geometry influence (optional)". The
  boundary-filter normal-dot threshold default moves from 1.0 to 0.6.
- **Mesh overview bake raised from 15k to 60k cells** (~5 px instead of ~10 px
  blocks at full-screen zoom-to-extent), with a new
  `SWMM2DMeshLayer::rebuildOverviewAsync()` that rebuilds the pyramid on a
  worker behind `overviewBuildStarted`/`Finished` signals, wired to a busy
  bar, status message and Message Log lines at mesh adoption (mirroring the
  raster `.ovr` UX), plus a "Rebuild pyramid" button on the layer-properties
  Metadata tab, disabled while a build is in flight. On a 204k-triangle mesh
  the background bake is identical to the load-time bake.
- **DTM thinning now performs its configured number of passes.** Fixing the
  single-pass defect below changes output: the dialog's default of 3 passes
  now performs three real passes, so meshes are thinned harder than before and
  the threshold/iteration defaults may want re-tuning.
- **Layer tree fills the dock width.** The Name column stretches so the tree
  always occupies the full width of the dock and tracks dock resizes, while
  the Opacity column stays Interactive at a fixed 60 px default so it is never
  pinned by the stretch (`stretchLastSection` stays off).
- **Copyright holder on the GPL v3 notice changed** from "Caleb Buahin" to
  "HydroCouple".
- **`examples/demo_road_culvert` retuned for conveyance and run time.** The
  original layout (Ø0.15 m culvert over 500 m at zero slope, coupling
  AREA 0.4 m²) could convey only ~0.003 m³/s: roughly 17 ML drained into the
  coupled junctions, could not pass, and flooded straight back onto the mesh,
  so no downstream discharge ever appeared and the drain/spill churn dominated
  the run time. The culvert is now Ø0.6 m over just the 60 m road crossing, the
  exchange AREA is matched to the barrel cross-section (0.283 m²), the
  downstream end is a free outfall, the embankment widens to X = 500 ± 10 m,
  and the model runs on a 10 m mesh (101 × 11 vertices, 2000 triangles) with
  `THREADS 4`, `FLUX_DH_EPS 0.01` and `MAX_TIMESTEP 10`. Together with the
  engine's smoothed coupling-volume delivery — spread over the
  `COUPLING_WINDOW` instead of a single-routing-step pulse — the upstream pond
  now genuinely drains through the barrel and discharges visibly on the lee
  side.

### Fixed

- **OpenGL truncated any scene-graph node holding more than ~65k vertices.**
  Qt's batch renderer addresses batched vertices with 16-bit indices, so a
  single `QSGGeometryNode` above the cap wraps those indices and draws garbage
  triangles spanning unrelated primitives. On the mesh renderer — and OpenGL
  is the RHI `main.cpp` forces on macOS — a row-ordered mesh drew only its
  first ~21k triangles, reading as "the mesh shows only a sliver of the top
  end", with partial batches producing far-point "crystal" triangles; Metal
  tolerates giant nodes, which is why the offscreen harness never reproduced
  it. All mesh QSG uploads now split into ≤ 65,532-vertex chunks that spill
  into child geometry nodes (`uploadVertsChunked`), with flat-colour passes
  propagating colour edits to their overflow chunks. The 2D **results**
  renderer had the same unguarded uploads — its edge, isoline, band, cell-fill,
  velocity and highlight passes all wrapped as soon as they crossed the cap,
  which on a real mesh is immediately — and now shares the chunked path; the
  indexed smooth fill was immune (its `UnsignedIntType` index buffer makes the
  batch renderer refuse to merge it), which is why the artifact appeared in the
  edges and contours but not there, and it gains a `pruneOverflowChildren()`
  call because the indexed and expanded paths do alternate at runtime.
  65,532 is the largest value below 2¹⁶−1 divisible by both 3 (bare triangles)
  and 6 (thick-segment quads), so a chunk boundary never splits a primitive.
  Verified with an in-process canvas snapshot of the 9.4M-triangle Virginia
  mesh at zoom-to-extent rendering its complete footprint on GL.
- **Mesh QSG colours were uploaded straight-alpha into a premultiplied
  material.** `QSGVertexColorMaterial` samples vertex colours as premultiplied
  alpha, but the mesh fill, iso-band and vertex-marker passes uploaded
  straight-alpha colours at fill alpha 160. On composite the colours clamped
  toward saturation — ochre → pure yellow, hillshaded tan → salmon pink, and
  the off-white high-terrain ramp stops → background white, which read as the
  mesh being truncated at far zoom. All `ColoredPoint2D` writes now
  premultiply, with the cache keeping straight-alpha RGB so opacity edits stay
  cache-friendly; screenshots at all three zooms are now visually identical
  between the QSG and `QPainter` pipelines. (The 2D results renderer shares the
  straight-alpha pattern and still warrants the same audit.)
- **QML types were registered too late, silently disabling every GPU
  renderer.** `qmlRegisterType` calls now run *before* the application object
  is constructed in `main()`: `SWMMVisApplication`'s constructor builds the
  main window and pumps events for the splash screen, so anything queued
  during construction could create a `MapCanvas` before the OpenSWMM QML
  module existed — `swmmlayer.qml` failed with "module is not installed" and
  every GPU renderer fell back to the CPU painters for the whole session
  (10-second paints on the 9.4M mesh).
- **Far-zoom mesh rendering.** Three separate defects made the QSG mesh look
  blocky or truncated where the CPU painter did not. The QSG fill blended
  hillshade by `hillshadeStrength` (0.3 default) on top of a formula that
  already consumes strength through `zExaggeration`, drawing only ~30% of the
  `QPainter` relief; it is now exact `colour * lit`, identical to
  `SWMM2DMeshGraphicsItem::paint()`. At Far LOD the QSG drew only the coarse
  quad-bake overview, giving blocky fill and a cell-quantised mesh boundary;
  the fill pass now overlays real cells ≥ ~16 px² largest-first via
  `m_trisBySizeDesc`, coverage-rect culled, matching the CPU painter's LOD
  pass. And the Far-bucket threshold itself was misaligned — the QSG switched
  to the overview below 8 px² mean cell area while the `QPainter` fallback
  switches at span < 2 px (= 4 px²), so a whole zoom band rendered blocky
  overview where the CPU path already drew native cells;
  `Qsg2DLodInputs::farMaxCellAreaPx` (default 8, preserving results-renderer
  behaviour) lets the mesh renderer set 4.
- **Mesh style controls that edited the wrong thing, or nothing.** Terrain fill
  colour is driven by the FILL sublayer's classification scheme — the only ramp
  previously reachable from the mesh style editor edited the contour-**band**
  scheme, and bands are off by default, which is the reported "applying a color
  scale does not get applied to the shaded relief". The `QPainter` fill now
  honours the fill scheme, flat colour and ramp inversion via the same
  `schemeDrivesColor` decision as the
  QSG fill, instead of hardcoding the legacy ramp. `hillshadeMinLit` (the
  shadow floor) was a `thread_local` namespace global, so GUI edits were
  invisible to the scene-graph thread and all mesh layers shared one value; it
  is now a per-layer member. The inert "attribute" field is hidden in the
  sublayer style dialog for mesh sublayers, since terrain always classifies by
  bed elevation. `MapCanvas::onLayerRepaintRequested` also invalidates the
  cached QSG frame for `SWMM2DMeshLayer` senders, so style and selection edits
  no longer show a stale frame.
- **Cross-section shape picker wrote the wrong shape for every code from 8
  up.** `kXsectShapes` is the single source of truth for the compound editor,
  the Property Browser and the Attribute Table, and is fed straight to
  `swmm_link_set_xsect`, but it spelled its engine ids as bare integers copied
  from the pre-6.0 `SWMM_XSectShape` enum. Picking EGGSHAPED wrote a
  baskethandle; picking IRREGULAR wrote a vertical ellipse; only 0–7 and STREET
  landed on the chosen shape. The ids are now the `SWMM_XSECT_*` constants
  themselves so the table cannot drift from the engine again, with row order
  still following the legacy SWMM-GUI presentation order. Two further copies
  of the shape-name list — one in the Attribute Table, one in the Property
  Browser, both numbered 0..19 and both mislabelling everything from 8 up — are
  replaced by a shared `xsectShapeName()`, which returns empty rather than
  falling back to CIRCULAR, so a display path renders UNKNOWN instead of
  confidently naming the wrong shape. Requires the matching engine
  renumbering.
- **Mesh generation could appear to hang indefinitely on large domains**, sitting
  at around 37% with the Stop button unresponsive. Not an infinite loop —
  unbounded work, from the grid-step unit slip below compounded by the segment
  spatial hash's insertion cost. Progress text now names the sub-stage instead of
  parking on a percentage, `stageMark()` logs wall time for every heavy step
  (thinning, reprojection, Poisson, each boundary-filter phase, Triangle) with
  chunk/cell/insertion counters, a 20M-chunk ceiling warns and skips the
  near-segment rejection rather than grinding, and the rejection loop checks for
  cancellation every 64k candidates — Stop was previously dead through the
  longest stretch of the pipeline.
- **The DTM grid step was consumed as a mesh-CRS distance.** `gStep` comes from
  the DTM's pixel size and is therefore a DTM-CRS quantity, but every consumer
  below it — the Poisson-disk terrain filter and the boundary-buffer auto
  formula — measures distances in **mesh** CRS units. With a geographic DTM
  (Bellinge: 1 arc-second = 0.000278°) against a projected mesh (EPSG:25832,
  metres) the Poisson filter was applying a 0.000278 spacing threshold to metre
  coordinates and so rejected nothing at all, and the auto boundary buffer came
  out as 0.5 × 0.000278 = 0.14 mm instead of ~12 m — which, since the segment
  hash chunks each edge to buffer length, sent the ~205 km of
  subcatchment-union boundary (7331 edges) to ~1.5 × 10⁹ chunks at ~9 hash
  insertions each: not an infinite loop, just unbounded work plus OOM. A new
  `gStepMesh` converts one grid step at the domain centre through the existing
  DTM→mesh transform, using the mean of the two axis steps since a geographic
  pixel is anisotropic once projected (17.6 m E-W vs. 30.9 m N-S here), and
  `effSpacing` derives from it. On that domain the CRS-correct 12.13 m buffer
  gives 21,336 chunks, a segment hash that returns in 66 ms and a full pipeline
  of 1024 ms.
- **Natural-neighbour vertex elevation crashed the whole mesh pipeline.**
  `NaturalNeighbourInterpolator::build()` passed the switch string `"znQN"` to
  Triangle. Per `triangle.h` the `N` switch leaves `out->pointlist`
  *uninitialised* rather than allocating it, so the copy loop dereferenced NULL
  and segfaulted; the switch was there on a misreading of `N` as "no node
  markers" (that is `B`, for boundary markers, which this code never reads).
  The failure signature was inverted — every degenerate seed set (empty,
  < 3 unique, collinear, NaN coordinates, count mismatch) returned cleanly
  because those paths return before the bad line, while every *valid* seed set
  crashed — observed as a no-DTM mesh run dying in `runMeshPipeline` with
  `EXC_BAD_ACCESS` at `0x0`. Consequence: natural-neighbour vertex elevation
  had never actually produced a value in this pipeline; runs either fell back
  to IDW or died. `meshgenerator.cpp` uses `"pzeA…"` and was never affected.
- **DTM thinning: three defects and a scaling limit.** Iterative decimation
  only ever ran a single pass — every active pixel starts with `inScore == 1`
  and the rescore-enqueue test is `active[ni] && !inScore[ni]`, which can
  therefore never fire, so `nextScore` came back empty and the loop exited
  after iteration 0 regardless of `maxIterations`; `inScore` is now retired for
  the outgoing list each pass, and the removal batch is deactivated before the
  neighbourhood scan so a removed vertex cannot be enqueued as another removed
  vertex's "active neighbour". The block sampler had no out-of-raster bounds
  test (unlike `sampleAt`) and silently edge-clamped, fabricating elevations
  for grid points beyond the DEM footprint — 8500 of 12,100 values on the test
  raster; it now mirrors `sampleAt` exactly and returns NaN so those points
  become inactive. `generatePoints` read the entire clipped domain bbox in one
  `RasterIO`, allocating a float buffer the size of the raster window, which is
  fatal on a multi-GB DEM; it now processes the grid in horizontal bands,
  reading only the raster strip each band needs, so peak buffer memory is
  bounded by a fixed budget while the `RasterIO` call count stays O(bands)
  rather than O(points) — interpolation is unchanged because bilinear sampling
  only ever touches a 2×2 neighbourhood, and `readPixels` gets the same strip
  treatment plus a capped up-front reserve. Finally, grid sizing was
  `int N = cols * rows` with `cols`/`rows` produced by an out-of-range
  `double`→`int` conversion, so the value being range-checked could itself be
  UB; sizing is now done in `double` and `qint64` before narrowing, behind a
  working-set ceiling that counts every simultaneously-live container
  (~46 B/grid point — the previous estimate of 13 counted only a third of
  them).
- **Every DEM sample on a raster's last column with a fractional y was halved
  toward zero.** `RasterIO` fills the bilinear window contiguously in row-major
  order for the region it actually read, and does not honour the
  `{v00, v10, v01, v11}` layout the formula expects — only the 2×2 case
  coincides. For a 1-wide × 2-tall read (last column, interpolating in y) GDAL
  writes the second *row* into the `v10` slot; the old fix-up clobbered it and
  then copied `window[2]`, a slot `RasterIO` never wrote and still zero from
  the initialiser, so `v01 = v11 = 0` and the sample collapsed to
  `v00 × (1 − dy)` — 29.5 instead of 54.0 on the ramp fixture.
- **A freshly generated 2D mesh no longer disappears on save/reopen.** Three
  linked defects along the mesh persistence path. On generation completion the
  dialog only deactivated existing mesh layers and always added a new one, so
  regenerating at an existing path left a stale `SWMM2DMeshLayer` on the canvas
  — and because the save path pushes every mesh layer into the engine, the old
  mesh could win and reappear on reopen; any existing mesh layer whose source
  path matches the new output path is now removed (absolute-path compare)
  before the new layer is added, and generating an external mesh over an
  existing output path warns first. On save, the engine serialises its
  *in-memory* 2D mesh — still the mesh loaded at open, since there is no engine
  mesh-replace API — so a freshly generated external mesh was lost and the old
  one reappeared; the external `.2dm` is now snapshotted *before*
  `swmm_model_write` and restored *after*, then the just-written `.inp` is
  re-pointed at it via `InpMeshWriter::writeMeshFileRef()`, stripping any stale
  inline `[2D_*]` the engine emitted — robust whether the engine clobbers the
  `.2dm` or writes stale inline. That retarget is gated on the reader's
  authoritative `isExternal` flag, now carried onto `SWMM2DMeshLayer`, because
  an **inline** mesh's `sourcePath()` is the `.inp` itself: retargeting one
  would write the pre-save snapshot back over the `.inp` the engine had just
  written, strip the inline `[2D_VERTICES]` / `[2D_TRIANGLES]` /
  `[2D_VERTEX_NODE_MAP]` / `[2D_BOUNDARY_CONDITIONS]` sections and append a
  `[2D_MESH_FILE]` reference pointing at the `.inp` itself, so on reopen the
  mesh would resolve to a file with no vertices and the 2D domain would be
  empty — `examples/demo_road_culvert` 3135 lines short, running in under a
  second with all-zero flows and producing a 1 kB header-only `.2d.h5`, against
  0.236/0.523/0.639 CMS, 113,405 steps and 65 MB of 2D state when it is intact.
  Inline projects therefore keep the engine's own output; they still need an
  engine-side mesh-replace API. Separately, `InpMeshReader` now resolves
  TAG-form first tokens in
  `[2D_VERTEX_NODE_MAP]` — the writer prefers tags, so GUI-written vertex maps
  never round-tripped.
- **2D depth colour scale is anchored to the run's global peak.** `max_depth_`
  is now seeded from a scan over every frame at load, mirroring the existing
  velocity seed, so the true peak depth maps to the top of the ramp from the
  first frame. Previously it only grew as frames were visited, so the deepest
  water never reached the maximum colour until the user scrubbed onto the exact
  peak frame.
- **2D rendering now consumes the engine's wet-masked signed vertex depth
  field.** The layer previously rendered the solver's vertex-*head* field
  (`Mesh2_node_head` / `swmm_2d_vertex_get_heads_bulk`), whose stencil blends
  dry-cell bed elevations into shoreline vertices — water surfaces climbed
  adverse slopes and bed steps with no driving head — and the
  `max(0, head − z)` conversion destroyed the signed sub-cell shoreline
  signal. `Mesh2DH5Reader` gains `readVertexSignedDepthsAt()`
  (`/Mesh2_node_depth`, with a probe-once presence cache); `SimulationRunner`
  calls `swmm_2d_vertex_get_render_depths_bulk` per tick into a new
  `twoDVertexDepthsAvailable` signal, replacing the heads signal, and
  `EngineMesh2DSource` stores the signed floats unclamped;
  `applyCurrentDepths_` keeps negative source vertex depths (the sub-cell
  shoreline intercept) and sanitises only non-finite values. Legacy
  `Mesh2_node_head` is no longer consumed for rendering, and older files fall
  back to the GUI-side reconstruction — whose per-cell η now comes from the
  planar-bed stage-storage inversion (`cellEtaFromMeanDepth`, mirroring the
  engine closure) instead of `z_c + h`, which overstated η on partially wet
  step-spanning cells, with a flat-closure fallback for bad indices or nodata
  z.
- **Animation slider range and scrub lag.** The global slider anchored
  normalised 0.0 at `START_DATE` while data frames begin at period 0's report
  time (`reportStart + reportStep`), leaving a dead zone at the left of the
  track whenever `REPORT_START != START_DATE`, plus an off-by-one in
  `periodIndexForDateTime`; a new
  `SWMMResultsLayer::reportedStartDateTime()` (period 0's time) now anchors
  `driverStartTime()` and the period-index grid. Scrubbing also ran the full
  synchronous data path per drag pixel, and each seek was amplified roughly 8×
  by `dispatchAnimationTick` invalidating every dynamic sublayer (a redundant
  `fetchResultsForStep` per sublayer plus Structural escalation to a full
  `populateScene` rebuild per tick). `SWMMResultsLayer` now overrides
  `dispatchAnimationTick` for a Values-only restyle with no sublayer
  invalidation (`wireRefresh` keeps Structural for style edits), the
  controller-managed 2D fallback is skipped in the `swmmvis` `currentTime`
  fan-out (it was advanced twice per seek), binary search replaces the O(n)
  nearest-frame scans in `setCurrentSimTime` / `setCurrentSimTimeAsOf`, scrub
  seeks are coalesced through a 40 ms trailing-edge timer, and a new
  `CursorWindowSlider::cursorReleased` seeks exactly on drop.
- **Newly added objects appear immediately.** Node/link/subcatchment/gage adds
  updated the structure-of-arrays but never realigned the batched
  `SWMMLayerItem`'s cached bounding rect or the scene's BSP index, so a new
  object outside the stale bounds was culled from `paint()` until a pan or zoom
  reindexed the scene — the documented `m_batchedItem` contract was never
  honoured. `applyNodeAdd` / `applyLinkAdd` / `applyGageAdd` /
  `applySubcatchAdd` now call `m_batchedItem->refreshBoundingRect()`, and those
  paths plus `applyNodeMove` and `applySubcatchVertices` call
  `recomputeExtentFromCaches()` so the cached model extent stays in sync for
  the mesh/zoom gates that read it. On the GPU path the same symptom had a
  second cause: the QSG renderer builds node/link/subcatchment geometry only
  inside its `m_contentDirty` block, driven by `repaintRequested`, and that
  handler absorbs one repaint after a `selectionChanged`
  (`m_selectionPending`) — which could swallow the repaint a newly
  added-and-selected object emits, leaving the object out of GPU geometry until
  an unrelated rebuild and forcing a layer off/on toggle to see e.g. a new
  junction. `SWMMModelLayer::geometryChanged` (emitted on every add, move and
  delete, and exempt from the absorb) is now connected straight to a full
  content rebuild and clears the pending-absorb state.
- **Windows was built with RTTI off and no unwind semantics.** The Windows
  presets set `CMAKE_C_FLAGS` / `CMAKE_CXX_FLAGS` as preset *cache* variables,
  which overwrites CMake's MSVC initialiser (`/DWIN32 /D_WINDOWS /GR /EHsc`)
  rather than adding to it. The entire Windows leg was therefore built without
  `/GR` or `/EHsc` while Qt6 and GDAL ship built with both — an ABI mismatch
  across the link, and a plausible root cause for the Windows-only faults
  scattered across the GUI tests with no Linux or macOS equivalent (prior
  Windows build logs should show a flood of MSVC C4530, which is what an absent
  `/EHsc` produces). Both Windows presets restore
  `/DWIN32 /D_WINDOWS /EHsc /GR`, and `/LTCG` is added to the shared, module
  and static linker flags to match `/GL` on the compile side (only
  `CMAKE_EXE_LINKER_FLAGS` carried it). `test_meshgenerator` runs on the
  Windows leg: the suspected native crash inside vendored `triangle.c` is ruled
  out —
  `test_artifacts/meshgen_repro/` ports the input-building half of
  `MeshGenerator::generate()` to Qt-free types against unmodified `triangle.c`
  and runs all 7 cases plus 4000 randomised PSLGs clean under ASan/UBSan at a
  512 KB stack (Windows default is 1 MB), with zero out-of-range output indices
  and zero `triexit()`/`longjmp` hits; the wrapper's `free()` bookkeeping is
  also correct, already declining to free `out.holelist` / `out.regionlist`,
  which Triangle aliases to the input under `-p`.
- **Windows path normalization in `SaveAsPathNormalizer`.** The project-branch
  directory now comes from `QFileInfo::path()` rather than `absolutePath()`,
  which resolved against the process CWD — making a pure string normalizer
  depend on ambient state and, on Windows, mis-resolving a drive-less rooted
  path like `/p/m.inp.oswp` against the *current drive*, returning `D:/p` and
  producing `D:/p/m.inp` instead of `/p/m.inp`. The non-project branch already
  returned its input verbatim, so preserving the caller's directory as given is
  the consistent behaviour.
- **Windows/macOS CI and packaging.** Engine and vcpkg DLLs are placed on the
  test PATH and PROJ/GDAL data directories are set for the test targets, so the
  Windows leg can run its suite at all; `QTEST_FUNCTION_TIMEOUT` plus a 60 s
  `ctest --timeout` backstop (a 34× margin over the slowest legitimate test on
  any platform, 1.77 s) name a hang instead of letting the job stall; an
  import-table sweep and probe steps were added as a decisive Windows hang
  diagnostic; 12 hanging Windows GUI tests plus
  `test_ioportabilitynormalizer` are quarantined behind an exclude regex
  (hidden, not fixed, each tracked to re-enable) and the compiler-cache bypass
  is turned off; vcpkg binary caching and its cache-save trap are fixed, with
  the save skipped on an exact hit; the whole job gains a 180-minute ceiling
  with tighter per-step caps *below* it (Build and Package 110, Test 25,
  Install 15, Package 20) so a granular timeout fires first and names the
  offending step rather than the job ceiling killing the run anonymously —
  `windeployqt` and NSIS `makensis`, both reached for the first time now that
  tests are quarantined, being the prime suspects; artifacts are stored
  uncompressed (`.dmg`/`.zip`/`.tar.gz` are already compressed) and uploaded
  per platform (Windows `.exe`, no stray `.dmg`), with `packages/` listed and
  the step failed if empty; macOS bundle signing is fixed and DMG packaging
  retries on an `hdiutil` flake, detaching leftover mounts between attempts;
  and four failing tests (`test_timeseries_editor_dialog`,
  `test_userflags_roundtrip`, `test_userflagsmodel`, `test_rptparser`) are
  repaired.

## [6.0.0-alpha.2] — 2026-07-12

The legacy Delphi `epaswmm5.exe` GUI (last shipped as `v5.2.4`, see
below) is being replaced by **SWMMVis**, a new Qt6/C++ application built
directly on `openswmm.engine`. Work started 2025-04-03 as a bare project
skeleton; substantive features begin with commit `9df607d` (2026-04-28).
An informal "Alpha 1" milestone was reached 2026-05-22 (commit
`55098b6`); everything in this section landed between then and the
`6.0.0-alpha.3` version bump on 2026-07-12.

### Added

- **Geometric storage-unit shapes.** CYLINDRICAL, CONICAL, PARABOLOID and
  PYRAMIDAL storage shapes are editable in the Property Browser and the
  Attribute Table. The `StorageShape` enum extends to the engine's six
  values and `storageShape()` now reads `swmm_node_get_storage_shape`
  instead of inferring TABULAR from `curve >= 0`, which could not
  represent the curve-less geometric shapes; `storageParam1/2/3`
  `Q_PROPERTY`s and attribute-table columns bind to the engine's raw
  L/W/Z through `swmm_node_get/set_storage_geometry`; and
  `storageshapegeom.h` supplies per-shape dimension labels and
  applicability so the generic rows get a shape-specific tooltip and
  greying, mirroring the link `geom1`–`geom4` split. Also fixes a
  bootstrap bug: the engine validates a geometric shape's L/W/Z
  atomically, so a node freshly switched to one (dimensions 0/0/0)
  rejected the Property Browser's one-cell-at-a-time edits and the switch
  never took. Both `setStorageShape` paths (adapter and attribute table)
  now seed a valid unit default when the current dimensions don't
  validate, and keep existing valid dimensions on a re-switch.
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
- **2D mesh/results rendering: 1M-cell QSG architecture.** A
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
- **Engine editors:** rain gage, outlet (node/subcatchment),
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
  delegating to the engine's own encode/decode primitives): every call
  site (time-series editors/registry, results-layer clocks, the
  `[EVENTS]` editor, the climatology date field, the 2D auto-load anchor)
  moved off at least four independent hand-rolled OLE-Automation-epoch
  implementations, deleting the old `swmmjuliandatetime.h` shim. Fixes a
  minute-truncation bug (`GH #1`, e.g. 00:15 decoding as 00:14) and a
  `QTimeZone::LocalTime` vs. UTC inconsistency (`GH #2`).
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

- **Report viewer navigation.** The reported "report viewer truncates the
  `.rpt`" behaviour was navigational, not lossy — the viewer holds every
  byte of the raw file, and an offline probe shows 0 dropped characters
  and 0 bad anchors across all sections. Two causes: the block above the
  first `****` rule — engine banner, title/notes and the whole
  WARNING/ERROR list, 15% of a real report — was bookmarked as
  "(untitled)", so the report looked like it began at Element Count; it
  is now "Report Header & Notes", plus a "Warnings & Errors (N)"
  bookmark anchored on the first notice when the block contains any. And
  bookmark clicks used `ensureCursorVisible()`, which scrolls minimally,
  so a downward jump landed the section title on the *bottom* edge of the
  viewport with the body below the fold — indistinguishable from "the
  section has headers but no content"; jumps now scroll the section title
  to the *top* of the viewport by setting the vertical scrollbar to the
  anchor's block number. The viewer is now a `QPlainTextEdit` with a
  line-number gutter: the report renders wholesale with `NoWrap`, so one
  gutter number equals one file line and the last number equals the
  file's line count, making "is anything missing?" answerable at a glance
  against any external editor — and `QPlainTextEdit`'s lazy block layout
  is faster than `QTextEdit` for 11k-line reports.
  `tests/manual/rpt_anchor_probe.py` replicates `RptParser::parse()` plus
  the bookmark construction offline; on `Rich_BC_Baseline_CRST.rpt` it
  verifies 951,304/951,304 characters covered, 28 bookmarks, 0 bad
  anchors.
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
