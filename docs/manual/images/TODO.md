# Figure and video placeholders

Generated checklist of every `\figtodo` / `\videotodo` in the manual. Capture the screenshot, save it under `docs/manual/images/<file>`, then change `\figtodo` to `\fig` in the chapter (for videos: `\videotodo{caption}` → `\video{YOUTUBE_ID, caption}`). Regenerate with `grep -rn "figtodo\|videotodo" docs/manual`.
Totals: 326 figures, 35 videos.


## 01_introduction.md

- [ ] `01_main_window_annotated.png` (line 19) — The SWMMVis main window after opening a model — ribbon; map canvas; Layers; Object Browser; Properties and Message Logs
- [ ] `01_install_macos_dmg.png` (line 202) — The macOS disk image with SWMMVis.app being dragged into Applications
- [ ] `01_license_agreement.png` (line 230) — The License Agreement dialog with the GPL v3 notice and the show-on-startup checkbox
- [ ] `01_welcome_page.png` (line 248) — The Welcome page showing Start Modeling; Open Recent Files; Learn SWMM and Example Projects
- [ ] `01_example_copy_prompt.png` (line 287) — Choosing the destination folder before an example is copied and opened
- [ ] VIDEO (line 289) — First launch — accepting the licence; touring the Welcome page and opening a bundled example into a working folder
- [ ] `01_first_model_open.png` (line 328) — A freshly opened example model on the map canvas with the status bar bound to the project
- [ ] `01_about_dialog.png` (line 345) — The About dialog listing shipped components with the licence text for the selected entry

## 02_interface.md

- [ ] `02_window_regions.png` (line 24) — The main window with the ribbon; tab strip; MDI workspace; docks and status bar called out
- [ ] `02_ribbon_home_tab.png` (line 79) — The Home tab with its Project; History; Navigate; Select; Inspect; Import and Run groups
- [ ] `02_ribbon_model_tab.png` (line 81) — The Model tab showing the Nodes; Links and Data Objects groups
- [ ] `02_ribbon_compact_modes.png` (line 118) — The same ribbon row at three window widths showing Full; Compact and Collapsed groups
- [ ] `02_menu_model_expanded.png` (line 320) — The Model menu expanded with the Add Node; Add Link; Climate; Data Objects and Mesh submenus
- [ ] `02_docks_default_layout.png` (line 360) — The default dock arrangement with Layers on the left; Object Browser and Properties on the right and Message Logs at the bottom
- [ ] `02_status_bar.png` (line 385) — The status bar with the Engine; Flow Units; Offset Mode; Auto-Length; Coordinates; Map Scale and CRS widgets
- [ ] `02_command_palette.png` (line 398) — The Command Palette filtered to a few matching commands
- [ ] VIDEO (line 400) — A tour of the window — switching ribbon tabs; revealing the contextual Mesh 2D tab; rearranging docks and finding a command with the Command Palette
- [ ] `02_shortcut_editor.png` (line 424) — The Keyboard page of Preferences with a command selected and its key-sequence editor

## 03_projects.md

- [ ] `03_file_menu.png` (line 28) — The File menu open with the Open Recent submenu expanded
- [ ] `03_new_untitled_project.png` (line 59) — A freshly created untitled project with an empty canvas and the status bar showing the preference defaults
- [ ] `03_save_as_dialog.png` (line 141) — The Save As dialog with the format dropdown showing the project; input and GeoPackage filters
- [ ] `03_two_projects_tabs.png` (line 180) — Two projects open in adjacent tabs with different flow units in the status bar
- [ ] `03_hotstart_saves_table.png` (line 211) — The scheduled hot-start saves table with one dated row and one end-of-run row
- [ ] `03_portability_warnings.png` (line 380) — Message Logs showing a portability pre-flight warning after a Save As
- [ ] VIDEO (line 382) — Saving a project — Save As to a new folder; reading the portability pre-flight warnings and confirming the sidecar and 2D mesh travelled with it
- [ ] `03_user_flags_dialog.png` (line 406) — The User Flags dialog with three flag definitions of different types
- [ ] `03_user_flag_values.png` (line 425) — The User Flag Values dialog for a selected junction with one boolean and one real flag

## 04_preferences.md

- [ ] `04_preferences_general.png` (line 25) — The Preferences dialog with the category list on the left and the General page selected
- [ ] `04_preferences_selection.png` (line 78) — The Selection page with the Selection Pens and Fills property tree expanded
- [ ] `04_preferences_rendering.png` (line 106) — The Rendering page with the Link Pens tree and the GPU rendering checkboxes
- [ ] `04_preferences_2d_defaults.png` (line 158) — The 2D Defaults page showing the solver group and the mesh generation defaults
- [ ] `04_preferences_object_defaults.png` (line 180) — The Object Defaults page on the Links tab with the unit-system selector at the top
- [ ] `04_preferences_appearance.png` (line 262) — The Appearance page with the System; Light and Dark radio buttons
- [ ] VIDEO (line 285) — Setting up Preferences before starting a project — simulation and 2D defaults; object defaults for both unit systems and a couple of shortcut rebinds

## 05_map_navigation.md

- [ ] `05_canvas_overview.png` (line 26) — The map canvas with a basemap; the SWMM network and the status-bar readouts
- [ ] `05_zoom_rubber_band.png` (line 83) — Zoom In tool dragging a rubber-band rectangle over part of the network
- [ ] `05_hover_tooltip.png` (line 117) — Hover tooltip over a conduit showing its name; type and vertex count
- [ ] `05_measure_panel.png` (line 143) — The measure tool in Area mode with the floating Mode and Units panel
- [ ] VIDEO (line 145) — Navigating a model — wheel zoom; middle-drag pan; Zoom Extent and a distance measurement
- [ ] `05_canvas_context_menu.png` (line 172) — Canvas context menu on a conduit with the Plot Time Series and Convert To submenus
- [ ] `05_export_map_dialog.png` (line 200) — The Export Map file dialog with the PNG filter selected

## 06_crs.md

- [ ] `06_status_bar_crs_button.png` (line 20) — The Coordinate Reference System button on the status bar
- [ ] `06_crs_required_prompt.png` (line 59) — The CRS Required prompt with Choose CRS; Use local projected and Abort Open
- [ ] `06_crs_selection_dialog.png` (line 83) — The CRS selection dialog with a search term; the type filter and the WKT preview
- [ ] `06_crs_change_dialog.png` (line 104) — The CRS change dialog offering Reproject stored coordinates or Re-render only
- [ ] `06_2d_results_alignment.png` (line 195) — A 2D results layer correctly aligned over the mesh and the 1D network
- [ ] VIDEO (line 197) — Assigning a CRS to a model with none; adding a basemap and re-rendering into Web Mercator

## 07_layers.md

- [ ] `07_layers_panel.png` (line 30) — The Layers panel with a basemap; a DEM; a SWMM model layer and a results layer
- [ ] `07_layer_context_menu.png` (line 113) — The layer-row context menu with the Styles submenu open
- [ ] `07_layer_properties_source_tab.png` (line 133) — The Layer Properties dialog on the Source tab showing the CRS row
- [ ] `07_sublayer_selection_dialog.png` (line 191) — The sublayer selection dialog listing the layers inside a GeoPackage
- [ ] `07_add_basemap_wms_tab.png` (line 281) — The Add Basemap dialog on the WMS / WMTS tab after Connect
- [ ] VIDEO (line 337) — Adding an XYZ basemap and a WMS service; then a georeferenced local raster

## 08_styling.md

- [ ] `08_layer_style_dialog.png` (line 23) — The Layer Properties dialog with its vertical tab sidebar on the Symbology tab
- [ ] `08_kind_tree_symbology.png` (line 84) — The kind tree on the Symbology tab with renderer badges beside each SWMM object kind
- [ ] `08_classification_editor.png` (line 145) — The classification editor showing the method combo; class count and the editable breaks table
- [ ] `08_color_ramp_editor.png` (line 199) — The custom colour-ramp editor with the gradient preview and the stop table
- [ ] `08_label_expression_dialog.png` (line 229) — The label expression builder with the field list; the template and the live preview
- [ ] `08_style_manager.png` (line 255) — The Style Manager with a library entry selected and its preview
- [ ] `08_mesh_style_panel.png` (line 290) — The 2D mesh style panel on the Terrain Fill tab with the hillshade group
- [ ] `08_2d_results_style_panel.png` (line 309) — The 2D results style panel on the Depth Isolines tab
- [ ] VIDEO (line 311) — Styling a model — graduated conduits by diameter; labelling junctions and colouring a 2D flood map
- [ ] `08_legend_dock_and_overlay.png` (line 350) — The Legend dock beside the draggable on-canvas legend

## 09_selection.md

- [ ] `09_selection_overview.png` (line 10) — A selection made on the map mirrored in the Object Browser; the Properties panel and the Attribute Table
- [ ] `09_select_tool_preferences.png` (line 93) — Preferences → Selection with the click tolerance; drag threshold and selection pens
- [ ] `09_select_by_polygon.png` (line 109) — A lasso being drawn across a sewer network with the enclosed objects highlighted
- [ ] `09_search_filter.png` (line 146) — The Object Browser filtered to names containing OUT
- [ ] `09_select_upstream.png` (line 177) — The upstream subnetwork of a selected outfall highlighted on the map
- [ ] `09_mesh_edge_selection.png` (line 225) — Boundary edges of a 2D mesh selected along an outfall face
- [ ] VIDEO (line 257) — Selecting a whole 2D boundary run with Ctrl-click path picking
- [ ] `09_confirm_bulk_delete.png` (line 345) — The Confirm Delete prompt for a multi-object selection

## 10_object_browser.md

- [ ] `10_three_docks.png` (line 11) — The Object Browser on the left with the Properties panel and Section View on the right
- [ ] `10_object_tree.png` (line 50) — The Object Browser tree with network categories above the Data Objects divider
- [ ] `10_browser_context_menu.png` (line 120) — The Object Browser leaf context menu on a junction
- [ ] `10_properties_panel.png` (line 165) — The Properties panel showing a storage node with its stats source combo
- [ ] `10_offset_mode_toggle.png` (line 208) — The status-bar offset-mode switch with the property rows relabelled to Elevation
- [ ] `10_node_compound_inflows.png` (line 247) — The node compound editor on the Inflows page
- [ ] `10_link_xsection_editor.png` (line 261) — The cross-section page with a live preview of an arch section
- [ ] `10_subcatch_lid_usage.png` (line 277) — The subcatchment compound editor on the LID Usage page
- [ ] `10_convert_to_menu.png` (line 316) — The Convert To submenu with Virtual Junction greyed out and its rule tooltip
- [ ] `10_section_view_link.png` (line 348) — The Section View showing a conduit cross-section at the 1:1 default
- [ ] `10_section_view_profile.png` (line 350) — The same conduit as a profile at the 10:1 default — the exaggeration stated under the drawing
- [ ] `10_section_view_node.png` (line 352) — The Section View showing a node profile with four connecting links
- [ ] VIDEO (line 369) — Editing a conduit's cross-section and watching the map; Section View and Attribute Table update together

## 11_attribute_tables.md

- [ ] `11_attribute_table_dock.png` (line 11) — The Attribute Table dock showing the Conduits category with the query bar and selection radios
- [ ] `11_dynamics_columns.png` (line 111) — The right-hand dynamics block of the Conduits table after a run
- [ ] `11_query_and_selection.png` (line 175) — A WHERE clause matching 47 of 1205 rows with the Replace radio armed
- [ ] `11_apply_value_to_rows.png` (line 217) — The right-click menu offering to apply one roughness value to twelve selected conduits
- [ ] `11_mesh_cells_smallest_first.png` (line 359) — The mesh Cells table sorted ascending by area with the smallest cells selected
- [ ] `11_assign_infiltration_dialog.png` (line 389) — The Assign Infiltration to Selection dialog with Green-Ampt parameters and the region-tag option
- [ ] VIDEO (line 411) — Querying conduits by slope; selecting the matches and bulk-applying a roughness value

## 12_map_editing.md

- [ ] `12_editing_toolbar.png` (line 11) — The Model ribbon tab with the Edit; Nodes; Links; Subcatchments; Rain Gages and Annotation groups
- [ ] `12_inline_vertex_edit.png` (line 86) — A conduit in edit mode with interior vertex handles and two selected
- [ ] `12_add_node_terrain.png` (line 150) — Placing a junction with a DTM active on the Terrain toolbar
- [ ] `12_inlet_junction_setup.png` (line 183) — The Inlet Junction Setup dialog collecting a design and a capture node
- [ ] `12_add_conduit_snap.png` (line 198) — Drawing a conduit with the snap indicator ringing the target node
- [ ] `12_annotation_style_dialog.png` (line 257) — The Add Text Annotation dialog with the halo and background groups expanded
- [ ] `12_object_defaults_page.png` (line 281) — The Object Defaults preferences page on the Links tab
- [ ] `12_import_feature_layer.png` (line 305) — The Import Feature Layer dialog with the attribute mapping table and a preview
- [ ] VIDEO (line 326) — Importing a manhole shapefile as junctions and a pipe shapefile as conduits with endpoint snapping

## 13_hydrology.md

- [ ] `13_model_data_objects_menu.png` (line 27) — The Model → Data Objects submenu with the hydrology editors
- [ ] `13_raingage_properties.png` (line 69) — Rain gage properties with a file data source
- [ ] `13_assign_rain_gages.png` (line 94) — The Assign Rain Gages dialog previewing an interpolated plan
- [ ] VIDEO (line 96) — Assigning rain gages by Thiessen majority and then by natural-neighbour interpolation
- [ ] `13_subcatchment_properties.png` (line 122) — Subcatchment property rows including the compound Edit buttons
- [ ] `13_subcatch_lid_usage_page.png` (line 173) — The LID Usage page of the subcatchment compound editor
- [ ] `13_lid_control_editor.png` (line 206) — The LID Control editor with the layer-stack diagram
- [ ] `13_lid_types.png` (line 208) — The LID type list showing all eight control types
- [ ] `13_snowpack_editor.png` (line 230) — The Snow Pack editor showing the four parameter groups
- [ ] `13_aquifer_editor.png` (line 256) — The Aquifer editor with the two-zone illustration
- [ ] `13_groundwater_exchange.png` (line 311) — The Groundwater Exchange dialog with a validated LATERAL expression
- [ ] VIDEO (line 313) — Defining an aquifer and wiring a subcatchment's groundwater exchange with a custom GWF expression
- [ ] `13_unit_hydrograph_editor.png` (line 364) — The Unit Hydrograph editor with the RTK tab and the preview plot
- [ ] `13_rdii_decay_tab.png` (line 366) — The Initial Abstraction tab showing linear IA and the exponential decay table

## 14_hydraulics.md

- [ ] `14_model_ribbon_nodes_links.png` (line 29) — The Model ribbon tab with the node and link groups
- [ ] `14_offset_mode_toggle.png` (line 49) — The status-bar offset-mode toggle in elevation mode
- [ ] `14_inlet_junction_setup.png` (line 134) — The Inlet Junction Setup dialog
- [ ] `14_inlet_junction_properties.png` (line 136) — Inlet junction property rows with the dashed capture-node connector on the map
- [ ] `14_storage_shapes.png` (line 189) — Storage shape rows for a conical unit
- [ ] `14_node_inflows_page.png` (line 240) — The External Inflows page of the node compound editor
- [ ] `14_treatment_page.png` (line 242) — The Pollutant Treatment page with a validated expression
- [ ] VIDEO (line 244) — Adding a time-series inflow and a dry-weather-flow pattern to a junction
- [ ] `14_link_properties_conduit.png` (line 314) — Conduit property rows including cross-section and losses
- [ ] `14_xsection_editor.png` (line 367) — The cross-section editor with the shape gallery and the live section preview
- [ ] `14_transect_editor.png` (line 392) — The Transect editor with the station table and the section chart
- [ ] `14_street_editor.png` (line 416) — The Street editor with the schematic section preview
- [ ] `14_inlet_editor.png` (line 451) — The Inlet editor with the plan-and-section drawing
- [ ] `14_inlet_usage_page.png` (line 468) — The Inlets page of the link compound editor
- [ ] VIDEO (line 470) — Splitting a street conduit into an inlet junction and wiring its capture node
- [ ] `14_rules_editor.png` (line 509) — The control rules editor with syntax highlighting and a valid badge
- [ ] `14_rules_completion.png` (line 511) — Rule completion offering live node names

## 15_climate.md

- [ ] `15_climatology_tabs.png` (line 41) — The Climatology dialog showing its six tabs
- [ ] `15_temperature_tab.png` (line 68) — The Temperature tab with an external climate file selected
- [ ] `15_evaporation_tab.png` (line 91) — The Evaporation tab with monthly averages
- [ ] `15_wind_tab.png` (line 103) — The Wind Speed tab with monthly averages
- [ ] `15_snowmelt_tab.png` (line 124) — The Snow Melt tab
- [ ] `15_areal_depletion_tab.png` (line 136) — The Areal Depletion tab with the four preset buttons
- [ ] `15_adjustments_tab.png` (line 149) — The Adjustments tab with monthly multipliers
- [ ] VIDEO (line 151) — Setting up temperature from a climate file and adding monthly evaporation adjustments
- [ ] `15_heat_solar_tab.png` (line 197) — The Solar tab of the Heat Configuration dialog
- [ ] `15_heat_radiative_tab.png` (line 199) — The Radiative tab with the three shortwave source options

## 16_water_quality.md

- [ ] `16_quality_overview.png` (line 14) — The Model menu quality entries — Data Objects with Pollutants and Land Uses; plus Initial Quality; Reaction System and Heat Configuration
- [ ] VIDEO (line 34) — Adding a pollutant with a land use and build-up/wash-off then running and theming the result
- [ ] `16_pollutant_editor.png` (line 45) — The Pollutants editor — list pane on the left and the field form on the right
- [ ] `16_landuse_editor.png` (line 76) — The Land Uses editor with the General && Sweeping tab
- [ ] `16_landuse_buildup.png` (line 92) — The Buildup tab — one row per pollutant with function and coefficients
- [ ] `16_landuse_washoff.png` (line 110) — The Washoff tab with per-pollutant coefficients and sweeping and BMP efficiencies
- [ ] `16_treatment_editor.png` (line 140) — The Treatment page of the node compound editor with a validated expression
- [ ] `16_initial_quality.png` (line 157) — The Initial Quality dialog with node and link rows
- [ ] `16_reaction_system_species.png` (line 189) — The Reaction System editor on the Species tab
- [ ] `16_reaction_expressions.png` (line 219) — The Expressions tab with the syntax-highlighted expression editor and the validation banner
- [ ] `16_reaction_file_tab.png` (line 264) — The File tab showing the serialised .rxn text
- [ ] `16_water_age_sources.png` (line 287) — The Water Age Sources dialog — global ages above and per-node overrides below
- [ ] `16_heat_sources.png` (line 322) — The Sources tab of the Heat Configuration dialog
- [ ] `16_heat_radiative.png` (line 340) — The Radiative tab with the shortwave mode radio buttons and the radiative parameters
- [ ] `16_heat_solar.png` (line 364) — The Solar tab with the site geometry and Bird atmosphere parameters
- [ ] `16_species_attributes.png` (line 400) — A results style panel listing pollutant species alongside water age and temperature

## 17_data_objects.md

- [ ] `17_data_objects_menu.png` (line 13) — The Model → Data Objects submenu
- [ ] VIDEO (line 36) — Creating a rainfall time series from a CSV file then referencing it from a rain gage
- [ ] `17_timeseries_editor.png` (line 46) — The Time Series editor — series list; point grid and chart
- [ ] `17_timeseries_chart_edit.png` (line 101) — Editing time-series points on the chart with a rubber-band selection
- [ ] `17_timeseries_source_card.png` (line 126) — The source card linking a series to a CSV column
- [ ] `17_curve_editor.png` (line 152) — The Curve editor with a storage curve and its preview chart
- [ ] `17_curve_type_combo.png` (line 186) — The curve type combo with the eleven SWMM curve types
- [ ] `17_pattern_editor.png` (line 193) — The Time Pattern editor with an hourly pattern and its step-line preview
- [ ] `17_observed_comparison.png` (line 238) — A comparison plot with an observed series loaded alongside a simulated one

## 18_simulation_options.md

- [ ] `18_options_dialog.png` (line 13) — The Simulation Options dialog with the category sidebar and the Models / Processes page
- [ ] VIDEO (line 39) — Setting up a dynamic-wave run — dates; time steps; solver tolerances and threads
- [ ] `18_title_notes.png` (line 51) — The Title / Notes page with its formatting toolbar
- [ ] `18_models_processes.png` (line 55) — The Models / Processes page — process models; modules and active processes
- [ ] `18_dates_times.png` (line 102) — The Dates & Times page with the simulation window; time steps and the Events table
- [ ] `18_routing_hydraulics.png` (line 161) — The Routing & Hydraulics page — surcharge handling and the solver group
- [ ] `18_fv_groups.png` (line 217) — The finite-volume solver and performance groups enabled under FV routing
- [ ] `18_quality_transport.png` (line 251) — The Quality & Transport page with the solver selection and the reserved-species group
- [ ] `18_performance.png` (line 301) — The System / Performance page with the thread spin and the effective-thread summary
- [ ] `18_spatial_crs.png` (line 327) — The Spatial & CRS page showing the layer CRS and the model extent
- [ ] `18_mesh_page.png` (line 347) — The Mesh page listing candidate .2dm files next to the project
- [ ] `18_2d_options.png` (line 372) — The 2D Surface Routing page with the explicit marcher and coupling groups
- [ ] `18_files_subtab.png` (line 470) — The Files sub-tab with the secondary file references and the scheduled hot-start saves
- [ ] `18_output_subtab.png` (line 494) — The Output sub-tab with writer combos and the report contents group
- [ ] `18_plugins_subtab.png` (line 533) — The Plugins sub-tab editing the model's PLUGINS section

## 19_2d_mesh.md

- [ ] `19_2d_overview.png` (line 13) — A generated 2D mesh over a DTM with the 1D network and coupled nodes
- [ ] VIDEO (line 33) — From a DTM raster to a generated coupled mesh with boundary conditions
- [ ] `19_terrain_toolbar.png` (line 67) — The Terrain toolbar with the active raster; vertical unit and invert offsets
- [ ] `19_generate_mesh_sources.png` (line 85) — The Generate 2D Mesh dialog on the Sources tab
- [ ] `19_generate_mesh_quality.png` (line 132) — The Quality tab with the Triangle quality and minimum cell size groups
- [ ] `19_generate_mesh_hydraulics.png` (line 185) — The Hydraulics tab with the initial cell values and the region defaults table
- [ ] `19_mesh2d_ribbon.png` (line 250) — The Mesh 2D ribbon tab with the Mesh; Vertices; Edges; 2D Results; Profile and Coupling groups
- [ ] `19_cell_editor.png` (line 300) — The per-cell parameter editor on the Mesh 2D tab with cells selected
- [ ] `19_assign_cell_data.png` (line 340) — The Assign 2D Cell Data dialog in classified infiltration lookup mode
- [ ] `19_edge_bc.png` (line 389) — Assigning a boundary condition to a selected run of boundary edges
- [ ] `19_coupling.png` (line 433) — Coupled vertices and cells highlighted with their SWMM node ids
- [ ] `19_gw2d_preview.png` (line 489) — The 2D Groundwater preview dialog with its disabled inputs and banner
- [ ] `19_mesh_style_panel.png` (line 508) — The 2D mesh style panel showing the terrain fill and isoline tabs

## 20_running.md

- [ ] `20_run_overview.png` (line 13) — A simulation in flight — Simulation Status dock; Message Logs and the live map animation
- [ ] `20_run_preconditions_log.png` (line 53) — Message Logs showing the auto-save line and the resolved output paths
- [ ] `20_overwrite_prompt.png` (line 97) — The Overwrite output prompt listing the 1D and 2D results files
- [ ] `20_simulation_status_dock.png` (line 125) — The Simulation Status dock with a running job and its warning children
- [ ] `20_message_logs.png` (line 142) — The Message Logs dock with an engine warning and its context menu
- [ ] VIDEO (line 160) — Running a model — Execute; watching progress; pausing and stopping with partial results
- [ ] `20_live_toggles.png` (line 188) — The Live 1D and Live 2D check boxes in the Results ribbon Display group
- [ ] `20_report_viewer.png` (line 236) — The report viewer with the section list; the continuity banner and the search bar

## 21_results.md

- [ ] `21_results_map_animated.png` (line 10) — The map canvas showing a 1D network coloured by flow over an animated 2D depth surface
- [ ] `21_add_results_dialogs.png` (line 45) — The Open SWMM Output and Add 2D Results file pickers
- [ ] `21_active_results_combos.png` (line 74) — The 1D results and 2D results selectors in the Analysis ribbon
- [ ] `21_results_symbology_tab.png` (line 110) — The Symbology tab of a results layer with a graduated flow renderer
- [ ] `21_legend_overlay_and_dock.png` (line 123) — The canvas legend overlay beside the dockable Legend panel
- [ ] `21_results_ribbon_tab.png` (line 166) — The Results ribbon tab — Playback; Timeline and Display groups
- [ ] VIDEO (line 168) — Loading a run and animating it — choosing the active results layer; colouring by flow and scrubbing the timeline
- [ ] `21_2d_style_panel_tabs.png` (line 191) — The 2D results styling panel showing the Depth Isolines tab

## 22_time_series_plots.md

- [ ] `22_comparison_plot_overview.png` (line 13) — The Comparison Plot dialog — series tree; stacked chart rows; range slider and statistics panel
- [ ] `22_plot_variables_picker.png` (line 43) — The Plot Variables dialog with one group per selected object and the system group
- [ ] `22_series_tree_context_menu.png` (line 113) — The series tree with a run group; a baseline marker and the series context menu
- [ ] `22_load_observed.png` (line 133) — The observed-attribute prompt after choosing a CSV
- [ ] `22_chart_modes_toolbar.png` (line 168) — The Comparison Plot toolbar with the interaction modes and view toggles
- [ ] `22_series_style_editor.png` (line 187) — The series property editor showing line and marker groups
- [ ] `22_statistics_panel.png` (line 226) — The statistics panel with summary columns and fit metrics
- [ ] `22_1v1_scatter_metrics.png` (line 262) — A 1v1 scatter with the identity line and the fit metrics in the title
- [ ] VIDEO (line 264) — Comparing a simulated hydrograph with an observed record — loading a CSV; reading the fit metrics and exporting the data

## 23_profile_plots.md

- [ ] `23_profile_overview.png` (line 14) — A 1D profile plot with the HGL animation and two attribute tracks below it
- [ ] `23_profile_path_picker.png` (line 63) — The path picker listing candidate routes with length; conduit counts and invert drop
- [ ] `23_profile_anatomy.png` (line 99) — An annotated 1D profile identifying ground; inverts; crowns; HGL; max HGL and node glyphs
- [ ] `23_profile_toolbar_layers.png` (line 124) — The profile toolbar and the Layers panel with the HGL and label toggles
- [ ] `23_profile_sources_tab.png` (line 142) — The Sources tab of the profile Display Options dialog
- [ ] `23_profile_attribute_tracks.png` (line 170) — Two attribute tracks below a profile with the envelope overlay visible
- [ ] VIDEO (line 202) — Tracing a profile between two manholes — picking a route; animating the HGL and adding attribute tracks
- [ ] `23_2d_mesh_profile.png` (line 235) — A 2D mesh profile with the bed; the animated water surface and the maximum-depth envelope
- [ ] `23_terrain_profile.png` (line 276) — A terrain profile traced over a DEM with the position marker on the map

## 24_tabular_results.md

- [ ] `24_tabular_overview.png` (line 9) — The Attribute Table dock showing simulated statistics columns beside the model attributes
- [ ] `24_attribute_table_dynamics.png` (line 55) — The dynamics block at the right of a conduit attribute table
- [ ] `24_statistics_dashboard.png` (line 80) — The Statistics Dashboard with the Links tab filtered by a query and its histogram
- [ ] `24_report_viewer_sections.png` (line 96) — The report viewer section list filtered to the continuity sections
- [ ] VIDEO (line 98) — From a finished run to a spreadsheet — filtering the attribute table; summarising the run and exporting CSV

## 25_analysis_tools.md

- [ ] `25_analysis_ribbon.png` (line 10) — The Analysis ribbon tab with the Report; Plots and Network Analysis groups
- [ ] `25_flow_balance_result.png` (line 47) — The Flow Balance summary reporting subnetwork size and boundary inflow-outflow
- [ ] `25_travel_time_result.png` (line 65) — The Travel Time summary for a downstream subnetwork
- [ ] `25_rainfall_visualization.png` (line 102) — The Rainfall Visualization window on the Overlay tab with the gage summary table
- [ ] VIDEO (line 104) — Comparing rain gages — switching to cumulative depth and spotting a gage with a broken rain file
- [ ] `25_pick_2d_cells_menu.png` (line 150) — A lasso selection of mesh cells with the attribute context menu open
- [ ] `25_statistics_dashboard_histogram.png` (line 175) — The statistics dashboard histogram for a selected column

## appendices/a01_shortcuts.md

- [ ] `a01_mesh_edge_path_pick.png` (line 186) — Ctrl-clicking two boundary edges to select the whole run between them
- [ ] `a01_timeseries_editor_keys.png` (line 217) — The time-series editor toolbar showing the Insert; Delete; Copy and Paste actions
- [ ] `a01_shortcut_editor.png` (line 239) — The Keyboard page of Preferences with a command selected and a new sequence being recorded
- [ ] `a01_command_palette.png` (line 308) — The command palette filtered to a few commands showing category chips and shortcuts

## appendices/a02_file_formats.md

- [ ] `a02_oswp_structure.png` (line 155) — An .oswp opened in a text editor showing the sessions and meshLayers blocks
- [ ] `a02_export_filters.png` (line 476) — The Export Map dialog showing the available filters

## appendices/a03_plugins.md

- [ ] `a03_plugins_dialog.png` (line 64) — The Tools Plugins dialog with the role groups expanded
- [ ] `a03_plugins_table.png` (line 158) — The Plugins sub-tab of the Files Output Plugins page in Simulation Options
- [ ] `a03_backend_combo.png` (line 284) — The Backend combo in the Performance group of the 2D Surface Routing page

## appendices/a04_performance.md

- [ ] `a04_redraw_log.png` (line 304) — Terminal output from SWMMVIS_LOG_REDRAW during a pan and a selection

## appendices/a05_troubleshooting.md

- [ ] `a05_message_logs.png` (line 42) — The Message Logs dock with an error row and its context menu open
- [ ] `a05_basemap_test_connection.png` (line 158) — The Add Basemap dialog's Test Connection result
- [ ] `a05_mesh_generation_error.png` (line 304) — The Generate 2D Mesh dialog reporting a CRS failure
- [ ] `a05_about_copy_environment.png` (line 443) — The About dialog's Copy environment button

## appendices/a06_about.md

- [ ] `a06_about_dialog.png` (line 23) — The About dialog with a component selected and its licence in the right pane
- [ ] `a06_license_agreement.png` (line 94) — The startup License Agreement dialog

## manual.md

- [ ] `00_overview_annotated_window.png` (line 19) — SWMMVis main window — ribbon; map canvas; Layers; Object Browser; Properties and Message Logs panels

## tutorials/t01_site_drainage.md

- [ ] VIDEO (line 13) — Opening the site drainage model — setting a CRS — running it and animating the result
- [ ] `t01_open_model.png` (line 72) — The site drainage model on the map canvas immediately after opening
- [ ] `t01_crs_dialog.png` (line 99) — Select Coordinate Reference System with the Local group expanded
- [ ] `t01_basemap.png` (line 101) — The network over a CartoDB Positron basemap
- [ ] `t01_object_browser.png` (line 133) — The Object Browser with subcatchment S5 selected and the Properties dock beside it
- [ ] `t01_attribute_table.png` (line 135) — The conduit attribute table sorted by length
- [ ] `t01_rainfall_visualization.png` (line 162) — Rainfall Visualization showing the 2-yr design storm
- [ ] `t01_landuse_editor.png` (line 195) — The Land Uses dialog on the Buildup tab
- [ ] `t01_coverages.png` (line 197) — The Land Use Coverage compound editor for subcatchment S4
- [ ] `t01_simulation_options.png` (line 223) — Simulation Options on the Models / Processes page
- [ ] `t01_run_status.png` (line 236) — The Simulation Status dock during a run
- [ ] `t01_report_viewer.png` (line 271) — The Report Viewer with the section navigator open on Flow Routing Continuity
- [ ] `t01_statistics_dashboard.png` (line 273) — The Statistics Dashboard on the Links tab
- [ ] `t01_results_style.png` (line 294) — The results style editor classifying link flow with a fixed range
- [ ] `t01_animation_peak.png` (line 296) — The map at the storm peak with links coloured by flow and the legend showing
- [ ] `t01_plot_variables.png` (line 326) — The Plot Variables picker with a node and a link expanded
- [ ] `t01_comparison_plot.png` (line 328) — Depth at J11 and flow in C11 in the Comparison Plot
- [ ] `t01_tss_plot.png` (line 330) — TSS concentration at three conduits showing the first flush
- [ ] `t01_profile_plot.png` (line 355) — Profile plot from J3 to the outfall with the maximum HGL envelope
- [ ] `t01_tabular_results.png` (line 370) — The Attribute Table dock showing the conduit dynamics columns after a run
- [ ] `t01_routing_comparison.png` (line 423) — Flow in C11 under dynamic wave — kinematic wave and finite volume
- [ ] `t01_lid_editor.png` (line 453) — The LID Controls dialog with a bio-retention cell defined
- [ ] `t01_lid_usage.png` (line 455) — The LID Usage compound editor on subcatchment S5

## tutorials/t02_street_inlets.md

- [ ] VIDEO (line 12) — Editing a street cross-section — designing a curb-opening inlet — inserting an inlet junction on a street conduit
- [ ] `t02_open_model.png` (line 62) — The street reach and the parallel sewer after opening the model
- [ ] `t02_section_view_street.png` (line 76) — The Section View showing the ST_MAIN street cross-section at 10:1 exaggeration
- [ ] `t02_street_editor.png` (line 109) — The Street Cross-Sections dialog with ST_MAIN selected and the preview beside it
- [ ] `t02_inlet_editor_curb.png` (line 167) — The Inlet Editor showing the Curb1 design with its elevation and throat drawings
- [ ] `t02_inlet_editor_combo.png` (line 169) — The Combo1 combination design with the sweeper note
- [ ] `t02_inlet_editor_custom.png` (line 171) — The Custom1 design rendered as a capture curve
- [ ] `t02_link_inlet_editor.png` (line 201) — The Edit Link Attribute dialog on the Inlets page for conduit ST_A
- [ ] `t02_inlet_junction_properties.png` (line 218) — The Properties dock for inlet junction IJ1
- [ ] `t02_new_inlet_junction.png` (line 239) — The New Inlet Junction dialog with a design and capture node chosen
- [ ] `t02_report_street_tables.png` (line 301) — The Report Viewer on the Street Inlet Flow Summary with the section navigator open
- [ ] `t02_capture_bypass_plot.png` (line 324) — Flow in ST_A — ST_B and ST_C with the capture gaps visible
- [ ] `t02_rule_violation.png` (line 413) — The greyed Convert To entry with the virtual-junction rule text as its tooltip

## tutorials/t03_2d_inundation.md

- [ ] VIDEO (line 13) — Opening the Snoopy Lagoon project — styling the mesh — animating 2D depth across the bowl
- [ ] `t03_project_open.png` (line 78) — The Snoopy Lagoon project with the mesh layer and the 1D network
- [ ] `t03_raster_style.png` (line 102) — The raster symbology editor with an auto-stretched ramp and hillshade enabled
- [ ] `t03_mesh_style_panel.png` (line 140) — The mesh style panel on the Terrain Fill tab
- [ ] `t03_mesh_elevation.png` (line 142) — The mesh coloured by bed elevation with hillshade and the wireframe visible
- [ ] `t03_mesh_attribute_table.png` (line 169) — The mesh Vertices table sorted by elevation with the coupled vertex highlighted
- [ ] `t03_mesh_generation_sources.png` (line 212) — The Generate 2D Mesh dialog on the Sources tab with a DTM selected
- [ ] `t03_sim_options_2d.png` (line 267) — The 2D Surface Routing page of Simulation Options
- [ ] `t03_add_2d_results.png` (line 290) — The Add 2D Results file dialog
- [ ] `t03_results_style_panel.png` (line 335) — The 2D results style panel on the Cell Depth Fill tab
- [ ] `t03_animation_peak.png` (line 337) — The bowl at maximum inundation with the depth ramp and legend
- [ ] `t03_velocity_vectors.png` (line 339) — Velocity vectors over the draining bowl
- [ ] `t03_cell_timeseries.png` (line 361) — Depth time series for a rim cell and a centre cell in the Comparison Plot
- [ ] `t03_2d_profile.png` (line 387) — A 2D mesh profile across the bowl with the ground line and water surface
- [ ] `t03_mass_balance_2d.png` (line 417) — The Report Viewer on the 2D Surface Routing Continuity block

## tutorials/t04_1d2d_coupling.md

- [ ] VIDEO (line 13) — Finding the vertex-node map — running the coupled model — watching J1 spill onto the plaza and drain back
- [ ] `t04_project_open.png` (line 122) — The capped street project with the inline mesh and the four 1D nodes
- [ ] `t04_vertex_coupling_toolbar.png` (line 165) — The Mesh 2D Vertices group showing vertex 33 coupled to J1 with Cd and Area
- [ ] `t04_vertices_attribute_table.png` (line 167) — The mesh Vertices table with the three coupled rows
- [ ] `t04_coupled_nodes_style.png` (line 169) — The mesh with coupled vertices marked
- [ ] `t04_retired_key_warnings.png` (line 245) — The Message Logs dock with the WARNING 104 lines for the retired 2D options
- [ ] `t04_comparison_1d_2d.png` (line 302) — Node overflow at J1 and J2 against 2D cell depth at the same locations
- [ ] `t04_animation_ponding.png` (line 323) — The plaza at maximum depth with the ramp and legend
- [ ] `t04_outfall_tailwater.png` (line 346) — Head at OUT1 against 2D depth at the coupled corner vertex
- [ ] `t04_profile_2d_overlay.png` (line 373) — Profile from J1 to OUT1 with the 2D water surface overlaid
- [ ] `t04_mass_balance_coupled.png` (line 417) — The Report Viewer on the 2D Surface Routing Continuity block

## tutorials/t05_2d_boundaries.md

- [ ] `t05_road_culvert_overview.png` (line 41) — The road culvert model on the map canvas — flat mesh; transverse embankment; three coupled junctions along the culvert centreline
- [ ] `t05_select_mesh_edges.png` (line 87) — The Select Mesh Edges tool with the whole east outflow face selected and the Edges group showing Normal Flow
- [ ] `t05_edge_attribute_table.png` (line 127) — Mesh edge rows in the attribute table with the BC Type; Bed Slope and Conveyance columns visible
- [ ] `t05_bc_style_tab.png` (line 147) — The Boundary Conditions tab of the mesh layer style dialog with per-type colour and width
- [ ] `t05_coupled_vertex.png` (line 192) — A coupled culvert vertex selected — the mesh toolbar Vertices group showing node J_P00; Cd 0.65 and Area 0.283 m2
- [ ] VIDEO (line 217) — Animating the road-culvert storm — ponding; culvert engagement and road overtopping
- [ ] `t05_profile_across_embankment.png` (line 234) — A 2D mesh profile across the road embankment showing the ponded upstream water surface and the crest
- [ ] `t05_report_continuity.png` (line 277) — The status report's flow routing and 2D surface routing continuity blocks
- [ ] `t05_sim_options_2d_page.png` (line 416) — The 2D Surface Routing page of the Simulation Options dialog
- [ ] `t05_bc_types_reference.png` (line 456) — The reference model's six boundary rows highlighted on the map with per-type colours
- [ ] `t05_edge_conveyance.png` (line 502) — Two interior berm edges selected with the mesh toolbar psi spin set to 0.300

## tutorials/t06_pure_2d.md

- [ ] `t06_vfr_slope_overview.png` (line 34) — The tilted-plane mesh coloured by bed elevation with the rain gage symbol west of the domain
- [ ] `t06_mesh_style_terrain.png` (line 122) — The Terrain Fill tab of the mesh layer style dialog with hillshade settings
- [ ] `t06_rainfall_visualization.png` (line 169) — The Rainfall Visualization dialog showing the one-hour 20 mm per hour block
- [ ] VIDEO (line 190) — Running the stand-alone 2D slope model and animating depth and velocity
- [ ] `t06_depth_animation_frame.png` (line 250) — A mid-drainage animation frame — a wedge of water against the west wall and a thin film upslope
- [ ] `t06_cell_timeseries.png` (line 266) — A comparison plot with depth traces from a crest cell and a toe cell
- [ ] `t06_flat_vs_vfr.png` (line 308) — Depth along the ramp under FLAT and under VFR at the same drainage time
- [ ] `t06_generate_mesh_dialog.png` (line 359) — The Generate 2D Mesh dialog on the Sources tab

## tutorials/t07_transport.md

- [ ] `t07_model_layout.png` (line 44) — The five-junction line on the map canvas with the inflow at J1 and the outfall OUT1
- [ ] `t07_pollutant_editor.png` (line 126) — The Pollutants dialog with TRACER selected
- [ ] `t07_initial_quality.png` (line 150) — The Initial Quality dialog with a node row and a link row
- [ ] `t07_water_age_sources.png` (line 180) — The Water Age Sources dialog with global ages and one per-node override
- [ ] `t07_heat_config_solar.png` (line 230) — The Solar tab of the Heat Configuration dialog
- [ ] `t07_reaction_editor_expressions.png` (line 327) — The Expressions tab of the Reaction System editor with the Arrhenius rate expression
- [ ] `t07_sim_options_quality.png` (line 386) — The Quality and Transport page of the Simulation Options dialog
- [ ] `t07_timeseries_species.png` (line 426) — A time-series plot with tracer; decaying constituent; water age and temperature at J1 and J5
- [ ] `t07_solver_comparison.png` (line 463) — A comparison plot of the tracer front at J5 under the legacy; ARD and Lagrangian solvers
- [ ] `t07_map_by_water_age.png` (line 482) — The node symbols graduated by water age mid-flush
- [ ] VIDEO (line 484) — Building the transport model — pollutants; reaction system; water age; heat; and comparing the three quality solvers

## tutorials/t08_bellinge.md

- [ ] `t08_bellinge_overview.png` (line 42) — The Bellinge network over a basemap with the 2D mesh visible
- [ ] `t08_welcome_examples.png` (line 71) — The Welcome page with the bundled examples list
- [ ] `t08_add_basemap_xyz.png` (line 154) — The Add Basemap dialog on the XYZ Tiles tab with the built-in providers
- [ ] VIDEO (line 156) — Adding an XYZ basemap and the SRTM DEM to the Bellinge project and checking the CRS
- [ ] `t08_layers_panel_large.png` (line 193) — The Layers panel with all eight categories populated
- [ ] `t08_select_upstream.png` (line 245) — An upstream trace from an outfall highlighting a whole sewershed
- [ ] `t08_control_rules_editor.png` (line 278) — The Control Rules Editor with one of the five pump rules selected
- [ ] `t08_simulation_status.png` (line 364) — The Simulation Status panel mid-run with live continuity errors
- [ ] `t08_2d_animation_frame.png` (line 385) — A 2D inundation frame over the Bellinge street network with a basemap underneath
- [ ] `t08_profile_pumped_branch.png` (line 403) — A profile plot through a pumped branch with the HGL at an animation time
- [ ] `t08_rainfall_two_gages.png` (line 466) — The Rainfall Visualization dialog overlaying both Bellinge gages
- [ ] `t08_comparison_two_runs.png` (line 493) — A comparison plot of the same junction under two runs with the animation cursor showing
