# Figure and video placeholders

Generated — do not edit by hand. Refresh with:

    python3 scripts/manual_figures.py todo

Capture a screenshot into `tests/output/manual_figures/`, review it, then
publish it with `python3 scripts/manual_figures.py flip --chapter NN`
(that copies the PNG into `docs/manual/images/` and turns `\figtodo` into
`\fig`). Videos are swapped by hand: `\videotodo{caption}` →
`\video{YOUTUBE_ID, caption}`.

Remaining: 174 figures, 35 videos. Published so far: 159.

## 01_introduction.md

- [ ] `01_install_macos_dmg.png` (line 207) — The macOS disk image with SWMMVis.app being dragged into Applications
- [ ] `01_license_agreement.png` (line 235) — The License Agreement dialog with the GPL v3 notice and the show-on-startup checkbox
- [ ] `01_example_copy_prompt.png` (line 324) — Choosing the destination folder before an example is copied and opened

## 02_interface.md

- [ ] `02_ribbon_compact_modes.png` (line 133) — The same ribbon row at three window widths showing Full; Compact and Collapsed groups
- [ ] `02_menu_model_expanded.png` (line 335) — The Model menu expanded with the Add Node; Add Link; Climate; Data Objects and Mesh submenus

## 03_projects.md

- [ ] `03_file_menu.png` (line 28) — The File menu open with the Open Recent submenu expanded
- [ ] `03_new_untitled_project.png` (line 59) — A freshly created untitled project with an empty canvas and the status bar showing the preference defaults
- [ ] `03_save_as_dialog.png` (line 141) — The Save As dialog with the format dropdown showing the project; input and GeoPackage filters
- [ ] `03_two_projects_tabs.png` (line 180) — Two projects open in adjacent tabs with different flow units in the status bar
- [ ] `03_hotstart_saves_table.png` (line 211) — The scheduled hot-start saves table with one dated row and one end-of-run row
- [ ] `03_portability_warnings.png` (line 380) — Message Logs showing a portability pre-flight warning after a Save As
- [ ] `03_user_flag_values.png` (line 425) — The User Flag Values dialog for a selected junction with one boolean and one real flag

## 05_map_navigation.md

- [ ] `05_canvas_overview.png` (line 26) — The map canvas with a basemap; the SWMM network and the status-bar readouts
- [ ] `05_zoom_rubber_band.png` (line 83) — Zoom In tool dragging a rubber-band rectangle over part of the network
- [ ] `05_hover_tooltip.png` (line 117) — Hover tooltip over a conduit showing its name; type and vertex count
- [ ] `05_measure_panel.png` (line 143) — The measure tool in Area mode with the floating Mode and Units panel
- [ ] `05_canvas_context_menu.png` (line 172) — Canvas context menu on a conduit with the Plot Time Series and Convert To submenus
- [ ] `05_export_map_dialog.png` (line 237) — The Export Map file dialog with the PNG filter selected

## 06_crs.md

- [ ] `06_crs_required_prompt.png` (line 59) — The CRS Required prompt with Choose CRS; Use local projected and Abort Open
- [ ] `06_crs_change_dialog.png` (line 104) — The CRS change dialog offering Reproject stored coordinates or Re-render only
- [ ] `06_2d_results_alignment.png` (line 195) — A 2D results layer correctly aligned over the mesh and the 1D network

## 07_layers.md

- [ ] `07_layer_context_menu.png` (line 135) — The layer-row context menu with the Styles submenu open
- [ ] `07_sublayer_selection_dialog.png` (line 213) — The sublayer selection dialog listing the layers inside a GeoPackage

## 08_styling.md

- [ ] `08_color_ramp_editor.png` (line 199) — The custom colour-ramp editor with the gradient preview and the stop table
- [ ] `08_style_manager.png` (line 255) — The Style Manager with a library entry selected and its preview
- [ ] `08_legend_dock_and_overlay.png` (line 350) — The Legend dock beside the draggable on-canvas legend

## 09_selection.md

- [ ] `09_select_by_polygon.png` (line 109) — A lasso being drawn across a sewer network with the enclosed objects highlighted
- [ ] `09_mesh_edge_selection.png` (line 225) — Boundary edges of a 2D mesh selected along an outfall face
- [ ] `09_confirm_bulk_delete.png` (line 345) — The Confirm Delete prompt for a multi-object selection

## 10_object_browser.md

- [ ] `10_three_docks.png` (line 11) — The Object Browser on the left with the Properties panel and Section View on the right
- [ ] `10_browser_context_menu.png` (line 120) — The Object Browser leaf context menu on a junction
- [ ] `10_offset_mode_toggle.png` (line 208) — The status-bar offset-mode switch with the property rows relabelled to Elevation
- [ ] `10_subcatch_lid_usage.png` (line 277) — The subcatchment compound editor on the LID Usage page
- [ ] `10_convert_to_menu.png` (line 316) — The Convert To submenu with Virtual Junction greyed out and its rule tooltip
- [ ] `10_section_view_link.png` (line 348) — The Section View showing a conduit cross-section at the 1:1 default
- [ ] `10_section_view_profile.png` (line 350) — The same conduit as a profile at the 10:1 default — the exaggeration stated under the drawing
- [ ] `10_section_view_node.png` (line 352) — The Section View showing a node profile with four connecting links

## 11_attribute_tables.md

- [ ] `11_dynamics_columns.png` (line 111) — The right-hand dynamics block of the Conduits table after a run
- [ ] `11_query_and_selection.png` (line 175) — A WHERE clause matching 47 of 1205 rows with the Replace radio armed
- [ ] `11_apply_value_to_rows.png` (line 217) — The right-click menu offering to apply one roughness value to twelve selected conduits
- [ ] `11_mesh_cells_smallest_first.png` (line 360) — The mesh Cells table sorted ascending by area with the smallest cells selected
- [ ] `11_assign_infiltration_dialog.png` (line 390) — The Assign Infiltration to Selection dialog with Green-Ampt parameters and the region-tag option

## 12_map_editing.md

- [ ] `12_inline_vertex_edit.png` (line 86) — A conduit in edit mode with interior vertex handles and two selected
- [ ] `12_add_node_terrain.png` (line 150) — Placing a junction with a DTM active on the Terrain toolbar
- [ ] `12_inlet_junction_setup.png` (line 216) — The Inlet Junction Setup dialog collecting a design and a capture node
- [ ] `12_add_conduit_snap.png` (line 231) — Drawing a conduit with the snap indicator ringing the target node
- [ ] `12_annotation_style_dialog.png` (line 290) — The Add Text Annotation dialog with the halo and background groups expanded
- [ ] `12_import_feature_layer.png` (line 338) — The Import Feature Layer dialog with the attribute mapping table and a preview

## 12a_feature_layers.md

- [ ] `12a_feature_vertices.png` (line 91) — A selected feature with its part; ring and vertex coordinates in the Features panel

## 13_hydrology.md

- [ ] `13_model_data_objects_menu.png` (line 27) — The Model → Data Objects submenu with the hydrology editors
- [ ] `13_raingage_properties.png` (line 98) — Rain gage properties with a file data source
- [ ] `13_assign_rain_gages.png` (line 123) — The Assign Rain Gages dialog previewing an interpolated plan
- [ ] `13_lid_types.png` (line 247) — The LID type list showing all eight control types

## 14_hydraulics.md

- [ ] `14_offset_mode_toggle.png` (line 49) — The status-bar offset-mode toggle in elevation mode
- [ ] `14_inlet_junction_setup.png` (line 138) — The Inlet Junction Setup dialog
- [ ] `14_inlet_junction_properties.png` (line 140) — Inlet junction property rows with the dashed capture-node connector on the map
- [ ] `14_storage_shapes.png` (line 193) — Storage shape rows for a conical unit
- [ ] `14_treatment_page.png` (line 246) — The Pollutant Treatment page with a validated expression
- [ ] `14_transect_editor.png` (line 396) — The Transect editor with the station table and the section chart
- [ ] `14_rules_completion.png` (line 515) — Rule completion offering live node names

## 15_climate.md

- [ ] `15_temperature_tab.png` (line 67) — The Temperature tab with an external climate file selected

## 16_water_quality.md

- [ ] `16_quality_overview.png` (line 14) — The Model menu quality entries — Data Objects with Pollutants and Land Uses; plus Initial Quality; Reaction System and Heat Configuration
- [ ] `16_treatment_editor.png` (line 140) — The Treatment page of the node compound editor with a validated expression
- [ ] `16_initial_quality.png` (line 157) — The Initial Quality dialog with node and link rows
- [ ] `16_species_attributes.png` (line 400) — A results style panel listing pollutant species alongside water age and temperature

## 17_data_objects.md

- [ ] `17_data_objects_menu.png` (line 13) — The Model → Data Objects submenu
- [ ] `17_timeseries_chart_edit.png` (line 101) — Editing time-series points on the chart with a rubber-band selection
- [ ] `17_timeseries_source_card.png` (line 126) — The source card linking a series to a CSV column
- [ ] `17_curve_type_combo.png` (line 186) — The curve type combo with the eleven SWMM curve types
- [ ] `17_observed_comparison.png` (line 238) — A comparison plot with an observed series loaded alongside a simulated one

## 19_2d_mesh.md

- [ ] `19_2d_overview.png` (line 13) — A generated 2D mesh over a DTM with the 1D network and coupled nodes
- [ ] `19_terrain_toolbar.png` (line 68) — The Terrain toolbar with the active raster; vertical unit and invert offsets
- [ ] `19_cell_editor.png` (line 399) — The per-cell parameter editor on the Mesh 2D tab with cells selected
- [ ] `19_assign_cell_data.png` (line 438) — The Assign 2D Cell Data dialog in classified infiltration lookup mode
- [ ] `19_edge_bc.png` (line 487) — Assigning a boundary condition to a selected run of boundary edges
- [ ] `19_coupling.png` (line 531) — Coupled vertices and cells highlighted with their SWMM node ids

## 20_running.md

- [ ] `20_run_overview.png` (line 13) — A simulation in flight — Simulation Status dock; Message Logs and the live map animation
- [ ] `20_run_preconditions_log.png` (line 53) — Message Logs showing the auto-save line and the resolved output paths
- [ ] `20_overwrite_prompt.png` (line 97) — The Overwrite output prompt listing the 1D and 2D results files
- [ ] `20_simulation_status_dock.png` (line 125) — The Simulation Status dock with a running job and its warning children

## 21_results.md

- [ ] `21_results_map_animated.png` (line 10) — The map canvas showing a 1D network coloured by flow over an animated 2D depth surface
- [ ] `21_add_results_dialogs.png` (line 45) — The Open SWMM Output and Add 2D Results file pickers
- [ ] `21_legend_overlay_and_dock.png` (line 123) — The canvas legend overlay beside the dockable Legend panel

## 22_time_series_plots.md

- [ ] `22_series_tree_context_menu.png` (line 113) — The series tree with a run group; a baseline marker and the series context menu
- [ ] `22_load_observed.png` (line 133) — The observed-attribute prompt after choosing a CSV
- [ ] `22_series_style_editor.png` (line 187) — The series property editor showing line and marker groups
- [ ] `22_1v1_scatter_metrics.png` (line 262) — A 1v1 scatter with the identity line and the fit metrics in the title

## 23_profile_plots.md

- [ ] `23_profile_path_picker.png` (line 63) — The path picker listing candidate routes with length; conduit counts and invert drop
- [ ] `23_profile_anatomy.png` (line 98) — An annotated 1D profile identifying ground; inverts; crowns; HGL; max HGL and node glyphs
- [ ] `23_profile_attribute_tracks.png` (line 169) — Two attribute tracks below a profile with the envelope overlay visible
- [ ] `23_2d_mesh_profile.png` (line 234) — A 2D mesh profile with the bed; the animated water surface and the maximum-depth envelope
- [ ] `23_terrain_profile.png` (line 297) — A terrain profile traced over a DEM with the position marker on the map

## 24_tabular_results.md

- [ ] `24_statistics_dashboard.png` (line 79) — The Statistics Dashboard with the Links tab filtered by a query

## 25_analysis_tools.md

- [ ] `25_flow_balance_result.png` (line 51) — Flow-balance analysis beneath a completed output with proportional flow widths on the map
- [ ] `25_travel_time_result.png` (line 53) — Travel-time analysis with time represented by color and flow fraction by width
- [ ] `25_pick_2d_cells_menu.png` (line 138) — A lasso selection of mesh cells with the attribute context menu open
- [ ] `25_statistics_dashboard_histogram.png` (line 163) — The Statistics Dashboard with result tables and query controls

## appendices/a01_shortcuts.md

- [ ] `a01_mesh_edge_path_pick.png` (line 186) — Ctrl-clicking two boundary edges to select the whole run between them
- [ ] `a01_timeseries_editor_keys.png` (line 217) — The time-series editor toolbar showing the Insert; Delete; Copy and Paste actions

## appendices/a02_file_formats.md

- [ ] `a02_oswp_structure.png` (line 155) — An .oswp opened in a text editor showing the sessions and meshLayers blocks
- [ ] `a02_export_filters.png` (line 478) — The Export Map dialog showing the available filters

## appendices/a03_plugins.md

- [ ] `a03_backend_combo.png` (line 286) — The Backend combo in the Performance group of 2D Surface Routing - Performance & Output

## appendices/a04_performance.md

- [ ] `a04_redraw_log.png` (line 304) — Terminal output from SWMMVIS_LOG_REDRAW during a pan and a selection

## appendices/a05_troubleshooting.md

- [ ] `a05_message_logs.png` (line 42) — The Message Logs dock with an error row and its context menu open
- [ ] `a05_basemap_test_connection.png` (line 158) — The Add Basemap dialog's Test Connection result
- [ ] `a05_mesh_generation_error.png` (line 304) — The Generate 2D Mesh dialog reporting a CRS failure
- [ ] `a05_about_copy_environment.png` (line 443) — The About dialog's Copy environment button

## appendices/a06_about.md

- [ ] `a06_license_agreement.png` (line 94) — The startup License Agreement dialog

## tutorials/t01_site_drainage.md

- [ ] `t01_animation_peak.png` (line 322) — The map at the storm peak with links coloured by flow and the legend showing
- [ ] `t01_tss_plot.png` (line 359) — TSS concentration at C1 — C10 and C11 for comparison with their flow hydrographs
- [ ] `t01_profile_plot.png` (line 384) — Profile plot from J3 to the outfall with the maximum HGL envelope
- [ ] `t01_routing_comparison.png` (line 512) — Flow in C11 under dynamic wave — kinematic wave and finite volume
- [ ] `t01_lid_editor.png` (line 543) — The LID Controls dialog with a bio-retention cell defined
- [ ] `t01_lid_usage.png` (line 545) — The LID Usage compound editor on subcatchment S5

## tutorials/t02_street_inlets.md

- [ ] `t02_inlet_editor_custom.png` (line 180) — The Custom1 design rendered as a capture curve
- [ ] `t02_new_inlet_junction.png` (line 249) — The New Inlet Junction dialog with a design and capture node chosen
- [ ] `t02_rule_violation.png` (line 451) — The greyed Convert To entry with the virtual-junction rule text as its tooltip

## tutorials/t03_2d_inundation.md

- [ ] `t03_project_open.png` (line 82) — The Snoopy Lagoon project with the mesh layer and the 1D network
- [ ] `t03_raster_style.png` (line 113) — The raster symbology editor with an auto-stretched ramp and hillshade enabled
- [ ] `t03_mesh_style_panel.png` (line 151) — The mesh style panel on the Terrain Fill tab
- [ ] `t03_mesh_elevation.png` (line 153) — The mesh coloured by bed elevation with hillshade and the wireframe visible
- [ ] `t03_mesh_attribute_table.png` (line 180) — The mesh Vertices table sorted by elevation with the coupled vertex highlighted
- [ ] `t03_mesh_generation_sources.png` (line 223) — The Generate 2D Mesh dialog on the Sources tab with a DTM selected
- [ ] `t03_sim_options_2d.png` (line 280) — The 2D Surface Routing page of Simulation Options
- [ ] `t03_add_2d_results.png` (line 303) — The Add 2D Results file dialog
- [ ] `t03_results_style_panel.png` (line 348) — The 2D results style panel on the Cell Depth Fill tab
- [ ] `t03_animation_peak.png` (line 350) — The bowl at maximum inundation with the depth ramp and legend
- [ ] `t03_velocity_vectors.png` (line 352) — Velocity vectors over the draining bowl
- [ ] `t03_cell_timeseries.png` (line 374) — Depth time series for a rim cell and a centre cell in the Comparison Plot
- [ ] `t03_2d_profile.png` (line 400) — A 2D mesh profile across the bowl with the ground line and water surface
- [ ] `t03_mass_balance_2d.png` (line 430) — The Report Viewer on the 2D Surface Routing Continuity block

## tutorials/t04_1d2d_coupling.md

- [ ] `t04_project_open.png` (line 126) — The capped street project with the inline mesh and the four 1D nodes
- [ ] `t04_vertex_coupling_toolbar.png` (line 169) — The Mesh 2D Vertices group showing vertex 33 coupled to J1 with Cd and Area
- [ ] `t04_vertices_attribute_table.png` (line 171) — The mesh Vertices table with the three coupled rows
- [ ] `t04_coupled_nodes_style.png` (line 173) — The mesh with coupled vertices marked
- [ ] `t04_retired_key_warnings.png` (line 249) — The Message Logs dock with the WARNING 104 lines for the retired 2D options
- [ ] `t04_comparison_1d_2d.png` (line 306) — Node overflow at J1 and J2 against 2D cell depth at the same locations
- [ ] `t04_animation_ponding.png` (line 327) — The plaza at maximum depth with the ramp and legend
- [ ] `t04_outfall_tailwater.png` (line 350) — Head at OUT1 against 2D depth at the coupled corner vertex
- [ ] `t04_profile_2d_overlay.png` (line 377) — Profile from J1 to OUT1 with the 2D water surface overlaid
- [ ] `t04_mass_balance_coupled.png` (line 421) — The Report Viewer on the 2D Surface Routing Continuity block

## tutorials/t05_2d_boundaries.md

- [ ] `t05_road_culvert_overview.png` (line 41) — The road culvert model on the map canvas — flat mesh; transverse embankment; three coupled junctions along the culvert centreline
- [ ] `t05_select_mesh_edges.png` (line 87) — The Select Mesh Edges tool with the whole east outflow face selected and the Edges group showing Normal Flow
- [ ] `t05_edge_attribute_table.png` (line 127) — Mesh edge rows in the attribute table with the BC Type; Bed Slope and Conveyance columns visible
- [ ] `t05_bc_style_tab.png` (line 147) — The Boundary Conditions tab of the mesh layer style dialog with per-type colour and width
- [ ] `t05_coupled_vertex.png` (line 192) — A coupled culvert vertex selected — the mesh toolbar Vertices group showing node J_P00; Cd 0.65 and Area 0.283 m2
- [ ] `t05_profile_across_embankment.png` (line 234) — A 2D mesh profile across the road embankment showing the ponded upstream water surface and the crest
- [ ] `t05_report_continuity.png` (line 277) — The status report's flow routing and 2D surface routing continuity blocks
- [ ] `t05_sim_options_2d_page.png` (line 421) — The 2D Surface Routing page of the Simulation Options dialog
- [ ] `t05_bc_types_reference.png` (line 461) — The reference model's six boundary rows highlighted on the map with per-type colours
- [ ] `t05_edge_conveyance.png` (line 507) — Two interior berm edges selected with the mesh toolbar psi spin set to 0.300

## tutorials/t06_pure_2d.md

- [ ] `t06_vfr_slope_overview.png` (line 38) — The tilted-plane mesh coloured by bed elevation with the rain gage symbol west of the domain
- [ ] `t06_mesh_style_terrain.png` (line 126) — The Terrain Fill tab of the mesh layer style dialog with hillshade settings
- [ ] `t06_rainfall_visualization.png` (line 173) — The Rainfall Visualization dialog showing the one-hour 20 mm per hour block
- [ ] `t06_depth_animation_frame.png` (line 254) — A mid-drainage animation frame — a wedge of water against the west wall and a thin film upslope
- [ ] `t06_cell_timeseries.png` (line 270) — A comparison plot with depth traces from a crest cell and a toe cell
- [ ] `t06_flat_vs_vfr.png` (line 313) — Depth along the ramp under FLAT and under VFR at the same drainage time
- [ ] `t06_generate_mesh_dialog.png` (line 364) — The Generate 2D Mesh dialog on the Sources tab

## tutorials/t07_transport.md

- [ ] `t07_model_layout.png` (line 44) — The five-junction line on the map canvas with the inflow at J1 and the outfall OUT1
- [ ] `t07_initial_quality.png` (line 150) — The Initial Quality dialog with a node row and a link row
- [ ] `t07_water_age_sources.png` (line 180) — The Water Age Sources dialog with global ages and one per-node override
- [ ] `t07_timeseries_species.png` (line 430) — A time-series plot with tracer; decaying constituent; water age and temperature at J1 and J5
- [ ] `t07_solver_comparison.png` (line 467) — A comparison plot of the tracer front at J5 under the legacy; ARD and Lagrangian solvers
- [ ] `t07_map_by_water_age.png` (line 486) — The node symbols graduated by water age mid-flush

## tutorials/t08_bellinge.md

- [ ] `t08_bellinge_overview.png` (line 42) — The Bellinge network over a basemap with the 2D mesh visible
- [ ] `t08_welcome_examples.png` (line 71) — The Welcome page with the bundled examples list
- [ ] `t08_add_basemap_xyz.png` (line 156) — The Add Basemap dialog on the XYZ Tiles tab with the built-in providers
- [ ] `t08_layers_panel_large.png` (line 195) — The Layers panel with all eight categories populated
- [ ] `t08_select_upstream.png` (line 247) — An upstream trace from an outfall highlighting a whole sewershed
- [ ] `t08_simulation_status.png` (line 366) — The Simulation Status panel mid-run with live continuity errors
- [ ] `t08_2d_animation_frame.png` (line 387) — A 2D inundation frame over the Bellinge street network with a basemap underneath
- [ ] `t08_profile_pumped_branch.png` (line 405) — A profile plot through a pumped branch with the HGL at an animation time
- [ ] `t08_rainfall_two_gages.png` (line 468) — The Rainfall Visualization dialog overlaying both Bellinge gages
- [ ] `t08_comparison_two_runs.png` (line 495) — A comparison plot of the same junction under two runs with the animation cursor showing
