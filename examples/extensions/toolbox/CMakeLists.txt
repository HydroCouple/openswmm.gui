# Copyright (c) 2026 Caleb Buahin. All rights reserved.
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.21)
project(OpenSWMMToolboxExtension VERSION 1.0.0 LANGUAGES CXX)
find_package(OpenSWMMVisSdk 1.10 CONFIG REQUIRED)
openswmmvis_add_extension(toolbox_example
    ID org.openswmm.toolbox-example CAPTION "Toolbox example"
    VERSION "${PROJECT_VERSION}" VENDOR "OpenSWMM" LICENSE "GPL-3.0-or-later"
    SOURCES extension.cpp)
