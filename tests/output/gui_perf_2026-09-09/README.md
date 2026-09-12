# GUI long-run memory / speed measurements — 2026-09-09

Companion to `workplans/GUI_LONG_RUN_MEMORY_PERF_PLAN_2026-09-09.md` (W0 / W5).
Everything this round measures lands in this directory (CLAUDE.md §4.1).

## Layout

- `watch_rss.sh` — copy of the 2026-08-13 watchdog: samples `phys_footprint` every 0.5 s into
  `rss_timeline.csv`, dumps `vmmap` / `heap` / `sample` once past `CAPTURE_MB`, kills past `KILL_MB`.
- `decks/bellinge_T{8,0,1}.inp` — `~/Downloads/bellinge_2d/BellingeSWMM_v021_nopervious.inp`
  with only the `THREADS` line changed (8 = as saved by the GUI, 0 = engine auto, 1 = serial).
  The user's ≥50k-cell deck goes beside them as `decks/<name>_T{8,0,1}.inp`.
- `cli_<deck>.log`, `gui_<deck>.log`, `sample_<deck>.txt`, `threads_<deck>.txt` — per-run captures.

## Protocol

CLI (per deck variant; the machine otherwise idle) — `./run_cli_baseline.sh T8 T0 T1` does all of this:

```sh
E=/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine
cd decks && /usr/bin/time -l $E/build/darwin/src/cli/openswmm bellinge_T8.inp bellinge_T8.rpt bellinge_T8.out 2>&1 | tee ../cli_bellinge_T8.log
# ~90 s in, from another shell:
ps -M -p $(pgrep -x openswmm) | awk 'NR>1 && $3>90' | wc -l     # threads > 90 % CPU
```
(`openswmm <inp> <rpt> [out]` — there is no `run` subcommand; the `.out` argument turns on
result saving, which the GUI always does.)

GUI (per deck variant):

```sh
G=/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui
KILL_MB=14000 CAPTURE_MB=7000 ./watch_rss.sh &
SWMM_LOG_FILE=$PWD/gui_bellinge_T8.log SWMMVIS_OPEN_ON_STARTUP=$PWD/decks/bellinge_T8.inp \
  $G/build/SWMMVis.app/Contents/MacOS/SWMMVis
# press Run; every 30 s:
ps -M -p $(pgrep -x SWMMVis) | awk 'NR>1 && $3>90' | wc -l
ps -M -p $(pgrep -x SWMMVis) | grep -c libomp
sample $(pgrep -x SWMMVis) 5 -file sample_bellinge_T8.txt
```

Engine wall time = `step loop` → `end` timestamps in `decks/<deck>.runlog.txt`
(the GUI writes one per run beside the report). The runlog also prints
`2D ticks skipped (GUI busy): N` when the back-pressure guard dropped frames.

Discriminators (same deck): env `KMP_BLOCKTIME=0` (passive OpenMP waits);
Preferences → Simulation `Live 2D history cap` 100 vs 2000, `Live 2D history budget`,
`Progress-tick interval` 1000 vs 5000, `Live 1D results` off; App Nap
`defaults write org.openswmm.swmmvis NSAppSleepDisabled -bool YES` (delete afterwards).
libomp images: `DYLD_PRINT_LIBRARIES=1 …/SWMMVis 2>&1 | grep -c libomp.dylib` (expect 1).

Crash rehearsal: two projects open, run in A, close B, toggle any dock 20×, close A;
then `ls -t ~/Library/Logs/DiagnosticReports | grep SWMMVis | head -1`.

## Status 2026-09-09 evening

- Fixes committed: GUI `cf6d7bc` (swmm6_gui), engine `27784b10` (swmm6_rel). Gates: GUI 17/17
  related tests (incl. the new `test_layertree_canvas_rebind`), engine 208/211 with the three
  known pre-existing reds (2d_output_options, 2d_infil_subcatch_aquifer, msx_parity).
- Clean app bundle for the manual gates (HEAD = cf6d7bc, no peers' in-flight hunks):
  `../../../../openswmm.gui.memfix/build/SWMMVis.app` — verified: plugin rpath has no Homebrew
  entry, plugin/libomp intact, `codesign -v --deep --strict` valid, bundled engine UUID = install.
  The shared `openswmm.gui/build/SWMMVis.app` was being rebuilt by peers at the time (its plugin
  copies had been truncated by two concurrent builds once).
- CLI baseline: `run_cli_baseline.sh` was run while the machine carried a peer's full build
  (load average 110–195) plus a two-day-old `openswmm` from `/tmp/e1e2-base` — treat
  `cli_bellinge_*.log` as indicative only and RERUN on an idle box. The first
  `threads_cli_bellinge_T8.txt` was re-taken by hand for the right pid (the script's
  `pgrep -x | head -1` picked the older stray process; use `pgrep -n -x openswmm`).
- GUI-side runs (watchdog, crash rehearsal, backgrounded run, ≥50k-cell deck) need someone at the
  keyboard — not yet run.

## Round 2 (2026-09-10) — the deferred accumulators

- GUI `27a18c0`: the comparison plot resolves only the new tail of each series per live tick
  (`SeriesData::firstPeriod` / `periodCount`; per-line `consumed` cursor; one full re-read when a
  live 2D source thins its history). Before: every series re-read in full every tick, with a
  full-mesh copy per frame for 2D velocity / edge-flux / rainfall. Pinned by
  `test_live2d_coalesce::runLayerResolvesOnlyTheRequestedTail`; all 7 comparison-plot tests pass.
- Engine (see git log for the hash): `ctx.control_log` entries are POD with the rule name interned
  by index, capped at 1 000 000 entries (24 MB) with a one-line overflow notice in the report;
  `ExplicitInertialSolver` folds active-fraction min/mean/max as samples are taken and keeps the
  sample vector only when `OPENSWMM_2D_MARCHER_TELEMETRY` is set.
- NOT done, on purpose — `ProfileBuilder::SourceDerived` per-period `QVector`s: the memory is
  payload (3 doubles × path nodes × periods; ~126 MB for 105k periods × 50 nodes), not allocation
  overhead (≈10 % for typical path lengths). Flattening would touch profileplotwidget.cpp and 25
  test sites for that 10 %. The real lever, if long-run profiles matter, is float storage (halves
  it) — a separate decision.

## Status 2026-09-11

- Engine `f397c94c` (swmm6_rel): FILLED_CIRCULAR conduit offsets cross the C API as authored values
  (follow-up to peer 3a's `4039342b`; without it a GUI edit of a filled conduit saved offsets shifted
  by the sediment depth). Full ctest on the shared tree 208/213, the five non-passes pre-existing.
  NOT installed into `install/Darwin` (shared tree carried peers' uncommitted hunks).
- GUI `067cccf` (swmm6_gui): stale `test_objectbrowser_tree_refresh` assertion updated to the
  72d188e contract (1/1); CHANGELOG Unreleased entries for cf6d7bc / 27a18c0 (engine CHANGELOG
  likewise for 27784b10 / b85b79c8 / f397c94c).
- `run_gui_bellinge.sh` — unattended GUI run (launch on deck, watchdog pinned by pid, Execute via
  System Events, 30 s thread samples, `sample` at 120 s and after finish, runlog wall, Cmd+Q).
  `decks_gui/` is a second copy of the deck so GUI and CLI runs never share output files.
- Protocol corrections (the commands above were wrong on real `ps -M` output): %CPU is column 4 on
  the process row and column 2 on thread rows, so count busy threads with the first float field
  (see `busy_threads` in the driver); `ps -M` never names `libomp` — count OpenMP threads from a
  `sample` call tree (`omp_threads_in_sample`) and OpenMP images from `DYLD_PRINT_LIBRARIES`.
  With two SWMMVis processes (the user's window + the harness) pin the watchdog with `PID=`.
- The CLI baseline's own thread snapshot (`pgrep -n -x openswmm`) picked a peer's parity binary of
  the same name; `threads_cli_bellinge_T8_pid77085.txt` is the one taken against the right pid.
- Pipeline outcome (evening): CLI T8 on the bundle's engine dylib took 26 469 s wall / 110 338 s
  user (13:10–20:31) under a load average of 50–73 from other sessions' parity sweeps — its eight
  OpenMP workers each held ~35 % of a core at the 90 s snapshot and spin-waited through the
  contention (`cli_bellinge_T8_loaded_2026-09-11.log`; the CLI has no host-thread reservation, so
  it never takes the passive-wait path however loaded the machine is). The paired GUI T8
  (`gui_T8_20260911_2031/`, hook-started at 20:31:48, load ~12) was CANCELLED from the keyboard at
  20:42:12 (the runlog's `finished: cancelled`; the only cancel paths are the Stop dialog, closing
  the project or the app) — someone was using the machine, and `open -j` does not keep the hidden
  run hidden once the project window opens. Pipeline stopped; nothing comparable was measured.
  Peak footprint while it ran: 1673 MB at 632 s (idle app with the deck open: 1349 MB), no thread
  above 90 %, one libomp image. Re-run when the machine is free:
  `./run_gates_v2_2026-09-11.sh` (CLI paired right after each GUI run) or a single GUI run with
  `TAG=T8 ./run_gui_bellinge.sh`.
- Observed, confounded: the user's own GUI run of the Bellinge deck from the shared
  `openswmm.gui/build` bundle on 2026-09-11 took `step loop` 06:04:04 → `end` 07:15:38 (4294 s)
  while this session's engine + GUI builds and a peer's full-corpus parity sweep were running
  (load average 21–37). Not a measurement.

## Results

| deck / variant | CLI wall (s) | GUI step-loop wall (s) | ratio | threads >90 % | libomp threads | footprint plateau (MB) | 2D ticks skipped | notes |
|---|---|---|---|---|---|---|---|---|
| bellinge_T8 (before) | 3641 (LOAD-DOMINATED: load avg 110–195, peer full build running; 16 620 s user = ~4.6 cores busy; max RSS 65.6 MB) | 1048 (2026-09-08 GUI runlog, quieter machine) | n/a | — | — | — | — | T0/T1 runs STOPPED — rerun `./run_cli_baseline.sh` on an idle box |
| bellinge_T8 on the STALE 10k-cell mesh (2026-09-11 08:19) | 1663 (load avg 8 → 19/25/53: peer parity sweep; 8555 s user = 5.1 cores; max RSS 59 MB; `cli_bellinge_T8_10kmesh_2026-09-11.log`) | — | — | — | — | — | — | SUPERSEDED: `decks/` carried a 10,211-cell mesh copied on 09-09, not the user's 42,358-cell `~/Downloads/bellinge_2d` mesh (kept as `decks/*_10k_2026-09-09.2dm`). Decks restaged from the Downloads files at 11:27. |
| bellinge_T8 (after, 42,358 cells) | `cli_bellinge_T8.log` | `gui_T8_<ts>/driver.log` | | | | | | pipeline `run_gates_2026-09-11.sh` → `gates_2026-09-11.log`; engine f397c94c (+ peers' uncommitted tree), GUI bundle = memfix at c20e23e (27a18c0 + the run-on-startup hook 0ad4ed3) |
| bellinge_T8 hidden from launch (App Nap) | — | `gui_T8_<ts>_bg/driver.log` | | | | | | W5 gate 5: ≤ 1.05× the foreground wall |
| bellinge_T0 (after, 42,358 cells) | `cli_bellinge_T0.log` | `gui_T0_<ts>/driver.log` | | | | | | |
| bellinge_T0 (before) | | | | | | | | |
| bellinge_T1 (before) | | | | | | | | |
| bellinge_T8 (after)  | | | | | | | | |
| bellinge_T0 (after)  | | | | | | | | |

Reference points already on record: GUI run of the as-saved Bellinge deck on 2026-09-08 took
17 min 28 s (`step loop` 07:19:25 → `end` 07:36:53, runlog); the 2026-09-07 crash report shows
21 libomp threads in one SWMMVis process on this 10-CPU machine.
