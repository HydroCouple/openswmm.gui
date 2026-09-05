# Attribute Table dynamics columns — output-backed values

Validation of `SWMMResultsLayer::linkStatsFor` / `subcatchStatsFor`, the
aggregators behind the Attribute Table's post-run "dynamics" columns.

## Why the change

The dynamics columns (Max Depth (Sim.), Max Flow (Sim.), Total Runoff Volume,
…) read the **editing** engine's statistics via `swmm_node_get_stat_*` and
friends. Nothing ever populates those: `SimulationRunner` runs on its own
throw-away `SWMM_Engine` (or an out-of-process worker), so the editing engine
never reaches the `ENDED` state the stat getters require. Every dynamics cell
read back **0**.

They now read the **active 1D results layer** — the run selected in the
Analysis toolbar's "Results (1D)" combo — and re-read when that selection
changes.

## Precision caveat

The `.out` file is written at REPORT-step resolution, so maxima are the
largest *reported* value and volume / duration integrals use the report step
as `dt`. Expect small disagreement with the `.rpt` summary tables when
`REPORT_STEP` is much coarser than `ROUTING_STEP`. This is the same caveat
the engine's own `swmm_output_get_node_stat_*` aggregators carry (gap QA-01).

## Reproduce

```sh
# from the repo root
SWMMVis.app/Contents/MacOS/openswmm \
    tests/manual/attribute_table_dynamics/site_drainage_model.inp \
    tests/manual/attribute_table_dynamics/site_drainage_model.rpt \
    tests/manual/attribute_table_dynamics/site_drainage_model.out

ENG=../openswmm.engine/install/Darwin
clang++ -std=c++20 -I$ENG/include \
    tests/manual/attribute_table_dynamics/verify_dynamics.cpp \
    -L$ENG/lib -lopenswmm.engine -Wl,-rpath,$(cd $ENG/lib && pwd) \
    -o tests/manual/attribute_table_dynamics/verify_dynamics

tests/manual/attribute_table_dynamics/verify_dynamics \
    tests/manual/attribute_table_dynamics/site_drainage_model.inp \
    tests/manual/attribute_table_dynamics/site_drainage_model.out
```

`verify_dynamics.cpp` replicates the layer's arithmetic exactly (same Qcf /
Ucf tables, same series, same loop), so its output *is* what the Attribute
Table shows. Full run captured in `VERIFY_OUTPUT.txt`.

## Results — model `site_drainage_model.inp` (360 periods, CFS, 300 s report step)

### Links vs. `Link Flow Summary`

| Link | maxFlow | .rpt | maxVeloc | .rpt | maxFilling | .rpt Max/Full Depth |
|------|--------:|-----:|---------:|-----:|-----------:|--------------------:|
| C1   | 15.34 | 15.47 |  1.65 |  1.71 | 0.32 | 0.32 |
| C2   | 13.45 | 14.21 |  2.74 |  2.81 | 0.63 | 0.64 |
| C3   | 11.19 | 11.29 |  8.71 |  8.81 | 0.36 | 0.36 |
| C4   | 10.65 | 10.89 |  1.16 |  1.16 | 0.32 | 0.32 |
| C5   | 20.60 | 21.81 |  1.47 |  1.50 | 0.42 | 0.43 |
| C6   | 22.19 | 22.36 |  2.00 |  2.09 | 0.36 | 0.36 |
| C7   | 37.86 | 40.20 | 11.97 | 12.24 | 0.36 | 0.37 |
| C8   | 37.24 | 39.71 |  2.31 |  2.34 | 0.48 | 0.50 |
| C9   | 35.93 | 38.32 |  1.62 |  1.67 | 0.56 | 0.57 |
| C10  | 53.01 | 54.87 |  2.03 |  2.03 | 0.62 | 0.63 |
| C11  | 71.15 | 73.87 | 10.14 | 10.26 | 0.42 | 0.43 |

Every value sits at or just below the report figure — the signature of a
peak that falls between two report times. No conduit surcharged in this run
and the computed surcharge durations are all 0.00 h, matching the report's
"No conduits were surcharged."

### Subcatchments vs. `Subcatchment Runoff Summary`

| Sub | Precip (in) | .rpt | Runoff (10⁶ gal) | .rpt | Peak Runoff (CFS) | .rpt |
|-----|------------:|-----:|-----------------:|-----:|------------------:|-----:|
| S1  | 2.83 | 2.83 | 0.237 | 0.24 | 15.90 | 15.90 |
| S2  | 2.83 | 2.83 | 0.263 | 0.26 | 17.70 | 17.70 |
| S3  | 2.83 | 2.83 | 0.159 | 0.16 | 11.33 | 11.33 |
| S4  | 2.83 | 2.83 | 0.328 | 0.33 | 22.78 | 22.78 |
| S5  | 2.83 | 2.83 | 0.332 | 0.33 | 21.51 | 21.51 |
| S6  | 2.83 | 2.83 | 0.145 | 0.14 | 9.05  | 9.05  |
| S7  | 2.83 | 2.83 | 0.047 | 0.05 | 4.23  | 4.54  |

Precipitation depth is exact for all seven; runoff volume agrees to the
report's 2-decimal precision; six of seven peak runoffs are exact. S7's peak
is 6.8 % low — its hydrograph peaks between report times, the same
resolution effect as the conduits.

## Bug found and fixed during validation

The first implementation derived max-filling from `SWMM_OUT_LINK_CAPACITY`,
on the assumption that it was the depth ratio. It is not — `link.c:700`
defines it as `xsect_getAofY(y) / aFull`, an **area** ratio, whereas the
engine's `stat_max_filling` is `depth / y_full` (`SWMMEngine.cpp:3209`). The
two differ by roughly a factor of two at part-full flow: C1 came out 0.16
against the report's 0.32, and C2 0.39 against 0.64.

Fixed by reading `SWMM_OUT_LINK_DEPTH` and dividing by the `y_full` from the
model's resolved cross-section (`swmm_link_create_xsect` +
`swmm_xsect_full_properties`), which is correct for IRREGULAR and CUSTOM
shapes too — their `y_full` is not simply `geom1`. The table above is the
post-fix run.

Max flow and max velocity were also signed initially; both engine statistics
accumulate `|q|`, so they now take magnitudes.

## Pumps — model `user3_base.inp` (5 pumps, CMS, 6 h)

The three pump statistics were initially written off as unrecoverable, on the
belief that the `.out` file records no pump state. That was wrong. The
engine's on/off test is `setting > 0 && flow > 0` (`SWMMEngine.cpp:3280`), and
**both** operands survive into the output: flow directly, and `setting` as
`SWMM_OUT_LINK_CAPACITY` — for a non-conduit that variable is the pump speed /
regulator opening, not a fill ratio (`link.c:703`). Reconstructing from those
reproduces the engine's own accumulation loop step for step.

### At the model's own REPORT_STEP of 60 s (ROUTING_STEP 0.5 s)

| Pump | Cycles | .rpt | % Utilized | .rpt | Volume (m³) | .rpt (10⁶ L) |
|------|-------:|-----:|-----------:|-----:|------------:|-------------:|
| PUMP1 | 1 | 1 | 100.00 | 100.00 | 30943 | 30.940 |
| PUMP2 | **2** | **3** | 99.17 | 98.90 | 51642 | 51.636 |
| PUMP3 | 1 | 1 | 100.00 | 100.00 | 66072 | 66.051 |
| PUMP4 | 1 | 1 |  95.83 |  95.80 | 15392 | 15.391 |
| PUMP5 | 1 | 1 |  95.83 |  95.83 |  6334 |  6.333 |

On-time is within 0.3 % and volume within 0.04 % on every pump. PUMP2's cycle
count is short by one: it switches off and back on entirely inside a single
60 s report step (120 routing steps), leaving no trace in the output.

### Same model at REPORT_STEP 1 s

| Pump | Cycles | .rpt | % Utilized | .rpt | Volume (m³) | .rpt (10⁶ L) |
|------|-------:|-----:|-----------:|-----:|------------:|-------------:|
| PUMP1 | 1 | 1 | 100.00 | 100.00 | 30943 | 30.940 |
| PUMP2 | **3** | **3** |  98.90 |  98.90 | 51641 | 51.636 |
| PUMP3 | 1 | 1 | 100.00 | 100.00 | 66059 | 66.051 |
| PUMP4 | 1 | 1 |  95.80 |  95.80 | 15392 | 15.391 |
| PUMP5 | 1 | 1 |  95.83 |  95.83 |  6334 |  6.333 |

Every cycle count and every utilisation figure now matches the report exactly,
which confirms the discrepancy above is purely report-step resolution and not
a defect in the reconstruction.

### Consequence for the UI

Cycle count is the one statistic where the report-step caveat bites hard: it
is a **lower bound**, not an approximation with a small error bar. A pump
cycling every 30 s under a 15-minute report step could read 1 where the truth
is 30. On-time and volume degrade gracefully over the same run because a brief
outage barely moves an integral.

The Pump Cycles column therefore carries a header tooltip saying so, and
pointing at the remedy (shorten `REPORT_STEP`). No other dynamics column
needs one.
