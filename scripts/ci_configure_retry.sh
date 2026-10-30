#!/usr/bin/env bash
# Resume dependency installation after transient vcpkg download DNS failures.
set -euo pipefail

if (( $# == 0 )); then
  echo "Usage: bash ci_configure_retry.sh cmake [configure arguments...]" >&2
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
  # Check only this attempt's output; genuine CMake errors must stay fatal.
  if (( attempt == 3 )) || ! grep -Fq 'curl: (6) Could not resolve host:' "$log"; then
    exit "$status"
  fi

  delay=$((20 * attempt))
  echo "::warning::Configure attempt $attempt failed to resolve a download host; retrying in ${delay}s."
  sleep "$delay"
done
