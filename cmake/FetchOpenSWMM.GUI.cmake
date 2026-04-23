# cmake/FetchOPENSWMMGUI.cmake
#
# Provides the `openswmm_engine` target.
#
# Resolution order (first match wins):
#   1. Explicit `-DOPENSWMMGUI_SOURCE_DIR=/path/to/checkout`         (user override)
#   2. Sibling checkout at `${CMAKE_SOURCE_DIR}/../OPENSWMMGUI`      (current dev workflow)
#   3. GitHub fetch via FetchContent
#
# Cache variables (override at configure time with -D…):
#   OPENSWMMGUI_SOURCE_DIR     local checkout path (empty = use auto/fetch)
#   OPENSWMMGUI_GIT_REPOSITORY upstream URL
#   OPENSWMMGUI_GIT_TAG        branch / tag / commit SHA
#
# After include(): the `openswmm_engine` target is available and
# `HAVE_OPENSWMMGUI` is added to compile definitions. Failure to
# create the engine target is a fatal configure error.

include_guard(GLOBAL)
include(FetchContent)

set(OPENSWMMGUI_SOURCE_DIR "" CACHE PATH
    "Optional path to a local OPENSWMMGUI checkout (overrides the GitHub fetch).")
set(OPENSWMMGUI_GIT_REPOSITORY
    "https://github.com/HydroCouple/openswmm.gui.git" CACHE STRING
    "Git URL to fetch the OPENSWMMGUI engine from.")
set(OPENSWMMGUI_GIT_TAG "swmm6_rel" CACHE STRING
    "Branch / tag / commit-SHA of OPENSWMMGUI to use.")

# Disable OPENSWMMGUI's tests / cli / python bindings inside this build to
# keep configure + compile time reasonable; we only need the engine library.
set(OPENSWMM_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(OPENSWMM_BUILD_CLI      OFF CACHE BOOL "" FORCE)
set(OPENSWMM_BUILD_PYTHON   OFF CACHE BOOL "" FORCE)
set(OPENSWMM_BUILD_LEGACY   ON  CACHE BOOL "" FORCE)

if(NOT OPENSWMMGUI_SOURCE_DIR
        AND EXISTS "${CMAKE_SOURCE_DIR}/../openswmm.gui/CMakeLists.txt")
    set(OPENSWMMGUI_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../openswmm.gui")
endif()

if(OPENSWMMGUI_SOURCE_DIR AND EXISTS "${OPENSWMMGUI_SOURCE_DIR}/CMakeLists.txt")
    message(STATUS "OPENSWMMGUI: using local checkout at ${OPENSWMMGUI_SOURCE_DIR}")
    add_subdirectory("${OPENSWMMGUI_SOURCE_DIR}" openswmm_gui)
    add_compile_definitions(HAVE_OPENSWMMGUI_build EXCLUDE_FROM_ALL)
else()
    message(STATUS "OPENSWMMGUI: fetching ${OPENSWMMGUI_GIT_REPOSITORY} @ ${OPENSWMMGUI_GIT_TAG}")
    FetchContent_Declare(OPENSWMMGUI
        GIT_REPOSITORY "${OPENSWMMGUI_GIT_REPOSITORY}"
        GIT_TAG        "${OPENSWMMGUI_GIT_TAG}"
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(OPENSWMMGUI)
endif()

if(NOT TARGET openswmm_engine)
    message(FATAL_ERROR
        "openswmm_engine target was not created by the OPENSWMMGUI checkout/fetch.\n"
        "If you set -DOPENSWMMGUI_SOURCE_DIR, confirm it points at a directory whose\n"
        "CMakeLists.txt creates the `openswmm_engine` library target.")
endif()
add_compile_definitions(HAVE_OPENSWMMGUI)
