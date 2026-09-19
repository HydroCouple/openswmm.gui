<!-- crosswalk-source-ref: fc18a61ed935ee5c1001f6ab08684e63c99c57dd -->
# Legacy manual crosswalk — 2026-09-19

Evidence of record for retiring the four legacy EPA-derived documentation trees
(`docs/user-guide/`, `docs/reference/`, `docs/basic-tutorial/`, `docs/inlet-tutorial/`).
Every `##` and `###` heading in those four files appears in the table below exactly once.

This file is **not** part of the published site: `docs/Doxyfile` `INPUT` lists only
`../README.md ../AUTHORS.md ../include ../src ./manual`. It is tracked in git so it
outlives the files it justifies.

Verify with `python3 scripts/manual_docs_audit.py crosswalk` (add `--strict` to also
assert every target is a real Doxygen page id). After the legacy files are deleted the
checker resolves them through `git show <crosswalk-source-ref>:<path>`, so it keeps
working. `crosswalk-source-ref` is pinned to a commit that still contains the four
legacy files, so it stays resolvable however the deletion is later committed or rebased.

`####` headings (111 of them) are out of scope; each is covered by its parent `###` row.

## Disposition vocabulary

| Verb | Meaning |
|---|---|
| `ported` | The content was carried into the new manual, possibly rewritten. |
| `superseded` | The new manual covers the same ground a different way; nothing to carry. |
| `retired` | The feature does not exist in SWMMVis. Nothing to document. |
| `engine` | Engine truth. Belongs to the `openswmm.engine` docs, not here. |

Engine-side dispositions for the legacy chapters (`Chapter1`–`Chapter13`, `AppendixA`–`AppendixE`)
live in the engine repo's own crosswalk; they are not duplicated here.

## Crosswalk

| Source | Level | Heading | Disposition | Target | Note |
|---|---|---|---|---|---|
| user-guide.md | h2 | Introducing EPA SWMM | superseded | manual_introduction | |
| user-guide.md | h3 | Hydrologic Modeling Features | superseded | manual_introduction | capability list restated for SWMM 6 |
| user-guide.md | h3 | Hydraulic Modeling Features | superseded | manual_introduction | |
| user-guide.md | h3 | Water Quality Modeling Features | superseded | manual_introduction | |
| user-guide.md | h3 | Typical Applications of SWMM | superseded | manual_introduction | |
| user-guide.md | h3 | Steps in Using SWMM | superseded | manual_introduction | |
| user-guide.md | h3 | What's New in Release 5.2.4 | retired | | release notes now live in `docs/releases/` |
| user-guide.md | h2 | SWMM's Conceptual Model | engine | | engine object catalogue and computational methods |
| user-guide.md | h3 | Visual Objects | engine | | |
| user-guide.md | h3 | Non-visual Objects | engine | | |
| user-guide.md | h3 | Tabular Data | engine | | |
| user-guide.md | h3 | Computational Methods | engine | | the three reference manuals own the mathematics |
| user-guide.md | h2 | SWMM's Main Window | superseded | manual_interface | |
| user-guide.md | h3 | Main Menu | superseded | manual_interface | ribbon and menus documented item by item |
| user-guide.md | h3 | Main Toolbar | superseded | manual_interface | |
| user-guide.md | h3 | Map Toolbar | superseded | manual_interface | |
| user-guide.md | h3 | Status Bar | superseded | manual_interface | |
| user-guide.md | h3 | Study Area Map | superseded | manual_map_navigation | |
| user-guide.md | h3 | Project Browser | superseded | manual_object_browser | |
| user-guide.md | h3 | Map Browser | superseded | manual_layers | Layers panel plus result styling replace it |
| user-guide.md | h3 | Property Editor | superseded | manual_object_browser | |
| user-guide.md | h3 | Setting Program Preferences | superseded | manual_preferences | |
| user-guide.md | h2 | Working with Projects | superseded | manual_projects | |
| user-guide.md | h3 | Creating a New Project | superseded | manual_projects | |
| user-guide.md | h3 | Opening an Existing Project | superseded | manual_projects | |
| user-guide.md | h3 | Saving a Project | superseded | manual_projects | |
| user-guide.md | h3 | Setting Project Defaults | superseded | manual_projects | |
| user-guide.md | h3 | Units of Measurement | ported | manual_reference_tables | A7 section *Units of Measurement* |
| user-guide.md | h3 | Link Offset Conventions | superseded | manual_hydraulics | |
| user-guide.md | h3 | Registering Calibration Data | superseded | manual_data_objects | |
| user-guide.md | h3 | Viewing All Project Data | superseded | manual_object_browser | the Section View |
| user-guide.md | h2 | Working with Objects | superseded | manual_map_editing | |
| user-guide.md | h3 | Types of Objects | superseded | manual_property_index | A9 is the consolidated object index |
| user-guide.md | h3 | Adding an Object | superseded | manual_map_editing | |
| user-guide.md | h3 | Selecting an Object | superseded | manual_selection | |
| user-guide.md | h3 | Moving an Object | superseded | manual_map_editing | |
| user-guide.md | h3 | Editing an Object | superseded | manual_object_browser | legacy anchor is malformed `{editing_object}` |
| user-guide.md | h3 | Converting an Object | superseded | manual_map_editing | |
| user-guide.md | h3 | Copying and Pasting Objects | superseded | manual_map_editing | |
| user-guide.md | h3 | Deleting an Object | superseded | manual_map_editing | |
| user-guide.md | h3 | Shaping a Link | superseded | manual_map_editing | |
| user-guide.md | h3 | Shaping a Subcatchment | superseded | manual_map_editing | |
| user-guide.md | h3 | Selecting a Group of Objects | superseded | manual_selection | |
| user-guide.md | h3 | Deleting a Group of Objects | superseded | manual_selection | |
| user-guide.md | h3 | Editing a Group of Objects | superseded | manual_attribute_tables | applying one value to many rows |
| user-guide.md | h2 | Working with the Map | superseded | manual_map_navigation | |
| user-guide.md | h3 | Viewing Map Layers | superseded | manual_layers | |
| user-guide.md | h3 | Selecting a Map Theme | superseded | manual_styling | themes replaced by symbology and result styling |
| user-guide.md | h3 | Setting the Map Dimensions | superseded | manual_crs | extent and units come from the CRS |
| user-guide.md | h3 | Utilizing a Backdrop Image | superseded | manual_layers | raster, WMS/WMTS and XYZ basemaps replace it |
| user-guide.md | h3 | Measuring Distances | superseded | manual_map_navigation | |
| user-guide.md | h3 | Zooming the Map | superseded | manual_map_navigation | |
| user-guide.md | h3 | Panning the Map | superseded | manual_map_navigation | |
| user-guide.md | h3 | Viewing at Full Extent | superseded | manual_map_navigation | |
| user-guide.md | h3 | Finding an Object | superseded | manual_selection | |
| user-guide.md | h3 | Submitting a Map Query | superseded | manual_attribute_tables | selection queries |
| user-guide.md | h3 | Using the Map Legends | superseded | manual_styling | |
| user-guide.md | h3 | Using the Overview Map | superseded | manual_map_navigation | `overviewmappanel.h` exists but is not wired into this release; documented as such |
| user-guide.md | h3 | Setting Map Display Options | superseded | manual_styling | |
| user-guide.md | h3 | Exporting the Map | superseded | manual_map_navigation | |
| user-guide.md | h2 | Running a Simulation | superseded | manual_running | |
| user-guide.md | h3 | Setting Simulation Options | superseded | manual_simulation_options | |
| user-guide.md | h3 | Starting a Simulation | superseded | manual_running | |
| user-guide.md | h3 | Troubleshooting Results | superseded | manual_troubleshooting | |
| user-guide.md | h2 | Viewing Simulation Results | superseded | manual_results | |
| user-guide.md | h3 | Viewing a Status Report | superseded | manual_tabular_results | the report viewer |
| user-guide.md | h3 | Viewing Summary Results | superseded | manual_tabular_results | |
| user-guide.md | h3 | Variables That Can be Viewed | superseded | manual_results | |
| user-guide.md | h3 | Viewing Results on the Map | superseded | manual_results | |
| user-guide.md | h3 | Viewing Results with a Graph | superseded | manual_time_series_plots | |
| user-guide.md | h3 | Viewing Results with a Table | superseded | manual_tabular_results | |
| user-guide.md | h3 | Viewing a Statistics Report | retired | | no event-frequency analysis in SWMMVis |
| user-guide.md | h2 | Printing and Copying | ported | manual_map_navigation | |
| user-guide.md | h3 | Selecting a Printer | retired | | the platform print dialog owns printer choice |
| user-guide.md | h3 | Setting the Page Format | retired | | no `QPageSetupDialog`; the platform panel owns it |
| user-guide.md | h3 | Previewing the Page | retired | | no `QPrintPreviewDialog`; macOS panel previews |
| user-guide.md | h3 | Printing the Current View | ported | manual_map_navigation | rewritten from `swmmvis.cpp` print path |
| user-guide.md | h3 | Copying the Current View | ported | manual_map_navigation | focus-aware copy |
| user-guide.md | h2 | Files Used by SWMM | superseded | manual_file_formats | |
| user-guide.md | h3 | Project File | superseded | manual_file_formats | `.inp` plus the `.oswp` project |
| user-guide.md | h3 | Report and Output Files | superseded | manual_file_formats | |
| user-guide.md | h3 | Rainfall Files | superseded | manual_file_formats | |
| user-guide.md | h3 | Climate Files | superseded | manual_file_formats | |
| user-guide.md | h3 | Calibration Files | superseded | manual_file_formats | |
| user-guide.md | h3 | Time Series Files | superseded | manual_file_formats | |
| user-guide.md | h3 | Interface Files | superseded | manual_file_formats | the `[FILES]` section |
| user-guide.md | h2 | Using Add-In Tools | retired | | no add-in mechanism; see `manual_plugins` |
| user-guide.md | h3 | What are Add-In Tools | retired | | |
| user-guide.md | h3 | Configuring Add-In Tools | retired | | |
| reference.md | h2 | Measurement Units | ported | manual_reference_tables | |
| reference.md | h3 | US Customary | ported | manual_reference_tables | |
| reference.md | h3 | SI Metric Units | ported | manual_reference_tables | |
| reference.md | h2 | Tables of Parameter Values | ported | manual_reference_tables | |
| reference.md | h3 | Soil Characteristics | ported | manual_reference_tables | supplies the heading the engine copy lost |
| reference.md | h3 | SCS Curve Numbers | ported | manual_reference_tables | |
| reference.md | h3 | Soil Group Definitions | ported | manual_reference_tables | NRCS hydrologic soil groups |
| reference.md | h3 | Depression Storage | ported | manual_reference_tables | |
| reference.md | h3 | Manning's n - Overland Flow | ported | manual_reference_tables | |
| reference.md | h3 | Manning's n - Closed Conduits | ported | manual_reference_tables | |
| reference.md | h3 | Manning's n - Open Channels | ported | manual_reference_tables | |
| reference.md | h3 | Water Quality Characteristics of Urban Runoff | ported | manual_reference_tables | |
| reference.md | h3 | Culvert Code Numbers | ported | manual_reference_tables | gated against `culvertcodes.cpp` |
| reference.md | h3 | Culvert Inlet Loss Coefficients | ported | manual_reference_tables | |
| reference.md | h3 | Standard Elliptical Pipe Sizes | ported | manual_reference_tables | gated against `xsect_tables.hpp` |
| reference.md | h3 | Standard Arch Pipe Sizes | ported | manual_reference_tables | gated against `xsect_tables.hpp` |
| reference.md | h2 | Visual Object Properties | ported | manual_property_index | A9 routes to the per-object sections |
| reference.md | h3 | Rain Gage Properties | ported | manual_property_index | |
| reference.md | h3 | Subcatchment Properties | ported | manual_property_index | |
| reference.md | h3 | Junction Properties | ported | manual_property_index | |
| reference.md | h3 | Outfall Properties | ported | manual_property_index | |
| reference.md | h3 | Flow Divider Properties | ported | manual_property_index | |
| reference.md | h3 | Storage Unit Properties | ported | manual_property_index | |
| reference.md | h3 | Conduit Properties | ported | manual_property_index | |
| reference.md | h3 | Pump Properties | ported | manual_property_index | |
| reference.md | h3 | Orifice Properties | ported | manual_property_index | |
| reference.md | h3 | Weir Properties | ported | manual_property_index | |
| reference.md | h3 | Outlet Properties | ported | manual_property_index | |
| reference.md | h3 | Map Label Properties | retired | | `[LABELS]` is parsed and discarded; use text annotations |
| reference.md | h2 | Special Dialog Forms | superseded | manual_object_browser | compound editors replace the modal dialog set |
| reference.md | h3 | Aquifer Editor | superseded | manual_hydrology | |
| reference.md | h3 | Backdrop Dimensions Dialog | retired | | basemaps are georeferenced; no manual fitting |
| reference.md | h3 | Backdrop Image Selector Dialog | retired | | superseded by raster and basemap layers |
| reference.md | h3 | Climatology Editor | superseded | manual_climate | |
| reference.md | h3 | Copy Dialog | retired | | copy is focus-aware; no format chooser |
| reference.md | h3 | Cross-Section Editor | superseded | manual_hydraulics | |
| reference.md | h3 | Curve Editor | superseded | manual_data_objects | |
| reference.md | h3 | Events Editor | superseded | manual_simulation_options | the Dates page |
| reference.md | h3 | Graph Options Dialog | superseded | manual_time_series_plots | |
| reference.md | h3 | Groundwater Flow Editor | superseded | manual_hydrology | |
| reference.md | h3 | Groundwater Equation Editor | superseded | manual_hydrology | `gwfexpressionedit.h` |
| reference.md | h3 | Group Edit Dialog | superseded | manual_attribute_tables | |
| reference.md | h3 | Infiltration Editor | superseded | manual_hydrology | |
| reference.md | h3 | Inflows Editor | superseded | manual_hydraulics | |
| reference.md | h3 | Initial Buildup Editor | superseded | manual_water_quality | |
| reference.md | h3 | Inlet Structure Editor | superseded | manual_hydraulics | |
| reference.md | h3 | Inlet Usage Editor | superseded | manual_hydraulics | |
| reference.md | h3 | Interface File Combine Dialog | retired | | no combine tool; `[FILES]` is edited directly |
| reference.md | h3 | Interface File Selection Dialog | superseded | manual_file_formats | |
| reference.md | h3 | Land Use Assignment Editor | superseded | manual_water_quality | |
| reference.md | h3 | Land Use Editor | superseded | manual_water_quality | |
| reference.md | h3 | Legend Editor | superseded | manual_styling | |
| reference.md | h3 | LID Editors | superseded | manual_hydrology | |
| reference.md | h3 | Map Dimensions Dialog | retired | | extent and units come from the CRS |
| reference.md | h3 | Map Options Dialog | superseded | manual_styling | |
| reference.md | h3 | Pollutant Editor | superseded | manual_water_quality | |
| reference.md | h3 | Profile Plot Selection Dialog | superseded | manual_profile_plots | |
| reference.md | h3 | Profile Plot Options Dialog | superseded | manual_profile_plots | |
| reference.md | h3 | Project Defaults Dialog | superseded | manual_projects | |
| reference.md | h3 | Reporting Options Dialog | superseded | manual_simulation_options | |
| reference.md | h3 | Scatter Plot Dialog | superseded | manual_time_series_plots | the 1v1 scatter column |
| reference.md | h3 | Simulation Options Dialog | superseded | manual_simulation_options | |
| reference.md | h3 | Simulation Options - Dynamic Wave | superseded | manual_simulation_options | Routing and Hydraulics page |
| reference.md | h3 | Statistics Selection Dialog | retired | | no event-frequency analysis; feature gap not doc gap |
| reference.md | h3 | Storage Shape Editor | superseded | manual_hydraulics | |
| reference.md | h3 | Street Section Editor | superseded | manual_hydraulics | |
| reference.md | h3 | Table by Object Dialog | retired | | builder not reachable; see `manual_tabular_results` |
| reference.md | h3 | Table by Variable Dialog | retired | | builder not reachable; see `manual_tabular_results` |
| reference.md | h3 | Time Pattern Editor | superseded | manual_data_objects | |
| reference.md | h3 | Time Series Editor | superseded | manual_data_objects | |
| reference.md | h3 | Time Series Plot Selection Dialog | superseded | manual_time_series_plots | |
| reference.md | h3 | Data Series Selection Dialog | superseded | manual_time_series_plots | |
| reference.md | h3 | Tool Properties Dialog | retired | | no add-in tools; see `manual_plugins` |
| reference.md | h3 | Transect Editor | superseded | manual_hydraulics | |
| reference.md | h3 | Treatment Editor | superseded | manual_water_quality | |
| reference.md | h3 | Unit Hydrograph Editor | superseded | manual_hydrology | |
| reference.md | h2 | Error Messages | ported | manual_error_codes | A8 generated from `ErrorCodes.hpp` |
| reference.md | h3 | Run Time Errors | ported | manual_error_codes | |
| reference.md | h3 | Property Errors | ported | manual_error_codes | |
| reference.md | h3 | Format Errors | ported | manual_error_codes | |
| reference.md | h3 | File Errors | ported | manual_error_codes | |
| reference.md | h3 | Warning Messages | ported | manual_error_codes | adds WARNING 13 and 101-108 |
| basic-tutorial.md | h2 | Introduction | superseded | tutorial_site_drainage | |
| basic-tutorial.md | h2 | Example Study Area | superseded | tutorial_site_drainage | |
| basic-tutorial.md | h3 | Study Area map | superseded | tutorial_site_drainage | |
| basic-tutorial.md | h2 | Project Setup | ported | tutorial_site_drainage | *Build this model from scratch* variation |
| basic-tutorial.md | h2 | Setting Map Options | superseded | manual_styling | |
| basic-tutorial.md | h2 | Drawing the Drainage Area Subcatchments | ported | tutorial_site_drainage | |
| basic-tutorial.md | h2 | Drawing the Drainage System Nodes | ported | tutorial_site_drainage | |
| basic-tutorial.md | h2 | Drawing the Drainage System Links | ported | tutorial_site_drainage | |
| basic-tutorial.md | h2 | Adding a Rain Gage | ported | tutorial_site_drainage | |
| basic-tutorial.md | h2 | Re-Positioning Objects | superseded | manual_map_editing | |
| basic-tutorial.md | h2 | Setting Properties | ported | tutorial_site_drainage | |
| basic-tutorial.md | h2 | Setting Subcatchment Properties | ported | tutorial_site_drainage | |
| basic-tutorial.md | h2 | Setting Node/Link Properties | ported | tutorial_site_drainage | |
| basic-tutorial.md | h2 | Setting Rain Gage Properties | ported | tutorial_site_drainage | |
| basic-tutorial.md | h2 | Saving and Opening Projects | superseded | manual_projects | |
| basic-tutorial.md | h2 | Running a Kinematic Wave Analysis | superseded | tutorial_site_drainage | |
| basic-tutorial.md | h2 | Viewing Analysis Results | superseded | manual_results | |
| basic-tutorial.md | h2 | Viewing the Status/Summary Reports | superseded | manual_tabular_results | |
| basic-tutorial.md | h2 | Viewing Results on the Map | superseded | manual_results | |
| basic-tutorial.md | h2 | Viewing a Time Series Plot | superseded | manual_time_series_plots | |
| basic-tutorial.md | h2 | Viewing a Profile Plot | superseded | manual_profile_plots | |
| basic-tutorial.md | h2 | Running a Dynamic Wave Analysis | superseded | manual_simulation_options | |
| basic-tutorial.md | h2 | Simulating Runoff Water Quality | superseded | tutorial_site_drainage | |
| basic-tutorial.md | h2 | Adding Pollutants | superseded | manual_water_quality | |
| basic-tutorial.md | h2 | Adding Land Uses | superseded | manual_water_quality | |
| basic-tutorial.md | h2 | Defining Buildup and Washoff Functions | superseded | manual_water_quality | |
| basic-tutorial.md | h2 | Assigning Land Uses to Subcatchments | superseded | manual_water_quality | |
| basic-tutorial.md | h2 | Running a Water Quality Analysis | superseded | tutorial_site_drainage | |
| basic-tutorial.md | h2 | Running a Continuous Simulation | superseded | manual_simulation_options | thin cover - no page frames it as a workflow |
| basic-tutorial.md | h2 | Performing a Frequency Analysis | retired | | no event-frequency analysis in SWMMVis |
| basic-tutorial.md | h2 | Other Features to Explore | superseded | manual_introduction | |
| inlet-tutorial.md | h2 | Introduction | superseded | tutorial_street_inlets | |
| inlet-tutorial.md | h3 | Inlet Types | superseded | manual_hydraulics | |
| inlet-tutorial.md | h3 | Inlet Concepts | engine | | HEC-22 capture concepts |
| inlet-tutorial.md | h3 | Inlet Analysis Workflow | superseded | tutorial_street_inlets | |
| inlet-tutorial.md | h2 | Example Project | superseded | tutorial_street_inlets | |
| inlet-tutorial.md | h3 | Step 1 - Lay Out the Network | superseded | tutorial_street_inlets | |
| inlet-tutorial.md | h3 | Step 2 - Add External Inflows | superseded | tutorial_street_inlets | |
| inlet-tutorial.md | h3 | Step 3 - Create Street Cross-Sections | superseded | tutorial_street_inlets | |
| inlet-tutorial.md | h3 | Step 4 - Assign Street Cross-Sections | superseded | tutorial_street_inlets | |
| inlet-tutorial.md | h3 | Step 5 - Create Inlet Designs | superseded | tutorial_street_inlets | |
| inlet-tutorial.md | h3 | Step 6 - Assign Inlet Designs | superseded | tutorial_street_inlets | |
| inlet-tutorial.md | h3 | Step 7 - Run a Simulation | superseded | tutorial_street_inlets | |
| inlet-tutorial.md | h3 | Step 8 - View Simulation Results | superseded | tutorial_street_inlets | |
| inlet-tutorial.md | h3 | Additional Analyses | superseded | tutorial_street_inlets | |
| inlet-tutorial.md | h2 | Computational Methods | engine | | hydraulics reference manual owns the equations |
| inlet-tutorial.md | h3 | Curb and Gutter Inlets | engine | | |
| inlet-tutorial.md | h2 | Drop Inlets | engine | | |
| inlet-tutorial.md | h2 | Custom Inlets | engine | | rating and diversion curve methods |

## Summary

| Disposition | Rows |
|---|---:|
| superseded | 144 |
| ported | 47 |
| retired | 19 |
| engine | 10 |
| **Total** | **220** |

Reproduce with `python3 scripts/manual_docs_audit.py crosswalk`, which prints the tally.

## Legacy images

All 175 image assets under the four legacy `images/` directories are referenced only by
the four legacy Markdown files and are deleted with them. They are Delphi-era screenshots
of EPA SWMM 5.x; the new manual captures its own figures through
`docs/manual/figures.json` and `scripts/manual_figures.py`.
