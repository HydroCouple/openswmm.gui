#!/usr/bin/env bash
# Resume vcpkg bootstrap/configure after transient download failures.
set -euo pipefail

if (( $# == 0 )); then
  echo "Usage: bash ci_configure_retry.sh command [arguments...]" >&2
  exit 2
fi

log=$(mktemp "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/openswmm-configure.XXXXXX")
trap 'rm -f "$log"' EXIT

for attempt in 1 2 3; do
  set +e
  "$@" 2>&1 | tee "$log"
  status=${PIPESTATUS[0]}
  set -e

  if (( status == 0 )); then
    exit 0
  fi
  # Check only this attempt's output; other command failures must stay fatal.
  if (( attempt == 3 )) || ! grep -Eq 'curl: \((6|7|28|35|52|56)\)|curl: \(22\) The requested URL returned error: (408|429|500|502|503|504)([^0-9]|$)' "$log"; then
    exit "$status"
  fi

  delay=$((20 * attempt))
  echo "::warning::Dependency command attempt $attempt hit a transient download failure; retrying in ${delay}s."
  sleep "$delay"
done
