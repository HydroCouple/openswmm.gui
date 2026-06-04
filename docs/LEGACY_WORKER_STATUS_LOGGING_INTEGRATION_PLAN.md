# Legacy Worker — Status, Performance & Logging Integration Plan

**Status:** Planned (not yet implemented)
**Date:** 2026-05-31
**Owner:** TBD
**Scope:** Make the SWMM 5.x (legacy) out-of-process run path convey the same
status, performance, and failure-logging signal to the Simulation Status panel
and message log that the 6.0.0 in-process path already provides — and remove the
places where that signal is currently *tenuous* (derived, generic, or missing).

> This plan **supersedes nothing** but **extends**
> [`LEGACY_WORKER_WARNING_FORWARDING_PLAN.md`](./LEGACY_WORKER_WARNING_FORWARDING_PLAN.md),
> which already specifies the warning-forwarding piece (Gap 1 below). That plan's
> Option A remains the recommended approach and is incorporated here by reference;
> this document adds the surrounding status/perf/failure-logging gaps so the whole
> worker→UI channel can be closed in one coordinated pass.

---

## 1. Why the legacy engine runs out-of-process (current configuration)

The legacy SWMM engine (`openswmm.engine/src/legacy/engine`) is built on
file-scope/global state and is **not thread-safe**: two simulations in the same
process would corrupt each other's globals. The refactored 6.0.0 engine takes an
opaque `SWMM_Engine` handle and is re-entrant, so it runs in-process on a
`QtConcurrent` worker thread. To get parallelism (and isolation) for 5.x without
the thread-safety risk, each legacy run is launched as its **own child process**,
so each has a private copy of the engine globals.

### Components

| Concern | Location |
| --- | --- |
| Worker entrypoint / lifecycle | `openswmm.engine/src/legacy/worker/main.cpp` |
| Worker→parent wire format (JSON helpers) | `openswmm.engine/src/legacy/worker/worker_progress.h` |
| Worker CMake target | `openswmm.engine/src/legacy/worker/CMakeLists.txt` — target `openswmm_legacy_worker`, output binary `openswmm-legacy-worker` |
| Worker discovery (GUI) | `openswmm.gui/src/simulation/simulationrunner.cpp` — `findLegacyWorker()` (~line 63) |
| Process launch + drain (GUI) | `simulationrunner.cpp` — legacy branch of `SimulationRunner::start()` (~lines 516–671) |
| Version gate | `simulationrunner.cpp:232` — `const bool useLegacy = engineVersion.startsWith("5.");` |
| Status model (UI) | `openswmm.gui/include/simulation/simulationstatusmodel.h`, `src/simulation/simulationstatusmodel.cpp` |
| Status dock + delegates + wiring | `openswmm.gui/src/swmmvis.cpp` (`initializeSimulationStatusDockWidget`, `onRunSimulation`, `warningReceived` → log) |

### Wire protocol (today)

The worker speaks **newline-delimited JSON on stdout**, plus a raw line on stderr
for errors, plus the **process exit code**. The GUI never blocks on
`waitForFinished` + `readAll`; it drains stdout incrementally
(`waitForReadyRead(200)` → split on `\n` → parse), which is what fixed the
~64 KB pipe-buffer deadlock noted in the prior plan. Cancel is implemented by
`worker.kill()`.

Message types emitted by the worker (`worker_progress.h`):

| `type` | Fields | Emitted | Consumed by GUI → |
| --- | --- | --- | --- |
| `dates` | `start`, `end` (OADate) | once, after `swmm_start` | `simulationDatesKnown` → Start/End columns |
| `progress` | `stepCount`, `elapsed` (elapsed **days**) | every 10 steps | `progressChanged` → Progress %, Current date, Avg timestep |
| `warning` | `message` (and GUI reads `code`) | **never produced today** | `warningReceived` → log + status child rows |
| `error` | `code`, `message` | on `swmm_open/start/step` failure | stored as `lastErrorMsg`, surfaced at `finished` |
| `continuity` | `runoff`, `routing` (fractions) | once, after `swmm_end` | `finished` → Runoff/Routing-err columns |

### What the Simulation Status panel expects

`SimulationStatusModel` renders, per job: Name, Status, Progress (bar),
Sim Start / Current / End dates, Runoff-err %, Routing-err %, wall-clock
Duration, Avg timestep, Engine version, and **warning child rows**. Duration,
Name, Status colour, and Version are filled GUI-side; everything else must arrive
from the run path.

---

## 2. In-process (6.0.0) vs legacy worker — signal parity

This is the crux of "is the signal tenuous?" The in-process path polls the engine
each tick and emits **measured** values; the legacy path emits a thinner stream
and the GUI **derives** the rest.

| Panel item | 6.0.0 in-process | Legacy worker | Assessment |
| --- | --- | --- | --- |
| Start / End dates | measured (`swmm_get_start/end_time`) | measured (`swmm_getValue(STARTDATE/ENDDATE)`) | **OK / parity** |
| Progress fraction | `current_time / span` (measured) | `elapsedDays / span` (measured days) | OK |
| Current sim date | measured current time | derived `simStart + elapsedDays` | OK (equivalent) |
| Avg timestep | `Σelapsed / steps` (measured) | `elapsedDays·86400 / steps` (derived) | OK |
| Initial 0 % tick | **yes** — row populates immediately | **no** — first update only at step 10 | **Gap 4** |
| Live continuity error | measured each tick | running value via `swmm_getRunningMassBalErr` on each progress line | **Done** (Gap 3, 2026-05-31) |
| Final continuity error | measured | measured (`continuity` line) | OK |
| Update cadence | wall-clock rate-limited (`progressTickMs` user pref) | every 10 **steps**, ignores the pref | **Gap 2** |
| Engine warnings (live) | `swmm_set_warning_callback` → child rows + log | **never forwarded** (only in `.rpt`) | **Gap 1** (see warning plan) |
| Failure message | real text via `swmm_error_message(rc)` | generic `"swmm_step failed"` + code | **Gap 5** |
| stderr / engine console on failure | n/a (in-process) | read **only** if no `error` JSON was seen; otherwise discarded | **Gap 6** |

### Things that are already fine — do not touch

Dates, progress fraction, current date, avg timestep, final continuity, cancel,
and the deadlock-safe drain all work and reach the panel correctly. Per the
project's surgical-changes guideline, this plan deliberately leaves them alone.

---

## 3. Gaps (ranked) and the integration for each

### Gap 1 — Engine warnings never reach the UI on legacy runs ✅ DONE (2026-05-31)

Implemented per Option A of
[`LEGACY_WORKER_WARNING_FORWARDING_PLAN.md`](./LEGACY_WORKER_WARNING_FORWARDING_PLAN.md):

- **Engine:** `swmm_setWarningCallback(cb, userData)` + `swmm_LegacyWarningCallback`
  typedef (`openswmm_solver.h`, `swmm5.c`); process-global storage in `globals.h`
  (`WarningCallback` / `WarningCallbackData`). `report_writeWarningMsg()` now also
  invokes the callback, and a shared `report_invokeWarningCallback()` helper
  (`report.c`, `funcs.h`) is called from the direct-write sites that bypass it:
  `input.c:118/232` (unknown section) and `project.c:478` (unknown option). The
  `lidproc.c` WARN09 site was found to be **dead/commented-out code**, so it was
  left untouched.
- **Worker:** registers the callback **before** `swmm_open` (so input-parsing
  warnings are captured), forwarding each to `WorkerProgress::emitWarning`.
- **GUI:** unchanged — the drain loop already parses `warning` lines →
  `warningReceived` → log + Simulation Status child rows.

**Warning `code`:** decided to forward message-only (`code` 0). Legacy warnings
are inconsistent (some carry WARNxx, many are free text); the status model
already renders message-only when `code == 0`, and the GUI's existing
`obj.value("code")` read defaults to 0 when the field is absent, so no schema
change was needed.

### Gap 2 — Progress cadence ignores the user's tick preference and wall-clock

In-process honours `PreferencesManager::progressTickMs()` (default ~1 Hz). The
worker instead emits "every 10 steps" (`main.cpp:80`). On a fast small model 10
steps can be sub-millisecond (flooding the pipe/event queue); on a slow large
model 10 steps can be many seconds (sluggish, looks hung). The cadence is
decoupled from wall-clock and from the user setting.

**Integration:**
1. Pass the snapshotted `tickIntervalMs` to the worker as a 4th CLI argument
   (keep it optional/defaulted so the contract stays backward compatible).
2. In the worker step loop, gate emission on a monotonic wall-clock timer
   (`std::chrono::steady_clock`) instead of `stepCount % 10`, mirroring the
   in-process `kTickIntervalMs` gate. Still emit the **first** step immediately
   (see Gap 4) and always emit the final state.

> Keep this minimal: a single `steady_clock` "last emit" timestamp in `main.cpp`.
> No new abstractions.

### Gap 3 — Live continuity error reads 0.0 during legacy runs ✅ DONE (2026-05-31)

**Resolved by an engine API expansion, not a UI workaround.** Investigation
showed the original premise (mass balance "only finalises at `swmm_end()`") was
wrong: `massbal_getRunoffError()` / `massbal_getFlowError()`
(`src/legacy/engine/massbal.c`) recompute the error from the live accumulators
(`RunoffTotals` / `FlowTotals`) plus a fresh storage query on every call, with no
dependency on `massbal_close()` (which only frees memory). The reason the worker
saw `0.0` was solely that `swmm_getMassBalErr` is gated to
`IsOpenFlag && !IsStartedFlag` (post-`swmm_end` only).

Implemented:

- **Engine:** new `swmm_getRunningMassBalErr(float *runoffErr, float *flowErr)`
  (`openswmm_solver.h`, `swmm5.c`) returns the running runoff/flow continuity
  errors while `IsStartedFlag` is true, via the existing getters. `swmm_getMassBalErr`
  is untouched. Calling mid-run is side-effect-safe — `massbal_report()`
  recomputes the final errors at `swmm_end()`.
- **Worker:** `emitProgress` now carries `runoff`/`routing` fractions; `main.cpp`
  calls `swmm_getRunningMassBalErr` each progress emit (percent → fraction).
- **GUI:** the legacy `progress` branch in `simulationrunner.cpp` reads
  `runoff`/`routing` and feeds them to `progressChanged` instead of `0.0, 0.0`,
  so the Runoff/Routing-err columns update live during a 5.x run.

No UI "pending" rendering was needed. The final `continuity` line (after
`swmm_end`) remains authoritative for the end-of-run value.

### Gap 4 — No initial 0 % tick on legacy runs

The in-process path emits a one-shot 0 % `progressChanged` right after dates so
the row paints before the first (possibly slow) step. The worker emits `dates`
then nothing until step 10, so the row looks stalled on large models during
`swmm_open`/`swmm_start`/first steps.

**Integration:** in `main.cpp`, immediately after `emitDates`, emit one
`progress` with `stepCount=0, elapsed=0.0`. The GUI already clamps `frac` to
`[0,1]` and tolerates `stepCount==0` (avg-timestep guard at
`simulationrunner.cpp:596`), so no GUI change is required.

### Gap 5 — Failure messages are generic, not the engine's real text

On failure the worker emits `"swmm_open failed"` / `"swmm_start failed"` /
`"swmm_step failed"` (`main.cpp:39,46,68`) — only an error **code**, not the
human-readable cause. The in-process path surfaces real text via
`swmm_error_message(rc)`. The legacy API already exposes the equivalent:

- `swmm_getError(char *errMsg, int msgLen)` — `openswmm_solver.h:742`
- `swmm_getErrorFromCode(int error_code, char *outErrMsg[1024])` — `:750`
- (internally `error_getMsg(int, char*)` — `src/legacy/engine/error.c:33`)

**Integration:** in each worker failure branch, look up the real message with
`swmm_getErrorFromCode(rc, buf)` (or `swmm_getError`) and pass it to
`emitError(rc, realMsg)`. The GUI already prefers `lastErrorMsg` at `finished`
(`simulationrunner.cpp:661`), so the descriptive text flows straight to the
status row tooltip and the message log with no GUI change.

### Gap 6 — stderr / engine console output is discarded once an `error` line exists

The GUI reads `worker.readAllStandardError()` **only** when `lastErrorMsg` is
empty (`simulationrunner.cpp:661–665`). If the worker emitted any `error` JSON,
the richer stderr stream (the worker's own `ERROR %d: %s` plus any legacy-engine
`fprintf`/console output) is dropped — so a crash *with* a partial diagnostic can
still lose the most useful context.

**Integration (GUI-side):**
1. Capture stderr continuously alongside stdout (or call
   `worker.setProcessChannelMode(QProcess::SeparateChannels)` and drain stderr in
   the same loop into a bounded buffer).
2. On failure, **append** captured stderr to the surfaced message instead of
   only using it as a fallback, and post it to the message log at Error severity
   so the `.rpt` is not the only place the detail survives.
3. Keep it bounded (e.g. last N KB) to avoid unbounded growth on a chatty engine.

---

## 4. Suggested sequencing

Ordered by value-to-risk; each step is independently shippable and verifiable.

1. **Gap 5** (real failure text) and **Gap 4** (initial tick) — worker-only,
   tiny, no protocol change. → verify: a deliberately broken `.inp` shows the
   real engine error in the log/tooltip; a large model paints a 0 % row instantly.
2. **Gap 6** (stderr capture) — GUI-only. → verify: kill the worker mid-run; the
   surfaced message includes stderr context.
3. **Gap 3** (running continuity) — ✅ **DONE 2026-05-31** via engine
   `swmm_getRunningMassBalErr` + worker emit + GUI parse. → verify: legacy run
   shows non-zero continuity while running, converging to the final `.rpt` value.
4. **Gap 2** (wall-clock cadence + 4th CLI arg) — worker + one GUI arg. → verify:
   fast small model no longer floods; slow large model updates ~1 Hz; honours the
   preference.
5. **Gap 1** (warning forwarding, Option A) — ✅ **DONE 2026-05-31**. → verify per
   that plan's checklist (e.g. `examples/demo_weir_culvert/weir_culvert.inp`, an
   unknown-section model, and a convergence-warning model) that warnings appear
   **live** in both the log and the status child rows, matching the `.rpt`.

---

## 5. Protocol contract after this work (target)

| `type` | Fields | When |
| --- | --- | --- |
| `dates` | `start`, `end` | once, after `swmm_start` |
| `progress` | `stepCount`, `elapsed` | first step (0), then wall-clock rate-limited, then final |
| `warning` | `code`, `message` | live, as the engine emits each warning |
| `error` | `code`, `message` (**real engine text**) | on any lifecycle failure |
| `continuity` | `runoff`, `routing` | once, after `swmm_end` |

Backward-compatible: same `type` set, only field enrichment (`code` on `warning`,
real text on `error`) and cadence change; the new 4th CLI arg is optional.

## 6. Out of scope / explicitly not doing

- No change to the in-process 6.0.0 path (already correct).
- No `.rpt` post-scrape (Option B in the warning plan) unless Gap 1's engine
  change must be deferred — then use the warning plan's Option C stopgap.
- No watchdog/timeout redesign; `findLegacyWorker`, discovery, and the
  deadlock-safe drain stay as-is.

## 7. Verification matrix

| Gap | Test artifact | Pass criterion |
| --- | --- | --- |
| 1 ✅ | unknown-section + convergence-warning models | warnings live in log **and** status child rows, count matches `.rpt` |
| 2 | one fast small model + one slow large model | updates paced to the pref, no flooding, no multi-second stalls |
| 3 ✅ | any legacy run | continuity shows live non-zero values while running, converging to the final `.rpt` % |
| 4 | large `.inp` (slow open) | status row paints at 0 % immediately |
| 5 | malformed `.inp` (e.g. bad option value) | status tooltip + log show the **real** engine message, not "swmm_step failed" |
| 6 | `kill -9` the worker mid-run | surfaced failure includes stderr context, logged at Error severity |
