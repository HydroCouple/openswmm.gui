# cmake/FetchOpenSWMMCore.cmake
#
# Provides the `openswmm_engine` target — the OpenSWMMCore C++/C simulation
# engine consumed by the GUI. The engine is a **hard dependency** of
# SWMMVis: without it there is no `.inp` parsing, no simulation, and no
# results to visualize. Failing to locate/fetch the engine is a fatal
# configure-time error.
#
# Resolution order (first match wins):
#   1. Explicit `-DOPENSWMMCORE_SOURCE_DIR=/path/to/checkout`  (user override)
#   2. Sibling checkout at `${CMAKE_SOURCE_DIR}/../openswmm.engine`
#      (current dev workflow)
#   3. GitHub fetch via FetchContent
#
# Cache variables (override at configure time with -D…):
#   OPENSWMMCORE_SOURCE_DIR     local checkout path (empty = auto/fetch)
#   OPENSWMMCORE_GIT_REPOSITORY upstream URL
#   OPENSWMMCORE_GIT_TAG        branch / tag / commit SHA
#
# After include(): the `openswmm_engine` target is available and
# `HAVE_OPENSWMMCORE` is added to compile definitions.

include_guard(GLOBAL)
include(FetchContent)

set(OPENSWMMCORE_SOURCE_DIR "" CACHE PATH
    "Optional path to a local openswmm.engine checkout (overrides the GitHub fetch).")
set(OPENSWMMCORE_GIT_REPOSITORY
    "https://github.com/HydroCouple/openswmm.engine.git" CACHE STRING
    "Git URL to fetch OpenSWMMCore from.")
set(OPENSWMMCORE_GIT_TAG "main" CACHE STRING
    "Branch / tag / commit-SHA of openswmm.engine to use.")

# Disable the engine's tests / cli / python bindings inside this build to
# keep configure + compile time reasonable; we only need the engine library.
set(OPENSWMM_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(OPENSWMM_BUILD_CLI      OFF CACHE BOOL "" FORCE)
set(OPENSWMM_BUILD_PYTHON   OFF CACHE BOOL "" FORCE)
set(OPENSWMM_BUILD_LEGACY   ON  CACHE BOOL "" FORCE)

# Backward-compat: honor the older, misnamed OPENSWMMGUI_SOURCE_DIR cache
# variable so existing CMakePresets / dev scripts keep working.
if(NOT OPENSWMMCORE_SOURCE_DIR AND OPENSWMMGUI_SOURCE_DIR)
    set(OPENSWMMCORE_SOURCE_DIR "${OPENSWMMGUI_SOURCE_DIR}")
endif()

# Sibling-checkout fallback — the engine lives at ../openswmm.engine.
if(NOT OPENSWMMCORE_SOURCE_DIR
        AND EXISTS "${CMAKE_SOURCE_DIR}/../openswmm.engine/CMakeLists.txt")
    set(OPENSWMMCORE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../openswmm.engine")
endif()

if(OPENSWMMCORE_SOURCE_DIR AND EXISTS "${OPENSWMMCORE_SOURCE_DIR}/CMakeLists.txt")
    message(STATUS "OpenSWMMCore: using local checkout at ${OPENSWMMCORE_SOURCE_DIR}")
    add_subdirectory("${OPENSWMMCORE_SOURCE_DIR}" openswmm_engine)
else()
    message(STATUS "OpenSWMMCore: fetching ${OPENSWMMCORE_GIT_REPOSITORY} @ ${OPENSWMMCORE_GIT_TAG}")
    FetchContent_Declare(openswmm_core
        GIT_REPOSITORY "${OPENSWMMCORE_GIT_REPOSITORY}"
        GIT_TAG        "${OPENSWMMCORE_GIT_TAG}"
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(openswmm_core)
endif()

if(NOT TARGET openswmm_engine)
    message(FATAL_ERROR
        "openswmm_engine target was not created by the OpenSWMMCore checkout/fetch.\n"
        "If you set -DOPENSWMMCORE_SOURCE_DIR, confirm it points at a directory whose\n"
        "CMakeLists.txt creates the `openswmm_engine` library target.")
endif()

# The engine is a hard dependency — the GUI sources unconditionally use
# the engine's C ABI, no #ifdef guards.
add_compile_definitions(HAVE_OPENSWMMCORE)
