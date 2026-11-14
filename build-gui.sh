#!/bin/bash
set -u
cd "$(dirname "$0")" || exit 1
LOG=build/build-gui.log
: > "$LOG"
echo "=== $(date) ===" >> "$LOG"
cmake --build build -j 8 >> "$LOG" 2>&1
STATUS=$?
echo "BUILD_STATUS=$STATUS" | tee -a "$LOG"
exit "$STATUS"
