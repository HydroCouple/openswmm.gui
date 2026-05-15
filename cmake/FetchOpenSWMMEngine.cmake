# cmake/FetchOpenSWMMEngine.cmake
#
# Provides the `openswmm_engine` target — the openswmm.engine C++/C simulation
# engine consumed by the GUI. The engine is a **hard dependency** of
# SWMMVis: without it there is no `.inp` parsing, no simulation, and no
# results to visualize. Failing to locate/fetch the engine is a fatal
# configure-time error.
#
# Resolution order (first match wins):
#   1. Explicit `-DOPENSWMMENGINE_SOURCE_DIR=/path/to/checkout`  (user override)
#   2. Sibling checkout at `${CMAKE_SOURCE_DIR}/../openswmm.engine`
#      (current dev workflow)
#   3. GitHub fetch via FetchContent
#
# Cache variables (override at configure time with -D…):
#   OPENSWMMENGINE_SOURCE_DIR     local checkout path (empty = auto/fetch)
#   OPENSWMMENGINE_GIT_REPOSITORY upstream URL
#   OPENSWMMENGINE_GIT_TAG        branch / tag / commit SHA
#
# After include(): the `openswmm_engine` target is available and
# `HAVE_OPENSWMMENGINE` is added to compile definitions.

include_guard(GLOBAL)
include(FetchContent)

set(OPENSWMMENGINE_SOURCE_DIR "" CACHE PATH
    "Optional path to a local openswmm.engine checkout (overrides the GitHub fetch).")
set(OPENSWMMENGINE_GIT_REPOSITORY
    "https://github.com/HydroCouple/openswmm.engine.git" CACHE STRING
    "Git URL to fetch openswmm.engine from.")
set(OPENSWMMENGINE_GIT_TAG "swmm6_rel" CACHE STRING
    "Branch / tag / commit-SHA of openswmm.engine to use.")

# Disable the engine's tests / cli / python bindings inside this build to
# keep configure + compile time reasonable; we only need the engine library.
set(OPENSWMM_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(OPENSWMM_BUILD_CLI      OFF CACHE BOOL "" FORCE)
set(OPENSWMM_BUILD_PYTHON   OFF CACHE BOOL "" FORCE)
set(OPENSWMM_BUILD_LEGACY   ON  CACHE BOOL "" FORCE)
# Enable GeoPackage I/O by default — the plugin registers *.gpkg filters
# via discover_all_filters() which the GUI's FileFilterRegistry surfaces
# in Open / Save As / Plugins dialogs automatically.
set(OPENSWMM_WITH_GEOPACKAGE ON CACHE BOOL "" FORCE)

# Backward-compat: honor the older OpenSWMMCore / OpenSWMMGUI cache variable
# names so existing presets and dev scripts keep working.
if(NOT OPENSWMMENGINE_SOURCE_DIR AND OPENSWMMCORE_SOURCE_DIR)
    set(OPENSWMMENGINE_SOURCE_DIR "${OPENSWMMCORE_SOURCE_DIR}")
endif()
if(NOT OPENSWMMENGINE_SOURCE_DIR AND OPENSWMMGUI_SOURCE_DIR)
    set(OPENSWMMENGINE_SOURCE_DIR "${OPENSWMMGUI_SOURCE_DIR}")
endif()
if(OPENSWMMCORE_GIT_REPOSITORY)
    set(OPENSWMMENGINE_GIT_REPOSITORY "${OPENSWMMCORE_GIT_REPOSITORY}")
endif()
if(OPENSWMMCORE_GIT_TAG)
    set(OPENSWMMENGINE_GIT_TAG "${OPENSWMMCORE_GIT_TAG}")
endif()

# Sibling-checkout fallback — the engine lives at ../openswmm.engine.
if(NOT OPENSWMMENGINE_SOURCE_DIR
        AND EXISTS "${CMAKE_SOURCE_DIR}/../openswmm.engine/CMakeLists.txt")
    set(OPENSWMMENGINE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../openswmm.engine")
endif()

if(OPENSWMMENGINE_SOURCE_DIR AND EXISTS "${OPENSWMMENGINE_SOURCE_DIR}/CMakeLists.txt")
    message(STATUS "openswmm.engine: using local checkout at ${OPENSWMMENGINE_SOURCE_DIR}")
    add_subdirectory("${OPENSWMMENGINE_SOURCE_DIR}" openswmm_engine)
else()
    message(STATUS "openswmm.engine: fetching ${OPENSWMMENGINE_GIT_REPOSITORY} @ ${OPENSWMMENGINE_GIT_TAG}")
    FetchContent_Declare(openswmm_engine
        GIT_REPOSITORY "${OPENSWMMENGINE_GIT_REPOSITORY}"
        GIT_TAG        "${OPENSWMMENGINE_GIT_TAG}"
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(openswmm_engine)
endif()

if(NOT TARGET openswmm_engine)
    message(FATAL_ERROR
        "openswmm_engine target was not created by the openswmm.engine checkout/fetch.\n"
        "If you set -DOPENSWMMENGINE_SOURCE_DIR, confirm it points at a directory whose\n"
        "CMakeLists.txt creates the `openswmm_engine` library target.")
endif()

# The engine is a hard dependency — the GUI sources unconditionally use
# the engine's C ABI, no #ifdef guards.
add_compile_definitions(HAVE_OPENSWMMENGINE HAVE_OPENSWMMCORE)

