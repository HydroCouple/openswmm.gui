# Live 1D Results (Profile + Comparison Plots) — Revised Plan v2 (2026-09-03)

**Status:** IMPLEMENTED 2026-09-03 (uncompiled — see
`LIVE_1D_RESULTS_VERIFY_HANDOFF_2026-09-03.md`). Deviations from the text
below: (1) no new `SimulationRunner::resultsFileGrew` signal — the existing
`progressChanged` (both branches, `progressTickMs` cadence) drives
`SWMMVis::tickLive1DResults`; (2) `appendChartTails` resolves each series in
full and appends only newer points (no `IRunLayer` range overload yet — a
perf follow-up if short report steps on large models make it visible);
(3) the profile dialog caches the fetched `SourceSeries` beside the
`SourceDerived` so `appendTail` + `appendPeriods` work in place.
Supersedes
`workplans/LIVE_1D_RESULTS_GUI_PLAN_2026-08-04.md` and
`openswmm.engine/plans/LIVE_RESULTS_HOST_PLUGIN_PLAN_2026-08-04.md` once approved.
Nothing from either 2026-08-04 plan has been implemented; this revision changes
the delivery mechanism, so it needs a decision before any code is written
(CLAUDE.md §5.0).

**Scope change requested 2026-09-03:** live 1D results for **both** the
refactored (6.x) engine **and** the legacy (5.x) engines. The 2026-08-04 plans
excluded legacy by decision; §1 explains why that decision was also the wrong
lever and what replaces it.

---

## 0. Validation of the 2026-08-04 plans against today's code

Audited 2026-09-03 against `openswmm.gui` @ `bb03c58` and `openswmm.engine`
@ `d230c412`.

| Finding | Consequence |
|---|---|
| **Nothing landed.** `swmm_engine_add_output_plugin`, `HostPluginRegistration.hpp`, `LiveResultsBuffer`, `LiveResultsPlugin`, `LiveEngineRunLayer`, `appendChartTails`, `ProfileBuilder::appendPeriod`, `liveResults1DEnabled` — zero hits in both repos. | Free to change approach without migration. |
| **Legacy runs are a separate process.** `SimulationRunner` spawns `openswmm-legacy-worker` (`src/simulation/simulationrunner.cpp:660-678`) and reads JSON progress lines from its stdout; the GUI links no legacy engine symbols at all. | The v1 in-process `IOutputPlugin` design is *architecturally unreachable* for legacy, not merely "gated off". Any legacy-capable design must work across the process boundary. |
| **The `IOutputPlugin` sketch in v1 §4 would not compile.** Real interface (`include/openswmm/plugin_sdk/IOutputPlugin.hpp:85-167`): pure-virtual `state()` (omitted in v1), `initialize(args, const IPluginComponentInfo*)` (v1 had `void*`), `finalize(const SimulationContext&)` (v1 had no-arg). A plugin that does not drive its own `state()` through VALIDATED→PREPARED is silently skipped by `prepare_all` / `update_all` (`PluginFactory.cpp:475, 493-495`). | v1 §4 must be rewritten if the plugin route is kept. |
| **No "optional API" precedent exists** in `simulationrunner.cpp` (v1 §5 cites `:515,539`; those lines are now the progress emit and the 2D depth emit; no `dlsym`/weak-symbol guard anywhere). | v1 Phase 6's "follow the precedent" step has nothing to follow. |
| `swmm_engine_start(eng, 1 /* save_results */)` at `simulationrunner.cpp:393`. | v1's "confirm save_results" item is already satisfied. |
| **Every file:line citation in both plans is stale** (`SWMMEngine.cpp` by 250-1300 lines; `swmmvis.cpp` finish handlers now at `:7842` (1D) and `:8162` (2D); `.out` open/swap at `:7938-7945`; `mActive2DResultsLayers` at `swmmvis.h:789`; `rebuildCharts` `:1306`, `onRowsChanged` `:858` (still a no-op), `onAnimationTimeChanged` `:901`; profile `rebindSources` `:1137`, `invalidateSourceCacheFor` `:1344`, `ensureCacheInvalidationWired` `:1355`; `progressTickMs` `preferencesmanager.cpp:893`). | Citations below are current as of the audit. |
| Structural claims that **still hold:** IO queue depth 8 with blocking `post()` (`IOThread.hpp:83`, `IOThread.cpp:61-75`); `SwmmOutRunLayer::periodCount()` re-polls the layer (`swmmoutrunlayer.cpp:52-57`); `ProfileBuilder::compute` has no incremental entry; `ComparisonPlotDialog::rebuildCharts` is a full teardown; `.out` is opened only in the finish handler; `SWMMResultsLayer::totalTimeSteps()` is a cached member (`swmmresultslayer.cpp:1886`); `AnimationController` takes its range from `totalTimeStepsChanged` (`animationcontroller.cpp:51, 176`). | The *view-side* work in v1 (incremental append, tail fetch) is still needed under any mechanism. |

---

## 1. The mechanism decision: tail the `.out` file

### 1.1 Options

| | A. Host-registered `IOutputPlugin` + in-memory ring (v1) | **B. Tail the growing `.out` (proposed)** | C. Stream values over the legacy worker's stdout |
|---|---|---|---|
| Refactored engine | yes | yes | — |
| Legacy engine | **no** (separate process) | **yes** — same file format, same writer path | yes |
| Parity with post-run `.out` | by construction (same snapshot) | **by identity** (it *is* the file) | only if the worker samples at report steps |
| History for a plot opened mid-run | no (ring + subscription; v1 §9 non-goal) | **full**, from period 0 | no |
| Memory ceiling | ring × channels (v1 §6: ~17 MB typical) | **disk**; GUI reads only what is plotted | ring again |
| New engine surface | exported C++ ABI (`SWMM_ENGINE_API` on a `plugin_sdk` header), lifecycle contract, ownership transfer | **two small C functions on the existing read-only output API** + `fflush` per period in two writers | worker protocol extension + a GUI-side parser + a second buffer type |
| New GUI surface | buffer, plugin, adapter run-layer, subscription refcounting, window-relative index rule | **one new open mode + refresh on `SWMMResultsLayer`**; the layer the post-run already uses becomes the live one — no live→`.out` swap, no "(live)" qualifier, no backwards-extending jump | parser + adapter |
| Live map animation of 1D results (not requested, falls out for free) | no | **yes** — same layer drives the canvas | no |
| Latency | one report step | one report step + flush (§2.1) + GUI tick (≤ `progressTickMs`) | one report step |
| Threading hazards | `update()` must never block the IO queue (v1 §8 row 1) | none new: reader is on the GUI thread, writer untouched apart from `fflush` | worker stdout back-pressure |

### 1.2 Decision (recommended)

**B, for both engines.** A is retired (including the engine-side
`HostPluginRegistration` plan); C is unnecessary once B exists. Rationale in
priority order: (1) it is the only option that covers legacy without a second
mechanism; (2) it removes the v1 ring/subscription/window-index machinery and
the mid-run-open limitation entirely; (3) parity is structural — the file the
user scrubs after the run is the file they watched during it; (4) it makes the
1D map animation live as a side effect, matching what 2D already does.

What B gives up: sub-report-step latency (not a goal) and independence from
disk — a `.out` on a slow network share tails slowly (acceptable; the run is
writing there anyway).

---

## 2. Engine changes (`openswmm.engine`)

### 2.1 Writers flush each period

Neither writer flushes: `DefaultOutputPlugin` buffers **1 MB**
(`src/engine/plugins/DefaultOutputPlugin.cpp:92`, `update()` at `:111-215`,
no `fflush`); legacy `output_saveResults` (`src/legacy/engine/output.c`) uses
the stdio default buffer, no `fflush`. A tailing reader would see data in
buffer-sized jumps.

- `DefaultOutputPlugin::update()`: `std::fflush(file_)` after the system-results
  write (`:210`). This runs on the IO thread, so the sim thread never waits on
  it; one `write(2)` per report step is negligible next to the snapshot copy.
- `output_saveResults()` (legacy): `fflush(Fout.file)` after the `SysResults`
  `fwrite`. The legacy worker is the only in-tree legacy consumer the GUI uses.
- Keep the 1 MB `setvbuf` — it still batches the per-element `fwrite`s inside a
  period; only the period boundary is forced to disk.

Verify: engine timing on a large benchmark with/without the flush (expect
noise-level); `test_default_output_plugin` asserts the on-disk size equals
`output_start_pos + n*bytes_per_period` immediately after each `update()`.

### 2.2 Reader: live open + refresh

`OutputReader::open` (`src/engine/output/OutputReader.cpp:50-79`) reads the
footer first (`readFooter()` `:295-319`, `fseek(-24, SEEK_END)` + magic check)
and fails on a file that has none. The header is complete before period 0 —
`writeHeader` `:241-405` writes magic/version/units/counts, ID lists, property
tables, variable codes, start date + report step, and records
`output_start_pos_` at `:404` — so every offset the footer supplies is
derivable by parsing forward:

```
id_start_pos      = 7 * 4                       (after magic, version, flow units, 4 counts)
input_start_pos   = end of the ID + pollutant-unit block (variable length, parsed)
output_start_pos  = end of property tables + variable-code tables + REAL8 + INT4
bytes_per_period  = 8 + (Nsub*nSubVars + Nnode*nNodeVars + Nlink*nLinkVars + 15) * 4
n_periods(live)   = floor((fileSize - output_start_pos) / bytes_per_period)
```

`floor` is what tolerates a period that is only partially on disk (§2.1 makes
that rare, not impossible).

- `OutputReader::openLive(path)`: forward parse as above; `live_ = true`;
  `n_periods_` from the size formula. Same `fopen(path, "rb")` — MSVC defaults
  to `_SH_DENYNO` and both writers use plain `fopen("w+b")`, so Windows
  sharing is not a blocker (verified: no `_fsopen`/`CreateFile` anywhere).
- `OutputReader::refresh()`: `fstat`/`fseek(END)`; recompute `n_periods_`;
  if the trailing 24 bytes now carry `MAGIC_NUMBER`, read the footer, adopt its
  `n_periods` and error code, and clear `live_` (the run finished). Returns
  the new period count. Every existing bounds check (`period < n_periods_`,
  `:126, 138, 151, 162, 284`) keeps working unchanged.
- C API (`include/openswmm/engine/openswmm_output.h`):
  `SMO_Handle swmm_output_open_live(const char* path)` and
  `int swmm_output_refresh(SMO_Handle, int* periods)`. `swmm_output_open`
  is untouched (post-run behaviour identical).
- Node "stat" getters (`get_node_stat_max_depth` etc.) are computed from the
  series, so they work live; they are simply "so far".

Verify: `tests/unit/engine/test_output_reader_live.cpp` — write a small model
with `DefaultOutputPlugin`, copy the file at three points (0 periods, k
periods, after `finalize`), plus a copy truncated mid-record; `openLive` on
each yields the floor count; `refresh()` on a growing file tracks it;
`get_node_series` values read live equal the post-run read bit-for-bit; the
legacy CLI's `.out` for the same model opens live and finalises the same way.

### 2.2a Format compatibility (none broken)

- The `.out` byte layout is untouched: same header, per-period records and
  6-int footer as SWMM 5. `fflush` only moves the same bytes to disk earlier;
  a finished file is byte-for-byte what it is today, so EPA SWMM, PySWMM /
  `swmm-toolkit`, and the existing `swmm_output_open` keep working unchanged.
- `swmm_output_open` is not modified; `openLive`/`refresh` are additive.
- `openLive` depends only on the header layout, which legacy 5.x and the
  refactored writer share, so any existing `.out` (including one from a
  crashed run that never got its footer) opens live. `refresh()` is
  read-only: it must never synthesise the missing footer.

### 2.3 Legacy worker (optional, cheap)

`src/legacy/worker/main.cpp:100-130` already runs the `swmm_step` loop and
emits rate-limited `{"type":"progress",...}` lines. No change is required for
B — the GUI refreshes on progress ticks and the reader counts periods from
the file size itself (the legacy `Nperiods` counter is internal to
`output.c` and not needed). **Recommendation: no worker change.**

### 2.4 Retired

`plans/LIVE_RESULTS_HOST_PLUGIN_PLAN_2026-08-04.md` (all four phases). Mark
SUPERSEDED, do not implement.

---

## 3. GUI architecture (MVC per CLAUDE.md §5.1)

The post-run results layer becomes the live one. There is exactly one model
of "the run's results" at any time, and every existing view already listens
to it.

- **Model:** `SWMMResultsLayer` opened via `openResultsLive(path)` (§4.1).
  Owns the handle, `m_totalSteps`, the id maps. `refreshLive()` grows
  `m_totalSteps` and emits the existing `totalTimeStepsChanged(int)` plus a
  new `periodsAppended(int firstNew, int count)`.
- **Producer / clock:** the engine writers (§2.1). **Notifier:**
  `SimulationRunner` — refactored branch at the existing `kTickIntervalMs`
  gate (`simulationrunner.cpp:483-485`); legacy branch on each parsed
  `progress` line (`:687-790`). One new signal `resultsFileGrew(int jobId)`.
  No per-period signal: cadence is `progressTickMs` (default 1000 ms, pref
  already exists at `preferencesmanager.cpp:893`).
- **Adapter:** `SwmmOutRunLayer` — unchanged; `periodCount()` already re-polls
  `m_layer->totalTimeSteps()` (`swmmoutrunlayer.cpp:52-57`).
- **Views:** `ComparisonPlotDialog`, `ProfilePlotDialog` gain incremental
  append (v1 Phases 4-5, kept); `AnimationController` already grows its range
  on `totalTimeStepsChanged` (`animationcontroller.cpp:51`); the canvas
  animation therefore becomes live for 1D with no further work.

```
engine IO thread / legacy worker      GUI thread
  write period, fflush  ──(.out)──►  SimulationRunner tick ─► resultsFileGrew(jobId)
                                        └─► SWMMResultsLayer::refreshLive()
                                              ├─ totalTimeStepsChanged ─► AnimationController, map
                                              └─ periodsAppended(first,n) ─► ComparisonPlotDialog::appendChartTails
                                                                             ProfilePlotDialog::appendPeriods
```

**Index space (replaces v1 §3):** the file is complete from period 0, so the
absolute period index *is* a valid key during the run. The time-based
cross-source sync (`periodIndexForDateTime`, `SeriesData::timesJulian`) stays
as is; nothing needs the v1 "window-relative" rule.

---

## 4. GUI changes

### 4.1 `SWMMResultsLayer` (`src/layers/swmmresultslayer.cpp`)

- `bool openResultsLive(const QString& path)`: `swmm_output_open_live`, then
  the existing `finishOpen()` (`:773-866`) with one change — allow
  `m_totalSteps == 0` in live mode (today it bails at `:790`); in that case
  `m_endDateTime` = reported start, and the layer reports 0 periods until the
  first refresh. `buildOutputIdMaps()` (`:823`) runs once, at open, as today.
- `int refreshLive()`: `swmm_output_refresh`; if grown: update `m_totalSteps`,
  `m_endDateTime` (last period time, `:817` logic), prefetch nothing, emit
  `totalTimeStepsChanged(m_totalSteps)` and `periodsAppended(old, new-old)`.
  When the reader reports the footer arrived: set `m_live = false`, emit
  `resultsFinalized()`.
- `bool isLive() const`.
- `closeResults()` (`:926`) already zeroes state; unchanged.

### 4.2 `SimulationRunner`

- Refactored branch: inside the throttle block (`:483-485`), after the
  progress emit, `emit resultsFileGrew(jobId)`. The writer is on the engine's
  IO thread, so the file lags the step count by the queue depth (≤ 8 periods);
  the reader's `floor` absorbs that.
- Legacy branch: after each `progress` line is parsed, `emit resultsFileGrew(jobId)`.
- `outPath()` already exists (`include/simulation/simulationrunner.h:61`).
- Guard: only emit while a pref-controlled flag is on (§4.6), snapshotted on
  the GUI thread before the worker starts, like `tickIntervalMs` (`:236`).

### 4.3 `SWMMVis` (`src/swmmvis.cpp`)

- **Open early.** On the first `resultsFileGrew` for a job (or first
  progress tick), do what the finish handler does today at `:7908-7945` —
  find/create the project's results layer, `closeResults()` if it is bound
  to the same path (`:7921` handshake, now mandatory *before* the engine
  truncates the file: move it to run start), `openResultsLive(outPath)`,
  `setActiveResultsLayer`, `setPrimaryLayer`. Live-open may fail if the
  header is not yet on disk (engine still in `prepare`); retry on the next
  tick, no error.
- **Finish handler** (`:7842`): if the layer is live, `refreshLive()` (the
  footer is there now) instead of `closeResults()`+`openResults()`. The
  `hasResults` rule for cancelled runs (`:7908`) becomes "the file has ≥ 1
  period". Layer identity is preserved, so open dialogs keep their run
  sources; nothing jumps.
- There are exactly two `finished` connects on the runner (1D `:7842`, 2D
  `:8162`); extend the 1D one. Do not add a third.
- Wire `resultsFileGrew` → `layer->refreshLive()` for the job's layer
  (mirror `mActive2DResultsLayers`, `swmmvis.h:789`, with a 1D map keyed by
  jobId).

### 4.4 `ComparisonPlotDialog` (kept from v1, re-cited)

`rebuildCharts()` (`:1306`) is a full teardown; `onRowsChanged()` (`:858`)
is a no-op although `ComparisonPlotModel::rebuildRows` is documented as the
live hook (`comparisonplotmodel.h:190-193`). Add `appendChartTails(int
firstNew, int count)`: keep every `QChart`/series/axis; per-series
`m_lastAppendedPeriod`; append only newer points via `get_*_series(start,
len)` tail reads; extend `QDateTimeAxis`/`QValueAxis` only outward, never
shrink mid-run. Route `periodsAppended` (via the run source → model
`rebuildRows` → `onRowsChanged`) to it. `deriveRows_()` sanitises y to
`[0,1]` (`comparisonplotmodel.cpp:254-260`) — do not autoscale through the
model. Bonus: the same path can be connected to
`SWMM2DResultsLayer::timeRangeChanged` to un-stale 2D series (v1 §9 non-goal,
now cheap).

### 4.5 `ProfilePlotDialog` / `ProfileSourceFetcher` / `ProfileBuilder` (kept from v1, re-cited)

- `ProfileSourceFetcher::fetch(...)` (`profilesourcefetcher.cpp:57`) reads
  `[0, periodCount-1]` (`:68`, range reads `:35`, `:49`); add a `startPeriod`
  parameter for tail reads.
- `ProfileBuilder::appendPeriods(SourceDerived&, const SourceSeries& tail)`:
  `SourceDerived` is period-major (`profilebuilder.h:271-280`), so a period is
  one `push_back` per array plus an O(pathLength) running min/max update.
  Acceptance: byte-identical to a full `compute()` over the same periods.
- On `periodsAppended`: append, then `m_plot->setSeries` is *not* needed —
  `SeriesBinding` holds a `shared_ptr<SourceDerived>`; mutate in place and
  `update()`. Do **not** go through `invalidateSourceCacheFor()` (`:1344`,
  drops the cache and triggers a full async `rebindSources()` `:1137`);
  extend `ensureCacheInvalidationWired` (`:1355`) with the append path.
- `onAnimationTimeChanged` (`:1637-1642`) needs no change.

### 4.6 Preferences / toggle

- `liveResults1DEnabled` (default true) next to `progressTickMs`
  (`preferencesmanager.cpp:893-903`). Cadence reuses `progressTickMs`; no
  second interval.
- Toolbar: a "Live 1D" checkbox beside the existing 2D "Live render"
  checkbox (`swmmvis.cpp:1495-1509`). Note the 2D one is per-layer UI state
  (`liveRenderEnabled()`), not a pref; the 1D toggle should write the pref.
- Off ⇒ no early open, no refresh; behaviour is exactly today's (open at
  finish).

### 4.7 Retired from v1

`LiveResultsBuffer`, `SubscriptionSet`, `LiveResultsPlugin`,
`LiveEngineRunLayer` (+ `_codes.cpp`), `liveResults1DMaxPeriods`, the
window-relative index rule, the live→`.out` swap and "(live)" qualifier.

---

## 5. Phased checklist

```
Phase E1 (engine): fflush per period in DefaultOutputPlugin + legacy output.c
   → verify: on-disk size == output_start_pos + n*bytes_per_period after each
     update(); benchmark delta ≤ noise (tests/artifacts/live_1d/flush_timing.csv).

Phase E2 (engine): OutputReader::openLive/refresh + swmm_output_open_live /
   swmm_output_refresh; install header.
   → verify: test_output_reader_live (0 / k / truncated-mid-record / finalised
     copies; refresh tracks growth; live vs final series bit-identical;
     legacy CLI .out opens live and finalises).

Phase G1 (GUI): SWMMResultsLayer::openResultsLive / refreshLive / periodsAppended.
   → verify: tests/gui/test_swmmresultslayer_live — a growing fixture (append
     periods between calls, then footer) drives totalTimeStepsChanged and
     periodsAppended with correct (first, count); finalisation flips isLive().

Phase G2 (GUI): SimulationRunner::resultsFileGrew (both branches) + SWMMVis early
   open / refresh / finish-handler change / re-run handshake at start.
   → verify: run a small model on BOTH engines with the Results toolbar
     visible — the animation range grows during the run; cancel mid-run keeps
     the partial results; re-running the same model does not read a stale
     handle (close-before-truncate).

Phase G3 (GUI): ComparisonPlotDialog::appendChartTails.
   → verify: tests/gui/test_comparisonplot_live — N appends add N points with
     zero chart reconstruction; axes only grow; append cost flat in total
     periods.

Phase G4 (GUI): ProfileSourceFetcher startPeriod + ProfileBuilder::appendPeriods.
   → verify: tests/unit/test_profilebuilder_append — appendPeriods() ≡ full
     compute() byte-for-byte (the correctness crux, unchanged from v1).

Phase G5: END-TO-END PARITY (acceptance).
   → Both engines, both dialogs open from before the run starts and one opened
     mid-run: dump live series to tests/artifacts/live_1d/<engine>/live_series.csv
     as they arrive and the post-run series to out_series.csv; assert equality
     (not epsilon: same file). Repeat with [REPORT] AVERAGES YES and a non-zero
     REPORT_START.

Phase G6: preference + toolbar toggle + docs/manual + CHANGELOG (both repos).
```

---

## 6. Risks

| Risk | Mitigation |
|---|---|
| Partially written last record | Writers flush per period (E1); reader floors (E2); reader never trusts the last record's bytes until the size covers it. |
| Refactored writer lags the step counter (IO queue depth 8, blocking `post()` — still true, `IOThread.hpp:83`) | Live count comes from the *file*, never from the step count; lag is invisible except as latency. |
| Engine truncates a `.out` the GUI still has open (re-run of the same model) | Move the close-before-run handshake to run start (§4.3); the 2D path already does this ("dual-stream re-run handshake", `SWMM2DResultsLayer::closeSource`). |
| Header not yet on disk at first tick | `openResultsLive` returns false; retry next tick; no dialog. |
| Antivirus / network share holds the file | Same exposure the post-run open has today; refresh failures are silent and retried. |
| `appendChartTails` too slow at short report steps | Cadence is `progressTickMs`, so many periods coalesce per tick already; tail reads are bulk `get_*_series` calls. |
| Legacy worker on Windows: file written by another process | Plain `fopen` on both sides (`_SH_DENYNO`); verified no exclusive sharing anywhere. |

---

## 7. Non-goals

- Sub-report-step ("every solver step") values — the 2D live path remains the
  only wall-clock-throttled bulk-getter stream.
- Any host-registered engine plugin API (retired with A).
- Live scatter, statistics dashboard, tabular results dialogs (they can adopt
  `periodsAppended` later; not in scope).
- Python / CLI consumers of `swmm_output_open_live` (it is public, but only
  the GUI is a customer here).

---

## 8. Open questions for the reviewer

1. Approve replacing mechanism A with B (and retiring the engine host-plugin plan)?
2. `fflush` per period unconditionally, or behind an engine option
   (`[OPTIONS] LIVE_OUTPUT_FLUSH YES` / worker flag) defaulting to on?
3. Should the 1D "Live" toggle be a persisted preference (proposed) or
   per-layer UI state like the 2D checkbox?
4. Is live **map** animation of 1D results during a run desired, or should the
   early open feed only the plot dialogs? (Free either way; affects whether
   `setPrimaryLayer` happens at first tick or at finish.)
