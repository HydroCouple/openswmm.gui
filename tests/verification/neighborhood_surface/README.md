# Neighborhood surface verification

Read-only source: `../vfr_slope_steps/bellinge-partial-2026-10-01.h5`.
The large generated HDF5 fixture remains local and is not included in Git.
Set `VFR_TEST_BELLINGE` to an available saved output to repeat this optional check.
All maps/profiles use the production results layer. Depth-only CPU and native
OpenGL GPU captures isolate the water layer from terrain/basemap overlays.
The whole-mesh GPU capture exercises large geometry buffers.

- `bellinge-final.csv`: production geometry continuity, exact component-volume
  closure, cell-mean residuals and optimized timing on all 12 frames.
- `bellinge-*-cpu.png`, `bellinge-*-gpu.png`: maps for frames 3, 8, 11 and full mesh.
- `bellinge-*-profile.png`: controlled Bellinge section, not the user's exact
  original saved section (its section name was not supplied).
- `final-tests.log`: seven rebuilt focused suites with saved-fixture validation.
- `bellinge-gpu.log`: separate native macOS OpenGL test.
- `bundle-audit.json`: dependency/signing audit of the testing bundle.

Earlier candidate CSVs document the volume-correction iterations; they are not
acceptance results. The accepted fit preserves total component volume but cannot
exactly preserve every individual cell average with a shared linear surface.
Final frame absolute area-weighted RMS error: 4.46 cm; cellwise 95th percentile:
8.85 cm; maximum: 79.7 cm. See workplan section 17 for limits and full results.

Re-run the layer fixture by setting `VFR_TEST_BELLINGE` to the copied HDF5 file,
`VFR_TEST_ARTIFACTS` to this directory, and invoking
`test_2dresults_vizfixes bellingeNeighborhoodSavedLiveAndRendering`.
Set `VFR_TEST_GPU=1`, `QT_QPA_PLATFORM=cocoa` and `QSG_RHI_BACKEND=opengl` for
native GPU captures. Offscreen mode runs the CPU and geometry checks.
