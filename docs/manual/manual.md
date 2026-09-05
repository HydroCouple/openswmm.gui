@page user_manual User Manual

**SWMMVis** (the OpenSWMM GUI) is a Qt 6 / C++ GIS-based desktop application
for building, running, and analysing SWMM 6.0 storm-water models with the
next-generation OpenSWMM engine. It reads and writes standard SWMM `.inp`
files, adds a GIS map canvas with coordinate reference systems and web/GIS
data layers, and exposes every SWMM 6.0 engine capability — dynamic-wave and
finite-volume 1D routing, 2D overland flow on unstructured meshes, 1D–2D
coupling, advection–reaction–dispersion transport, multispecies reaction
systems, heat transport, water age, groundwater exchange, streets and inlet
junctions — through dialogs, tables, plots, and animated maps.

This manual is organised in five parts. Part I gets you oriented; Parts II–IV
are reference chapters, one per capability area; Part V walks through small
but complete case studies that exercise the engine end-to-end. Every chapter
follows the same pattern: *what you'll do*, *where to find it*, a
*step-by-step* reference, *tips and gotchas*, and *related* chapters.

\figtodo{00_overview_annotated_window.png, SWMMVis main window — ribbon; map canvas; Layers; Object Browser; Properties and Message Logs panels}

## Part I — Getting started

- \subpage manual_introduction — what SWMMVis is, installation, first launch, the Welcome page
- \subpage manual_interface — window layout, ribbon and menus (every item), panels, status bar, command palette, shortcuts
- \subpage manual_projects — projects (`.inp` / `.oswp`), open, save, recents, multiple projects, undo, portability, user flags
- \subpage manual_preferences — the Preferences dialog

## Part II — Map, GIS and layers

- \subpage manual_map_navigation — the map canvas: pan, zoom, measure, export and print
- \subpage manual_crs — coordinate reference systems
- \subpage manual_layers — the Layers panel and every data source (vector, raster, WMS/WMTS/WCS, WFS, XYZ tiles, basemaps, delimited text)
- \subpage manual_styling — symbology, labelling, colour ramps, the Style Manager, legends
- \subpage manual_selection — selecting, searching, and tracing upstream/downstream
- \subpage manual_object_browser — the Object Browser, the Properties panel, compound editors, the Section View
- \subpage manual_attribute_tables — attribute tables, selection queries and bulk edits, mesh tables, tabular data layers
- \subpage manual_map_editing — drawing and editing the network on the map, annotations, importing GIS features

## Part III — Building a model

- \subpage manual_hydrology — rain gages, subcatchments, infiltration, LID controls, snowpacks, aquifers and groundwater, unit hydrographs / RDII
- \subpage manual_hydraulics — nodes, links, cross-sections and transects, streets and inlets, control rules, inflows
- \subpage manual_climate — temperature, evaporation, wind, snowmelt and solar radiation
- \subpage manual_water_quality — pollutants, land uses, build-up/wash-off, treatment, initial quality, reaction systems, water age, heat transport
- \subpage manual_data_objects — time series, curves, patterns, calibration data
- \subpage manual_simulation_options — the Simulation Options dialog, page by page
- \subpage manual_2d_mesh — 2D overland flow: mesh generation, mesh editing, cell parameters, boundary conditions, 1D–2D coupling

## Part IV — Running simulations and analysing results

- \subpage manual_running — running, pausing and cancelling a simulation; status, logs, live results, reports, mass balance
- \subpage manual_results — opening 1D and 2D results, results layers, result styling, legends and map animation
- \subpage manual_time_series_plots — time-series and comparison plots, observed data, statistics, scatter plots, export
- \subpage manual_profile_plots — profile plots along 1D paths, 2D mesh profiles, terrain profiles
- \subpage manual_tabular_results — result columns in the attribute table, the statistics dashboard, the report viewer and export
- \subpage manual_analysis_tools — flow balance, travel time, rainfall visualisation, 2D cell time series, statistics dashboard

## Part V — Tutorials and case studies

Each tutorial is small enough to run in minutes but exercises a distinct set of
engine capabilities. They are ordered from the classic 1D workflow to full
1D–2D coupling.

- \subpage tutorial_site_drainage — a 1D site drainage model with water quality (build-up/wash-off)
- \subpage tutorial_street_inlets — streets, HEC-22 inlets and the SWMM 6 inlet junction
- \subpage tutorial_2d_inundation — from a DTM raster to an animated 2D inundation map
- \subpage tutorial_1d2d_coupling — capped street inlets coupled to a 2D surface
- \subpage tutorial_2d_boundaries — culverts under embankments, edge boundary conditions and edge conveyance
- \subpage tutorial_pure_2d — a stand-alone 2D overland-flow model with system rainfall
- \subpage tutorial_transport — advection–reaction–dispersion transport, reaction systems, water age and heat
- \subpage tutorial_bellinge — a real-world catchment: external mesh, DEM, long rain records and basemaps

## Appendices

- \subpage manual_shortcuts — default keyboard shortcuts
- \subpage manual_file_formats — files SWMMVis reads and writes
- \subpage manual_plugins — engine and GUI plugins
- \subpage manual_performance — redraw policy, large models and performance tuning
- \subpage manual_troubleshooting — common problems and their fixes
- \subpage manual_about — about, credits and licences

## Conventions used in this manual

- Menu paths are written **File → Import → Add Vector Data…**. The same
  commands are on the ribbon; the ribbon tab is named in the *Where to find
  it* section of each chapter.
- Keyboard shortcuts are the defaults; you can change any of them in
  **Help → Keyboard Shortcuts…** (see \ref manual_shortcuts).
- SWMM input-file keywords are written in `MONOSPACE` and refer to sections
  and options of the `.inp` file; the engine's own documentation describes
  their numerical meaning.
- Figures marked *FIGURE PLACEHOLDER* and videos marked *VIDEO PLACEHOLDER*
  are still to be captured. See `docs/manual/README.md` for how to add them.
