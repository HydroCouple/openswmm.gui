#!/usr/bin/env bash
# run_gates_2026-09-11.sh — the unattended gate pipeline for the Bellinge deck
# (plan W5), one step after another so no two timed runs share the machine:
#   1. rebuild the clean bundle in the memfix worktree (adds SWMMVIS_RUN_ON_STARTUP)
#   2. CLI T8 baseline                       → cli_bellinge_T8.log
#   3. GUI T8, foreground                    → gui_T8_<ts>/
#   4. GUI T8, hidden from launch (App Nap)  → gui_T8_<ts>_bg/
#   5. GUI T8 with the OpenMP plugin forced (OPENSWMM_2D_MIN_PARALLEL_CELLS=1000),
#      stopped after 4 min — the libomp image count      → gui_T8_<ts>_env/
#   6. CLI T0 baseline, then GUI T0          → cli_bellinge_T0.log, gui_T0_<ts>/
# Progress lines go to gates_2026-09-11.log beside this script.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
LOG="$HERE/gates_2026-09-11.log"
MEMFIX=/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui.memfix
step() { echo "$(date '+%F %T') STEP $*" | tee -a "$LOG"; }

step "1 build memfix bundle ($(git -C "$MEMFIX" log --oneline -1))"
if cmake --build "$MEMFIX/build" -j6 > "$HERE/memfix_build_2026-09-11.log" 2>&1; then
    step "1 build ok; bundle $(ls -la "$MEMFIX/build/SWMMVis.app/Contents/MacOS/SWMMVis" | awk '{print $6, $7, $8}')"
    codesign -v --deep --strict "$MEMFIX/build/SWMMVis.app" >> "$LOG" 2>&1 && step "1 codesign valid" || step "1 WARNING codesign invalid"
else
    step "1 BUILD FAILED (see memfix_build_2026-09-11.log) — GUI steps will report 'run did not start'"
fi

# Same engine build for both sides of the ratio: the CLI loads the bundle's
# engine + libomp dylibs (run_cli_baseline.sh exports DYLD_LIBRARY_PATH).
export ENGINE_LIB_DIR="$MEMFIX/build/SWMMVis.app/Contents/Frameworks"
step "2 CLI T8 (engine dylib from $ENGINE_LIB_DIR)"
"$HERE/run_cli_baseline.sh" T8 > "$HERE/cli_run_T8_2026-09-11.log" 2>&1
step "2 CLI T8 done: $(grep -m1 'real' "$HERE/cli_bellinge_T8.log" | tr -s ' ')"

step "3 GUI T8 foreground"
TAG=T8 "$HERE/run_gui_bellinge.sh" > "$HERE/gui_run_T8_fg.log" 2>&1
step "3 GUI T8 done: $(grep -E 'run ended|ERROR|peak footprint' "$HERE/gui_run_T8_fg.log" | tr '\n' ' | ')"

step "4 GUI T8 hidden (App Nap)"
TAG=T8 BACKGROUNDED=1 "$HERE/run_gui_bellinge.sh" > "$HERE/gui_run_T8_bg.log" 2>&1
step "4 GUI T8 hidden done: $(grep -E 'run ended|ERROR|peak footprint' "$HERE/gui_run_T8_bg.log" | tr '\n' ' | ')"

step "5 GUI T8 with the OpenMP plugin forced, 4 min"
TAG=T8 MAX_S=240 ENV_EXTRA="OPENSWMM_2D_MIN_PARALLEL_CELLS=1000" "$HERE/run_gui_bellinge.sh" > "$HERE/gui_run_T8_omp.log" 2>&1
step "5 done: $(grep -E 'libomp images|OpenMP threads|ERROR' "$HERE/gui_run_T8_omp.log" | tr '\n' ' | ')"

step "6 CLI T0"
"$HERE/run_cli_baseline.sh" T0 > "$HERE/cli_run_T0_2026-09-11.log" 2>&1
step "6 CLI T0 done: $(grep -m1 'real' "$HERE/cli_bellinge_T0.log" | tr -s ' ')"
cp "$HERE/decks/bellinge_T0.inp" "$HERE/decks_gui/bellinge_T0.inp"
step "6 GUI T0 foreground"
TAG=T0 "$HERE/run_gui_bellinge.sh" > "$HERE/gui_run_T0_fg.log" 2>&1
step "6 GUI T0 done: $(grep -E 'run ended|ERROR|peak footprint' "$HERE/gui_run_T0_fg.log" | tr '\n' ' | ')"

step "ALL DONE"
