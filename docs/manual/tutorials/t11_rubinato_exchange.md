@page tutorial_rubinato_exchange T11 — Sewer-Surface Exchange: Rubinato Laboratory Comparison

## Goal

Run a small sewer/surface model, inspect exchange in both directions, and
compare its output with published measurements while keeping numerical
checks and experimental agreement distinct. The 0.24 m manhole and flume
dimensions follow Rubinato et al. (2017). The boundary conditions are
reconstructed or synthetic, as identified below.

## Capabilities exercised

Node coupling type and geometry, coefficient presets, mesh mapping,
surface-head sampling, storage area, simulation options, 1D and 2D results,
head/depth plots, continuity reporting, and model save/reload.

## Files

Open **Rubinato 2017: Sewer-Surface Exchange** from the Welcome page and
choose a working folder. The example opens `model.inp`, the synthetic T9
demonstration. Alternatively, download and extract
[rubinato2017_examples.zip](rubinato2017_examples.zip), then open `model.inp`. Use
`steady_s1.inp` through `steady_s4.inp` for the
four flow regimes. Copy the inputs and reference CSV files into a working
folder before running. The included README records every modelling
assumption and the report's reference demonstration values.

The [hydraulic reference comparison](https://www.hydrocouple.org/openswmm.engine/dc/d71/hydraulics_ref_rubinato2017.html)
provides the illustrated report, all 36 steady comparisons and the exchange
formulation. The ZIP also includes the reference tables and comparison CSV.
Experimental qualification remains incomplete.

Open the [interactive results viewer](rubinato2017_results.html) to inspect
the measured/simulated comparisons by regime and play the synthetic T9
surface results with a shared hydrograph cursor. It works offline. A
[printable four-figure supplement](rubinato2017_visual_supplement.pdf) and
[PNG/SVG figures with the plotted data](rubinato2017_visuals.zip) are
available for review and reuse.

## Steps

1. Open `model.inp`. Inspect the 8 m by 4 m triangular mesh and the
   central MH storage node. IN and OUT are schematic pipe-boundary nodes;
   only MH exchanges with the surface. Preserve that authored mapping.
2. Inspect MH's node properties. Its **2D coupling** type is `MANHOLE`,
   diameter 0.24 m and preset `RUBINATO2017`. Its storage curve and
   `MIN_SURFAREA` are both 0.0452389 m2. Storage nodes use their own curve;
   selecting a junction pond mode does not replace it. The map row omits
   Cd/Area overrides so the structure coefficients govern.
3. In **Simulation Options**, inspect the 2D exchange law `WEIR_ORIFICE`,
   surface head `STENCIL_WET_MEAN`, and coupling equalisation. The model is
   in metres and m3/s. Inspect the synthetic inflow timeseries: the rise
   starts at 00:01:00 and the plateau begins at 00:01:30. Times have three
   fields, hours:minutes:seconds. Two fields would move the pulse outside
   the ten-minute run.
4. Run the model. Open the report and inspect ordinary **Flow Routing
   Continuity** and **2D Surface Routing Continuity** separately. Confirm
   nonzero gross **1D -> 2D Spill Inflow** and **2D -> 1D Drain Outflow**.
   In the checked expected-preset demonstration these are about 1.415 m3
   and 0.201 m3. Their difference is the signed net transfer; neither
   gross value alone is the net. The ordinary 1D continuity error is about
   0.77%. Check the separate surface balance as well.
5. Load the matching 1D `.out` and 2D `.2d.h5` results. Plot MH head and
   surface depth/HGL from cells beside the manhole using a common time
   axis. Inspect the 0.478 m crest level, surcharge during the imposed
   pulse and subsequent drainback. The review's synthetic plot is model
   output only; it contains no measured exchange hydrograph.
6. Save the input and project with the matching outputs, close and reopen
   them. Confirm the storage geometry, coupling type, unauthored map
   coefficients and result associations are retained.

## Follow the synthetic surface results

\fig{t11_rubinato_transient.png, Actual saved synthetic T9 surface depths and signed exchange; model output only}

The maps use a fixed 0–25 mm colour scale; the orange ring marks the manhole.
The vertical markers identify their times on the hydrograph. Surface inflow
continues throughout the run, so the map includes both inflow and surcharge
water. In the [viewer](rubinato2017_results.html), use **Play**, **Restart**,
the speed selector and the frame slider to inspect the saved states. Gross
spill and gross drainback update alongside the displayed time. The depth
fields are saved every 10 s and are not interpolated; their maximum is
20.909 mm, while the all-step maximum is 24.803 mm. The native hydrograph
retains the routing-step samples. These figures are calculated from saved
results, rather than screenshots of the application.

## Compare with the steady measurements

Run each steady input. Use the automated benchmark CSV for late-window
exchange: it exports the 240-300 s mean and compares it with 180-240 s. The
end-of-run report totals divided by 300 s give a whole-run average, not the
last-minute rate. Only compare a
rate as steady when those windows agree acceptably and both domain ledgers
are acceptable. The full review includes all rows; the four supplied steady
inputs are only the first row of each scenario.

| Scenario | Measurement in the supplied seed (L/s) | Meaning |
|---|---|---|
| S1 | 0.69 | Free-weir drainage, no sewer inflow |
| S2 | 0.24 | Submerged-weir drainage near the crest |
| S3 | 1.73 | Surcharge onto a wet floodplain |
| S4 | 1.74 | Surcharge with no incoming surface flow |

Do not treat a successful run or recovered regression slope as an
experimental pass. Only 8 of 36 physical-shaft rates at the review's 0.5 s
step lie in the illustrative measurement-variation-plus-2% comparison band,
and some rates are not temporally converged. The paper's Table 3 slopes
belong to fits with nonzero intercepts. Its wet outward orifice value is not
an independently calibrated inward-orifice coefficient.

\fig{t11_rubinato_parity.png, Experimental versus simulated steady exchange for all 36 Table 2 measurements}

Blue points show the physical-shaft model, open triangles the original
junction model, and orange crosses the bare paper equations evaluated at
measured heads. The dashed line is 1:1 agreement. Horizontal error bars
show the paper's measurement variation \f$\xi_f\f$. Both axes within each
panel use the same scale; the scale changes between regimes. MAE describes
the physical-shaft absolute residual in L/s. All full-engine points here
use the review's 0.5 s step and remain subject to convergence checks.

\fig{t11_rubinato_residuals.png, Exchange residuals by experimental row; shaded comparison band is not a confidence interval}

Residuals are calculated minus measured discharge magnitudes. The green
band is \f$\pm(\xi_f+0.02Q_e)\f$, an illustrative comparison criterion.
Row numbers restart within each regime. The points and band retain the
disagreements that a successful run alone cannot reveal.

\fig{t11_rubinato_routing_steps.png, Selected experimental rates compared with physical-shaft results at two routing steps}

Green lines/bands show the signed experimental rate and \f$\xi_f\f$.
The blue points show how reducing the routing step changes the late-window
mean in one selected case per regime. Positive is sewer to surface.
These four reruns do not establish convergence of the full matrix.

## Variations

- Run copies with 0.1 s and 0.05 s routing steps. Compare late-window rates,
  head, gross transfer and continuity. Smaller drift is useful, but a
  change of mean rate between steps means time convergence remains open.
- Compare lower/expected/upper coefficient presets. This is a parameter
  sensitivity; the resulting envelope is not a confidence interval.
- Compare `CELL` and `STENCIL_WET_MEAN` head sampling. Record the selected
  cell positions and heads; these options do not reproduce the paper's
  exact pressure transducer or complete circular-opening average.
- Refine the mesh in a copy while preserving the physical structure and
  its only intended coupling. Compare the sampling and delivery footprint.
  The existing fine-mesh anti-choking test is not a mesh-convergence result.

## What remains unvalidated

The measured transient sewer-head and exchange series have not been
replayed. Table 1's ratios/durations do not determine those series. The
synthetic demonstration cannot reproduce the paper's observed suction
oscillation or establish transient experimental accuracy. The hydraulic
reference insert and illustrated review state the regularisation,
single-cell delivery and time-step limitations that require further work.

Reference: [Rubinato et al. (2017)](https://doi.org/10.1016/j.jhydrol.2017.06.024),
*Journal of Hydrology* 552, 421-432.

## Related

- \ref tutorial_1d2d_coupling — synthetic plaza spill and drainback
- \ref manual_hydraulics — node coupling properties
- \ref manual_simulation_options — exchange law and head sampling
- \ref manual_results — 1D/2D result association
