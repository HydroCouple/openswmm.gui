#!/usr/bin/env bash
# Sample SWMMVis memory and, when it spikes, capture WHAT is holding it.
#
# PID SELECTION: match the EXACT process name with `pgrep -x`, never `pgrep -f`
# (a -f match also matches the launching shell, whose command line contains the
# path; picking that wrapper reports ~1 MB forever and the watchdog never fires).
#
# Reports phys_footprint (resident + compressed — what Activity Monitor calls
# "Memory"), because macOS compresses aggressively and RSS understates a runaway.
#
# On crossing CAPTURE_MB it dumps, once:
#   vmmap -summary  -> which region grew (malloc vs GPU/IOAccelerator vs FBO)
#   heap -sortBySize -> allocation counts by class, top offenders
# Both are cheap enough to run against a live process and are the difference
# between "memory exploded" and knowing which subsystem did it.

set -uo pipefail

OUT="${OUT:-$(cd "$(dirname "$0")" && pwd)}"   # OUT=<dir> keeps one run's captures apart
mkdir -p "$OUT"
CSV="$OUT/rss_timeline.csv"
KILL_MB="${KILL_MB:-10240}"     # 32 GB machine — stay well clear of swap death
WARN_MB="${WARN_MB:-4096}"
CAPTURE_MB="${CAPTURE_MB:-5500}"

echo "elapsed_s,rss_mb,footprint_mb" > "$CSV"
start=$(date +%s)
peak=0
next_warn=$WARN_MB
captured=0

echo "waiting for SWMMVis…"
# PID=<pid> pins the instance when more than one SWMMVis is running (the
# user's own window vs. the harness launch); otherwise the first match wins.
if [ -n "${PID:-}" ]; then
    pid="$PID"
else
    while ! pgrep -x SWMMVis >/dev/null; do sleep 0.5; done
    pid=$(pgrep -x SWMMVis | head -1)
fi
echo "watching pid $pid — warn ${WARN_MB} MB, capture ${CAPTURE_MB} MB, kill ${KILL_MB} MB"

while kill -0 "$pid" 2>/dev/null; do
    rss=$(ps -o rss= -p "$pid" 2>/dev/null | tr -d ' ')
    if [ -n "${rss:-}" ]; then
        rss_mb=$((rss / 1024))
        fp_mb=$(/usr/bin/footprint -p "$pid" 2>/dev/null \
                | awk '/phys_footprint:/ {print $2; exit}')
        case "${fp_mb:-}" in ''|*[!0-9]*) fp_mb=$rss_mb ;; esac
        echo "$(( $(date +%s) - start )),$rss_mb,$fp_mb" >> "$CSV"

        big=$(( fp_mb > rss_mb ? fp_mb : rss_mb ))
        [ "$big" -gt "$peak" ] && peak=$big
        if [ "$peak" -ge "$next_warn" ]; then
            echo "WARN: ${peak} MB"
            next_warn=$(( next_warn * 2 ))
        fi

        # One-shot forensic capture while the memory is still held.
        if [ "$captured" -eq 0 ] && [ "$big" -gt "$CAPTURE_MB" ]; then
            captured=1
            echo "CAPTURE at ${big} MB — vmmap + heap + sample (~15 s)"
            /usr/bin/vmmap -summary "$pid" > "$OUT/vmmap_burst.txt" 2>&1 &
            vm=$!
            /usr/bin/heap -sortBySize "$pid" > "$OUT/heap_burst.txt" 2>&1 &
            hp=$!
            # WHAT is running: 5 s of stacks. The heap dump says which class is
            # eating memory; only a stack says which code path mints it, and
            # whether the process is spinning rather than idle.
            /usr/bin/sample "$pid" 5 -file "$OUT/sample_burst.txt" >/dev/null 2>&1 &
            sm=$!
            wait $vm $hp $sm 2>/dev/null
            # WHERE it was allocated, if the app was started with
            # MallocStackLogging=1 (see the launch command).
            if [ -n "${MallocStackLogging:-}" ]; then
                /usr/bin/malloc_history "$pid" -callTree -showContent \
                    > "$OUT/malloc_history_burst.txt" 2>&1 || true
            fi
            echo "CAPTURE done -> vmmap/heap/sample(+malloc_history)_burst.txt"
        fi

        if [ "$big" -gt "$KILL_MB" ]; then
            echo "!! ${big} MB exceeds ${KILL_MB} MB — killing pid $pid to protect the machine"
            kill -9 "$pid"
            break
        fi
    fi
    sleep 0.5
done

echo "SWMMVis exited. peak = ${peak} MB"
echo "timeline: $CSV"
