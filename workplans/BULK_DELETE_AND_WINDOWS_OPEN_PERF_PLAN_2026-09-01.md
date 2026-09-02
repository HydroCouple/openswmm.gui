# Bulk-Delete Performance + Windows Open Lag — Plan (2026-09-01)

## Context

Two complaints: (1) bulk-deleting nodes, links, subcatchments, and cross sections (transects) is very slow; (2) file open is extremely laggy on Windows. The user's instinct — "disable redrawing after each delete" — turns out to be **already implemented**: a `SWMMModelLayer::BulkEdit` RAII guard + `BulkEditCommand` macro ([swmmmodellayer.h:1676-1722](include/layers/swmmmodellayer.h), [mapundostack.h:683-704](include/map/mapundostack.h)) wraps all three main delete paths and defers rebuild/repaint to one shot per batch (`test_bulkdelete.cpp` pins the old 20.7 s fix). The remaining cost is elsewhere:

- **Engine**: every single `swmm_node_delete` does 3+ full-model scans, per-array `erase_at`, `renumber_refs` over 9 index arrays, and — worst — `NameIndex::remove_at` **clears and rehashes the entire name→index map per delete** (engine repo, `src/engine/data/NameIndex.hpp:224-231`). K deletes on a 104k-node/281k-link model = K full rehashes.
- **GUI**: `analyze_node_impact` (O(N+L)) runs **twice per node** before deleting ([maptoolselect.cpp:678](src/map/tools/maptoolselect.cpp), [mapundostack.cpp:929](src/map/mapundostack.cpp)); per-delete SoA `removeAt` is O(K·N) ([swmmmodellayer.cpp:5563-5629](src/layers/swmmmodellayer.cpp)).
- **Data objects (transects = the user's "cross sections", curves, timeseries) have NO batching at all**: `DeleteDataObjectCommand::redo` calls `registry->saveToEngine()` which **rewrites every remaining provider** per delete ([mapundostack.cpp:1186-1219](src/map/mapundostack.cpp)); editor dialogs call raw `registry->remove` with no undo. **Confirmed bug**: `TransectRegistry::remove` never calls `swmm_transect_delete` ([transectregistry.cpp:66-73](src/transect/transectregistry.cpp)) — the engine copy is never erased.

For Windows open lag: a 2026-07 async program already moved parse/SoA/geometry to a worker (macOS opens fine). The **prime Windows suspect** is [main.cpp:46-47](src/main.cpp): `QSG_RHI_BACKEND=opengl` — a documented **macOS Metal-bug workaround applied unconditionally** — forces Windows off its D3D11 default onto desktop GL (→ `opengl32sw.dll` software rasterization on RDP/VM/hybrid-GPU boxes), and every open constructs a fresh QQuickWidget+RHI context ([mapcanvas.cpp:187-212](src/map/mapcanvas.cpp)). Verified safe to guard: `qt_add_shaders` (CMakeLists.txt:1502) emits HLSL via qsb defaults, and no GL-only scenegraph code exists (grepped). Secondary GUI-thread tail (all untimed today): `AttributeTablePanel::refresh` fires many times per open with a per-refresh **delegate leak** + fresh `QSettings` (registry on Windows); `onLogMessage` does `scrollToBottom` per row while thousands of warnings drain; the .inp is read **4× per open** (2 on the GUI thread); `saveSettings()` registry write per open.

**User decisions**: engine-repo changes are IN scope (batch delete API). No Windows box available for measurement — ship high-confidence fixes blind, but still ship the telemetry so future Windows runs can measure (file-tee logging; today Windows telemetry is unreachable: `WIN32_EXECUTABLE` + no message handler = DebugView only).

## Phases

Each phase independently shippable/revertible. Mirror this plan to `workplans/BULK_DELETE_AND_WINDOWS_OPEN_PERF_PLAN_2026-09-01.md` (tracked corpus).

### Phase 0 — Instrumentation (GUI; ship first)
1. File-tee message handler, opt-in `SWMM_LOG_FILE=<path>` env (default `AppDataLocation/logs/`, location echoed to the Message Log — CLAUDE.md transparent-IO). Install in `main()` and **chain to the previous handler** so `test_asyncload`'s capture handlers still work.
2. New `openswmm.load.gui` category timing the untimed tail: each `onActiveSubWindowChanged` pass, each `AttributeTablePanel::refresh` (+trigger reason), `onLogMessage` drain, `readMapUnitsFromInp`, `parseTwoDOutputFile`, `saveSettings`. New `openswmm.bulkdelete` category timing engine-delete loop vs `endBulkEdit` phases.
3. Extend the `profileExternalModel` harness pattern with a bulk-delete probe; macOS baselines to `tests/output/openprofile/`.
4. Document the Windows protocol in the workplan (env vars `QSG_INFO=1`, `SWMM_LOG_FILE`; what the log shows) — **not a gate**, per user decision.
- **Gate**: full GUI ctest; macOS baseline recorded. Files: [main.cpp](src/main.cpp), [swmmvis.cpp](src/swmmvis.cpp), [attributetablepanel.cpp](src/ui/panels/attributetablepanel.cpp), [swmmmodellayer.cpp](src/layers/swmmmodellayer.cpp), test_asyncload.cpp.

### Phase A1 — Engine batch-delete API (ENGINE repo)
Per-type `swmm_node_delete_many(engine, const int* indices, int n, SWMM_ImpactReport*)` (+ link/subcatch/transect), next to the existing per-type declarations (`openswmm_edit.h:225-272`); a begin/end deferred mode was rejected (leaves the engine index-inconsistent between public calls). Semantics: indices + cascade report in ORIGINAL pre-batch indices; result state identical to sequential descending-index deletes; cascade set computed engine-side in one link scan (stays engine-owned).
Implementation in `ObjectDeleter.cpp`: mark bitmask → ONE link scan (cascade) + ONE subcatch scan + ONE node scan → old→new prefix-sum map → single compaction pass over every SoA array → one renumber pass → new `NameIndex::remove_many` (one rebuild). Total O(N+L+K), **one** rehash.
- **Gate**: parity test (random models/subsets: final state + cascade set identical to sequential path) + perf pin + **full engine ctest** (hard repo rule) before commit. Install engine before A2. Additive API — old path untouched.

### Phase A2 — GUI batch delete (GUI; needs A1 installed)
New `BatchDeleteCommand` (in [mapundostack.cpp](src/map/mapundostack.cpp)) replacing N× `DeleteObjectCommand` inside the existing `BulkEditCommand` at the three call sites ([maptoolselect.cpp:833](src/map/tools/maptoolselect.cpp), [attributetablepanel.cpp:2590](src/ui/panels/attributetablepanel.cpp), [objectbrowserpanel.cpp:564](src/ui/panels/objectbrowserpanel.cpp)):
1. All undo snapshots taken **before any delete** (reuse the classification pass's `analyze_node_impact` result instead of re-calling — kills the duplicate O(N+L) scan). With all reads pre-delete, there are no in-bulk readers → no tombstone machinery needed.
2. `redo()` = one `swmm_*_delete_many` + new `SWMMModelLayer::applyBatchDelete(sortedIndices)` mark-and-sweep SoA compaction using the same old→new map (replaces K× `removeAt`); `endBulkEdit` unchanged (one rebuild+repaint).
3. `undo()` = existing per-object restore, ascending original order (rare direction; documented).
4. Wrap the one unguarded delete site ([maptoolselect.cpp:1899-1929](src/map/tools/maptoolselect.cpp)) in the same machinery.
- **Gate**: extend `test_bulkdelete.cpp` — delete/undo/redo round-trip vs per-object reference; timing pin at 5k deletes; full GUI ctest.

### Phase A3 — Data-object deletes (GUI; independent of A1/A2)
1. **Bug fix**: `TransectRegistry::remove` calls `swmm_transect_delete` when engine-bound (follow `rename()`'s pattern at [transectregistry.cpp:85-92](src/transect/transectregistry.cpp)); audit curve/timeseries/pattern registries for the same gap.
2. With remove engine-authoritative, drop the full `saveToEngine()` reflush from `DeleteDataObjectCommand::redo` — O(total data) → O(1) per delete.
3. ~~Editor dialogs' deletes through `DeleteDataObjectCommand` in a `BulkEditCommand`~~ — **DEFERRED at implementation (2026-09-01)**: the dialogs delete ONE object at a time (`m_current`), so there is nothing to batch, and steps 1–2 already make their deletes engine-correct and O(1). The remaining value is undo support, which requires plumbing `SWMMModelLayer*` into the curve/timeseries dialog constructors (the transect dialog already has one) — a UX change, not a perf one. Recorded as debt.
- **Gate**: regression pinning engine transect count drops on remove + survives save/reload; dialog multi-delete undo test; full GUI ctest. Separate commits for 1/2/3 (2 depends on 1).

### Phase B1 — Windows RHI backend guard (GUI; 2 lines + smoke)
Wrap `QSG_RHI_BACKEND=opengl` / `QSG_RENDER_LOOP=threaded` ([main.cpp:46-47](src/main.cpp)) in `#ifdef Q_OS_MACOS`, and only when not already set (`qEnvironmentVariableIsSet`) so any platform can override for diagnostics. Windows returns to Qt's D3D11 default. Verified-safe: shaders ship HLSL 5.0; zero GL-only code.
- **Gate**: macOS full GUI ctest + visual smoke (scalar-fill material renders). Windows verification deferred to whenever a box is available (`QSG_INFO=1` must show `d3d11`).

### Phase B2 — GUI-thread open tail (GUI; ranked, each an independent commit)
1. Coalesce `AttributeTablePanel::refresh` via the zero-timer idiom (precedent: [swmmmodellayer.cpp:552-570](src/layers/swmmmodellayer.cpp)) across its 5 trigger signals; skip the engine-less refresh #1; cache the `QSettings` reads; **fix the per-column delegate leak** ([attributetablepanel.cpp:1356-1376](src/ui/panels/attributetablepanel.cpp)).
2. Batch `onLogMessage`'s `scrollToBottom` behind a zero-timer ([swmmvis.cpp:405-417](src/swmmvis.cpp)) — the warning drain currently does a tree relayout per row.
3. Move `readMapUnitsFromInp` into the existing load worker; dedupe the 4× .inp reads (Windows: each read = a full Defender scan).
4. Defer the per-open `saveSettings()` registry write to idle/close ([swmmvis.cpp:4888](src/swmmvis.cpp)).
Deferred (recorded in workplan, not this program): `onActiveSubWindowChanged` 3×-rebind restructure; engine open-progress callback (perception fix, needs engine); LARGE_MESH F4/F6.
- **Gate**: full GUI ctest + `openswmm.load.gui` before/after deltas on macOS; delegate leak pinned by an object-count test.

## Verification (end-to-end)
- Bulk delete: `test_bulkdelete` timing pins (5k-object delete) before/after A1+A2; manual check in the app — select thousands of nodes → Delete → single repaint, seconds not minutes; undo restores.
- Data objects: delete many transects from the editor/browser → instant, undoable; save+reload shows them gone (the bug fix).
- Open: macOS `SWMM_LOG_FILE` profile deltas per B2 item; Windows protocol documented for future measurement.
- Suites: full engine ctest before the A1 commit; full GUI ctest (180+) before every GUI commit.

## Order of execution
Phase 0 → A3 (small, self-contained, fixes a real bug) → A1 → A2 → B1 → B2. A3 and B1 can interleave anywhere; A2 strictly after A1's engine install.

## Appendix — Windows measurement protocol (Phase 0 deliverable)

No Windows box was available during implementation (user decision 2026-09-01),
so fixes shipped on code-level evidence. When a Windows machine is available,
one run captures everything:

1. Set environment variables before launching SWMMVis:
   - `SWMM_LOG_FILE=1` (logs to `%APPDATA%\logs\swmmvis-<timestamp>.log`; or
     give an explicit path)
   - `QT_LOGGING_RULES=openswmm.load.*=true;openswmm.bulkdelete=true`
   - `QSG_INFO=1` (Qt prints the active RHI backend/driver — after Phase B1
     this must say `d3d11`; `opengl32sw` means software rasterization, the
     Phase-B1 smoking gun)
2. Open the slow model twice (cold then warm — the delta is the antivirus /
   page-cache share). Every `openswmm.load.*` line lands in the log file.
3. Select ~1000 nodes and delete them; the `openswmm.bulkdelete` lines split
   classify / snapshots / execute+close / endBulkEdit sub-steps.
4. Offline twin (no GUI needed):
   `set SWMM_PROFILE_INP=<path.inp>` then run `test_bulkdelete profileBulkDelete`
   and `test_asyncload profileExternalModel`.
5. Send back the log file.
