# Copyright (c) 2026 Caleb Buahin. All rights reserved.
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.21)
project(OpenSWMMValueExtension VERSION 1.0.0 LANGUAGES CXX)
find_package(OpenSWMMVisSdk 1.9 CONFIG REQUIRED)
set(OPENSWMM_VALUE_EXAMPLE_ID "org.openswmm.values-example" CACHE STRING
    "Unique reverse-DNS identity for this independently built value example")
if(NOT OPENSWMM_VALUE_EXAMPLE_ID MATCHES "^[A-Za-z][A-Za-z0-9_-]*(\\.[A-Za-z][A-Za-z0-9_-]*)+$")
    message(FATAL_ERROR "OPENSWMM_VALUE_EXAMPLE_ID must be a reverse-DNS identity")
endif()
openswmmvis_add_extension(values_example
    ID "${OPENSWMM_VALUE_EXAMPLE_ID}" CAPTION "Copied event values example"
    VERSION "${PROJECT_VERSION}" VENDOR "OpenSWMM" LICENSE "GPL-3.0-or-later"
    SOURCES extension.cpp ValueEventAdapter.cpp ValueEventAdapter.hpp)
set_target_properties(values_example PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_compile_definitions(values_example PRIVATE
    OPENSWMM_VALUE_EXAMPLE_ID="${OPENSWMM_VALUE_EXAMPLE_ID}")
