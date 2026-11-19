# Exercise real configure/build steps: a second build must refresh the embedded
# date even though no application source or CMake input has changed.
cmake_minimum_required(VERSION 3.21)

function(run_checked)
    execute_process(COMMAND ${ARGV}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Command failed (${result}): ${ARGV}\n${output}\n${error}")
    endif()
endfunction()

file(MAKE_DIRECTORY "${TEST_BINARY_DIR}/source")
file(WRITE "${TEST_BINARY_DIR}/source/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.21)
project(BuildTimestampRegression LANGUAGES CXX)
include("${SWMMVIS_SOURCE_DIR}/cmake/BuildInfo.cmake")
add_executable(timestamp_probe main.cpp)
target_include_directories(timestamp_probe PRIVATE "${SWMMVIS_SOURCE_DIR}/include")
target_link_libraries(timestamp_probe PRIVATE swmmvis_build_info)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/probe-$<CONFIG>.txt"
    CONTENT "$<TARGET_FILE:timestamp_probe>")
]=])
file(WRITE "${TEST_BINARY_DIR}/source/main.cpp" [=[
#include "core/buildinfo.h"
#include <cstdio>
int main() { std::puts(openswmmvis::buildTimestamp()); }
]=])

run_checked("${CMAKE_COMMAND}" -S "${TEST_BINARY_DIR}/source" -B "${TEST_BINARY_DIR}/build"
    -G "${TEST_GENERATOR}" "-DSWMMVIS_SOURCE_DIR=${SWMMVIS_SOURCE_DIR}"
    "-DCMAKE_CXX_COMPILER=${TEST_COMPILER}" -DCMAKE_BUILD_TYPE=Release)
file(READ "${TEST_BINARY_DIR}/build/probe-Release.txt" probe)

# Fixed epochs make this fast and deterministic, and also check reproducible
# builds. Changing SOURCE_DATE_EPOCH must work without reconfiguring CMake.
foreach(epoch IN ITEMS 946684800 946771200)
    run_checked("${CMAKE_COMMAND}" -E env "SOURCE_DATE_EPOCH=${epoch}"
        "${CMAKE_COMMAND}" --build "${TEST_BINARY_DIR}/build"
        --config Release --target timestamp_probe)
    execute_process(COMMAND "${probe}"
        RESULT_VARIABLE result OUTPUT_VARIABLE timestamp OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(epoch EQUAL 946684800)
        set(expected "2000-01-01 00:00:00 UTC")
    else()
        set(expected "2000-01-02 00:00:00 UTC")
    endif()
    if(NOT result EQUAL 0 OR NOT timestamp STREQUAL expected)
        message(FATAL_ERROR "Expected '${expected}', got '${timestamp}' (exit ${result})")
    endif()
endforeach()
message(STATUS "Build timestamp refreshes on incremental builds")
