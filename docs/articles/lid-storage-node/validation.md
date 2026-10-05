# Validation and reproducibility — distributed LID examples

Validated locally on October 4, 2026 against the native engine library below.
The engine and GUI checkouts contain other local development changes. This
library hash identifies the actual binary used; a repository HEAD alone would
not identify the uncommitted pollutant corrections. Website files are prepared
locally, without pushing or publishing.

Library: `libopenswmm.engine.6.0.0.dylib`

SHA-256: `4a30e4e9c99443fca7bf99b649bc0f1c853eaaaa07a744275187e5d5b0180f10`

## Reproduce

From the GUI checkout:

```text
python3 docs/articles/lid-storage-node/figures/generate_examples.py
python3 docs/articles/lid-storage-node/figures/run_examples.py --library /absolute/path/to/current/libopenswmm.engine.dylib --steps 0.5 0.25 0.1
python3 docs/articles/lid-storage-node/figures/run_examples.py --library /absolute/path/to/current/libopenswmm.engine.dylib --steps 0.025 --append
python3 docs/articles/lid-storage-node/figures/run_examples.py --library /absolute/path/to/current/libopenswmm.engine.dylib --cases 01_passive_free 02_passive_backwater --steps 0.05 --append
python3 docs/articles/lid-storage-node/figures/make_figures.py
python3 docs/articles/lid-storage-node/figures/make_formulation_figures.py
```

The runner uses the native C API through Python's standard-library ctypes.
It records 30-second snapshots, report files, continuity queries and a binary
hash. It requires all runs to have no engine warnings and water and pollutant
continuity errors below 0.5%. It retains results under `results/` and deletes
expanded temporary decks and binary result files after each successful run.
The figures use the 0.025-second runs. The runner also records retained surface moisture to distinguish perched ponding from mobile water-table head in the network diagram. SVG sources, PNG posters and three 64-frame
GIFs are generated with rsvg-convert and ImageMagick. Moving arrowheads indicate
flow direction only. The pollutant-fate bars show final 24-hour engine totals;
they do not animate inferred intermediate inventories.

The six decks were run at fixed routing steps of 0.5, 0.25, 0.1 and 0.025 seconds, with additional 0.05-second runs for both passive cases: 26 distinct case/step combinations. All passed the 0.5% water/quality continuity criterion without engine warnings; pollutant errors were below 0.001%. Figures and tables use the 0.025-second runs. Continuity alone did not establish performance convergence: the passive 0.1-second runs shifted the sampled half-export time by up to 9.5 minutes and the reacted fraction by up to 0.7 percentage points relative to the finer results. The passive 0.05- and 0.025-second runs agree in half-export time at the 30-second sampling resolution and reacted fraction at report precision; their receiving-outlet peaks differ by less than 0.01%. Cumulative engine budgets are used for overflow and flooding. The seven engine regression suites passed 141 tests, including three interface-roundoff regressions and eight full-chain mass-conservation variants.

| First-storm strategy | V_BR peak (cfs) | Tracer 50% export (h) | Reacted by 24 h |
|---|---|---|---|
| Passive / free outlet | 0.0369 | 5.08 | 33.9% |
| Passive / backwater | 0.0389 | 10.33 | 43.9% |
| Timed hold | 0.0345 | 10.04 | 42.6% |
| Hold + head guard | 0.0345 | 10.04 | 42.6% |

## Second-event and failure budgets

The table uses cumulative engine totals, rather than summing aliased overflow
snapshots. Pollutant totals are rounded to 0.001 lb by the report. Mass-fate
bar lengths can therefore differ slightly from 100% after rounding.

| Case | Incoming reactive mass (lb) | Exported (lb) | Flood loss (lb) | Reacted (lb) | Stored at 24 h (lb) | Flood water (ft³) |
|---|---|---|---|---|---|---|
| Passive / free outlet | 1.347 | 0.850 | 0.000 | 0.456 | 0.042 | 0.00 |
| Passive / backwater | 1.347 | 0.712 | 0.000 | 0.592 | 0.043 | 0.00 |
| Timed hold | 1.347 | 0.733 | 0.000 | 0.574 | 0.040 | 0.00 |
| Hold + head guard | 1.347 | 0.733 | 0.000 | 0.574 | 0.040 | 0.00 |
| Second storm / guard | 3.369 | 2.292 | 0.000 | 0.987 | 0.089 | 0.00 |
| Valves stuck closed | 3.369 | 2.503 | 0.000 | 0.737 | 0.128 | 0.00 |

Seepage, evaporation and initial pollutant mass are zero in these decks.
The BOTTOM boundary is closed. External water totals can include clean
backflow from the receiver; pulse volume alone is not the full boundary budget.
For field work, account for all transfers over a common assessment horizon.
These synthetic models do not simulate receiving-water ecology or groundwater
pollutant fate. Tracer export time is not mean hydraulic residence time.

## Overflow convergence check

Four entries from the complete run set compare cumulative emergency-weir flow statistics, avoiding aliasing of 30-second snapshots. They use the same engine binary. The weirs have no reverse flow. Bypass totals differ by less than 0.1% between the 0.1 and 0.025-second steps; flooding is zero in both cases. See `overflow_totals.json`.

| Case | Step (s) | Emergency-weir volume (ft³) | Flooding (ft³) |
|---|---|---|---|
| 05_repeat_storm | 0.10 | 1208.02 | 0.00 |
| 05_repeat_storm | 0.03 | 1207.95 | 0.00 |
| 06_stuck_closed | 0.10 | 2006.56 | 0.00 |
| 06_stuck_closed | 0.03 | 2006.56 | 0.00 |

## Pollutant correction

- Boundary water carrying LAST concentration into a LID is now booked as an
  external pollutant source. ZERO boundary mode explicitly supplies clean water.
- Zero-volume links connected to LIDs use consistently solved current mobile
  mixtures for donor and recipient mass. Provisional iterations restore mass
  inventories and counters; outlet treatment is booked only once.
- The LID dry-node path retains the concentration needed to export freshly
  percolated water even when final mobile volume is below the legacy dry cutoff.
- Weir and rating-outlet ports use their physical subtype crest, including
  layer-anchor synchronization, rather than an unrelated generic offset.
- Crest conversion adds the difference between node datums to the authored
  offset, avoiding cancellation at a nonzero node invert. Interface selection
  tolerates only roundoff-sized differences (32 machine epsilons times a local
  elevation scale), and is shared by hydraulic and pollutant ports. This fixes
  a surface weir seeing media head and treatment instead of perched ponding.
  Three new regressions cover ponded withdrawal, one-ULP perturbations versus
  physical offsets, and mobile-outlet treatment at a layer interface.
- Floating-point cancellation of a fully captured external load is clamped
  only within 64 machine epsilons; it is not treated as a negative source.
- A failed coupled quality iteration warns explicitly. Passing these tests is
  not a universal accuracy guarantee for arbitrary networks or treatment rules.

The pre-correction diagnostic had about −21% conservative-tracer continuity
error in an aggregate/backwater train, and about +10.6% in a media-drainage
case. Those were separate diagnostic decks, not the final tutorial geometry.
They motivated the regression fixtures and are not used as before/after
performance comparisons in the article.

## Interface-roundoff reproduction

With a storage invert of 0.30 ft and a 2.00-ft local weir crest,
`(0.3 + 2.0) - 0.3` evaluates to `1.9999999999999998` in binary floating
point. A strict interval comparison therefore selected MEDIA below the
intended SURFACE boundary. In the regression, the underlying mobile water
table was 0.50 ft and surface ponding supplied a 2.20-ft local head. Before
the correction, the weir saw 0.50 ft and a trial withdrawal drew from media.
Afterward, it sees 2.20 ft and debits the surface cell. Physical offsets
1e-8 ft below an interface remain below it; only roundoff-sized differences
are treated as the interface. The rule is also used for outlet quality, so
hydraulic and pollutant routing cannot disagree on the donating layer.

## Engine regression results

| Suite | Tests passed |
|---|---|
| LID nodes | 28 |
| Quality routing | 21 |
| Treatment | 32 |
| Hotstart | 39 |
| Outfall backflow | 5 |
| LID water age | 6 |
| LID heat | 10 |
| Total | 141 |

The new chain regression covers MEDIA / AGGREGATE × low / high tailwater ×
ZERO / LAST boundary quality. It checks final balances for both constituents,
and conservative-tracer inventory during every routing step to 0.1% of input
mass. Separate focused cases cover sub-litre percolation drainage and weir
anchor changes. Existing ordinary-node quality paths remain on their prior
routing branch.

## Source scope and further reading

Current native implementation: engine `src/engine/hydrology/LidNode.cpp`,
`LidNodeTreatment.cpp`, `quality/QualityRouting.cpp`, and
`core/SWMMEngine.cpp`. Engine `plans/LID_StorageNode_Redesign.md` contains
superseded proposals as well as the implementation design; current Chapter 6
and the supplied decks describe the actual syntax.

Groundwater closing remarks are based on the local engine input reference:
`[2D_AQUIFER_OPTIONS]`, `[2D_AQUIFER]`, `[2D_AQUIFER_NODE]` and
`[2D_AQUIFER_LINKS]`. MESH mode represents one aquifer cell beneath each mesh
cell, with lateral flow and configurable surface/network exchanges. The
article proposes future investigations rather than claiming a validated
coupled LID–groundwater or groundwater treatment demonstration here.

Article research references are linked directly to EPA, the journal DOI and
the published ASCE paper and the PLOS ONE unsaturated-zone study. Their findings
motivate the questions. The network and fate animations use synthetic example
runs; the additional formulation figures are explicitly labeled schematics or
analytical illustrations, rather than additional engine results.
