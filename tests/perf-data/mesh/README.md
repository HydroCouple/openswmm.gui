# Mesh rendering perf fixtures

Synthetic terrain meshes for the Mesh Tiled LOD rendering plan
(`workplans/MESH_TILED_LOD_RENDERING_PLAN_2026-07-13.md`, Phase 0 baselines).

Generated files (`mesh_<N>tri.2dm` + `mesh_<N>tri.inp`) are **not committed**
(the 5M-triangle mesh is ~200 MB of text) — only this documented, reviewable
folder and the generator are. Regenerate with:

```sh
cmake --build build/darwin-debug --target mesh_perf_generator
build/darwin-debug/tests/tools/mesh_perf_generator tests/perf-data/mesh
```

Default sizes: 0.5M / 1M / 5M triangles. Pass explicit counts as extra
arguments to override. Each `.inp` is a minimal metric SWMM model whose
`[2D_MESH_FILE]` references the sibling `.2dm`, so the fixtures open directly
in the GUI (File → Open) as well as in the offscreen harness
(`tests/gui/test_meshperf_baseline.cpp`).

The analytic surface carries a regional slope, two ridge sets, a meandering
main channel, a straight tributary, and fine corrugation — features the
normal-deviation LOD pyramid (plan Phase 3) must preserve, plus flats it can
thin.
