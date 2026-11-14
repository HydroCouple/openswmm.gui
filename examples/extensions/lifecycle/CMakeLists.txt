# Copyright (c) 2026 Caleb Buahin. All rights reserved.
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.21)
project(OpenSWMMLifecycleExtension VERSION 1.0.0 LANGUAGES CXX)

find_package(OpenSWMMVisSdk 1.9 CONFIG REQUIRED)

set(OPENSWMM_LIFECYCLE_EXAMPLE_ID "org.openswmm.lifecycle-example" CACHE STRING
    "Unique reverse-DNS identity for this independently built example")
if(NOT OPENSWMM_LIFECYCLE_EXAMPLE_ID MATCHES "^[A-Za-z][A-Za-z0-9_-]*(\\.[A-Za-z][A-Za-z0-9_-]*)+$")
    message(FATAL_ERROR "OPENSWMM_LIFECYCLE_EXAMPLE_ID must be a reverse-DNS identity")
endif()

openswmmvis_add_extension(lifecycle_example
    ID "${OPENSWMM_LIFECYCLE_EXAMPLE_ID}"
    CAPTION "Session lifecycle example"
    VERSION "${PROJECT_VERSION}"
    VENDOR "OpenSWMM"
    LICENSE "GPL-3.0-or-later"
    SOURCES extension.cpp)
target_compile_definitions(lifecycle_example PRIVATE
    OPENSWMM_LIFECYCLE_EXAMPLE_ID="${OPENSWMM_LIFECYCLE_EXAMPLE_ID}")
