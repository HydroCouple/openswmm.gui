# Comparison Plot — Series Tree Names, 1v1 Fixes & Configurable Pairs

Date: 2026-06-04
Status: IMPLEMENTED (2026-06-04; see Implementation Notes at end)
Scope: `ComparisonPlotDialog`, `ComparisonPlotModel`, GUI tests.
Decisions (user-confirmed): pair-list-per-row config; 1v1 on by default with toolbar toggle; tree labels use output file name fallback.

---

## Findings (root causes)

1. **Tree shows "(baseline)" and blank labels.**
   `rs.label = rs.layer->scenarioName()` at `comparisonplotdialog.cpp:652, 690, 1038` and `comparisonplotmodel.cpp:27`. `SWMMResultsLayer::scenarioName()` returns `m_scenarioName`, which is empty unless explicitly set (Scenario panel). So every run label is empty; the baseline item only *looks* labeled because of the "⊙ … (baseline)" decoration in `rebuildSeriesTree()` (lines 786–790).

2. **No zoom/pan/wheel on 1v1 plots.**
   `rw.scatterView = new QChartView(...)` (line 1467) — a plain `QChartView`. `propagateModeToRows()` (lines ~373–388) only calls `setMode()` on `rw.view` (the time-series `InteractiveChartView`). `InteractiveChartView` is axis-agnostic (drives `QChart::scroll/zoomIn/zoomReset`), so it works on the scatter's `QValueAxis` charts as-is.

3. **1v1 broken on second+ rows.**
   The scatter is built inside the per-row loop (`rebuildCharts()`, lines 1311–1470), so all rows are *attempted*. The chart is created **even when no baseline/comparison pairs exist**, producing an empty 0–1 chart. Candidate root causes to confirm with a test:
   - baseline run has no series for that row's attribute → `baseByObj` empty → every comparison series skips (line 1366);
   - `halfStep = 0.5·|bt[1]−bt[0]|` is `0.0` when the baseline series has <2 points → pairing requires exact double equality (line 1388);
   - timestep mismatch between runs starves the nearest-match walk.
   Per the AT spec (GUI_IMPLEMENTATION_PLAN.md §5129–5131), rows with no pairs should *hide* the scatter pane, not show an empty chart.

---

## Phases

### Phase 1 — Run labels from output names
- `ComparisonPlotModel::addRunSource()`: fallback chain — explicit `label` → `layer->scenarioName()` → `QFileInfo(layer->persistenceKey()).fileName()`.
  (`SwmmOutRunLayer::persistenceKey()` = results file path; observed CSV / mesh layers return their own keys — strip to file/display name.)
- Remove the now-redundant per-call-site `rs.label = ...scenarioName()` assignments in the dialog (the model fallback covers them).
- `rebuildSeriesTree()` unchanged structurally: top-level item per run, real name shown, baseline keeps "⊙ … (baseline)".
- **Verify:** extend `test_comparisonplot_seriestree.cpp` — stub layer with empty `scenarioName()`, assert top-level item text contains the persistenceKey file name.

### Phase 2 — Fix second-row 1v1 + hide empty scatters
- New test `test_comparisonplot_scatter.cpp`: 2 stub runs × 2 attributes, same object → assert `m_rowWidgets[0]` **and** `[1]` have non-empty `scatterSeries`. Reproduce the failure first.
- Fix per diagnosis; expected fixes:
  - guard `halfStep == 0` (fall back to a small epsilon or comparison-stream step);
  - derive `halfStep` from min(baseline step, comparison step) so coarser/finer runs still pair.
- Only create `rw.scatterView` when ≥1 scatter series got points; otherwise leave it null (row shows time-series full-width).
- **Verify:** new test passes; existing comparisonplot tests still pass.

### Phase 3 — Zoom/pan/wheel on 1v1 plots
- `RowWidgets::scatterView` type → `InteractiveChartView*` (header line 215).
- `propagateModeToRows()`: also `rw.scatterView->setMode(m)`.
- Connect `chartContextMenuRequested` on scatter views → menu with Reset Zoom (reuse row lambda pattern).
- Do **not** connect `xRangeSelectionChanged` for scatter views (signal is QDateTime-typed; scatter axes are value axes).
- **Verify:** extend `test_comparisonplot_toolbar.cpp` — after mode change, scatter view mode matches.

### Phase 4 — Optional 1v1 column (CP.1 toggle pattern)
- New checkable toolbar action `m_actShow1v1` ("1v1 Plots"), default **checked**, placed with the CP.1 View toggles (buildToolBar lines 322–365).
- Off → `rebuildCharts()` skips the scatter build (saves compute); on → rebuild restores it. Handler triggers `rebuildCharts()`.
- "Charts Only" toggle leaves 1v1 state alone (it governs panels, not chart columns).
- **Verify:** toolbar test — toggle off → all `scatterView == nullptr`; toggle on with 2 runs → scatters rebuilt.

### Phase 5 — Configurable 1v1 comparisons (MVC: config lives in model)
- **Model** (`comparisonplotmodel.h/.cpp`):
  - `struct ComparisonPair { int xSeriesIndex = -1; int ySeriesIndex = -1; };`
  - `QVector<ComparisonPair> m_pairs` + `addPair / removePair / pairs() / clearPairs`, signal `pairsChanged()`.
  - Validation: both series valid and same `PlotAttribute` (same row).
  - `removeSeries()` drops/reindexes pairs referencing removed series.
  - Empty pair list = **auto mode** (current baseline-vs-others behavior).
- **UI**: "Configure 1v1…" toolbar button → small modal dialog (new `comparisonpairsdialog.h/.cpp`, matching existing dialog file conventions):
  - table of pairs: X-series combo, Y-series combo (combos grouped by attribute row, labeled with run + object + attr);
  - Add / Remove buttons; "Reset to Auto (baseline vs all)".
- **`rebuildCharts()` scatter section**: if the model has pairs for this row, build one scatter series per pair (reuse the timestep nearest-pair walk; X = x-series values, Y = y-series values; fit metrics per pair, best NSE in title). Else, auto behavior.
- **Verify:** model tests (add/remove/reindex/validation); scatter-from-pairs test.

---

## Order & dependencies
1 → 2 → 3 → 4 → 5 (5 builds on 2's scatter refactor; 4's skip-flag wraps the same block).

## Files touched
- `src/plot/comparisonplotmodel.cpp`, `include/plot/comparisonplotmodel.h`
- `src/ui/dialogs/comparisonplotdialog.cpp`, `include/ui/dialogs/comparisonplotdialog.h`
- new `include/ui/dialogs/comparisonpairsdialog.h`, `src/ui/dialogs/comparisonpairsdialog.cpp`
- `tests/gui/`: new `test_comparisonplot_scatter.cpp`, `test_comparisonplot_pairs.cpp`; extend `test_comparisonplot_seriestree.cpp`, `test_comparisonplot_toolbar.cpp`; `tests/gui/CMakeLists.txt`, root `CMakeLists.txt`

## Out of scope (per AT spec, deferred — flag if wanted)
- Linear-interpolation resampling when report steps differ (spec §5053; current nearest-match retained).
- Per-row regression/best-fit line + R² toggle ("tiny toolbar" from spec).

---

## Implementation Notes (2026-06-04)

Deviations from the plan, all small:

- **Phase 2** — the pairing walk was extracted to `include/plot/seriespairing.h`
  + `src/plot/seriespairing.cpp` (`pairSamplesNearest`). The dialog can't link
  into the self-contained GUI tests (GDAL-heavy layer deps), so the pure
  function is what `test_comparisonplot_scatter.cpp` pins. Tolerance uses
  half the SMALLER of the two streams' report steps (plan said min — kept),
  with an epsilon fallback so <2-sample streams still pair exact timestamps.
- **Phase 3** — no dialog-level mode-propagation test (same linkage
  constraint); `propagateModeToRows` simply calls `setMode` on both views,
  and `InteractiveChartView`'s mode API stays pinned by
  `test_comparisonplot_toolbar.cpp` (which already exercises QValueAxis charts).
  Scatter context menu = Reset Zoom only.
- **Phase 5** — axis titles: auto mode keeps "Baseline"/"Comparison"; one
  configured pair on a row names the actual series; several pairs fall back
  to "X series"/"Y series".
- Verification in-session: `pairSamplesNearest` compiled with
  `-Wall -Wextra -Werror` and all case assertions passed; the Qt-dependent
  test binaries couldn't be built in the sandbox (no Qt6) — run
  `ctest -L gui -R comparisonplot` locally to execute
  `test_comparisonplot_scatter`, `test_comparisonplot_pairs`, and the
  extended `test_comparisonplot_seriestree`.
