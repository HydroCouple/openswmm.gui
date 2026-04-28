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
