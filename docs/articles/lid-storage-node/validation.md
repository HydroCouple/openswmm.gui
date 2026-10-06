# Validation and reproducibility — Richards distributed LID examples

Updated October 5, 2026. The article now explicitly selects **Richards 1D**. The failed two-storm
1.25-s run is recorded in `results/richards/failure.json` and `failed/`.
The prior gravity/front matrix is retained under `results/existing-model/`;
its 20 case/step combinations and 241-test evidence do not validate Richards.
The current checkout includes other development work; the tested binary hash
identifies the implementation actually loaded, rather than implying a clean
HEAD build. Source/model hashes and native reports accompany the CSV samples.

Library: `libopenswmm.engine.6.0.0.dylib`

SHA-256: `dabc78a26287aff798c94bbaba0ca2d5171176ff2c4a0fa8c901ccf50291f146`

## Actual checks and interpretation limits

The six examples were run with Richards explicitly enabled. The first four illustrated strategies use eight cells per porous material and a common 1.25-second fixed routing step; the second-storm figure uses 0.625 s and the stuck-closed case 1.25 s. The publication package contains 14 checkpointed complete case/numerics combinations, including finer routing, 4/16-cell and tighter ODE-tolerance checks. All these completed runs passed the 0.5% water/tracer/reactive continuity criterion without engine warnings; maximum reported error was 5.03e-11%. **The two-storm case failed at 1.25 s with a Richards adaptive-step-limit error; it is retained as a failed check, not counted as a pass.** The pressure-profile fixture passed at 0.1 and 0.05 s. **These are not numerically converged performance predictions:** routing-step and cell-count changes materially affect outlet peaks, reverse volumes and treatment exposure. The recorded Richards publication verification passed 9 kernel tests and 49 of 50 LID-node integration tests; the US/SI storage-equivalence fixture remains failing. The older 241-test/20-run evidence applies to the previous gravity/front implementation, not Richards.

| Guarded Richards case at 1.25 s | Sampled V_BR peak (cfs) | Reacted by 24 h | Emergency bypass (ft³) |
|---|---|---|---|
| 4 cells per porous material | 0.0120 | 39.87% | 555.4 |
| 8 cells per porous material | 0.0074 | 48.33% | 449.4 |
| 16 cells per porous material | 0.0014 | 53.97% | 394.3 |

The grid dependence is large enough that these runs cannot establish a robust ranking of control strategies. The guarded case’s tighter ODE tolerance changes reacted fraction by 0.000 percentage points; this does not resolve the distinct cell-size/routing coupling sensitivity. Further numerical verification and measured retention properties are needed before design use. The 02_passive_backwater peak changes from 0.0155 to 0.0199 cfs at 0.625 s. The 04_head_guard peak changes from 0.0074 to 0.0115 cfs at 0.625 s.

The tighter options are atol = 1e-9, rtol = 1e-7, compared with 1e-7/1e-5.
Material properties are explicit synthetic van Genuchten–Mualem laws, not
calibrated media or defaults. Specific storage is a physical input, not a
solver stabilization parameter. The first four strategies use the same 1.25-s routing step. The second-storm
and failure-mode mass bars identify their separately tested steps. Sampled peaks and tracer timing use 30-second live-API
samples; cumulative bypass uses engine statistics. This sampling can miss
brief instantaneous peaks and is not a residence-time distribution.

### Current test failure, retained rather than hidden

`LidNodes.RichardsPhysicalStateIsConsistentAcrossInputUnits` fails its exact
storage-equivalence assertion: 65.005715 versus 64.9984766687 ft³, a difference
of 0.0072383313 ft³ (about 0.0111%). The existing functional-storage geometry
preserves SWMM's truncated SI volume conversion (0.02832 m³/ft³), while the
fixture authors its SI area using exact 0.3048². This mismatch requires a
separate unit-convention/fixture correction; it is not evidence of exact unit
parity. These article runs use CFS exclusively. The complete failing test
output is preserved in `results/richards/lid_node_tests.json`.

Richards kernel: 9 tests, 0 failures.
LID-node integration: 50 tests, 1 failure.
The tests include storage/retention inversion, hydrostatic equilibrium,
capillary rise, layered resistance, tolerance refinement, batch rollback,
port budgets, input/API/restart and pollutant routing. Broader application
verification remains necessary; passing balances do not establish accuracy.

<!-- START PARTITION_VALIDATION -->
Independent testing identified another problem in the earlier gravity/front model: an inlet above the reported water table could keep filling a partially submerged cell as retained moisture, while that cell stopped free drainage as soon as its bottom was submerged. At porosity, its remaining mobile capacity collapsed and its water inventory was abruptly reclassified. Water was conserved, but the resulting head and hydrograph were wrong. The reproduced free-outlet case rose 0.993 ft in 30 seconds while total water fell from about 517 to 514 ft³; the backwater case rose 0.631 ft at about hour 2.675. **The correction keeps partially submerged cells draining retained excess and routes incoming water to mobile storage when the receiving cell intersects the water table**, even if its inlet is above that table. Only fully submerged cells skip free drainage. This is the reduced model’s cell-level coupling approximation, not a resolved local Richards pressure field. After correction, the largest 30-second rise in B is about 0.0031 ft in both examples, and the free-outlet case no longer has the artificial reversal into A. All six legacy examples passed water/tracer/reactive balances at 0.5 and 0.25 s (12 completed runs); the current LID-node suite passes 52 of 53 tests, including three new regressions for partial-cell drainage, inlet ownership and per-step routed-head continuity. The previously reported Richards US/SI fixture still fails. These results replace the affected legacy performance comparison; the Richards figures retain their separately identified run provenance. **Continuity is necessary, but head continuity and a physically consistent storage partition must also be checked.**
<!-- END PARTITION_VALIDATION -->

## Reproduce

From the GUI checkout with a rebuilt current native library:

```text
python3 docs/articles/lid-storage-node/figures/generate_examples.py --richards
python3 docs/articles/lid-storage-node/figures/run_examples.py --library /absolute/path/to/libopenswmm.engine.dylib --decks docs/manual/tutorials/models/lid_richards_chain --formulation richards --cases 01_passive_free 02_passive_backwater 03_timed_hold 04_head_guard --steps 1.25 --output docs/articles/lid-storage-node/results/richards/baseline
python3 docs/articles/lid-storage-node/figures/run_examples.py --library /absolute/path/to/libopenswmm.engine.dylib --decks docs/manual/tutorials/models/lid_richards_chain --formulation richards --cases 02_passive_backwater 04_head_guard --steps 0.625 --output docs/articles/lid-storage-node/results/richards/routing-fine
python3 docs/articles/lid-storage-node/figures/generate_examples.py --richards --cells 4 --destination /tmp/lid-richards-cells4
python3 docs/articles/lid-storage-node/figures/generate_examples.py --richards --cells 16 --destination /tmp/lid-richards-cells16
python3 docs/articles/lid-storage-node/figures/generate_examples.py --richards --atol 1e-9 --rtol 1e-7 --destination /tmp/lid-richards-tight
```

Run each generated sensitivity deck's `04_head_guard.inp` at 1.25 s with
`run_examples.py`, recording outputs in `results/richards/cells4`, `cells16`
and `tight`. Run the second-storm case separately at its illustrated step and at 2.5 s,
and the stuck-closed case at 5/2.5/1.25 s. The second-storm 1.25-s failure is
not treated as successful evidence. `publish_richards_runs.py` consolidates
the checkpointed summaries and prepares matching downloads. The totals,
sampled profiles and reports are recorded below. `input_sha256` is recorded for every deck in the summaries.

Run `test_engine_richards_column` and `test_engine_lid_nodes` with JSON output
and copy the records as `kernel_tests.json` and `lid_node_tests.json` beside
this matrix. The current failure must remain disclosed until corrected.
Then:

```text
python3 docs/articles/lid-storage-node/figures/run_richards_cycle.py --library /absolute/path/to/libopenswmm.engine.dylib
python3 docs/articles/lid-storage-node/figures/publish_richards_runs.py
python3 docs/articles/lid-storage-node/figures/finalize_richards_text.py
python3 docs/articles/lid-storage-node/figures/make_figures.py
python3 docs/articles/lid-storage-node/figures/make_richards_figures.py
python3 docs/articles/lid-storage-node/figures/stage_site.py
```

GIF playback uses 24 centiseconds/frame over 64 source frames: 15.36 s per
cycle, versus 10.24 s previously. Simulation timestamps are unchanged.
Static alternatives and reduced-motion controls remain available. The
orifice GIF is analytical; the pressure/moisture cycle is native output.
The separate six-minute fixture passes at 0.1/0.05 s and does not assert
complete saturation of all media cells.

## Complete Richards run matrix

| Case | Step (s) | Water error (%) | Reactive error (%) | Tracer error (%) | Sampled V_BR peak (cfs) | Reacted (%) | Bypass (ft³) |
|---|---|---|---|---|---|---|---|
| 01_passive_free | 1.25 | -3.12e-12 | 0 | 0 | 0.0038 | 46.33 | 449.4 |
| 02_passive_backwater | 1.25 | 1.12e-12 | 0 | 0 | 0.0155 | 47.81 | 449.4 |
| 03_timed_hold | 1.25 | 4.24e-12 | 0 | 0 | 0.0074 | 48.33 | 449.4 |
| 04_head_guard | 1.25 | 4.24e-12 | 0 | 0 | 0.0074 | 48.33 | 449.4 |
| 05_repeat_storm | 0.625 | 1.89e-12 | 0 | 0 | 0.0124 | 26.54 | 1742.6 |
| 06_stuck_closed | 1.25 | 1.08e-12 | 0 | 0 | 0.0000 | 21.31 | 2015.5 |
| 04_head_guard | 1.25 | -9.5e-13 | 0 | 0 | 0.0120 | 39.87 | 555.4 |
| 04_head_guard | 1.25 | 5.03e-11 | 0 | 0 | 0.0014 | 53.97 | 394.3 |
| 04_head_guard | 1.25 | 2.03e-11 | 0 | 0 | 0.0074 | 48.33 | 449.5 |
| 02_passive_backwater | 0.625 | 6.93e-13 | 0 | 0 | 0.0199 | 47.22 | 449.5 |
| 04_head_guard | 0.625 | 7.83e-12 | 0 | 0 | 0.0115 | 47.81 | 449.5 |
| 06_stuck_closed | 5 | -2.25e-13 | 0 | 0 | 0.0000 | 21.31 | 2015.5 |
| 06_stuck_closed | 2.5 | -2.85e-13 | 0 | 0 | 0.0000 | 21.31 | 2015.5 |
| 05_repeat_storm | 2.5 | -2.61e-12 | 0 | 0 | 0.0044 | 24.96 | 1851.7 |


## Storage, coupling and pollutant scope

The porous column owns complete pore/elastic water and local pressure;
surface ponding belongs to the network reservoir. Signed face fluxes use
arithmetic conductivity within a material and half-cell resistances across
materials. Adaptive BDF1 accepts two half steps. Surface entry is integrated
with Richards, not Green–Ampt. Network port trials reset their budgets and
commit accepted transfers once. Routing-step sensitivity tests this split
coupling separately from the ODE solver.

Each porous cell owns its pollutant inventory, including saturation.
Directional accepted transfers debit resident donor mass and credit the
recipient; evaporation retains solute. Layer-exit removal is not repeated
at each numerical face. The 2/day decay assumption implies an 8.3-hour
half-life; longer exposure gives more reaction by construction.

Closed bottoms are used here. The kernel has a prescribed-head verification
boundary, but runtime aquifer-bed coupling is pending and rejected explicitly.
Water-age, heat and MSX porous-cell adapters and saved profile histories are
also pending. The sampled profile figures use the live API.

Primary scientific references are cited in the article. Local code evidence
belongs in this companion: `RichardsColumn.cpp`, `LidNodeRichards.cpp`,
`LidNode.cpp`, `LidNodeTreatment.cpp`, hydraulic ports and hotstart code.
The hydraulic and water-quality references document the formulations; the
GUI T10 tutorial documents explicit properties, controls and test workflow.
