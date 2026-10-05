# Validation and reproducibility — distributed LID examples

Validated locally on October 5, 2026 with the legacy-hydrology adaptation.
The engine checkout includes other development work; this binary hash
identifies the actual library tested rather than claiming a clean HEAD build.
These results supersede the previous power-law node-kernel runs.

Library: `libopenswmm.engine.6.0.0.dylib`

SHA-256: `c3c397ec3245125727141c4852df80ca4bd9b609e45bf8c05bb25f27e6a9a1dd`

## Reproduce

From the GUI checkout, using the rebuilt native library:

```text
python3 docs/articles/lid-storage-node/figures/generate_examples.py
python3 docs/articles/lid-storage-node/figures/run_examples.py --library /absolute/path/to/current/libopenswmm.engine.dylib --steps 0.1 0.05 0.025
python3 docs/articles/lid-storage-node/figures/run_examples.py --library /absolute/path/to/current/libopenswmm.engine.dylib --cases 05_repeat_storm --steps 0.0125 0.00625 --append
python3 docs/articles/lid-storage-node/figures/finalize_text.py
python3 docs/articles/lid-storage-node/figures/make_figures.py
python3 docs/articles/lid-storage-node/figures/make_formulation_figures.py
python3 docs/articles/lid-storage-node/figures/stage_site.py
```

The runner uses the native C API through standard-library ctypes. CSVs have
30-second samples; report files and cumulative continuity/statistics queries
supply the budgets. All accepted runs have no engine warnings and absolute
water/pollutant continuity errors below 0.5%. Append rejects a different
binary hash. The finest available step is selected independently per case.
All six decks use CFS, Dynamic Wave and Legacy quality; bottom seepage,
evaporation and initial pollutant mass are zero. Reverse boundary water is
clean, so external water includes tailwater receipts but no imported mass.

The six decks were rerun with the revised hydrology at 0.1, 0.05 and 0.025-second routing steps, with 2 additional second-storm refinements: 20 distinct case/step combinations. All passed the 0.5% water/quality continuity criterion without engine warnings; the maximum reported error was below 0.001%. Figures use each case’s finest recorded step. Comparing each case’s two finest steps, sampled half-export times agree at the 30-second sampling resolution, reacted fractions differ by 0.074 percentage points, receiving-outlet peaks by 0.025% and cumulative emergency-weir volumes by 0.007%. Tracer timing is sampled at 30 seconds and reaction budgets are rounded by the engine report. These are measured sensitivity limits, not a universal convergence guarantee. Cumulative engine budgets are used for bypass and flooding. Nine engine regression suites passed 241 tests, including legacy infiltration/LID paths, gradual resaturation–recession, second-event entry and restart history.

| First-storm strategy | V_BR peak (cfs) | Tracer 50% export (h) | Reacted by 24 h |
|---|---|---|---|
| Passive / free outlet | 0.0368 | 4.83 | 34.4% |
| Passive / backwater | 0.0389 | 10.18 | 43.6% |
| Timed hold | 0.0345 | 10.05 | 42.7% |
| Hold + head guard | 0.0345 | 10.05 | 42.7% |

## Finest-step performance sensitivity

Every pair below uses the same library. Differences are absolute, with
percent differences referenced to the finer result. Good mass closure does
not prove stable performance. First-order reaction is assumed, not calibrated.

| Case | Coarser/finer steps (s) | Half-export difference (min) | Reaction difference (percentage points) | Receiving-peak difference (%) | Bypass difference (%) |
|---|---|---|---|---|---|
| 01_passive_free | 0.05 / 0.025 | 0.00 | 0.074 | 0.025 | 0.007 |
| 02_passive_backwater | 0.05 / 0.025 | 0.00 | 0.000 | 0.002 | 0.007 |
| 03_timed_hold | 0.05 / 0.025 | 0.00 | 0.000 | 0.000 | 0.000 |
| 04_head_guard | 0.05 / 0.025 | 0.00 | 0.000 | 0.000 | 0.000 |
| 05_repeat_storm | 0.0125 / 0.00625 | 0.00 | 0.000 | 0.000 | 0.000 |
| 06_stuck_closed | 0.05 / 0.025 | 0.00 | 0.000 | 0.000 | 0.000 |

## Mass and overflow budgets at each case’s finest step

Emergency-weir totals come from cumulative engine flow statistics rather
than summing aliased snapshots. The weirs have no reverse flow. Pollutant
reports round totals to 0.001 lb, so rounded fate bars may not sum to 100%.

| Case | Step (s) | Reactive input (lb) | Exported (lb) | Flood loss (lb) | Reacted (lb) | Stored at 24 h (lb) | Emergency bypass (ft³) | Flood water (ft³) |
|---|---|---|---|---|---|---|---|---|
| Passive / free outlet | 0.025 | 1.347 | 0.843 | 0.000 | 0.464 | 0.041 | 324.23 | 0.00 |
| Passive / backwater | 0.025 | 1.347 | 0.718 | 0.000 | 0.587 | 0.043 | 324.23 | 0.00 |
| Timed hold | 0.025 | 1.347 | 0.732 | 0.000 | 0.575 | 0.040 | 384.02 | 0.00 |
| Hold + head guard | 0.025 | 1.347 | 0.732 | 0.000 | 0.575 | 0.040 | 384.02 | 0.00 |
| Second storm / guard | 0.00625 | 3.369 | 2.270 | 0.000 | 1.007 | 0.091 | 1189.44 | 0.00 |
| Valves stuck closed | 0.025 | 3.369 | 2.503 | 0.000 | 0.737 | 0.128 | 2006.56 | 0.00 |

For field studies, include all destinations over a common horizon; retained
mass is not treated mass. Tracer half-export time is not mean hydraulic
residence time. These decks do not simulate groundwater fate or receiving
water ecology. Holding increases modeled reaction directly under the assumed
2/day first-order law (half-life 8.3 h).

## Resaturation example and animation

`models/lid_resaturation.inp` is the separate six-minute native regression
fixture: a 10 ft² facility, an open reversible orifice, rising/falling
receiving stage and surface inflow at minutes 3–4. The engine test
`LidNodes.RoutedReversalResaturationRecessionAndSecondEventConserve` writes
one-second samples of head, stores, signed flow and infiltration history.
Copy its `resaturation_second_event.csv` to `results/resaturation_cycle.csv`
and record hashes in `results/resaturation_cycle.json` before rendering.
The JSON identifies the library, fixture and samples used in the animation.
The fixture is also shipped in the GUI tutorial models. It closes water
within 0.005 ft³ and conservative-tracer mass at each step within 0.1% of
incoming mass, with no warnings. Internal GA history is shown from the native
test export; it is not inferred from GUI depth outputs.

The four 64-frame looping GIFs have static PNG alternatives. Network/fate
animations use sampled 24-hour native runs. The resaturation GIF uses the
six-minute fixture; its pale lines show the rest of the sampled trajectory.
The orifice animation is analytical. Arrow motion shows flow direction,
not particle tracking. Formulation diagrams are explanatory SVGs rendered
with rsvg-convert; ImageMagick assembles GIFs. No image-generation model
produces the simulated data.

## Hydrology implementation checks

- MEDIA uses `Ks * exp(-slope * (porosity - theta))` above field capacity;
  the existing conductivity-slope parameter no longer acts as a power exponent.
- SURFACE → first MEDIA reuses modified Green–Ampt. Only accepted infiltration
  increments F/Fu; zero Ks or zero climate multiplier is impermeable.
- Backwater/accepted media-port wetting changes the finite-zone deficit/wetness
  without becoming surface infiltration. Fully submerged media clears the old
  front. Gradual recession tracks physical wetness until accepted surface
  entry starts the subsequent front. Dry recovery respects remaining water.
- Hydraulic trial/reset calls do not advance history. Native V10 preserves
  it exactly; compatible V9 reconstruction warns explicitly.
- The implementation does not compute intercell matric gradients, upward
  capillary redistribution or intersecting wetting-front dynamics. Tu, Wadzuk
  and Traver’s gravity-plus-diffusivity model and HYDRUS comparisons are not
  validation of this reduced kernel.

## Prior pollutant and interface corrections retained

- LAST-quality outfall backflow is an external pollutant source; ZERO supplies
  clean water. Zero-volume LID connections solve consistent donor/recipient
  mobile mixtures; provisional iterations restore counters and inventories.
- Nearly dry nodes export freshly percolated pollutant even when final mobile
  volume is below the legacy dry cutoff. Layer treatment is booked once.
- Physical weir/rating crests and layer anchors select their actual water
  source. Difference-of-datums arithmetic avoids `(0.3 + 2.0) - 0.3`
  classifying a 2.00-ft surface crest in MEDIA. Interface tolerance is only
  32 machine epsilons times local elevation scale; physical offsets remain
  distinct. Hydraulic and quality ports share the rule.
- Failed quality iteration warns rather than hiding a continuity problem.

## Engine regression results

| Suite | Tests passed |
|---|---|
| LID nodes | 40 |
| Quality routing | 21 |
| Treatment | 32 |
| Hotstart | 39 |
| Outfall backflow | 5 |
| LID water age | 6 |
| LID heat | 10 |
| Infiltration | 33 |
| Conventional LID | 55 |
| Total | 241 |

The full-chain regressions cover MEDIA / AGGREGATE × low / high tailwater ×
ZERO / LAST quality and per-step tracer closure. Focused cases cover sub-litre
drainage, surface weir roundoff and accepted-history/restart behavior.
The shared conventional infiltration and LID implementations were not edited.

## Source scope and further reading

Current kernel: engine `src/engine/hydrology/LidNode.cpp`,
`LidNodeTreatment.cpp`, `hydrology/Infiltration.cpp` and `core/HotStartManager.cpp`.
The hydraulics reference Chapter 2 documents the adapter; water-quality
reference Chapters 5 and 6 document mass transport and treatment. Engine
Chapter 6 describes syntax, lifecycle and limits. GUI T9/T10 describe editing
and running the supplied cases. Repository redesign plans contain superseded
proposals and are not authoritative descriptions of this runtime.

Groundwater closing remarks describe future investigations using spatial
`[2D_AQUIFER_OPTIONS]` / `[2D_AQUIFER]` / node/link exchanges. These closed-bottom
decks do not demonstrate coupled groundwater treatment. The article links
EPA primary sources, Tu et al.’s PLOS ONE study and the published ASCE valve
control paper; the local implementation sources belong here rather than the
article’s research-reference list.
