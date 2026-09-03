# Live 1D Results (Profile + Comparison Plots) — GUI Plan (2026-08-04)

**Status:** SUPERSEDED (pending review) by `LIVE_1D_RESULTS_PLAN_V2_2026-09-03.md`
— audited 2026-09-03: nothing here was implemented, the file:line citations are
stale, and the legacy-engine exclusion was found to be architectural (separate
worker process), so v2 switches to tailing the `.out` for both engines. Kept
for the record; do not implement from this document.

~~**Status:** PLANNED. **Blocked on** `openswmm.engine/plans/LIVE_RESULTS_HOST_PLUGIN_PLAN_2026-08-04.md` Phase 1 (`swmm_engine_add_output_plugin`).~~

**Decisions (user-approved 2026-08-04):**
1. **Sampling:** report-step aligned, via a host-registered `IOutputPlugin` — *not* polling bulk getters. Guarantees the live curve equals the post-run `.out` curve.
2. **Buffering:** subscription-based — only elements currently referenced by an open plot are retained.
3. **Memory:** bounded ring of retained periods; on finish, plots re-source from the completed `.out` for full history.
4. **Scope:** profile plot **and** comparison plot, all 1D attributes.
5. **Legacy engine (SWMM 5.x) is out of scope** — the feature is gated off when `engineVersion.startsWith("5.")`.

## 1. User-facing behavior

- During a run with the OpenSWMM 6.x engine, an open profile plot or comparison plot updates as report periods arrive, at the report interval.
- Opening a plot mid-run yields a series starting at that moment (subscription begins on open), further limited by the ring capacity. This is a deliberate trade for scale — see §6.
- On completion, plots silently re-source from the `.out` and gain full history back to report-start. The user sees the curve extend backwards, once.
- On a cancelled run, the `.out` is still adopted if it has data (mirrors the existing `hasResults` rule at `swmmvis.cpp:7219`).
- Live 1D is off for legacy runs and when `save_results` is 0. A master toggle mirrors the existing `mCheckBoxLive2D` (`swmmvis.cpp:1249`).

## 2. Architecture (MVC per CLAUDE.md §5.1)

The same result data is consumed by two independent dialogs and must stay synchronized, so the buffer is the single model and both plots are views over it.

- **Model:** `LiveResultsBuffer` (QObject, GUI thread) owns the bounded ring and the subscription set. Sole source of truth for live 1D.
- **Producer:** `LiveResultsPlugin : IOutputPlugin` runs on the engine IO thread, filters the snapshot down to the subscription, and queue-posts value copies to the buffer.
- **Adapter:** `LiveEngineRunLayer : IRunLayer` presents the buffer through the existing plot interface. `ProfileSourceFetcher` gains a live overload for the profile stack.
- **Views:** `ComparisonPlotDialog`, `ProfilePlotDialog` — unchanged in structure, gaining an incremental append path.

```
engine IO thread          |  GUI thread
LiveResultsPlugin::update |  LiveResultsBuffer (ring + subscription)
  filter to subscription  |    |  periodsAppended(first, count)
  copy scalars            |    +--> LiveEngineRunLayer --> ComparisonPlotDialog
  invokeMethod(Queued) ---+    +--> live SourceSeries  --> ProfilePlotDialog
```

## 3. Critical constraint: index space

The ring retains a **window**, so absolute period index is not a valid key into live data. Rule for the whole feature:

> **Absolute time (`timesJulian` / `QDateTime`) is the only cross-source key. All array indices are window-relative.**

This is already how the plot stack works — `ProfilePlotDialog::onAnimationTimeChanged` (`profileplotdialog.cpp:1248-1256`) converts a `QDateTime` to a period via `periodIndexForDateTime(dt)`, and `SeriesData` carries `timesJulian` explicitly (`irunlayer.h:91-96`). `ProfileBuilder::SourceSeries` also explicitly permits short series: *"Series with fewer periods than the longest source are accepted"* (`profilebuilder.h:180-182`). So the window model fits the existing contracts; it just must not be violated by new code.

`LiveEngineRunLayer::periodCount()` therefore returns the **retained** count, and a new `firstRetainedTimeJulian()` exposes the window start.

## 4. New files

### `include/plot/liveresultsbuffer.h` + `src/plot/liveresultsbuffer.cpp`

```cpp
namespace openswmmvis::plot {

/*! One subscribed (object, attribute) channel. */
struct LiveChannelKey {
    ObjectRef::Kind kind;
    QString         name;
    PlotAttribute   attr;
    bool operator==(const LiveChannelKey&) const noexcept;
};

/*! Immutable, shared with the IO thread by atomic pointer swap. */
struct SubscriptionSet {
    // Resolved to snapshot array indices once at prepare(), so update() is
    // an index gather with no string work on the IO thread.
    std::vector<int>           nodeIdx, linkIdx, subcatchIdx;
    std::vector<LiveChannelKey> channels;   // parallel to the gathered rows
};

class LiveResultsBuffer : public QObject {
    Q_OBJECT
public:
    explicit LiveResultsBuffer(int maxPeriods, QObject* parent = nullptr);

    int  subscribe(const QVector<LiveChannelKey>& keys);   // returns token
    void unsubscribe(int token);
    std::shared_ptr<const SubscriptionSet> currentSubscription() const;

    int    periodCount() const;              // retained, not absolute
    double firstRetainedTimeJulian() const;
    void   seriesFor(const LiveChannelKey&, SeriesData& out) const;

    /*! Called via QueuedConnection from LiveResultsPlugin::update(). */
    void appendPeriod(double simTimeJulian, std::vector<double> gathered);

signals:
    void periodsAppended(int firstNewIndex, int count);
    void subscriptionChanged();
};
} // namespace
```

Ring is a per-channel `std::deque<double>` (or flat vector + head index) capped at `maxPeriods`, plus one shared `std::deque<double>` of times. Eviction is uniform across channels so all channels share one time axis.

Subscription is refcounted by key: two dialogs plotting the same node share one channel; the channel drops when the last token releases it. A subscription change mid-run rebuilds the resolved index vectors and atomically swaps the `shared_ptr` — the IO thread reads it via `currentSubscription()` and never blocks on the GUI thread.

### `include/plot/liveresultsplugin.h` + `src/plot/liveresultsplugin.cpp`

```cpp
class LiveResultsPlugin final : public openswmm::IOutputPlugin {
public:
    explicit LiveResultsPlugin(QPointer<LiveResultsBuffer> buffer);
    int initialize(const std::vector<std::string>& args, void* ctx) override;
    int validate(const openswmm::SimulationContext& ctx) override;
    int prepare (const openswmm::SimulationContext& ctx) override;  // cache id tables
    int update  (const openswmm::SimulationSnapshot& snap) override; // IO THREAD
    int finalize() override;
};
```

`update()` contract, per the engine plan §3 — this is the whole hot path and must stay boring:

1. Load `shared_ptr<const SubscriptionSet>` (one atomic read).
2. Gather subscribed scalars into a `std::vector<double>`. Index-only; no string lookups, no allocation beyond the one gather vector.
3. `QMetaObject::invokeMethod(buffer, ..., Qt::QueuedConnection)` with the vector moved in.
4. Return. **Never** `BlockingQueuedConnection` — the IO queue is depth 8 and `post()` blocks the solver (`IOThread.cpp:45-59`).
5. **Never** retain snapshot pointers (`SimulationSnapshot.hpp:13-15`). Copy the id tables once in `prepare()`; the `node_ids`/`link_ids` pointers are borrowed, not deep-copied (`SWMMEngine.cpp:3849-3854`).

`QPointer` guards the case where the buffer outlives... actually the reverse: the runner self-deletes after `finished`, so the plugin must tolerate a destroyed buffer. `QPointer` + a null check covers it, but note the plugin is deleted by `PluginFactory::unload_all()`, not by us.

### `include/plot/liveenginerunlayer.h` + `src/plot/liveenginerunlayer.cpp`

`LiveEngineRunLayer : IRunLayer`, backed by `LiveResultsBuffer`. Follows `Mesh2DRunLayer` (`mesh2drunlayer.cpp:49-54`) in re-polling the source on every call rather than caching — correct for a growing source. `getSeriesAt` maps `ObjectRef + PlotAttribute` → `LiveChannelKey` → `buffer->seriesFor()`.

Put the `PlotAttribute` → channel mapping in a separate `liveenginerunlayer_codes.cpp`, following the precedent at `swmmoutrunlayer.cpp:67-69` (*"lives in swmmoutrunlayer_codes.cpp so it can be linked into unit tests without dragging in SWMMResultsLayer"*).

## 5. Changed files

### `src/simulation/simulationrunner.cpp` / `include/simulation/simulationrunner.h`

- In the **refactored branch only** (`if (!useLegacy)`, line 270): after `swmm_engine_open` (275) and before `swmm_engine_start` (370), construct `new LiveResultsPlugin(buffer)` and register it. Ordering is mandatory — see engine plan §3.
- Confirm `swmm_engine_start` is called with `save_results != 0`; otherwise no snapshots are produced at all.
- Guard on the engine actually exporting the symbol, following the optional-API precedent at `simulationrunner.cpp:515,539` — an older engine simply never produces live data.
- New accessor `LiveResultsBuffer* liveBuffer() const`, and a `liveResultsAvailable(int jobId)` signal so `swmmvis.cpp` can wire dialogs.
- **No new per-tick signal and no change to the `kTickIntervalMs` gate.** Live 1D bypasses the wall-clock throttle entirely; it is paced by the engine's report step. This is the deliberate difference from the 2D path.

### `src/ui/dialogs/comparisonplotdialog.cpp` — incremental append (the main perf work)

`rebuildCharts()` (`:1233`) is a full teardown: `deleteLater()` on every row frame, new `QChart`/axes/`QLineSeries` per row, and for **every** series a full-range `resolveSeries()` engine read plus a per-point `swmmDateTimeToQDateTime()` + `append()` loop. That is O(rows × series × periods) with widget churn. Calling it per report step is not viable.

Add `appendChartTails(int firstNew, int count)`:
- Keep every `QChart`, `QLineSeries` and axis alive.
- Track per-series `m_lastAppendedTimeJulian`; append only points newer than it.
- Extend the `QDateTimeAxis` range and grow the `QValueAxis` only when a new point falls outside — never shrink during a live run (rescaling on every tick makes the plot unreadable).
- Wire `LiveResultsBuffer::periodsAppended` → `appendChartTails`.

`ComparisonPlotDialog::onRowsChanged()` is currently an empty no-op (`:785`) even though `ComparisonPlotModel::rebuildRows()` is documented as the live-refresh hook (`comparisonplotmodel.h:172-175`). Wire it to `appendChartTails` rather than `rebuildCharts`. Leave `onAnimationTimeChanged` (`:826`) alone — it already moves only the cursor line.

Note `deriveRows_()` unconditionally sanitizes `ymin/ymax` to `[0,1]` (`comparisonplotmodel.cpp:238-253`); real y-ranges live in the dialog. Do not route live autoscale through the model.

### `include/plot/profilesourcefetcher.h` / `src/plot/profilesourcefetcher.cpp`

- Add a live overload: `fetch(LiveResultsBuffer*, const PathStatic&, const QString& sourceId)`.
- Add a `startPeriod` parameter to the `.out` overload for tail reads; it currently always reads `[0, periodCount-1]` (`profilesourcefetcher.cpp:57-89`).
- Keep both in this file — it is deliberately the one home for the engine call sequence.

### `src/ui/dialogs/profileplotdialog.cpp`

- Subscribe the current path's nodes and links on `rebindSources()`; release on path change and on close.
- On `periodsAppended`: append to the cached `SourceSeries` and recompute. `SourceDerived` is period-major (`hglByPeriod[period][pathNodeIdx]`, `profilebuilder.h:215-230`), so appending a period is a `push_back` of one inner vector — but `ProfileBuilder::compute()` returns a whole new `SourceDerived` with no incremental entry point. **Add `ProfileBuilder::appendPeriod()`** rather than recomputing the envelope from scratch each tick; the running min/max envelope updates in O(pathLength).
- Do **not** route live growth through `invalidateSourceCacheFor()` (`:1105`) — that drops the cache and triggers a full async `rebindSources()`. Extend `ensureCacheInvalidationWired` (`:1113`) with the append path instead.
- `setCurrentPeriod()` (`profileplotwidget.h:145`) is repaint-only and needs no change.

### `src/swmmvis.cpp`

- On `liveResultsAvailable`: create the `LiveEngineRunLayer`, register it by jobId in a `QHash<int, ...>` mirroring `mActive2DResultsLayers` (`swmmvis.h:652`), and offer it to open dialogs.
- **The swap point is `swmmvis.cpp:7259-7260`**, immediately after `rl->openResults()` succeeds and alongside `setActiveResultsLayer` / `setPrimaryLayer`. Retire the live layer, re-source both dialogs onto the `.out`-backed `SwmmOutRunLayer`, drop the "(live)" qualifier. This mirrors the existing 2D source-swap at `:7452-7469`.
- Caution: there are already **two** `finished` connects on the same runner (`:7157` 1D, `:7452` 2D). Add to the 1D one; do not introduce a third with ordering assumptions.

### `src/core/preferencesmanager.cpp` + `preferencesdialog.cpp`

- `liveResults1DEnabled` (default true) and `liveResults1DMaxPeriods` (default 3600), following `progressTickMs` (`preferencesmanager.cpp:89`).
- Snapshot the values on the GUI thread before the worker starts, exactly as `tickIntervalMs` is handled today.

## 6. Memory

Retained bytes ≈ `channels × maxPeriods × 8`. A profile over 200 elements with 3 attributes at 3600 periods is ~17 MB. Full-model buffering was rejected for this reason; subscription keeps the ceiling proportional to what is actually on screen.

**Honest caveat:** subscription bounds *GUI* memory only. The engine still builds and deep-copies a full `SimulationSnapshot` every report step regardless (`SWMMEngine.cpp:3856`) — that cost exists today for the `.out` writer and is unchanged by this feature.

## 7. Phased checklist

```
Phase 0: Engine Phase 1 landed (swmm_engine_add_output_plugin)
         → verify: GUI TU links against the INSTALLED engine on Windows + macOS.

Phase 1: LiveResultsBuffer + SubscriptionSet
         → verify: tests/unit/test_liveresultsbuffer — ring eviction keeps one
           shared time axis; refcounted subscribe/unsubscribe; window-relative
           indices after eviction; seriesFor() on an unsubscribed key returns
           ok=false rather than empty-but-ok.

Phase 2: LiveResultsPlugin
         → verify: tests/unit/test_liveresultsplugin drives synthetic
           SimulationSnapshots on a non-GUI thread and asserts the buffer
           receives them in order with correct values. Assert update() performs
           no string lookups (resolved indices only) and does not block.

Phase 3: LiveEngineRunLayer
         → verify: tests/gui/test_liveenginerunlayer, extending the existing
           stub pattern (tests/gui/test_comparisonplot_pairs.cpp:31 StubRunLayer).
           A GrowingRunLayer whose periodCount() increments between calls slots
           in with no new infrastructure.

Phase 4: ComparisonPlotDialog incremental append
         → verify: tests/gui/test_comparisonplot_live — N appends produce N new
           points with zero QChart reconstruction (count chart pointers before
           and after); axis extends and never shrinks; cursor still tracks.
           Timing check: append cost is flat in total period count, not linear.

Phase 5: Profile live path (ProfileBuilder::appendPeriod + fetcher overload)
         → verify: tests/unit/test_profilebuilder_append — appendPeriod() over
           a synthetic path yields SourceDerived byte-identical to a full
           compute() over the same periods. This is the correctness crux.

Phase 6: SimulationRunner + swmmvis wiring, live -> .out swap
         → verify: END-TO-END PARITY, the acceptance test for the whole feature.
           Run a small model with both dialogs open; dump the live series to
           tests/artifacts/live_1d/live_series.csv (reviewable location, per
           CLAUDE.md §4.1); after finish, dump the .out-backed series to
           .../out_series.csv; assert agreement within float32 epsilon.
           Then repeat with [REPORT] AVERAGES YES and a non-zero REPORT_START.

Phase 7: Preferences + legacy gating + docs
         → verify: legacy (5.x) run produces no live layer and no crash; toggle
           off suppresses subscription entirely; docs/manual updated.
```

## 8. Risks

| Risk | Mitigation |
|---|---|
| Slow `update()` stalls the solver (IO queue depth 8, blocking `post()`) | `update()` is gather + queued post only. Phase 2 asserts no blocking; Phase 6 compares wall-clock runtime with the feature on vs off. |
| `appendChartTails` still too slow at short report steps | Phase 4 measures. Fallback: coalesce appends behind a GUI-side timer — the buffer keeps every period regardless, so this is a view-only change. |
| Ring eviction desynchronizes the two dialogs | One shared time axis, uniform eviction (§4), and the §3 index rule. Phase 1 tests eviction directly. |
| Live/`.out` divergence | Structurally prevented — same snapshot object feeds both. Phase 6 verifies rather than assumes. |
| `.out` swap causes a visible jump | Expected and specified (§1): history extends backwards once. |

## 9. Non-goals

- Live 2D comparison-plot series. `Mesh2DRunLayer` already re-polls correctly (`mesh2drunlayer.cpp:49-54`) and `SWMM2DResultsLayer::timeRangeChanged` already exists, but `ComparisonPlotDialog` never connects to it — so 2D is stale-until-reopened today. The Phase 4 append path makes fixing this nearly free; do it as a follow-up, not here.
- Legacy engine (out of scope by decision).
- Live scatter, statistics dashboard, tabular results dialogs.
- Persisting live buffers across runs, or replaying them.
- Backfilling history for a plot opened mid-run (would require full-model buffering — the rejected option).
