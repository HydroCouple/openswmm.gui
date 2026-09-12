#!/usr/bin/env bash
# run_gates_v2_2026-09-11.sh — continuation of run_gates_2026-09-11.sh after its
# step 3 (GUI T8 foreground). The first pipeline's CLI T8 ran 13:10–20:31 under a
# load average of 50–73 from other sessions' parity sweeps (26 469 s wall,
# 110 338 s user: eight OpenMP workers spin-waiting through the contention), so
# it cannot be paired with a GUI run made under a load of ~12. This pipeline
# puts every CLI run IMMEDIATELY after the GUI run it is compared with:
#   1. CLI T8 (bundle engine dylib)            → cli_bellinge_T8.log   (the 13:10 run is kept as cli_bellinge_T8_loaded_2026-09-11.log)
#   2. GUI T8 hidden from launch (App Nap)     → gui_T8_<ts>_bg/
#   3. GUI T8 with the OpenMP plugin forced, 4 min → gui_T8_<ts>_env/  (libomp image count)
#   4. GUI T0 foreground, then CLI T0          → gui_T0_<ts>/, cli_bellinge_T0.log
# Progress lines append to gates_2026-09-11.log.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
LOG="$HERE/gates_2026-09-11.log"
MEMFIX=/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui.memfix
export ENGINE_LIB_DIR="$MEMFIX/build/SWMMVis.app/Contents/Frameworks"
step() { echo "$(date '+%F %T') V2 STEP $*" | tee -a "$LOG"; }

[ -f "$HERE/cli_bellinge_T8.log" ] && mv "$HERE/cli_bellinge_T8.log" "$HERE/cli_bellinge_T8_loaded_2026-09-11.log"
[ -f "$HERE/threads_cli_bellinge_T8.txt" ] && mv "$HERE/threads_cli_bellinge_T8.txt" "$HERE/threads_cli_bellinge_T8_loaded_2026-09-11.txt"

step "1 CLI T8 (paired with the 20:31 GUI T8; load $(sysctl -n vm.loadavg))"
"$HERE/run_cli_baseline.sh" T8 > "$HERE/cli_run_T8_v2.log" 2>&1
step "1 CLI T8 done: $(grep -m1 'real' "$HERE/cli_bellinge_T8.log" | tr -s ' ')"

step "2 GUI T8 hidden (App Nap)"
TAG=T8 BACKGROUNDED=1 "$HERE/run_gui_bellinge.sh" > "$HERE/gui_run_T8_bg.log" 2>&1
step "2 GUI T8 hidden done: $(grep -E 'run ended|ERROR|peak footprint' "$HERE/gui_run_T8_bg.log" | tr '\n' ' | ')"

step "3 GUI T8 with the OpenMP plugin forced, 4 min"
TAG=T8 MAX_S=240 ENV_EXTRA="OPENSWMM_2D_MIN_PARALLEL_CELLS=1000" "$HERE/run_gui_bellinge.sh" > "$HERE/gui_run_T8_omp.log" 2>&1
step "3 done: $(grep -E 'libomp images|OpenMP threads|ERROR' "$HERE/gui_run_T8_omp.log" | tr '\n' ' | ')"

cp "$HERE/decks/bellinge_T0.inp" "$HERE/decks_gui/bellinge_T0.inp"
step "4 GUI T0 foreground"
TAG=T0 "$HERE/run_gui_bellinge.sh" > "$HERE/gui_run_T0_fg.log" 2>&1
step "4 GUI T0 done: $(grep -E 'run ended|ERROR|peak footprint' "$HERE/gui_run_T0_fg.log" | tr '\n' ' | ')"
step "4 CLI T0 (paired; load $(sysctl -n vm.loadavg))"
"$HERE/run_cli_baseline.sh" T0 > "$HERE/cli_run_T0_v2.log" 2>&1
step "4 CLI T0 done: $(grep -m1 'real' "$HERE/cli_bellinge_T0.log" | tr -s ' ')"

step "ALL DONE (v2)"
