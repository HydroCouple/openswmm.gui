#!/usr/bin/env bash
# Compatibility entry point; see --help for app, manifest and batch options.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec python3 "$REPO/scripts/capture_manual_figures.py" "$@"
