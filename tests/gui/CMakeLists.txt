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

add_swmmvis_gui_test(test_unitsystemfacade
    test_unitsystemfacade.cpp
    ${CMAKE_SOURCE_DIR}/src/core/unitsystem.cpp
    ${CMAKE_SOURCE_DIR}/include/core/unitsystem.h
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

# test_simulationoptionsdialog (test_simulationoptionsdialog.cpp) — parked
# alongside the layertreemodel test until the `swmmvis_core` static-lib
# extraction lands. The Q_OBJECT moc bindings on SimulationOptionsDialog
# now reference the Tab 5 (Spatial) slots which pull in SpatialReferenceSystem
# → OGR/GDAL — too much to link into a leaf test. Pure-helper coverage is
# still in test_simulationoptionsdialog.cpp; re-enable when the static lib
# can satisfy the moc vtable without the OGR cascade.
#
# add_swmmvis_gui_test(test_simulationoptionsdialog
#     test_simulationoptionsdialog.cpp
#     ${CMAKE_SOURCE_DIR}/src/ui/dialogs/simulationoptionshelpers.cpp
#     ${CMAKE_SOURCE_DIR}/include/ui/dialogs/simulationoptionsdialog.h
# )

# LayerTreeModel test (test_layertreemodel.cpp) is parked until the
# `swmmvis_core` static-library extraction lands. Compiling layertreepanel.cpp
# in isolation pulls in Qt6Network + GIS layer + WMS dialog code — too much to
# manually mirror in a leaf test. Tracked under "Known follow-ups" in
# docs/GUI_IMPLEMENTATION_PLAN.md.
