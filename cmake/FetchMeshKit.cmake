# cmake/FetchMeshKit.cmake
#
# Provides MeshKit (meshkit::runtime, meshkit::core_qt, meshkit::io_qt and the
# Qt-free targets) from its own repository, pinned to a release tag.
#
# Resolution order (first match wins), as for QPropertyModel:
#   1. Explicit `-DMESHKIT_SOURCE_DIR=/path/to/checkout`   (user override)
#   2. Sibling checkout at `${CMAKE_SOURCE_DIR}/../MeshKit` (or `../Meshkit`) (development)
#   3. GitHub fetch via FetchContent at MESHKIT_GIT_TAG
#
# Cache variables (override at configure time with -D…):
#   MESHKIT_SOURCE_DIR      local checkout path (empty = use auto/fetch)
#   MESHKIT_GIT_REPOSITORY  upstream URL
#   MESHKIT_GIT_TAG         the pinned release tag, vX.Y.Z (or a branch / commit SHA)
#   MESHKIT_REQUIRE_PINNED  ON for release and CI builds: ignore local checkouts
#                           and always fetch MESHKIT_GIT_TAG
#
# The version MeshKit's sources declare is compared with the pinned tag: a
# fetched tag that does not carry its own version stops the configure, and a
# local checkout of another version is reported (it is fine for developing
# MeshKit alongside, never for a release).
#
# After include(): the meshkit:: targets exist, meshkit_SOURCE_DIR names the
# MeshKit sources (license installs, test source lists) and
# MESHKIT_SOURCE_VERSION the version they declare.

include_guard(GLOBAL)
include(FetchContent)

set(MESHKIT_SOURCE_DIR "" CACHE PATH
    "Optional path to a local MeshKit checkout (overrides the GitHub fetch).")
set(MESHKIT_GIT_REPOSITORY "https://github.com/HydroCouple/Meshkit.git" CACHE STRING
    "Git URL to fetch MeshKit from.")
set(MESHKIT_GIT_TAG "v0.2.0" CACHE STRING
    "MeshKit release tag (vX.Y.Z) to build against, or a branch / commit SHA.")
option(MESHKIT_REQUIRE_PINNED
    "Release and CI builds: ignore local MeshKit checkouts and fetch MESHKIT_GIT_TAG" OFF)

if(MESHKIT_REQUIRE_PINNED)
    if(MESHKIT_SOURCE_DIR)
        message(FATAL_ERROR "MeshKit: MESHKIT_REQUIRE_PINNED is ON, so MeshKit must come "
            "from ${MESHKIT_GIT_TAG}; unset MESHKIT_SOURCE_DIR (${MESHKIT_SOURCE_DIR}).")
    endif()
else()
    # A clone of HydroCouple/Meshkit lands in `Meshkit/`; accept either spelling
    # (they differ on case-sensitive filesystems).
    foreach(_meshkit_sibling MeshKit Meshkit)
        if(NOT MESHKIT_SOURCE_DIR
                AND EXISTS "${CMAKE_SOURCE_DIR}/../${_meshkit_sibling}/CMakeLists.txt")
            set(MESHKIT_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../${_meshkit_sibling}")
        endif()
    endforeach()
endif()

# The GUI uses the Qt adapters; MeshKit's own tests, CLI and Python bindings
# are not part of this build.
set(MESHKIT_BUILD_QT ON)
set(MESHKIT_BUILD_TESTS OFF)
set(MESHKIT_BUILD_CLI OFF)
set(MESHKIT_BUILD_PYTHON OFF)
set(MESHKIT_BUILD_BENCHMARKS OFF)

# Built only as the GUI's targets need it, as the old add_subdirectory did.
set(_meshkit_exclude "")
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    set(_meshkit_exclude EXCLUDE_FROM_ALL)
endif()

set(_meshkit_local FALSE)
if(MESHKIT_SOURCE_DIR AND EXISTS "${MESHKIT_SOURCE_DIR}/CMakeLists.txt")
    set(_meshkit_local TRUE)
    execute_process(
        COMMAND git -C "${MESHKIT_SOURCE_DIR}" describe --tags --always --dirty
        OUTPUT_VARIABLE _meshkit_describe OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    message(STATUS "MeshKit: using local checkout at ${MESHKIT_SOURCE_DIR} (${_meshkit_describe})")
    FetchContent_Declare(MeshKit
        SOURCE_DIR "${MESHKIT_SOURCE_DIR}"
        ${_meshkit_exclude}
    )
else()
    message(STATUS "MeshKit: fetching ${MESHKIT_GIT_REPOSITORY} @ ${MESHKIT_GIT_TAG}")
    FetchContent_Declare(MeshKit
        GIT_REPOSITORY "${MESHKIT_GIT_REPOSITORY}"
        GIT_TAG        "${MESHKIT_GIT_TAG}"
        GIT_SHALLOW    TRUE
        ${_meshkit_exclude}
    )
endif()

FetchContent_MakeAvailable(MeshKit)

# Is it the pinned release? Compare the version MeshKit's sources declare with
# the tag (a pre-release tag such as v1.0.0-beta.1 carries CMake version 1.0.0).
file(STRINGS "${meshkit_SOURCE_DIR}/CMakeLists.txt" _meshkit_project
    REGEX "^project\\(MeshKit VERSION [0-9]+\\.[0-9]+\\.[0-9]+")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" MESHKIT_SOURCE_VERSION "${_meshkit_project}")
if(NOT MESHKIT_SOURCE_VERSION)
    message(FATAL_ERROR "MeshKit: cannot read the version from ${meshkit_SOURCE_DIR}/CMakeLists.txt")
endif()
if(MESHKIT_GIT_TAG MATCHES "^v?([0-9]+\\.[0-9]+\\.[0-9]+)")
    set(_meshkit_pinned "${CMAKE_MATCH_1}")
    if(NOT MESHKIT_SOURCE_VERSION VERSION_EQUAL _meshkit_pinned)
        if(_meshkit_local)
            message(WARNING "MeshKit: the local checkout is ${MESHKIT_SOURCE_VERSION}, not the "
                "pinned release ${MESHKIT_GIT_TAG}. That is fine while developing MeshKit "
                "alongside; release builds use -DMESHKIT_REQUIRE_PINNED=ON.")
        else()
            message(FATAL_ERROR "MeshKit: ${MESHKIT_GIT_TAG} declares version "
                "${MESHKIT_SOURCE_VERSION}; a release tag must carry the version it names.")
        endif()
    endif()
else()
    message(STATUS "MeshKit: ${MESHKIT_GIT_TAG} is not a release tag; version not checked")
endif()

foreach(_meshkit_target meshkit::runtime meshkit::core_qt meshkit::io_qt)
    if(NOT TARGET ${_meshkit_target})
        message(FATAL_ERROR "MeshKit did not provide ${_meshkit_target}")
    endif()
endforeach()
