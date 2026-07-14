#
# tests/CMakeLists.txt - Test suite for openswmm.gui
#
# Frameworks:
#   unit/   — Google Test (non-widget logic; UnitSystem, ProjectSerializer, etc.)
#   gui/    — QtTest (widget interaction; runs under offscreen QPA)
#
# Build flags:
#   -DSWMMVIS_BUILD_TESTS=ON   (set from root CMakeLists.txt)
#
# CTest labels:
#   ctest -L unit     Run unit tests only
#   ctest -L gui      Run GUI integration tests only
#   ctest             Run all tests
#

cmake_minimum_required(VERSION 3.21)

find_package(GTest CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Test)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TEST_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/unit/data)

# ── Windows: make the test binaries' DLLs findable ──────────────────────────
#
# Windows has no RPATH. A test executable resolves its imports from exactly three
# places: its own directory, the system directories, and PATH. Neither
# openswmm.engine.dll (which lives in the prebuilt engine's install tree) nor the
# vcpkg DLLs (gdal.dll, hdf5.dll, sqlite3.dll, ... — x64-windows is a DYNAMIC
# triplet) is in any of them. The root CMakeLists copies the engine DLL next to
# SWMMVis.exe, but the tests build into tests/gui/ and tests/unit/, so they never
# see it.
#
# The consequence is not a link error — it is a HANG. The loader fails with
# STATUS_DLL_NOT_FOUND *before main() runs*, Windows raises a WER dialog, and the
# process blocks until ctest's timeout kills it. On CI this presented as 27 tests
# "hanging" for 180 s each. The import table proved it exactly:
#
#     imports openswmm.engine.dll -> HUNG   27 / 27   (100%)
#     does not import it          -> PASSED 95 / 95   (100%)
#
# So: prepend those directories to PATH for every test. SWMMVIS_TEST_DLL_DIRS is
# consumed by the add_swmmvis_*_test helpers in unit/ and gui/.
if(WIN32)
    # ENVIRONMENT_MODIFICATION requires CMake >= 3.22. An UNKNOWN test property is
    # silently ignored, so on 3.21 this fix would quietly do nothing and the tests
    # would go back to hanging with no clue why. Fail loudly instead.
    if(CMAKE_VERSION VERSION_LESS 3.22)
        message(FATAL_ERROR
            "Building the Windows test suite needs CMake >= 3.22 "
            "(ENVIRONMENT_MODIFICATION); found ${CMAKE_VERSION}. Without it the "
            "test binaries cannot locate openswmm.engine.dll / the vcpkg DLLs and "
            "every engine-linked test hangs at load time.")
    endif()

    set(SWMMVIS_TEST_DLL_DIRS "")
    if(TARGET openswmm_engine)
        # Imported target -> ask CMake where the DLL actually is, rather than
        # guessing bin/ vs lib/.
        list(APPEND SWMMVIS_TEST_DLL_DIRS
             "PATH=path_list_prepend:$<TARGET_FILE_DIR:openswmm_engine>")
    endif()
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        list(APPEND SWMMVIS_TEST_DLL_DIRS
             "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    endif()
    message(STATUS "Windows test DLL search dirs: ${SWMMVIS_TEST_DLL_DIRS}")
endif()

add_subdirectory(unit)
add_subdirectory(gui)
