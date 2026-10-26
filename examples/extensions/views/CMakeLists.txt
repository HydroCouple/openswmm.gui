# Copyright (c) 2026 Caleb Buahin. All rights reserved.
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.21)
project(OpenSWMMViewExtension VERSION 1.0.0 LANGUAGES CXX)
find_package(OpenSWMMVisSdk 1.4 CONFIG REQUIRED)
set(OPENSWMM_VIEW_EXAMPLE_ID "org.openswmm.views-example" CACHE STRING
    "Unique reverse-DNS identity for this independently built view example")
if(NOT OPENSWMM_VIEW_EXAMPLE_ID MATCHES "^[A-Za-z][A-Za-z0-9_-]*(\\.[A-Za-z][A-Za-z0-9_-]*)+$")
    message(FATAL_ERROR "OPENSWMM_VIEW_EXAMPLE_ID must be a reverse-DNS identity")
endif()
openswmmvis_add_extension(views_example
    ID "${OPENSWMM_VIEW_EXAMPLE_ID}" CAPTION "Map and results example"
    VERSION "${PROJECT_VERSION}" VENDOR "OpenSWMM" LICENSE "GPL-3.0-or-later"
    SOURCES extension.cpp)
openswmmvis_add_view_adapter(views_example)
target_compile_definitions(views_example PRIVATE
    OPENSWMM_VIEW_EXAMPLE_ID="${OPENSWMM_VIEW_EXAMPLE_ID}")
