# Verify handoff — Live 1D results by tailing the `.out` (2026-09-03)

Implements `LIVE_1D_RESULTS_PLAN_V2_2026-09-03.md`. Authored without a
compiler (sandbox has no cmake/Qt): engine sources passed `g++ -fsyntax-only`
against vcpkg's HDF5/gtest headers; the GUI was **not compiled**. Your job:
build both repos, run the suites, fix what does not compile or fails (keep
fixes minimal and inside this scope), then run the smoke + parity checks.

Companion handoff for the earlier, still-unverified GUI commits of this
week: `2D_CONTEXT_MENU_RAINFALL_VERIFY_HANDOFF_2026-09-02.md` — do that one
first if it has not been done; this feature builds on the same tree.

## Commits

| Repo | Commit | Subject |
|---|---|---|
| `openswmm.engine` | (see `git log --oneline -1`) | feat(output): live .out reading — open_live/refresh + per-period flush |
| `openswmm.gui` | (see `git log --oneline -1`) | feat(results): live 1D results — tail the .out during a run |

Both trees carry **unrelated uncommitted work** (engine: S4 transport hunks;
GUI: offset-mode / misc). Do not stash; commit fixes with `git add -p` or a
filtered patch. `.git/_stale_locks/` in both repos is inert lock/tmp-object
junk the sandbox could not unlink — `rm -r` it.

## What changed

### Engine
- `src/engine/plugins/DefaultOutputPlugin.cpp` `update()`: `std::fflush` after
  the system-results write.
- `src/legacy/engine/output.c` `output_saveResults()`: `fflush(Fout.file)`
  after the `SysResults` write (before the outlet-interface write).
- `src/engine/output/OutputReader.{hpp,cpp}`: `openLive(path)` (forward header
  parse: `id_start = ftell after 7 ints`, skip `n_polluts` unit ints →
  `input_start`, then the existing `readVariableCodes()` → `output_start =
  ftell`), `refresh()` (size-based count; adopts footer when
  `size == output_start + periods*bpp + 24` and magic matches), `is_live()`,
  `live_` member; `close()` resets it.
- `src/engine/output/openswmm_output_impl.cpp` + `include/openswmm/engine/
  openswmm_output.h`: `swmm_output_open_live`, `swmm_output_refresh`,
  `swmm_output_is_live`.
- `tests/unit/engine/test_output_reader_live.cpp` (+ CMake entry
  `test_engine_output_reader_live`): truncated copies of
  `data/site_drainage_model.out` written to `data/live_out/` (gitignored).

### GUI
- `SWMMResultsLayer`: `openResultsLive(errors)`, `refreshLive()`, `isLive()`,
  signals `periodsAppended(first,count)` / `resultsFinalized()`; `finishOpen`
  tolerates 0 periods in live mode; `closeResults` resets `m_live`.
- `PreferencesManager`: `liveResults1DEnabled()` / setter
  (`SWMMVis/.../Simulation/LiveResults1D`, default true).
- `SWMMVis`: Results-toolbar "Live 1D" checkbox (`mCheckBoxLive1D`);
  `mLive1DLayers`; `findOrCreateResultsLayer()`; `tickLive1DResults()` driven
  by `SimulationRunner::progressChanged`; the 1D `finished` handler finalises
  the live layer in place (`refreshLive`) and falls back to the classic
  close+open otherwise.
- `ComparisonPlotDialog::appendChartTails()` — connected to
  `SWMMResultsLayer::periodsAppended` (in `ensureRunSourceForLayer`) and to
  `SWMM2DResultsLayer::timeRangeChanged` (in `ensureRunSourceForMeshLayer`).
- `ProfileSourceFetcher::appendTail()`, `ProfileBuilder::appendPeriods()` +
  `accumulatePeriod()` (kernel shared with `compute()`),
  `ProfilePlotDialog::appendLivePeriods()` with `m_sourceSeriesCache` and the
  `m_rebindsInFlight` guard; unit tests appended to
  `tests/unit/test_profilebuilder.cpp`.

## Step 1 — Engine

```
cd ~/Documents/Projects/cbuahin_github/openswmm.engine
cmake --build build-arm64-osx -j 8 2>&1 | tee build/verify_live_out_build.log | grep -E "error"
(cd build-arm64-osx && ctest --output-on-failure -R "output_reader_live|output_node_stats|output_quality|plugin_lifecycle|legacy" 2>&1 | tee ../build/verify_live_out_ctest.log | tail -25)
cmake --install build-arm64-osx
```
Pass: all green; `test_engine_output_reader_live` writes
`tests/unit/engine/data/live_out/{header_only,k_periods,k_periods_partial,growing}.out`.
Likely trouble: `readVariableCodes()` assumes the input-property block
sizes (1 / 3 / 5 props) — if `output_start` from `openLive` disagrees with
the footer's on the fixture, `FinishedFileOpensLiveAsFinal` fails; compare
the two offsets in a debugger and fix the forward parse, not the writer.

Then the flush timing check: run a large benchmark `.inp` twice (this
commit vs its parent) and record wall time in
`tests/artifacts/live_1d/flush_timing.csv`; expect ≤ noise.

## Step 2 — GUI build + tests

```
cd ~/Documents/Projects/cbuahin_github/openswmm.gui
./build-gui.sh 2>&1 | tee build/verify_live_1d_build.log | grep -E "error:"
(cd build && ctest --output-on-failure -R "profilebuilder|comparisonplot|resultslayer|preferences" 2>&1 | tee ../build/verify_live_1d_ctest.log | tail -30)
```
Likely trouble spots:
- `swmmresultslayer.cpp`: `swmm_output_open_live/refresh/is_live` need the
  **installed** engine header (Step 1's `cmake --install`).
- `swmmvis.cpp`: the `progressChanged` lambda takes 5 of the signal's 7
  parameters (Qt allows fewer) — if the compiler objects, match all 7.
- `comparisonplotdialog.cpp`: `QLineSeries::append(const QList<QPointF>&)`,
  `line->at(i)` — both Qt 6 API; `std::any_of` needs `<algorithm>` (present).
- `profileplotdialog.cpp`: `ProfileBuilder::kGravityFps2` is what
  `rebindSources` already uses; `appendLivePeriods` is declared private.
- `test_profilebuilder.cpp`: `ASSERT_EQ` on `QVector<double>` needs
  `operator==` (Qt provides) — NaN slots compare unequal! If
  `AppendPeriods_MatchesFullComputeExactly` fails only on rows that contain
  NaN (a node absent from the output), replace the row compare with an
  element-wise `EXPECT_TRUE(a==b || (isnan(a)&&isnan(b)))`. The fixture
  here has no NaNs, so it should pass as written.

## Step 3 — Smoke (needs a model that runs ≥ 20 s; use a long REPORT span)

For BOTH the 6.x engine and the legacy 5.3.0 worker (status-bar engine combo):
1. Results toolbar → "Live 1D" checked. Run. Within ~1 s of the first
   progress tick a results layer appears in the layer tree, the animation
   slider's range grows as the run proceeds, and the map colours animate if
   you press Play.
2. Before starting the run, open a Profile plot and a Comparison plot on
   nodes of the model; during the run both extend to the right tick by tick
   with no chart flicker (Comparison: the `QChart` objects are not recreated
   — set a breakpoint in `rebuildCharts` to confirm it is not hit per tick).
3. Open a second Comparison plot mid-run → it shows full history from
   period 0 (the file has it).
4. Cancel mid-run → partial results stay loaded; the layer is no longer
   live (`isLive()` false) once the engine wrote the footer.
5. Re-run the same model → no stale-handle error; the same layer object is
   reused (layer tree does not gain a duplicate).
6. "Live 1D" unchecked → old behaviour: nothing appears until finish.
7. Legacy worker only: confirm with `ls -l` that the `.out` grows during the
   run in period-sized steps (the flush), not in ~4 KB jumps.

## Step 4 — Parity (acceptance)

With the plots from Step 3.2 open, export the Comparison plot's row data
("Export Row Data…") to `tests/artifacts/live_1d/<engine>/live_series.csv`
just before the run ends, and again after it finishes to
`out_series.csv`. Assert the earlier file is a prefix of the later one
(same values, not epsilon — same file). Repeat once with
`[REPORT] AVERAGES YES` and a non-zero `REPORT_START`.

## Step 5 — Report

Append results + any fixes under `## Results` here; commit fixes as
`fix(results): …` / `fix(output): …` without sweeping the unrelated hunks.
