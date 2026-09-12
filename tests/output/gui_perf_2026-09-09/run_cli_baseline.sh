#!/usr/bin/env bash
# CLI baseline for the GUI-vs-CLI gap (plan W0): run each deck variant with the
# engine CLI, record wall time + max RSS (/usr/bin/time -l), the load average
# before the run, and a per-thread CPU snapshot ~90 s in (how many OpenMP
# threads are spinning). Outputs land beside this script (CLAUDE.md §4.1).
#
#   ./run_cli_baseline.sh [T8 T0 T1 ...]        # default: T8 T0 T1
#   WAIT_FOR=<file-with-CTEST_EXIT> ./run_cli_baseline.sh   # start after a build/test log completes
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
CLI="${CLI:-/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/build/darwin/src/cli/openswmm}"
VARIANTS="${*:-T8 T0 T1}"
# ENGINE_LIB_DIR=<dir> makes the CLI load that directory's libopenswmm.engine and
# libomp instead of build/darwin's — point it at the GUI bundle's
# Contents/Frameworks so the GUI and CLI runs compare the SAME engine build.
if [ -n "${ENGINE_LIB_DIR:-}" ]; then export DYLD_LIBRARY_PATH="$ENGINE_LIB_DIR"; fi

if [ -n "${WAIT_FOR:-}" ]; then
    until grep -q "CTEST_EXIT=" "$WAIT_FOR" 2>/dev/null; do sleep 15; done
fi

cd "$HERE/decks" || exit 1
for v in $VARIANTS; do
    deck="bellinge_$v"
    log="$HERE/cli_$deck.log"
    {
        echo "=== $deck  started $(date '+%Y-%m-%d %H:%M:%S')"
        echo "engine: $CLI"
        [ -n "${ENGINE_LIB_DIR:-}" ] && echo "engine dylib: $(dwarfdump --uuid "$ENGINE_LIB_DIR/libopenswmm.engine.6.dylib" 2>/dev/null | awk '{print $2}') from $ENGINE_LIB_DIR"
        uptime
    } > "$log"
    t0=$(date +%s)
    /usr/bin/time -l "$CLI" "$deck.inp" "$deck.rpt" "$deck.out" >> "$log" 2>&1 &
    tpid=$!
    # Thread snapshot while the run is in its stride, against THIS run's pid
    # (the child of /usr/bin/time) — `pgrep -x openswmm` also matches peers'
    # parity binaries of the same name, and -f matches this shell.
    ( sleep 90; cpid=$(pgrep -P "$tpid" -x openswmm); [ -n "$cpid" ] && ps -M -p "$cpid" > "$HERE/threads_cli_$deck.txt" 2>&1 ) &
    snap=$!
    wait "$tpid"
    rc=$?
    t1=$(date +%s)
    wait $snap 2>/dev/null
    {
        echo "CLI_EXIT=$rc wall_s=$((t1 - t0))"
        uptime
        echo "=== $deck  finished $(date '+%Y-%m-%d %H:%M:%S')"
    } >> "$log"
    echo "$deck CLI_EXIT=$rc wall_s=$((t1 - t0))"
done
echo ALL_CLI_DONE
