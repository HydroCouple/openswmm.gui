# Phase 20 — projected terrain coordinate precision

This independent W2 checkpoint addresses the confirmed M6 horizontal-coordinate
loss in terrain normal scoring. It does not complete W2 or feature-preserving
thinning.

## Change

`DTMThinner::fillBandGrid` previously narrowed absolute map coordinates to floats.
At UTM-scale northings this collapsed or displaced sub-metre neighbours before
their normal vectors were calculated. The same narrowed values also became the
returned Steiner coordinates.

Normal scoring now stores horizontal offsets from the band's origin. These offsets
are calculated directly from double-precision lattice indices and spacing before
the float cast, without adding arrays or increasing the existing band budget.
Returned coordinates are reconstructed from the original global double lattice.
Raster sampling still uses global double coordinates and the existing elevation
sampling path.

The mesh stage cache format advances to version 2 so a previously rounded terrain
result cannot be reused after this fix. Cache entries regenerate normally.

## Regression evidence

The new `test_dtmthinner_precision` uses retained, inspectable GeoTIFF fixtures and
covers 0.125, 0.25, 0.5 and 1.0-unit grids translated from the origin to UTM-scale
(500,000; 4,500,000) and state-plane-scale (10,000,000; 12,000,000) coordinates.
Each configuration tests minimum and average normal scores, and single-band and
forced multi-band processing. It compares retained count, row-major point identity,
and sampled elevation exactly. A separate no-thinning case checks every point of
the projected 0.125-unit lattice.

- Before the fix: 6 passed, **29 failed** (including QtTest setup/cleanup).
- After the fix: **35 passed, 0 failed**.
- Representative failure: the 0.125-unit UTM copy retained 2,600 points where the
  identical origin-centred terrain retained 225. The corrected results agree.
- The isolated executable compiles the actual production `dtmthinner.cpp` and
  `mapextent.cpp`, using the configured Qt/GDAL libraries and pinned macOS SDK.
- Source whitespace/diff checks passed.

Evidence and all raster fixtures are retained under
`workplans/artifacts/phase_20_parallel/terrain/`: `before_test.log`,
`after_test.log`, `before_compile.log`, `after_compile.log`, the executables,
`run_precision.py`, and the `fixtures_before`/`fixtures_after` directories.

Integrated CMake validation passed all four suites: precision (35), banded (11),
sample-many (9), and stage cache (8): **63 QtTest passes including fixtures**,
no failures or skips. See `terrain/integrated_tests.log`. The parent phase owns
the combined application rebuild and GUI launch. The focused target
must use the same sources and GDAL linkage as `test_dtmthinner_banded`.
`SWMMVIS_TERRAIN_PRECISION_OUTPUT` selects a retained fixture directory; when unset,
the test writes to `terrain_precision_output` under its working directory.

## Remaining limits

This correction does not normalize geographic degrees or mixed horizontal and
vertical units; those require the separate W2 metric-frame work. Raster values and
normal scores retain their existing float precision. The exact translated-copy
checks use binary-representable spacings and offsets; they do not claim exact
sampling equality for every floating-point geotransform. This also does not add
road/river constraints, bound final terrain reconstruction error, or change
Poisson filtering and downstream meshing. Those remain in the approved W2/W5
work packages.
