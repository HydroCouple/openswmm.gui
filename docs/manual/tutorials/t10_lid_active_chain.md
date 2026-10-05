@page tutorial_lid_active_chain T10 — Distributed LIDs: Active Control and Receiving-Water Backwater

## Goal

Explore two chained storage-node LIDs as a connected system. Compare passive
outlets, timed detention, downstream-head guards, a second storm and valve
failure. Assess flow attenuation and cumulative pollutant fate at every exit.

## Capabilities exercised

- Layered LID assignment on ordinary storage nodes
- Explicit inter-facility offsets and layer-relative overflow anchors
- Dynamic Wave backwater and signed flow through controlled orifices
- Timed rules; head isolation; a reopening band and depth relief
- Conservative-tracer checks and reacting-pollutant mass accounting

## Files

These six self-contained native INP files are in
`docs/manual/tutorials/models/lid_active_chain/`. Copy them into a working
folder. Each contains all time series; no rainfall or external data file is required.

| Download | Experiment |
|---|---|
| [01_passive_free.inp](01_passive_free.inp) | Open valves and low receiving stage |
| [02_passive_backwater.inp](02_passive_backwater.inp) | Open valves and high downstream stage |
| [03_timed_hold.inp](03_timed_hold.inp) | Hold A until 6 h and B until 8 h |
| [04_head_guard.inp](04_head_guard.inp) | Timed hold plus downstream-head guards |
| [05_repeat_storm.inp](05_repeat_storm.inp) | Guarded system with a second larger pulse |
| [06_stuck_closed.inp](06_stuck_closed.inp) | Both valves remain closed during both storms |

These are synthetic tests with hypothetical treatment kinetics. They are not
calibrated design recommendations. They require an engine containing the
storage-node LID pollutant-balance corrections described in the article's
validation notes. Use Dynamic Wave and `QUALITY_SOLVER LEGACY`.

## Steps

### 1. Open the baseline and inspect the network

Open **01_passive_free.inp**. A and B are 1,000 ft² constant-footprint storage
nodes, with inverts 0.30 ft and 0 ft and maximum depths of 2.50 ft. V_AB connects
A to B; V_BR connects B to receiving outfall R. W_A and W_B connect surface
overflows to separate outfalls FA and FB. Do not omit these overflow exits
from the system assessment.

\fig{t10_network-static.png, Three strategies in the same chained LID system at 5.5 hours}

A receives a triangular 0–0.30–0 cfs pulse at 0–1–2 h: 1,080 ft³. REACTIVE
and TRACER each enter at 20 mg/L. TRACER has no reaction; REACTIVE is a
hypothetical first-order constituent. Direct rainfall on a storage-node
footprint must come from a contributing subcatchment or explicit inflow.

### 2. Inspect or recreate the layered control

Select A, then B, in the Object Browser. Set **LID Control** to **Train** and
**LID Initial Saturation (%)** to **10**. Open **Model → LID Control** and select Train. For a new control, choose **New layered LID** and set
**Media / aggregate layers** to **2**.

| Ordered layer | Physical properties in this CFS project |
|---|---|
| 1 SURFACE | Thickness 6 in; vegetation fraction 0.1 |
| 2 MEDIA | Thickness 12 in; porosity 0.45; field capacity 0.20; wilting point 0.08; conductivity 5 in/h; conductivity slope 3; suction 3 in |
| 3 AGGREGATE | Thickness 12 in; porosity 0.40; conductivity 100 in/h |
| BOTTOM boundary | Seepage 0; clogging 0 |

Select each porous layer and open **Pollutant treatment**. **Add pollutant**
REACTIVE, set **Removal (%) = 0**, **Decay (1/day) = 2**, and leave
**Expression** empty. TRACER has no treatment row. Use **Apply layers and
treatment**. Total physical thickness is 30 inches: 2.50 ft. BOTTOM is a
boundary, not a treatment layer. See \ref tutorial_lid_storage for captured
physical-property and treatment editor views.

Equivalent native records are:

```text
[LID_CONTROLS]
Train NODE
Train SURFACE 6 .1
Train MEDIA 12 .45 .20 .08 5 3 3
Train AGGREGATE 12 .40 100
Train BOTTOM 0 0
[LID_NODES]
A Train 10
B Train 10
[LID_LAYER_TREATMENT]
Train 2 REACTIVE 0 2 -
Train 3 REACTIVE 0 2 -
```

### Physical interpretation of the layered control

Retained water occupies part of each cell’s pore volume. Connected mobile
water fills the remaining pores below the node water table. The reported
inventory includes both stores; an internal drainage transfer removes and
adds the same water volume once.

\fig{t10_water-stores.png, Retained moisture and connected mobile storage in one layered facility}

Intercell drainage is a gravity-only Darcy–Buckingham approximation, not
an adjacent-cell total-head calculation. For downward-positive `xi`, the
full one-dimensional relation is `q = K(theta) * (1 - dpsi/dxi)`; this
kernel omits the matric-head gradient and uses donor conductivity:

```text
q_i = Ks_i * clamp(theta_i / porosity_i, 0, 1)^n_i
      if theta_i > field_capacity_i; otherwise zero
Q_i = area_i * q_i
accepted_volume = min(Q_i * substep,
                      max(0, (theta_i - field_capacity_i) * G_i),
                      max(0, (porosity_receiver - theta_receiver) * G_receiver))
```

The receiving-space bound applies to a retained receiver entirely above
the mobile water table. Otherwise, the transfer enters mobile storage.
Cells whose bottoms lie below that table skip free drainage. Transfers
use a common pre-update state and are debited/credited once, with explicit
substeps at most one second. Aggregate cells use the same power law with
zero field capacity and the default exponent three. The surface entry
closure is separate:

```text
q_inf = K_receiver * (1 + suction_receiver * max(porosity_receiver - theta_receiver, 0)
                          / max(suction_receiver + theta_surface * thickness_surface, epsilon))
```

It uses the surface area and the same donor/receiver bounds; it does not
track a wetting front. For media with Ks = 10 mm/h, theta = 0.30,
porosity = 0.45, n = 3 and field capacity = 0.20, the trial drainage is
2.96 mm/h, independent of receiving-cell suction.

\fig{t10_interlayer-flux.png, Donor-based gravity flux and conservative limits compared with matric-gradient transport}

[Tu, Wadzuk and Traver (2020), Sections 2.1–2.3](https://doi.org/10.1371/journal.pone.0235528)
retain gravity and matric-gradient transport, with van Genuchten relations,
bidirectional connections and equal-matric-head conversion between different
textures. The present kernel omits that gradient term and adds a field-capacity
cutoff. Its network backflow is not upward capillary redistribution. More
cells do not restore the missing process. The approximation may suit coarse,
relatively uniform media dominated by gravity drainage, but contrasting textures,
capillary barriers and detailed root-zone moisture require additional assessment.
The paper's HYDRUS comparisons do not validate this simpler kernel.

\fig{t10_richards-approximation.png, What is retained and simplified from the Richards-equation formulation}

The assigned `Decay (1/day) = 2` has a half-life of `ln(2)/2` days, or
**8.3 hours**. Greater reaction with longer exposure follows directly from
this assumption; these tests examine how hydraulics change that exposure
and the mass leaving or remaining in the system. Fixed removal acts at a
physical authored-layer exit and is not repeated at every media cell.

\fig{t10_treatment-formulation.png, Pollutant compartments and the assumed first-order exposure-time response}

### 3. Inspect the connections and receiving boundary

V_AB is a side orifice with diameter 0.15 ft and coefficient 0.6. Its authored
orifice offset is 0.05 ft above the A invert. A link between two LID nodes uses explicit offsets rather
than a single-ended LID anchor. V_BR has diameter 0.10 ft and coefficient 0.6;
its anchor is **layer 3 / BOTTOM**. W_A and W_B are transverse weirs with
coefficient 3.3, 0.5-ft crest width and 0.5-ft section height. Their physical
crests are 2.0 ft above the respective node inverts, at **layer 1 / BOTTOM**.

```text
[LID_NODE_OUTLETS]
V_BR 3 BOTTOM
W_A 1 BOTTOM
W_B 1 BOTTOM
```

Open **02_passive_backwater.inp**. R's Tailwater time series is zero until
3 h, rises linearly to 1.60 ft at 4 h, stays there until 7 h, then falls to
zero at 9 h. The deck explicitly uses `OUTFALL_BACKFLOW_QUALITY ZERO`:
returning boundary water is clean. With held-concentration backflow, imported
pollutant must also be counted in the mass balance.

### 4. Compare timed detention and head-aware rules

Open **03_timed_hold.inp**, then **Model → Data Objects → Control Rules…**.
Inspect the two rules that keep V_AB closed until 6 h and V_BR until 8 h.
Open **04_head_guard.inp** to inspect the complete guarded rule set.

A representative downstream-head rule is:

```text
RULE IsolateB
IF NODE R HEAD > 1.25
THEN ORIFICE V_BR SETTING = 0
PRIORITY 3

RULE KeepBIsolated
IF NODE R HEAD > 1.05
AND LINK V_BR SETTING < 0.5
THEN ORIFICE V_BR SETTING = 0
PRIORITY 3

RULE ReliefB
IF NODE B DEPTH > 2.20
AND NODE R HEAD < 1.05
THEN ORIFICE V_BR SETTING = 1
PRIORITY 5
```

The reopening band avoids rapid switching at a single threshold. HEAD is an
absolute hydraulic elevation; DEPTH is relative to the selected node's
invert. The relief rule opens only when downstream head is low. It does not
replace an emergency overflow. Node HEAD and DEPTH track the mobile water table; perched surface ponding can be higher and supplies the local overflow-port head. Use the supplied full deck to reproduce all
rules and priorities. Save a separate copy when editing.

### 5. Run and compare results

Run each file separately. Inspect A/B depth and volume, R head, actual valve
settings, signed flows in V_AB and V_BR, W_A/W_B flows and pollutant
concentrations. Negative link flow indicates reversal. Use comparison plots
as described in \ref manual_time_series_plots.

\fig{t10_release-hydrograph.png, Receiving-outlet flow shifts with backwater and detention control}

Check water and pollutant continuity. For each pollutant, include initial
mass, all external sources, outfall discharge, flooding, seepage, reacted
mass and final inventory. Reported storage includes retained and mobile
water. A low effluent concentration or a closed valve does not prove removal.

\fig{t10_pollutant-fate-static.png, Cumulative tracer export and the final reactive-mass budget}

![Animated comparison of backwater and active controls](t10_network.gif)

![Animated cumulative tracer export with final pollutant fate](t10_pollutant-fate.gif)

These animations show the same sampled runs. The mass-fate bars are final
24-hour totals; only the time-series curves progressively reveal simulated
time. Static versions of both figures appear above.

<!-- START RESULTS_TABLE -->
| First-storm strategy | V_BR peak (cfs) | Tracer 50% export (h) | Reacted by 24 h |
|---|---|---|---|
| Passive / free outlet | 0.0369 | 5.08 | 33.9% |
| Passive / backwater | 0.0389 | 10.33 | 43.9% |
| Timed hold | 0.0345 | 10.04 | 42.6% |
| Hold + head guard | 0.0345 | 10.04 | 42.6% |
<!-- END RESULTS_TABLE -->


### 6. Add the second storm and failure test

**05_repeat_storm.inp** adds a 0–0.45–0 cfs pulse at 9–10–11 h.
**06_stuck_closed.inp** holds both orifices closed for all 24 hours. Examine
surface bypass, any flooding and the mass left at the end. Compare every
exit to the environment, not only V_BR. Do not credit retained pollutant as
treated pollutant. Examine the remaining capacity before the second pulse. At the 0.025-second
step the guarded two-storm run conveys approximately 1,208 ft³ through the
emergency weirs; the stuck-closed run conveys approximately 2,007 ft³
through the weirs. Neither case has a flooding loss in the corrected runs.
These volumes use cumulative engine accounting rather than snapshot sums.

## What to look for

- Downstream head can reverse flow and change available detention in both LIDs.
- Timed holding delays tracer export relative to the free-outlet case. Here its receiving-outlet peak is lower, but emergency bypass is larger.
- Timed and guarded holding give the same result in this first-storm test: the additional guard rules do not change its valve schedule. Test other stages and storms before claiming a control benefit.
- Passive backwater can produce more calculated reaction than the controller;
  treatment alone is insufficient to rank alternatives.
- A stuck closed valve still permits emergency overflow and leaves an inventory.
- MEDIA's five internal cells do not imply five repeated removal events.
  Saturated mobile water remains a shared storage reactor.

## Variations

Change only one feature per saved copy: the hold duration, receiving stage,
second-storm timing, footprint, media conductivity or decay coefficient.
Run a no-reaction control first. Try an additional LID in the chain and
compare all receiving-water exits over a common simulation horizon.
Repeat with smaller routing steps; check pollutant continuity and convergence
of peak flows, bypass volumes and treatment metrics separately. Flooding
losses must remain visible even when a run has a small continuity error.

These decks have closed bottom boundaries and do not include groundwater.
A future tutorial will investigate the spatial groundwater model and the
pathways between distributed recharge and receiving waters.

## Formulation references

- [EPA Hydraulics Reference Manual](https://nepis.epa.gov/Exe/ZyPURL.cgi?Dockey=P100S9AS.txt)
- [Tu, Wadzuk and Traver (2020)](https://doi.org/10.1371/journal.pone.0235528) — a more detailed representation of unsaturated flow in SWMM
- [Gomes Junior et al. (2022), published valve-control paper](https://doi.org/10.1061/%28ASCE%29WR.1943-5452.0001588)

## Related

- \ref tutorial_lid_storage — layer geometry and pollutant treatment editing
- \ref manual_hydraulics — control rules and hydraulic links
- \ref manual_water_quality — pollutant inputs and treatment expressions
- \ref manual_running — simulation and continuity reports
- \ref manual_time_series_plots — comparing result series
- \ref manual_data_objects — time series and data-object editors
