#
# CMakeLists.txt — OpenSWMM GUI
#
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.21)

project(
    SWMMVis
    VERSION 6.0.0
    LANGUAGES C CXX
    HOMEPAGE_URL "https://github.com/hydrocouple/openswmm.gui"
    DESCRIPTION "A SWMM model setup, analysis, and visualization tool"
)

set(PROJECT_VERSION_SUFFIX "-alpha.1")

configure_file(
    ${PROJECT_SOURCE_DIR}/include/version.h.in
    ${PROJECT_SOURCE_DIR}/include/version.h
)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS forms)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SWMMVIS_BUILD_TESTS "Build unit tests" OFF)

# Project CMake helpers live under cmake/.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ── Qt root discovery from env ──────────────────────────────────────────────
# CMakePresets inject CMAKE_PREFIX_PATH from $env{QT_ROOT_DIR}. Replicate the
# same behaviour for non-preset `cmake -B build` invocations so a developer
# with QT_ROOT_DIR (or Qt6_DIR / QTDIR) exported in their shell can configure
# without a preset. list(PREPEND) keeps cache-var / command-line overrides
# winning when both mechanisms are active.
foreach(_qt_env_var IN ITEMS QT_ROOT_DIR Qt6_DIR QTDIR)
    if(DEFINED ENV{${_qt_env_var}} AND NOT "$ENV{${_qt_env_var}}" STREQUAL "")
        list(PREPEND CMAKE_PREFIX_PATH "$ENV{${_qt_env_var}}")
        message(STATUS "Qt: picking up $ENV{${_qt_env_var}} from env var ${_qt_env_var}")
        break()
    endif()
endforeach()
unset(_qt_env_var)

# ── Qt ──────────────────────────────────────────────────────────────────────
find_package(QT NAMES Qt6 Qt5 REQUIRED
    COMPONENTS Widgets OpenGL OpenGLWidgets Network Concurrent LinguistTools Svg)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED
    COMPONENTS Widgets OpenGL OpenGLWidgets Network Concurrent LinguistTools Svg Charts)

# ── GDAL (GIS/CRS) ──────────────────────────────────────────────────────────
find_package(GDAL REQUIRED)

# ── QPropertyModel (sibling checkout, else GitHub fetch) ────────────────────
include(FetchQPropertyModel)

# ── openswmm.engine (hard dependency — sibling checkout or GitHub fetch) ─────
include(FetchOpenSWMMEngine)

set(TS_FILES translation/SWMMVis_en_US.ts)

# macOS / Windows icons
set(MACOSX_BUNDLE_ICON_FILE swmmvis.icns)
set(app_icon_macos "${CMAKE_CURRENT_SOURCE_DIR}/resources/swmmvis.icns")
set_source_files_properties(${app_icon_macos} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
set(app_icon_resource_windows "${CMAKE_CURRENT_SOURCE_DIR}/resources/swmmvis.rc")

set(
    PROJECT_SOURCES

        # ── App ──────────────────────────────────────────────────────────────
        include/swmmvis.h
        include/swmmvisapplication.h
        include/swmmvisprojectwindow.h
        include/swmmvissplashscreen.h

        # ── Core ─────────────────────────────────────────────────────────────
        include/core/measurementunitmanager.h
        include/core/openswmmvislogmessage.h
        include/core/openswmmvisservice.h
        include/core/unitsystem.h
        include/core/crsreproject.h
        include/core/editgeometry.h
        include/selection/selectionmanager.h

        # ── Layers ───────────────────────────────────────────────────────────
        include/layers/openswmmvislayer.h
        include/layers/gisvectorlayer.h
        include/layers/gisrasterlayer.h
        include/layers/wmslayer.h
        include/layers/wmtslayer.h
        include/layers/swmmmodellayer.h
        include/layers/swmmresultslayer.h
        include/layers/xyztilelayer.h

        # ── Map / GIS ────────────────────────────────────────────────────────
        include/map/spatialreferencesystem.h
        include/map/mapextent.h
        include/map/crsmanager.h
        include/map/mapcanvas.h
        include/map/maprenderjob.h
        include/map/mapundostack.h
        include/map/openswmmvisgraphicsview.h
        include/map/openswmmvisscene.h
        include/map/graphicsitems.h

        # ── Map tools ────────────────────────────────────────────────────────
        include/map/tools/maptool.h
        include/map/tools/maptoolpan.h
        include/map/tools/maptoolzoom.h
        include/map/tools/maptoolmeasure.h
        include/map/tools/maptoolselect.h
        include/map/tools/maptoolidentify.h
        include/map/tools/maptoolmovenode.h
        include/map/tools/maptooleditvertex.h
        include/map/tools/maptooladdnode.h

        # ── Project / workspace ──────────────────────────────────────────────
        include/project/openswmmvisworkspace.h
        include/project/openswmmvisworkspacemodel.h
        include/project/openswmmvissession.h

        # ── Simulation ───────────────────────────────────────────────────────
        include/simulation/simulationrunner.h
        include/simulation/simulationstatusmodel.h

        # ── UI panels ────────────────────────────────────────────────────────
        include/ui/panels/attributepanel.h
        include/ui/panels/layertreepanel.h
        include/ui/panels/objectbrowserpanel.h

        # ── Dialogs ──────────────────────────────────────────────────────────
        include/ui/dialogs/crsselectiondialog.h
        include/ui/dialogs/crschangedialog.h
        include/ui/dialogs/aboutdialog.h
        include/ui/dialogs/layerpropertiesdialog.h
        include/ui/dialogs/simulationoptionsdialog.h
        include/ui/dialogs/timeseriesplotdialog.h
        include/ui/dialogs/wmsconnectiondialog.h
        include/ui/dialogs/wmtsconnectiondialog.h

        # ── Source files ─────────────────────────────────────────────────────
        src/main.cpp

        # App
        src/swmmvis.cpp
        src/swmmvisapplication.cpp
        src/swmmvisprojectwindow.cpp
        src/swmmvissplashscreen.cpp

        # Core
        src/core/measurementunitmanager.cpp
        src/core/openswmmvislogmessage.cpp
        src/core/unitsystem.cpp
        src/core/crsreproject.cpp
        src/core/editgeometry.cpp

        # Selection
        src/selection/selectionmanager.cpp

        # Layers
        src/layers/openswmmvislayer.cpp
        src/layers/gisvectorlayer.cpp
        src/layers/gisrasterlayer.cpp
        src/layers/wmslayer.cpp
        src/layers/wmtslayer.cpp
        src/layers/swmmmodellayer.cpp
        src/layers/swmmresultslayer.cpp
        src/layers/xyztilelayer.cpp

        # Map / GIS
        src/map/spatialreferencesystem.cpp
        src/map/mapextent.cpp
        src/map/crsmanager.cpp
        src/map/mapcanvas.cpp
        src/map/maprenderjob.cpp
        src/map/mapundostack.cpp
        src/map/openswmmvisgraphicsview.cpp
        src/map/openswmmvisscene.cpp
        src/map/graphicsitems.cpp

        # Map tools
        src/map/tools/maptool.cpp
        src/map/tools/maptoolpan.cpp
        src/map/tools/maptoolzoom.cpp
        src/map/tools/maptoolmeasure.cpp
        src/map/tools/maptoolselect.cpp
        src/map/tools/maptoolidentify.cpp
        src/map/tools/maptoolmovenode.cpp
        src/map/tools/maptooleditvertex.cpp
        src/map/tools/maptooladdnode.cpp

        # Project / workspace
        src/project/openswmmvisworkspace.cpp
        src/project/openswmmvisworkspacemodel.cpp
        src/project/openswmmvissession.cpp

        # Simulation
        src/simulation/simulationrunner.cpp
        src/simulation/simulationstatusmodel.cpp

        # UI panels
        src/ui/panels/attributepanel.cpp
        src/ui/panels/layertreepanel.cpp
        src/ui/panels/objectbrowserpanel.cpp

        # Dialogs
        src/ui/dialogs/crsselectiondialog.cpp
        src/ui/dialogs/crschangedialog.cpp
        src/ui/dialogs/aboutdialog.cpp
        src/ui/dialogs/layerpropertiesdialog.cpp
        src/ui/dialogs/simulationoptionsdialog.cpp
        src/ui/dialogs/simulationoptionshelpers.cpp
        src/ui/dialogs/timeseriesplotdialog.cpp
        src/ui/dialogs/wmsconnectiondialog.cpp
        src/ui/dialogs/wmtsconnectiondialog.cpp

        # ── Forms ────────────────────────────────────────────────────────────
        forms/swmmvis.ui

        # ── Translation ──────────────────────────────────────────────────────
        ${TS_FILES}

        # ── Resources ────────────────────────────────────────────────────────
        ${app_icon_macos}
        ${app_icon_resource_windows}
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(SWMMVis
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        resources/swmmvis.qrc
        resources/about.qrc
    )
    qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
else()
    if(ANDROID)
        add_library(SWMMVis SHARED ${PROJECT_SOURCES})
    else()
        add_executable(SWMMVis ${PROJECT_SOURCES})
    endif()
    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()

target_include_directories(SWMMVis
    PRIVATE include
    PRIVATE forms
    PRIVATE ${GDAL_INCLUDE_DIRS}
)

target_link_libraries(SWMMVis
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::OpenGL
        Qt${QT_VERSION_MAJOR}::OpenGLWidgets
        Qt${QT_VERSION_MAJOR}::Network
        Qt${QT_VERSION_MAJOR}::Concurrent
        Qt${QT_VERSION_MAJOR}::Svg
        Qt${QT_VERSION_MAJOR}::Charts
        GDAL::GDAL
        $<$<TARGET_EXISTS:QPropertyModel>:QPropertyModel>
        openswmm_engine
)

set_target_properties(SWMMVis PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER org.openswmm.swmmvis
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

install(TARGETS SWMMVis
    BUNDLE  DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(SWMMVis)
endif()

# ── Unit Tests ──────────────────────────────────────────────────────────────
if(SWMMVIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Doxygen ─────────────────────────────────────────────────────────────────
find_package(Doxygen OPTIONAL_COMPONENTS dot)
if(DOXYGEN_FOUND)
    add_custom_target(doc
        COMMAND ${DOXYGEN_EXECUTABLE} ${PROJECT_SOURCE_DIR}/docs/Doxyfile
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
endif()
