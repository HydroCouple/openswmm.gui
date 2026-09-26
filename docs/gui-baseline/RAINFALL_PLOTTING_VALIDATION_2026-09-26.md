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

## Update: background extraction (item 2), 2026-09-26

Implemented; uncommitted at the time of writing.

- **Coordinated HDF5 access.** `Mesh2DH5Reader` is the only HDF5 user in the GUI process. GDAL is built without HDF5, and the engine links its own private static copy. Every reader entry point that calls the library now holds one process-wide recursive lock for that call (one frame). Cached getters take it only on a miss. This gives the non-thread-safe build the serialization a thread-safe build would provide, without changing the HDF5 port.
- **Worker-owned reader.** `Mesh2DExtractionJob` opens its own `HDF5Mesh2DSource` and carries copies of the bed-elevation and cell-size geometry. No handle, layer or adapter state is shared with the GUI thread. The frame loop is the same code as the direct batch path (`extractCellFrames_`), and a test asserts identical values.
- **Dialog.** A comparison plot over a saved-results mesh run queues uncached cell series (`setDeferFileReads`); they show "Loading…" until the job's results are accepted. A strip under the toolbar shows progress and a Cancel button; after a cancel the series show "Loading cancelled" and the button becomes Retry. A job whose source was replaced meanwhile is rejected and re-queued against the new source. Closing the dialog cancels and joins, waiting at most one frame read. Export Data reads directly, since it needs every value. Live runs, tails and vertex/edge series are unchanged.
- **Tests** (`test_plot_batch_counts`, real `.h5` fixture in `test_artifacts/`): worker results equal the direct read; a cancel is honoured before the first frame and waits for Retry; a stale job is rejected; the dialog adds without reading on the GUI thread and loads on the worker; Cancel → Retry → close-while-loading. 30 repeated runs passed, as did 17 related plot, 2D-results and reader suites.

Same 10,000 × 1,000 file (regenerated with `tests/tools/generate_plot_benchmark.py`), Release, load average about 6. The GUI stall columns are the longest gap between 10 ms timer ticks. The benchmark waits without resolving series while the worker runs, like the application.

| Cells × variables | Dialog add, before (GUI blocked) | Dialog add, now (GUI blocked) | Loaded (worker) | Worst stall while loading | Worst stall overall |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 × 1 | 488 ms | 3.7 ms | 522 ms | 17 ms | 17 ms |
| 1 × 3 | 2,826 ms | 1.7 ms | 2,354 ms | 31 ms | 31 ms |
| 10 × 1 | 483 ms | 1.4 ms | 414 ms | 17 ms | 19 ms |
| 10 × 3 | 2,885 ms | 3.5 ms | 2,352 ms | 22 ms | 39 ms |
| 50 × 1 | 587 ms | 4.6 ms | 515 ms | 12 ms | 38 ms |
| 50 × 3 | 3,930 ms | 12.2 ms | 2,406 ms | 16 ms | 117 ms |

The one stall over 100 ms is the GUI-thread chart and statistics rebuild of 150 × 1,000-point series after the results arrive (item 4 below), not extraction. An earlier variant of the benchmark resolved series while the worker ran; each resolve's HDF5 `timeCount()` then waited behind the worker, producing 68–187 ms stalls. A map animation reading frames during a load can wait in the same way, for at most one worker HDF5 call. The longest such call is the one-time edge-geometry read for velocity. Logs: `build/rainfall-validation/benchmark-10000-async*.log`.

Remaining, in order: inspect the affected run (item 1, needs the model); retain dataset handles inside a reader (item 3); reduce the post-load rebuild for large selections (item 4).

## Update: rainfall on dry cells, 2026-09-26

Reproduced on Bellinge (`BellingeSWMM_v021_nopervious_test.inp`, saved `.2d.h5`, 185,779 cells, 15-minute records, two 1-minute VOLUME gages). Uncommitted.

**Dry cells receive rain.** Every cell has positive cumulative rain at the end of the run. The engine lands rain on inactive cells through the lazy source tier and books the per-cell cumulative volume for every cell. A four-cell engine test that never activates confirms this: stored water, booked volume and the held gage depth agree to 1e-9, and the booked volumes sum to the mass-balance rainfall inflow.

**Zeros in plots are sampling.** "Rainfall (2D cell)" is the instantaneous rate at each report time. In 3 of the 112 Bellinge intervals, 557,267 cell-intervals, 55,637 of them on dry cells, show 0 although rain fell. Summing the snapshots gives a median of 195 mm; 298.6 mm was delivered. Two derived plot variables now show this rain:

- **Rainfall, interval mean (mm/hr)**: the change in cumulative volume divided by cell area and interval. No value at the first record. An unreadable record widens the interval rather than reading zero.
- **Cumulative rainfall (mm)**: cumulative volume divided by cell area.

Cell area uses the engine's formula and matches `Mesh2_face_area` exactly on Bellinge. The summed interval means reproduce each cell's cumulative depth to 2e-13 mm (`build/rainfall-validation/dry_cell_rain_check.py`).

**Engine fix: the 2D rain field lagged the gages.** It was refreshed only after at least 30 s, so 1-minute gage records were shifted and partly lost for all cells, wet or dry (−12.3 % on a 1-minute on/off test storm at a 7 s step). It now also refreshes whenever a gage value changes. Bellinge re-run to 06/29 11:00, compared with the 1D subcatchment totals on the same gages:

| Gage site | 1D | 2D before | 2D after |
| --- | ---: | ---: | ---: |
| rg5425 | 194.00 mm | 193.82 mm | 194.96 mm |
| rg5427 | 236.01 mm | 227.84 mm | 235.97 mm |

Tests: engine rainfall suite 18/18 (new dry-cell test fails without the fix), output suite 15/15 (new between-reports pulse test), 2D subset 34/34; GUI `test_plot_batch_counts` 12/12 and 28 related suites. `test_2d_output_options_page` fails in the simulation-options dialog, which none of these changes touch. The app's bundled engine predates the fix and must be reinstalled.
