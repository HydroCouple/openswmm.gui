# Standalone checks — external-column-file parsing core

Qt-free verification of `include/ui/util/externalcolumnfilecore.h`, the pure
parsing core behind the shared CSV/TSV/PCSWMM-`.tsf` column util
(`ui/util/externalcolumnfile.{h,cpp}`). Written for environments without a Qt
toolchain (see the GUI handoff's Build & verification section in
`workplans/HANDOFF_MULTICOLUMN_SERIES_GUI_2026-08-17.md`); the Qt-level
`readHeaders` / `readColumn` wrapper is covered by
`tests/unit/test_externalcolumnfile.cpp` and the full app build.

What it exercises (all rules mirror the engine's
`src/engine/input/MultiColumnSeriesFile.cpp` / `CatchmentHandler.cpp`):

- quote-aware cell splitting + whitespace/quote trimming
- BOM / trailing-`\r` normalization, `;` / `#` comment lines
- case-insensitive `IDs:` TSF header detection
- delimiter sniffing: tab-vs-comma (ties → tab, the engine's rule), the GUI's
  `;` extension, and **quoted regions excluded from the counts** (review B-8)
- series datetimes: ISO-8601, single-digit US `M/D/YYYY`, 12-hour AM/PM
  (12 AM → midnight, 12 PM → noon)
- the unified `path:col` token split/compose used by both engine readers
  (review B-2): last colon, Windows drive letter exempt, and a suffix
  containing a path separator is part of the path — incl. a colon inside a
  directory name, and compose→split round-trips for every shape
- the fabricated `col_N` display-name convention (never persisted)
- case-insensitive column-name resolution
- the 3-row TSF header flow over the bundled `sample.tsf` fixture

Build & run (from this directory):

```sh
c++ -std=c++17 -Wall -Wextra -o standalone_externalcolumn main.cpp
./standalone_externalcolumn
```

Exit code 0 and `ALL CHECKS PASSED` mean success; any failure prints its
file:line and the failed condition.
