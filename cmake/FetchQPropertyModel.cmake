# cmake/FetchQPropertyModel.cmake
#
# Provides the QPropertyModel target.
#
# Resolution order (first match wins):
#   1. Explicit `-DQPROPERTYMODEL_SOURCE_DIR=/path/to/checkout`     (user override)
#   2. Sibling checkout at `${CMAKE_SOURCE_DIR}/../QPropertyModel`  (current dev workflow)
#   3. GitHub fetch via FetchContent
#
# Cache variables (override at configure time with -D…):
#   QPROPERTYMODEL_SOURCE_DIR     local checkout path (empty = use auto/fetch)
#   QPROPERTYMODEL_GIT_REPOSITORY upstream URL
#   QPROPERTYMODEL_GIT_TAG        branch / tag / commit SHA
#
# After include(): defines the `QPropertyModel` target and adds
# `HAVE_QPROPERTYMODEL` to compile definitions.

include_guard(GLOBAL)
include(FetchContent)

set(QPROPERTYMODEL_SOURCE_DIR "" CACHE PATH
    "Optional path to a local QPropertyModel checkout (overrides the GitHub fetch).")
set(QPROPERTYMODEL_GIT_REPOSITORY "https://github.com/cbuahin/QPropertyModel.git" CACHE STRING
    "Git URL to fetch QPropertyModel from.")
set(QPROPERTYMODEL_GIT_TAG "dev" CACHE STRING
    "Branch / tag / commit-SHA of QPropertyModel to use.")

if(NOT QPROPERTYMODEL_SOURCE_DIR
        AND EXISTS "${CMAKE_SOURCE_DIR}/../QPropertyModel/CMakeLists.txt")
    set(QPROPERTYMODEL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../QPropertyModel")
endif()

if(QPROPERTYMODEL_SOURCE_DIR AND EXISTS "${QPROPERTYMODEL_SOURCE_DIR}/CMakeLists.txt")
    message(STATUS "QPropertyModel: using local checkout at ${QPROPERTYMODEL_SOURCE_DIR}")
    FetchContent_Declare(QPropertyModel
        SOURCE_DIR "${QPROPERTYMODEL_SOURCE_DIR}"
    )
else()
    message(STATUS "QPropertyModel: fetching ${QPROPERTYMODEL_GIT_REPOSITORY} @ ${QPROPERTYMODEL_GIT_TAG}")
    FetchContent_Declare(QPropertyModel
        GIT_REPOSITORY "${QPROPERTYMODEL_GIT_REPOSITORY}"
        GIT_TAG        "${QPROPERTYMODEL_GIT_TAG}"
        GIT_SHALLOW    TRUE
    )
endif()

FetchContent_MakeAvailable(QPropertyModel)

if(TARGET QPropertyModel)
    add_compile_definitions(HAVE_QPROPERTYMODEL)
endif()
