# Rubinato 2017 coupling comparison

These portable SI models accompany the 21 October 2026 coupling review.
They demonstrate the implemented exchange controls and expose remaining
validation limits. They are **not experimentally validated design models**.

| Input | Purpose | Expected exchange direction |
|---|---|---|
| `steady_s1.inp` | First Table 2 free-weir row | Surface to sewer |
| `steady_s2.inp` | First Table 2 submerged-weir row, at the crest transition | Surface to sewer |
| `steady_s3.inp` | First Table 2 wet-surface surcharge row | Sewer to surface |
| `steady_s4.inp` | First Table 2 no-incoming-surface-flow row | Sewer to surface |
| `model.inp` | Synthetic Table 1 T9 peak/duration demonstration | Drain, surcharge, drainback |

The steady files use the actual 0.24 m shaft storage area (0.0452389 m2),
the same MIN_SURFAREA, 0.5 s routing, WEIR_ORIFICE, STENCIL_WET_MEAN and the
RUBINATO2017 coefficient preset. The central manhole location, reconstructed
inflows and downstream pressure reservoir are assumptions. The 0.2 m grid
has 1,600 triangles; it is not the authors' 11,186-cell mesh. Node coordinates
place the schematic network on the map; hydraulic pipe lengths remain those
specified in `[CONDUITS]`.

The synthetic file runs 600 s, with surface inflow 8.15 L/s, a 30 s rise
starting at 60 s, the Table 1 T9 peak ratio 1.26 held for 341 s, then a 30 s
fall. Its downstream stage is assumed to be 0.45 m. Times are HH:MM:SS.
The measured sewer pressure series was not used; this is not Figure 10/11
validation. The expected-preset reference demonstration has about 1.415 m3
gross spill and 0.201 m3 gross drainback; its ordinary 1D continuity is about
0.77%. Modifying the model or engine can change those numbers.

Open **Rubinato 2017: Sewer-Surface Exchange** from the Welcome page, or
open `model.inp` in SWMMVis. Check the mesh and MH's 2D coupling properties,
then run it. The files request 1D node/link output and 2D results so the
corresponding `.out` and `.2d.h5` can be inspected together. Run copies in a
working folder to keep generated reports and output with the matching input.
Only MH is mapped to the mesh. IN and OUT are explicitly not coupleable;
OUT supplies the artificial pipe boundary, not a second surface exchange.

Use the ordinary 1D and separate 2D continuity blocks, gross spill and drain,
node head and nearby surface depth. A process exit code or high regression
R-squared does not establish agreement with the measurements. The steady
comparison at 0.5 s includes unresolved time sensitivity: four representative
0.05 s runs change exchange materially. The S2 crest row is particularly
sensitive to smoothing and rounded measured pressure.

`table1_unsteady_tests.csv`, `table2_steady_exchange.csv`,
`table3_coefficients.csv` and `rig_geometry.yaml` are unchanged copies of the
engine's reference files. `steady_comparison.csv` contains all 36 observed
rows and both reconstruction variants at the report's 0.5 s diagnostic step.
The apparent fit of an unconverged run must not be used for calibration.

## Physical-time smoothing follow-up (22 October 2026)

The supplied inputs preserve the dated review settings. To test the new
momentum option in a copy, open Simulation Options → 2D Surface Routing →
Hydraulics. In Explicit marcher, set Momentum θ to 0.8 and Smoothing
reference interval to 1 s. Zero means the original per-step smoothing.
An older engine disables the new control. A positive interval requires an
engine supporting `THETA_REFERENCE_STEP`; surface GPU plugins require ABI 9.

Keep that 1 s reference fixed while setting routing intervals to 0.0125,
0.00625 and 0.003125 s in three copies. The full 36-case study uses 80 1D
maximum trials, head tolerance 0.00001 m, variable step 0 and lengthening
step 0. These are numerical controls, not changes to the physical shaft or
exchange coefficients. Compare exchange means over 240–300 s and the
preceding minute, heads and separate 1D/2D continuity. The reference interval
is a declared numerical timescale; it was not fitted to the measurements.

T11 and the hydraulic reference comparison include the new all-row plots,
small data download and follow-up report. Blue filled circles in those new
plots are the fine-step physical-time results; gray open circles are the
older 0.5 s per-step results. Improved timestep consistency does not establish
experimental agreement or spatial convergence. The synthetic T9 outputs
quoted above remain the original demonstration, not a rerun in the new mode.

The hydraulic reference manual includes the comparison page and exchange
formulation. The dated report remains in the engine repository under
`docs/validation/rubinato2017/`. The SWMMVis walkthrough is
`docs/manual/tutorials/t11_rubinato_exchange.md`.
The Welcome page opens `model.inp`; open the steady inputs from the same
working folder to inspect each regime.

Reference: [Rubinato et al. (2017)](https://doi.org/10.1016/j.jhydrol.2017.06.024),
*Journal of Hydrology* 552, 421-432. Reference tables: CC-BY-4.0, with original
headers and article anomalies retained. Original example decks: CC0-1.0.

Regenerate the documentation ZIP with `python3 scripts/package_rubinato_example.py`
from the SWMMVis repository root; `--check` verifies its input files match.
