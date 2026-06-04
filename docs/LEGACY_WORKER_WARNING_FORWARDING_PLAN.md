# Legacy Worker — Engine Warning Forwarding Plan

**Status:** ✅ Implemented 2026-05-31 (Option A). See
`LEGACY_WORKER_STATUS_LOGGING_INTEGRATION_PLAN.md` Gap 1 for the as-built notes.
One deviation from the audit list below: the `lidproc.c:328` WARN09 site is
dead/commented-out code and was left untouched. Warning `code` is forwarded as 0
(message-only) — no `emitWarning` schema change was needed.
**Date:** 2026-05-29
**Owner:** TBD

## Goal

Make the SWMM 5.3.0 (legacy) run path surface the engine's **internal**
warnings (e.g. `Unknown section '[X]'`, `Unknown option keyword …`,
`undefined object …`) live to the GUI's message log and Simulation Status
panel — the same way the 6.0.0 in-process path already does via its
registered warning callback.

## Background / Why this is missing today

- The 6.0.0 engine exposes `swmm_set_warning_callback(eng, cb, ud)`. The
  GUI's `SimulationRunner` registers `warningCallback`, which posts each
  warning back to the GUI thread → `warningReceived` →
  `SimulationStatusModel::addWarning` **and** the message log
  (`SWMMVis::onLogMessage`, wired 2026-05-29). So 6.0.0 warnings already
  appear in both places.
- The **legacy** engine (`openswmm.engine/src/legacy/engine`) has **no
  warning-callback API**. It writes warnings only to the `.rpt` file via
  `report_writeWarningMsg()` (`src/legacy/engine/report.c:1485`) and exposes
  just a count via `swmm_getWarnings()`
  (`include/openswmm/legacy/engine/openswmm_solver.h:756`).
- The legacy **worker** process (`openswmm.engine/src/legacy/worker/main.cpp`)
  only emits JSON for failures it detects on `swmm_open` / `swmm_start` /
  `swmm_step`. It does **not** forward the engine's internal warnings.
- Net effect: during a 5.3.0 run, only worker-detected hard failures reach
  the log/status. Finer engine warnings live only in the `.rpt`.

## Relevant files

- `openswmm.engine/src/legacy/worker/main.cpp` — worker entrypoint / lifecycle.
- `openswmm.engine/src/legacy/worker/worker_progress.h` — JSON emit helpers
  (`emitProgress`, `emitWarning`, `emitError`, `emitDates`).
- `openswmm.engine/src/legacy/engine/report.c` — `report_writeWarningMsg()`
  (line ~1485), `report_writeLine()` (line ~171).
- `openswmm.engine/include/openswmm/legacy/engine/openswmm_solver.h` —
  `swmm_getWarnings()` (line ~756); add any new callback setter here.
- `openswmm.gui/src/simulation/simulationrunner.cpp` — legacy drain loop
  (already parses JSON `dates` / `progress` / `warning` / `error`).
- `openswmm.gui/src/swmmvis.cpp` — `warningReceived` → log + status wiring
  (~line 4668).

## Options (pick one)

### Option A — Warning callback in the legacy engine (preferred, cleanest)

Mirror the 6.0.0 design so live warnings stream as they occur.

1. **Engine:** add a registerable callback, e.g.
   `typedef void (*swmm_LegacyWarningCallback)(const char *msg, void *ud);`
   and `void swmm_setWarningCallback(swmm_LegacyWarningCallback cb, void *ud);`
   in `openswmm_solver.h`. Store the function pointer + user-data in a
   file-scope/global in the engine.
2. In `report_writeWarningMsg()` (and any other warning emitters), after
   composing the message string, invoke the callback if registered — in
   addition to writing the `.rpt` line.
3. **Worker** (`main.cpp`): after `swmm_open`, register a callback that calls
   `WorkerProgress::emitWarning(msg)` (already emits
   `{"type":"warning","message":"…"}` to stdout, fflushed).
4. **GUI:** no change — the legacy drain loop already parses `warning` lines
   and emits `warningReceived`, which now goes to both the log and the
   Simulation Status tree.

**Pros:** live, ordered, matches 6.0.0 semantics, no `.rpt` parsing.
**Cons:** touches the legacy engine C code (global state; keep it
process-local — fine because each worker is its own process). Audit all
warning-emit sites, not just `report_writeWarningMsg`.

### Option B — Post-run `.rpt` scrape (no engine change)

After the worker finishes (or in the GUI after `finished`), read the `.rpt`,
extract `WARNING:` / `ERROR` lines, and emit them as warnings.

- Worker variant: after `swmm_close`, reopen the `.rpt`, grep warning lines,
  `emitWarning` each.
- GUI variant: in the `finished` handler for legacy jobs, parse the `.rpt`
  and feed `SimulationStatusModel::addWarning` + `onLogMessage`.

**Pros:** zero engine change; captures every warning the report has.
**Cons:** not live (only after run); brittle string parsing; duplicates the
report's formatting logic; warnings arrive all at once at the end.

### Option C — Count-only summary (minimal stopgap)

In the worker after `swmm_close`, call `swmm_getWarnings()`; if `> 0`, emit a
single warning like `"N warning(s) issued — see the report file (.rpt)."`

**Pros:** trivial, low-risk.
**Cons:** doesn't surface the actual messages; user must open the `.rpt`.

## Recommendation

Implement **Option A** (engine warning callback). It gives parity with the
6.0.0 path, is live and correctly ordered, and reuses the worker's existing
`emitWarning` + the GUI's existing `warning`-line parsing — so the only new
code is the engine callback hook and one registration call in the worker.

Use **Option C** only as an interim if engine changes must be deferred.

## Implementation checklist (Option A)

- [ ] Add `swmm_setWarningCallback` + typedef to `openswmm_solver.h`.
- [ ] Add process-local callback storage in the legacy engine; reset it in
      `swmm_open`/`swmm_close`.
- [ ] Invoke the callback from `report_writeWarningMsg()` and audit other
      warning-emit paths (grep `WARNING` / `report_write*` in
      `src/legacy/engine/`) to route them all through one helper.
- [ ] Register the callback in `worker/main.cpp` after `swmm_open`, calling
      `WorkerProgress::emitWarning`.
- [ ] Confirm GUI drain loop forwards `warning` lines (already does) and that
      they reach both the log and Simulation Status.
- [ ] Verify with `examples/demo_weir_culvert/weir_culvert.inp` (its title
      previously produced `Unknown section` warnings) and a model that emits
      runtime continuity / convergence warnings.

## Verification

1. Build `openswmm-legacy-worker` and `SWMMVis`.
2. Run a model with **SWMM 5.3.0** selected that is known to emit warnings.
3. Confirm warnings appear **live** in the message log (Warning severity) and
   as child rows under the job in the Simulation Status panel — not only in
   the `.rpt`.

## Related work already shipped (2026-05-29)

- Legacy run pipe-buffer **deadlock** fixed (incremental stdout drain) +
  live progress/date forwarding + working Cancel —
  `simulationrunner.cpp` legacy path, `worker/main.cpp`,
  `worker/worker_progress.h` (`emitDates`).
- Model-open errors now surface the real engine message via
  `swmm_error_message(rc)` — `swmmmodellayer.cpp` `loadModel`.
- Engine `warningReceived` now mirrored to the message log (in addition to
  the Simulation Status tree) — `swmmvis.cpp` ~line 4671.
