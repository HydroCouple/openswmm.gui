# cmake/EngineVersions.cmake
#
# Pins and fetches the additional SWMM engine versions the GUI ships as
# subprocess workers (MULTI_ENGINE_VERSION_SUPPORT_PLAN Phase 2 / V2 M-2).
# The 6.x engine itself is NOT handled here — see FetchOpenSWMMEngine.cmake.
#
# One block per version. Each fetches SOURCE ONLY (never runs the fetched
# tree's CMake) and hands the tree to add_legacy_engine_worker(), which
# compiles the solver and worker statically into one versioned executable
# that findLegacyWorker() (src/simulation/simulationrunner.cpp) looks up by
# name: openswmm-legacy-worker-<version>.
#
# ── SWMM 5.2.4 (EPA) ────────────────────────────────────────────────────────
# Source: https://github.com/HydroCouple/openswmm.engine, branch build-v5.2.4,
# at the pinned tag below. Tag, never a branch name: a GUI build must not
# change without a GUI commit. Bump OPENSWMM_ENGINE_524_TAG to move.
#
# Resolution order (first match wins):
#   1. -DFETCHCONTENT_SOURCE_DIR_OPENSWMM_ENGINE_524=<path>   (explicit; CI
#      passes its own checkout so the build never touches the network; pass
#      it EMPTY to force the GitHub fetch and skip sibling detection)
#   2. sibling checkout: ../openswmm.engine.v524  or
#      ../swmm5.2.4/openswmm.engine.5.2.4          (local/offline dev)
#   3. shallow clone of the tag from GitHub
#
# Why the fetched CMake is never run: the branch's own CMakeLists.txt is a
# top-level project, not a parent-safe subproject — it FORCEs
# CMAKE_INSTALL_PREFIX, include()s CPack and InstallRequiredSystemLibraries
# unguarded, POST_BUILD-copies its binaries into the PARENT's bin/, and
# file(COPY)s a generated header into its own source tree. Populating the
# source and compiling it ourselves sidesteps all of that and also lets the
# worker carry the GUI's FP policy (LegacyEngineWorker.cmake).
#
# Cache variables:
#   SWMMVIS_ENABLE_ENGINE_524   build + bundle the 5.2.4 worker (default ON)
#   OPENSWMM_ENGINE_524_TAG     pinned tag on the build-v5.2.4 branch

include_guard(GLOBAL)
include(FetchContent)

option(SWMMVIS_ENABLE_ENGINE_524 "Build and bundle the EPA SWMM 5.2.4 legacy worker" ON)
if(NOT SWMMVIS_ENABLE_ENGINE_524)
    message(STATUS "Engine 5.2.4: disabled (SWMMVIS_ENABLE_ENGINE_524=OFF)")
    return()
endif()

set(OPENSWMM_ENGINE_524_TAG "v5.2.4-swmmvis.1" CACHE STRING
    "Pinned tag of HydroCouple/openswmm.engine (branch build-v5.2.4) the 5.2.4 worker is built from.")

# Sibling checkout wins over the network fetch. Both layouts the plan and the
# working tree use are accepted. A checkout only counts if it carries the
# worker source, i.e. it is at or past the tag above.
if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_OPENSWMM_ENGINE_524)
    foreach(_e524_cand
            "${CMAKE_SOURCE_DIR}/../openswmm.engine.v524"
            "${CMAKE_SOURCE_DIR}/../swmm5.2.4/openswmm.engine.5.2.4")
        if(EXISTS "${_e524_cand}/src/solver/swmm5.c"
           AND EXISTS "${_e524_cand}/src/worker/main.cpp")
            get_filename_component(_e524_cand "${_e524_cand}" ABSOLUTE)
            set(FETCHCONTENT_SOURCE_DIR_OPENSWMM_ENGINE_524 "${_e524_cand}" CACHE PATH
                "Sibling checkout of openswmm.engine @ build-v5.2.4 (overrides the GitHub fetch)")
            break()
        endif()
    endforeach()
endif()

# SOURCE_SUBDIR names a directory that does not exist, so
# FetchContent_MakeAvailable populates the tree (clone/checkout, or the
# sibling override) but never add_subdirectory()s it. This is the documented
# source-only idiom for CMake 3.21 … 4.x; the older single-argument
# FetchContent_Populate(<name>) is deprecated since 3.30 (CMP0169).
FetchContent_Declare(openswmm_engine_524
    GIT_REPOSITORY https://github.com/HydroCouple/openswmm.engine.git
    GIT_TAG        "${OPENSWMM_ENGINE_524_TAG}"
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _swmmvis_source_only_
)
FetchContent_MakeAvailable(openswmm_engine_524)
# openswmm_engine_524_SOURCE_DIR now holds the populated tree.

set(_e524_desc "")
find_package(Git QUIET)
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${openswmm_engine_524_SOURCE_DIR}" describe --tags --always --dirty
        OUTPUT_VARIABLE _e524_desc OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
message(STATUS "Engine 5.2.4: ${openswmm_engine_524_SOURCE_DIR} (${_e524_desc}; pin ${OPENSWMM_ENGINE_524_TAG})")

include(LegacyEngineWorker)
add_legacy_engine_worker(VERSION 5.2.4 SOURCE_DIR "${openswmm_engine_524_SOURCE_DIR}")

# Lets the engine combos offer "5.2.4" only when the worker is actually built.
add_compile_definitions(SWMMVIS_HAVE_ENGINE_524)
