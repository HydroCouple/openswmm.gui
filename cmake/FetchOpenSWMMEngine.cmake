# cmake/FetchOpenSWMMEngine.cmake
#
# Provides the `openswmm_engine` target — the openswmm.engine C++/C simulation
# engine consumed by the GUI. The engine is a **hard dependency** of SWMMVis:
# without it there is no `.inp` parsing, no simulation, and no results to
# visualize.
#
# Consumption model: the engine is built+installed STANDALONE (its own vcpkg
# manifest — kokkos + sundials[kokkos], default GPU plugin) and the GUI links
# the resulting prebuilt SHARED library via find_package(OpenSWMMEngine). This
# keeps the engine's compilation identical whether built standalone or for the
# GUI, and keeps the GUI's own vcpkg tree free of the engine's heavy deps:
# SUNDIALS/HDF5/OpenMP/Kokkos are PRIVATE to the engine SHARED lib (baked in,
# not linked or found by consumers). GPU support ships as the engine's
# runtime-loaded plugin (openswmm_gpu_omp), co-located beside the engine lib;
# nothing Kokkos is linked into the GUI on any platform.
#
# Build + install the engine FIRST, e.g. on macOS:
#   cd ../openswmm.engine
#   cmake -S . -B build/darwin -DCMAKE_INSTALL_PREFIX=$PWD/install/Darwin \
#         --preset Darwin            # (or pass the vcpkg toolchain explicitly)
#   cmake --build   build/darwin
#   cmake --install build/darwin
#
# Cache variables (override at configure time with -D…):
#   OPENSWMMENGINE_INSTALL_DIR   install prefix of the prebuilt engine package
#                                (the dir containing lib/cmake/OpenSWMMEngine).
#                                Default: ../openswmm.engine/install/<System>
#
# After include(): the `openswmm_engine` target (an alias of the imported
# OpenSWMMEngine::openswmm_engine) is available and HAVE_OPENSWMMENGINE is
# added to compile definitions.

include_guard(GLOBAL)

# Sibling-checkout install convention: ../openswmm.engine/install/<System>.
# <System> mirrors the engine's CMake preset names (Darwin / Linux / Windows).
set(OPENSWMMENGINE_INSTALL_DIR
    "${CMAKE_SOURCE_DIR}/../openswmm.engine/install/${CMAKE_HOST_SYSTEM_NAME}"
    CACHE PATH
    "Install prefix of the prebuilt openswmm.engine package (contains lib/cmake/OpenSWMMEngine).")

if(OPENSWMMENGINE_INSTALL_DIR)
    list(PREPEND CMAKE_PREFIX_PATH "${OPENSWMMENGINE_INSTALL_DIR}")
endif()

find_package(OpenSWMMEngine CONFIG REQUIRED)

if(NOT TARGET OpenSWMMEngine::openswmm_engine)
    message(FATAL_ERROR
        "find_package(OpenSWMMEngine) loaded a config but did not define the\n"
        "OpenSWMMEngine::openswmm_engine target. Rebuild/reinstall the engine.")
endif()

message(STATUS "openswmm.engine: using prebuilt package at ${OpenSWMMEngine_DIR}")

# ── Engine API compatibility gate (issue #8) ─────────────────────────────────
# find_package succeeds against ANY installed engine, including one built from
# a branch that predates entry points the GUI calls unconditionally. What the
# builder then sees is a wall of `error C3861: 'swmm_node_get_rim_depth':
# identifier not found` several minutes into compilation, with nothing naming
# the actual cause.
#
# Issue #8 was exactly that: an engine built from `develop`, which is ~300
# commits behind `swmm6_rel` and predates two 2026-08-13 commits — dfcd4d12
# (rim depth on [VIRTUAL_JUNCTIONS]) and 5ca78b70 (in-place rename for snow
# packs, aquifers, inlets, streets). One sentinel per commit, since an engine
# pinned between the two would satisfy only one.
#
# The check reads the installed headers rather than compiling a probe: it costs
# nothing, works before any compiler feature test, and reports every missing
# symbol at once instead of one per rebuild. If the headers cannot be located
# the gate stays quiet — an unfamiliar install layout should not block a build
# that may well be fine.
set(_oe_api_sentinels
    "openswmm_nodes.h:swmm_node_get_rim_depth"
    "openswmm_infrastructure.h:swmm_street_rename"
)
get_target_property(_oe_incdirs OpenSWMMEngine::openswmm_engine
                    INTERFACE_INCLUDE_DIRECTORIES)
set(_oe_missing "")
set(_oe_checked FALSE)
foreach(_sentinel IN LISTS _oe_api_sentinels)
    string(REPLACE ":" ";" _parts "${_sentinel}")
    list(GET _parts 0 _hdr)
    list(GET _parts 1 _sym)
    unset(_oe_hdr_path CACHE)
    find_path(_oe_hdr_path "openswmm/engine/${_hdr}"
              HINTS ${_oe_incdirs} "${OPENSWMMENGINE_INSTALL_DIR}/include"
              NO_DEFAULT_PATH)
    if(_oe_hdr_path)
        set(_oe_checked TRUE)
        file(READ "${_oe_hdr_path}/openswmm/engine/${_hdr}" _oe_hdr_text)
        string(FIND "${_oe_hdr_text}" "${_sym}" _oe_found)
        if(_oe_found EQUAL -1)
            list(APPEND _oe_missing "${_sym}  (expected in ${_hdr})")
        endif()
    endif()
endforeach()
if(_oe_checked AND _oe_missing)
    string(REPLACE ";" "\n    " _oe_missing_text "${_oe_missing}")
    message(FATAL_ERROR
        "The installed openswmm.engine is too old for this GUI.\n"
        "\n"
        "Missing C API entry points the GUI calls unconditionally:\n"
        "    ${_oe_missing_text}\n"
        "\n"
        "Engine package: ${OpenSWMMEngine_DIR}\n"
        "\n"
        "The GUI builds against the engine's `swmm6_rel` branch, not `develop`\n"
        "(see the dependency table in README.md). Rebuild and reinstall the\n"
        "engine from that branch:\n"
        "\n"
        "    cd ../openswmm.engine && git checkout swmm6_rel\n"
        "    cmake -S . -B build --preset <Darwin|Linux|Windows> \\\n"
        "          -DCMAKE_INSTALL_PREFIX=$PWD/install/${CMAKE_HOST_SYSTEM_NAME}\n"
        "    cmake --build build && cmake --install build\n")
endif()
unset(_oe_hdr_path CACHE)

# Unnamespaced alias so the rest of this project keeps referring to
# `openswmm_engine` (target_link_libraries, $<TARGET_FILE:…> bundle copies,
# include propagation) exactly as it did under the previous add_subdirectory
# build — no churn at the use sites.
if(NOT TARGET openswmm_engine)
    add_library(openswmm_engine ALIAS OpenSWMMEngine::openswmm_engine)
endif()

# The engine is a hard dependency — the GUI sources unconditionally use the
# engine's C ABI, no #ifdef guards.
add_compile_definitions(HAVE_OPENSWMMENGINE)
