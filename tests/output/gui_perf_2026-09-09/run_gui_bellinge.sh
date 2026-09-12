#!/usr/bin/env bash
# run_gui_bellinge.sh — one unattended GUI run of a deck with the clean bundle
# (plan W0 step 2 / W5 gates 2, 3, 5). Launches SWMMVis on DECK with the
# SWMMVIS_RUN_ON_STARTUP dev hook (the app presses Execute itself once the mesh
# lands — no macOS Accessibility needed), pins watch_rss.sh to that pid,
# samples per-thread CPU every 30 s, takes one `sample` at ~120 s and one right
# after the run ends (the post-finish libomp spin check), waits for the runlog
# `end`, then stops the app. Everything lands in gui_<TAG>_<timestamp>[_bg]/.
#
#   TAG=T8 [DECK=decks_gui/bellinge_T8.inp] [BACKGROUNDED=1] [WAIT_FOR_PID=<pid>]
#   [ENV_EXTRA="OPENSWMM_2D_MIN_PARALLEL_CELLS=1000"] [MAX_S=300] ./run_gui_bellinge.sh
#
# BACKGROUNDED=1 launches the bundle through `open -j` (hidden from the start,
# the App Nap condition) instead of exec'ing the binary in the foreground.
#
# Gotchas baked in: `ps -M` puts %CPU in column 4 on the process row and
# column 2 on thread rows (use the first float field); it never names libomp
# (count OpenMP threads from the `sample` call trees instead); a second
# SWMMVis (the user's own window) makes `pgrep -x` ambiguous (pin by pid);
# an existing .out triggers the overwrite prompt (delete it first); the
# SWMM_LOG_FILE carries the qCInfo categories, not the Message Log panel, so
# the load marker is `openswmm.load.mesh: … LOD pyramid ready`.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
TAG="${TAG:-T8}"
DECK="${DECK:-$HERE/decks_gui/bellinge_$TAG.inp}"
BUNDLE="${BUNDLE:-/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui.memfix/build/SWMMVis.app}"
APP="$BUNDLE/Contents/MacOS/SWMMVis"
OUT="$HERE/gui_${TAG}_$(date +%Y%m%d_%H%M)${BACKGROUNDED:+_bg}${ENV_EXTRA:+_env}"
mkdir -p "$OUT"
log() { echo "$(date +%H:%M:%S) $*" | tee -a "$OUT/driver.log"; }

busy_threads() { ps -M -p "$1" | awk 'NR>1 { for (i=1;i<=NF;i++) if ($i ~ /^[0-9]+\.[0-9]+$/) { if ($i+0>90) n++; break } } END { print n+0 }'; }
# `sample` thread headers look like "    2537 Thread_23429871: Main Thread …" (leading sample count).
omp_threads_in_sample() { awk '/^ *[0-9]+ Thread_[0-9]+/ {t++; omp=0} /libomp|__kmp/ { if (!omp) { omp=1; n++ } } END { print n+0 "/" t+0 }' "$1" 2>/dev/null; }

if [ -n "${WAIT_FOR_PID:-}" ]; then
    log "waiting for pid $WAIT_FOR_PID to exit"
    while kill -0 "$WAIT_FOR_PID" 2>/dev/null; do sleep 15; done
fi

stem="${DECK%.inp}"
rm -f "$stem.out" "$stem.rpt" "$stem.runlog.txt" "$(dirname "$DECK")"/*.2d.h5
log "deck $DECK"
log "app  $APP ($(git -C "$(dirname "$BUNDLE")/.." log --oneline -1 2>/dev/null || echo 'no git'))"
log "load average before: $(sysctl -n vm.loadavg)"

before=$(pgrep -x SWMMVis | sort)
if [ -n "${BACKGROUNDED:-}" ]; then
    envargs=(--env SWMM_LOG_FILE="$OUT/gui.log" --env SWMMVIS_OPEN_ON_STARTUP="$DECK"
             --env SWMMVIS_RUN_ON_STARTUP=1 --env DYLD_PRINT_LIBRARIES=1)
    for kv in ${ENV_EXTRA:-}; do envargs+=(--env "$kv"); done
    open -j -n -a "$BUNDLE" "${envargs[@]}" --stdout "$OUT/app_stdout.txt" --stderr "$OUT/app_stderr.txt"
    APP_PID=""
    for _ in $(seq 1 60); do
        APP_PID=$(comm -13 <(echo "$before") <(pgrep -x SWMMVis | sort) | head -1)
        [ -n "$APP_PID" ] && break
        sleep 0.5
    done
    [ -z "$APP_PID" ] && { log "ERROR: hidden launch produced no SWMMVis process"; exit 1; }
    log "app pid $APP_PID (hidden launch via open -j)"
else
    # shellcheck disable=SC2086
    env ${ENV_EXTRA:-} DYLD_PRINT_LIBRARIES=1 SWMM_LOG_FILE="$OUT/gui.log" \
        SWMMVIS_OPEN_ON_STARTUP="$DECK" SWMMVIS_RUN_ON_STARTUP=1 \
        "$APP" > "$OUT/app_stderr.txt" 2>&1 &
    APP_PID=$!
    log "app pid $APP_PID"
fi
OUT="$OUT" PID="$APP_PID" KILL_MB="${KILL_MB:-14000}" CAPTURE_MB="${CAPTURE_MB:-7000}" \
    "$HERE/watch_rss.sh" > "$OUT/watchdog.txt" 2>&1 &
WD_PID=$!

for _ in $(seq 1 240); do
    grep -q 'LOD pyramid ready' "$OUT/gui.log" 2>/dev/null && break
    kill -0 "$APP_PID" 2>/dev/null || { log "app died during open"; exit 1; }
    sleep 2
done
grep -q 'LOD pyramid ready' "$OUT/gui.log" 2>/dev/null && log "mesh loaded" || log "WARNING: mesh marker not seen after 480 s"

RUNLOG="$stem.runlog.txt"
for _ in $(seq 1 150); do
    grep -q 'step loop' "$RUNLOG" 2>/dev/null && break
    kill -0 "$APP_PID" 2>/dev/null || { log "app died before the run started"; exit 1; }
    sleep 2
done
if grep -q 'step loop' "$RUNLOG" 2>/dev/null; then
    log "run started: $(grep 'step loop' "$RUNLOG")"
else
    log "ERROR: no 'step loop' in $RUNLOG after 300 s — the run did not start (hook missing in this bundle, or a prompt is up)"
    kill -TERM "$APP_PID"; kill "$WD_PID" 2>/dev/null; exit 1
fi

t0=$(date +%s); sampled=0
echo "elapsed_s,threads_gt90,cpu_pct_lifetime" > "$OUT/threads.csv"
while kill -0 "$APP_PID" 2>/dev/null; do
    if grep -q ' end$' "$RUNLOG" 2>/dev/null; then break; fi
    el=$(( $(date +%s) - t0 ))
    echo "$el,$(busy_threads "$APP_PID"),$(ps -o pcpu= -p "$APP_PID" | tr -d ' ')" >> "$OUT/threads.csv"
    if [ "$sampled" -eq 0 ] && [ "$el" -ge 120 ]; then
        /usr/bin/sample "$APP_PID" 5 -file "$OUT/sample_running.txt" >/dev/null 2>&1 &
        sampled=1
    fi
    if [ "$el" -gt "${MAX_S:-14400}" ]; then log "MAX_S reached after $el s — stopping the run"; break; fi
    sleep 30
done
log "run ended (driver elapsed $(( $(date +%s) - t0 )) s)"
sleep 30   # W5 gate 3: no libomp thread > 90 % thirty seconds after finish
/usr/bin/sample "$APP_PID" 5 -file "$OUT/sample_after_finish.txt" >/dev/null 2>&1
log "busy threads 30 s after finish: $(busy_threads "$APP_PID")"
log "OpenMP threads in sample (running / after finish): $(omp_threads_in_sample "$OUT/sample_running.txt") / $(omp_threads_in_sample "$OUT/sample_after_finish.txt")"
log "libomp images loaded: $(grep -c 'libomp.dylib' "$OUT/app_stderr.txt" 2>/dev/null)"
grep -n 'step loop\| end$\|2D ticks skipped\|finished' "$RUNLOG" 2>/dev/null | tee -a "$OUT/driver.log"
cp "$RUNLOG" "$OUT/" 2>/dev/null
log "load average after: $(sysctl -n vm.loadavg)"

kill -TERM "$APP_PID" 2>/dev/null
for _ in $(seq 1 20); do kill -0 "$APP_PID" 2>/dev/null || break; sleep 1; done
kill -0 "$APP_PID" 2>/dev/null && { log "app still up after SIGTERM — SIGKILL"; kill -KILL "$APP_PID"; }
wait "$WD_PID" 2>/dev/null
log "peak footprint (elapsed_s,rss_mb,footprint_mb): $(sort -t, -k3 -n "$OUT/rss_timeline.csv" | tail -1)"
log "done → $OUT"
