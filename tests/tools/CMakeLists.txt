#
# tests/tools/CMakeLists.txt — developer tools that support the test suite
# (not tests themselves; nothing here is registered with CTest).
#

# Mesh Tiled LOD plan Phase 0 — synthetic terrain-mesh generator. Plain C++
# (no Qt/GDAL); writes user-reviewable perf fixtures into tests/perf-data/mesh/.
#   cmake --build <build> --target mesh_perf_generator
#   <build>/tests/tools/mesh_perf_generator <repo>/tests/perf-data/mesh
add_executable(mesh_perf_generator meshperfgen.cpp)

# PROFILE_ATTRIBUTE_TRACKS_VERIFICATION.md §3 — drives the real profile +
# attribute-tracks widget pair (in the splitter/scroll-area composition the
# dialog builds) against a real .inp/.out pair, and writes PASS/FAIL rows plus
# PNG grabs for the checks that otherwise need a human at the screen.
#   cmake --build <build> --target profile_attribute_tracks_verify
#   <build>/tests/tools/profile_attribute_tracks_verify \
#       examples/demo_snoopy_lagoon/snoopy.inp \
#       examples/demo_snoopy_lagoon/snoopy.out \
#       tests/manual/profile_attribute_tracks
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
add_executable(profile_attribute_tracks_verify
    profileattributetracksverify.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/plotattribute.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/profileattributetrackoptions.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/profileattributetrackswidget.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/profilebuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/profileplotoptions.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/profileplotwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/numberformat.cpp
    ${CMAKE_SOURCE_DIR}/src/plot/swmmoutrunlayer_codes.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/theme/thememanager.cpp
    # ProfilePlotOptions reads its axis number-format defaults from prefs;
    # PreferencesManager's custom ramp persistence pulls in RasterColorRamp.
    ${CMAKE_SOURCE_DIR}/src/core/preferencesmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/render/colorramp.cpp
    ${CMAKE_SOURCE_DIR}/include/core/preferencesmanager.h
    ${CMAKE_SOURCE_DIR}/include/render/colorramp.h
    # Q_OBJECT headers must be listed for AUTOMOC to generate their moc TUs.
    ${CMAKE_SOURCE_DIR}/include/plot/profileattributetrackoptions.h
    ${CMAKE_SOURCE_DIR}/include/plot/profileattributetrackswidget.h
    ${CMAKE_SOURCE_DIR}/include/plot/profileplotoptions.h
    ${CMAKE_SOURCE_DIR}/include/plot/profileplotwidget.h
    ${CMAKE_SOURCE_DIR}/include/ui/theme/thememanager.h
)
set_target_properties(profile_attribute_tracks_verify PROPERTIES AUTOMOC ON)
target_include_directories(profile_attribute_tracks_verify PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(profile_attribute_tracks_verify PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets
)
if(TARGET openswmm_engine)
    target_link_libraries(profile_attribute_tracks_verify PRIVATE openswmm_engine)
endif()
