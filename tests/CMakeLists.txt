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

add_subdirectory(unit)
add_subdirectory(gui)
