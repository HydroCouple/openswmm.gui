# cmake/FetchOpenSWMMData.cmake
#
# Build the Data Sources GUI extension against this build's public SDK. The
# result remains a loadable MODULE; deployment into the GUI search directory is
# owned by the application so copying happens before platform deployment/signing.
#
# Resolution order: standard FetchContent source override, explicit checkout,
# sibling openswmm.gui.data checkout, then the pinned/configured GitHub revision.
# OPENSWMM_DATA_BUILD_TESTS remains the Data project's own option.
# SPDX-License-Identifier: GPL-3.0-or-later

include_guard(GLOBAL)

option(SWMMVIS_BUNDLE_DATA_PLUGIN "Build and package the Data Sources GUI extension" ON)
if(NOT SWMMVIS_BUNDLE_DATA_PLUGIN)
    return()
endif()

include(FetchContent)

set(OPENSWMM_DATA_SOURCE_DIR "" CACHE PATH
    "Optional Data Sources checkout; empty selects a sibling checkout or GitHub fetch.")
set(OPENSWMM_DATA_GIT_REPOSITORY "https://github.com/HydroCouple/hydrocouple.gui.data.git" CACHE STRING
    "Git URL for the Data Sources GUI extension.")
set(OPENSWMM_DATA_GIT_TAG "7c3b45908219c8440592f34ee4f6e7470a7ec1d5" CACHE STRING
    "Commit SHA, tag or branch of the Data Sources GUI extension.")

if(NOT TARGET OpenSWMMVis::Sdk OR NOT COMMAND openswmmvis_add_extension OR
   NOT EXISTS "${OpenSWMMVisSdk_MANIFEST}")
    message(FATAL_ERROR
        "The bundled Data Sources extension requires this GUI build's SDK target, "
        "extension helper and compatibility manifest before FetchOpenSWMMData is included.")
endif()

set(_openswmm_data_source "")
if(FETCHCONTENT_SOURCE_DIR_OPENSWMMDATA)
    set(_openswmm_data_source "${FETCHCONTENT_SOURCE_DIR_OPENSWMMDATA}")
elseif(OPENSWMM_DATA_SOURCE_DIR)
    set(_openswmm_data_source "${OPENSWMM_DATA_SOURCE_DIR}")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/../openswmm.gui.data/CMakeLists.txt")
    set(_openswmm_data_source "${CMAKE_SOURCE_DIR}/../openswmm.gui.data")
endif()

if(_openswmm_data_source)
    if(NOT EXISTS "${_openswmm_data_source}/CMakeLists.txt")
        message(FATAL_ERROR
            "Data Sources checkout has no CMakeLists.txt: ${_openswmm_data_source}")
    endif()
    message(STATUS "openswmm.gui.data: using local checkout at ${_openswmm_data_source}")
    FetchContent_Declare(OpenSWMMData SOURCE_DIR "${_openswmm_data_source}")
else()
    message(STATUS "openswmm.gui.data: fetching ${OPENSWMM_DATA_GIT_REPOSITORY} @ ${OPENSWMM_DATA_GIT_TAG}")
    # A full clone also supports a pinned commit outside the branch's current tip.
    FetchContent_Declare(OpenSWMMData
        GIT_REPOSITORY "${OPENSWMM_DATA_GIT_REPOSITORY}"
        GIT_TAG "${OPENSWMM_DATA_GIT_TAG}"
        GIT_SHALLOW FALSE)
endif()

FetchContent_MakeAvailable(OpenSWMMData)

if(NOT TARGET openswmm_data)
    message(FATAL_ERROR
        "The selected Data Sources checkout did not define the openswmm_data plugin target.")
endif()
unset(_openswmm_data_source)
