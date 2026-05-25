#
# tests/gui/CMakeLists.txt — QtTest GUI integration tests
#
# Framework: QtTest (Qt6::Test)
# Default platform: offscreen (set via QT_QPA_PLATFORM env)
#
# CTest label: gui
#

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test)

set(GUI_TEST_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)

# Helper: register a QtTest binary that links against the same sources as the
# main app so it can drive real classes (no .cpp duplication needed because the
# test sources include the .cpp files they exercise directly when small enough,
# or link against compiled .o files via the SWMMVis_obj target if extracted later).
function(add_swmmvis_gui_test TEST_NAME)
    add_executable(${TEST_NAME} ${ARGN})
    set_target_properties(${TEST_NAME} PROPERTIES
        AUTOMOC ON
        AUTORCC ON)
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/forms
    )
    target_link_libraries(${TEST_NAME} PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
    )
    # Engine is brought in via add_subdirectory at the root; link target is
    # `openswmm_engine`. HAVE_OPENSWMMCORE is set globally there too.
    if(TARGET openswmm_engine)
        target_link_libraries(${TEST_NAME} PRIVATE openswmm_engine)
    endif()
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        LABELS "gui"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SWMMVIS_GUI_TEST_DATA=${GUI_TEST_DATA_DIR}")
endfunction()

# ── Self-contained tests (no app linkage required) ──────────────────────────
# These exercise headers/sources that are simple enough to include directly.

add_swmmvis_gui_test(test_dirtytrackingbasic
    test_dirtytrackingbasic.cpp
)

# Slice BI Phase 8.13.6.1 + 8.13.6.2 — IFeatureRenderer interface + types,
# plus the four concrete renderer classes (Single, Graduated functional;
# Categorized, RuleBased stubs). Self-contained: pulls in only render/*.cpp
# (no Qt widgets, no GDAL, no engine).
add_swmmvis_gui_test(test_ifeaturerenderer
    test_ifeaturerenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/legendsymbolitem.cpp
    # Slice BB-α — GraduatedRenderer now depends on ColorRamp + IntervalBinner.
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/src/render/intervalbinner.cpp
    # Slice BI Phase 8.13.43-α — SingleSymbol gained DataDefinedScalar.
    ${CMAKE_SOURCE_DIR}/src/render/datadefined.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/singlesymbolrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/graduatedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/categorizedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/rulebasedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/include/render/featureref.h
    ${CMAKE_SOURCE_DIR}/include/render/symbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/symbolstyle.h
    ${CMAKE_SOURCE_DIR}/include/render/legendsymbolitem.h
    ${CMAKE_SOURCE_DIR}/include/render/colorramp.h
    ${CMAKE_SOURCE_DIR}/include/render/intervalbinner.h
    ${CMAKE_SOURCE_DIR}/include/render/ifeaturerenderer.h
    ${CMAKE_SOURCE_DIR}/include/render/renderers/singlesymbolrenderer.h
    ${CMAKE_SOURCE_DIR}/include/render/renderers/graduatedrenderer.h
    ${CMAKE_SOURCE_DIR}/include/render/renderers/categorizedrenderer.h
    ${CMAKE_SOURCE_DIR}/include/render/renderers/rulebasedrenderer.h
)

# Slice BI Phase 8.13.6.7 — IRasterRenderer interface + the
# SingleBandPseudoColorRenderer stub. Self-contained: pulls in only the
# raster renderer .cpp plus the LegendSymbolItem / SymbolStyle / SymbolLayer
# pieces it depends on (no Qt widgets, no GDAL, no engine).
add_swmmvis_gui_test(test_irasterrenderer
    test_irasterrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/legendsymbolitem.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/singlebandpseudocolorrenderer.cpp
    ${CMAKE_SOURCE_DIR}/include/render/symbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/symbolstyle.h
    ${CMAKE_SOURCE_DIR}/include/render/legendsymbolitem.h
    ${CMAKE_SOURCE_DIR}/include/render/irasterrenderer.h
    ${CMAKE_SOURCE_DIR}/include/render/renderers/singlebandpseudocolorrenderer.h
)

add_swmmvis_gui_test(test_unitsystemfacade
    test_unitsystemfacade.cpp
    ${CMAKE_SOURCE_DIR}/src/core/unitsystem.cpp
    ${CMAKE_SOURCE_DIR}/include/core/unitsystem.h
)

# Slice BM Phase 6.3 — PropertyEditorRegistry framework. Self-contained:
# pulls in only the registry .cpp + the IPropertyEditor interface header.
# The dummy editor lives inside the test TU, so no other GUI code is
# linked in.
add_swmmvis_gui_test(test_propertyeditorregistry
    test_propertyeditorregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/properties/propertyeditorregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/selection/selectionmanager.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/properties/ipropertyeditor.h
    ${CMAKE_SOURCE_DIR}/include/ui/properties/propertyeditorregistry.h
    ${CMAKE_SOURCE_DIR}/include/selection/selectionmanager.h
)

# Slice AT.2 — pins the system-attribute additions (14 PlotAttribute::System*
# enumerators + ObjectRef::Kind::System + SwmmOutRunLayer::variableCodeFor
# mapping to engine SWMM_OUT_SYS_* codes). Self-contained: only links the
# pure mapping code (swmmoutrunlayer_codes.cpp) so it dodges SWMMResultsLayer
# and its GDAL / Qt-Widgets transitive deps. Engine target supplies the
# SWMM_OUT_SYS_* constants via openswmm/engine/openswmm_output.h.
add_swmmvis_gui_test(test_comparisonplot_system_attrs
    test_comparisonplot_system_attrs.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/plotattribute.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/swmmoutrunlayer_codes.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/plotattribute.h
    ${CMAKE_SOURCE_DIR}/include/plot/irunlayer.h
    ${CMAKE_SOURCE_DIR}/include/plot/swmmoutrunlayer.h
)

# Slice AT.2 — pins InteractiveChartView's public API (mode round-trip,
# modeChanged signal, resetZoom, right-click context-menu signal, cursor
# reflection). Self-contained: only links the widget .cpp; needs Qt6::Charts
# for QChart / QChartView.
add_swmmvis_gui_test(test_comparisonplot_toolbar
    test_comparisonplot_toolbar.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/interactivechartview.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/interactivechartview.h
)
target_link_libraries(test_comparisonplot_toolbar PRIVATE Qt6::Charts)

# Slice BQ Phase 6.7.3.5 — TimeseriesEditChartView smoke tests
# (provider → chart refresh, selection mgmt, edit-mode toggle, external-
# mode read-only guard). Links the interactive base class + the timeseries
# foundation layer.
add_swmmvis_gui_test(test_timeseries_edit_chart_view
    test_timeseries_edit_chart_view.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/interactivechartview.h
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/timeserieseditchartview.h
    ${CMAKE_SOURCE_DIR}/include/timeseries/timeseriesprovider.h
    ${CMAKE_SOURCE_DIR}/include/timeseries/timeseriesundocommands.h
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/interactivechartview.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/timeserieseditchartview.cpp
    ${CMAKE_SOURCE_DIR}/src/timeseries/timeseriesprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/timeseries/timeseriesundocommands.cpp
)
target_link_libraries(test_timeseries_edit_chart_view PRIVATE Qt6::Charts)

# Slice BQ Phase 6.7.3.3 — TimeseriesEditorDialog shell smoke tests
# (dialog assembles grid + chart + toolbar; edits propagate both ways via
# the shared TimeseriesProvider; undo round-trips).
add_swmmvis_gui_test(test_timeseries_editor_dialog
    test_timeseries_editor_dialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/timeserieseditordialog.h
    ${CMAKE_SOURCE_DIR}/include/ui/panels/timeseriestablemodel.h
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/interactivechartview.h
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/timeserieseditchartview.h
    ${CMAKE_SOURCE_DIR}/include/timeseries/timeseriesprovider.h
    ${CMAKE_SOURCE_DIR}/include/timeseries/timeseriesregistry.h
    ${CMAKE_SOURCE_DIR}/include/timeseries/timeseriesundocommands.h
    ${CMAKE_SOURCE_DIR}/include/io/timeseriesparse.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/timeserieseditordialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/timeseriestablemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/interactivechartview.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/timeserieseditchartview.cpp
    ${CMAKE_SOURCE_DIR}/src/timeseries/timeseriesprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/timeseries/timeseriesregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/timeseries/timeseriesundocommands.cpp
    ${CMAKE_SOURCE_DIR}/src/io/timeseriesparse.cpp
)
target_link_libraries(test_timeseries_editor_dialog PRIVATE Qt6::Charts)

# Slice BM.0-Add-New (2026-05-24) — pins the createNew factory contract that
# ObjectBrowserPanel's Add-New dispatch relies on after NewDataObjectDialog
# removal (create-card validation, mode transition, source-mode propagation,
# chart-view setProvider rebind).
add_swmmvis_gui_test(test_objectbrowser_add_new_dispatch
    test_objectbrowser_add_new_dispatch.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/timeserieseditordialog.h
    ${CMAKE_SOURCE_DIR}/include/ui/panels/timeseriestablemodel.h
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/interactivechartview.h
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/timeserieseditchartview.h
    ${CMAKE_SOURCE_DIR}/include/timeseries/timeseriesprovider.h
    ${CMAKE_SOURCE_DIR}/include/timeseries/timeseriesregistry.h
    ${CMAKE_SOURCE_DIR}/include/timeseries/timeseriesundocommands.h
    ${CMAKE_SOURCE_DIR}/include/io/timeseriesparse.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/timeserieseditordialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/timeseriestablemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/interactivechartview.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/timeserieseditchartview.cpp
    ${CMAKE_SOURCE_DIR}/src/timeseries/timeseriesprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/timeseries/timeseriesregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/timeseries/timeseriesundocommands.cpp
    ${CMAKE_SOURCE_DIR}/src/io/timeseriesparse.cpp
)
target_link_libraries(test_objectbrowser_add_new_dispatch PRIVATE Qt6::Charts)

# Slice BQ Phase 6.7.2 — PatternEditorDialog + PatternProvider + PatternRegistry
# + PatternFactorTableModel coverage. Self-contained: pulls in only the
# pattern foundation + dialog sources + Qt6::Charts (for the bar-chart pane).
add_swmmvis_gui_test(test_pattern_editor_dialog
    test_pattern_editor_dialog.cpp
    ${CMAKE_SOURCE_DIR}/include/pattern/patternprovider.h
    ${CMAKE_SOURCE_DIR}/include/pattern/patternregistry.h
    ${CMAKE_SOURCE_DIR}/include/ui/panels/patternfactortablemodel.h
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/patterneditordialog.h
    ${CMAKE_SOURCE_DIR}/src/pattern/patternprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/pattern/patternregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/patternfactortablemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/patterneditordialog.cpp
)
target_link_libraries(test_pattern_editor_dialog PRIVATE Qt6::Charts)

# Slice BQ Phase 6.7.1 — CurveEditorDialog + CurveProvider + CurveRegistry
# + CurvePointTableModel coverage. Self-contained.
add_swmmvis_gui_test(test_curve_editor_dialog
    test_curve_editor_dialog.cpp
    ${CMAKE_SOURCE_DIR}/include/curve/curveprovider.h
    ${CMAKE_SOURCE_DIR}/include/curve/curveregistry.h
    ${CMAKE_SOURCE_DIR}/include/ui/panels/curvepointtablemodel.h
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/curveeditordialog.h
    ${CMAKE_SOURCE_DIR}/src/curve/curveprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/curve/curveregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/curvepointtablemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/curveeditordialog.cpp
)
target_link_libraries(test_curve_editor_dialog PRIVATE Qt6::Charts)

# Slice BQ Phase 6.7.3.8 — TimeseriesRegistry::loadFromEngine round-trips a
# BUILDING-state engine with two Tseries (point values + times). Links the
# engine target (via add_swmmvis_gui_test helper) to drive swmm_engine_new
# / swmm_timeseries_add / swmm_table_add_point against real impl code.
add_swmmvis_gui_test(test_timeseries_registry_engine
    test_timeseries_registry_engine.cpp
    ${CMAKE_SOURCE_DIR}/include/timeseries/timeseriesprovider.h
    ${CMAKE_SOURCE_DIR}/include/timeseries/timeseriesregistry.h
    ${CMAKE_SOURCE_DIR}/src/timeseries/timeseriesprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/timeseries/timeseriesregistry.cpp
)

# Slice AT.2 — pins the recursion-guarded X-axis linking idiom used in
# ComparisonPlotDialog::wireXAxisSync. Self-contained: in-test XAxisSyncBus
# mirrors the dialog's pattern; no dialog code needed. Needs Qt6::Charts
# for QDateTimeAxis.
add_swmmvis_gui_test(test_comparisonplot_xaxis_link
    test_comparisonplot_xaxis_link.cpp
)
target_link_libraries(test_comparisonplot_xaxis_link PRIVATE Qt6::Charts)

# Slice AT.3 — pins RangeSliderWidget invariants.
add_swmmvis_gui_test(test_comparisonplot_rangeslider
    test_comparisonplot_rangeslider.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/rangeslider.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/rangeslider.h
)

# Slice AT.3 — pins ChartProperties wrapper semantics.
add_swmmvis_gui_test(test_comparisonplot_chartproperties
    test_comparisonplot_chartproperties.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/chartproperties.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/chartproperties.h
)
target_link_libraries(test_comparisonplot_chartproperties PRIVATE Qt6::Charts)

# Slice AT.3 — pins computeStatistics() pure-function behaviour.
add_swmmvis_gui_test(test_comparisonplot_statstab
    test_comparisonplot_statstab.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/seriesstatistics.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/seriesstatistics.h
)

# Slice AT.3 — pins the model surface that the series-tree checkbox + context
# menu drives (updateStyle / removeSeries / Plot-This-Only loop).
add_swmmvis_gui_test(test_comparisonplot_seriestree
    test_comparisonplot_seriestree.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/comparisonplotmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/seriesstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/plotattribute.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/comparisonplotmodel.h
    ${CMAKE_SOURCE_DIR}/include/plot/seriesstyle.h
    ${CMAKE_SOURCE_DIR}/include/plot/plotattribute.h
    ${CMAKE_SOURCE_DIR}/include/plot/irunlayer.h
)

# Slice CX (Phase CX.1) — INP-as-source-of-truth hydration contract.
# Exercises §M.1 trigger 1 (project open) / trigger 2 (tab activation) /
# trigger 3 (external mutation) for FLOW_UNITS via UnitSystem + the engine
# ABI. Links openswmm_engine through the helper above so swmm_options_get /
# _set round-trip against a real engine handle.
add_swmmvis_gui_test(test_options_hydration_contract
    test_options_hydration_contract.cpp
    ${CMAKE_SOURCE_DIR}/src/core/unitsystem.cpp
    ${CMAKE_SOURCE_DIR}/include/core/unitsystem.h
    ${CMAKE_SOURCE_DIR}/include/ui/swmmvis_hydration_audit.h
)

# Slice DA.1 — pins the engine ABI surface that
# `SWMMModelLayer::dataObjectCount` / `dataObjectNameAt` rely on for the
# Control Rules and Unit Hydrographs Object Browser sections. Validates
# `swmm_control_get_id` (DA-ENG-02) and
# `swmm_hydrograph_group_count` / `swmm_hydrograph_group_id` (DA-ENG-01)
# and the GUI's sentinel mapping for malformed rules.
add_swmmvis_gui_test(test_dataobjectnaming
    test_dataobjectnaming.cpp
)

# Slice DA.2 — non-spatial property-adapter round-trip tests
# Exercises the rich-coverage adapters (Pollutant, LandUse, RainGage)
# against a real engine handle. The thin adapters (Aquifer, Snowpack,
# LIDControl, Transect, Hydrograph, Street, Inlet, ControlRule,
# Curve, TimeSeries, Pattern) are exercised by test_dataobjectnaming
# for name / count / group enumeration.
# Slice BM.0-Add-New (2026-05-24) — `NewDataObjectDialog` + the
# panel-side `addNewDataObject` wrapper were removed. The Add-New
# dispatch path is covered by `test_objectbrowser_add_new_dispatch`
# below.

add_swmmvis_gui_test(test_nonspatial_adapters
    test_nonspatial_adapters.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/properties/swmmdataobjectpropertyadapter.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/properties/swmmpollutantpropertyadapter.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/properties/swmmlandusepropertyadapter.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/properties/swmmraingagepropertyadapter.cpp
    ${CMAKE_SOURCE_DIR}/src/core/unitsystem.cpp
    # Headers listed explicitly so AUTOMOC picks up their Q_OBJECT macros.
    ${CMAKE_SOURCE_DIR}/include/ui/properties/swmmdataobjectpropertyadapter.h
    ${CMAKE_SOURCE_DIR}/include/ui/properties/swmmpollutantpropertyadapter.h
    ${CMAKE_SOURCE_DIR}/include/ui/properties/swmmlandusepropertyadapter.h
    ${CMAKE_SOURCE_DIR}/include/ui/properties/swmmraingagepropertyadapter.h
    ${CMAKE_SOURCE_DIR}/include/core/unitsystem.h
)

# Slice DB — SWMMNodePropertyAdapter scalar + coord + summary round-trip.
# Self-contained: pulls only the node adapter + UnitSystem (no widgets,
# no layers). Verifies the four type-aware subclasses' Q_PROPERTY shape
# stays in sync with the engine API contract.
add_swmmvis_gui_test(test_nodepropertyadapter
    test_nodepropertyadapter.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/properties/swmmnodepropertyadapter.cpp
    ${CMAKE_SOURCE_DIR}/src/core/unitsystem.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/properties/swmmnodepropertyadapter.h
    ${CMAKE_SOURCE_DIR}/include/core/unitsystem.h
)

# Slice BC, Phase 8.7.2 — pins the engine-ABI contract that
# `swmm_link_get_offset_up/dn` always returns offsets as **depth above the
# connecting node invert**, regardless of the source .inp's LINK_OFFSETS
# setting. The GUI's `profilenetworkadapter_model.cpp` removed the in-GUI
# elevation→depth re-conversion under this contract; this test guards the
# contract end-to-end via two fixtures describing the same physical
# conduit in DEPTH and ELEVATION mode.
add_swmmvis_gui_test(test_profilenetworkadapter_model
    test_profilenetworkadapter_model.cpp
)

add_swmmvis_gui_test(test_crschangedialog
    test_crschangedialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/crschangedialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/crschangedialog.h
)

add_swmmvis_gui_test(test_aboutdialog
    test_aboutdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/aboutdialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/aboutdialog.h
)

add_swmmvis_gui_test(test_selectionmanager
    test_selectionmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/selection/selectionmanager.cpp
    ${CMAKE_SOURCE_DIR}/include/selection/selectionmanager.h
)

add_swmmvis_gui_test(test_editgeometry
    test_editgeometry.cpp
    ${CMAKE_SOURCE_DIR}/src/core/editgeometry.cpp
    ${CMAKE_SOURCE_DIR}/include/core/editgeometry.h
)

# Smoke test for the reprojection primitive every layer uses when rendering
# in a canvas CRS different from the layer's own CRS. Links GDAL directly.
add_swmmvis_gui_test(test_layerreprojection
    test_layerreprojection.cpp
    ${CMAKE_SOURCE_DIR}/src/map/spatialreferencesystem.cpp
    ${CMAKE_SOURCE_DIR}/include/map/spatialreferencesystem.h
)
target_include_directories(test_layerreprojection PRIVATE ${GDAL_INCLUDE_DIRS})
target_link_libraries(test_layerreprojection PRIVATE GDAL::GDAL)

# Slice AU — Mesh generation + DTM sampling. Both link triangle_lib (Shewchuk
# vendored at vendor/triangle/) and GDAL respectively.
add_swmmvis_gui_test(test_meshgenerator
    test_meshgenerator.cpp
    ${CMAKE_SOURCE_DIR}/src/mesh/meshgenerator.cpp
    ${CMAKE_SOURCE_DIR}/include/mesh/meshgenerator.h
    ${CMAKE_SOURCE_DIR}/include/mesh/meshresult.h
)
target_link_libraries(test_meshgenerator PRIVATE triangle_lib)

add_swmmvis_gui_test(test_dtmsampler
    test_dtmsampler.cpp
    ${CMAKE_SOURCE_DIR}/src/mesh/dtmsampler.cpp
    ${CMAKE_SOURCE_DIR}/include/mesh/dtmsampler.h
)
target_include_directories(test_dtmsampler PRIVATE ${GDAL_INCLUDE_DIRS})
target_link_libraries(test_dtmsampler PRIVATE GDAL::GDAL)

add_swmmvis_gui_test(test_inpmeshwriter
    test_inpmeshwriter.cpp
    ${CMAKE_SOURCE_DIR}/src/mesh/inpmeshwriter.cpp
    ${CMAKE_SOURCE_DIR}/include/mesh/inpmeshwriter.h
    ${CMAKE_SOURCE_DIR}/include/mesh/meshresult.h
)

# Slice BJ.2-lite — marching-triangles contour line extraction. Header-only
# templated algorithm; the test self-supplies triangle fixtures.
add_swmmvis_gui_test(test_marchingtriangles
    test_marchingtriangles.cpp
    ${CMAKE_SOURCE_DIR}/include/contour/marchingtriangles.h
)

# Slice BB-α — RasterColorRamp data type, HSV interpolation, built-in
# catalogue, JSON round-trip. Self-contained: pulls in colorramp.cpp only.
add_swmmvis_gui_test(test_colorramp
    test_colorramp.cpp
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/include/render/colorramp.h
)

# Slice BB-α — IntervalBinner (equal-interval / quantile / manual).
add_swmmvis_gui_test(test_intervalbinner
    test_intervalbinner.cpp
    ${CMAKE_SOURCE_DIR}/src/render/intervalbinner.cpp
    ${CMAKE_SOURCE_DIR}/include/render/intervalbinner.h
)

# Slice BI Phase 8.13.43-α — DataDefinedScalar (size/width by attribute).
add_swmmvis_gui_test(test_datadefinedscalar
    test_datadefinedscalar.cpp
    ${CMAKE_SOURCE_DIR}/src/render/datadefined.cpp
    ${CMAKE_SOURCE_DIR}/include/render/datadefined.h
)

# Slice BB-γ — CategoricalPalette named lookup + 7 Plotly categorical palettes.
add_swmmvis_gui_test(test_categoricalpalette
    test_categoricalpalette.cpp
    ${CMAKE_SOURCE_DIR}/src/render/categoricalpalette.cpp
    ${CMAKE_SOURCE_DIR}/include/render/categoricalpalette.h
)

# Slice BI-MK.1 Phase 8.13.40 — MultiKindRenderer (per-kind dispatch).
# Self-contained: pulls in the multikind impl + the same render/*.cpp
# subset that test_ifeaturerenderer compiles, since fromJson() factories
# the four concrete renderer classes.
add_swmmvis_gui_test(test_multikindrenderer
    test_multikindrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/multikindrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/legendsymbolitem.cpp
    # Slice BB-α — GraduatedRenderer depends on ColorRamp + IntervalBinner.
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/src/render/intervalbinner.cpp
    # Slice BI Phase 8.13.43-α — SingleSymbol gained DataDefinedScalar.
    ${CMAKE_SOURCE_DIR}/src/render/datadefined.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/singlesymbolrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/graduatedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/categorizedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/rulebasedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/include/render/multikindrenderer.h
    ${CMAKE_SOURCE_DIR}/include/render/featureref.h
    ${CMAKE_SOURCE_DIR}/include/render/symbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/symbolstyle.h
    ${CMAKE_SOURCE_DIR}/include/render/legendsymbolitem.h
    ${CMAKE_SOURCE_DIR}/include/render/colorramp.h
    ${CMAKE_SOURCE_DIR}/include/render/intervalbinner.h
    ${CMAKE_SOURCE_DIR}/include/render/ifeaturerenderer.h
    ${CMAKE_SOURCE_DIR}/include/render/renderers/singlesymbolrenderer.h
    ${CMAKE_SOURCE_DIR}/include/render/renderers/graduatedrenderer.h
    ${CMAKE_SOURCE_DIR}/include/render/renderers/categorizedrenderer.h
    ${CMAKE_SOURCE_DIR}/include/render/renderers/rulebasedrenderer.h
)

# Slice CF.MVP — HDF5 reader for the engine's CF/UGRID 2D output file.
# Self-generates a tiny fixture .h5 in setUp so we don't depend on a
# running engine.
find_package(HDF5 REQUIRED COMPONENTS C)
add_swmmvis_gui_test(test_mesh2dh5reader
    test_mesh2dh5reader.cpp
    ${CMAKE_SOURCE_DIR}/src/io/mesh2dh5reader.cpp
    ${CMAKE_SOURCE_DIR}/include/io/mesh2dh5reader.h
)
target_include_directories(test_mesh2dh5reader PRIVATE ${HDF5_INCLUDE_DIRS})
target_link_libraries(test_mesh2dh5reader PRIVATE ${HDF5_C_LIBRARIES})

# ArcGIS REST rendering — URL building, tile coordinate math, zoom selection,
# and axis-order correctness for XYZTileLayer (including the ESRI World Imagery
# builtin). Requires Qt6::Network (QNetworkAccessManager in XYZTileLayer ctor)
# and GDAL (OGRSpatialReference in XYZTileLayer ctor).
find_package(Qt6 REQUIRED COMPONENTS Network)
add_swmmvis_gui_test(test_arcgisrestrendering
    test_arcgisrestrendering.cpp
    ${CMAKE_SOURCE_DIR}/src/layers/xyztilelayer.cpp
    ${CMAKE_SOURCE_DIR}/include/layers/xyztilelayer.h
    ${CMAKE_SOURCE_DIR}/src/layers/openswmmvislayer.cpp
    ${CMAKE_SOURCE_DIR}/include/layers/openswmmvislayer.h
    ${CMAKE_SOURCE_DIR}/src/map/mapextent.cpp
    ${CMAKE_SOURCE_DIR}/include/map/mapextent.h
    ${CMAKE_SOURCE_DIR}/src/map/spatialreferencesystem.cpp
    ${CMAKE_SOURCE_DIR}/include/map/spatialreferencesystem.h
)
target_link_libraries(test_arcgisrestrendering PRIVATE
    Qt6::Network
    GDAL::GDAL
)
target_include_directories(test_arcgisrestrendering PRIVATE ${GDAL_INCLUDE_DIRS})

# test_simulationoptionsdialog — re-enabled 2026-05-21 (Slice CW follow-up).
#
# The previous parking comment said this had to wait for the `swmmvis_core`
# static-lib extraction because including the dialog header (Q_OBJECT) made
# AUTOMOC generate moc_simulationoptionsdialog.cpp, which then needs every
# slot symbol — including Tab 5's CRS slots → OGR/GDAL cascade.
#
# Surgical unpark: do NOT pass the header in the source list (that's what
# forces AUTOMOC to process it).  The .cpp only calls *static* methods on
# SimulationOptionsDialog (parseEngineBool / formatEngineDateTime /
# oaDateFromQDateTime / etc.) — those symbols are defined in
# simulationoptionshelpers.cpp.  The test never instantiates the dialog,
# so the moc-generated vtable is never referenced, and the linker has no
# reason to pull in the spatial-tab slot bodies.
#
# If a future change adds a non-static helper to the test, the moc cascade
# re-emerges and the test must wait for swmmvis_core extraction or grow
# stubs for the slot bodies.
add_swmmvis_gui_test(test_simulationoptionsdialog
    test_simulationoptionsdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/simulationoptionshelpers.cpp
)

# Phase B.1 of docs/RENDERING_5M_PLAN.md — interactive spike that
# verifies `QPainter::beginNativePainting()` works on the host's Qt /
# GL build before we commit the canvas viewport swap. Not registered
# with CTest; build with `cmake --build . --target spike_glnativepaint`
# and run interactively.
find_package(Qt6 COMPONENTS OpenGL OpenGLWidgets QUIET)
if(TARGET Qt6::OpenGL AND TARGET Qt6::OpenGLWidgets)
    add_executable(spike_glnativepaint spike_glnativepaint.cpp)
    set_target_properties(spike_glnativepaint PROPERTIES
        AUTOMOC ON)
    target_link_libraries(spike_glnativepaint PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
endif()

# Phase B.RHI.1 of docs/RENDERING_5M_PLAN.md — interactive spike that
# verifies Qt's scene graph + QRhi (Metal on macOS, Vulkan/D3D11
# elsewhere) renders QSGGeometryNode geometry correctly in the host's
# Qt build. Pivot away from the OpenGL renderer that hit unfixable
# QPainter fill bugs on Apple Silicon — see the plan doc.
find_package(Qt6 COMPONENTS Quick QUIET)
if(TARGET Qt6::Quick)
    add_executable(spike_qsgnode spike_qsgnode.cpp)
    set_target_properties(spike_qsgnode PROPERTIES
        AUTOMOC ON)
    target_link_libraries(spike_qsgnode PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick)
endif()

# LayerTreeModel test (test_layertreemodel.cpp) is parked until the
# `swmmvis_core` static-library extraction lands. Compiling layertreepanel.cpp
# in isolation pulls in Qt6Network + GIS layer + WMS dialog code — too much to
# manually mirror in a leaf test. Tracked under "Known follow-ups" in
# docs/GUI_IMPLEMENTATION_PLAN.md.
