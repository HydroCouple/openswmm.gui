# Chart / time-series number-format controls — build & test handoff

**Branch:** `swmm6_gui` &nbsp;•&nbsp; **Repo:** `openswmm.gui`

## What changed (scope of this commit)

Configurable **decimals / significant figures** plus an optional **custom printf
format** for the numbers on chart, time-series, profile, cross-section, curve,
pattern, and hydrograph axes (and series point labels).

1. **`plot::NumberFormat` (single source of truth)** gained an optional
   `QString custom` printf override. When `custom` holds exactly one valid
   floating-point conversion (`eEfFgGaA`, no `*`, no extra conversions) it is
   used verbatim by `printfSpec()` / `format()`; otherwise it falls back to
   `mode` + `count`. Validation is `NumberFormat::hasValidCustom()`.
2. **Precision is now a plain spin box, not a dropdown.** The `DecimalPlaces`
   `Q_ENUM` was removed from `ChartProperties`, `ProfilePlotOptions`,
   `MeshProfilePlotOptions`; `xLabelPrecision`/`yLabelPrecision` are now `int`
   `Q_PROPERTY`s (clamped 0–10). Each also gained `xLabelFormat`/`yLabelFormat`
   (`QString`) custom-format properties. `SeriesStyle` gained `pointLabelFormat`
   (JSON-persisted).
3. **Per-chart "Chart Properties…" in the editors.** New reusable
   `ui::ChartAxisFormatController` (holds persistent X/Y `NumberFormat`, applies
   to a chart's `QValueAxis`, and opens the shared `ChartPropertiesDialog`).
   Wired into: scatter plot, pattern editor, curve editor, time-series editor,
   hydrograph group editor (replaces the old hardcoded `"%g"`). The
   transect/cross-section editor instead got the same axis-format `Q_PROPERTY`s
   added directly to `TransectChartView`, so they appear in its existing
   right-click **Chart properties…** dialog.

## Prerequisites

- Qt 6.11.x (the local preset points at `~/Qt/6.11.1/macos`)
- vcpkg toolchain (`VCPKG_ROOT`)
- CMake ≥ 3.21, Ninja

`CMakeUserPresets.json` already defines a machine-local preset `Darwin-local`
(inherits `Darwin-debug`) with `QT_ROOT_DIR` and `VCPKG_ROOT` set. Adjust those
paths if the machine differs.

## Configure + build

```bash
cd <repo>/openswmm.gui

# Configure (use the preset matching the platform; Darwin-local on this Mac).
cmake --preset Darwin-local

# Build everything (app + test targets).
cmake --build --preset Darwin-local
```

If not using the local preset, configure explicitly:

```bash
cmake -S . -B build/darwin-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build/darwin-debug
```

## Automated tests

Two targeted suites cover the non-GUI logic that this change touches:

```bash
# From the configured build directory (e.g. build/darwin-debug):
ctest --test-dir build/darwin-debug -R "test_numberformat|test_seriesstyleobject" --output-on-failure

# Or run the binaries directly:
./build/darwin-debug/tests/unit/test_numberformat
./build/darwin-debug/tests/gui/test_seriesstyleobject
```

New assertions added:
- `test_numberformat.cpp`: `CustomOverridesModeAndCount`,
  `InvalidCustomFallsBackToModeAndCount` (valid specs incl. `"%.1f m"`,
  `"%.1f%%"`; rejects `"%s"`, `"%d"`, `"%.*f"`, multi-conversion, plain text).
- `test_seriesstyleobject.cpp`: `pointLabelFormat` is included in the
  `SeriesStyle` JSON round-trip.

Full regression run (optional, slower):

```bash
ctest --test-dir build/darwin-debug --output-on-failure
```

## Manual smoke test (GUI)

The per-editor wiring is UI-level and not unit-tested. For each editor below,
right-click the chart → **Chart Properties…** (transect: **Chart properties…**),
then confirm:

- **Precision** shows a spin box you can type a number into (0–10).
- **Number format** is a Decimals / Significant figures dropdown.
- **Custom format** is a text field; e.g. `%.2f` or `%.1f m` overrides the axis
  labels, and an invalid string (`%q`, `%.*f`) silently falls back.

Editors to check: scatter plot, curve editor, pattern editor, time-series
editor, hydrograph group editor, transect/cross-section editor, profile plot,
2-D mesh profile plot, comparison plot.

Also confirm precision survives a replot (edit points / change selection) and,
for the scatter plot, survives a full chart rebuild (change X/Y variables).

## Notes / known limitations

- The custom-format field is **per-chart / per-series**, seeded from the global
  `PreferencesManager` defaults. There is no global *custom-format* default in
  Preferences (the global precision spin box already exists). Easy to add later.
- The auto-generated precision spin box may visually allow values > 10 before
  the setter clamps; add a delegate if a hard UI cap is wanted.
- Could not be compiled in the authoring environment (no Qt toolchain there);
  the printf-validator + `asprintf` logic was verified standalone. Please run
  the build + ctest steps above to confirm on a Qt machine.
