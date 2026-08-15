#
# tests/tools/CMakeLists.txt — developer tools that support the test suite
# (not tests themselves; nothing here is registered with CTest).
#

# Mesh Tiled LOD plan Phase 0 — synthetic terrain-mesh generator. Plain C++
# (no Qt/GDAL); writes user-reviewable perf fixtures into tests/perf-data/mesh/.
#   cmake --build <build> --target mesh_perf_generator
#   <build>/tests/tools/mesh_perf_generator <repo>/tests/perf-data/mesh
add_executable(mesh_perf_generator meshperfgen.cpp)
