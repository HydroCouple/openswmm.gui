# Copyright (c) 2026 Caleb Buahin. All rights reserved.
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.21)
project(OpenSWMMActivityExtension VERSION 1.0.0 LANGUAGES CXX)
find_package(OpenSWMMVisSdk 1.5 CONFIG REQUIRED)
set(OPENSWMM_ACTIVITY_EXAMPLE_ID "org.openswmm.activity-example" CACHE STRING
    "Unique reverse-DNS identity for this independently built activity example")
if(NOT OPENSWMM_ACTIVITY_EXAMPLE_ID MATCHES "^[A-Za-z][A-Za-z0-9_-]*(\\.[A-Za-z][A-Za-z0-9_-]*)+$")
    message(FATAL_ERROR "OPENSWMM_ACTIVITY_EXAMPLE_ID must be a reverse-DNS identity")
endif()
openswmmvis_add_extension(activity_example
    ID "${OPENSWMM_ACTIVITY_EXAMPLE_ID}" CAPTION "Process and document example"
    VERSION "${PROJECT_VERSION}" VENDOR "OpenSWMM" LICENSE "GPL-3.0-or-later"
    SOURCES extension.cpp)
openswmmvis_add_activity_adapter(activity_example)
target_compile_definitions(activity_example PRIVATE
    OPENSWMM_ACTIVITY_EXAMPLE_ID="${OPENSWMM_ACTIVITY_EXAMPLE_ID}")
