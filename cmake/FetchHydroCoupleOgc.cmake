# cmake/FetchHydroCoupleOgc.cmake
#
# Provides HydroCoupleOgc::HydroCoupleOgc — the OGC web service clients (WMS,
# WMTS, WFS) this GUI shares with HydroCoupleComposer.
#
# Consumption model differs from the engine's deliberately. The engine is a
# heavyweight prebuilt package with its own vcpkg tree; this is a small Qt-only
# library with no dependencies of its own beyond Qt, so it is fetched and built
# in-tree. That means a fresh checkout builds without a manual sibling build
# step, and it keeps the two editable together: a sibling checkout, when there
# is one, is used in place of the download.
#
# Cache variables (override at configure time with -D…):
#   FETCHCONTENT_SOURCE_DIR_HYDROCOUPLEOGC   local checkout to build instead of
#                                            downloading.
#
# Licensing: HydroCoupleOgc is LGPL-3.0-or-later and this GUI is
# GPL-3.0-or-later, which is the direction that combines.

include_guard(GLOBAL)
include(FetchContent)

if(EXISTS "${CMAKE_SOURCE_DIR}/../HydroCoupleOgc/CMakeLists.txt"
   AND NOT DEFINED FETCHCONTENT_SOURCE_DIR_HYDROCOUPLEOGC)
    set(FETCHCONTENT_SOURCE_DIR_HYDROCOUPLEOGC
        "${CMAKE_SOURCE_DIR}/../HydroCoupleOgc"
        CACHE PATH "Sibling checkout of HydroCoupleOgc")
endif()

# Its suites are its own repository's business; requiring them here would
# require Google Test of this build whether or not it wants tests.
set(HYDROCOUPLEOGC_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(HydroCoupleOgc
    GIT_REPOSITORY https://github.com/HydroCouple/HydroCoupleOgc.git
    GIT_TAG main
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(HydroCoupleOgc)

message(STATUS "HydroCoupleOgc : ${hydrocoupleogc_SOURCE_DIR}")
