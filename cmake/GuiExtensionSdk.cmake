# Copyright (c) 2026 Caleb Buahin. All rights reserved.
# SPDX-License-Identifier: GPL-3.0-or-later
include(CMakePackageConfigHelpers)

set(OPENSWMMVIS_SDK_API_VERSION "1.1")
set(_sdk_engine_tag "")
if(TARGET OpenSWMMEngine::openswmm_engine)
    get_target_property(_sdk_engine_includes OpenSWMMEngine::openswmm_engine INTERFACE_INCLUDE_DIRECTORIES)
    foreach(_directory IN LISTS _sdk_engine_includes)
        if(EXISTS "${_directory}/openswmm/engine/openswmm_engine.h")
            if(EXISTS "${_directory}/openswmm/engine/openswmm_abi_tag.h")
                set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                    "${_directory}/openswmm/engine/openswmm_abi_tag.h")
                file(STRINGS "${_directory}/openswmm/engine/openswmm_abi_tag.h" _sdk_tag_definition
                    REGEX "^#define OPENSWMM_ENGINE_ABI_TAG ")
                string(REGEX MATCH "\"([^\"]+)\"" _sdk_tag_match "${_sdk_tag_definition}")
                if(_sdk_tag_match)
                    set(_sdk_engine_tag "${CMAKE_MATCH_1}")
                    break()
                endif()
            endif()
            file(GLOB _sdk_engine_headers CONFIGURE_DEPENDS "${_directory}/openswmm/engine/*.h")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_sdk_engine_headers})
            list(SORT _sdk_engine_headers)
            set(_sdk_hashes "")
            foreach(_header IN LISTS _sdk_engine_headers)
                file(SHA256 "${_header}" _hash)
                string(APPEND _sdk_hashes "${_hash}")
            endforeach()
            string(SHA256 _sdk_engine_tag "${_sdk_hashes}")
            set(_sdk_engine_tag "headers-sha256:${_sdk_engine_tag}")
            break()
        endif()
    endforeach()
endif()
string(REGEX MATCH "^[0-9]+" _sdk_compiler_major "${CMAKE_CXX_COMPILER_VERSION}")
set(_sdk_compiler_abi "${CMAKE_CXX_COMPILER_ID}-${_sdk_compiler_major}-${CMAKE_SIZEOF_VOID_P}")
if(MSVC)
    set(_sdk_compiler_abi "msvc-${MSVC_TOOLSET_VERSION}-${CMAKE_SIZEOF_VOID_P}")
endif()
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    string(REPLACE ";" "+" _sdk_arch "${CMAKE_OSX_ARCHITECTURES}")
else()
    set(_sdk_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(_sdk_manifest "{\"guiVersion\":\"${PROJECT_VERSION}\",\"sdkApiVersion\":\"${OPENSWMMVIS_SDK_API_VERSION}\",\"qt\":\"${Qt6_VERSION}\",\"compilerAbi\":\"${_sdk_compiler_abi}\",\"engineVersion\":\"${PROJECT_VERSION}\",\"engineAbiTag\":\"${_sdk_engine_tag}\",\"arch\":\"${_sdk_arch}\"}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/sdk")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sdk/sdk-manifest.json" "${_sdk_manifest}\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sdk/sdk-build-manifest.h"
    "#pragma once\n#define OPENSWMMVIS_SDK_MANIFEST_JSON R\"manifest(${_sdk_manifest})manifest\"\n")

add_library(openswmmvis_sdk INTERFACE)
add_library(OpenSWMMVis::Sdk ALIAS openswmmvis_sdk)
set_target_properties(openswmmvis_sdk PROPERTIES EXPORT_NAME Sdk)
target_compile_features(openswmmvis_sdk INTERFACE cxx_std_20)
target_include_directories(openswmmvis_sdk INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/openswmmvis>)
target_link_libraries(openswmmvis_sdk INTERFACE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

install(TARGETS openswmmvis_sdk EXPORT OpenSWMMVisSdkTargets COMPONENT GuiSdk)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/sdk/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/openswmmvis/sdk" COMPONENT GuiSdk)
install(FILES "${PROJECT_SOURCE_DIR}/docs/sdk/extensions.dox"
    "${PROJECT_SOURCE_DIR}/docs/manual/26_extensions.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/openswmmvis-sdk" COMPONENT GuiSdk)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/examples/extensions/template/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/openswmmvis-sdk/examples/template" COMPONENT GuiSdk)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sdk/sdk-manifest.json"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OpenSWMMVisSdk" COMPONENT GuiSdk)
install(EXPORT OpenSWMMVisSdkTargets NAMESPACE OpenSWMMVis::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OpenSWMMVisSdk" COMPONENT GuiSdk)
configure_package_config_file("${PROJECT_SOURCE_DIR}/cmake/OpenSWMMVisSdkConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/sdk/OpenSWMMVisSdkConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OpenSWMMVisSdk")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/sdk/OpenSWMMVisSdkConfigVersion.cmake"
    VERSION "${OPENSWMMVIS_SDK_API_VERSION}" COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sdk/OpenSWMMVisSdkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/sdk/OpenSWMMVisSdkConfigVersion.cmake"
    "${PROJECT_SOURCE_DIR}/cmake/OpenSWMMVisExtension.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OpenSWMMVisSdk" COMPONENT GuiSdk)

if(NOT SWMMVIS_SDK_ONLY)
    add_library(openswmmvis_extensions STATIC
        "${PROJECT_SOURCE_DIR}/include/plugins/extensionmanager.h"
        "${PROJECT_SOURCE_DIR}/include/plugins/guiextensionhost.h"
        "${PROJECT_SOURCE_DIR}/src/plugins/extensionmanager.cpp"
        "${PROJECT_SOURCE_DIR}/src/plugins/extensionnetwork.cpp"
        "${PROJECT_SOURCE_DIR}/src/plugins/extensionattributes.cpp"
        "${PROJECT_SOURCE_DIR}/src/plugins/guiextensionhost.cpp")
    set_target_properties(openswmmvis_extensions PROPERTIES AUTOMOC ON)
    target_include_directories(openswmmvis_extensions PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/sdk")
    target_link_libraries(openswmmvis_extensions PUBLIC openswmmvis_sdk)
endif()
