cmake_minimum_required(VERSION 3.21)
project(OpenSWMMExampleExtension VERSION 1.0.0 LANGUAGES CXX)
find_package(OpenSWMMVisSdk 1.0 CONFIG REQUIRED)
openswmmvis_add_extension(example_extension
    ID org.openswmm.example CAPTION "Example extension" VERSION 1.0.0
    VENDOR "OpenSWMM" LICENSE "GPL-3.0-or-later" SOURCES extension.cpp)
