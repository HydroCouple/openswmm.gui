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
    # QTEST_FUNCTION_TIMEOUT: if a single test FUNCTION blocks longer than this,
    # QtTest aborts and prints WHICH function hung — turning an opaque 60 s ctest
    # timeout ("test_pattern_editor_dialog ***Timeout") into a named failure
    # ("Test function timed out: someDialog_opens()"). 30 s is 50x the slowest
    # gui test (0.6 s), so it only fires on a genuine hang. Diagnostic for the
    # ~12 chart/widget tests still hanging on Windows for reasons not yet pinned.
    set_tests_properties(${TEST_NAME} PROPERTIES
        LABELS "gui"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SWMMVIS_GUI_TEST_DATA=${GUI_TEST_DATA_DIR};QTEST_FUNCTION_TIMEOUT=30000")

    # Runtime env: Windows DLL search path + PROJ/GDAL data dirs (see
    # tests/CMakeLists.txt). This is a SEPARATE property from ENVIRONMENT above —
    # ENVIRONMENT_MODIFICATION is applied after it, so the two compose rather than
    # clobber each other (offscreen QPA + data-dir vars both take effect).
    if(SWMMVIS_TEST_ENV)
        set_tests_properties(${TEST_NAME} PROPERTIES
            ENVIRONMENT_MODIFICATION "${SWMMVIS_TEST_ENV}")
    endif()
endfunction()

# ── Self-contained tests (no app linkage required) ──────────────────────────
# These exercise headers/sources that are simple enough to include directly.

add_swmmvis_gui_test(test_dirtytrackingbasic
    test_dirtytrackingbasic.cpp
)

# Slice LTR-2026-05-30 — layer-tree category bucketing. Headless: links
# only against the extracted layertreecategories.cpp (no Qt widgets, no
# MapCanvas, no layer object graph). The static_assert that keeps the
# LayerTypeOrdinal mirror in sync with OpenSWMMVisLayer lives in
# layertreepanel.cpp, so any drift fails the main GUI build.
add_swmmvis_gui_test(test_layertreecategories
    test_layertreecategories.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/layertreecategories.cpp
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
    # Slice Z (RENDERING_RULE_MODEL_PLAN) — RuleBasedRenderer evaluates
    # filter expressions through ExpressionEvaluator.
    ${CMAKE_SOURCE_DIR}/src/render/expressionevaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/singlesymbolrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/graduatedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/categorizedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/rulebasedrenderer.cpp
    # Gap A1.3 — archetype-seeded renderer factory + the typed specs it
    # uses for prop skeletons; UnclassedColors is a factory product too.
    ${CMAKE_SOURCE_DIR}/src/render/rendererfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/unclassedcolorsrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/markersymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/linesymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/fillsymbollayer.cpp
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

# Gap A1.2 — canonical colour prop convention: SymbolProps tolerant reads /
# canonical writes, typed-spec hex tolerance, JSON-boundary rehydration of
# every colour-suffixed key (incl. raster grammar keys).
add_swmmvis_gui_test(test_symbolprops_color
    test_symbolprops_color.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/markersymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/linesymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/fillsymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/rastersymbollayers.cpp
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/src/render/intervalbinner.cpp
    ${CMAKE_SOURCE_DIR}/include/render/symbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/symbolstyle.h
    ${CMAKE_SOURCE_DIR}/include/render/markersymbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/linesymbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/fillsymbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/rastersymbollayers.h
)

# Slice Z.1 — Rule + RuleList data model (RENDERING_RULE_MODEL_PLAN.md).
# Self-contained: pulls in only the render/*.cpp pieces a Rule needs
# (renderer set + symbol model). No Qt widgets, no engine.
add_swmmvis_gui_test(test_rule
    test_rule.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/legendsymbolitem.cpp
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/src/render/intervalbinner.cpp
    ${CMAKE_SOURCE_DIR}/src/render/datadefined.cpp
    # Slice Z — RuleBasedRenderer depends on ExpressionEvaluator.
    ${CMAKE_SOURCE_DIR}/src/render/expressionevaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/singlesymbolrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/graduatedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/categorizedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/rulebasedrenderer.cpp
    # Slice Z.9 — rule.cpp now factories "unclassed".
    ${CMAKE_SOURCE_DIR}/src/render/renderers/unclassedcolorsrenderer.cpp
    # Gap A1.3 — rule.cpp constructs through the archetype-seeded factory.
    ${CMAKE_SOURCE_DIR}/src/render/rendererfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/render/markersymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/linesymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/fillsymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/rule.cpp
    ${CMAKE_SOURCE_DIR}/src/render/rulelist.cpp
    # Slice Z.2 — Rule-as-ILayerStyleSubject adapter.
    # (Rule.rebinPerFrame Z.7 tests live in this same target.)
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/ilayerstylesubject.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/rulestylesubject.cpp
    ${CMAKE_SOURCE_DIR}/include/render/rule.h
    ${CMAKE_SOURCE_DIR}/include/render/rulelist.h
    # SublayerStyle is a QObject — header listed so AUTOMOC emits its
    # staticMetaObject for the qobject_cast in ilayerstylesubject.cpp.
    ${CMAKE_SOURCE_DIR}/include/render/sublayerstyle.h
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/ilayerstylesubject.h
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/rulestylesubject.h
)

# ILayerStyleSubject snapshot/restore — the contract LayerStyleDialog's
# Cancel rollback and undo/redo ride on. Pins the QFont round trip that the
# generic QVariant->JSON walk used to drop (LAYER_STYLING_LABELING_PLAN F5).
# SwmmElementSymbolAdapter is header-only, so only the subject .cpp links.
add_swmmvis_gui_test(test_layerstylesubject
    test_layerstylesubject.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/ilayerstylesubject.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/ilayerstylesubject.h
    ${CMAKE_SOURCE_DIR}/include/layers/swmmelementsymboladapter.h
    # SublayerStyle is a QObject — header listed so AUTOMOC emits its
    # staticMetaObject for the qobject_cast in ilayerstylesubject.cpp.
    ${CMAKE_SOURCE_DIR}/include/render/sublayerstyle.h
    # The adapter's markerShape Q_PROPERTY is a Q_ENUM_NS, so the namespace's
    # staticMetaObject has to be emitted (header) and its helpers linked.
    ${CMAKE_SOURCE_DIR}/include/render/markershape.h
    ${CMAKE_SOURCE_DIR}/src/render/markershape.cpp
)

# Selection locator overlay — the screen-space contract (beacon size must not
# track zoom) and the zoom-dependent aggregation. Self-contained: paints into
# a QImage, no canvas or layer stack.
add_swmmvis_gui_test(test_selectionbeacon
    test_selectionbeacon.cpp
    ${CMAKE_SOURCE_DIR}/src/map/selectionbeaconoverlay.cpp
    ${CMAKE_SOURCE_DIR}/include/map/selectionbeaconoverlay.h
)

# Slice Z.4 — MarkerShape + MarkerSymbolLayerSpec tests.
# Self-contained: pulls in the render symbol primitives and the new Z.4
# files. Uses Qt Widgets (QImage / QPainter / QPen / QBrush).
add_swmmvis_gui_test(test_markershape
    test_markershape.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/markershape.cpp
    ${CMAKE_SOURCE_DIR}/src/render/markersymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/include/render/markershape.h
    ${CMAKE_SOURCE_DIR}/include/render/markersymbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/symbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/symbolstyle.h
)

# Slice Z.5 — LineSymbolLayerSpec + arrow draw tests.
add_swmmvis_gui_test(test_linesymbollayer
    test_linesymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/linesymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/include/render/linesymbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/symbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/symbolstyle.h
)

# VS.2 — FillSymbolLayerSpec + fill draw tests (third primitive).
add_swmmvis_gui_test(test_fillsymbollayer
    test_fillsymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/fillsymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/include/render/fillsymbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/symbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/symbolstyle.h
)

# VS.6 — PalettedRasterRenderer (categorical raster) tests.
add_swmmvis_gui_test(test_palettedrasterrenderer
    test_palettedrasterrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/palettedrasterrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/categoricalpalette.cpp
    ${CMAKE_SOURCE_DIR}/src/render/fillsymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/legendsymbolitem.cpp
)

# Slice B.6c — SymbolStyleAdapter tests.
add_swmmvis_gui_test(test_symbolstyleadapter
    test_symbolstyleadapter.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/legendsymbolitem.cpp
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/src/render/intervalbinner.cpp
    ${CMAKE_SOURCE_DIR}/src/render/datadefined.cpp
    # Slice Z — RuleBasedRenderer depends on ExpressionEvaluator.
    ${CMAKE_SOURCE_DIR}/src/render/expressionevaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/singlesymbolrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/graduatedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/categorizedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/rulebasedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/unclassedcolorsrenderer.cpp
    # Gap A1.3 — rule.cpp constructs through the archetype-seeded factory.
    ${CMAKE_SOURCE_DIR}/src/render/rendererfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/render/markersymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/linesymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/fillsymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/rule.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyleadapter.cpp
    # symbolstyleadapter.cpp reads MarkerShape via markerShapeFromString;
    # the header carries Q_NAMESPACE so AUTOMOC must see it to generate
    # the namespace staticMetaObject.
    ${CMAKE_SOURCE_DIR}/src/render/markershape.cpp
    ${CMAKE_SOURCE_DIR}/include/render/markershape.h
    ${CMAKE_SOURCE_DIR}/include/render/rule.h
    ${CMAKE_SOURCE_DIR}/include/render/symbolstyleadapter.h
)

# Slice Z.7 — animation bin sampler tests.
# Self-contained: pulls in only IntervalBinner + the new sampler.
add_swmmvis_gui_test(test_binsampler
    test_binsampler.cpp
    ${CMAKE_SOURCE_DIR}/src/render/intervalbinner.cpp
    ${CMAKE_SOURCE_DIR}/src/render/binsampler.cpp
    ${CMAKE_SOURCE_DIR}/include/render/intervalbinner.h
    ${CMAKE_SOURCE_DIR}/include/render/binsampler.h
)

# Issue 4 — 2D profile water-surface bridging. Header-only helper, no .cpp.
add_swmmvis_gui_test(test_meshprofileinterp
    test_meshprofileinterp.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/meshprofileinterp.h
    ${CMAKE_SOURCE_DIR}/include/plot/meshprofilesampler.h
)

# 2D profile WSE extrapolation — constant-η fill of no-data corners + vertex-
# scoped dryness (CellSurfaceInterp, shared with SWMM2DResultsLayer). Header-
# only, so the layer's link closure stays out of the test.
add_swmmvis_gui_test(test_profile_wse_extrapolation
    test_profile_wse_extrapolation.cpp
    ${CMAKE_SOURCE_DIR}/include/layers/cellsurfaceinterp.h
)

# GUI-fallback vertex free-surface reconstruction (engine mirror) incl. the
# wetted-contact gate. Header-only, same pattern as cellsurfaceinterp.
add_swmmvis_gui_test(test_vertexdepthreconstruct
    test_vertexdepthreconstruct.cpp
    ${CMAKE_SOURCE_DIR}/include/layers/vertexdepthreconstruct.h
)

# 2D map pooling extrapolation — dry corners of a partially-wet cell carry the
# driving head's signed depth so the marching bands cut the shoreline on the bed
# intercept. Header-only, same pattern as the two above.
add_swmmvis_gui_test(test_map_pooling_extrapolation
    test_map_pooling_extrapolation.cpp
    ${CMAKE_SOURCE_DIR}/include/layers/vertexdepthreconstruct.h
    ${CMAKE_SOURCE_DIR}/include/layers/cellsurfaceinterp.h
)

# Profile plots — endpoint label hit-testing and min/max range-edge edits.
add_swmmvis_gui_test(test_profileplot_axis_edges
    test_profileplot_axis_edges.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/profileplotwidget.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/profileplotwidget.h
    ${CMAKE_SOURCE_DIR}/src/plot/profilebuilder.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/profilebuilder.h
    ${CMAKE_SOURCE_DIR}/src/plot/profileplotoptions.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/profileplotoptions.h
    ${CMAKE_SOURCE_DIR}/src/plot/meshprofileplotwidget.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/meshprofileplotwidget.h
    ${CMAKE_SOURCE_DIR}/src/plot/meshprofileplotoptions.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/meshprofileplotoptions.h
    ${CMAKE_SOURCE_DIR}/include/plot/meshprofileinterp.h
    ${CMAKE_SOURCE_DIR}/include/plot/meshprofilesampler.h
    ${CMAKE_SOURCE_DIR}/src/core/preferencesmanager.cpp
    ${CMAKE_SOURCE_DIR}/include/core/preferencesmanager.h
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/include/render/colorramp.h
    ${CMAKE_SOURCE_DIR}/src/plot/numberformat.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/numberformat.h
    ${CMAKE_SOURCE_DIR}/include/ui/theme/thememanager.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/thememanager.cpp
)

# Layer-metadata plan — mesh edge-count identity (2·E = 3·T + B). Value types.
add_swmmvis_gui_test(test_meshedgecount
    test_meshedgecount.cpp
    ${CMAKE_SOURCE_DIR}/include/mesh/meshresult.h
)

# Slice Z.17-files — RuleList .swmm-rule.json I/O tests.
add_swmmvis_gui_test(test_rulelistio
    test_rulelistio.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/legendsymbolitem.cpp
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/src/render/intervalbinner.cpp
    ${CMAKE_SOURCE_DIR}/src/render/datadefined.cpp
    # Slice Z — RuleBasedRenderer depends on ExpressionEvaluator.
    ${CMAKE_SOURCE_DIR}/src/render/expressionevaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/singlesymbolrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/graduatedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/categorizedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/rulebasedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/unclassedcolorsrenderer.cpp
    # Gap A1.3 — rule.cpp constructs through the archetype-seeded factory.
    ${CMAKE_SOURCE_DIR}/src/render/rendererfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/render/markersymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/linesymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/fillsymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/rule.cpp
    ${CMAKE_SOURCE_DIR}/src/render/rulelist.cpp
    ${CMAKE_SOURCE_DIR}/src/render/rulelistio.cpp
    ${CMAKE_SOURCE_DIR}/include/render/rule.h
    ${CMAKE_SOURCE_DIR}/include/render/rulelist.h
    ${CMAKE_SOURCE_DIR}/include/render/rulelistio.h
)

# Slice Z.3b — Rule-aware RendererPanelContext + Rule::setRendererById
# tests. Uses Rule + irendererpanel.h (struct definition only) — keeps
# the dep surface small (no SymbologyTab widget, no editor panels).
add_swmmvis_gui_test(test_rendererpanelcontext
    test_rendererpanelcontext.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/legendsymbolitem.cpp
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/src/render/intervalbinner.cpp
    ${CMAKE_SOURCE_DIR}/src/render/datadefined.cpp
    # Slice Z — RuleBasedRenderer depends on ExpressionEvaluator.
    ${CMAKE_SOURCE_DIR}/src/render/expressionevaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/singlesymbolrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/graduatedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/categorizedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/rulebasedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/unclassedcolorsrenderer.cpp
    # Gap A1.3 — rule.cpp constructs through the archetype-seeded factory.
    ${CMAKE_SOURCE_DIR}/src/render/rendererfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/render/markersymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/linesymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/fillsymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/rule.cpp
    ${CMAKE_SOURCE_DIR}/include/render/rule.h
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/irendererpanel.h
)

# Slice Z.13-data — TemporalSpec value type tests.
add_swmmvis_gui_test(test_temporalspec
    test_temporalspec.cpp
    ${CMAKE_SOURCE_DIR}/src/render/temporalspec.cpp
    ${CMAKE_SOURCE_DIR}/include/render/temporalspec.h
)

# Slice Z.14-data — MaskSpec value type tests.
add_swmmvis_gui_test(test_maskspec
    test_maskspec.cpp
    ${CMAKE_SOURCE_DIR}/src/render/maskspec.cpp
    ${CMAKE_SOURCE_DIR}/include/render/maskspec.h
)

# Slice Z.15-data — AuxiliaryStorageSpec value type tests.
add_swmmvis_gui_test(test_auxiliarystoragespec
    test_auxiliarystoragespec.cpp
    ${CMAKE_SOURCE_DIR}/src/render/auxiliarystoragespec.cpp
    ${CMAKE_SOURCE_DIR}/include/render/auxiliarystoragespec.h
)

# Slice Z.16-data — JoinSpec value type tests.
add_swmmvis_gui_test(test_joinspec
    test_joinspec.cpp
    ${CMAKE_SOURCE_DIR}/src/render/joinspec.cpp
    ${CMAKE_SOURCE_DIR}/include/render/joinspec.h
)

# Slice Z.12-data — DiagramSpec value type tests.
add_swmmvis_gui_test(test_diagramspec
    test_diagramspec.cpp
    ${CMAKE_SOURCE_DIR}/src/render/diagramspec.cpp
    ${CMAKE_SOURCE_DIR}/include/render/diagramspec.h
)

# Slice Z.5b — polyline offset tests. Self-contained: only the helper.
add_swmmvis_gui_test(test_polylineoffset
    test_polylineoffset.cpp
    ${CMAKE_SOURCE_DIR}/src/render/polylineoffset.cpp
    ${CMAKE_SOURCE_DIR}/include/render/polylineoffset.h
)

# Slice Z.9 — UnclassedColorsRenderer tests.
add_swmmvis_gui_test(test_unclassedcolorsrenderer
    test_unclassedcolorsrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/legendsymbolitem.cpp
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/src/render/intervalbinner.cpp
    ${CMAKE_SOURCE_DIR}/src/render/datadefined.cpp
    # Slice Z — RuleBasedRenderer depends on ExpressionEvaluator.
    ${CMAKE_SOURCE_DIR}/src/render/expressionevaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/singlesymbolrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/graduatedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/categorizedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/rulebasedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/unclassedcolorsrenderer.cpp
    # Gap A1.3 — rule.cpp constructs through the archetype-seeded factory.
    ${CMAKE_SOURCE_DIR}/src/render/rendererfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/render/markersymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/linesymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/fillsymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/rule.cpp
    ${CMAKE_SOURCE_DIR}/include/render/renderers/unclassedcolorsrenderer.h
    ${CMAKE_SOURCE_DIR}/include/render/rule.h
)

# Slice Z.11 — Symbol Levels tests. Self-contained: SymbolLayer +
# SymbolStyle + the new symbollevels helper.
add_swmmvis_gui_test(test_symbollevels
    test_symbollevels.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollevels.cpp
    ${CMAKE_SOURCE_DIR}/include/render/symbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/symbolstyle.h
    ${CMAKE_SOURCE_DIR}/include/render/symbollevels.h
)

# Slice Z.6 — Raster + TIN Symbol Layer spec tests.
# Self-contained: pulls in colorramp + intervalbinner + marker spec from
# earlier slices for the embedded sub-objects.
add_swmmvis_gui_test(test_rastersymbollayers
    test_rastersymbollayers.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/src/render/intervalbinner.cpp
    ${CMAKE_SOURCE_DIR}/src/render/markershape.cpp
    ${CMAKE_SOURCE_DIR}/src/render/markersymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/rastersymbollayers.cpp
    ${CMAKE_SOURCE_DIR}/include/render/colorramp.h
    ${CMAKE_SOURCE_DIR}/include/render/intervalbinner.h
    ${CMAKE_SOURCE_DIR}/include/render/markershape.h
    ${CMAKE_SOURCE_DIR}/include/render/markersymbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/rastersymbollayers.h
    ${CMAKE_SOURCE_DIR}/include/render/symbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/symbolstyle.h
)

# Slice Z.3 — RuleSymbologyTab widget tests
# (RENDERING_RULE_MODEL_PLAN.md). Uses Qt Widgets; runs offscreen.
add_swmmvis_gui_test(test_rulesymbologytab
    test_rulesymbologytab.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/legendsymbolitem.cpp
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/src/render/intervalbinner.cpp
    ${CMAKE_SOURCE_DIR}/src/render/datadefined.cpp
    # Slice Z — RuleBasedRenderer depends on ExpressionEvaluator.
    ${CMAKE_SOURCE_DIR}/src/render/expressionevaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/singlesymbolrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/graduatedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/categorizedrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/renderers/rulebasedrenderer.cpp
    # Z.9 — rule.cpp now factories "unclassed".
    ${CMAKE_SOURCE_DIR}/src/render/renderers/unclassedcolorsrenderer.cpp
    # Gap A1.3 — rule.cpp constructs through the archetype-seeded factory.
    ${CMAKE_SOURCE_DIR}/src/render/rendererfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/render/markersymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/linesymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/fillsymbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/rule.cpp
    ${CMAKE_SOURCE_DIR}/src/render/rulelist.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/irendererpanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/rulesymbologytab.cpp
    # Iteration 3 — rulesymbologytab buttons render through IconFactory.
    ${CMAKE_SOURCE_DIR}/include/ui/theme/iconfactory.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/iconfactory.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/theme/thememanager.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/thememanager.cpp
    ${CMAKE_SOURCE_DIR}/include/render/rule.h
    ${CMAKE_SOURCE_DIR}/include/render/rulelist.h
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/rulesymbologytab.h
)
target_link_libraries(test_rulesymbologytab PRIVATE Qt6::Svg)

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

# Slice S1 (RENDERING_OUTPUT_SUBLAYERS_PLAN.md) — sublayer foundation:
# ISublayer interface, SublayerStyle property-bag base, SizeUnit enum, and
# screen-pixel helpers (screenpixels.h). Self-contained: links only
# LegendSymbolItem (whose JSON gained a sublayerId field in S1) and its
# SymbolStyle / SymbolLayer dependencies. The ISublayer + SublayerStyle
# subclasses are defined inside the test TU.
add_swmmvis_gui_test(test_sublayer_interface
    test_sublayer_interface.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/legendsymbolitem.cpp
    ${CMAKE_SOURCE_DIR}/include/render/symbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/symbolstyle.h
    ${CMAKE_SOURCE_DIR}/include/render/legendsymbolitem.h
    ${CMAKE_SOURCE_DIR}/include/render/sizeunit.h
    ${CMAKE_SOURCE_DIR}/include/render/screenpixels.h
    ${CMAKE_SOURCE_DIR}/include/render/sublayerstyle.h
    ${CMAKE_SOURCE_DIR}/include/render/isublayer.h
)

# Slice S2.1 (RENDERING_OUTPUT_SUBLAYERS_PLAN.md) — ISublayerHost mixin +
# animation-tick dispatch. Self-contained: pulls in the same lean leaf-test
# dependency set as test_sublayer_interface (LegendSymbolItem + its
# SymbolStyle/SymbolLayer deps); FakeSublayer + FakeHost stubs live inside
# the test TU.
add_swmmvis_gui_test(test_sublayer_host_dispatch
    test_sublayer_host_dispatch.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/legendsymbolitem.cpp
    ${CMAKE_SOURCE_DIR}/include/render/symbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/symbolstyle.h
    ${CMAKE_SOURCE_DIR}/include/render/legendsymbolitem.h
    ${CMAKE_SOURCE_DIR}/include/render/isublayer.h
    ${CMAKE_SOURCE_DIR}/include/render/isublayerhost.h
)

# Slice U-0 — granular per-Category FeatureSublayer + 3 archetype style
# bags (Point / Line / Polygon). Replaces the four legacy per-class tests
# (NodeMarker / ConduitLine / ConduitArrow / SubcatchmentFill) with one
# self-contained suite. Pulls in SWMMModelLayer just for the Category
# enum — the test never instantiates SWMMModelLayer itself.
add_swmmvis_gui_test(test_featuresublayer
    test_featuresublayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/legendsymbolitem.cpp
    ${CMAKE_SOURCE_DIR}/src/render/sublayers/feature/featuresublayer.cpp
    # FeatureSublayerStyle's L-1 label config round-trips through LabelConfig.
    ${CMAKE_SOURCE_DIR}/src/render/labelconfig.cpp
    ${CMAKE_SOURCE_DIR}/include/render/symbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/symbolstyle.h
    ${CMAKE_SOURCE_DIR}/include/render/legendsymbolitem.h
    ${CMAKE_SOURCE_DIR}/include/render/sublayerstyle.h
    ${CMAKE_SOURCE_DIR}/include/render/isublayer.h
    ${CMAKE_SOURCE_DIR}/include/render/sublayers/feature/featuresublayer.h
    ${CMAKE_SOURCE_DIR}/include/render/sublayers/feature/featuresublayerstyle.h
)

# Slice S5 (RENDERING_OUTPUT_SUBLAYERS_PLAN.md) — 2D sublayer classes bundled
# into one self-contained test TU. Same lean leaf-test dependency pattern; the
# .cpp impls link against the same LegendSymbolItem + SymbolStyle dependency
# set as the 1D sublayer tests.
add_swmmvis_gui_test(test_2d_sublayers
    test_2d_sublayers.cpp
    # Sublayer styles classify scalar fields through ClassificationScheme,
    # which serialises its bins via IntervalBinner.
    ${CMAKE_SOURCE_DIR}/src/render/classificationscheme.cpp
    ${CMAKE_SOURCE_DIR}/include/render/classificationscheme.h
    ${CMAKE_SOURCE_DIR}/src/render/intervalbinner.cpp
    ${CMAKE_SOURCE_DIR}/include/render/intervalbinner.h
    ${CMAKE_SOURCE_DIR}/src/render/symbollayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/render/legendsymbolitem.cpp
    ${CMAKE_SOURCE_DIR}/src/render/sublayers/meshfillsublayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/sublayers/meshedgesublayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/sublayers/velocityvectorsublayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/sublayers/isolinesublayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/sublayers/contourbandsublayer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/sublayers/scalarfillsublayer.cpp
    # VelocityVectorStyle::colorForSpeed samples a named RasterColorRamp.
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/include/render/symbollayer.h
    ${CMAKE_SOURCE_DIR}/include/render/symbolstyle.h
    ${CMAKE_SOURCE_DIR}/include/render/legendsymbolitem.h
    ${CMAKE_SOURCE_DIR}/include/render/sublayerstyle.h
    ${CMAKE_SOURCE_DIR}/include/render/isublayer.h
    ${CMAKE_SOURCE_DIR}/include/render/sublayers/meshfillsublayer.h
    ${CMAKE_SOURCE_DIR}/include/render/sublayers/meshedgesublayer.h
    ${CMAKE_SOURCE_DIR}/include/render/sublayers/velocityvectorsublayer.h
    ${CMAKE_SOURCE_DIR}/include/render/sublayers/isolinesublayer.h
    ${CMAKE_SOURCE_DIR}/include/render/sublayers/contourbandsublayer.h
    ${CMAKE_SOURCE_DIR}/include/render/sublayers/scalarfillsublayer.h
)
# D1-a — the property grid surfaces a ClassificationScheme Q_PROPERTY as an
# editable leaf row through QPropertyModel; the test reflects on the model.
target_link_libraries(test_2d_sublayers PRIVATE
    $<$<TARGET_EXISTS:QPropertyModel>:QPropertyModel>)

# NOTE — test_2dresults_vizfixes.cpp (2D viz refinement, PLAN_2D_VIZ_REFINEMENT.md)
# is intentionally NOT registered. It drives the real SWMM2DResultsLayer, whose
# link closure transitively pulls the QObject base (OpenSWMMVisLayer →
# OpenSWMMVisWorkspace → SWMMModelLayer), the mask-clip resolver → GISVectorLayer
# → GDAL/OGR, the HDF5 mesh reader, and the full symbol/renderer/sublayer graph —
# i.e. a large fraction of the app, disproportionate for a leaf test. The four
# behaviours it would pin are already covered by registered tests:
#   Issue 1 → test_cursorwindowslider ; Issue 3 → test_meshedgecount ;
#   Issue 4 → test_meshprofileinterp.  Re-enabling it cleanly needs an extracted
# SWMMVis object library to link against (see PLAN_2D_VIZ_REFINEMENT §3).

# Slice U-0 — the old test_sublayer_persistence test relied on the
# deleted NodeMarkerSublayer; FeatureSublayer JSON round-trip is now
# covered by test_featuresublayer's *_JsonRoundtrip slots above.

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

# Plot Time Series toolbar dialog — pins the shared canonical attribute
# lists (the regression fence for the 4-site enumeration refactor) and the
# PlotVariablePickerDialog's tree/gating/select-all/filter behaviour with a
# stub IRunLayer. Engine-free.
add_swmmvis_gui_test(test_plotvariablepickerdialog
    test_plotvariablepickerdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/plotvariablepickerdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/plotattribute.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/plotvariablepickerdialog.h
    ${CMAKE_SOURCE_DIR}/include/plot/plotattribute.h
    ${CMAKE_SOURCE_DIR}/include/plot/irunlayer.h
)

# Iteration 4 — pins PreferencesManager::TwoDDefaults (2D Defaults page):
# compiled-in defaults contract + QSettings round-trip + change signal.
add_swmmvis_gui_test(test_twod_defaults_prefs
    test_twod_defaults_prefs.cpp
    ${CMAKE_SOURCE_DIR}/src/core/preferencesmanager.cpp
    ${CMAKE_SOURCE_DIR}/include/core/preferencesmanager.h
    # PreferencesManager custom color-ramp persistence references RasterColorRamp.
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/include/render/colorramp.h
)

# Object-creation defaults bundle — pins the signed-off defaults table and
# the independent US/SI persistence (see
# workplans/OBJECT_CREATION_DEFAULTS_PLAN_2026-08-03.md).
add_swmmvis_gui_test(test_object_defaults_prefs
    test_object_defaults_prefs.cpp
    ${CMAKE_SOURCE_DIR}/src/core/preferencesmanager.cpp
    ${CMAKE_SOURCE_DIR}/include/core/preferencesmanager.h
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/include/render/colorramp.h
)

# Slice AT.2 — pins InteractiveChartView's public API (mode round-trip,
# modeChanged signal, resetZoom, right-click context-menu signal, cursor
# reflection). Self-contained: only links the widget .cpp; needs Qt6::Charts
# for QChart / QChartView.
add_swmmvis_gui_test(test_comparisonplot_toolbar
    test_comparisonplot_toolbar.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/interactivechartview.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/interactivechartview.h
    # InteractiveChartView formats axis labels via PreferencesManager + NumberFormat.
    ${CMAKE_SOURCE_DIR}/src/core/preferencesmanager.cpp
    ${CMAKE_SOURCE_DIR}/include/core/preferencesmanager.h
    # PreferencesManager custom color-ramp persistence references RasterColorRamp.
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/include/render/colorramp.h
    ${CMAKE_SOURCE_DIR}/src/plot/numberformat.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/numberformat.h
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
    # Axis labels go through PreferencesManager + NumberFormat.
    ${CMAKE_SOURCE_DIR}/src/core/preferencesmanager.cpp
    ${CMAKE_SOURCE_DIR}/include/core/preferencesmanager.h
    # PreferencesManager custom color-ramp persistence references RasterColorRamp.
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/include/render/colorramp.h
    # TimeseriesEditChartView's context menu opens ChartAxisFormatController.
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/chartaxisformatcontroller.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/chartaxisformatcontroller.h
    # ChartAxisFormatController opens the shared ChartPropertiesDialog over a ChartProperties.
    ${CMAKE_SOURCE_DIR}/src/plot/chartproperties.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/chartproperties.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/chartpropertiesdialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/chartpropertiesdialog.h
    ${CMAKE_SOURCE_DIR}/src/plot/numberformat.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/numberformat.h
)
target_link_libraries(test_timeseries_edit_chart_view PRIVATE Qt6::Charts
    # chartpropertiesdialog.cpp (pulled in via ChartAxisFormatController) uses QPropertyModel.
    $<$<TARGET_EXISTS:QPropertyModel>:QPropertyModel>)

# Slice BQ Phase 6.7.3.3 — TimeseriesEditorDialog shell smoke tests
# (dialog assembles grid + chart + toolbar; edits propagate both ways via
# the shared TimeseriesProvider; undo round-trips).
add_swmmvis_gui_test(test_timeseries_editor_dialog
    test_timeseries_editor_dialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/timeserieseditordialog.h
    ${CMAKE_SOURCE_DIR}/include/ui/panels/timeseriestablemodel.h
    ${CMAKE_SOURCE_DIR}/include/ui/panels/timeserieslistmodel.h
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/interactivechartview.h
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/timeserieseditchartview.h
    ${CMAKE_SOURCE_DIR}/include/timeseries/timeseriesprovider.h
    ${CMAKE_SOURCE_DIR}/include/timeseries/timeseriesregistry.h
    ${CMAKE_SOURCE_DIR}/include/timeseries/timeseriesundocommands.h
    ${CMAKE_SOURCE_DIR}/include/io/timeseriesparse.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/timeserieseditordialog.cpp
    # Grid time column edits through DateTimeDelegate (MM/dd/yyyy HH:mm).
    ${CMAKE_SOURCE_DIR}/include/ui/delegates/datetimedelegate.h
    ${CMAKE_SOURCE_DIR}/src/ui/delegates/datetimedelegate.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/theme/iconfactory.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/iconfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/timeseriestablemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/timeserieslistmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/interactivechartview.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/timeserieseditchartview.cpp
    ${CMAKE_SOURCE_DIR}/src/timeseries/timeseriesprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/timeseries/timeseriesregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/timeseries/timeseriesundocommands.cpp
    ${CMAKE_SOURCE_DIR}/src/io/timeseriesparse.cpp
    # Shared external-column-file util (CSV/TSV/TSF headers + column read).
    ${CMAKE_SOURCE_DIR}/include/ui/util/externalcolumnfile.h
    ${CMAKE_SOURCE_DIR}/src/ui/util/externalcolumnfile.cpp
    # Axis labels: PreferencesManager + NumberFormat; geometry persistence:
    # DialogLayoutPersistence (applyAlwaysOnTopPolicy / save+restoreDialogLayout).
    ${CMAKE_SOURCE_DIR}/src/core/preferencesmanager.cpp
    ${CMAKE_SOURCE_DIR}/include/core/preferencesmanager.h
    # PreferencesManager custom color-ramp persistence references RasterColorRamp.
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/include/render/colorramp.h
    # TimeseriesEditorDialog/EditChartView open ChartAxisFormatController.
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/chartaxisformatcontroller.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/chartaxisformatcontroller.h
    # ChartAxisFormatController opens the shared ChartPropertiesDialog over a ChartProperties.
    ${CMAKE_SOURCE_DIR}/src/plot/chartproperties.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/chartproperties.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/chartpropertiesdialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/chartpropertiesdialog.h
    ${CMAKE_SOURCE_DIR}/src/plot/numberformat.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/numberformat.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/dialoglayoutpersistence.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/dialoglayoutpersistence.h
    ${CMAKE_SOURCE_DIR}/include/ui/theme/thememanager.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/thememanager.cpp
)
target_link_libraries(test_timeseries_editor_dialog PRIVATE Qt6::Charts Qt6::Svg
    # chartpropertiesdialog.cpp (pulled in via ChartAxisFormatController) uses QPropertyModel.
    $<$<TARGET_EXISTS:QPropertyModel>:QPropertyModel>)

# Slice BM.0-Add-New (2026-05-24) — pins the createNew factory contract that
# ObjectBrowserPanel's Add-New dispatch relies on after NewDataObjectDialog
# removal (create-card validation, mode transition, source-mode propagation,
# chart-view setProvider rebind).
add_swmmvis_gui_test(test_objectbrowser_add_new_dispatch
    test_objectbrowser_add_new_dispatch.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/timeserieseditordialog.h
    ${CMAKE_SOURCE_DIR}/include/ui/panels/timeseriestablemodel.h
    ${CMAKE_SOURCE_DIR}/include/ui/panels/timeserieslistmodel.h
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/interactivechartview.h
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/timeserieseditchartview.h
    ${CMAKE_SOURCE_DIR}/include/timeseries/timeseriesprovider.h
    ${CMAKE_SOURCE_DIR}/include/timeseries/timeseriesregistry.h
    ${CMAKE_SOURCE_DIR}/include/timeseries/timeseriesundocommands.h
    ${CMAKE_SOURCE_DIR}/include/io/timeseriesparse.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/timeserieseditordialog.cpp
    # Grid time column edits through DateTimeDelegate (MM/dd/yyyy HH:mm).
    ${CMAKE_SOURCE_DIR}/include/ui/delegates/datetimedelegate.h
    ${CMAKE_SOURCE_DIR}/src/ui/delegates/datetimedelegate.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/theme/iconfactory.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/iconfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/timeseriestablemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/timeserieslistmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/interactivechartview.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/timeserieseditchartview.cpp
    ${CMAKE_SOURCE_DIR}/src/timeseries/timeseriesprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/timeseries/timeseriesregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/timeseries/timeseriesundocommands.cpp
    ${CMAKE_SOURCE_DIR}/src/io/timeseriesparse.cpp
    # Shared external-column-file util (CSV/TSV/TSF headers + column read).
    ${CMAKE_SOURCE_DIR}/include/ui/util/externalcolumnfile.h
    ${CMAKE_SOURCE_DIR}/src/ui/util/externalcolumnfile.cpp
    # Same TimeseriesEditorDialog transitive deps as test_timeseries_editor_dialog.
    ${CMAKE_SOURCE_DIR}/src/core/preferencesmanager.cpp
    ${CMAKE_SOURCE_DIR}/include/core/preferencesmanager.h
    # PreferencesManager custom color-ramp persistence references RasterColorRamp.
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/include/render/colorramp.h
    # TimeseriesEditorDialog/EditChartView open ChartAxisFormatController.
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/chartaxisformatcontroller.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/chartaxisformatcontroller.h
    # ChartAxisFormatController opens the shared ChartPropertiesDialog over a ChartProperties.
    ${CMAKE_SOURCE_DIR}/src/plot/chartproperties.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/chartproperties.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/chartpropertiesdialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/chartpropertiesdialog.h
    ${CMAKE_SOURCE_DIR}/src/plot/numberformat.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/numberformat.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/dialoglayoutpersistence.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/dialoglayoutpersistence.h
    ${CMAKE_SOURCE_DIR}/include/ui/theme/thememanager.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/thememanager.cpp
)
target_link_libraries(test_objectbrowser_add_new_dispatch PRIVATE Qt6::Charts Qt6::Svg
    # chartpropertiesdialog.cpp (pulled in via ChartAxisFormatController) uses QPropertyModel.
    $<$<TARGET_EXISTS:QPropertyModel>:QPropertyModel>)

# Slice BQ Phase 6.7.2 — PatternEditorDialog + PatternProvider + PatternRegistry
# + PatternFactorTableModel coverage. Self-contained: pulls in only the
# pattern foundation + dialog sources + Qt6::Charts (for the bar-chart pane).
# Iteration 4 — treatment-expression editing stack (highlighter vocab
# drift-guard, completer, engine-backed validation, cell delegate).
add_swmmvis_gui_test(test_treatment_expression_editor
    test_treatment_expression_editor.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/treatmentexpressionedit.h
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/treatmentexpressionedit.cpp
)

# Iteration 4 — SubcatchCompoundEditDialog: editable coverage matrix +
# initial-loadings page ([LOADINGS]).
add_swmmvis_gui_test(test_subcatch_coverage_loadings
    test_subcatch_coverage_loadings.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/subcatchcompoundeditdialog.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/subcatchcompoundeditdialog.cpp
)

# Iteration 4 — the unified Land Use editor (tabbed detail, engine-bound
# Buildup/Washoff grids, impact-aware delete, engine-backed rename).
add_swmmvis_gui_test(test_landuse_unified_editor
    test_landuse_unified_editor.cpp
    ${CMAKE_SOURCE_DIR}/include/landuse/landuseprovider.h
    ${CMAKE_SOURCE_DIR}/include/landuse/landuseregistry.h
    ${CMAKE_SOURCE_DIR}/include/pollutant/pollutantprovider.h
    ${CMAKE_SOURCE_DIR}/include/pollutant/pollutantregistry.h
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/landuseeditordialog.h
    ${CMAKE_SOURCE_DIR}/include/ui/models/landuselistmodel.h
    ${CMAKE_SOURCE_DIR}/include/ui/models/qualityfunctiontablemodels.h
    ${CMAKE_SOURCE_DIR}/src/landuse/landuseprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/landuse/landuseregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/pollutant/pollutantprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/pollutant/pollutantregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/landuseeditordialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/models/landuselistmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/models/qualityfunctiontablemodels.cpp
    # Dialog buttons go through IconFactory (themed SVG icons).
    ${CMAKE_SOURCE_DIR}/include/ui/theme/iconfactory.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/iconfactory.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/theme/thememanager.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/thememanager.cpp
)
target_link_libraries(test_landuse_unified_editor PRIVATE Qt6::Svg)

add_swmmvis_gui_test(test_pattern_editor_dialog
    test_pattern_editor_dialog.cpp
    ${CMAKE_SOURCE_DIR}/include/pattern/patternprovider.h
    ${CMAKE_SOURCE_DIR}/include/pattern/patternregistry.h
    ${CMAKE_SOURCE_DIR}/include/ui/panels/patternfactortablemodel.h
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/patterneditordialog.h
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/interactivechartview.h
    ${CMAKE_SOURCE_DIR}/src/pattern/patternprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/pattern/patternregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/patternfactortablemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/patterneditordialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/theme/thememanager.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/thememanager.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/theme/iconfactory.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/iconfactory.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/interactivechartview.cpp
    # PatternEditorDialog hosts a PatternEditChartView (complete type needed for
    # chartView()->grab() in the test, and its impl for linking; it pushes
    # pattern undo commands and formats axes via PreferencesManager + NumberFormat).
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/patterneditchartview.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/patterneditchartview.h
    ${CMAKE_SOURCE_DIR}/src/pattern/patternundocommands.cpp
    ${CMAKE_SOURCE_DIR}/include/pattern/patternundocommands.h
    ${CMAKE_SOURCE_DIR}/src/core/preferencesmanager.cpp
    ${CMAKE_SOURCE_DIR}/include/core/preferencesmanager.h
    # PreferencesManager custom color-ramp persistence references RasterColorRamp.
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/include/render/colorramp.h
    # PatternEditorDialog opens ChartAxisFormatController for "Chart Properties...".
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/chartaxisformatcontroller.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/chartaxisformatcontroller.h
    # ChartAxisFormatController opens the shared ChartPropertiesDialog over a ChartProperties.
    ${CMAKE_SOURCE_DIR}/src/plot/chartproperties.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/chartproperties.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/chartpropertiesdialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/chartpropertiesdialog.h
    ${CMAKE_SOURCE_DIR}/src/plot/numberformat.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/numberformat.h
)
target_link_libraries(test_pattern_editor_dialog PRIVATE Qt6::Charts Qt6::Svg
    # chartpropertiesdialog.cpp (pulled in via ChartAxisFormatController) uses QPropertyModel.
    $<$<TARGET_EXISTS:QPropertyModel>:QPropertyModel>)

# Slice BQ Phase 6.7.1 — CurveEditorDialog + CurveProvider + CurveRegistry
# + CurvePointTableModel coverage. Self-contained.
add_swmmvis_gui_test(test_curve_editor_dialog
    test_curve_editor_dialog.cpp
    ${CMAKE_SOURCE_DIR}/include/curve/curveprovider.h
    ${CMAKE_SOURCE_DIR}/include/curve/curveregistry.h
    ${CMAKE_SOURCE_DIR}/include/ui/panels/curvepointtablemodel.h
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/curveeditordialog.h
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/interactivechartview.h
    ${CMAKE_SOURCE_DIR}/src/curve/curveprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/curve/curveregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/curvepointtablemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/curveeditordialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/theme/iconfactory.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/iconfactory.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/theme/thememanager.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/thememanager.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/interactivechartview.cpp
    # CurveEditorDialog hosts a CurveEditChartView (which pushes curve undo
    # commands); axis labels via PreferencesManager + NumberFormat.
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/curveeditchartview.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/curveeditchartview.h
    ${CMAKE_SOURCE_DIR}/src/curve/curveundocommands.cpp
    ${CMAKE_SOURCE_DIR}/include/curve/curveundocommands.h
    ${CMAKE_SOURCE_DIR}/src/core/preferencesmanager.cpp
    ${CMAKE_SOURCE_DIR}/include/core/preferencesmanager.h
    # PreferencesManager custom color-ramp persistence references RasterColorRamp.
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/include/render/colorramp.h
    # CurveEditorDialog/EditChartView open ChartAxisFormatController.
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/chartaxisformatcontroller.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/chartaxisformatcontroller.h
    # ChartAxisFormatController opens the shared ChartPropertiesDialog over a ChartProperties.
    ${CMAKE_SOURCE_DIR}/src/plot/chartproperties.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/chartproperties.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/chartpropertiesdialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/chartpropertiesdialog.h
    ${CMAKE_SOURCE_DIR}/src/plot/numberformat.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/numberformat.h
)
target_link_libraries(test_curve_editor_dialog PRIVATE Qt6::Charts Qt6::Svg
    # chartpropertiesdialog.cpp (pulled in via ChartAxisFormatController) uses QPropertyModel.
    $<$<TARGET_EXISTS:QPropertyModel>:QPropertyModel>)

# Slice BQ Phase 6.7.4 — TransectEditorDialog + TransectProvider +
# TransectRegistry + TransectListModel + TransectStationTableModel +
# TransectChartView + TransectPropertyBag coverage. Self-contained — pulls
# in only the transect foundation + dialog sources + Qt6::Charts.
add_swmmvis_gui_test(test_transect_editor_dialog
    test_transect_editor_dialog.cpp
    ${CMAKE_SOURCE_DIR}/include/transect/transectprovider.h
    ${CMAKE_SOURCE_DIR}/include/transect/transectregistry.h
    ${CMAKE_SOURCE_DIR}/include/transect/transectundocommands.h
    ${CMAKE_SOURCE_DIR}/include/ui/models/transectlistmodel.h
    ${CMAKE_SOURCE_DIR}/include/ui/models/transectstationtablemodel.h
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/transectchartview.h
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/transectpropertybag.h
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/transecteditordialog.h
    ${CMAKE_SOURCE_DIR}/src/transect/transectprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/transect/transectregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/transect/transectundocommands.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/models/transectlistmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/models/transectstationtablemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/transectchartview.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/transectpropertybag.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/transecteditordialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/theme/iconfactory.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/iconfactory.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/theme/thememanager.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/thememanager.cpp
    # Chart axis labels via PreferencesManager + NumberFormat.
    ${CMAKE_SOURCE_DIR}/src/core/preferencesmanager.cpp
    ${CMAKE_SOURCE_DIR}/include/core/preferencesmanager.h
    # PreferencesManager custom color-ramp persistence references RasterColorRamp.
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/include/render/colorramp.h
    ${CMAKE_SOURCE_DIR}/src/plot/numberformat.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/numberformat.h
)
target_link_libraries(test_transect_editor_dialog PRIVATE Qt6::Svg
    Qt6::Charts
    # transecteditordialog.cpp uses QPropertyModel for the chart-properties
    # dialog and the middle-pane property tree (see lines 17, 359, 650).
    $<$<TARGET_EXISTS:QPropertyModel>:QPropertyModel>
)

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

# SWMM DateTime Consolidation Phase 1 (workplans/
# SWMM_DATETIME_CONSOLIDATION_PLAN_2026-07-10.md) — golden round-trip for
# the time-series minute-truncation fix (GH #1) against a real fixture
# opened through the engine. Same link set as test_timeseries_registry_engine
# above, since it drives the same TimeseriesRegistry/TimeseriesProvider code.
add_swmmvis_gui_test(test_timeseries_datetime_roundtrip
    test_timeseries_datetime_roundtrip.cpp
    ${CMAKE_SOURCE_DIR}/include/timeseries/timeseriesprovider.h
    ${CMAKE_SOURCE_DIR}/include/timeseries/timeseriesregistry.h
    ${CMAKE_SOURCE_DIR}/src/timeseries/timeseriesprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/timeseries/timeseriesregistry.cpp
)

# Phase 1 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — UserFlagsModel
# define/undefine/value round-trips against a real engine handle, signal
# emission, and [USER_FLAGS]/[USER_FLAG_VALUES] INP persistence (the
# round-trip .inp is written to the gui-test data dir for review).
add_swmmvis_gui_test(test_userflagsmodel
    test_userflagsmodel.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/models/userflagsmodel.h
    ${CMAKE_SOURCE_DIR}/src/ui/models/userflagsmodel.cpp
)

# Phase 2 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — UserFlagsDialog staged
# commit: add / remove (value cascade), description-only redefine keeps
# values, type change clears them, empty/duplicate-name validation.
add_swmmvis_gui_test(test_userflagsdialog
    test_userflagsdialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/models/userflagsmodel.h
    ${CMAKE_SOURCE_DIR}/src/ui/models/userflagsmodel.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/userflagsdialog.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/userflagsdialog.cpp
)

# Phase 4 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — per-object
# UserFlagValuesDialog: staged set/clear by type, engine rejection of
# mistyped values, and userFlagsSummaryFor() cell summaries.
add_swmmvis_gui_test(test_userflagvaluesdialog
    test_userflagvaluesdialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/models/userflagsmodel.h
    ${CMAKE_SOURCE_DIR}/src/ui/models/userflagsmodel.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/userflagvaluesdialog.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/userflagvaluesdialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/properties/userflagseditref.h
    ${CMAKE_SOURCE_DIR}/src/ui/properties/userflagseditref.cpp
)

# Phase 5 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — full INP round-trip
# from data/userflags_fixture.inp: mixed-case names, quoted strings,
# negative integers, %g reals; edit → save → reopen fidelity. Output INP
# (userflags_phase5_out.inp) stays in data/ for review.
add_swmmvis_gui_test(test_userflags_roundtrip
    test_userflags_roundtrip.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/models/userflagsmodel.h
    ${CMAKE_SOURCE_DIR}/src/ui/models/userflagsmodel.cpp
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

# Issue 1 — pins CursorWindowSlider invariants (single-thumb animation scrubber
# with a non-interactive look-back window band; replaced the two-thumb slider).
add_swmmvis_gui_test(test_cursorwindowslider
    test_cursorwindowslider.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/cursorwindowslider.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/cursorwindowslider.h
)

# Slice AT.3 — pins ChartProperties wrapper semantics.
add_swmmvis_gui_test(test_comparisonplot_chartproperties
    test_comparisonplot_chartproperties.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/chartproperties.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/chartpropertiesdialog.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/chartproperties.h
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/chartpropertiesdialog.h
    # ChartProperties reads axis defaults from PreferencesManager + NumberFormat.
    ${CMAKE_SOURCE_DIR}/src/core/preferencesmanager.cpp
    ${CMAKE_SOURCE_DIR}/include/core/preferencesmanager.h
    # PreferencesManager custom color-ramp persistence references RasterColorRamp.
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/include/render/colorramp.h
    ${CMAKE_SOURCE_DIR}/src/plot/numberformat.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/numberformat.h
)
target_link_libraries(test_comparisonplot_chartproperties PRIVATE
    Qt6::Charts
    $<$<TARGET_EXISTS:QPropertyModel>:QPropertyModel>)

# Pins the QObject wrapper for SeriesStyle (Q_PROPERTY surface, signal
# semantics, JSON round-trip, applySeriesStyle effects on a QLineSeries).
add_swmmvis_gui_test(test_seriesstyleobject
    test_seriesstyleobject.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/seriesstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/seriesstyleobject.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/seriesstyleeditor.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/seriesstyle.h
    ${CMAKE_SOURCE_DIR}/include/plot/seriesstyleobject.h
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/seriesstyleeditor.h
    # SeriesStyle label formatting uses NumberFormat::printfSpec().
    ${CMAKE_SOURCE_DIR}/src/plot/numberformat.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/numberformat.h
)
target_link_libraries(test_seriesstyleobject PRIVATE
    Qt6::Charts
    $<$<TARGET_EXISTS:QPropertyModel>:QPropertyModel>)

# Slice AT.3 — pins computeStatistics() pure-function behaviour.
add_swmmvis_gui_test(test_comparisonplot_statstab
    test_comparisonplot_statstab.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/seriesstatistics.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/numberformat.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/seriesstatistics.h
    ${CMAKE_SOURCE_DIR}/include/plot/numberformat.h
)

# COMPARISON_PLOT_1V1_AND_TREE_PLAN Phase 2 — pins the 1v1 scatter pairing
# walk (pairSamplesNearest) + the per-attribute-row pairing recipe so every
# chart row produces baseline↔comparison pairs (second-row regression).
add_swmmvis_gui_test(test_comparisonplot_scatter
    test_comparisonplot_scatter.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/seriespairing.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/comparisonplotmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/seriesstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/plotattribute.cpp
    # seriesstyle.cpp formats point labels via NumberFormat::printfSpec().
    ${CMAKE_SOURCE_DIR}/src/plot/numberformat.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/numberformat.h
    ${CMAKE_SOURCE_DIR}/include/plot/seriespairing.h
    ${CMAKE_SOURCE_DIR}/include/plot/comparisonplotmodel.h
    ${CMAKE_SOURCE_DIR}/include/plot/irunlayer.h
)
# seriesstyle.cpp includes <QScatterSeries> (Qt6::Charts).
target_link_libraries(test_comparisonplot_scatter PRIVATE Qt6::Charts)

# COMPARISON_PLOT_1V1_AND_TREE_PLAN Phase 5 — pins the ComparisonPair
# surface (addPair validation, remove/clear, reindexing on series/run
# removal, pairsChanged emission).
add_swmmvis_gui_test(test_comparisonplot_pairs
    test_comparisonplot_pairs.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/comparisonplotmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/seriesstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/plotattribute.cpp
    # seriesstyle.cpp formats point labels via NumberFormat::printfSpec().
    ${CMAKE_SOURCE_DIR}/src/plot/numberformat.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/numberformat.h
    ${CMAKE_SOURCE_DIR}/include/plot/comparisonplotmodel.h
    ${CMAKE_SOURCE_DIR}/include/plot/irunlayer.h
)
# seriesstyle.cpp includes <QScatterSeries> (Qt6::Charts).
target_link_libraries(test_comparisonplot_pairs PRIVATE Qt6::Charts)

# Slice AT.3 — pins the model surface that the series-tree checkbox + context
# menu drives (updateStyle / removeSeries / Plot-This-Only loop).
add_swmmvis_gui_test(test_comparisonplot_seriestree
    test_comparisonplot_seriestree.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/comparisonplotmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/seriesstyle.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/plotattribute.cpp
    # seriesstyle.cpp formats point labels via NumberFormat::printfSpec().
    ${CMAKE_SOURCE_DIR}/src/plot/numberformat.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/numberformat.h
    ${CMAKE_SOURCE_DIR}/include/plot/comparisonplotmodel.h
    ${CMAKE_SOURCE_DIR}/include/plot/seriesstyle.h
    ${CMAKE_SOURCE_DIR}/include/plot/plotattribute.h
    ${CMAKE_SOURCE_DIR}/include/plot/irunlayer.h
)
# seriesstyle.cpp includes <QScatterSeries>, which lives in Qt6::Charts;
# without this link the include is unresolved on MSVC.
target_link_libraries(test_comparisonplot_seriestree PRIVATE Qt6::Charts)

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

# Pins the [2D_OPTIONS] VFR-closure ABI (CELL_CLOSURE / FACE_RECONSTRUCTION /
# VFR_MIN_WET_FRAC) that SimulationOptionsDialog's new closure controls persist
# through swmm_options_*_ext + swmm_model_write. Engine-ABI only (no dialog).
add_swmmvis_gui_test(test_2d_vfr_options_contract
    test_2d_vfr_options_contract.cpp
)

# Full disk round-trip for the [OPTIONS] keys whose broken C-API surface made
# Simulation Options dialog edits revert on reload (MINIMUM_STEP missing from
# swmm_options_get/set, LAT/SYS_FLOW_TOL fraction-vs-percent double
# conversion, RULE_STEP > 24 h clamping). Engine-ABI only (no dialog).
add_swmmvis_gui_test(test_simoptions_persistence_contract
    test_simoptions_persistence_contract.cpp
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

# TODO(Slice Z follow-up) — three tests below are temporarily disabled.
# They predate the Rule Model refactor and rely on hand-rolled stub
# bodies for SWMMModelLayer/OpenSWMMVisLayer virtuals + Q_PROPERTY
# accessors. The new Rule Model adds ruleList(), TemporalSpec/MaskSpec/
# JoinSpec/AuxStorageSpec/DiagramSpec properties on the base layer, plus
# SpatialReferenceSystem became a richer Q_PROPERTY surface — the stub
# block would need ~50 more one-liners to satisfy MOC vtable + comparator
# generation. Right move is to rewrite these tests against a small static
# lib in a follow-up slice; for now they're skipped so the build is
# clean.
#
# add_swmmvis_gui_test(test_hydrograph_models   test_hydrograph_models.cpp …)
# add_swmmvis_gui_test(test_control_rule_models test_control_rule_models.cpp …)
# add_swmmvis_gui_test(test_rules_editor_dialog test_rules_editor_dialog.cpp …)

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
    ${CMAKE_SOURCE_DIR}/src/ui/properties/swmmtransectpropertyadapter.cpp
    ${CMAKE_SOURCE_DIR}/src/core/unitsystem.cpp
    # The rain-gage adapter reconciles a gage's column when its path changes
    # (review R3), so it now needs the shared column reader — and that needs
    # the timeseries timestamp parser it is built on.
    ${CMAKE_SOURCE_DIR}/src/ui/util/externalcolumnfile.cpp
    ${CMAKE_SOURCE_DIR}/src/io/timeseriesparse.cpp
    # The rain-gage adapter reconciles its stored column against the file on
    # disk via ui/util/externalcolumnfile.
    ${CMAKE_SOURCE_DIR}/src/ui/util/externalcolumnfile.cpp
    ${CMAKE_SOURCE_DIR}/src/io/timeseriesparse.cpp
    # Headers listed explicitly so AUTOMOC picks up their Q_OBJECT macros.
    ${CMAKE_SOURCE_DIR}/include/ui/properties/swmmdataobjectpropertyadapter.h
    ${CMAKE_SOURCE_DIR}/include/ui/properties/swmmpollutantpropertyadapter.h
    ${CMAKE_SOURCE_DIR}/include/ui/properties/swmmlandusepropertyadapter.h
    ${CMAKE_SOURCE_DIR}/include/ui/properties/swmmraingagepropertyadapter.h
    ${CMAKE_SOURCE_DIR}/include/ui/properties/swmmtransectpropertyadapter.h
    ${CMAKE_SOURCE_DIR}/include/core/unitsystem.h
)

# swmmnodepropertyadapter.cpp references SWMMResultsLayer::nodeStat* +
# OutputStatsRegistry::identityFor + SWMMModelLayer::ensureUserFlagsModel;
# linking the real definitions pulls SWMMResultsLayer's full transitive chain.
# The unit test never sets a stats source or a layer, so those paths are
# unreachable — test_nodepropertyadapter_stubs.cpp supplies trivial link-only
# definitions instead, the same technique test_typeconversionflow uses for the
# SWMMModelLayer::apply*Convert methods. (Extracting a proper stats interface so
# the adapter no longer depends on that surface remains a Slice Z follow-up.)
add_swmmvis_gui_test(test_nodepropertyadapter
    test_nodepropertyadapter.cpp
    test_nodepropertyadapter_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/properties/swmmnodepropertyadapter.cpp
    ${CMAKE_SOURCE_DIR}/src/core/unitsystem.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/properties/swmmnodepropertyadapter.h
    ${CMAKE_SOURCE_DIR}/include/core/unitsystem.h
)

# TODO(Slice Z follow-up) — disabled. linkcompoundeditdialog.cpp now
# pulls TransectEditorDialog + TransectRegistry + LabeledPickerCombo +
# SWMMModelLayer::ensureTransectRegistry (all new after the Rule Model
# refactor) into the test link. Same fix as
# test_nodepropertyadapter above: split the dialog's transect-picker
# surface so the adapter test can compile without the full chain.
#
# add_swmmvis_gui_test(test_linkpropertyadapter test_linkpropertyadapter.cpp …)

# Node/link type conversion — the shared TypeConversionFlow text builders
# plus the engine swmm_node_convert / swmm_link_convert contract the layer
# methods wrap. Self-contained: compiles typeconversionflow.cpp and stubs
# the two SWMMModelLayer::apply*Convert methods at link time (the modal
# run() is never invoked), so it avoids the full SWMMModelLayer link chain.
add_swmmvis_gui_test(test_typeconversionflow
    test_typeconversionflow.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/typeconversionflow.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/typeconversionflow.h
)

# Slice TA, §T.6 — covers the subcatchment-side `tag` Q_PROPERTY round-trip
#   (mirror of the Slice SA link test) + the metaobject writable contract
#   + `displayLabelFor` localisation + rename-keeps-tag + multi-subcatch
#   isolation.
# Self-contained: pulls in the subcatch adapter and UnitSystem only.
add_swmmvis_gui_test(test_subcatchpropertyadapter
    test_subcatchpropertyadapter.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/properties/swmmsubcatchpropertyadapter.cpp
    ${CMAKE_SOURCE_DIR}/src/core/unitsystem.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/properties/swmmsubcatchpropertyadapter.h
    ${CMAKE_SOURCE_DIR}/include/core/unitsystem.h
    # The adapter's row builder references the USER_FLAGS surface
    # (userFlagsSummaryFor + SWMMModelLayer::ensureUserFlagsModel). Pull in the
    # two small real helpers; resolve the heavy layer method with a link stub
    # rather than the ~5000-line swmmmodellayer.cpp (the tag test has no layer).
    ${CMAKE_SOURCE_DIR}/src/ui/properties/userflagseditref.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/properties/userflagseditref.h
    ${CMAKE_SOURCE_DIR}/src/ui/models/userflagsmodel.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/models/userflagsmodel.h
    swmmmodellayer_userflags_linkstub.cpp
)

# ── Slice SP — section / profile preview panels ─────────────────────────────
# SP.1: the engine cross-section sampler. Links only the sampler + the icon
# renderer's nominal-geom table; the assertions are engine-facing (does every
# surfaced shape build, does the sampled outline match swmm_xsect_full_properties)
# so this is the test that fails first if an engine update changes the xsect
# API or renumbers SWMM_XSectShape.
add_swmmvis_gui_test(test_xsectsampler
    test_xsectsampler.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sectionview/xsectsampler.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sectionview/xsecticonrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sectionview/sectiondiagram.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/sectionview/xsectsampler.h
    ${CMAKE_SOURCE_DIR}/include/ui/sectionview/xsecticonrenderer.h
    ${CMAKE_SOURCE_DIR}/include/ui/properties/xsectshapegeom.h
)

# SP.2 / SP.6: diagram model, painter and the LID layer stacks. No engine
# calls — the model is a plain value, which is the point of the builder/painter
# split, so this one runs anywhere Qt does.
add_swmmvis_gui_test(test_sectiondiagram
    test_sectiondiagram.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sectionview/sectiondiagram.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sectionview/sectionpreviewwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sectionview/lidlayerdiagram.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/sectionview/sectiondiagram.h
    ${CMAKE_SOURCE_DIR}/include/ui/sectionview/sectionpreviewwidget.h
    ${CMAKE_SOURCE_DIR}/include/ui/sectionview/lidlayerdiagram.h
)

# SP.3: the engine → model builders. Unlike test_sectiondiagram above this one
# DOES touch the engine (in-memory fixtures, no .inp), because the numbers on
# the leader labels — invert and crown elevations at each end — are the whole
# point of the builders and the only place a wrong one would surface.
add_swmmvis_gui_test(test_sectionmodelbuilders
    test_sectionmodelbuilders.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sectionview/sectionmodelbuilders.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sectionview/xsectsampler.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sectionview/sectiondiagram.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/sectionview/sectionmodelbuilders.h
    ${CMAKE_SOURCE_DIR}/include/ui/sectionview/xsectsampler.h
    ${CMAKE_SOURCE_DIR}/include/ui/sectionview/sectiondiagram.h
)

# Inline cross-section geom1..geom4 — direct-edit fields surfaced next to
# the compound XSection dialog (both Property Browser + Attribute Table).
# Covers the shared shape/geom applicability helper and the link adapter's
# read-modify-write geom getters/setters. Lean link chain, same shape as
# test_subcatchpropertyadapter (the heavier test_linkpropertyadapter, which
# also drives LinkCompoundEditDialog, stays disabled pending a transect
# dependency split — see the commented block below).
add_swmmvis_gui_test(test_xsectgeominline
    test_xsectgeominline.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/properties/swmmlinkpropertyadapter.cpp
    ${CMAKE_SOURCE_DIR}/src/core/unitsystem.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/properties/swmmlinkpropertyadapter.h
    ${CMAKE_SOURCE_DIR}/include/ui/properties/xsectshapegeom.h
    ${CMAKE_SOURCE_DIR}/include/core/unitsystem.h
    # The adapter's userFlagsRef() row references userFlagsSummaryFor +
    # SWMMModelLayer::ensureUserFlagsModel; pull in the two small helpers and
    # resolve the layer method with the existing link stub (no layer here).
    ${CMAKE_SOURCE_DIR}/src/ui/properties/userflagseditref.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/properties/userflagseditref.h
    ${CMAKE_SOURCE_DIR}/src/ui/models/userflagsmodel.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/models/userflagsmodel.h
    swmmmodellayer_userflags_linkstub.cpp
)

# Slice BC, Phase 8.7.2 — pins the engine-ABI contract that
# `swmm_link_get_offset_up/dn` always returns offsets as **depth above the
# connecting node invert**, regardless of the source .inp's LINK_OFFSETS
# setting. The GUI's `profilenetworkadapter_model.cpp` removed the in-GUI
# elevation→depth re-conversion under this contract; this test guards the
# contract end-to-end via two fixtures describing the same physical
# conduit in DEPTH and ELEVATION mode.
#
# It also covers the pure half of the adapter (toNodeKind / renderRimDepth),
# which decides that a virtual junction draws as pipe rather than as a manhole
# and that its rendering-only [VIRTUAL_JUNCTIONS] MaxDepth — not the derived
# pipe crown — is where the ground line goes.
add_swmmvis_gui_test(test_profilenetworkadapter_model
    test_profilenetworkadapter_model.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/profilenetworkadapter.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/profilenetworkadapter.h
    # buildPathStatic() calls ProfileBuilder::computeChainage; both files are
    # the pure-logic halves and link without the model layer.
    ${CMAKE_SOURCE_DIR}/src/plot/profilebuilder.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/profilebuilder.h
)

add_swmmvis_gui_test(test_crschangedialog
    test_crschangedialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/crschangedialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/crschangedialog.h
    ${CMAKE_SOURCE_DIR}/include/ui/theme/thememanager.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/thememanager.cpp
)

add_swmmvis_gui_test(test_aboutdialog
    test_aboutdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/aboutdialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/aboutdialog.h
)

# Slice IO-10 — relative-path picker widget unit test.
add_swmmvis_gui_test(test_relativepathpicker
    test_relativepathpicker.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/relativepathpicker.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/relativepathpicker.h
)

# Slice IO-10 — IoPortabilityNormalizer pre-save check.
add_swmmvis_gui_test(test_ioportabilitynormalizer
    test_ioportabilitynormalizer.cpp
    ${CMAKE_SOURCE_DIR}/src/project/ioportabilitynormalizer.cpp
    ${CMAKE_SOURCE_DIR}/include/project/ioportabilitynormalizer.h
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

# Volume-conserving weighted blend of rain-gage series. Reproduces the engine's
# boxcar reading of a rainfall table (Gage.cpp::updateAllGages).
add_swmmvis_gui_test(test_gageblend
    test_gageblend.cpp
    ${CMAKE_SOURCE_DIR}/src/core/gageblend.cpp
    ${CMAKE_SOURCE_DIR}/include/core/gageblend.h
)

# Thiessen area shares, interior sampling, IDW fallback, weight clustering.
add_swmmvis_gui_test(test_gageassignment
    test_gageassignment.cpp
    ${CMAKE_SOURCE_DIR}/src/core/gageassignment.cpp
    ${CMAKE_SOURCE_DIR}/src/core/editgeometry.cpp
    ${CMAKE_SOURCE_DIR}/include/core/gageassignment.h
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
    ${CMAKE_SOURCE_DIR}/src/mesh/trirefinehook.cpp
    ${CMAKE_SOURCE_DIR}/include/mesh/meshgenerator.h
    ${CMAKE_SOURCE_DIR}/include/mesh/meshresult.h
)
target_link_libraries(test_meshgenerator PRIVATE triangle_lib)

add_swmmvis_gui_test(test_nninterpdegenerate
    test_nninterpdegenerate.cpp
    ${CMAKE_SOURCE_DIR}/src/mesh/naturalnbinterpolator.cpp
    ${CMAKE_SOURCE_DIR}/src/mesh/trirefinehook.cpp
    ${CMAKE_SOURCE_DIR}/include/mesh/naturalnbinterpolator.h
)
target_link_libraries(test_nninterpdegenerate PRIVATE triangle_lib)

# Characterization of the interpolator's NUMERIC output. test_nninterpdegenerate
# above asserts only on degenerate inputs, so it cannot catch a weight
# regression — which would silently change the mesh generator's no-DTM
# elevation fallback.
add_swmmvis_gui_test(test_nninterpvalues
    test_nninterpvalues.cpp
    ${CMAKE_SOURCE_DIR}/src/mesh/naturalnbinterpolator.cpp
    ${CMAKE_SOURCE_DIR}/src/mesh/trirefinehook.cpp
    ${CMAKE_SOURCE_DIR}/include/mesh/naturalnbinterpolator.h
)
target_link_libraries(test_nninterpvalues PRIVATE triangle_lib)

add_swmmvis_gui_test(test_dtmsampler
    test_dtmsampler.cpp
    ${CMAKE_SOURCE_DIR}/src/mesh/dtmsampler.cpp
    ${CMAKE_SOURCE_DIR}/include/mesh/dtmsampler.h
)
target_include_directories(test_dtmsampler PRIVATE ${GDAL_INCLUDE_DIRS})
target_link_libraries(test_dtmsampler PRIVATE GDAL::GDAL)

# PSLG preparation: hole-ring simplify/validate/densify/seed (serial and
# parallel), the y-banded point-in-rings index, and snapAndDedupe.
find_package(Qt6 REQUIRED COMPONENTS Concurrent)
add_swmmvis_gui_test(test_pslgprep
    test_pslgprep.cpp
    ${CMAKE_SOURCE_DIR}/src/mesh/pslgprep.cpp
    ${CMAKE_SOURCE_DIR}/src/core/editgeometry.cpp
    ${CMAKE_SOURCE_DIR}/include/mesh/pslgprep.h
)
target_link_libraries(test_pslgprep PRIVATE Qt6::Concurrent)

# Hilbert renumbering: permutation validity, locality gain, idempotence.
add_swmmvis_gui_test(test_meshreorder
    test_meshreorder.cpp
    ${CMAKE_SOURCE_DIR}/src/mesh/meshreorder.cpp
    ${CMAKE_SOURCE_DIR}/include/mesh/meshreorder.h
)

# Mesh stage cache: payload round-trips, key sensitivity, pruning.
add_swmmvis_gui_test(test_meshstagecache
    test_meshstagecache.cpp
    ${CMAKE_SOURCE_DIR}/src/mesh/meshstagecache.cpp
    ${CMAKE_SOURCE_DIR}/src/mesh/dtmthinner.cpp
    ${CMAKE_SOURCE_DIR}/src/map/mapextent.cpp
    ${CMAKE_SOURCE_DIR}/include/mesh/meshstagecache.h
)
target_include_directories(test_meshstagecache PRIVATE ${GDAL_INCLUDE_DIRS})
target_link_libraries(test_meshstagecache PRIVATE GDAL::GDAL)

# Batch sampler parity: sampleMany must be bit-identical to per-point
# sampleAt across NoData, edge clamps, and strip-boundary windows.
add_swmmvis_gui_test(test_dtmthinner_samplemany
    test_dtmthinner_samplemany.cpp
    ${CMAKE_SOURCE_DIR}/src/mesh/dtmthinner.cpp
    ${CMAKE_SOURCE_DIR}/include/mesh/dtmthinner.h
    ${CMAKE_SOURCE_DIR}/src/map/mapextent.cpp
)
target_include_directories(test_dtmthinner_samplemany PRIVATE ${GDAL_INCLUDE_DIRS})
target_link_libraries(test_dtmthinner_samplemany PRIVATE GDAL::GDAL)

# Banded (tiled) thinning: forced tiny per-band budgets must be bit-identical
# to the untiled run (halo/ghost-zone exactness, global lattice, NoData across
# seams, row-major ordering), plus progress/cancel and guard-rail coverage.
add_swmmvis_gui_test(test_dtmthinner_banded
    test_dtmthinner_banded.cpp
    ${CMAKE_SOURCE_DIR}/src/mesh/dtmthinner.cpp
    ${CMAKE_SOURCE_DIR}/include/mesh/dtmthinner.h
    ${CMAKE_SOURCE_DIR}/src/map/mapextent.cpp
)
target_include_directories(test_dtmthinner_banded PRIVATE ${GDAL_INCLUDE_DIRS})
target_link_libraries(test_dtmthinner_banded PRIVATE GDAL::GDAL)

# DEM-raster longitudinal profile sampler — the terrain peer of
# MeshProfileSampler. Pure logic over DTMSampler (bilinear) +
# SpatialReferenceSystem (canvas → raster CRS), so it links GDAL directly and
# pulls in no layer / widget graph.
add_swmmvis_gui_test(test_rasterprofilesampler
    test_rasterprofilesampler.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/rasterprofilesampler.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/rasterprofilesampler.h
    ${CMAKE_SOURCE_DIR}/include/plot/profilesection.h
    ${CMAKE_SOURCE_DIR}/src/mesh/dtmsampler.cpp
    ${CMAKE_SOURCE_DIR}/include/mesh/dtmsampler.h
    ${CMAKE_SOURCE_DIR}/src/map/spatialreferencesystem.cpp
    ${CMAKE_SOURCE_DIR}/include/map/spatialreferencesystem.h
)
target_include_directories(test_rasterprofilesampler PRIVATE ${GDAL_INCLUDE_DIRS})
target_link_libraries(test_rasterprofilesampler PRIVATE GDAL::GDAL)

# 2D two-zone groundwater preview dialog — guards that the placeholder stays
# a placeholder (no editable input) and that its vocabularies match the
# engine's draft [2D_AQUIFER] section.
add_swmmvis_gui_test(test_mesh2dgroundwaterdialog
    test_mesh2dgroundwaterdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/mesh2dgroundwaterdialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/mesh2dgroundwaterdialog.h
)

# Per-cell 2D parameter registry — the single table the mesh toolbar, the
# Cell Data dialog and the cell property adapter all read. Layer-free by
# design (writes live in map/meshcommands.cpp), so this links nothing heavy.
add_swmmvis_gui_test(test_meshcellparams
    test_meshcellparams.cpp
    ${CMAKE_SOURCE_DIR}/src/mesh/meshcellparams.cpp
    ${CMAKE_SOURCE_DIR}/include/mesh/meshcellparams.h
    ${CMAKE_SOURCE_DIR}/include/mesh/meshresult.h
)

add_swmmvis_gui_test(test_inpmeshwriter
    test_inpmeshwriter.cpp
    ${CMAKE_SOURCE_DIR}/src/mesh/inpmeshwriter.cpp
    # patchAttributeSections coverage round-trips through the reader.
    ${CMAKE_SOURCE_DIR}/src/mesh/inpmeshreader.cpp
    # inpmeshwriter.cpp now uses mesh::MeshBCTypes::inpToken for the BC
    # section serialiser.
    ${CMAKE_SOURCE_DIR}/src/mesh/meshbctype.cpp
    ${CMAKE_SOURCE_DIR}/include/mesh/inpmeshreader.h
    ${CMAKE_SOURCE_DIR}/include/mesh/inpmeshwriter.h
    ${CMAKE_SOURCE_DIR}/include/mesh/meshbctype.h
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
    # Slice Z — RuleBasedRenderer depends on ExpressionEvaluator.
    ${CMAKE_SOURCE_DIR}/src/render/expressionevaluator.cpp
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
    # Slice Z (RENDERING_RULE_MODEL_PLAN) — OpenSWMMVisLayer's new
    # Q_PROPERTY tabs (Temporal / Mask / Auxiliary Storage / Joins /
    # Diagrams) need their value-type operator== implementations linked
    # in for the MOC equality slot. XYZTileLayer::render also calls
    # BasemapRenderParams::applyTo.
    ${CMAKE_SOURCE_DIR}/src/render/temporalspec.cpp
    ${CMAKE_SOURCE_DIR}/src/render/maskspec.cpp
    ${CMAKE_SOURCE_DIR}/src/render/auxiliarystoragespec.cpp
    ${CMAKE_SOURCE_DIR}/src/render/joinspec.cpp
    ${CMAKE_SOURCE_DIR}/src/render/diagramspec.cpp
    ${CMAKE_SOURCE_DIR}/src/render/basemaprenderparams.cpp
)
target_link_libraries(test_arcgisrestrendering PRIVATE
    Qt6::Network
    GDAL::GDAL
)
target_include_directories(test_arcgisrestrendering PRIVATE ${GDAL_INCLUDE_DIRS})

# TilePyramidLayer — the shared tile cache + extent-based coarser-tile
# fallback behind GISRasterLayer/WMTSLayer. Self-contained: only the base
# class and the layer/value-type sources it drags in (same closure as
# test_arcgisrestrendering, minus the XYZ layer).
add_swmmvis_gui_test(test_tilepyramidfallback
    test_tilepyramidfallback.cpp
    ${CMAKE_SOURCE_DIR}/src/layers/tilepyramidlayer.cpp
    ${CMAKE_SOURCE_DIR}/include/layers/tilepyramidlayer.h
    ${CMAKE_SOURCE_DIR}/src/layers/openswmmvislayer.cpp
    ${CMAKE_SOURCE_DIR}/include/layers/openswmmvislayer.h
    ${CMAKE_SOURCE_DIR}/src/map/mapextent.cpp
    ${CMAKE_SOURCE_DIR}/include/map/mapextent.h
    ${CMAKE_SOURCE_DIR}/src/map/spatialreferencesystem.cpp
    ${CMAKE_SOURCE_DIR}/include/map/spatialreferencesystem.h
    ${CMAKE_SOURCE_DIR}/src/render/temporalspec.cpp
    ${CMAKE_SOURCE_DIR}/src/render/maskspec.cpp
    ${CMAKE_SOURCE_DIR}/src/render/auxiliarystoragespec.cpp
    ${CMAKE_SOURCE_DIR}/src/render/joinspec.cpp
    ${CMAKE_SOURCE_DIR}/src/render/diagramspec.cpp
)
target_link_libraries(test_tilepyramidfallback PRIVATE
    GDAL::GDAL
)
target_include_directories(test_tilepyramidfallback PRIVATE ${GDAL_INCLUDE_DIRS})

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
    ${CMAKE_SOURCE_DIR}/include/ui/theme/thememanager.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/thememanager.cpp
)

# Climatology dialog — instantiated against a synthetic BUILDING engine; the
# dialog is dependency-light (Qt Widgets + the swmm_climate_* engine ABI only).
add_swmmvis_gui_test(test_climatologydialog
    test_climatologydialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/climatologydialog.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/climatologydialog.cpp
    # The climate-file row is a RelativePathPicker, so the project's relative
    # path convention applies to it like every other secondary file reference.
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/relativepathpicker.h
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/relativepathpicker.cpp
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

# Simulation Status "2D Err (%)" column — runs the real SimulationRunner
# (in-process engine) wired to a real SimulationStatusModel against a minimal
# inline 2D model and a minimal 1D model. Run artifacts land in
# tests/gui/data/output_simstatus2derr/ for post-test review.
find_package(Qt6 REQUIRED COMPONENTS Concurrent)
# Headers are listed explicitly because they live in include/ (not next to
# the .cpp), so AUTOMOC would otherwise skip their Q_OBJECT moc output.
add_swmmvis_gui_test(test_simstatus_2derr
    test_simstatus_2derr.cpp
    ${CMAKE_SOURCE_DIR}/src/simulation/simulationrunner.cpp
    ${CMAKE_SOURCE_DIR}/src/simulation/simulationstatusmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/core/preferencesmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/include/simulation/simulationrunner.h
    ${CMAKE_SOURCE_DIR}/include/simulation/simulationstatusmodel.h
    ${CMAKE_SOURCE_DIR}/include/core/preferencesmanager.h
)
target_link_libraries(test_simstatus_2derr PRIVATE Qt6::Concurrent)

# LayerTreeModel test (test_layertreemodel.cpp) is parked until the
# `swmmvis_core` static-library extraction lands. Compiling layertreepanel.cpp
# in isolation pulls in Qt6Network + GIS layer + WMS dialog code — too much to
# manually mirror in a leaf test. Tracked under "Known follow-ups" in
# docs/GUI_IMPLEMENTATION_PLAN.md.

# ─────────────────────────────────────────────────────────────────────────
# test_typedselection / test_asyncload — coverage for two Windows-adaptation
# commits: a35798e (fix(selection): typed per-kind selection/visibility) and
# 82fc82b (feat(load): async model load).
#
# Both drive the real SWMMModelLayer (Q_OBJECT, ~6750-line .cpp), which has
# never been linked into a tests/gui/ binary before: AUTOMOC-generating its
# header pulls the whole swmmmodellayer.cpp translation unit in (every
# symbol it transitively touches — the render/rule/sublayer stack, ~10
# data-object registries, GDAL/OGR — must resolve at link time), and there
# is no narrow seam to cherry-pick around that (see the three still-disabled
# tests further up this file for prior attempts at exactly that).
#
# Root CMakeLists.txt assembles PROJECT_SOURCES — essentially every .cpp/.h
# the shipping SWMMVis app compiles — BEFORE add_subdirectory(tests) runs,
# so it is visible here via ordinary CMake variable scoping. Building our
# own executable from that same list (minus src/main.cpp) compiles exactly
# what the app already compiles successfully, sidestepping the missing-
# symbol whack-a-mole entirely.
# PROJECT_SOURCES entries are written relative to the ROOT source dir (the
# scope it was assembled in) — e.g. "include/swmmvis.h". Reused as-is here,
# CMake would resolve them relative to THIS directory (tests/gui/) instead
# and fail to find them, so every relative entry is re-rooted at
# CMAKE_SOURCE_DIR; entries that are already absolute (icon/resource paths
# set via CMAKE_CURRENT_SOURCE_DIR at root scope) pass through unchanged.
set(SWMMVIS_TEST_SOURCES "")
foreach(_swmmvis_src ${PROJECT_SOURCES})
    if(IS_ABSOLUTE "${_swmmvis_src}")
        list(APPEND SWMMVIS_TEST_SOURCES "${_swmmvis_src}")
    else()
        list(APPEND SWMMVIS_TEST_SOURCES "${CMAKE_SOURCE_DIR}/${_swmmvis_src}")
    endif()
endforeach()
list(REMOVE_ITEM SWMMVIS_TEST_SOURCES "${CMAKE_SOURCE_DIR}/src/main.cpp")
# forms/swmmvis.ui is listed as a direct SOURCE (not merely found via
# CMAKE_AUTOUIC_SEARCH_PATHS) in PROJECT_SOURCES. AUTOUIC's generated-header
# rule for a directly-listed .ui source is keyed by its path relative to
# CMAKE_CURRENT_BINARY_DIR, not per-target — fine for the single SWMMVis
# target, but our TWO new targets share this directory's binary dir, so
# both would emit a "generate tests/gui/forms/ui_swmmvis.h" ninja rule and
# collide. Drop it from the source list; CMAKE_AUTOUIC_SEARCH_PATHS=forms
# (set at root scope, inherited here) still lets AUTOUIC find and generate
# it on demand — into each target's own <target>_autogen dir — for
# whichever .cpp does `#include "ui_swmmvis.h"`.
list(REMOVE_ITEM SWMMVIS_TEST_SOURCES "${CMAKE_SOURCE_DIR}/forms/swmmvis.ui")

# On Apple, SWMMVis adds this Objective-C++ source via target_sources()
# rather than through PROJECT_SOURCES, and links AppKit. It's needed here
# too: src/swmmvisapplication.cpp (part of PROJECT_SOURCES) calls
# openswmmvis::platform::attachAsChildWindow() under an Apple-only #ifdef,
# and that symbol is defined in this .mm file.
set(SWMMVIS_TEST_EXTRA_SOURCES "")
if(APPLE)
    list(APPEND SWMMVIS_TEST_EXTRA_SOURCES ${CMAKE_SOURCE_DIR}/src/platform/macoswindowutils.mm)
endif()

# Mirrors target_link_libraries(SWMMVis ...) at the top of the root
# CMakeLists.txt — every one of these libraries is transitively required by
# some file in PROJECT_SOURCES.
find_package(Qt6 REQUIRED COMPONENTS OpenGL OpenGLWidgets Network Concurrent
    Svg Charts Qml Quick QuickWidgets PrintSupport)
find_package(HDF5 REQUIRED COMPONENTS C)

function(swmmvis_link_full_app_deps TEST_NAME)
    # CMAKE_AUTOUIC_SEARCH_PATHS is set to the RELATIVE path "forms" at root
    # scope, resolved against each target's own CMAKE_CURRENT_SOURCE_DIR —
    # "forms" under the root source dir for SWMMVis, but under tests/gui/
    # (nonexistent) for targets defined here. src/swmmvis.cpp's
    # `#include "ui_swmmvis.h"` needs forms/swmmvis.ui found via an absolute
    # search path instead.
    set_target_properties(${TEST_NAME} PROPERTIES
        AUTOUIC_SEARCH_PATHS "${CMAKE_SOURCE_DIR}/forms")
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/forms
        ${GDAL_INCLUDE_DIRS}
        ${HDF5_INCLUDE_DIRS}
    )
    target_link_libraries(${TEST_NAME} PRIVATE
        Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Network Qt6::Concurrent
        Qt6::Svg Qt6::Charts Qt6::Qml Qt6::Quick Qt6::QuickWidgets Qt6::PrintSupport
        GDAL::GDAL OpenSSL::SSL OpenSSL::Crypto nanoflann::nanoflann
        $<$<TARGET_EXISTS:QPropertyModel>:QPropertyModel>
        openswmm_engine triangle_lib ${HDF5_C_LIBRARIES}
    )
    if(APPLE)
        target_link_libraries(${TEST_NAME} PRIVATE "-framework AppKit")
    endif()
endfunction()

# a35798e — typed per-kind selection/visibility on SWMMModelLayer.
add_swmmvis_gui_test(test_typedselection
    test_typedselection.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_typedselection)

# Object Browser refresh on data-object mutations — dataObjectsChanged →
# coalesced tree reload, registry-preferred counts (staged adds visible
# before saveToEngine; deletes disappear despite the engine-side row).
add_swmmvis_gui_test(test_objectbrowser_tree_refresh
    test_objectbrowser_tree_refresh.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_objectbrowser_tree_refresh)

# 82fc82b — async model load (SWMMModelLayer::openEngineForPath/
# adoptOpenEngine, SWMMVisProjectWindow::loadModelAsync). Also constructs a
# real SWMMVisProjectWindow (a QWidget), so the .cpp uses QTEST_MAIN (full
# QApplication) rather than QTEST_APPLESS_MAIN, even though it still runs
# under the offscreen QPA like every other gui test.
add_swmmvis_gui_test(test_asyncload
    test_asyncload.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_asyncload)

# Registry rename must not duplicate the object in the engine, and snow-pack
# parameters must survive save/reload. Both were live data-corruption bugs:
# rename() never told the engine, so saveToEngine re-ADDED the object under the
# new name; and SnowpackProvider carried no parameters at all.
add_swmmvis_gui_test(test_registry_rename
    test_registry_rename.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_registry_rename)

# The pre-run auto-save gate: SWMMVis::onRunSimulation only saves when
# hasChanges() is true and SimulationRunner then re-opens the .inp from disk,
# so an engine mutation that never reaches SWMMModelLayer::modelEdited() is
# silently dropped from the run. Drives the real layer + project window.
add_swmmvis_gui_test(test_dirtytracking_run
    test_dirtytracking_run.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_dirtytracking_run)

# Browse-and-load of an existing .2dm (SWMMVisProjectWindow::importMeshFileAsync).
# A mesh could previously only reach a model by already sitting next to its
# .inp. Guards the staging copy, the active-external-mesh adoption the save
# path retargets [2D_MESH_FILE] at, the engine MESH_FILE mirror, cleanup of
# the copy after a rejected file, and the no-clobber name-collision path.
add_swmmvis_gui_test(test_meshimport
    test_meshimport.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_meshimport)

# Spatial rain-gage assignment against a real engine-loaded model. Guards the
# un-located-gage trap (engine says (0,0); the display cache invents a position)
# and the mediated write that lets a bulk assignment refresh the views.
add_swmmvis_gui_test(test_gageassignintegration
    test_gageassignintegration.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_gageassignintegration)

# identifyByName / nodeIndex / linkIndex became hash lookups instead of linear
# scans (a 13 KB .oswp restore cost 22 s on a 122k-link model). The hashes must
# reproduce the scans' kind precedence exactly — reusing the existing
# single-keyed m_nameToSoa would have flipped subcatchment/gage collisions.
add_swmmvis_gui_test(test_namelookup_precedence
    test_namelookup_precedence.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_namelookup_precedence)

# The map scene must stay out of the platform accessibility tree. Exposing one
# QGraphicsItem per network element let macOS mint ~94 million
# QMacAccessibilityElement wrappers (4.5 GB) as it re-walked the tree on window
# activation -- i.e. every tab switch.
add_swmmvis_gui_test(test_a11y_scene_exposure
    test_a11y_scene_exposure.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_a11y_scene_exposure)

# A few corrupt coordinates must not collapse the link spatial index. West
# Whiteland 2024 has 10 junctions ~40,000,000 units out, which stretched the
# grid extent until the whole real network fitted in one cell and every query
# became a linear scan over 281,049 links.
add_swmmvis_gui_test(test_linkgrid_outliers
    test_linkgrid_outliers.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_linkgrid_outliers)

# Mesh Tiled LOD plan Phase 0 — rendering-perf baseline harness. Self-skips
# when tests/perf-data/mesh/ has no generated fixtures (they are gitignored;
# see the README there), so it is a fast no-op on CI. The 5M-triangle rows
# legitimately take minutes when the fixtures ARE present, hence the raised
# per-function and ctest timeouts.
add_swmmvis_gui_test(test_meshperf_baseline
    test_meshperf_baseline.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_meshperf_baseline)
set_tests_properties(test_meshperf_baseline PROPERTIES
    TIMEOUT 3600
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SWMMVIS_GUI_TEST_DATA=${GUI_TEST_DATA_DIR};SWMMVIS_MESH_PERF_DIR=${CMAKE_SOURCE_DIR}/tests/perf-data/mesh;QTEST_FUNCTION_TIMEOUT=1800000")

# Attribute Table performance harness. Deliberately does NOT set
# SWMMVIS_ATTR_PERF_INP — the all-pipes fixtures live outside the repo, so
# the developer names one per run and the test self-skips otherwise (a fast
# no-op on CI). Same raised timeouts as the mesh harness: a 500k-object
# category legitimately takes minutes on the pre-fix code.
add_swmmvis_gui_test(test_attrtableperf_baseline
    test_attrtableperf_baseline.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_attrtableperf_baseline)
set_tests_properties(test_attrtableperf_baseline PROPERTIES
    TIMEOUT 3600
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SWMMVIS_GUI_TEST_DATA=${GUI_TEST_DATA_DIR};SWMMVIS_ATTR_PERF_OUT=${CMAKE_SOURCE_DIR}/tests/output/attrtable_perf;QTEST_FUNCTION_TIMEOUT=1800000")

# Mesh Tiled LOD plan Phase 1 — QsgMeshEnabled preference, the mesh layer's
# qsgOwnsRendering CPU-paint gate, async worker-build equivalence, and
# QPainter-vs-QSG parity screenshots (written to tests/output/mesh_qsg_parity).
add_swmmvis_gui_test(test_meshasyncload
    test_meshasyncload.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_meshasyncload)

# Boundary-edge graph on the layer (Ctrl-click shortest-path edge selection):
# slots agree with isBoundaryEdge(), paths take the short arc, and a
# progressive load defers the build until sceneGeometryReady().
add_swmmvis_gui_test(test_meshboundarypath
    test_meshboundarypath.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_meshboundarypath)

# Undoable per-vertex / per-edge mesh attribute commands (ids 45 / 46) and the
# push helpers every editing surface funnels through — snapshot fidelity for the
# interdependent coupling + BC fields, and the interior-edge conveyance mirror
# under undo. Needs the real SWMM2DMeshLayer, hence the full-app link.
add_swmmvis_gui_test(test_meshcommands_vertex_edge
    test_meshcommands_vertex_edge.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_meshcommands_vertex_edge)

# Attribute Table's 2D mesh source — row identity (interior edges collapse to
# one row), read-only derived columns, edit → layer → single-row dataChanged,
# and the deferred-geometry gate on the Edges view.
add_swmmvis_gui_test(test_meshattributetable
    test_meshattributetable.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_meshattributetable)

# Stale-render invalidation — vertex-Z edits must invalidate the renderer's
# geomRevision-keyed shading caches, and layer changes must advance the
# renderer content revision MapCanvas polls to regrab its framebuffer.
add_swmmvis_gui_test(test_render_invalidation
    test_render_invalidation.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_render_invalidation)

# Attribute Table column schema — the per-category ColumnSpec contract.
# Guards the conduit cross-section block (XSection compound cell, inline
# Geom 1-4, Barrels) against dropping out of the schema or re-acquiring the
# per-shape edit lock, that the post-run dynamics columns stay a contiguous
# read-only suffix, and that the engine-backed attribute coverage
# (conduit Slope, subcatchment Tag, storage exfiltration, outfall RouteTo)
# is actually surfaced.
add_swmmvis_gui_test(test_attributetableschema
    test_attributetableschema.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_attributetableschema)

# Rain-gage file / column / format coupling — review follow-ups R3 (a path edit
# reconciles the column and undoes as one step) and A-2 (the file format is
# explicitly settable, which is the only way back out of USER_CSV).
add_swmmvis_gui_test(test_raingagefilecolumn
    test_raingagefilecolumn.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_raingagefilecolumn)

# Toolbar selection ops — SelectionOps::invert (category-scoped Invert
# Selection), SelectionOps::trace (Select Upstream / Downstream with
# subcatchment outlet chaining), and AttributeTablePanel::selectionAsTsv
# (Copy). Constructs a real AttributeTablePanel (a QWidget), so QTEST_MAIN
# under the offscreen QPA — same shape as test_asyncload above.
add_swmmvis_gui_test(test_selectionops
    test_selectionops.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_selectionops)

# Object-creation defaults — drawn objects must land with the configured
# ObjectDefaults for the active unit system; geometry-derived overrides win;
# undo rolls the whole object back (see
# workplans/OBJECT_CREATION_DEFAULTS_PLAN_2026-08-03.md).
add_swmmvis_gui_test(test_object_creation_defaults
    test_object_creation_defaults.cpp
    ${SWMMVIS_TEST_SOURCES}
    ${SWMMVIS_TEST_EXTRA_SOURCES}
)
swmmvis_link_full_app_deps(test_object_creation_defaults)

# UI redesign P0 — standalone audit of forms/swmmvis.ui. The form declares
# MapCanvas as a customwidget but never instantiates one, so Ui::SWMMVis
# sets up on a bare QMainWindow with no app linkage (Qt-only, fast).
# Guards: no bare-word/media-key shortcuts, no duplicate shortcuts, every
# action has text, repaired-shortcut regression anchors.
# The .ui is deliberately NOT a direct source (see the collision note above
# SWMMVIS_TEST_SOURCES) — AUTOUIC finds it via the absolute search path when
# the .cpp does `#include "ui_swmmvis.h"`.
add_swmmvis_gui_test(test_ui_form_audit
    test_ui_form_audit.cpp
    ${CMAKE_SOURCE_DIR}/resources/swmmvis.qrc
)
set_target_properties(test_ui_form_audit PROPERTIES
    AUTOUIC ON
    AUTOUIC_SEARCH_PATHS "${CMAKE_SOURCE_DIR}/forms")

# UI redesign P1 — action catalog integrity (unique ids/objectNames,
# default shortcuts parse + no duplicates, tab ids known) and
# ActionRegistry behavior (adopt-in-place identity, QSettings override
# persistence, tag enable/disable, conflict lookup, keyClick firing).
# Both link only actionregistry.cpp — no app object graph.
add_swmmvis_gui_test(test_action_catalog
    test_action_catalog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/actionregistry.h
    ${CMAKE_SOURCE_DIR}/src/ui/actionregistry.cpp
    ${CMAKE_SOURCE_DIR}/resources/swmmvis.qrc
)
add_swmmvis_gui_test(test_action_registry
    test_action_registry.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/actionregistry.h
    ${CMAKE_SOURCE_DIR}/src/ui/actionregistry.cpp
)

# UI redesign P2 — ThemeManager behavior (mode switching, palette
# application, themeChanged discipline, offscreen System→Light fallback)
# and the WCAG AA contrast gate over the design tokens (pure math, both
# schemes; fails any token edit that regresses text contrast).
add_swmmvis_gui_test(test_theme_manager
    test_theme_manager.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/theme/thememanager.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/thememanager.cpp
)
add_swmmvis_gui_test(test_theme_contrast
    test_theme_contrast.cpp
)

# Welcome-screen chrome — MDI backdrop tracks the theme and TabbedView
# sub-windows stay maximized on activation. Self-contained: the chrome
# helper depends only on QtWidgets + ThemeManager, so no app linkage.
add_swmmvis_gui_test(test_mdi_tab_maximize
    test_mdi_tab_maximize.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/mdiworkspacechrome.h
    ${CMAKE_SOURCE_DIR}/src/ui/mdiworkspacechrome.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/theme/thememanager.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/thememanager.cpp
)

# UI redesign P3 — IconFactory / ThemedIconEngine: catalog aliases render,
# light/dark/disabled renders differ, pixmap cache stable, unknown alias
# null. Links the real qrc + theme sources; QSvgRenderer needs Qt6::Svg.
add_swmmvis_gui_test(test_icon_factory
    test_icon_factory.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/theme/thememanager.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/thememanager.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/theme/iconfactory.cpp
    ${CMAKE_SOURCE_DIR}/resources/swmmvis.qrc
)
target_link_libraries(test_icon_factory PRIVATE Qt6::Svg)

# UI redesign P4 — chrome-color lint: fails on new hex/named-color
# stylesheets or QColor-literal palette writes in chrome code paths
# (src/ui, src/plot, main-window TUs) outside the reviewed allowlist.
add_test(NAME lint_chrome_colors
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/lint_chrome_colors.sh)
set_tests_properties(lint_chrome_colors PROPERTIES LABELS "gui")

# UI redesign P6 — CompactToolbarController: tab switching swaps row
# visibility, contextual tab reveal/fallback, last-tab persistence, and
# catalog tab-id vocabulary. Standalone QMainWindow, no app linkage.
add_swmmvis_gui_test(test_compact_toolbar
    test_compact_toolbar.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/toolbars/compacttoolbarcontroller.h
    ${CMAKE_SOURCE_DIR}/src/ui/toolbars/compacttoolbarcontroller.cpp
)

# UI redesign P7 — command palette: fuzzy ranking, model filtering over
# the registry, keyClicks type-filter-Enter end-to-end, Esc dismissal,
# disabled-action refusal.
add_swmmvis_gui_test(test_command_palette
    test_command_palette.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/actionregistry.h
    ${CMAKE_SOURCE_DIR}/src/ui/actionregistry.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/commandpalette.h
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/commandpalette.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/commandpalettemodel.h
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/commandpalettemodel.cpp
)

# UI redesign P8 — shortcut editor: assign persists + applies live,
# duplicate binding hard-blocked naming the holder, reserved-key warning,
# reset one/all restores catalog defaults.
add_swmmvis_gui_test(test_shortcut_editor
    test_shortcut_editor.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/actionregistry.h
    ${CMAKE_SOURCE_DIR}/src/ui/actionregistry.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/widgets/shortcuteditorwidget.h
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/shortcuteditorwidget.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/theme/thememanager.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/thememanager.cpp
)

# UI redesign iteration 2 (D1) — dialog layout persistence: extended
# helper vocabulary (geometry/splitter/header/tab/page/toggle, opt-in by
# objectName, bounds-checked, qt_* ignored) + the app-wide
# DialogLayoutWatcher (restore on first Show, save on Hide/Close,
# noLayoutPersistence opt-out). QSettings isolated to a QTemporaryDir.
add_swmmvis_gui_test(test_dialog_layout_persistence
    test_dialog_layout_persistence.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/dialoglayoutpersistence.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/dialoglayoutwatcher.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/dialoglayoutwatcher.cpp
)

# Profile attribute tracks — the synced attribute-chart pane below the
# profile plot: options round-trip / signal discipline, the pixel↔virtual-x
# alignment contract, and sampler attribute classification (inline header
# fns — the sampler .cpp itself needs the engine + results layer and is
# exercised in-app per PROFILE_ATTRIBUTE_TRACKS_VERIFICATION.md).
add_swmmvis_gui_test(test_profile_attribute_tracks
    test_profile_attribute_tracks.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/profileattributetrackoptions.h
    ${CMAKE_SOURCE_DIR}/src/plot/profileattributetrackoptions.cpp
    ${CMAKE_SOURCE_DIR}/include/plot/profileattributetrackswidget.h
    ${CMAKE_SOURCE_DIR}/src/plot/profileattributetrackswidget.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/plotattribute.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/theme/thememanager.h
    ${CMAKE_SOURCE_DIR}/src/ui/theme/thememanager.cpp
)

# Dialog window management — the MRU register of open modeless dialogs that
# backs the Window menu's recovery entries and the pure-Qt stacking mode.
# Bookkeeping only (offscreen QPA); real multi-monitor z-order behaviour is
# covered by the manual checklist in
# workplans/DIALOG_WINDOW_MANAGEMENT_VERIFICATION.md.
add_swmmvis_gui_test(test_dialog_registry
    test_dialog_registry.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/dialogregistry.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/dialogregistry.cpp
)

# UI redesign iteration 2 (D4) — dialog accessibility audit over the
# cheaply-constructible dialogs, guarding the sweep invariants (mnemonic
# uniqueness, icon-only button naming, persistence naming) via the shared
# dialog_a11y_checks.h assertions.
add_swmmvis_gui_test(test_dialog_a11y_standalone
    test_dialog_a11y_standalone.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/aboutdialog.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/aboutdialog.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/pluginsdialog.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/pluginsdialog.cpp
    ${CMAKE_SOURCE_DIR}/include/plugins/filefilterregistry.h
    ${CMAKE_SOURCE_DIR}/src/plugins/filefilterregistry.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/dialogs/licenseagreementdialog.h
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/licenseagreementdialog.cpp
)

# UI redesign iteration 2 (R2) — ribbon framework: pure solver demotion
# order + hysteresis dead band, captioned-group defaultAction mirroring
# and mode switching at constant height, measured width shrinkage,
# widget-host rigidity, and split-button last-used restore + promotion
# on trigger and on programmatic toggled(true). Standalone, no app
# linkage (ribbonlayoutsolver.h is header-only).
add_swmmvis_gui_test(test_ribbongroup
    test_ribbongroup.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/toolbars/ribbongroup.h
    ${CMAKE_SOURCE_DIR}/src/ui/toolbars/ribbongroup.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/toolbars/ribbonsplitbutton.h
    ${CMAKE_SOURCE_DIR}/src/ui/toolbars/ribbonsplitbutton.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/toolbars/ribboncompactor.h
    ${CMAKE_SOURCE_DIR}/src/ui/toolbars/ribboncompactor.cpp
)
