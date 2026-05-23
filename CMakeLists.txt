#
# CMakeLists.txt — OpenSWMM GUI
#
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.21)

include(GNUInstallDirs)

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

if(WIN32)
    configure_file(
        ${PROJECT_SOURCE_DIR}/resources/swmmvis.rc.in
        ${PROJECT_SOURCE_DIR}/resources/swmmvis.rc
        @ONLY
    )
endif()

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS forms)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SWMMVIS_BUILD_TESTS "Build unit tests" ON)

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
    COMPONENTS Widgets OpenGL OpenGLWidgets Network Concurrent LinguistTools Svg
               Quick QuickWidgets Qml)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED
    COMPONENTS Widgets OpenGL OpenGLWidgets Network Concurrent LinguistTools Svg Charts
               Quick QuickWidgets Qml)

# ── GDAL (GIS/CRS) ──────────────────────────────────────────────────────────
find_package(GDAL REQUIRED)

# ── OpenSSL (AES-256 encryption for stored credentials) ─────────────────────
find_package(OpenSSL REQUIRED)

# ── nanoflann (header-only KD-tree for spatial queries) ─────────────────────
find_package(nanoflann CONFIG REQUIRED)

# ── QPropertyModel (sibling checkout, else GitHub fetch) ────────────────────
include(FetchQPropertyModel)

# ── openswmm.engine (hard dependency — sibling checkout or GitHub fetch) ─────
include(FetchOpenSWMMEngine)

# ── Triangle (Shewchuk) — vendored at vendor/triangle/. Compiled as a small
#    static C library and linked into SWMMVis. See Slice AU. License is
#    Shewchuk's research/institutional-free terms — already documented at
#    resources/licenses/triangle.txt and resources/about/components.json.
add_library(triangle_lib STATIC vendor/triangle/triangle.c)
target_include_directories(triangle_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/triangle)
target_compile_definitions(triangle_lib PRIVATE
    TRILIBRARY              # exclude main(), CLI, file I/O — library mode only.
    ANSI_DECLARATORS        # K&R → ANSI prototypes for clean C99 compile.
    NO_TIMER                # drop getrusage()/gettimeofday() dep.
)
# Triangle pre-dates C99 strictness; quiet the legitimate-but-noisy warnings
# that come with it (Shewchuk's predicates use clever tricks the compiler
# rightly flags). We don't want to modify the upstream source (license).
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU|AppleClang")
    target_compile_options(triangle_lib PRIVATE
        -Wno-unused-but-set-variable
        -Wno-unused-result
        -Wno-implicit-fallthrough
        -Wno-deprecated-non-prototype
        -Wno-format-overflow
        -Wno-unused-function
        -Wno-strict-prototypes
    )
endif()
set_target_properties(triangle_lib PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

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
        include/core/queryparser.h
        include/core/preferencesmanager.h
        include/core/linkrenderingprefs.h
        include/core/selectionrenderingprefs.h
        include/core/unitsystem.h
        include/core/crsreproject.h
        include/core/editgeometry.h
        include/core/gisdatapaths.h
        include/selection/selectionmanager.h

        # ── Animation ────────────────────────────────────────────────────────
        include/animation/animationcontroller.h

        # ── Render ───────────────────────────────────────────────────────────
        include/render/categoricalpalette.h
        # Slice BI Phase 8.13.6.1 — IFeatureRenderer abstraction + types.
        include/render/featureref.h
        include/render/symbollayer.h
        include/render/symbolstyle.h
        include/render/legendsymbolitem.h
        include/render/ifeaturerenderer.h
        # Slice BI Phase 8.13.6.2 — concrete renderer classes.
        # SingleSymbol + Graduated are functional v1; Categorized + RuleBased
        # are stubs (JSON + paint) — UI editors land in Slice BI.3.
        include/render/renderers/singlesymbolrenderer.h
        include/render/renderers/graduatedrenderer.h
        include/render/renderers/categorizedrenderer.h
        include/render/renderers/rulebasedrenderer.h
        # Slice BI Phase 8.13.6.7 — raster-renderer sister interface +
        # SingleBandPseudoColor stub.
        include/render/irasterrenderer.h
        include/render/renderers/singlebandpseudocolorrenderer.h

        # ── Plot ─────────────────────────────────────────────────────────────
        include/plot/profilerouter.h
        include/plot/profilebuilder.h
        include/plot/profilenetworkadapter.h
        include/plot/profileplotwidget.h
        include/plot/profileplotoptions.h
        include/plot/profilesourcefetcher.h
        include/plot/plotattribute.h
        include/plot/chartproperties.h
        include/plot/seriesstatistics.h
        include/plot/swmmjuliandatetime.h
        include/plot/seriesstyle.h
        include/plot/fitmetrics.h
        include/plot/irunlayer.h
        include/plot/comparisonplotmodel.h
        include/plot/swmmoutrunlayer.h
        include/plot/mesh2drunlayer.h
        include/plot/observedcsvrunlayer.h

        # ── Layers ───────────────────────────────────────────────────────────
        include/layers/openswmmvislayer.h
        include/layers/gisvectorlayer.h
        include/layers/gisrasterlayer.h
        include/layers/wcslayer.h
        include/layers/wmslayer.h
        include/layers/wmtslayer.h
        include/layers/swmmmodellayer.h
        include/layers/swmmresultslayer.h
        include/layers/tabulardatalayer.h
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
        include/map/scalebarsettings.h
        include/map/snapengine.h
        include/map/swmmlayeritem.h
        include/map/swmmlayerglrenderer.h
        include/map/swmmlayerqsgrenderer.h
        include/map/swmm2dmeshqsgrenderer.h
        include/map/profilepathoverlay.h

        # ── Map tools ────────────────────────────────────────────────────────
        include/map/tools/maptool.h
        include/map/tools/maptoolpan.h
        include/map/tools/maptoolzoom.h
        include/map/tools/maptoolmeasure.h
        include/map/tools/maptoolselect.h
        include/map/tools/maptoolplotpick.h
        include/map/tools/maptoolpick2dcells.h
        include/map/tools/maptoolidentify.h
        include/map/tools/maptooladdnode.h
        include/map/tools/maptooladdlink.h
        include/map/tools/maptooladdgage.h
        include/map/tools/maptooladdsubcatchment.h
        include/map/tools/maptoolselectprofile.h

        # ── Project / workspace ──────────────────────────────────────────────
        include/project/openswmmvisworkspace.h
        include/project/openswmmvisworkspacemodel.h
        include/project/openswmmvissession.h
        include/project/projectserializer.h
        include/project/swmmvisproject.h

        # ── Plugins (engine plugin SDK bridge) ───────────────────────────────
        include/layers/swmm2dmeshlayer.h
        include/layers/swmm2dresultslayer.h
        include/io/mesh2dh5reader.h
        include/io/rptparser.h
        include/mesh/dtmsampler.h
        include/mesh/dtmthinner.h
        include/mesh/inpmeshreader.h
        include/mesh/inpmeshwriter.h
        include/mesh/meshgenerator.h
        include/mesh/meshresult.h
        include/plugins/filefilterregistry.h

        # ── Simulation ───────────────────────────────────────────────────────
        include/simulation/simulationrunner.h
        include/simulation/simulationstatusmodel.h

        # ── UI panels ────────────────────────────────────────────────────────
        include/ui/panels/attributedelegates.h
        include/ui/panels/attributepanel.h
        include/ui/panels/attributetablepanel.h
        include/ui/panels/layertreepanel.h
        include/ui/panels/objectbrowserpanel.h
        include/ui/panels/swmmattributetablemodel.h
        include/ui/panels/swmmobjecttreemodel.h
        include/ui/panels/tabulardatatablemodel.h
        include/ui/panels/overviewmappanel.h

        # ── UI property adapters (Slice Z.5.3 / AG.3) ────────────────────────
        include/ui/properties/swmmlinkpropertyadapter.h
        include/ui/properties/swmmnodepropertyadapter.h
        include/ui/properties/swmmsubcatchpropertyadapter.h

        # ── Compound editor cell widget (Slice DB.2) ─────────────────────────
        include/ui/properties/nodecompoundeditref.h
        include/ui/properties/nodecompoundeditbutton.h

        # ── Non-spatial Data Object property adapters (Slice DA.2) ──────────
        include/ui/properties/swmmdataobjectpropertyadapter.h
        include/ui/properties/swmmaquiferpropertyadapter.h
        include/ui/properties/swmmcontrolrulepropertyadapter.h
        include/ui/properties/swmmcurvepropertyadapter.h
        include/ui/properties/swmmhydrographpropertyadapter.h
        include/ui/properties/swmminletpropertyadapter.h
        include/ui/properties/swmmlandusepropertyadapter.h
        include/ui/properties/swmmlidcontrolpropertyadapter.h
        include/ui/properties/swmmpatternpropertyadapter.h
        include/ui/properties/swmmpollutantpropertyadapter.h
        include/ui/properties/swmmraingagepropertyadapter.h
        include/ui/properties/swmmsnowpackpropertyadapter.h
        include/ui/properties/swmmstreetpropertyadapter.h
        include/ui/properties/swmmtimeseriespropertyadapter.h
        include/ui/properties/swmmtransectpropertyadapter.h

        # ── Property editor framework (Slice BM) ─────────────────────────────
        include/ui/properties/ipropertyeditor.h
        include/ui/properties/propertyeditorregistry.h

        # ── Connections ──────────────────────────────────────────────────────
        include/connections/basemapconnection.h
        include/connections/basemapconnectionstore.h

        # ── UI toolbars ──────────────────────────────────────────────────────
        include/ui/toolbars/terraintoolbar.h

        # ── UI widgets ───────────────────────────────────────────────────────
        include/ui/widgets/basemaphttpheaderswidget.h
        include/ui/widgets/profilelayerpanel.h
        include/ui/widgets/seriesstyleeditor.h
        include/ui/widgets/perattributethemingwidget.h
        include/ui/widgets/legendoverlay.h
        include/ui/widgets/interactivechartview.h
        include/ui/widgets/attributepickermenu.h
        include/ui/widgets/rangeslider.h
        include/ui/widgets/statssummarypanel.h
        include/ui/widgets/labeledcontrols.h

        # ── Dialogs ──────────────────────────────────────────────────────────
        include/ui/dialogs/addbasemapdialog.h
        include/ui/dialogs/crsselectiondialog.h
        include/ui/dialogs/crschangedialog.h
        include/ui/dialogs/aboutdialog.h
        include/ui/dialogs/layerpropertiesdialog.h
        include/ui/dialogs/meshgenerationdialog.h
        include/ui/dialogs/newdataobjectdialog.h
        include/ui/dialogs/nodecompoundeditdialog.h
        include/ui/dialogs/newprojectdialog.h
        include/ui/dialogs/pluginsdialog.h
        include/ui/dialogs/preferencesdialog.h
        include/ui/dialogs/simulationoptionsdialog.h
        include/ui/dialogs/hotstartsavesmodel.h
        include/ui/dialogs/pathbrowsedelegate.h
        include/ui/dialogs/pluginstablemodel.h
        include/ui/dialogs/timeseriesplotdialog.h
        include/ui/dialogs/comparisonplotdialog.h
        include/ui/dialogs/chartpropertiesdialog.h
        include/ui/dialogs/colorrampeditordialog.h
        include/ui/dialogs/statisticsdashboarddialog.h
        include/ui/dialogs/statusreportdialog.h
        include/ui/dialogs/tabularresultsdialog.h
        include/ui/dialogs/scatterplotdialog.h
        include/ui/dialogs/customreportdialog.h
        include/ui/dialogs/symbologydialog.h
        include/ui/dialogs/calibrationdatadialog.h
        include/ui/dialogs/profilepathpickerdialog.h
        include/ui/dialogs/profileplotdialog.h
        include/ui/dialogs/profileoptionsdialog.h
        include/ui/dialogs/profilesourcestyleadapter.h
        include/ui/dialogs/wmsconnectiondialog.h
        include/ui/dialogs/wmtsconnectiondialog.h
        include/ui/dialogs/licenseagreementdialog.h

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
        src/core/preferencesmanager.cpp
        src/core/linkrenderingprefs.cpp
        src/core/selectionrenderingprefs.cpp
        src/core/queryparser.cpp
        src/core/unitsystem.cpp
        src/core/crsreproject.cpp
        src/core/editgeometry.cpp
        src/core/gisdatapaths.cpp
        src/core/basemapcrypto.cpp

        # Connections
        src/connections/basemapconnectionstore.cpp

        # Selection
        src/selection/selectionmanager.cpp

        # Animation
        src/animation/animationcontroller.cpp

        # Render
        src/render/categoricalpalette.cpp
        # Slice BI Phase 8.13.6.1 — JSON round-trip impls for the new types.
        src/render/symbollayer.cpp
        src/render/symbolstyle.cpp
        src/render/legendsymbolitem.cpp
        # Slice BI Phase 8.13.6.2 — concrete renderer impls.
        src/render/renderers/singlesymbolrenderer.cpp
        src/render/renderers/graduatedrenderer.cpp
        src/render/renderers/categorizedrenderer.cpp
        src/render/renderers/rulebasedrenderer.cpp
        # Slice BI Phase 8.13.6.7 — raster-renderer stub impl.
        src/render/renderers/singlebandpseudocolorrenderer.cpp
        # Slice BI-MK.1 Phase 8.13.40 — per-kind renderer dispatch adapter.
        src/render/multikindrenderer.cpp

        # Plot
        src/plot/profilerouter.cpp
        src/plot/profilebuilder.cpp
        src/plot/profilenetworkadapter.cpp
        src/plot/profilenetworkadapter_model.cpp
        src/plot/profileplotwidget.cpp
        src/plot/profileplotoptions.cpp
        src/plot/profilesourcefetcher.cpp
        src/plot/plotattribute.cpp
        src/plot/chartproperties.cpp
        src/plot/seriesstatistics.cpp
        src/plot/seriesstyle.cpp
        src/plot/fitmetrics.cpp
        src/plot/comparisonplotmodel.cpp
        src/plot/swmmoutrunlayer.cpp
        src/plot/swmmoutrunlayer_codes.cpp
        src/plot/mesh2drunlayer.cpp
        src/plot/observedcsvrunlayer.cpp

        # Layers
        src/layers/openswmmvislayer.cpp
        src/layers/gisvectorlayer.cpp
        src/layers/gisrasterlayer.cpp
        src/layers/wcslayer.cpp
        src/layers/wmslayer.cpp
        src/layers/wmtslayer.cpp
        src/layers/swmmmodellayer.cpp
        src/layers/swmmresultslayer.cpp
        src/layers/tabulardatalayer.cpp
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
        src/map/scalebarsettings.cpp
        src/map/snapengine.cpp
        src/map/swmmlayeritem.cpp
        src/map/swmmlayerglrenderer.cpp
        src/map/swmmlayerqsgrenderer.cpp
        src/map/swmm2dmeshqsgrenderer.cpp
        src/map/profilepathoverlay.cpp

        # Map tools
        src/map/tools/maptool.cpp
        src/map/tools/maptoolpan.cpp
        src/map/tools/maptoolzoom.cpp
        src/map/tools/maptoolmeasure.cpp
        src/map/tools/maptoolselect.cpp
        src/map/tools/maptoolplotpick.cpp
        src/map/tools/maptoolpick2dcells.cpp
        src/map/tools/maptoolidentify.cpp
        src/map/tools/maptooladdnode.cpp
        src/map/tools/maptooladdlink.cpp
        src/map/tools/maptooladdgage.cpp
        src/map/tools/maptooladdsubcatchment.cpp
        src/map/tools/maptoolselectprofile.cpp

        # Project / workspace
        src/project/openswmmvisworkspace.cpp
        src/project/openswmmvisworkspacemodel.cpp
        src/project/openswmmvissession.cpp
        src/project/projectserializer.cpp
        src/project/swmmvisproject.cpp

        # Plugins (engine plugin SDK bridge)
        src/layers/swmm2dmeshlayer.cpp
        src/layers/swmm2dresultslayer.cpp
        src/io/mesh2dh5reader.cpp
        src/io/rptparser.cpp
        src/mesh/dtmsampler.cpp
        src/mesh/dtmthinner.cpp
        src/mesh/inpmeshreader.cpp
        src/mesh/inpmeshwriter.cpp
        src/mesh/meshgenerator.cpp
        src/plugins/filefilterregistry.cpp

        # Simulation
        src/simulation/simulationrunner.cpp
        src/simulation/simulationstatusmodel.cpp

        # UI panels
        src/ui/panels/attributedelegates.cpp
        src/ui/panels/attributepanel.cpp
        src/ui/panels/attributetablepanel.cpp
        src/ui/panels/layertreepanel.cpp
        src/ui/panels/objectbrowserpanel.cpp
        src/ui/panels/overviewmappanel.cpp
        src/ui/panels/swmmattributetablemodel.cpp
        src/ui/panels/swmmobjecttreemodel.cpp
        src/ui/panels/tabulardatatablemodel.cpp

        # UI property adapters
        src/ui/properties/swmmlinkpropertyadapter.cpp
        src/ui/properties/swmmnodepropertyadapter.cpp
        src/ui/properties/swmmsubcatchpropertyadapter.cpp

        # Compound editor cell widget (Slice DB.2)
        src/ui/properties/nodecompoundeditref.cpp
        src/ui/properties/nodecompoundeditbutton.cpp

        # Non-spatial Data Object adapters (Slice DA.2)
        src/ui/properties/swmmdataobjectpropertyadapter.cpp
        src/ui/properties/swmmaquiferpropertyadapter.cpp
        src/ui/properties/swmmcontrolrulepropertyadapter.cpp
        src/ui/properties/swmmcurvepropertyadapter.cpp
        src/ui/properties/swmmhydrographpropertyadapter.cpp
        src/ui/properties/swmminletpropertyadapter.cpp
        src/ui/properties/swmmlandusepropertyadapter.cpp
        src/ui/properties/swmmlidcontrolpropertyadapter.cpp
        src/ui/properties/swmmpatternpropertyadapter.cpp
        src/ui/properties/swmmpollutantpropertyadapter.cpp
        src/ui/properties/swmmraingagepropertyadapter.cpp
        src/ui/properties/swmmsnowpackpropertyadapter.cpp
        src/ui/properties/swmmstreetpropertyadapter.cpp
        src/ui/properties/swmmtimeseriespropertyadapter.cpp
        src/ui/properties/swmmtransectpropertyadapter.cpp

        # Property editor framework (Slice BM)
        src/ui/properties/propertyeditorregistry.cpp

        # Dialogs
        src/ui/dialogs/crsselectiondialog.cpp
        src/ui/dialogs/crschangedialog.cpp
        src/ui/dialogs/aboutdialog.cpp
        src/ui/dialogs/layerpropertiesdialog.cpp
        src/ui/dialogs/meshgenerationdialog.cpp
        src/ui/dialogs/newdataobjectdialog.cpp
        src/ui/dialogs/nodecompoundeditdialog.cpp
        src/ui/dialogs/newprojectdialog.cpp
        src/ui/dialogs/pluginsdialog.cpp
        src/ui/dialogs/preferencesdialog.cpp
        src/ui/dialogs/simulationoptionsdialog.cpp
        src/ui/dialogs/simulationoptionshelpers.cpp
        src/ui/dialogs/hotstartsavesmodel.cpp
        src/ui/dialogs/pathbrowsedelegate.cpp
        src/ui/dialogs/pluginstablemodel.cpp
        src/ui/dialogs/timeseriesplotdialog.cpp
        src/ui/dialogs/comparisonplotdialog.cpp
        src/ui/dialogs/chartpropertiesdialog.cpp
        src/ui/dialogs/colorrampeditordialog.cpp
        src/ui/dialogs/statisticsdashboarddialog.cpp
        src/ui/dialogs/statusreportdialog.cpp
        src/ui/dialogs/tabularresultsdialog.cpp
        src/ui/dialogs/scatterplotdialog.cpp
        src/ui/dialogs/customreportdialog.cpp
        src/ui/dialogs/symbologydialog.cpp
        src/ui/dialogs/calibrationdatadialog.cpp
        src/ui/dialogs/profilepathpickerdialog.cpp
        src/ui/dialogs/profileplotdialog.cpp
        src/ui/dialogs/profileoptionsdialog.cpp
        src/ui/dialogs/profilesourcestyleadapter.cpp
        src/ui/dialogs/wmsconnectiondialog.cpp
        src/ui/dialogs/wmtsconnectiondialog.cpp
        src/ui/dialogs/addbasemapdialog.cpp
        src/ui/dialogs/licenseagreementdialog.cpp

        # Toolbars
        src/ui/toolbars/terraintoolbar.cpp

        # Widgets
        src/ui/widgets/basemaphttpheaderswidget.cpp
        src/ui/widgets/profilelayerpanel.cpp
        src/ui/widgets/seriesstyleeditor.cpp
        src/ui/widgets/perattributethemingwidget.cpp
        src/ui/widgets/legendoverlay.cpp
        src/ui/widgets/interactivechartview.cpp
        src/ui/widgets/attributepickermenu.cpp
        src/ui/widgets/rangeslider.cpp
        src/ui/widgets/statssummarypanel.cpp
        src/ui/widgets/labeledcontrols.cpp

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
    # legacy_version.h is generated into the legacy engine's build dir;
    # expose it so swmmvis.cpp can use LEGACY_SWMM_VERSION macros.
    PRIVATE "${CMAKE_BINARY_DIR}/openswmm_engine/src/legacy/engine"
)

find_package(HDF5 REQUIRED COMPONENTS C)
target_include_directories(SWMMVis PRIVATE ${HDF5_INCLUDE_DIRS})

target_link_libraries(SWMMVis
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::OpenGL
        Qt${QT_VERSION_MAJOR}::OpenGLWidgets
        Qt${QT_VERSION_MAJOR}::Network
        Qt${QT_VERSION_MAJOR}::Concurrent
        Qt${QT_VERSION_MAJOR}::Svg
        Qt${QT_VERSION_MAJOR}::Charts
        Qt${QT_VERSION_MAJOR}::Qml
        Qt${QT_VERSION_MAJOR}::Quick
        Qt${QT_VERSION_MAJOR}::QuickWidgets
        GDAL::GDAL
        OpenSSL::SSL
        OpenSSL::Crypto
        nanoflann::nanoflann
        $<$<TARGET_EXISTS:QPropertyModel>:QPropertyModel>
        openswmm_engine
        triangle_lib
        ${HDF5_C_LIBRARIES}
)

set_target_properties(SWMMVis PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER org.openswmm.swmmvis
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE_COPYRIGHT "Copyright (C) 2026 Caleb Buahin. Licensed under GPLv3."
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# ── Portable GDAL / PROJ data (vcpkg) ───────────────────────────────────────
# Copy the vcpkg-installed share trees into the SWMMVis output so
# setupBundledGisDataPaths() can find them without a system install.
#   - macOS .app bundle: SWMMVis.app/Contents/Resources/{gdal,proj}/
#   - Linux / Windows  : <exe-dir>/{gdal,proj}/
# See docs/PORTABLE_GDAL_PROJ.md for the deployment rationale.
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(_SWMMVIS_GDAL_SHARE "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/gdal")
    set(_SWMMVIS_PROJ_SHARE "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/proj")
    if(EXISTS "${_SWMMVIS_GDAL_SHARE}" AND EXISTS "${_SWMMVIS_PROJ_SHARE}")
        if(APPLE)
            # POST_BUILD: place data in Contents/Resources/; macdeployqt preserves them.
            set(_SWMMVIS_GDAL_DEST "$<TARGET_BUNDLE_CONTENT_DIR:SWMMVis>/Resources/gdal")
            set(_SWMMVIS_PROJ_DEST "$<TARGET_BUNDLE_CONTENT_DIR:SWMMVis>/Resources/proj")
            add_custom_command(TARGET SWMMVis POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${_SWMMVIS_GDAL_SHARE}" "${_SWMMVIS_GDAL_DEST}"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${_SWMMVIS_PROJ_SHARE}" "${_SWMMVIS_PROJ_DEST}"
                COMMENT "Copy GDAL/PROJ share data into .app bundle Resources (portable)"
                VERBATIM
            )
            # install(): the whole bundle (incl. Resources) is installed via install(BUNDLE)
            # in the deployment block below — no extra rule needed here for macOS.
        else()
            set(_SWMMVIS_GDAL_DEST "$<TARGET_FILE_DIR:SWMMVis>/gdal")
            set(_SWMMVIS_PROJ_DEST "$<TARGET_FILE_DIR:SWMMVis>/proj")
            add_custom_command(TARGET SWMMVis POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${_SWMMVIS_GDAL_SHARE}" "${_SWMMVIS_GDAL_DEST}"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${_SWMMVIS_PROJ_SHARE}" "${_SWMMVIS_PROJ_DEST}"
                COMMENT "Copy GDAL/PROJ share data alongside SWMMVis (portable)"
                VERBATIM
            )
            # install(): mirror the same share data into the install tree so
            # CPack picks them up. Place beside the executable (BINDIR).
            install(DIRECTORY "${_SWMMVIS_GDAL_SHARE}"
                DESTINATION ${CMAKE_INSTALL_BINDIR}/gdal
                COMPONENT Runtime
            )
            install(DIRECTORY "${_SWMMVIS_PROJ_SHARE}"
                DESTINATION ${CMAKE_INSTALL_BINDIR}/proj
                COMPONENT Runtime
            )
        endif()
    endif()
endif()

# ── Qt and GDAL runtime deployment ─────────────────────────────────────────
# POST_BUILD: makes the build-tree binary runnable without system-installed deps.
# INSTALL:    cmake --install creates a self-contained staging tree for CPack.
#
# macOS  : macdeployqt bundles Qt frameworks + all linked dylibs (GDAL and all
#          transitive deps) into the .app bundle. Runs at POST_BUILD and again
#          at install time on the freshly-installed bundle.
# Windows: windeployqt copies Qt DLLs (POST_BUILD + install); the entire vcpkg
#          bin/ is copied for GDAL and every transitive DLL; openswmm_engine.dll
#          is copied explicitly (it is built by FetchContent, not vcpkg).
# Linux  : vcpkg lib/*.so* are copied next to the executable via a cmake -P
#          helper that follows symlink chains; openswmm_engine.so is copied
#          explicitly; RPATH is set to $ORIGIN so the loader finds them.

if(APPLE)
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt
        HINTS "${Qt6_DIR}/../../../bin"
              "${QT_ROOT_DIR}/bin"
              "$ENV{QT_ROOT_DIR}/bin"
    )
    if(MACDEPLOYQT_EXECUTABLE)
        # POST_BUILD: produce a self-contained .app in the build tree
        add_custom_command(TARGET SWMMVis POST_BUILD
            COMMAND "${MACDEPLOYQT_EXECUTABLE}"
                    "$<TARGET_BUNDLE_DIR:SWMMVis>"
                    -always-overwrite
            COMMENT "macdeployqt: bundling Qt frameworks and linked dylibs (incl. GDAL)"
            VERBATIM
        )
    else()
        message(WARNING
            "macdeployqt not found — Qt frameworks will not be bundled. "
            "Set QT_ROOT_DIR or add the Qt bin directory to PATH.")
    endif()

    # Ad-hoc re-sign after every build. Without this, when CMake
    # rebuilds a dependent dylib (engine, QPropertyModel, …) macOS's
    # dyld rejects the .app at launch with `EXC_BAD_ACCESS / SIGKILL
    # (Code Signature Invalid)` because the embedded code-directory
    # hash no longer matches the now-modified linked binary. The
    # `--deep --force` recurses into every nested .framework / .dylib
    # under Contents/. Empty signing identity (`-`) is the macOS
    # "ad-hoc" signature — fine for local development; release builds
    # should swap in a real Developer ID via -DCODESIGN_IDENTITY=…
    set(_codesign_identity "-")
    if(DEFINED CODESIGN_IDENTITY AND NOT "${CODESIGN_IDENTITY}" STREQUAL "")
        set(_codesign_identity "${CODESIGN_IDENTITY}")
    endif()
    add_custom_command(TARGET SWMMVis POST_BUILD
        COMMAND codesign --force --sign "${_codesign_identity}" --deep
                "$<TARGET_BUNDLE_DIR:SWMMVis>"
        COMMENT "codesign: re-signing bundle (identity=${_codesign_identity})"
        VERBATIM
    )

    install(TARGETS SWMMVis BUNDLE DESTINATION .)

    # INSTALL: macdeployqt must run again on the freshly-installed bundle because
    # cmake --install copies a clean bundle (without the POST_BUILD modifications).
    if(MACDEPLOYQT_EXECUTABLE)
        install(CODE
            "execute_process(COMMAND
                \"${MACDEPLOYQT_EXECUTABLE}\"
                \"\${CMAKE_INSTALL_PREFIX}/SWMMVis.app\"
                -always-overwrite)"
            COMPONENT Runtime
        )
    endif()

    # Re-sign after install-time macdeployqt. macdeployqt embeds new dylibs (Qt
    # frameworks, GDAL, PROJ and all transitive deps) which invalidates the bundle's
    # code directory. --options runtime enables the Hardened Runtime required for
    # notarization; --timestamp adds a secure timestamp so the signature survives
    # certificate expiry. Use the same identity that was set at configure time via
    # -DCODESIGN_IDENTITY (defaults to "-" for ad-hoc local builds).
    install(CODE
        "execute_process(COMMAND
            codesign --force --sign \"${_codesign_identity}\"
            --options runtime --timestamp
            \"\${CMAKE_INSTALL_PREFIX}/SWMMVis.app\")"
        COMPONENT Runtime
    )

elseif(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
        HINTS "${Qt6_DIR}/../../../bin"
              "${QT_ROOT_DIR}/bin"
              "$ENV{QT_ROOT_DIR}/bin"
    )
    if(WINDEPLOYQT_EXECUTABLE)
        # POST_BUILD: copy Qt DLLs and plugins to the build output dir
        add_custom_command(TARGET SWMMVis POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-compiler-runtime
                    "$<TARGET_FILE:SWMMVis>"
            COMMENT "windeployqt: copying Qt DLLs alongside SWMMVis.exe"
            VERBATIM
        )
    else()
        message(WARNING
            "windeployqt not found — Qt DLLs will not be copied. "
            "Set QT_ROOT_DIR or add the Qt bin directory to PATH.")
    endif()

    # Copy all vcpkg runtime DLLs: GDAL, PROJ, GEOS, libcurl, and every other
    # transitive dependency. vcpkg places only runtime DLLs in <triplet>/bin/.
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        set(_vcpkg_bin "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
        add_custom_command(TARGET SWMMVis POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${_vcpkg_bin}" "$<TARGET_FILE_DIR:SWMMVis>"
            COMMENT "Copying vcpkg DLLs (GDAL and all transitive deps) alongside SWMMVis.exe"
            VERBATIM
        )
    endif()

    # openswmm_engine is a SHARED lib built via FetchContent — not in vcpkg bin/.
    add_custom_command(TARGET SWMMVis POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:openswmm_engine>"
                "$<TARGET_FILE_DIR:SWMMVis>/"
        COMMENT "Copy openswmm_engine.dll alongside SWMMVis.exe"
        VERBATIM
    )

    install(TARGETS SWMMVis     RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(TARGETS openswmm_engine RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        install(DIRECTORY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/"
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            FILES_MATCHING PATTERN "*.dll"
        )
    endif()

    if(WINDEPLOYQT_EXECUTABLE)
        install(CODE
            "execute_process(COMMAND
                \"${WINDEPLOYQT_EXECUTABLE}\"
                --no-compiler-runtime
                \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/SWMMVis.exe\")"
            COMPONENT Runtime
        )
    endif()

else()
    # Linux
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        set(_vcpkg_lib "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")

        # POST_BUILD: copy vcpkg .so files; the helper follows symlink chains so
        # physical files (not dangling absolute-path symlinks) land in the build dir.
        add_custom_command(TARGET SWMMVis POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                    "-DVCPKG_LIB_DIR=${_vcpkg_lib}"
                    "-DOUTPUT_DIR=$<TARGET_FILE_DIR:SWMMVis>"
                    -P "${CMAKE_SOURCE_DIR}/cmake/CopyVcpkgLibs.cmake"
            COMMENT "Copying vcpkg shared libraries (GDAL and all transitive deps)"
            VERBATIM
        )

        # INSTALL: same set of .so files into the install lib dir
        install(CODE
            "file(GLOB _so_files LIST_DIRECTORIES false
                \"${_vcpkg_lib}/*.so\" \"${_vcpkg_lib}/*.so.*\")
            foreach(_lib IN LISTS _so_files)
                file(INSTALL \"\${_lib}\"
                    DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}\"
                    FOLLOW_SYMLINK_CHAIN)
            endforeach()"
            COMPONENT Runtime
        )
    endif()

    # openswmm_engine is a SHARED lib built via FetchContent — copy explicitly.
    add_custom_command(TARGET SWMMVis POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:openswmm_engine>"
                "$<TARGET_FILE_DIR:SWMMVis>/"
        COMMENT "Copy openswmm_engine shared library alongside SWMMVis"
        VERBATIM
    )

    set_target_properties(SWMMVis PROPERTIES
        BUILD_RPATH   "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )

    # Qt plugins: Qt needs a platforms/ plugin to show any window at all, plus
    # imageformats/ (SVG), xcbglintegrations/ (OpenGL on X11), and styles/.
    # On Linux there is no deploy tool equivalent to macdeployqt/windeployqt,
    # so we locate and copy the required plugin directories manually.
    get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
    if(_qt_qmake)
        execute_process(
            COMMAND "${_qt_qmake}" -query QT_INSTALL_PLUGINS
            OUTPUT_VARIABLE _qt_plugins_dir
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(IS_DIRECTORY "${_qt_plugins_dir}")
            foreach(_plugin_subdir IN ITEMS platforms imageformats xcbglintegrations styles)
                if(IS_DIRECTORY "${_qt_plugins_dir}/${_plugin_subdir}")
                    add_custom_command(TARGET SWMMVis POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_directory
                                "${_qt_plugins_dir}/${_plugin_subdir}"
                                "$<TARGET_FILE_DIR:SWMMVis>/plugins/${_plugin_subdir}"
                        COMMENT "Copying Qt plugin directory: ${_plugin_subdir}"
                        VERBATIM
                    )
                    install(DIRECTORY "${_qt_plugins_dir}/${_plugin_subdir}"
                        DESTINATION ${CMAKE_INSTALL_BINDIR}/plugins/${_plugin_subdir}
                        COMPONENT Runtime
                    )
                endif()
            endforeach()

            # qt.conf tells the Qt runtime where to find plugins relative to
            # the executable. Without it Qt looks in the system Qt install dir.
            set(_qt_conf "${CMAKE_CURRENT_BINARY_DIR}/qt.conf")
            file(WRITE "${_qt_conf}" "[Paths]\nPlugins = plugins\n")
            add_custom_command(TARGET SWMMVis POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_qt_conf}" "$<TARGET_FILE_DIR:SWMMVis>/qt.conf"
                COMMENT "Deploy qt.conf (plugins path)"
                VERBATIM
            )
            install(FILES "${_qt_conf}"
                DESTINATION ${CMAKE_INSTALL_BINDIR}
                COMPONENT Runtime
            )
        endif()
        unset(_qt_plugins_dir)
    endif()
    unset(_qt_qmake)

    install(TARGETS SWMMVis     RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(TARGETS openswmm_engine LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

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

# ── Packaging (CPack) ────────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME              "SWMMVis")
set(CPACK_PACKAGE_VENDOR            "Caleb Buahin")
set(CPACK_PACKAGE_VERSION           "${PROJECT_VERSION}${PROJECT_VERSION_SUFFIX}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_RESOURCE_FILE_LICENSE     "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES               TRUE)

if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME   "SWMMVis ${PROJECT_VERSION}")
    # Display GPLv3 text when the DMG is mounted (satisfies license-visibility
    # requirement for binary redistribution). CMake 3.17+ handles the hdiutil
    # SLA resource embedding; this project requires 3.21+ so it is safe.
    set(CPACK_DMG_SLA_FILE      "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_DMG_VOLUME_ICON   "${CMAKE_CURRENT_SOURCE_DIR}/resources/swmmvis.icns")

elseif(WIN32)
    set(CPACK_GENERATOR "NSIS")

    # ── NSIS installer metadata ──────────────────────────────────────────────
    set(CPACK_NSIS_PACKAGE_NAME         "SWMMVis ${PROJECT_VERSION}${PROJECT_VERSION_SUFFIX}")
    set(CPACK_NSIS_DISPLAY_NAME         "SWMMVis ${PROJECT_VERSION}${PROJECT_VERSION_SUFFIX}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "SWMMVis")
    set(CPACK_NSIS_INSTALL_ROOT         "$PROGRAMFILES64")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH          OFF)
    set(CPACK_NSIS_URL_INFO_ABOUT       "https://github.com/hydrocouple/openswmm.gui")
    set(CPACK_NSIS_CONTACT              "calebgh@gmail.com")
    set(CPACK_NSIS_HELP_LINK            "https://hydrocouple.org/openswmm.gui")
    # Path relative to install root, NSIS backslash syntax (double-escaped for CMake)
    set(CPACK_NSIS_INSTALLED_ICON_NAME  "bin\\\\SWMMVis.exe")
    set(CPACK_NSIS_MUI_ICON             "${CMAKE_CURRENT_SOURCE_DIR}/resources/swmmvis.ico")
    set(CPACK_NSIS_MUI_UNIICON          "${CMAKE_CURRENT_SOURCE_DIR}/resources/swmmvis.ico")
    # Raw NSIS script inserted verbatim — backslashes need \\\\ in CMake string
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortcut '$DESKTOP\\\\SWMMVis.lnk' '$INSTDIR\\\\bin\\\\SWMMVis.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$DESKTOP\\\\SWMMVis.lnk'")
    # License page is shown automatically because CPACK_RESOURCE_FILE_LICENSE is set above.

else()
    # TGZ for direct distribution. A portable AppImage is built separately
    # in CI via linuxdeploy (see .github/workflows/build_and_test.yml).
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
