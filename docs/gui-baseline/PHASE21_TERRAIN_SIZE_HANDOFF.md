# Phase 21 — accepted terrain input reaches mesh sizing

This bounded W2 checkpoint addresses M1's missing terrain-data handoff. It does
not complete W2 or prove line-feature fidelity.

## Implementation

The generation worker previously sent its filtered DEM survivors to the mesh
generator while supplying only its original feature/auxiliary Steiner vector to
`SizeField::build`. The terrain-density option consequently saw no DEM cloud.
Marker-zero auxiliary inputs could instead be interpreted as terrain.

`TerrainSizeInput` provides a distinct, bounded accumulator. Its grid uses the
same layout as the final size field. Each accepted mesh-coordinate terrain
sample contributes a cell count; the accumulator stores no copy of the point
cloud. The worker prepares this input before terrain filtering, adds only actual
survivors after reprojection/spacing/boundary rejection, and passes it separately
to the size field. Cached terrain candidates use the same downstream path.

With an explicit terrain input, neither auxiliary marker-zero points nor tagged
nodes become terrain-density samples. Tagged inputs still seed feature distance.
Existing callers without an explicit terrain input retain their prior API and
marker-zero semantics. Layout mismatch is refused rather than silently applying
counts at the wrong locations. Invalid or far-outside coordinates are ignored
before integer conversion. Counts use a fixed grid whose allocation is bounded
independently of sample count.

## Validation

The retained baseline probe reproduces the previous worker handoff: 1,600
accepted mesh points produce a terrain size constraint of zero (exit 1).
The parent also reproduced the defect through the actual generation worker:
the no-DEM control, raw-DEM run and thinner-DEM run each produced 18 cells
(10 quads), so both terrain-refinement assertions failed. That baseline is
retained in `terrain/baseline_tests.log` (2 passed, 2 failed including fixtures).
`test_terrainsizehandoff` compiles against production `sizefield.cpp` and passes
all eight QtTest checks (including setup/cleanup):

- accepted terrain produces a local refinement bound without a cloud copy;
- auxiliary/tagged samples remain separate from terrain;
- terrain-only input is usable and minimum-area precedence is preserved;
- invalid coordinates and reinitialization are handled consistently;
- mismatched input/field grids are refused;
- storage stays bounded as samples accumulate and disabling density clears it.

Evidence is retained under `workplans/artifacts/phase_21_parallel/terrain/`,
including `baseline_handoff.cpp`, `baseline_test.log`, `after_test.log`, compiler
logs, isolated executables, and `run_test.py`.

The integrated worker regression now passes all four QtTest checks (including
fixtures). Both raw and thinner paths contribute **1,024 accepted terrain samples**
and produce **930 cells: 226 triangles and 704 quads**. The no-DEM control produces
10 cells (6 quads); removing auxiliary marker-zero density intentionally also
changes that control from the old 18-cell baseline. Each repeated DEM run logs a
Stage-B cache hit and reproduces vertex coordinates, cell count and quad count.
This proves the production handoff affects generated meshes, not terrain fidelity.

Final integration passed seven suites with **115 QtTest passes**, no unexpected
failures or skips: handoff 8, actual worker 4, existing size field 11, Initial
Quality 16, terrain precision 35, stage cache 8 and quad-region end-to-end 33.
The quad-region suite retains its pre-existing expected failure for excess
triangles around a pinned junction. Evidence: `final_focused_tests.log`,
`final_pipeline_tests.log` and `final_quad_tests.log` under the phase artifacts.
The consolidated guide records the application build/signature/launch result.

## Deliberate limits

The density formula remains `pitch / sqrt(samples in cell)`. This is an areal
estimate dependent on background-grid pitch. It does not guarantee along-road or
along-river spacing, transverse bank separation, corridor connectivity, or final
surface reconstruction error. Header and implementation comments now state that
limitation explicitly.

The existing worker policy still enables terrain-density sizing for global
quads with positive maximum area and positive gradation. Explicit Free regions,
zero gradation, uncapped area and fixed-spacing fidelity policy remain M2 work.
Rejected candidates do not contribute a density constraint; assessing hydraulic
feature loss in those filters remains W5. No generation publication or project
transaction behavior changes in this checkpoint.
