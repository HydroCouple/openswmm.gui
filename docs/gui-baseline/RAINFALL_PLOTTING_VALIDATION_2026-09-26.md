# Rainfall and saved-mesh plotting validation

## Scope

Follow-up to engine `e7fc5fe1` and GUI `643797b5`. The affected user model and saved results have not been supplied, so this does not establish the cause of the originally reported dry cells.

## Rainfall

The refreshed GUI bundle's engine passed all 17 rainfall tests, including US/SI inputs, INTENSITY/VOLUME/CUMULATIVE formats, natural and nearest modes, and mesh-only gage advancement through the end of a storm. The bundled library's Mach-O UUID matched the installed engine: `49D9E75A-CC1C-3EDC-BE4E-DFF1724ABFE9`.

An additional runtime probe exercised 800 triangular cells and three gages at projected coordinates near (500,000 m, 5,000,000 m). In both modes:

- All 800 cells received positive rainfall during a uniform 25.4 mm/hr interval.
- Every cell had finite, nonnegative weights summing to one.
- During a subsequent interval with gages at 0, 10 and 40 mm/hr, delivered rainfall matched the weighted values (maximum error below 3.4e-21 m/s).
- All cells returned to zero rain after the storm, and all accumulated positive rainfall volume.
- Nearest mode selected the geometrically closest gage for every cell. Natural mode used natural-neighbour weights in 225 cells and inverse-distance fallback in 575 cells outside the gage hull.

The full probe, decks and outputs are retained locally under `build/rainfall-validation/`. These are controlled fixtures, not a reproduction of the user's model. Plot rainfall units remain mm/hr for both US and SI projects: 1 in/hr is displayed as 25.4 mm/hr. The cumulative rainfall output is volume in m³, not depth.

## Additional confirmed defects

### Compressed result files

The GUI dependency manifest disabled HDF5's default features without enabling `zlib`. Reading a genuinely gzip-compressed rainfall/depth frame failed because the deflate filter was unavailable. The existing road-culvert example declares deflate in its dataset metadata but skips that filter in its stored chunks, so it did not expose this problem.

The manifest now explicitly enables HDF5's `zlib` feature. The reader regression suite writes a **mandatory-deflate** rainfall chunk, verifies that compression was actually applied, and checks its decoded values. The rebuilt reader suite passes.

### macOS package signing

The build copied QPropertyModel into the app after code signing; the install rule also copied it after install-time signing. The initial package failed `codesign --verify --deep --strict` and identified this library as modified. `macdeployqt` already deploys this linked library before signing, so the redundant macOS copy/install rule was removed. Windows and Linux copies remain.

## Repeatable plotting benchmark

`test_plot_batch_counts::savedResultsBenchmark` is opt-in and uses the real saved-file adapter and Comparison Plot dialog. It requests 1, 10 and 50 cells with either depth alone or depth/rainfall/velocity magnitude. Every batched series is checked against cached results; an optional scalar comparison also checks separately extracted series.

```sh
python3 tests/tools/generate_plot_benchmark.py build/plot-10000.2d.h5
SWMMVIS_PLOT_BENCHMARK_FILE="$PWD/build/plot-10000.2d.h5" \
QTEST_FUNCTION_TIMEOUT=900000 \
SWMMVIS_PLOT_BENCHMARK_SCALAR=1 QT_QPA_PLATFORM=offscreen \
  build/tests/gui/test_plot_batch_counts savedResultsBenchmark
```

The generator requires NumPy and h5py and refuses to overwrite an existing file. Its default fixture has 10,000 cells and 1,000 periods, float64 fields, and gzip level 4 whole-frame chunks; size is about 353 MB. Fields vary in space and time but are **synthetic performance data**, not hydraulic results.

Timing terms:

- **Uncached batch:** fresh adapter cache; operating-system disk caches are not flushed.
- **Cached batch:** repeat the same request on that adapter.
- **Separate series:** the current single-series extraction path, initially uncached. This is not an old-version application benchmark and excludes the historical repeated chart rebuilds.
- **Cached dialog add:** a fresh dialog using already cached series; isolates chart/statistics construction from file extraction.
- **Dialog add:** one batched addition to a fresh dialog, including extraction, chart construction and statistics. Offscreen measurement excludes on-screen GPU presentation.

Other local builds ran during some measurements. Times are indicative local observations, not a hardware-independent performance guarantee.

## Measurements

Release build, Apple Silicon, Qt 6.9.3. Values below are seconds. Both complete batched/cached benchmark matrices passed, including equality checks against the cache. The small example also passed all scalar equality checks.

| File dimensions | Cells selected | Variables | Uncached batch | Cached batch | Dialog add | Cached dialog add |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 2,000 cells × 252 periods | 1 | 1 | 0.010 | 0.0006 | 0.011 | 0.003 |
| 2,000 cells × 252 periods | 1 | 3 | 0.095 | 0.0031 | 0.056 | 0.007 |
| 2,000 cells × 252 periods | 10 | 1 | 0.010 | 0.0005 | 0.040 | 0.023 |
| 2,000 cells × 252 periods | 10 | 3 | 0.073 | 0.0005 | 0.162 | 0.060 |
| 2,000 cells × 252 periods | 50 | 1 | 0.014 | 0.0005 | 0.069 | 0.060 |
| 2,000 cells × 252 periods | 50 | 3 | 0.076 | 0.0006 | 0.217 | 0.164 |
| 10,000 cells × 1,000 periods | 1 | 1 | 0.543 | 0.0008 | 0.488 | 0.002 |
| 10,000 cells × 1,000 periods | 1 | 3 | 3.181 | 0.0005 | 2.826 | 0.005 |
| 10,000 cells × 1,000 periods | 10 | 1 | 0.485 | 0.0005 | 0.483 | 0.011 |
| 10,000 cells × 1,000 periods | 10 | 3 | 3.134 | 0.0006 | 2.885 | 0.035 |
| 10,000 cells × 1,000 periods | 50 | 1 | 0.547 | 0.0006 | 0.587 | 0.061 |
| 10,000 cells × 1,000 periods | 50 | 3 | 2.848 | 0.0008 | 3.930 | 0.391 |

The large file's separate-series profiling run completed the 10-cell/3-variable case in 40.14 s versus 3.24 s batched, with identical values. That run also completed the 50-cell depth-only case (35.11 s separate versus 0.52 s batched). Its final 50-cell/3-variable scalar baseline exceeded QtTest's **five-minute limit for the entire benchmark method**; this is not a measured five-minute latency for that individual selection. No scalar time is reported for that final case. A subsequent full matrix with scalar profiling disabled passed in 26.5 s. Set `QTEST_FUNCTION_TIMEOUT=900000` when deliberately profiling the slow scalar path.

These comparisons measure removal of repeated extraction in the current code, not an old application checkout. The historical repeated chart-rebuild cost would add further work and is not quantified here.

## Final package checks

- Rebuilt `build/SWMMVis.app` with the installed engine and zlib-enabled GUI reader.
- Regenerated the packaging rules and verified that no library copy follows final signing. Re-signed the completed local bundle; `codesign --verify --deep --strict` passes.
- Re-ran all 17 engine rainfall tests and the 800-cell probe against the final bundled engine, after another local engine build refreshed the installed library. Both pass; the UUID above identifies the final bundle.
- Freshly rebuilt `test_mesh2dh5reader` and `test_plot_batch_counts`: both CTest suites pass. The optional benchmark is skipped in normal CTest runs.
- Offscreen launch opened Preferences → Simulation Defaults → 2D Coupling & Rainfall and captured a nonblank image. With a 10-second startup allowance, the application exited with status 0. Earlier 1.5-second captures completed before normal startup had settled and needed timeout cleanup; they do not establish a normal GUI shutdown failure.

Logs, fixture data, benchmark JSON, screenshots and the runtime probe are retained under `build/rainfall-validation/`. The benchmark generator and test are committed; large generated files are excluded. Deployment still reports missing optional Mimer/ODBC/PostgreSQL driver dependencies from the Qt SDK; those database plugins were not exercised by this validation.

## Remaining work, in priority order

1. **Inspect the affected run.** Obtain its model and saved `.2d.h5` paths, suspect cell IDs and time window. Use the rainfall-weight API to distinguish valid dry contributors, missing/unlocated/duplicate gages, coordinate/unit mismatches and sampled storm timing. The controlled tests do not establish which applies to that run.
2. **Background extraction with progress and cancellation.** The large-file results confirm that initial extraction still blocks the window for several seconds. Use a worker-owned reader; do not move the live layer or share its mutable HDF5 handle. This build's HDF5 is not thread-safe (`H5_HAVE_THREADSAFE` is undefined), so first provide a thread-safe HDF5 build with coordinated access, or isolate extraction in a helper process. Keep chart objects on the GUI thread and reject results after source/selection changes. Test cancellation, closing a project, replacing a source and stale completions. Target a responsive window during reads and cancellation between bounded frame operations.
3. **Retain dataset handles and profile again.** Whole-frame reads remain appropriate for existing files, but dataset opens can be reused within the owned reader. Verify output equality and rerun this matrix before changing future output chunk layouts.
4. **Measure larger rendering workloads.** Cached creation of 150 × 1,000-point series took about 0.39 s, substantially less than extraction but still noticeable. If longer histories make rendering dominant, use peak-preserving display reduction while keeping full values for statistics/export.

Background loading/cancellation, persistent dataset handles and display reduction are **not implemented by this follow-up**. The new work supplies the dependency/package fixes, repeatable measurements, and the evidence to prioritize that implementation.
