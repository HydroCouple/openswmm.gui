# Copyright (c) 2026 Caleb Buahin. All rights reserved.
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.21)
project(OpenSWMMEventExtension VERSION 1.0.0 LANGUAGES CXX)

find_package(OpenSWMMVisSdk 1.2 CONFIG REQUIRED)

set(OPENSWMM_EVENTS_EXAMPLE_ID "org.openswmm.events-example" CACHE STRING
    "Unique reverse-DNS identity for this independently built example")
if(NOT OPENSWMM_EVENTS_EXAMPLE_ID MATCHES "^[A-Za-z][A-Za-z0-9_-]*(\\.[A-Za-z][A-Za-z0-9_-]*)+$")
    message(FATAL_ERROR "OPENSWMM_EVENTS_EXAMPLE_ID must be a reverse-DNS identity")
endif()

openswmmvis_add_extension(events_example
    ID "${OPENSWMM_EVENTS_EXAMPLE_ID}"
    CAPTION "Typed event example"
    VERSION "${PROJECT_VERSION}"
    VENDOR "OpenSWMM"
    LICENSE "GPL-3.0-or-later"
    SOURCES extension.cpp)
target_compile_definitions(events_example PRIVATE
    OPENSWMM_EVENTS_EXAMPLE_ID="${OPENSWMM_EVENTS_EXAMPLE_ID}")
openswmmvis_add_event_adapter(events_example)
