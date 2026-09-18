#!/usr/bin/env bash
#
# Capture user-manual figures by driving the built app with a manifest.
#
#   scripts/capture_manual_figures.sh [-m MODEL] [-o ONLY] [-l] [-- EXTRA_ENV...]
#
#     -m MODEL  .inp to open first (repo-relative or absolute). Figures that
#               need no project can omit it.
#     -o ONLY   comma-separated figure names; default is the whole manifest.
#     -l        live lane: run on cocoa with a real display instead of
#               offscreen. Required for anything containing map / 2D mesh /
#               2D results pixels — the offscreen platform reads those back
#               blank (tests/output/mesh_qsg_parity/qsg_full_blank.png is the
#               standing evidence).
#
# PNGs and run.json land in tests/output/manual_figures/ (git-ignored, but in
# the tree so they can be reviewed before publishing). Publish with:
#
#   python3 scripts/manual_figures.py flip --chapter 18
#
# Exits with the capture's failure count, so this gates in a script.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$REPO/build/SWMMVis.app/Contents/MacOS/SWMMVis"
MANIFEST="$REPO/docs/manual/figures.json"
OUT="$REPO/tests/output/manual_figures"

MODEL=""
ONLY=""
LIVE=0
while getopts ":m:o:l" opt; do
    case "$opt" in
        m) MODEL="$OPTARG" ;;
        o) ONLY="$OPTARG" ;;
        l) LIVE=1 ;;
        *) echo "usage: $0 [-m MODEL] [-o ONLY] [-l]" >&2; exit 2 ;;
    esac
done

[ -x "$APP" ] || { echo "no app at $APP — cmake --build build --target SWMMVis" >&2; exit 2; }
[ -f "$MANIFEST" ] || { echo "no manifest at $MANIFEST" >&2; exit 2; }
mkdir -p "$OUT"

env=(SWMMVIS_CAPTURE_MANIFEST="$MANIFEST" QT_LOGGING_RULES="openswmm.figcap=true")

if [ "$LIVE" -eq 1 ]; then
    env+=(QT_QPA_PLATFORM=cocoa)
else
    # The deployed bundle ships only libqcocoa — macdeployqt bundles just the
    # platform it detects — so point Qt at the SDK's plugin dir for offscreen.
    QT_PLUGINS="${QT_PLUGINS:-$HOME/Qt/6.9.3/macos/plugins/platforms}"
    [ -f "$QT_PLUGINS/libqoffscreen.dylib" ] || {
        echo "no libqoffscreen.dylib under $QT_PLUGINS — set QT_PLUGINS" >&2
        exit 2
    }
    env+=(QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORM_PLUGIN_PATH="$QT_PLUGINS")
fi

if [ -n "$MODEL" ]; then
    case "$MODEL" in
        /*) abs="$MODEL" ;;
        *)  abs="$REPO/$MODEL" ;;
    esac
    [ -f "$abs" ] || { echo "no model at $abs" >&2; exit 2; }
    env+=(SWMMVIS_OPEN_ON_STARTUP="$abs")
fi

[ -n "$ONLY" ] && env+=(SWMMVIS_CAPTURE_ONLY="$ONLY")

log="$OUT/_capture.log"
# The engine quits itself when the manifest is done; the timeout is only a
# backstop for a row that wedges the app before its watchdog can fire.
set +e
timeout -s TERM 900 env "${env[@]}" "$APP" > "$log" 2>&1
status=$?
set -e

grep -a "figcap" "$log" || true
[ "$status" -eq 124 ] && echo "TIMED OUT — see $log" >&2
echo "report: $OUT/run.json"
exit "$status"
