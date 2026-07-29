#!/usr/bin/env bash
# Build + run the Qt-free MeshGenerator repro under ASan/UBSan.
# Usage: ./run.sh            (sanitized build)
#        ./run.sh plain      (no sanitizers)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/build"
mkdir -p "$OUT"

SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
[ "${1:-}" = "plain" ] && SAN="-g"

set -x
gcc -c "$ROOT/vendor/triangle/triangle.c" -o "$OUT/triangle.o" \
    -I"$ROOT/vendor/triangle" \
    -DTRILIBRARY -DANSI_DECLARATORS -DNO_TIMER \
    -O1 $SAN -w || exit 1

g++ -std=c++20 -c "$HERE/repro.cpp" -o "$OUT/repro.o" \
    -I"$ROOT/vendor/triangle" \
    -O1 $SAN -Wall || exit 1

g++ "$OUT/repro.o" "$OUT/triangle.o" -o "$OUT/repro" $SAN || exit 1
set +x

ASAN_OPTIONS=detect_leaks=1:abort_on_error=0:print_stacktrace=1 \
UBSAN_OPTIONS=print_stacktrace=1 \
"$OUT/repro"
echo "exit=$?"
