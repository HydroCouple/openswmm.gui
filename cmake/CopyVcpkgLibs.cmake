# cmake/CopyVcpkgLibs.cmake
#
# Copies shared libraries (*.so, *.so.*) from a vcpkg lib directory to a
# destination directory.  Symlink chains are followed so that resolved
# physical files (not dangling symlinks pointing back into the vcpkg store)
# end up in OUTPUT_DIR.
#
# Called via POST_BUILD custom command:
#   cmake -DVCPKG_LIB_DIR=<path> -DOUTPUT_DIR=<path> -P CopyVcpkgLibs.cmake

cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED VCPKG_LIB_DIR OR NOT IS_DIRECTORY "${VCPKG_LIB_DIR}")
    message(WARNING "CopyVcpkgLibs: VCPKG_LIB_DIR is not set or is not a directory — skipping.")
    return()
endif()

if(NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "CopyVcpkgLibs: OUTPUT_DIR must be set.")
endif()

file(GLOB _so_files
    LIST_DIRECTORIES false
    "${VCPKG_LIB_DIR}/*.so"
    "${VCPKG_LIB_DIR}/*.so.*"
)

foreach(_lib IN LISTS _so_files)
    file(COPY "${_lib}"
        DESTINATION "${OUTPUT_DIR}"
        FOLLOW_SYMLINK_CHAIN
    )
endforeach()

list(LENGTH _so_files _count)
message(STATUS "CopyVcpkgLibs: copied ${_count} shared library file(s) to ${OUTPUT_DIR}")
