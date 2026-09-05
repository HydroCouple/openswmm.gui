# Legacy FILE timeseries — repro + verification (2026-09-03/04)

**Bug:** a model whose `[TIMESERIES]` is specified by filepath
(`TS FILE "path"`) would not load in the GUI when the file used any legacy
row form other than `M/D/Y H:MM value` — elapsed `H:MM value`, decimal-hour
times, dash/month-name dates. The v6 engine (which the GUI uses to open every
model) parsed zero rows and failed the open with ERROR 363, while the same
model ran fine under the legacy engine.

**Fix:** engine commit `5fdb655a` — `load_external_timeseries_files` in
`src/engine/input/PostParseResolver.cpp` now mirrors legacy
`table_parseFileLine()` exactly (elapsed rows anchored at
StartDate + StartTime per `input.c:176`). Regression tests in
`tests/unit/engine/test_timeseries_file_roundtrip.cpp` with fixtures under
`tests/unit/engine/data/rain_series/legacy_*.dat`.

## Files

- `model_file_ts.inp` + `inflow_ts.dat` — dated `M/D/Y H:MM value` file (always worked)
- `model_hours.inp` + `inflow_hours.dat` — elapsed `H:MM value` rows (the failing case)
- `model_dechours.inp` + `inflow_dechours.dat` — decimal-hour rows
- `model_mixed.inp` + `inflow_mixed.dat` — dash dates, `Jan-`-style month name,
  date-less carryover row, decimal-hour time on a dated row
- `model_hours_offset.inp` — elapsed file with nonzero START_TIME (anchor check)
- `model_noquote.inp` — unquoted FILE token
- `roundtrip_driver.c` — mimics the GUI's open → save path
  (`swmm_engine_open` + `swmm_model_write`, the same calls
  `SWMMVisProjectWindow::saveAs` makes)

## How to re-verify

```sh
ENG=../../../../openswmm.engine/install/Darwin
cc -o roundtrip_driver roundtrip_driver.c -I$ENG/include -L$ENG/lib \
   -lopenswmm.engine -Wl,-rpath,$ENG/lib
./roundtrip_driver model_hours.inp rt.inp          # GUI open+save simulation
$ENG/bin/openswmm-legacy-worker rt.inp rt.rpt rt.out 1000   # legacy run
grep "External Inflow" rt.rpt                      # expect 0.909 ac-ft
```

Every variant produces **External Inflow 0.909 ac-ft** identically through:
legacy worker on the original file, v6 engine on the original file, and
legacy worker on the GUI-round-tripped file (FILE reference preserved
verbatim in `[TIMESERIES]`).
