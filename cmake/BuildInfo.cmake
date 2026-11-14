# Generate at build time: __DATE__/__TIME__ in the About dialog stay stale
# whenever an incremental build reuses that translation unit. Keep the changing
# value in a tiny separate library so only that source needs recompiling.
include_guard(GLOBAL)

set(_swmmvis_build_info_source "${CMAKE_CURRENT_BINARY_DIR}/buildinfo/$<CONFIG>/buildinfo.cpp")
add_custom_target(swmmvis_build_info_stamp
    COMMAND "${CMAKE_COMMAND}"
        "-DOUTPUT_FILE=${_swmmvis_build_info_source}"
        -P "${CMAKE_CURRENT_LIST_DIR}/WriteBuildTimestamp.cmake"
    BYPRODUCTS "${_swmmvis_build_info_source}"
    COMMENT "Updating SWMMVis build timestamp"
    VERBATIM
)
add_library(swmmvis_build_info STATIC "${_swmmvis_build_info_source}")
add_dependencies(swmmvis_build_info swmmvis_build_info_stamp)
target_include_directories(swmmvis_build_info PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../include")
set_target_properties(swmmvis_build_info PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
    POSITION_INDEPENDENT_CODE ON
)
unset(_swmmvis_build_info_source)
