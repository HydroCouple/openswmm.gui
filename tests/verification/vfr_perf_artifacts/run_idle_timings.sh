#!/bin/zsh
# Idle-machine timings for the 2D VFR performance recovery
# (workplans/2D_VFR_PERF_RECOVERY_PLAN_2026-10-02.md, M1-M3).
#
# Timings taken while other jobs run are not comparable, so every section
# waits up to 15 minutes for the 1-minute load average to fall below 3 and
# refuses to start if it does not.
#
#   run_idle_timings.sh m1    header-level fit benchmark, before vs after
#   run_idle_timings.sh m2    layer-level benchmark + worker latency
#   run_idle_timings.sh m3    headless engine comparison (about 80 minutes)
#   run_idle_timings.sh all
#
# Outputs: this folder (before/, after/, engine_ab/) and
#          openswmm.engine/tools/bench_2d/out/vfr_ab_20261002/.
# Nothing is written into the user's model folder.
set -u
G=/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui
E=/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine
ART=$G/tests/verification/vfr_perf_artifacts
FIX=$G/tests/verification/vfr_slope_steps/bellinge-partial-2026-10-01.h5
OUT=$E/tools/bench_2d/out/vfr_ab_20261002
TESTDIR=$G/build/animation-verification/gui-testing-2026-10-02
MAXLOAD=${MAXLOAD:-3}

# Wait up to SETTLE_MIN minutes for the load to fall below the limit. A run
# of this script raises the load itself, so runs are spaced by this wait.
settle() {
    local deadline=$(( $(date +%s) + ${SETTLE_MIN:-15} * 60 ))
    while [ $(date +%s) -lt $deadline ]; do
        awk -v l="$(sysctl -n vm.loadavg | awk '{print $2}')" -v m="$MAXLOAD" 'BEGIN{exit !(l<m)}' && return 0
        sleep 10
    done
    return 0   # the gate below decides
}

gate() {
    settle
    local load=$(sysctl -n vm.loadavg | awk '{print $2}')
    if ! awk -v l="$load" -v m="$MAXLOAD" 'BEGIN{exit !(l<m)}'; then
        echo "REFUSED ($1): 1-minute load is $load, limit $MAXLOAD. Busy processes:"
        pgrep -fl 'ninja|clang|parallel_verify|run_corpus|openswmm ' | cut -c1-110 | head -n 6
        return 1
    fi
    echo "# $(date '+%Y-%m-%d %H:%M:%S') load=$(sysctl -n vm.loadavg) $1"
}

m1() {
    gate m1 || return 1
    for arm in before after before after before after; do   # interleaved
        gate "m1 $arm" >> $ART/$arm/bellinge-perf.log || return 1
        $ART/$arm/bellinge-perf $FIX 5 > $ART/$arm/bellinge-perf.csv.tmp 2>> $ART/$arm/bellinge-perf.log || return 1
        # keep the fastest of the three runs per arm (sum of fit_ms, column 17)
        local new=$(awk -F, 'NR>1{s+=$17}END{print s}' $ART/$arm/bellinge-perf.csv.tmp)
        local old=$( [ -f $ART/$arm/bellinge-perf.csv ] && awk -F, 'NR>1{s+=$17}END{print s}' $ART/$arm/bellinge-perf.csv || echo 1e30 )
        if awk -v n="$new" -v o="$old" 'BEGIN{exit !(n<o)}'; then mv $ART/$arm/bellinge-perf.csv.tmp $ART/$arm/bellinge-perf.csv
        else rm $ART/$arm/bellinge-perf.csv.tmp; fi
    done
    echo "deterministic columns identical: $(diff <(cut -d, -f1-15 $ART/before/bellinge-perf.csv) <(cut -d, -f1-15 $ART/after/bellinge-perf.csv) >/dev/null && echo yes || echo NO)"
    paste -d, <(cut -d, -f1,17 $ART/before/bellinge-perf.csv) <(cut -d, -f17 $ART/after/bellinge-perf.csv) | column -s, -t
}

m2() {
    gate m2 || return 1
    export QT_QPA_PLATFORM=offscreen SWMMVIS_GUI_TEST_DATA=$G/tests/gui/data QTEST_FUNCTION_TIMEOUT=900000 VFR_TEST_BELLINGE=$FIX
    VFR_PERF_OUT=$ART/before $TESTDIR/test_2dresults_vizfixes.before bellingePerfBaseline 2>&1 | tail -n 4
    gate "m2 after" || return 1
    VFR_PERF_OUT=$ART/after $TESTDIR/test_2dresults_vizfixes bellingePerfBaseline bellingeWorkerEqualityAndLatency 2>&1 | tail -n 5
    { echo "git=$(git -C $G rev-parse --short HEAD) $(date '+%Y-%m-%d %H:%M:%S') load=$(sysctl -n vm.loadavg)"; } > $ART/after/layer-perf-summary.txt
}

m3() {
    local run=$E/tools/bench_2d/run_one.py now=$E/install/flow-trace/bin/openswmm
    local t
    for t in warmup now_t8_free now_t8_reserved3 now_t7_free now_t7_reserved3 now_t8_free_r2; do
        gate "m3 $t" || return 1
        if [[ $t == *reserved3 ]]; then
            python3 $run $t $OUT/$t.inp --cli $now --outdir $OUT --env OPENSWMM_HOST_RESERVED_THREADS=3
        else
            python3 $run $t $OUT/$t.inp --cli $now --outdir $OUT
        fi
    done
    echo "same engine, wait policy only: results must be identical"
    h5diff $OUT/now_t8_free.2d.h5 $OUT/now_t8_reserved3.2d.h5 /Mesh2_face_depth && echo "Mesh2_face_depth identical"
    h5diff $OUT/now_t8_free.2d.h5 $OUT/now_t8_free_r2.2d.h5 /Mesh2_face_depth && echo "repeat identical"
    mkdir -p $ART/engine_ab && cp $OUT/results.csv $ART/engine_ab/results.csv
    column -s, -t $OUT/results.csv | cut -c1-200
}

case "${1:-}" in
    m1) m1 ;; m2) m2 ;; m3) m3 ;; all) m1; m2; m3 ;;
    *) sed -n '2,16p' "$0" ;;
esac
