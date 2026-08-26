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

# ---------------------------------------------------------------------------
# Compile cache. The gui test targets each compile their own copies of the
# app sources they list (330+ targets; single TUs recompiled up to ~27x per
# full build), so a content-addressed compile cache collapses the duplicate
# work into cache hits — including across the revert/reapply cycles the
# validation workflow leans on. Opt out with -DSWMMVIS_USE_CCACHE=OFF, or
# by pre-setting CMAKE_<LANG>_COMPILER_LAUNCHER yourself.
# ---------------------------------------------------------------------------
option(SWMMVIS_USE_CCACHE "Route compiles through ccache when available" ON)
if(SWMMVIS_USE_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(SWMMVIS_CCACHE_EXECUTABLE ccache)
    if(SWMMVIS_CCACHE_EXECUTABLE)
        set(CMAKE_C_COMPILER_LAUNCHER   "${SWMMVIS_CCACHE_EXECUTABLE}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${SWMMVIS_CCACHE_EXECUTABLE}")
        message(STATUS "ccache enabled: ${SWMMVIS_CCACHE_EXECUTABLE}")
    endif()
endif()

set(PROJECT_VERSION_SUFFIX "-alpha.3")

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

# Source files are UTF-8. Without /utf-8 MSVC reads them as the system ANSI
# codepage and bakes UTF-8 string literals (×, ², ³, °, …) into the binary as
# mojibake. GCC/Clang already default to UTF-8 for both input and execution
# charset, so the flag is MSVC-only.
if(MSVC)
    add_compile_options(/utf-8)
endif()

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
               Quick QuickWidgets Qml PrintSupport)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED
    COMPONENTS Widgets OpenGL OpenGLWidgets Network Concurrent LinguistTools Svg Charts
               Quick QuickWidgets Qml PrintSupport)
# QSG-2D-1M Phase 8 — qsb compilation for the scalar-fill material shaders.
if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS ShaderTools)
endif()

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
    EXTERNAL_TEST           # triunsuitable() supplied by src/mesh/trirefinehook.cpp
                            # instead of triangle.c's own stub. Upstream-sanctioned
                            # extension point (triangle.c:1363-1390) — this is what
                            # lets us cancel, report progress, and grade element size
                            # during refinement WITHOUT patching the vendored source.
                            # The symbol is left undefined in this static library and
                            # resolved at the final link of SWMMVis.
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
# The handle macros round pointers through an integer; that integer is now
# uintptr_t (was `unsigned long`, which is 32-bit on LLP64 Windows and
# truncated every pointer at or above 4 GiB). C4311/C4312 are MSVC's
# diagnostics for exactly that truncation — leave them ENABLED so a
# regression is caught at compile time rather than as a large-mesh crash.
# The remaining noise is the same pre-C99 strictness quieted above.
if(MSVC)
    target_compile_options(triangle_lib PRIVATE /wd4996 /wd4244 /wd4267)
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
        include/core/noderenderingprefs.h
        include/core/meshbcrenderingprefs.h
        include/core/selectionrenderingprefs.h
        include/core/unitsystem.h
        include/core/loadprogress.h
        include/core/crsreproject.h
        include/core/editgeometry.h
        include/core/gageassignment.h
        include/core/gageblend.h
        include/core/gisdatapaths.h
        include/core/swmmdatetime.h
        include/core/swmmdatetimeformat.h
        include/selection/selectionmanager.h
        include/selection/selectionops.h

        # ── Animation ────────────────────────────────────────────────────────
        include/animation/animationcontroller.h

        # ── Render ───────────────────────────────────────────────────────────
        include/render/categoricalpalette.h
        # Slice BB-α — colour-ramp data type + interval binner.
        include/render/colorramp.h
        include/render/intervalbinner.h
        # Slice US.1 — shared classification scheme (UNIFIED_STYLING plan S1).
        include/render/classificationscheme.h
        # P2 — static/dynamic source + range-mode spine (header-only enums).
        include/render/attributesource.h
        include/render/labelconfig.h
        # Slice US.B3 — shared label painter (UNIFIED_STYLING plan B3).
        include/render/labelpainter.h
        include/render/basemaprenderparams.h
        include/render/stylefileio.h
        include/render/expressionevaluator.h
        # Slice BI Phase 8.13.7-α / 8.13.43-α — typed data-defined scalar.
        include/render/datadefined.h
        # Slice BI Phase 8.13.6.1 — IFeatureRenderer abstraction + types.
        include/render/featureref.h
        include/render/symbollayer.h
        include/render/symbolstyle.h
        include/render/legendsymbolitem.h
        include/render/ifeaturerenderer.h
        # Slice BB Phase 8.6.16 — shared model for on-canvas legend chrome.
        include/render/legendoverlaystyle.h
        # Slice BI Phase 8.13.6.2 — concrete renderer classes.
        # SingleSymbol + Graduated are functional v1; Categorized + RuleBased
        # are stubs (JSON + paint) — UI editors land in Slice BI.3.
        include/render/renderers/singlesymbolrenderer.h
        include/render/renderers/graduatedrenderer.h
        include/render/renderers/categorizedrenderer.h
        include/render/renderers/rulebasedrenderer.h
        # Slice Z.9 — continuous-colour (no-bins) renderer
        # (RENDERING_RULE_MODEL_PLAN.md §5).
        include/render/renderers/unclassedcolorsrenderer.h
        # Slice BI Phase 8.13.6.7 — raster-renderer sister interface +
        # SingleBandPseudoColor stub.
        include/render/irasterrenderer.h
        include/render/renderers/singlebandpseudocolorrenderer.h
        # VS.6 — categorical / paletted raster renderer.
        include/render/renderers/palettedrasterrenderer.h
        # Slice S1 (RENDERING_OUTPUT_SUBLAYERS_PLAN.md) — sublayer foundation:
        # ISublayer interface, SublayerStyle property-bag base, SizeUnit enum,
        # and the screen-pixel inverse-zoom helpers consumed by S2+ sublayers.
        include/render/sizeunit.h
        include/render/screenpixels.h
        include/render/sublayerstyle.h
        include/render/isublayer.h
        # Slice S2.1 — host mixin a layer adopts to expose a sublayer list +
        # the AnimationController dispatch contract (only dynamic sublayers
        # invalidate per tick — RENDERING_OUTPUT_SUBLAYERS_PLAN.md §2 Decision 3).
        include/render/isublayerhost.h
        # Slice U-0 — granular per-Category sublayer base + archetype style
        # bags (Point / Line / Polygon). Replaces the legacy NodeMarker /
        # ConduitLine / ConduitArrow / SubcatchmentFill mix with one
        # FeatureSublayer instance per SWMMModelLayer::Category (11 total).
        include/render/sublayers/feature/featuresublayerstyle.h
        include/render/sublayers/feature/featuresublayer.h
        # Slice S5 — five 2D sublayer classes for the SWMM2DResultsLayer mix.
        include/render/sublayers/meshfillsublayer.h
        include/render/sublayers/meshedgesublayer.h
        include/render/sublayers/meshnodesublayer.h
        include/render/sublayers/velocityvectorsublayer.h
        include/render/sublayers/isolinesublayer.h
        include/render/sublayers/contourbandsublayer.h
        include/render/sublayers/scalarfillsublayer.h
        # Mesh styling restructure — BC indicators + coupled-node markers as
        # first-class sublayers.
        include/render/sublayers/meshbcsublayer.h
        include/render/sublayers/couplednodesublayer.h
        # Slice Z.1 — layer-scope Rule + RuleList (Q_OBJECT classes;
        # AUTOMOC needs the headers in this list).
        include/render/rule.h
        include/render/rulelist.h
        # Slice B.6c — SymbolStyleAdapter (Q_OBJECT — AUTOMOC needs it).
        include/render/symbolstyleadapter.h
        # Slice DM.1 — IAttributeProvider seam (Q_DECLARE_INTERFACE +
        # Q_DECLARE_METATYPE). Header-only; listing keeps it in the
        # project tree for tooling and AUTOMOC interface discovery on
        # layers that Q_INTERFACES it.
        include/render/iattributeprovider.h
        # Slice Z.4 — canonical MarkerShape enum + typed
        # MarkerSymbolLayerSpec (RENDERING_RULE_MODEL_PLAN.md §6.1).
        include/render/markershape.h
        include/render/markersymbollayer.h
        # Slice Z.5 — typed LineSymbolLayerSpec + arrow spec
        # (RENDERING_RULE_MODEL_PLAN.md §6.2).
        include/render/linesymbollayer.h
        # Slice Z.5b — polyline perpendicular offset.
        include/render/polylineoffset.h
        # Slice Z.6 — Raster + TIN Symbol Layer specs
        # (RENDERING_RULE_MODEL_PLAN.md §6.4).
        include/render/rastersymbollayers.h
        # Slice Z.7 — animation bin sampler
        # (RENDERING_RULE_MODEL_PLAN.md §14.1).
        include/render/binsampler.h
        # Slice Z.11 — Symbol Levels paint-order helper
        # (RENDERING_RULE_MODEL_PLAN.md §8).
        include/render/symbollevels.h
        # Slice Z.17-files — native .swmm-rule.json I/O
        # (RENDERING_RULE_MODEL_PLAN.md §12.1).
        include/render/rulelistio.h
        # Slice Z.17b — QGIS .qml import/export for Rule Lists.
        include/render/qmlrulelistio.h
        # Slice Z.13-data — TemporalSpec for the Temporal tab.
        include/render/temporalspec.h
        # Slice Z.14-data — MaskSpec for the Mask tab.
        include/render/maskspec.h
        # Slice Z.14-paint — runtime clip-path resolver.
        include/render/maskclipresolver.h
        # Slice Z.15-data — AuxiliaryStorageSpec for the Auxiliary Storage tab.
        include/render/auxiliarystoragespec.h
        # Slice Z.16-data — JoinSpec for the Joins tab.
        include/render/joinspec.h
        # Slice Z.12-data — DiagramSpec for the Diagrams tab.
        include/render/diagramspec.h
        # QSG-2D-1M — pure rendering-decision helpers for the 2D mesh /
        # results QSG renderers (perf stats, dirty domains, LOD policy,
        # chunked render culling, static geometry buffers, async guard).
        include/render/qsg2drenderstats.h
        include/render/qsg2ddirtystate.h
        include/render/qsg2dlodpolicy.h
        include/render/meshrenderchunkindex.h
        include/render/meshstaticgeometrybuffers.h
        include/render/qsg2dasyncresult.h
        # QSG-2D-1M Phase 7/8 — async contour worker + GPU ramp LUT math.
        include/render/contourjob.h
        include/render/scalarramplut.h
        include/map/scalarfillmaterial.h

        # ── Plot ─────────────────────────────────────────────────────────────
        include/plot/profilerouter.h
        include/plot/profilebuilder.h
        include/plot/profilenetworkadapter.h
        include/plot/profileplotwidget.h
        include/plot/profileattributesampler.h
        include/plot/profileattributetrackoptions.h
        include/plot/profileattributetrackswidget.h
        include/plot/profileplotoptions.h
        include/plot/profilesection.h
        include/plot/meshprofilesampler.h
        include/plot/rasterprofilesampler.h
        include/plot/meshprofileplotwidget.h
        include/plot/meshprofileplotoptions.h
        include/plot/profilesourcefetcher.h
        include/plot/plotattribute.h
        include/plot/numberformat.h
        include/plot/chartproperties.h
        include/plot/seriesstatistics.h
        include/plot/seriesstyle.h
        include/plot/seriesstyleobject.h
        include/plot/fitmetrics.h
        include/plot/irunlayer.h
        include/plot/resultdescriptor.h
        include/plot/comparisonplotmodel.h
        include/plot/seriesdataexport.h
        include/plot/swmmoutrunlayer.h
        include/plot/mesh2drunlayer.h
        include/plot/observedcsvrunlayer.h

        # ── Layers ───────────────────────────────────────────────────────────
        include/layers/openswmmvislayer.h
        include/layers/tilepyramidlayer.h
        include/layers/gisvectorlayer.h
        include/layers/gisvectorsymboladapter.h
        include/layers/gisrasterlayer.h
        include/layers/wcslayer.h
        include/layers/wmslayer.h
        include/layers/wmtslayer.h
        include/layers/swmm_category.h
        include/layers/swmmmodellayer.h
        include/layers/swmmelementsymboladapter.h
        include/layers/hydrographmodels.h
        include/layers/speciesattributes.h
        include/layers/swmmresultslayer.h
        include/layers/tabulardatalayer.h
        include/layers/xyztilelayer.h
        include/layers/annotationtextitem.h
        include/layers/annotationlayer.h

        # ── Map / GIS ────────────────────────────────────────────────────────
        include/map/spatialreferencesystem.h
        include/map/mapextent.h
        include/map/crsmanager.h
        include/map/mapcanvas.h
        include/map/maprenderjob.h
        include/map/mapundostack.h
        # Object-creation defaults applied by the Add*Commands (see
        # workplans/OBJECT_CREATION_DEFAULTS_PLAN_2026-08-03.md).
        include/map/objectdefaultsapplier.h
        # FEATURE_LAYER_TO_SWMM_IMPORT — import-specific undo commands.
        include/map/importcommands.h
        # Undoable per-cell 2D mesh attribute edits (toolbar / panel / dialog).
        include/map/meshcommands.h
        # Slice BB Phase 8.6.16 — QUndoCommand for per-class renderer edits.
        include/map/legendclasseditcommands.h
        # Gap B1 — canonical legend-content helpers shared by both legend views.
        include/map/legendcontent.h
        include/map/openswmmvisgraphicsview.h
        include/map/openswmmvisscene.h
        include/map/graphicsitems.h
        include/map/scalebarsettings.h
        include/map/snapengine.h
        include/map/swmmlayeritem.h
        include/map/swmmlayerqsgrenderer.h
        include/map/swmm2dmeshqsgrenderer.h
        include/map/swmm2dresultsqsgrenderer.h
        include/map/profilepathoverlay.h
        include/map/selectionbeaconoverlay.h
        include/map/meshprofileoverlay.h

        # ── Map tools ────────────────────────────────────────────────────────
        include/map/tools/maptool.h
        include/map/tools/maptoolprofilemarker.h
        include/map/tools/maptoolpan.h
        include/map/tools/maptoolzoom.h
        include/map/tools/maptoolmeasure.h
        include/map/tools/maptoolselect.h
        include/map/tools/maptoolselectpolygon.h
        include/map/tools/maptoolplotpick.h
        include/map/tools/maptoolpick2dcells.h
        include/map/tools/maptoolmeshprofile.h
        include/map/tools/maptoolmeshselectvertex.h
        include/map/tools/maptoolmeshselectedge.h
        include/map/tools/maptoolidentify.h
        include/map/tools/maptooladdnode.h
        include/map/tools/maptooladdvirtualnode.h
        include/map/tools/maptooladdlink.h
        include/map/tools/maptooladdgage.h
        include/map/tools/maptooladdsubcatchment.h
        include/map/tools/maptooladdtext.h
        include/map/tools/maptoolselectprofile.h

        # ── Project / workspace ──────────────────────────────────────────────
        include/project/openswmmvisworkspace.h
        include/project/openswmmvisworkspacemodel.h
        include/project/openswmmvissession.h
        include/project/examplesseeder.h
        include/project/projectserializer.h
        include/project/saveaspathnormalizer.h
        include/project/swmmvisproject.h

        # ── Plugins (engine plugin SDK bridge) ───────────────────────────────
        include/layers/swmm2dmeshlayer.h
        include/layers/swmm2dresultslayer.h
        include/io/mesh2dh5reader.h
        include/io/rptparser.h
        include/io/timeseriesparse.h
        include/io/gdaldrivers.h
        include/io/rastergeoref.h

        # ── Curve (Slice BQ Phase 6.7.1) ─────────────────────────────────────
        include/curve/curveprovider.h
        include/curve/curveregistry.h
        include/curve/curveundocommands.h

        # ── Pattern (Slice BQ Phase 6.7.2) ───────────────────────────────────
        include/pattern/patternprovider.h
        include/pattern/patternregistry.h
        include/pattern/patternundocommands.h

        # ── Controls (Slice BR Phase 6.8.1/2) ────────────────────────────────
        include/controls/controlruleprovider.h
        include/controls/controlruleregistry.h
        include/controls/rulesyntaxhighlighter.h
        include/controls/rulevalidator.h
        include/ui/models/rulelistmodel.h
        include/ui/widgets/rulecodeeditor.h
        include/ui/dialogs/ruleseditordialog.h

        # ── Climatology ([TEMPERATURE]/[EVAPORATION]/[ADJUSTMENTS]) ───────────
        include/ui/dialogs/climatologydialog.h
        include/ui/dialogs/wateragesourcesdialog.h
        include/ui/dialogs/initialqualitydialog.h
        include/ui/dialogs/reactionsystemeditordialog.h
        include/ui/widgets/reactionexpressionedit.h

        # ── Transect (Slice BQ Phase 6.7.4) ──────────────────────────────────
        include/transect/transectprovider.h
        include/transect/transectregistry.h
        include/transect/transectundocommands.h
        include/ui/models/transectlistmodel.h
        include/ui/models/transectstationtablemodel.h
        include/ui/widgets/transectchartview.h
        include/ui/dialogs/transectpropertybag.h
        include/ui/dialogs/transecteditordialog.h

        # ── Street ([STREETS] parametric cross-sections) ─────────────────────
        include/street/streetprovider.h
        include/street/streetregistry.h
        include/ui/models/streetlistmodel.h
        include/ui/dialogs/streeteditordialog.h

        # ── Pollutant ([POLLUTANTS]) ─────────────────────────────────────────
        include/pollutant/pollutantprovider.h
        include/pollutant/pollutantregistry.h
        include/ui/models/pollutantlistmodel.h
        include/ui/dialogs/pollutanteditordialog.h

        # ── Land use ([LANDUSES]) ────────────────────────────────────────────
        include/landuse/landuseprovider.h
        include/landuse/landuseregistry.h
        include/ui/models/landuselistmodel.h
        include/ui/models/qualityfunctiontablemodels.h
        include/ui/dialogs/landuseeditordialog.h

        # ── Aquifer ([AQUIFERS]) ─────────────────────────────────────────────
        include/aquifer/aquiferprovider.h
        include/aquifer/aquiferregistry.h
        include/ui/models/aquiferlistmodel.h
        include/ui/dialogs/aquifereditordialog.h

        # ── Snowpack ([SNOWPACKS]) ───────────────────────────────────────────
        include/snowpack/snowpackprovider.h
        include/snowpack/snowpackregistry.h
        include/ui/models/snowpacklistmodel.h
        include/ui/dialogs/snowpackeditordialog.h

        # ── Inlet ([INLETS]) ─────────────────────────────────────────────────
        include/inlet/inletprovider.h
        include/inlet/inletregistry.h
        include/ui/models/inletlistmodel.h
        include/ui/dialogs/inleteditordialog.h

        # ── LID control ([LID_CONTROLS]) ─────────────────────────────────────
        include/lid/lidcontrolprovider.h
        include/lid/lidcontrolregistry.h
        include/ui/models/lidcontrollistmodel.h
        include/ui/dialogs/lidcontroleditordialog.h

        # ── User flags ([USER_FLAGS] / [USER_FLAG_VALUES]) ───────────────────
        include/ui/models/userflagsmodel.h
        include/ui/dialogs/userflagsdialog.h
        include/ui/dialogs/userflagvaluesdialog.h
        include/ui/properties/userflagseditref.h
        include/ui/properties/userflagseditbutton.h
        include/ui/properties/initialqualityeditref.h
        include/ui/properties/initialqualityeditbutton.h

        # ── Timeseries (Slice BQ Phase 6.7.3) ────────────────────────────────
        include/timeseries/timeseriesprovider.h
        include/timeseries/timeseriesregistry.h
        include/timeseries/timeseriesseriescommands.h
        include/timeseries/timeseriesundocommands.h
        include/mesh/dtmsampler.h
        include/mesh/dtmthinner.h
        include/mesh/inpmeshreader.h
        include/mesh/inpmeshwriter.h
        include/mesh/meshautocouple.h
        include/mesh/meshbctype.h
        include/mesh/meshboundarygraph.h
        include/mesh/meshcellparams.h
        include/mesh/meshenginesync.h
        include/mesh/meshedgebc.h
        include/mesh/meshgenerator.h
        include/mesh/meshinfil.h
        include/mesh/meshminsizecleanup.h
        include/mesh/meshreorder.h
        include/mesh/meshstagecache.h
        include/mesh/pslgminsize.h
        include/mesh/pslgprep.h
        include/mesh/meshhoverprobe.h
        include/mesh/meshobjectref.h
        include/mesh/meshresult.h
        include/mesh/trirefinehook.h
        include/plugins/filefilterregistry.h

        # ── Output (Slice QA) ────────────────────────────────────────────────
        include/output/outputstatsregistry.h

        # ── Simulation ───────────────────────────────────────────────────────
        include/simulation/runpathresolver.h
        include/simulation/simulationrunner.h
        include/simulation/simulationstatusmodel.h

        # ── Section view (Slice SP — vector section / profile diagrams) ──────
        include/ui/sectionview/xsectsampler.h
        include/ui/sectionview/sectiondiagram.h
        include/ui/sectionview/sectionpreviewwidget.h
        include/ui/sectionview/sectionmodelbuilders.h
        include/ui/sectionview/lidlayerdiagram.h
        include/ui/sectionview/xsecticonrenderer.h

        # ── UI utilities ─────────────────────────────────────────────────────
        include/ui/util/externalcolumnfile.h
        include/ui/util/externalcolumnfilecore.h

        # ── UI panels ────────────────────────────────────────────────────────
        include/ui/panels/attributedelegates.h
        include/ui/panels/propertiespanel.h
        include/ui/panels/sectionviewpanel.h
        include/ui/panels/attributetablepanel.h
        include/ui/panels/layertreepanel.h
        include/ui/panels/layertreecategories.h
        include/ui/panels/objectbrowserpanel.h
        include/ui/panels/meshattributetablemodel.h
        include/ui/panels/swmmattributetablemodel.h
        include/ui/panels/swmmobjecttreemodel.h
        include/ui/panels/tabulardatatablemodel.h
        include/ui/panels/curvepointtablemodel.h
        include/ui/panels/patternfactortablemodel.h
        include/ui/panels/timeserieslistmodel.h
        include/ui/panels/timeseriestablemodel.h
        include/ui/panels/overviewmappanel.h
        # Slice BB Phase 8.6.11 / 8.6.16 — dockable per-class legend / style editor.
        include/ui/panels/legenddock.h
        # Slice Z.18 — always-open layer-styling editor dock.
        include/ui/panels/layerstylingdock.h

        # ── UI models (Slice BB Phase 8.6.16) ────────────────────────────────
        include/ui/models/legendlayertreemodel.h

        # ── UI delegates (Slice BB Phase 8.6.16) ─────────────────────────────
        include/ui/delegates/datetimedelegate.h
        include/ui/delegates/legendcolordelegate.h

        # ── UI property adapters (Slice Z.5.3 / AG.3) ────────────────────────
        include/ui/properties/xsectshapegeom.h               # shared shape/geom metadata
        include/ui/properties/swmmlinkpropertyadapter.h
        include/ui/properties/swmmnodepropertyadapter.h
        include/ui/properties/swmmsubcatchpropertyadapter.h
        include/ui/properties/meshvertexpropertyadapter.h    # §V.VF
        include/ui/properties/meshedgepropertyadapter.h      # §V.VF
        include/ui/properties/meshtrianglepropertyadapter.h  # §V.VF

        # ── Compound editor cell widget (Slice DB.2) ─────────────────────────
        include/ui/properties/nodecompoundeditref.h
        include/ui/properties/nodecompoundeditbutton.h
        # Phase 3 — subcatchment compound editor (land use / GW / LID)
        include/ui/properties/subcatchcompoundeditref.h
        include/ui/properties/subcatchcompoundeditbutton.h

        # ── Link compound editor cell widget (Slice SC.1) ────────────────────
        include/ui/properties/linkcompoundeditref.h
        include/ui/properties/linkcompoundeditbutton.h

        # ── Culvert code inline combobox (ATTRIBUTE_EDITOR_WIRING Phase 0) ──
        include/ui/properties/culvertcodes.h
        include/ui/properties/culvertcoderef.h
        include/ui/properties/culvertcodecombobox.h

        # ── Rain gage recording-interval H:MM combo (DA.2 parity) ───────────
        include/ui/properties/rainintervalref.h
        include/ui/properties/rainintervalcombobox.h

        # ── Data-object picker cell widget (Slice DA.4.3) ───────────────────
        include/ui/properties/dataobjectref.h
        include/ui/properties/dataobjectpickereditor.h

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

        # ── UI editors (Slice BM.0-Browse-Edit) ──────────────────────────────
        include/ui/editors/comprehensiveeditorregistry.h

        # ── Connections ──────────────────────────────────────────────────────
        include/connections/basemapconnection.h
        include/connections/basemapconnectionstore.h

        # ── UI action registry (UI redesign P1) ──────────────────────────────
        include/ui/actioncatalog.h
        include/ui/actionregistry.h
        include/ui/mdiworkspacechrome.h

        # ── UI theme (UI redesign P2/P3) ─────────────────────────────────────
        include/ui/theme/themetokens.h
        include/ui/theme/thememanager.h
        include/ui/theme/themehelpers.h
        include/ui/theme/iconfactory.h

        # ── UI toolbars ──────────────────────────────────────────────────────
        include/ui/toolbars/compacttoolbarcontroller.h
        include/ui/toolbars/mesheditingtoolbar.h
        include/ui/toolbars/ribboncompactor.h
        include/ui/toolbars/ribbongroup.h
        include/ui/toolbars/ribbonlayoutsolver.h
        include/ui/toolbars/ribbonsplitbutton.h
        include/ui/toolbars/terraintoolbar.h

        # ── UI widgets ───────────────────────────────────────────────────────
        include/ui/widgets/commandpalette.h
        include/ui/widgets/commandpalettemodel.h
        include/ui/widgets/shortcuteditorwidget.h
        include/ui/widgets/basemaphttpheaderswidget.h
        include/ui/widgets/profilelayerpanel.h
        include/ui/widgets/seriesstyleeditor.h
        include/ui/widgets/perattributethemingwidget.h
        include/ui/widgets/legendoverlay.h
        include/ui/widgets/interactivechartview.h
        include/ui/widgets/cursorwindowslider.h
        include/ui/widgets/chartaxisformatcontroller.h
        include/ui/widgets/timeserieseditchartview.h
        include/ui/widgets/curveeditchartview.h
        include/ui/widgets/patterneditchartview.h
        include/ui/widgets/attributepickermenu.h
        include/ui/widgets/rangeslider.h
        include/ui/widgets/statssummarypanel.h
        include/ui/widgets/labeledcontrols.h
        include/ui/widgets/treatmentexpressionedit.h
        # Slice BB-α — colour-ramp combobox with gradient-swatch dropdown.
        include/ui/widgets/colorrampcombobox.h
        # Slice US.1 — shared classification editor (UNIFIED_STYLING plan S1).
        include/ui/widgets/classificationeditor.h
        # Slice D1-a — ClassificationScheme property-grid cell editor + the
        # style-grid delegate factory (MESH_SUBLAYER_CLASSIFICATION_UI_PLAN).
        include/ui/widgets/classificationschemecelleditor.h
        include/ui/widgets/stylepropertydelegate.h
        # Phase GG0d — mesh generation region-defaults table (INTEGRATED2D_GW
        # GUI plan §3.3), extracted so buildUi() does not grow further (§7).
        include/ui/widgets/meshregiondefaultswidget.h

        # ── Dialogs ──────────────────────────────────────────────────────────
        # Shared confirm → convert → summary flow for node/link type changes.
        include/ui/dialogs/typeconversionflow.h
        include/ui/dialogs/addbasemapdialog.h
        include/ui/dialogs/crsselectiondialog.h
        include/ui/dialogs/crschangedialog.h
        include/ui/dialogs/aboutdialog.h
        include/ui/dialogs/mesh2dgroundwaterdialog.h
        include/ui/dialogs/assignraingagesdialog.h
        include/ui/dialogs/meshattributeassigndialog.h
        include/ui/dialogs/infilassigntoselectiondialog.h
        include/ui/dialogs/meshgenerationdialog.h
        include/ui/dialogs/nodecompoundeditdialog.h
        include/ui/dialogs/subcatchcompoundeditdialog.h
        include/ui/dialogs/linkcompoundeditdialog.h
        include/ui/dialogs/hydrographgroupeditor.h
        include/ui/dialogs/newprojectdialog.h
        include/ui/dialogs/pluginsdialog.h
        include/ui/dialogs/preferencesdialog.h
        # Object Defaults preferences page (self-contained widget).
        include/ui/dialogs/objectdefaultspage.h
        include/ui/dialogs/simulationoptionsdialog.h
        include/ui/dialogs/hotstartsavesmodel.h
        include/ui/dialogs/pathbrowsedelegate.h
        include/ui/dialogs/pluginstablemodel.h
        include/ui/dialogs/comparisonplotdialog.h
        include/ui/dialogs/comparisonpairsdialog.h
        include/ui/dialogs/plotvariablepickerdialog.h
        include/ui/dialogs/chartpropertiesdialog.h
        # Slice BB Phase 8.6.16 — legend right-click "Properties…" dialog.
        include/ui/dialogs/legendpropertiesdialog.h
        # Slice U-2 — ILayerStyleSubject interface for the unified
        # LayerStyleDialog. Each layer surfaces its styleable subjects
        # (sublayers, kind-symbols, raster-renderer config, etc.) via
        # OpenSWMMVisLayer::styleSubjects().
        include/ui/dialogs/ilayerstylesubject.h
        include/ui/dialogs/istyleeditorwidget.h
        include/ui/dialogs/irendererpanel.h
        include/ui/dialogs/symbologytab.h
        include/ui/dialogs/labelstab.h
        # VS.5/VS.11 — Temporal/Mask/AuxStorage/Joins/Diagrams tab widgets
        # removed (deferred-feature tabs dropped from the layer dialog).
        include/ui/dialogs/stylemanagerdialog.h
        include/ui/dialogs/kindtreesymbologypanel.h
        include/ui/dialogs/layerstyledialog.h
        include/ui/dialogs/sublayerselectiondialog.h
        # FEATURE_LAYER_TO_SWMM_IMPORT — feature layer → SWMM objects
        # import dialog (workplans/FEATURE_LAYER_TO_SWMM_IMPORT_DIALOG_
        # PLAN_2026-07-18.md).
        include/ui/dialogs/import/importtargetregistry.h
        include/ui/dialogs/import/importmapping.h
        include/ui/dialogs/import/importplan.h
        include/ui/dialogs/import/importplanning.h
        include/ui/dialogs/import/featurelayerimporter.h
        include/ui/dialogs/import/importmappingmodel.h
        include/ui/dialogs/import/importpreviewmodel.h
        include/ui/dialogs/import/importfeaturelayerdialog.h
        # Slice Z.2 — Rule-as-ILayerStyleSubject adapter
        # (RENDERING_RULE_MODEL_PLAN.md).
        include/ui/dialogs/rulestylesubject.h
        # Slice Z.3 — Rule-driven Symbology tab widget.
        include/ui/dialogs/rulesymbologytab.h
        # Slice U-V — custom GIS-themed editor widgets.
        include/ui/dialogs/editors/featurestyleeditor.h
        # SE.1–SE.3 — dedicated editors for the *SymbolStyleAdapter family.
        include/ui/dialogs/editors/symbolstyleeditors.h
        # O2-1 — 2D results layer-level display panel.
        include/ui/dialogs/swmm2dresultsstylepanel.h
        include/ui/dialogs/swmm2dmeshstylepanel.h
        include/ui/dialogs/sublayertabhelpers.h
        # SE.4 — dedicated editors for the raster / mesh / 2D archetypes.
        include/ui/dialogs/editors/symbolstyleeditors2d.h
        include/ui/dialogs/editors/swmmelementsymboleditor.h
        include/ui/dialogs/editors/gisvectorsymboleditor.h
        include/ui/dialogs/editors/rastercolorrampeditor.h
        include/ui/dialogs/editors/kindrendererpanel.h
        # Slice US.1 — classification bindings (UNIFIED_STYLING plan S1).
        include/ui/dialogs/editors/classificationbindings.h
        include/ui/dialogs/editors/markershapeeditor.h
        include/ui/widgets/colorbutton.h
        include/ui/widgets/sunpositionthumb.h
        include/ui/widgets/labelconfigeditor.h
        include/ui/dialogs/labelexpressiondialog.h
        include/ui/widgets/dashstylecombo.h
        include/ui/widgets/markershapecombo.h
        include/ui/widgets/stylepreviewswatch.h
        include/ui/widgets/relativepathpicker.h
        include/ui/dialogs/dialoglayoutpersistence.h
        include/ui/dialogs/dialoglayoutwatcher.h
        include/ui/dialogs/dialogregistry.h
        include/ui/dialogs/curveeditordialog.h
        include/ui/dialogs/patterneditordialog.h
        include/ui/dialogs/timeserieseditordialog.h
        include/ui/dialogs/colorrampeditordialog.h
        include/ui/dialogs/statisticsdashboarddialog.h
        include/ui/dialogs/statusreportdialog.h
        include/ui/dialogs/tabularresultsdialog.h
        include/ui/dialogs/scatterplotdialog.h
        include/ui/dialogs/customreportdialog.h
        include/ui/dialogs/annotationstyledialog.h
        include/ui/dialogs/calibrationdatadialog.h
        include/ui/dialogs/profilepathpickerdialog.h
        include/ui/dialogs/profileplotdialog.h
        include/ui/dialogs/profileoptionsdialog.h
        include/ui/dialogs/meshprofileplotdialog.h
        include/ui/dialogs/rasterprofileplotdialog.h
        include/ui/dialogs/profilesourcestyleadapter.h
        include/ui/dialogs/wmsconnectiondialog.h
        include/ui/dialogs/wmtsconnectiondialog.h
        include/ui/dialogs/licenseagreementdialog.h

        # ── Source files ─────────────────────────────────────────────────────
        src/main.cpp

        # App
        src/swmmvis.cpp
        src/swmmvisactions.cpp
        src/swmmvisapplication.cpp
        src/swmmvisprojectwindow.cpp
        src/swmmvissplashscreen.cpp

        # Core
        src/core/measurementunitmanager.cpp
        src/core/openswmmvislogmessage.cpp
        src/core/preferencesmanager.cpp
        src/core/linkrenderingprefs.cpp
        src/core/noderenderingprefs.cpp
        src/core/meshbcrenderingprefs.cpp
        src/core/selectionrenderingprefs.cpp
        src/core/queryparser.cpp
        src/core/unitsystem.cpp
        src/core/loadprogress.cpp
        src/core/crsreproject.cpp
        src/core/editgeometry.cpp
        src/core/gageassignment.cpp
        src/core/gageblend.cpp
        src/core/gisdatapaths.cpp
        src/core/basemapcrypto.cpp

        # Connections
        src/connections/basemapconnectionstore.cpp

        # Selection
        src/selection/selectionmanager.cpp
        src/selection/selectionops.cpp

        # Animation
        src/animation/animationcontroller.cpp

        # Render
        src/render/categoricalpalette.cpp
        # Slice BB-α — colour-ramp data type + interval binner impls.
        src/render/colorramp.cpp
        src/render/labelconfig.cpp
        # Slice US.B3 — shared label painter impl (UNIFIED_STYLING plan B3).
        src/render/labelpainter.cpp
        src/render/basemaprenderparams.cpp
        src/render/stylefileio.cpp
        src/render/expressionevaluator.cpp
        src/render/intervalbinner.cpp
        # Slice US.1 — shared classification scheme (UNIFIED_STYLING plan S1).
        src/render/classificationscheme.cpp
        # Slice BI Phase 8.13.7-α / 8.13.43-α — DataDefinedScalar impl.
        src/render/datadefined.cpp
        # Slice CTX.1 / CTX.3 — per-(layer,kind) attribute candidate lists.
        src/render/attributecandidates.cpp
        # Slice BI Phase 8.13.6.1 — JSON round-trip impls for the new types.
        src/render/symbollayer.cpp
        src/render/symbolstyle.cpp
        # Gap A1.3 — single archetype-seeded renderer construction path.
        src/render/rendererfactory.cpp
        src/render/legendsymbolitem.cpp
        # Slice BB Phase 8.6.16 — shared model for on-canvas legend chrome.
        src/render/legendoverlaystyle.cpp
        # Slice BI Phase 8.13.6.2 — concrete renderer impls.
        src/render/renderers/singlesymbolrenderer.cpp
        src/render/renderers/graduatedrenderer.cpp
        src/render/renderers/categorizedrenderer.cpp
        src/render/renderers/rulebasedrenderer.cpp
        # Slice Z.9 — UnclassedColorsRenderer impl.
        src/render/renderers/unclassedcolorsrenderer.cpp
        # Slice BI Phase 8.13.6.7 — raster-renderer stub impl.
        src/render/renderers/singlebandpseudocolorrenderer.cpp
        # VS.6 — categorical / paletted raster renderer.
        src/render/renderers/palettedrasterrenderer.cpp
        # Slice BI-MK.1 Phase 8.13.40 — per-kind renderer dispatch adapter.
        src/render/multikindrenderer.cpp
        # Slice Z.1 — layer-scope Rule + RuleList (RENDERING_RULE_MODEL_PLAN.md).
        src/render/rule.cpp
        src/render/rulelist.cpp
        # Slice B.6c — SymbolStyleAdapter for Single Symbol Rule editing.
        src/render/symbolstyleadapter.cpp
        # Slice Z.4 — MarkerShape enum + draw helper + MarkerSymbolLayerSpec.
        src/render/markershape.cpp
        src/render/markersymbollayer.cpp
        # Slice Z.5 — LineSymbolLayerSpec + arrow draw helper.
        src/render/linesymbollayer.cpp
        # VS.2 — FillSymbolLayerSpec + fill draw helper (third primitive).
        src/render/fillsymbollayer.cpp
        # Slice Z.5b — polyline perpendicular offset impl.
        src/render/polylineoffset.cpp
        # Slice Z.6 — Raster + TIN Symbol Layer spec impls.
        src/render/rastersymbollayers.cpp
        # Slice Z.7 — animation bin sampler impl.
        src/render/binsampler.cpp
        # Slice Z.11 — Symbol Levels paint-order impl.
        src/render/symbollevels.cpp
        # Slice Z.17-files — .swmm-rule.json I/O impl.
        src/render/rulelistio.cpp
        # Slice Z.17b — QGIS .qml import/export for Rule Lists impl.
        src/render/qmlrulelistio.cpp
        # Slice Z.13-data — TemporalSpec impl.
        src/render/temporalspec.cpp
        # Slice Z.14-data — MaskSpec impl.
        src/render/maskspec.cpp
        # QSG-2D-1M — render stats / LOD policy / chunk index impls.
        src/render/qsg2drenderstats.cpp
        src/render/qsg2dlodpolicy.cpp
        src/render/meshrenderchunkindex.cpp
        # QSG-2D-1M Phase 7 — thread-safe async contour worker.
        src/render/contourjob.cpp
        # QSG-2D-1M Phase 8 — GPU scalar-fill material (ramp LUT texture).
        src/map/scalarfillmaterial.cpp
        # Slice Z.14-paint — runtime clip-path resolver impl.
        src/render/maskclipresolver.cpp
        # Slice Z.15-data — AuxiliaryStorageSpec impl.
        src/render/auxiliarystoragespec.cpp
        # Slice Z.16-data — JoinSpec impl.
        src/render/joinspec.cpp
        # Slice Z.12-data — DiagramSpec impl.
        src/render/diagramspec.cpp
        # Slice U-0 — granular per-Category FeatureSublayer impl. Replaces
        # the four legacy sublayer .cpps.
        src/render/sublayers/feature/featuresublayer.cpp
        # Slice S5 — five 2D sublayer .cpp impls.
        src/render/sublayers/meshfillsublayer.cpp
        src/render/sublayers/meshedgesublayer.cpp
        src/render/sublayers/meshnodesublayer.cpp
        src/render/sublayers/velocityvectorsublayer.cpp
        src/render/sublayers/isolinesublayer.cpp
        src/render/sublayers/contourbandsublayer.cpp
        src/render/sublayers/scalarfillsublayer.cpp
        src/render/sublayers/meshbcsublayer.cpp
        src/render/sublayers/couplednodesublayer.cpp

        # Plot
        src/plot/profilerouter.cpp
        src/plot/profilebuilder.cpp
        src/plot/profilenetworkadapter.cpp
        src/plot/profilenetworkadapter_model.cpp
        src/plot/profileplotwidget.cpp
        src/plot/profileattributesampler.cpp
        src/plot/profileattributetrackoptions.cpp
        src/plot/profileattributetrackswidget.cpp
        src/plot/profileplotoptions.cpp
        src/plot/meshprofilesampler.cpp
        src/plot/rasterprofilesampler.cpp
        src/plot/meshprofileplotwidget.cpp
        src/plot/meshprofileplotoptions.cpp
        src/plot/profilesourcefetcher.cpp
        src/plot/plotattribute.cpp
        src/plot/numberformat.cpp
        src/plot/chartproperties.cpp
        src/plot/seriesstatistics.cpp
        src/plot/seriesstyle.cpp
        src/plot/seriesstyleobject.cpp
        src/plot/fitmetrics.cpp
        src/plot/seriespairing.cpp
        src/plot/comparisonplotmodel.cpp
        src/plot/seriesdataexport.cpp
        src/plot/resultdescriptor.cpp
        src/plot/swmmoutrunlayer.cpp
        src/plot/swmmoutrunlayer_codes.cpp
        src/plot/mesh2drunlayer.cpp
        src/plot/observedcsvrunlayer.cpp

        # Layers
        src/layers/openswmmvislayer.cpp
        src/layers/tilepyramidlayer.cpp
        src/layers/gisvectorlayer.cpp
        src/layers/gisrasterlayer.cpp
        src/layers/wcslayer.cpp
        src/layers/wmslayer.cpp
        src/layers/wmtslayer.cpp
        src/layers/swmmmodellayer.cpp
        src/layers/hydrographmodels.cpp
        src/layers/speciesattributes.cpp
        src/layers/swmmresultslayer.cpp
        src/layers/tabulardatalayer.cpp
        src/layers/xyztilelayer.cpp
        src/layers/annotationtextitem.cpp
        src/layers/annotationlayer.cpp

        # Map / GIS
        src/map/spatialreferencesystem.cpp
        src/map/mapextent.cpp
        src/map/crsmanager.cpp
        src/map/mapcanvas.cpp
        src/map/maprenderjob.cpp
        src/map/mapundostack.cpp
        # Object-creation defaults applied by the Add*Commands (see
        # workplans/OBJECT_CREATION_DEFAULTS_PLAN_2026-08-03.md).
        src/map/objectdefaultsapplier.cpp
        # FEATURE_LAYER_TO_SWMM_IMPORT — import-specific undo commands.
        src/map/importcommands.cpp
        # Undoable per-cell 2D mesh attribute edits (toolbar / panel / dialog).
        src/map/meshcommands.cpp
        # Slice BB Phase 8.6.16 — QUndoCommand for per-class renderer edits.
        src/map/legendclasseditcommands.cpp
        # Gap B1 — canonical legend-content helpers shared by both legend views.
        src/map/legendcontent.cpp
        src/map/openswmmvisgraphicsview.cpp
        src/map/openswmmvisscene.cpp
        src/map/graphicsitems.cpp
        src/map/scalebarsettings.cpp
        src/map/snapengine.cpp
        src/map/swmmlayeritem.cpp
        src/map/swmmlayerqsgrenderer.cpp
        src/map/swmm2dmeshqsgrenderer.cpp
        src/map/swmm2dresultsqsgrenderer.cpp
        src/map/profilepathoverlay.cpp
        src/map/selectionbeaconoverlay.cpp
        src/map/meshprofileoverlay.cpp

        # Map tools
        src/map/tools/maptool.cpp
        src/map/tools/maptoolprofilemarker.cpp
        src/map/tools/maptoolpan.cpp
        src/map/tools/maptoolzoom.cpp
        src/map/tools/maptoolmeasure.cpp
        src/map/tools/maptoolselect.cpp
        src/map/tools/maptoolselectpolygon.cpp
        src/map/tools/maptoolplotpick.cpp
        src/map/tools/maptoolpick2dcells.cpp
        src/map/tools/maptoolmeshprofile.cpp
        src/map/tools/maptoolmeshselectvertex.cpp
        src/map/tools/maptoolmeshselectedge.cpp
        src/map/tools/maptoolidentify.cpp
        src/map/tools/maptooladdnode.cpp
        src/map/tools/maptooladdvirtualnode.cpp
        src/map/tools/maptooladdlink.cpp
        src/map/tools/maptooladdgage.cpp
        src/map/tools/maptooladdsubcatchment.cpp
        src/map/tools/maptooladdtext.cpp
        src/map/tools/maptoolselectprofile.cpp

        # Project / workspace
        src/project/openswmmvisworkspace.cpp
        src/project/openswmmvisworkspacemodel.cpp
        src/project/openswmmvissession.cpp
        src/project/examplesseeder.cpp
        src/project/projectserializer.cpp
        src/project/saveaspathnormalizer.cpp
        src/project/swmmvisproject.cpp

        # Plugins (engine plugin SDK bridge)
        src/layers/swmm2dmeshlayer.cpp
        src/layers/swmm2dresultslayer.cpp
        src/layers/meshspatialgrid.cpp
        src/io/mesh2dh5reader.cpp
        src/io/rptparser.cpp
        src/io/timeseriesparse.cpp
        src/io/gdaldrivers.cpp
        src/io/rastergeoref.cpp

        # ── Curve (Slice BQ Phase 6.7.1) ─────────────────────────────────────
        src/curve/curveprovider.cpp
        src/curve/curveregistry.cpp
        src/curve/curveundocommands.cpp

        # ── Pattern (Slice BQ Phase 6.7.2) ───────────────────────────────────
        src/pattern/patternprovider.cpp
        src/pattern/patternregistry.cpp
        src/pattern/patternundocommands.cpp

        # ── Controls (Slice BR Phase 6.8.1/2) ────────────────────────────────
        src/controls/controlruleprovider.cpp
        src/controls/controlruleregistry.cpp
        src/controls/rulesyntaxhighlighter.cpp
        src/controls/rulevalidator.cpp
        src/ui/models/rulelistmodel.cpp
        src/ui/widgets/commandpalette.cpp
        src/ui/widgets/commandpalettemodel.cpp
        src/ui/widgets/shortcuteditorwidget.cpp
        src/ui/widgets/rulecodeeditor.cpp
        src/ui/dialogs/ruleseditordialog.cpp
        # Iteration 4 — treatment-expression editing stack (highlighter +
        # completing editor + cell delegate; NodeCompoundEditDialog Treatment).
        src/ui/widgets/treatmentexpressionedit.cpp

        # ── Climatology ([TEMPERATURE]/[EVAPORATION]/[ADJUSTMENTS]) ───────────
        src/ui/dialogs/climatologydialog.cpp
        src/ui/dialogs/wateragesourcesdialog.cpp
        src/ui/dialogs/initialqualitydialog.cpp
        src/ui/dialogs/reactionsystemeditordialog.cpp
        src/ui/widgets/reactionexpressionedit.cpp

        # ── Transect (Slice BQ Phase 6.7.4) ──────────────────────────────────
        src/transect/transectprovider.cpp
        src/transect/transectregistry.cpp
        src/transect/transectundocommands.cpp
        src/ui/models/transectlistmodel.cpp
        src/ui/models/transectstationtablemodel.cpp
        src/ui/widgets/transectchartview.cpp
        src/ui/dialogs/transectpropertybag.cpp
        src/ui/dialogs/transecteditordialog.cpp

        # ── Street ([STREETS] parametric cross-sections) ─────────────────────
        src/street/streetprovider.cpp
        src/street/streetregistry.cpp
        src/ui/models/streetlistmodel.cpp
        src/ui/dialogs/streeteditordialog.cpp

        # ── Pollutant ([POLLUTANTS]) ─────────────────────────────────────────
        src/pollutant/pollutantprovider.cpp
        src/pollutant/pollutantregistry.cpp
        src/ui/models/pollutantlistmodel.cpp
        src/ui/dialogs/pollutanteditordialog.cpp

        # ── Land use ([LANDUSES]) ────────────────────────────────────────────
        src/landuse/landuseprovider.cpp
        src/landuse/landuseregistry.cpp
        src/ui/models/landuselistmodel.cpp
        src/ui/models/qualityfunctiontablemodels.cpp
        src/ui/dialogs/landuseeditordialog.cpp

        # ── Aquifer ([AQUIFERS]) ─────────────────────────────────────────────
        src/aquifer/aquiferprovider.cpp
        src/aquifer/aquiferregistry.cpp
        src/ui/models/aquiferlistmodel.cpp
        src/ui/dialogs/aquifereditordialog.cpp

        # ── Snowpack ([SNOWPACKS]) ───────────────────────────────────────────
        src/snowpack/snowpackprovider.cpp
        src/snowpack/snowpackregistry.cpp
        src/ui/models/snowpacklistmodel.cpp
        src/ui/dialogs/snowpackeditordialog.cpp

        # ── Inlet ([INLETS]) ─────────────────────────────────────────────────
        src/inlet/inletprovider.cpp
        src/inlet/inletregistry.cpp
        src/ui/models/inletlistmodel.cpp
        src/ui/dialogs/inleteditordialog.cpp

        # ── LID control ([LID_CONTROLS]) ─────────────────────────────────────
        src/lid/lidcontrolprovider.cpp
        src/lid/lidcontrolregistry.cpp
        src/ui/models/lidcontrollistmodel.cpp
        src/ui/dialogs/lidcontroleditordialog.cpp

        # ── User flags ([USER_FLAGS] / [USER_FLAG_VALUES]) ───────────────────
        src/ui/models/userflagsmodel.cpp
        src/ui/dialogs/userflagsdialog.cpp
        src/ui/dialogs/userflagvaluesdialog.cpp
        src/ui/properties/userflagseditref.cpp
        src/ui/properties/userflagseditbutton.cpp
        src/ui/properties/initialqualityeditref.cpp
        src/ui/properties/initialqualityeditbutton.cpp

        # ── Timeseries (Slice BQ Phase 6.7.3) ────────────────────────────────
        src/timeseries/timeseriesprovider.cpp
        src/timeseries/timeseriesregistry.cpp
        src/timeseries/timeseriesseriescommands.cpp
        src/timeseries/timeseriesundocommands.cpp
        src/mesh/dtmsampler.cpp
        src/mesh/dtmthinner.cpp
        src/mesh/inpmeshreader.cpp
        src/mesh/inpmeshwriter.cpp
        src/mesh/meshautocouple.cpp
        src/mesh/meshbctype.cpp
        src/mesh/meshboundarygraph.cpp
        src/mesh/meshcellparams.cpp
        src/mesh/meshcellstats.cpp
        src/mesh/meshenginesync.cpp
        src/mesh/meshgenerator.cpp
        src/mesh/meshinfil.cpp
        src/mesh/meshminsizecleanup.cpp
        src/mesh/meshreorder.cpp
        src/mesh/meshstagecache.cpp
        src/mesh/pslgminsize.cpp
        src/mesh/pslgprep.cpp
        src/mesh/trirefinehook.cpp
        src/mesh/meshhoverprobe.cpp
        src/mesh/meshnodemapper.cpp
        src/mesh/meshobjectref.cpp
        src/mesh/naturalnbinterpolator.cpp
        src/plugins/filefilterregistry.cpp

        # Output (Slice QA)
        src/output/outputstatsregistry.cpp

        # Simulation
        src/simulation/runpathresolver.cpp
        src/simulation/simulationrunner.cpp
        src/simulation/simulationstatusmodel.cpp

        # UI utilities
        src/ui/util/externalcolumnfile.cpp

        # UI panels
        src/ui/panels/attributedelegates.cpp
        src/ui/panels/propertiespanel.cpp
        src/ui/panels/sectionviewpanel.cpp

        # ── Section view (Slice SP) ──────────────────────────────────────────
        src/ui/sectionview/xsectsampler.cpp
        src/ui/sectionview/sectiondiagram.cpp
        src/ui/sectionview/sectionpreviewwidget.cpp
        src/ui/sectionview/sectionmodelbuilders.cpp
        src/ui/sectionview/lidlayerdiagram.cpp
        src/ui/sectionview/xsecticonrenderer.cpp
        src/ui/panels/attributetablepanel.cpp
        src/ui/panels/layertreepanel.cpp
        src/ui/panels/layertreecategories.cpp
        src/ui/panels/objectbrowserpanel.cpp
        src/ui/panels/overviewmappanel.cpp
        src/ui/panels/meshattributetablemodel.cpp
        src/ui/panels/swmmattributetablemodel.cpp
        src/ui/panels/swmmobjecttreemodel.cpp
        src/ui/panels/curvepointtablemodel.cpp
        src/ui/panels/patternfactortablemodel.cpp
        src/ui/panels/tabulardatatablemodel.cpp
        src/ui/panels/timeserieslistmodel.cpp
        src/ui/panels/timeseriestablemodel.cpp
        # Slice BB Phase 8.6.11 / 8.6.16 — dockable per-class legend / style editor.
        src/ui/panels/legenddock.cpp
        # Slice Z.18 — always-open layer-styling editor dock.
        src/ui/panels/layerstylingdock.cpp

        # UI models (Slice BB Phase 8.6.16 — legend layer tree).
        src/ui/models/legendlayertreemodel.cpp

        # UI delegates (Slice BB Phase 8.6.16 — legend swatch click-to-edit).
        src/ui/delegates/datetimedelegate.cpp
        src/ui/delegates/legendcolordelegate.cpp

        # UI property adapters
        src/ui/properties/swmmlinkpropertyadapter.cpp
        src/ui/properties/swmmnodepropertyadapter.cpp
        src/ui/properties/swmmsubcatchpropertyadapter.cpp
        src/ui/properties/meshvertexpropertyadapter.cpp      # §V.VF
        src/ui/properties/meshedgepropertyadapter.cpp        # §V.VF
        src/ui/properties/meshtrianglepropertyadapter.cpp    # §V.VF

        # Compound editor cell widget (Slice DB.2)
        src/ui/properties/nodecompoundeditref.cpp
        src/ui/properties/nodecompoundeditbutton.cpp
        # Phase 3 — subcatchment compound editor (land use / GW / LID)
        src/ui/properties/subcatchcompoundeditref.cpp
        src/ui/properties/subcatchcompoundeditbutton.cpp

        # Link compound editor cell widget (Slice SC.1)
        src/ui/properties/linkcompoundeditref.cpp
        src/ui/properties/linkcompoundeditbutton.cpp

        # Culvert code inline combobox (ATTRIBUTE_EDITOR_WIRING Phase 0)
        src/ui/properties/culvertcodes.cpp
        src/ui/properties/culvertcoderef.cpp
        src/ui/properties/culvertcodecombobox.cpp

        # Rain gage recording-interval H:MM combo (DA.2 parity)
        src/ui/properties/rainintervalref.cpp
        src/ui/properties/rainintervalcombobox.cpp

        # Data-object picker cell widget (Slice DA.4.3)
        src/ui/properties/dataobjectref.cpp
        src/ui/properties/dataobjectpickereditor.cpp

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

        # UI editors (Slice BM.0-Browse-Edit)
        src/ui/editors/comprehensiveeditorregistry.cpp

        # Dialogs
        # Shared confirm → convert → summary flow for node/link type changes.
        src/ui/dialogs/typeconversionflow.cpp
        src/ui/dialogs/crsselectiondialog.cpp
        src/ui/dialogs/crschangedialog.cpp
        src/ui/dialogs/aboutdialog.cpp
        src/ui/dialogs/mesh2dgroundwaterdialog.cpp
        src/ui/dialogs/assignraingagesdialog.cpp
        src/ui/dialogs/meshattributeassigndialog.cpp
        src/ui/dialogs/infilassigntoselectiondialog.cpp
        src/ui/dialogs/meshgenerationdialog.cpp
        src/ui/dialogs/nodecompoundeditdialog.cpp
        src/ui/dialogs/subcatchcompoundeditdialog.cpp
        src/ui/dialogs/linkcompoundeditdialog.cpp
        src/ui/dialogs/hydrographgroupeditor.cpp
        src/ui/dialogs/newprojectdialog.cpp
        src/ui/dialogs/pluginsdialog.cpp
        src/ui/dialogs/preferencesdialog.cpp
        # Object Defaults preferences page (self-contained widget).
        src/ui/dialogs/objectdefaultspage.cpp
        src/ui/dialogs/simulationoptionsdialog.cpp
        src/ui/dialogs/simulationoptionshelpers.cpp
        src/ui/dialogs/hotstartsavesmodel.cpp
        src/ui/dialogs/pathbrowsedelegate.cpp
        src/ui/dialogs/pluginstablemodel.cpp
        src/ui/dialogs/comparisonplotdialog.cpp
        src/ui/dialogs/comparisonpairsdialog.cpp
        src/ui/dialogs/plotvariablepickerdialog.cpp
        src/ui/dialogs/chartpropertiesdialog.cpp
        # Slice BB Phase 8.6.16 — legend right-click "Properties…" dialog.
        src/ui/dialogs/legendpropertiesdialog.cpp
        # Slice U-2 / U-3 — unified LayerStyleDialog + ILayerStyleSubject impl.
        src/ui/dialogs/ilayerstylesubject.cpp
        src/ui/dialogs/istyleeditorwidget.cpp
        src/ui/dialogs/irendererpanel.cpp
        src/ui/dialogs/symbologytab.cpp
        src/ui/dialogs/labelstab.cpp
        # VS.5/VS.11 — Temporal/Mask/AuxStorage/Joins/Diagrams tab widgets removed.
        src/ui/dialogs/stylemanagerdialog.cpp
        src/ui/dialogs/kindtreesymbologypanel.cpp
        src/ui/dialogs/layerstyledialog.cpp
        src/ui/dialogs/sublayerselectiondialog.cpp
        # FEATURE_LAYER_TO_SWMM_IMPORT — feature layer → SWMM objects import.
        src/ui/dialogs/import/importtargetregistry.cpp
        src/ui/dialogs/import/importmapping.cpp
        src/ui/dialogs/import/importplanning.cpp
        src/ui/dialogs/import/featurelayerimporter.cpp
        src/ui/dialogs/import/importmappingmodel.cpp
        src/ui/dialogs/import/importpreviewmodel.cpp
        src/ui/dialogs/import/importfeaturelayerdialog.cpp
        # Slice Z.2 — Rule-as-ILayerStyleSubject adapter impl.
        src/ui/dialogs/rulestylesubject.cpp
        # Slice Z.3 — Rule-driven Symbology tab widget impl.
        src/ui/dialogs/rulesymbologytab.cpp
        # Slice U-V — custom GIS-themed editor widgets.
        src/ui/dialogs/editors/featurestyleeditor.cpp
        # SE.1–SE.3 — dedicated editors for the *SymbolStyleAdapter family.
        src/ui/dialogs/editors/symbolstyleeditors.cpp
        # O2-1 — 2D results layer-level display panel.
        src/ui/dialogs/swmm2dresultsstylepanel.cpp
        src/ui/dialogs/swmm2dmeshstylepanel.cpp
        # SE.4 — dedicated editors for the raster / mesh / 2D archetypes.
        src/ui/dialogs/editors/symbolstyleeditors2d.cpp
        src/ui/dialogs/editors/swmmelementsymboleditor.cpp
        src/ui/dialogs/editors/gisvectorsymboleditor.cpp
        src/ui/dialogs/editors/rastercolorrampeditor.cpp
        src/ui/dialogs/editors/kindrendererpanel.cpp
        # Slice US.1 — classification bindings impl (UNIFIED_STYLING plan S1).
        src/ui/dialogs/editors/classificationbindings.cpp
        src/ui/widgets/sunpositionthumb.cpp
        src/ui/dialogs/editors/singlesymbolrendererpanel.cpp
        src/ui/dialogs/editors/markershapeeditor.cpp
        src/ui/dialogs/editors/graduatedrendererpanel.cpp
        src/ui/dialogs/editors/categorizedrendererpanel.cpp
        src/ui/dialogs/editors/rulebasedrendererpanel.cpp
        # Slice IO-10 — relative-path picker + portability normalizer.
        src/ui/widgets/relativepathpicker.cpp
        src/project/ioportabilitynormalizer.cpp

        src/ui/widgets/colorbutton.cpp
        src/ui/widgets/labelconfigeditor.cpp
        src/ui/dialogs/labelexpressiondialog.cpp
        src/ui/widgets/dashstylecombo.cpp
        src/ui/widgets/markershapecombo.cpp
        src/ui/widgets/stylepreviewswatch.cpp
        src/ui/dialogs/dialoglayoutpersistence.cpp
        src/ui/dialogs/dialoglayoutwatcher.cpp
        src/ui/dialogs/dialogregistry.cpp
        src/ui/dialogs/curveeditordialog.cpp
        src/ui/dialogs/patterneditordialog.cpp
        src/ui/dialogs/timeserieseditordialog.cpp
        src/ui/dialogs/colorrampeditordialog.cpp
        src/ui/dialogs/statisticsdashboarddialog.cpp
        src/ui/dialogs/statusreportdialog.cpp
        src/ui/dialogs/tabularresultsdialog.cpp
        src/ui/dialogs/scatterplotdialog.cpp
        src/ui/dialogs/customreportdialog.cpp
        src/ui/dialogs/annotationstyledialog.cpp
        src/ui/dialogs/calibrationdatadialog.cpp
        src/ui/dialogs/profilepathpickerdialog.cpp
        src/ui/dialogs/profileplotdialog.cpp
        src/ui/dialogs/profileoptionsdialog.cpp
        src/ui/dialogs/meshprofileplotdialog.cpp
        src/ui/dialogs/rasterprofileplotdialog.cpp
        src/ui/dialogs/profilesourcestyleadapter.cpp
        src/ui/dialogs/wmsconnectiondialog.cpp
        src/ui/dialogs/wmtsconnectiondialog.cpp
        src/ui/dialogs/addbasemapdialog.cpp
        src/ui/dialogs/licenseagreementdialog.cpp

        # Toolbars
        src/ui/actionregistry.cpp
        src/ui/mdiworkspacechrome.cpp
        src/ui/theme/thememanager.cpp
        src/ui/theme/iconfactory.cpp
        src/ui/toolbars/compacttoolbarcontroller.cpp
        src/ui/toolbars/mesheditingtoolbar.cpp
        src/ui/toolbars/ribboncompactor.cpp
        src/ui/toolbars/ribbongroup.cpp
        src/ui/toolbars/ribbonsplitbutton.cpp
        src/ui/toolbars/terraintoolbar.cpp

        # Widgets
        src/ui/widgets/basemaphttpheaderswidget.cpp
        src/ui/widgets/profilelayerpanel.cpp
        src/ui/widgets/seriesstyleeditor.cpp
        src/ui/widgets/perattributethemingwidget.cpp
        src/ui/widgets/legendoverlay.cpp
        src/ui/widgets/interactivechartview.cpp
        src/ui/widgets/cursorwindowslider.cpp
        src/ui/widgets/chartaxisformatcontroller.cpp
        src/ui/widgets/timeserieseditchartview.cpp
        src/ui/widgets/curveeditchartview.cpp
        src/ui/widgets/patterneditchartview.cpp
        src/ui/widgets/attributepickermenu.cpp
        src/ui/widgets/rangeslider.cpp
        src/ui/widgets/statssummarypanel.cpp
        src/ui/widgets/labeledcontrols.cpp
        # Slice BB-α — colour-ramp combobox impl.
        src/ui/widgets/colorrampcombobox.cpp
        # Slice US.1 — shared classification editor impl (UNIFIED_STYLING plan S1).
        src/ui/widgets/classificationeditor.cpp
        # Slice D1-a — ClassificationScheme cell editor + style-grid delegate impl.
        src/ui/widgets/classificationschemecelleditor.cpp
        src/ui/widgets/stylepropertydelegate.cpp
        # Phase GG0d — mesh generation region-defaults table impl.
        src/ui/widgets/meshregiondefaultswidget.cpp

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
    # QSG-2D-1M Phase 8 — compile the scalar-fill material shaders to .qsb
    # (resource paths :/resources/shaders/scalarfill.{vert,frag}.qsb).
    qt_add_shaders(SWMMVis "swmmvis_scalarfill_shaders"
        PREFIX "/"
        FILES
            resources/shaders/scalarfill.vert
            resources/shaders/scalarfill.frag
    )
    qt_add_translation(QM_FILES ${TS_FILES})
else()
    if(ANDROID)
        add_library(SWMMVis SHARED ${PROJECT_SOURCES})
    else()
        add_executable(SWMMVis ${PROJECT_SOURCES})
    endif()
    qt5_add_translation(QM_FILES ${TS_FILES})
endif()

target_include_directories(SWMMVis
    PRIVATE include
    PRIVATE forms
    PRIVATE ${GDAL_INCLUDE_DIRS}
    # legacy_version.h (LEGACY_SWMM_VERSION macros used by swmmvis.cpp) is
    # provided transitively by the prebuilt engine: the installed package puts
    # the generated header on OpenSWMMEngine::openswmm_engine's INSTALL_INTERFACE
    # include path (include/openswmm/legacy/engine), so linking the engine is
    # sufficient — no reference to the engine's internal build targets.
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
        Qt${QT_VERSION_MAJOR}::PrintSupport
        GDAL::GDAL
        OpenSSL::SSL
        OpenSSL::Crypto
        nanoflann::nanoflann
        $<$<TARGET_EXISTS:QPropertyModel>:QPropertyModel>
        openswmm_engine
        triangle_lib
        ${HDF5_C_LIBRARIES}
)

# SE.0 — define HAVE_QPROPERTYMODEL whenever the QPropertyModel target is
# available (it's linked just above under the same guard). This compiles in
# the QPropertyModel-backed property-grid fallback in SingleSymbolPanel
# (singlesymbolrendererpanel.cpp), which surfaces every *SymbolStyleAdapter's
# grouped Q_PROPERTYs — including the marker-shape combo — so the Single
# Symbol editor is never an empty "no editor registered" panel.
target_compile_definitions(SWMMVis
    PRIVATE
        $<$<TARGET_EXISTS:QPropertyModel>:HAVE_QPROPERTYMODEL>
)

# ── macOS native window helper ──────────────────────────────────────────────
# On macOS, floating dialogs are attached as NSWindow child windows of the main
# window (see attachAsChildWindow) so they stay above the app's own windows
# without floating above OTHER applications. Objective-C++ + AppKit.
if(APPLE)
    target_sources(SWMMVis PRIVATE src/platform/macoswindowutils.mm)
    target_link_libraries(SWMMVis PRIVATE "-framework AppKit")
endif()

set_target_properties(SWMMVis PROPERTIES
    QT_QML_MODULE_NO_IMPORT_SCAN TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER org.openswmm.swmmvis
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE_COPYRIGHT "Copyright (C) 2026 Caleb Buahin. Licensed under GPLv3."
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# ── Windows stack reserve ───────────────────────────────────────────────────
# Windows threads default to a 1 MiB stack RESERVE taken from the PE header,
# and Qt passes dwStackSize = 0 to CreateThread, so QtConcurrent workers get
# that same 1 MiB — versus 8 MiB for the macOS main thread. The mesh pipeline
# runs on a pool worker, and triangulate() alone places ~81 KiB of bad-triangle
# queues (struct mesh's three 4096-element arrays) in a single frame before any
# of Triangle's recursive routines start. vertexsort() recurses on BOTH
# partitions with no smaller-half depth guard, so deep inputs are bounded only
# by the stack. Reserving 8 MiB matches the macOS headroom the code is
# routinely exercised against; it is address space, not committed memory.
if(MSVC)
    target_link_options(SWMMVis PRIVATE /STACK:8388608)
elseif(MINGW)
    target_link_options(SWMMVis PRIVATE -Wl,--stack,8388608)
endif()

# 64-bit only. On a 32-bit Windows build the 2 GiB user address space makes
# every large-mesh pool allocation fail, and nothing else in this file pins the
# architecture — CMakePresets uses Ninja and inherits whatever cl.exe is on
# PATH, so an x86 Native Tools prompt would silently produce one.
if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "SWMMVis requires a 64-bit Windows build (found ${CMAKE_SIZEOF_VOID_P}-byte "
        "pointers). Configure from an x64 Native Tools prompt.")
endif()

# ── Portable GDAL / PROJ data (vcpkg) ───────────────────────────────────────
# Copy the vcpkg-installed share trees into the SWMMVis output so
# setupBundledGisDataPaths() can find them without a system install.
#   - macOS .app bundle: SWMMVis.app/Contents/Resources/{gdal,proj}/
#   - Linux / Windows  : <exe-dir>/{gdal,proj}/
#
# Severity when the data cannot be staged: fatal on Windows, where there is no
# system PROJ to fall back on, so a missing proj.db means the shipped installer
# has no CRS support at all. Elsewhere a system-installed PROJ usually covers
# it, so a non-vcpkg configure stays buildable and only warns.
if(WIN32)
    set(_SWMMVIS_GIS_DATA_SEVERITY FATAL_ERROR)
else()
    set(_SWMMVIS_GIS_DATA_SEVERITY WARNING)
endif()
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
            # Trailing slash on the source is REQUIRED: without it,
            # install(DIRECTORY) copies the directory ITSELF into DESTINATION,
            # yielding bin/gdal/gdal + bin/proj/proj — one level deeper than
            # setupBundledGisDataPaths() probes (<exedir>/gdal, <exedir>/proj),
            # so the packaged app finds no proj.db and CRS operations fail.
            # With the slash the CONTENTS are copied, matching the POST_BUILD
            # copy_directory layout the build tree uses.
            install(DIRECTORY "${_SWMMVIS_GDAL_SHARE}/"
                DESTINATION ${CMAKE_INSTALL_BINDIR}/gdal
                COMPONENT Runtime
            )
            install(DIRECTORY "${_SWMMVIS_PROJ_SHARE}/"
                DESTINATION ${CMAKE_INSTALL_BINDIR}/proj
                COMPONENT Runtime
            )
        endif()
    else()
        # Without proj.db the CRS pickers enumerate zero systems and every
        # reprojection fails — but nothing else breaks, so this must not be
        # allowed to pass silently through configure/build/package.
        message(${_SWMMVIS_GIS_DATA_SEVERITY}
            "GDAL/PROJ share data not found in the vcpkg tree:\n"
            "  ${_SWMMVIS_GDAL_SHARE}\n"
            "  ${_SWMMVIS_PROJ_SHARE}\n"
            "Both must exist to deploy a portable proj.db. Without them the "
            "packaged application cannot select or transform coordinate "
            "reference systems.")
    endif()
else()
    message(${_SWMMVIS_GIS_DATA_SEVERITY}
        "VCPKG_INSTALLED_DIR / VCPKG_TARGET_TRIPLET are not defined, so the "
        "GDAL and PROJ data directories cannot be located. Configure with the "
        "vcpkg toolchain (use a CMakePresets preset); otherwise the build "
        "produces an application with no bundled proj.db and no CRS support.")
endif()

# ── Bundled example projects (Welcome page) ─────────────────────────────────
# Two kinds of example land in the examples dir the launcher scans
# (ExamplesSeeder::discoverExamples — src/project/examplesseeder.cpp):
#   - flat top-level .inp files (legacy single-file examples), and
#   - one SUBDIRECTORY per full example project (.inp + .oswp + data
#     sidecars + optional example.json manifest for display name/description).
# The seeder mirrors this payload into the per-user app-data dir at startup,
# and the Welcome page always copies an example elsewhere before opening it.
set(_SWMMVIS_EXAMPLE_INPS
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_road_culvert/road_culvert.inp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/site_drainage/site_drainage_model.inp"
)
set(_SWMMVIS_EXAMPLE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/bellinge_2d"
)
if(APPLE)
    # POST_BUILD into Contents/Resources/examples; install(TARGETS ... BUNDLE) ships it,
    # same as the GDAL/PROJ Resources above. Wipe the dest first so the dir reflects
    # exactly the curated list (no stale files from a previously-bundled example linger).
    set(_SWMMVIS_EXAMPLES_DEST "$<TARGET_BUNDLE_CONTENT_DIR:SWMMVis>/Resources/examples")
    set(_SWMMVIS_EXAMPLE_DIR_CMDS "")
    foreach(_ex_dir IN LISTS _SWMMVIS_EXAMPLE_DIRS)
        get_filename_component(_ex_name "${_ex_dir}" NAME)
        list(APPEND _SWMMVIS_EXAMPLE_DIR_CMDS
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${_ex_dir}" "${_SWMMVIS_EXAMPLES_DEST}/${_ex_name}")
    endforeach()
    add_custom_command(TARGET SWMMVis POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_SWMMVIS_EXAMPLES_DEST}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_SWMMVIS_EXAMPLES_DEST}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_SWMMVIS_EXAMPLE_INPS} "${_SWMMVIS_EXAMPLES_DEST}/"
        ${_SWMMVIS_EXAMPLE_DIR_CMDS}
        COMMENT "Copy bundled examples into SWMMVis.app/Contents/Resources/examples"
        VERBATIM
    )
else()
    # Linux + Windows: install into the share tree CPack packages.
    # CMAKE_INSTALL_DATAROOTDIR = "share" on all platforms, matching search path #1.
    install(FILES ${_SWMMVIS_EXAMPLE_INPS}
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/openswmmgui/examples
        COMPONENT Runtime
    )
    # No trailing slash — the directory itself is copied under examples/.
    install(DIRECTORY ${_SWMMVIS_EXAMPLE_DIRS}
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/openswmmgui/examples
        COMPONENT Runtime
    )
endif()

# ── Qt and GDAL runtime deployment ─────────────────────────────────────────
# POST_BUILD: makes the build-tree binary runnable without system-installed deps.
# INSTALL:    cmake --install creates a self-contained staging tree for CPack.
#
# macOS  : macdeployqt bundles Qt frameworks + all linked dylibs (GDAL and all
#          transitive deps) into the .app bundle. Runs at POST_BUILD and again
#          at install time on the freshly-installed bundle.
# Windows: windeployqt copies Qt DLLs (POST_BUILD + install); the entire vcpkg
#          bin/ is copied for GDAL and every transitive DLL; the prebuilt
#          openswmm_engine.dll (imported package) is copied explicitly.
# Linux  : vcpkg lib/*.so* are copied next to the executable via a cmake -P
#          helper that follows symlink chains; the prebuilt openswmm_engine.so
#          (imported package) is copied explicitly; RPATH is set to $ORIGIN.
#
# On EVERY platform the engine's runtime-loaded GPU surface-solver plugin
# (openswmm_gpu_omp) is co-located with the engine library: it is dlopen()ed by
# the engine (SurfaceSolverFactory), never link-time, so no deploy tool sees it.
# Placing it beside the engine lib lets the factory's dladdr/GetModuleHandleEx
# search root find it and `auto`-select the OpenMP backend on large meshes.

if(APPLE)
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt
        HINTS "${Qt6_DIR}/../../../bin"
              "${QT_ROOT_DIR}/bin"
              "$ENV{QT_ROOT_DIR}/bin"
    )
    if(MACDEPLOYQT_EXECUTABLE)
        # Capture a real dSYM BEFORE macdeployqt + codesign run, so the
        # debug-map links (N_OSO STABS) to per-.o DWARF are still intact.
        # macdeployqt re-links frameworks (@rpath rewrites) and codesign
        # re-stamps the executable; both can invalidate the embedded debug
        # map and prevent `atos` / `lldb image lookup` from resolving
        # SWMMVis frames to source.  Only emit the dSYM for Debug / RWDI
        # configs where users actually want backtraces — release builds
        # keep the existing fast path.
        find_program(DSYMUTIL_EXECUTABLE dsymutil)
        if(DSYMUTIL_EXECUTABLE)
            add_custom_command(TARGET SWMMVis POST_BUILD
                COMMAND "${DSYMUTIL_EXECUTABLE}"
                        "$<TARGET_FILE:SWMMVis>"
                        -o "$<TARGET_BUNDLE_DIR:SWMMVis>.dSYM"
                COMMENT "dsymutil: producing SWMMVis.app.dSYM for symbolicated backtraces"
                VERBATIM
            )
        endif()
        # -qmldir is REQUIRED, not optional. Unlike windeployqt — which sniffs
        # the QML marker out of the executable and deploys bin/qml unasked —
        # macdeployqt only scans for QML imports when pointed at a source dir,
        # and without it Contents/Resources/qml is never created. MapCanvas then
        # loads qrc:/openswmm/qml/swmmlayer.qml into its QQuickWidget, the
        # `import QtQuick` fails, setSource() leaves status != Ready,
        # rootObject() is null, and all three QSG renderers (mesh, 2D results,
        # 1D network) stay null. Because SWMMLayerItem::paint() skips the
        # element kinds QSG owns, the packaged app then draws no lines, points
        # or polygons at all — a silent, macOS-CI-only blank map. Dev builds hide
        # it: they resolve QtQuick through the Qt install's own import path.
        add_custom_command(TARGET SWMMVis POST_BUILD
            COMMAND "${MACDEPLOYQT_EXECUTABLE}"
                    "$<TARGET_BUNDLE_DIR:SWMMVis>"
                    "-qmldir=${CMAKE_CURRENT_SOURCE_DIR}/resources/qml"
                    -always-overwrite
            COMMENT "macdeployqt: bundling Qt frameworks, QML modules and linked dylibs (incl. GDAL)"
            VERBATIM
        )
    else()
        message(WARNING
            "macdeployqt not found — Qt frameworks will not be bundled. "
            "Set QT_ROOT_DIR or add the Qt bin directory to PATH.")
    endif()

    # Bundle the engine's runtime-loaded GPU surface-solver plugin (Kokkos
    # OpenMP, openswmm_gpu_omp). It is NOT a link-time dependency of SWMMVis —
    # the engine dlopen()s it at runtime — so macdeployqt does not see it and
    # never copies it. Place it next to the engine dylib in Contents/Frameworks
    # (where macdeployqt lands libopenswmm.engine.*.dylib) so the engine's
    # SurfaceSolverFactory, which resolves its search root via dladdr on the
    # loaded engine, discovers it and `auto` selects the OpenMP backend. libomp
    # is the plugin's only non-static, non-system dependency; co-locate it too
    # (the plugin's @loader_path RPATH then resolves it — and the engine itself
    # also needs it). The codesign step below re-signs the bundle including
    # these added dylibs. Source paths come from the prebuilt engine package
    # (OPENSWMMENGINE_INSTALL_DIR, set in cmake/FetchOpenSWMMEngine.cmake).
    set(_swmmvis_frameworks "$<TARGET_BUNDLE_DIR:SWMMVis>/Contents/Frameworks")
    add_custom_command(TARGET SWMMVis POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_swmmvis_frameworks}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${OPENSWMMENGINE_INSTALL_DIR}/lib/libopenswmm_gpu_omp.dylib"
                "${_swmmvis_frameworks}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${OPENSWMMENGINE_INSTALL_DIR}/bin/libomp.dylib"
                "${_swmmvis_frameworks}/"
        # The plugin links Homebrew's libomp by ABSOLUTE path. Left as-is the
        # process loads TWO libomp runtimes (bundle copy via the engine +
        # Homebrew copy via the plugin) and libomp aborts the app on its
        # duplicate-runtime check the moment a big-mesh run loads the plugin.
        # Rewrite the bundled copy to the bundled libomp; each -change is a
        # no-op when that source path isn't present (Apple Silicon vs Intel).
        COMMAND install_name_tool
                -change /opt/homebrew/opt/libomp/lib/libomp.dylib
                        @executable_path/../Frameworks/libomp.dylib
                -change /usr/local/opt/libomp/lib/libomp.dylib
                        @executable_path/../Frameworks/libomp.dylib
                "${_swmmvis_frameworks}/libopenswmm_gpu_omp.dylib"
        COMMENT "Bundle GPU plugin (openswmm_gpu_omp) + libomp into SWMMVis.app/Contents/Frameworks"
        VERBATIM
    )

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
    # Bundle the legacy-engine worker executable (openswmm-legacy-worker). The
    # GUI launches it as a subprocess for isolated legacy (EPA-SWMM) runs — see
    # findLegacyWorker() in src/simulation/simulationrunner.cpp, which looks for
    # it beside the app executable first. It is NOT a link-time dependency of
    # SWMMVis, so no deploy tool copies it. Place it in Contents/MacOS next to
    # SWMMVis. The worker links the legacy engine dylib (which in turn needs
    # libomp) via @rpath; co-locate that dylib in Contents/Frameworks (where
    # libomp already lands above) and add a matching @loader_path/../Frameworks
    # rpath to the worker. copy (not copy_if_different) keeps the worker pristine
    # each build so the add_rpath never duplicates an existing entry. The .6
    # tracks the engine major version (SONAME libopenswmm.legacy.engine.6.dylib).
    # The worker is code-signed here and again by the deep bundle sign below.
    set(_swmmvis_macos "$<TARGET_BUNDLE_DIR:SWMMVis>/Contents/MacOS")
    set(_legacy_worker "${OPENSWMMENGINE_INSTALL_DIR}/bin/openswmm-legacy-worker")
    if(EXISTS "${_legacy_worker}")
        add_custom_command(TARGET SWMMVis POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                    "${_legacy_worker}" "${_swmmvis_macos}/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${OPENSWMMENGINE_INSTALL_DIR}/lib/libopenswmm.legacy.engine.6.dylib"
                    "${_swmmvis_frameworks}/"
            COMMAND install_name_tool -add_rpath "@loader_path/../Frameworks"
                    "${_swmmvis_macos}/openswmm-legacy-worker"
            COMMAND codesign --force --sign "${_codesign_identity}"
                    "${_swmmvis_macos}/openswmm-legacy-worker"
            COMMENT "Bundle legacy-engine worker + legacy dylib into SWMMVis.app"
            VERBATIM
        )
    else()
        message(WARNING
            "openswmm-legacy-worker not found at ${_legacy_worker} — the GUI's "
            "legacy (EPA-SWMM) engine option will fail to start a worker process.")
    endif()

    # Refactored-engine command-line runner (openswmm). Shipped so the solver can
    # be driven without the GUI — batch runs, scripting, CI — from a single
    # install:
    #     /Applications/SWMMVis.app/Contents/MacOS/openswmm model.inp [rpt] [out]
    #
    # Contents/MacOS is where an auxiliary executable belongs in a bundle: it
    # stays inside the signed, notarisable unit and shares the dylibs already
    # deployed in Contents/Frameworks. The engine ships it with an
    # "@loader_path;@loader_path/../lib" rpath, neither of which resolves from
    # Contents/MacOS, so add the Frameworks path the same way the legacy worker
    # does. copy (not copy_if_different) keeps it pristine each build so
    # add_rpath cannot accumulate duplicate entries.
    set(_openswmm_cli "${OPENSWMMENGINE_INSTALL_DIR}/bin/openswmm")
    if(EXISTS "${_openswmm_cli}")
        add_custom_command(TARGET SWMMVis POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                    "${_openswmm_cli}" "${_swmmvis_macos}/"
            COMMAND install_name_tool -add_rpath "@loader_path/../Frameworks"
                    "${_swmmvis_macos}/openswmm"
            COMMAND codesign --force --sign "${_codesign_identity}"
                    "${_swmmvis_macos}/openswmm"
            COMMENT "Bundle engine CLI (openswmm) into SWMMVis.app"
            VERBATIM
        )
    else()
        message(WARNING
            "openswmm CLI not found at ${_openswmm_cli} — the packaged app will "
            "not include a standalone command-line runner.")
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
    # -qmldir must be repeated here for the same reason it is needed above: this
    # is the bundle CPack turns into the shipped DMG, so if QML is missing HERE
    # the released app is the one with the blank map.
    if(MACDEPLOYQT_EXECUTABLE)
        install(CODE
            "execute_process(COMMAND
                \"${MACDEPLOYQT_EXECUTABLE}\"
                \"\${CMAKE_INSTALL_PREFIX}/SWMMVis.app\"
                \"-qmldir=${CMAKE_CURRENT_SOURCE_DIR}/resources/qml\"
                -always-overwrite)"
            COMPONENT Runtime
        )
    endif()

    # Re-sign after install-time macdeployqt. macdeployqt embeds new dylibs (Qt
    # frameworks, GDAL, PROJ and all transitive deps) and rewrites their load
    # commands with install_name_tool, which invalidates every code directory it
    # touches.
    #
    # This rule used to be `codesign --force --sign "-" --options runtime --timestamp`
    # and it produced the DMG that macOS rejects with
    #     "SWMMVis.app is damaged and can't be opened."
    # Three bugs, each of which alone is enough to break the bundle:
    #
    #   1. NO --deep.  codesign then signs only the outer bundle and leaves the
    #      nested Qt plugins unsigned, so it ABORTS with:
    #          SWMMVis.app: code object is not signed at all
    #          In subcomponent: .../SWMMVis.app/Contents/PlugIns/...
    #      (The POST_BUILD rule above already passes --deep — this one forgot.)
    #
    #   2. --timestamp on an AD-HOC identity.  A secure timestamp requires a real
    #      Developer ID; ad-hoc signatures cannot carry one. Likewise --options
    #      runtime (Hardened Runtime) is only meaningful for notarised builds.
    #      Both belong exclusively to the real-identity path.
    #
    #   3. No COMMAND_ERROR_IS_FATAL.  So when codesign failed for (1), CMake
    #      ignored it and CPack wrapped an unsigned bundle into a .dmg. The build
    #      went green and shipped a broken app. Signing failures must be FATAL —
    #      on Apple Silicon an invalid signature means the binary will not load
    #      at all, which is exactly what "damaged" means.
    # NOTE: a plain space-separated STRING, not a list. install(CODE) does textual
    # substitution into a generated CMake script, and a list would expand as
    # "--options;runtime;--timestamp" — one mangled argument, not three.
    set(_codesign_notarize_opts "")
    if(NOT _codesign_identity STREQUAL "-")
        # Real Developer ID: enable Hardened Runtime + secure timestamp so the
        # bundle can be notarised.
        set(_codesign_notarize_opts "--options runtime --timestamp")
    endif()

    install(CODE
        "execute_process(COMMAND
            codesign --force --deep --sign \"${_codesign_identity}\"
            ${_codesign_notarize_opts}
            \"\${CMAKE_INSTALL_PREFIX}/SWMMVis.app\"
            COMMAND_ERROR_IS_FATAL ANY)
         execute_process(COMMAND
            codesign --verify --deep --strict
            \"\${CMAKE_INSTALL_PREFIX}/SWMMVis.app\"
            COMMAND_ERROR_IS_FATAL ANY)
         message(STATUS \"codesign: bundle signed and verified (identity=${_codesign_identity})\")"
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

    # openswmm_engine is a prebuilt IMPORTED package (find_package) — not in
    # vcpkg bin/ — so copy its DLL next to the exe explicitly.
    add_custom_command(TARGET SWMMVis POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:openswmm_engine>"
                "$<TARGET_FILE_DIR:SWMMVis>/"
        COMMENT "Copy openswmm_engine.dll alongside SWMMVis.exe"
        VERBATIM
    )

    # Engine GPU plugin (openswmm_gpu_omp): dlopen()ed by the engine at runtime,
    # so no deploy tool sees it. SurfaceSolverFactory resolves its search root via
    # GetModuleHandleEx on the loaded engine DLL, so the plugin must sit beside
    # openswmm_engine.dll (next to the exe here). The engine package installs the
    # plugin to bin/ on Windows; glob bin/+lib/ to stay layout-agnostic. Also
    # co-locate any libomp the engine ships (the plugin shares the engine's
    # OpenMP runtime; MSVC's vcomp140 is a system DLL and needs no bundling).
    # Absent the plugin, large meshes (>20k cells) fall back to the serial CVODE
    # solver — still hypre/BoomerAMG, just not OpenMP-parallel.
    # Any backend (omp / cuda / hip / sycl) — the engine ships whichever it was
    # built with; bundle whatever is present rather than hardcoding omp.
    file(GLOB _swmmvis_gpu_plugin
        "${OPENSWMMENGINE_INSTALL_DIR}/bin/*openswmm_gpu_*.dll"
        "${OPENSWMMENGINE_INSTALL_DIR}/lib/*openswmm_gpu_*.dll")
    # libomp (OpenMP backend) plus the SUNDIALS runtime the engine/plugin link
    # against — SUNDIALS comes from the ENGINE's vcpkg tree (this project's
    # manifest does not build it), so it must be bundled from the engine
    # package or the deployed engine/plugin fails to load.
    file(GLOB _swmmvis_engine_omp
        "${OPENSWMMENGINE_INSTALL_DIR}/bin/libomp*.dll"
        "${OPENSWMMENGINE_INSTALL_DIR}/lib/libomp*.dll"
        "${OPENSWMMENGINE_INSTALL_DIR}/bin/sundials_*.dll"
        "${OPENSWMMENGINE_INSTALL_DIR}/lib/sundials_*.dll")
    if(_swmmvis_gpu_plugin)
        add_custom_command(TARGET SWMMVis POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${_swmmvis_gpu_plugin} ${_swmmvis_engine_omp}
                    "$<TARGET_FILE_DIR:SWMMVis>/"
            COMMENT "Bundle GPU plugin (openswmm_gpu_omp) alongside SWMMVis.exe"
            VERBATIM
        )
        install(FILES ${_swmmvis_gpu_plugin} ${_swmmvis_engine_omp}
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT Runtime
        )
    else()
        message(WARNING
            "openswmm_gpu_omp plugin not found under ${OPENSWMMENGINE_INSTALL_DIR} "
            "(bin/ or lib/) — large-mesh OpenMP acceleration will be unavailable; "
            "the engine falls back to the serial CVODE+AMG solver.")
    endif()

    # Legacy-engine worker executable: launched as a subprocess by the GUI for
    # isolated legacy (EPA-SWMM) runs. Not a link-time dep of SWMMVis, so copy it
    # and the legacy engine DLL it loads next to the exe (libomp is deployed with
    # the engine above) — where findLegacyWorker() and the Windows DLL loader
    # both look first.
    set(_legacy_worker "${OPENSWMMENGINE_INSTALL_DIR}/bin/openswmm-legacy-worker.exe")
    if(EXISTS "${_legacy_worker}")
        add_custom_command(TARGET SWMMVis POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_legacy_worker}"
                    "$<TARGET_FILE:OpenSWMMEngine::openswmm_legacy_engine>"
                    "$<TARGET_FILE_DIR:SWMMVis>/"
            COMMENT "Bundle legacy-engine worker + legacy DLL alongside SWMMVis.exe"
            VERBATIM
        )
        install(PROGRAMS "${_legacy_worker}"
            DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
        install(IMPORTED_RUNTIME_ARTIFACTS OpenSWMMEngine::openswmm_legacy_engine
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    else()
        message(WARNING
            "openswmm-legacy-worker.exe not found at ${_legacy_worker} — the GUI's "
            "legacy (EPA-SWMM) engine option will fail to start a worker process.")
    endif()

    # Refactored-engine command-line runner, so the solver can be driven without
    # the GUI (batch runs, scripting, CI) from a single install:
    #     openswmm.exe model.inp [rpt] [out]
    # Installed beside SWMMVis.exe, where it picks up the engine DLL and the
    # vcpkg runtime DLLs already deployed there.
    set(_openswmm_cli "${OPENSWMMENGINE_INSTALL_DIR}/bin/openswmm.exe")
    if(EXISTS "${_openswmm_cli}")
        add_custom_command(TARGET SWMMVis POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_openswmm_cli}" "$<TARGET_FILE_DIR:SWMMVis>/"
            COMMENT "Bundle engine CLI (openswmm.exe) alongside SWMMVis.exe"
            VERBATIM
        )
        install(PROGRAMS "${_openswmm_cli}"
                DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
    else()
        message(WARNING
            "openswmm.exe not found at ${_openswmm_cli} — the packaged app will "
            "not include a standalone command-line runner.")
    endif()

    install(TARGETS SWMMVis     RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    # openswmm_engine is now a prebuilt IMPORTED target (find_package), so it
    # cannot be install(TARGETS)'d — install its runtime artifact (the DLL).
    install(IMPORTED_RUNTIME_ARTIFACTS OpenSWMMEngine::openswmm_engine
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

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

    # openswmm_engine is a prebuilt IMPORTED package (find_package) — copy its
    # .so next to the exe explicitly.
    add_custom_command(TARGET SWMMVis POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:openswmm_engine>"
                "$<TARGET_FILE_DIR:SWMMVis>/"
        COMMENT "Copy openswmm_engine shared library alongside SWMMVis"
        VERBATIM
    )

    # Engine GPU plugin (openswmm_gpu_omp): dlopen()ed by the engine at runtime,
    # so no deploy tool sees it. SurfaceSolverFactory resolves its search root via
    # dladdr on the loaded engine .so, so the plugin must sit beside the engine
    # lib — next to the exe in the build tree, and in LIBDIR in the install tree
    # (where the engine .so installs, and where the AppImage's `cp -r dist/*`
    # lands it under usr/lib so linuxdeploy then resolves the plugin's own deps,
    # e.g. libgomp). The plugin otherwise shares the engine's already-loaded
    # OpenMP runtime. The engine package installs the plugin to lib/ on Linux;
    # glob lib/+bin/ to stay layout-agnostic. Absent the plugin, large meshes
    # (>20k cells) fall back to the serial CVODE solver — still hypre/BoomerAMG.
    file(GLOB _swmmvis_gpu_plugin
        "${OPENSWMMENGINE_INSTALL_DIR}/lib/*openswmm_gpu_omp*"
        "${OPENSWMMENGINE_INSTALL_DIR}/bin/*openswmm_gpu_omp*")
    if(_swmmvis_gpu_plugin)
        add_custom_command(TARGET SWMMVis POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${_swmmvis_gpu_plugin}
                    "$<TARGET_FILE_DIR:SWMMVis>/"
            COMMENT "Bundle GPU plugin (openswmm_gpu_omp) alongside the engine lib"
            VERBATIM
        )
        install(FILES ${_swmmvis_gpu_plugin}
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT Runtime
        )
    else()
        message(WARNING
            "openswmm_gpu_omp plugin not found under ${OPENSWMMENGINE_INSTALL_DIR} "
            "(lib/ or bin/) — large-mesh OpenMP acceleration will be unavailable; "
            "the engine falls back to the serial CVODE+AMG solver.")
    endif()

    # Legacy-engine worker executable (subprocess for isolated legacy (EPA-SWMM)
    # runs). Not a link-time dep of SWMMVis, so copy it and the legacy engine .so
    # it loads next to the exe (libgomp is handled by linuxdeploy/AppImage, as for
    # the GPU plugin). findLegacyWorker() looks beside the binary, and the
    # worker's $ORIGIN RUNPATH resolves the co-located .so in the build tree
    # ($ORIGIN/../lib → LIBDIR in the install tree).
    set(_legacy_worker "${OPENSWMMENGINE_INSTALL_DIR}/bin/openswmm-legacy-worker")
    if(EXISTS "${_legacy_worker}")
        add_custom_command(TARGET SWMMVis POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_legacy_worker}"
                    "$<TARGET_FILE:OpenSWMMEngine::openswmm_legacy_engine>"
                    "$<TARGET_FILE_DIR:SWMMVis>/"
            COMMENT "Bundle legacy-engine worker + legacy .so alongside SWMMVis"
            VERBATIM
        )
    else()
        message(WARNING
            "openswmm-legacy-worker not found at ${_legacy_worker} — the GUI's "
            "legacy (EPA-SWMM) engine option will fail to start a worker process.")
    endif()

    # Refactored-engine command-line runner, so the solver can be driven without
    # the GUI (batch runs, scripting, CI) from a single install:
    #     openswmm model.inp [rpt] [out]
    # The engine builds it with an "$ORIGIN;$ORIGIN/../lib" RUNPATH, which
    # resolves the co-located .so in the build tree and LIBDIR in the install
    # tree — the same layout SWMMVis and the legacy worker already rely on.
    set(_openswmm_cli "${OPENSWMMENGINE_INSTALL_DIR}/bin/openswmm")
    if(EXISTS "${_openswmm_cli}")
        add_custom_command(TARGET SWMMVis POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_openswmm_cli}" "$<TARGET_FILE_DIR:SWMMVis>/"
            COMMENT "Bundle engine CLI (openswmm) alongside SWMMVis"
            VERBATIM
        )
    else()
        message(WARNING
            "openswmm CLI not found at ${_openswmm_cli} — the packaged app will "
            "not include a standalone command-line runner.")
    endif()

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
    # openswmm_engine is now a prebuilt IMPORTED target (find_package), so it
    # cannot be install(TARGETS)'d — install its runtime artifact (the .so).
    install(IMPORTED_RUNTIME_ARTIFACTS OpenSWMMEngine::openswmm_engine
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
    # Legacy worker → BINDIR (beside SWMMVis for findLegacyWorker); legacy .so →
    # LIBDIR (beside the engine .so, resolved via the worker's $ORIGIN/../lib).
    if(EXISTS "${_legacy_worker}")
        install(PROGRAMS "${_legacy_worker}"
                DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(IMPORTED_RUNTIME_ARTIFACTS OpenSWMMEngine::openswmm_legacy_engine
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()
    # Engine CLI → BINDIR (beside SWMMVis); its $ORIGIN/../lib RUNPATH finds the
    # engine .so installed in LIBDIR above.
    if(EXISTS "${_openswmm_cli}")
        install(PROGRAMS "${_openswmm_cli}"
                DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
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
# Monolithic: stage every component into ONE prefix. The install rules span
# two components (Runtime + Unspecified, e.g. install(TARGETS SWMMVis) carries
# no COMPONENT), and per-component staging breaks the install-time windeployqt
# step — it runs in the Runtime prefix where SWMMVis.exe (Unspecified) does
# not exist, so the package ships without Qt DLLs.
set(CPACK_MONOLITHIC_INSTALL ON)
set(CPACK_PACKAGE_NAME              "SWMMVis")
set(CPACK_PACKAGE_VENDOR            "hydrocouple.org")
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
