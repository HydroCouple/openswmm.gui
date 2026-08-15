#!/usr/bin/env bash
# UI redesign P4 — chrome-color lint.
#
# Fails when chrome code (src/ui, src/plot, src/map, src/simulation,
# include/, the main-window TUs) styles widgets with literal colors
# instead of the theme vocabulary (ui/theme/themehelpers.h, palette
# roles, ThemeColors tokens):
#   1. setStyleSheet(...) lines carrying a hex literal or `color: gray`
#   2. QPalette::setColor(...) lines carrying a QColor(...) literal
#   3. (D6) hex literals inside color-bearing QString/QStringLiteral text
#      — catches styles assigned to a variable first and rich-text
#      `color:#…` spans, both invisible to pattern 1
#   4. (D6) the three-argument setColor(group, role, QColor(...)) form
#
# Deliberate exceptions live in lint_chrome_colors_allowlist.txt (one
# `path:substring` fragment per line, # comments allowed) — each entry is
# visible, reviewed debt. Data symbology (src/render color ramps, series
# cycles, map layers) is out of scope by design: those colors encode data.
set -u
cd "$(dirname "$0")/.."

ALLOWLIST="scripts/lint_chrome_colors_allowlist.txt"

CHROME_PATHS=(src/ui src/plot src/map src/simulation include
              src/swmmvis.cpp src/swmmvisactions.cpp
              src/swmmvisapplication.cpp src/swmmvisprojectwindow.cpp
              src/swmmvissplashscreen.cpp)

matches=$(
    {
        grep -rnE 'setStyleSheet.*(#[0-9a-fA-F]{3,8}|color: *gray)' "${CHROME_PATHS[@]}" 2>/dev/null
        grep -rnE 'setColor\(QPalette::[A-Za-z]+, *QColor\(' "${CHROME_PATHS[@]}" 2>/dev/null
        grep -rnE '(QStringLiteral|QString)\("[^"]*color[^"]*#[0-9a-fA-F]{3,8}' "${CHROME_PATHS[@]}" 2>/dev/null
        grep -rnE 'setColor\(QPalette::[A-Za-z]+, *QPalette::[A-Za-z]+, *QColor\(' "${CHROME_PATHS[@]}" 2>/dev/null
    } | sort -u
)

violations=""
while IFS= read -r line; do
    [ -z "$line" ] && continue
    allowed=0
    while IFS= read -r pattern; do
        case "$pattern" in ''|'#'*) continue ;; esac
        case "$line" in *"$pattern"*) allowed=1; break ;; esac
    done < "$ALLOWLIST"
    [ "$allowed" -eq 0 ] && violations="${violations}${line}\n"
done <<< "$matches"

if [ -n "$violations" ]; then
    echo "chrome-color lint FAILED — use ui/theme/themehelpers.h or palette roles,"
    echo "or add a reviewed entry to $ALLOWLIST:"
    printf '%b' "$violations"
    exit 1
fi
echo "chrome-color lint OK"
