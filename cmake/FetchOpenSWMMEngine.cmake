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
# OpenSWMMEngine::openswmm_engine) is available and HAVE_OPENSWMMENGINE /
# HAVE_OPENSWMMCORE are added to compile definitions.

include_guard(GLOBAL)

# Sibling-checkout install convention: ../openswmm.engine/install/<System>.
# <System> mirrors the engine's CMake preset names (Darwin / Linux / Windows).
set(OPENSWMMENGINE_INSTALL_DIR
    "${CMAKE_SOURCE_DIR}/../openswmm.engine/install/${CMAKE_HOST_SYSTEM_NAME}"
    CACHE PATH
    "Install prefix of the prebuilt openswmm.engine package (contains lib/cmake/OpenSWMMEngine).")

# Backward-compat: honor older override variable names if a user set them.
if(OPENSWMMCORE_INSTALL_DIR AND NOT OPENSWMMENGINE_INSTALL_DIR)
    set(OPENSWMMENGINE_INSTALL_DIR "${OPENSWMMCORE_INSTALL_DIR}")
endif()

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

# Unnamespaced alias so the rest of this project keeps referring to
# `openswmm_engine` (target_link_libraries, $<TARGET_FILE:…> bundle copies,
# include propagation) exactly as it did under the previous add_subdirectory
# build — no churn at the use sites.
if(NOT TARGET openswmm_engine)
    add_library(openswmm_engine ALIAS OpenSWMMEngine::openswmm_engine)
endif()

# The engine is a hard dependency — the GUI sources unconditionally use the
# engine's C ABI, no #ifdef guards.
add_compile_definitions(HAVE_OPENSWMMENGINE HAVE_OPENSWMMCORE)
