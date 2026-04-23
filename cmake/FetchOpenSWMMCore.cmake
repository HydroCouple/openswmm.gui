# cmake/FetchOpenSWMMCore.cmake
#
# Provides the `openswmm_engine` target.
#
# Resolution order (first match wins):
#   1. Explicit `-DOPENSWMMCORE_SOURCE_DIR=/path/to/checkout`         (user override)
#   2. Sibling checkout at `${CMAKE_SOURCE_DIR}/../OpenSWMMCore`      (current dev workflow)
#   3. GitHub fetch via FetchContent
#
# Cache variables (override at configure time with -D…):
#   OPENSWMMCORE_SOURCE_DIR     local checkout path (empty = use auto/fetch)
#   OPENSWMMCORE_GIT_REPOSITORY upstream URL
#   OPENSWMMCORE_GIT_TAG        branch / tag / commit SHA
#
# After include(): the `openswmm_engine` target is available and
# `HAVE_OPENSWMMCORE` is added to compile definitions. Failure to
# create the engine target is a fatal configure error.

include_guard(GLOBAL)
include(FetchContent)

set(OPENSWMMCORE_SOURCE_DIR "" CACHE PATH
    "Optional path to a local OpenSWMMCore checkout (overrides the GitHub fetch).")
set(OPENSWMMCORE_GIT_REPOSITORY
    "https://github.com/HydroCouple/Stormwater-Management-Model.git" CACHE STRING
    "Git URL to fetch the OpenSWMMCore engine from.")
set(OPENSWMMCORE_GIT_TAG "swmm6_rel" CACHE STRING
    "Branch / tag / commit-SHA of OpenSWMMCore to use.")

# Disable OpenSWMMCore's tests / cli / python bindings inside this build to
# keep configure + compile time reasonable; we only need the engine library.
set(OPENSWMM_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(OPENSWMM_BUILD_CLI      OFF CACHE BOOL "" FORCE)
set(OPENSWMM_BUILD_PYTHON   OFF CACHE BOOL "" FORCE)
set(OPENSWMM_BUILD_LEGACY   ON  CACHE BOOL "" FORCE)

if(NOT OPENSWMMCORE_SOURCE_DIR
        AND EXISTS "${CMAKE_SOURCE_DIR}/../OpenSWMMCore/CMakeLists.txt")
    set(OPENSWMMCORE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../OpenSWMMCore")
endif()

if(OPENSWMMCORE_SOURCE_DIR AND EXISTS "${OPENSWMMCORE_SOURCE_DIR}/CMakeLists.txt")
    message(STATUS "OpenSWMMCore: using local checkout at ${OPENSWMMCORE_SOURCE_DIR}")
    add_subdirectory("${OPENSWMMCORE_SOURCE_DIR}" openswmmcore_build EXCLUDE_FROM_ALL)
else()
    message(STATUS "OpenSWMMCore: fetching ${OPENSWMMCORE_GIT_REPOSITORY} @ ${OPENSWMMCORE_GIT_TAG}")
    FetchContent_Declare(OpenSWMMCore
        GIT_REPOSITORY "${OPENSWMMCORE_GIT_REPOSITORY}"
        GIT_TAG        "${OPENSWMMCORE_GIT_TAG}"
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(OpenSWMMCore)
endif()

if(NOT TARGET openswmm_engine)
    message(FATAL_ERROR
        "openswmm_engine target was not created by the OpenSWMMCore checkout/fetch.\n"
        "If you set -DOPENSWMMCORE_SOURCE_DIR, confirm it points at a directory whose\n"
        "CMakeLists.txt creates the `openswmm_engine` library target.")
endif()
add_compile_definitions(HAVE_OPENSWMMCORE)
