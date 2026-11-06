# Hydraulic Controls and Distributed LID in Open-Source SWMM 6

### From individual practices to distributed systems: detention, treatment, active control and receiving waters

Caleb Buahin
October 4, 2026

A rain garden can perform well on its own and still be part of a drainage system that performs poorly. Its outlet may meet a high downstream water level. Several facilities may release together. Holding water for treatment may leave too little capacity for the next storm. And water that infiltrates does not necessarily disappear from the watershed.

These questions have motivated the new LID Storage node implementation in Open-Source SWMM 6 (SWMM2D). The aim is to move beyond evaluating performance of localized low impact development practices and examine the system-wide implications of distributed LIDs for receiving groundwater and water bodies: flow peaks, timing, cumulative pollutant loads and the pathways connecting facilities.

![Animation comparing passive backwater, timed holds and head-aware controls in two chained LIDs](assets/network.gif)

*Figure 1. A simulated storm moves through two layered storage nodes. Blue arrows show forward flow; amber arrows show reversal; crosses mark closed valves. The receiving-water stage rises between hours 3 and 4, stays high until hour 7, then falls. Dots indicate direction, not tracked particles. Blue shading shows porous-cell moisture; teal marks surface ponding and saturated cells. Cross-sections are schematic and use a common head datum. [Static figure](assets/network-static.png).*

## Why the network matters

Conventional SWMM LIDs describe rainfall, infiltration and drainage within a subcatchment. SWMM can also represent treatment trains by routing runoff between dedicated subcatchments; LIDs within one subcatchment are treated in parallel. See the [EPA SWMM 5.2 User's Manual](https://nepis.epa.gov/Exe/ZyPURL.cgi?Dockey=P10145M6.TXT).

**SWMM 5 already provides LID drain controls.** Version **5.1.013 (August 2018)** added separate drain opening and closing water levels and a control curve that adjusts nominal drain discharge as a function of the head above the drain outlet. Each conventional LID control has **one underdrain definition, one opening/closing threshold pair and one optional head-based discharge curve**. This represents the combined drain response of a unit, which may contain several physical drain pipes; it does not provide separate hydraulic connections and valve settings at different elevations. These controls respond to water levels within the LID; the conventional LID underdrain calculation does not include the receiving node's downstream head. [EPA release notes](https://github.com/USEPA/Stormwater-Management-Model/releases/tag/v5.1.13); [EPA LID underdrain implementation](https://github.com/USEPA/Stormwater-Management-Model/blob/develop/src/solver/lidproc.c).

The new work connects the layered LID directly to SWMM's existing hydraulic network and control rules. **A storage-node LID can have an arbitrary number of hydraulic connections at different elevations**, with separate rules for each controllable link. An orifice used to represent a valve can be closed, partially open or fully open through its setting from 0 to 1. **Link flow responds to both the facility's head and the downstream head**, allowing backwater to restrict drainage and, where the link permits it, reverse flow into the facility. Controls can also respond to receiving-water levels. This couples the layered water stores and treatment processes to the surrounding network.

| Control capability | Conventional SWMM LID | Storage-node LID in Open-Source SWMM 6 |
|---|---|---|
| Drain connections | One underdrain definition per LID control | Multiple network connections at independently specified elevations |
| Operating settings | One opening/closing threshold pair and an optional head-based discharge multiplier curve | Separate rules and partial openings for each controllable link |
| Hydraulic feedback | Drain discharge depends on internal LID head | Dynamic Wave accounts for head at both ends and permitted flow reversal |

The added flexibility comes from exposing the layered facility to SWMM's existing network links and controls. It supports coordinated operation of several outlets within one facility and across a treatment train.

For the layered facility, this supports questions such as:

- Does detention at an upstream facility reduce a downstream peak, or move it into the peak from another tributary?
- How does downstream backwater change available storage, flow direction and exposure to treatment media?
- Can a controller retain polluted runoff while preserving capacity for a second storm?
- How much pollutant is transformed, discharged, bypassed or still stored at the end of the assessment?

Research on water-quality-informed real-time control provides a motivation for asking these questions. Sharior, McDonald and Parolari studied detention-basin control using water-quality information, with outcomes dependent on rainfall conditions. Gomes Junior and colleagues examined valve-control strategies and detention/flood tradeoffs in a stormwater network. Those studies support investigating controls; they do not establish the performance of the hypothetical LID train below. [Sharior et al., 2019](https://doi.org/10.1016/j.jhydrol.2019.03.012); [Gomes Junior et al., 2022](https://doi.org/10.1061/%28ASCE%29WR.1943-5452.0001588).

## A layered facility that remains a storage node

The implementation attaches a LID control to an ordinary storage node. Storage geometry supplies its footprint. An ordered stack supplies the surface, media and aggregate thicknesses and moisture properties. Ordinary hydraulic links supply connections to other facilities and the receiving water.

For example, a facility can have a low-level drawdown valve, an independently controlled intermediate-level outlet to a second facility, and a high-level emergency overflow. Each connection has its own elevation and hydraulic properties; the two valves can follow different schedules or opening settings. Dynamic Wave routing represents the effect of downstream head and flow reversal. Layer-relative outlet anchors follow physical stack edits; a link joining two LID nodes uses explicit endpoint offsets.

The distinction matters for water quality. In Richards mode, porous cells retain individual water and pollutant inventories, including when saturated; surface ponding is a separate mixed reservoir. The existing flow model uses retained cells plus a shared mobile-water reactor. Layer treatment can specify fixed removal, first-order decay and optional expressions. A surface bypass does not automatically receive all the treatment rules beneath it. The stack is **not a sequence of independent saturated plug-flow reactors**.

In the present implementation, storage-node pollutant routing uses Dynamic Wave with the Legacy quality solver. Assigning a LID does not add rainfall automatically: supply runoff through contributing subcatchments or an external inflow. Native INP and GeoPackage preserve the extension; export to legacy SWMM 5 does not preserve this behavior.

## The formulation: water, head and pollutant mass

### A semi-discrete Richards column inside a hydraulic node

The node now offers two flow models. **Existing formulation (with backwater)** retains the exponential gravity-drainage law and modified Green–Ampt surface entry used in the earlier article tests. **Richards 1D** resolves vertical gravity and matric-pressure gradients within MEDIA and AGGREGATE. The revised experiments below explicitly select Richards; the earlier results remain a separately labeled comparison.

Matric head **ψ** expresses the pressure of pore water relative to atmospheric pressure, as an equivalent water-column height. It is negative in unsaturated media: the pore matrix holds water under suction. Total hydraulic head is **H = z + ψ**, with upward-positive elevation **z**. A drier region can draw water from a wetter neighbor through this head difference, even upward against gravity. The flow direction depends on total head, while its rate also depends on how readily the partly filled pores conduct water.

The continuous one-dimensional water balance is:

> ∂w(ψ)/∂t = −∂q/∂z − s, with q = −K(ψ) ∂(z + ψ)/∂z

Here q is upward-positive Darcy flux and s a volumetric sink. **Semi-discrete** means that the column is divided into spatial control volumes while their water balances remain ordinary differential equations in time. For cells ordered from top to bottom, define Q positive downward:

> Wᵢ = Gᵢ [θᵢ(ψᵢ) + Sₛ,ᵢ max(ψᵢ, 0)]
>
> dWᵢ/dt = Qᵢ₋₁/₂ − Qᵢ₊₁/₂ − Eᵢ
>
> Qᵢ₊₁/₂ = A_f K_f (Hᵢ − Hᵢ₊₁) / d_f

**Gᵢ** is cell geometric volume, **A_f** the shared face area, **d_f** the distance between cell centers and **Eᵢ** the evaporation withdrawal rate. Neighboring cells receive equal and opposite interface transfers. **Sₛ** is specific storage: when pressure becomes positive, θ stays at porosity, while elastic water storage can increase. The porous column owns all pore and elastic water; the network node owns surface ponding. Saturated pore water is not counted again in a shared mobile reactor.

![Richards cell storage, local pressure and surface ponding, with the conservative spatial water balance](assets/water-stores.png)

*Figure 2. Water ownership in Richards mode. Every porous cell has its own water volume and pressure; surface ponding is the separate hydraulic reservoir. Numerical cells subdivide physical materials without adding new authored treatment layers. This is a formulation schematic.*

### How interlayer flux is calculated

Richards mode uses explicit **van Genuchten retention and Mualem conductivity** properties for each porous material. With m = 1 − 1/n:

> Sₑ = [1 + (α |ψ|)ⁿ]⁻ᵐ for ψ < 0; Sₑ = 1 for ψ ≥ 0
>
> θ = θᵣ + (φ − θᵣ) Sₑ
>
> K = Kₛ Sₑˡ [1 − (1 − Sₑ¹/ᵐ)ᵐ]²

Residual content **θᵣ**, porosity **φ**, saturated conductivity **Kₛ**, inverse capillary-length parameter **α**, shape parameters **n** and **l**, and specific storage **Sₛ** must be supplied. Conductivity slope, field capacity and Green–Ampt suction from the existing model do not determine these curves. The Richards flux has no field-capacity cutoff: redistribution continues wherever conductivity and a head gradient permit it.

Within equal-size cells of one material, the face conductivity is the arithmetic mean of the two cell conductivities. Across different materials, the two half-cell resistances act in series:

> Q_f = A_f (Hᵢ − Hᵢ₊₁) / [(Δzᵢ/2)/Kᵢ + (Δzᵢ₊₁/2)/Kᵢ₊₁]

Thus both sides influence interlayer flow. A positive downward Q drains the upper cell; a negative Q redistributes water upward. At the pond/media contact, the mean of saturated surface-contact conductivity and first-cell conductivity is used over half a cell thickness. Downward entry smoothly tends to zero through the last 1 micrometre of ponding; upward return remains possible. No separate Green–Ampt front is evolved in Richards mode. The spatial approach draws on the method-of-lines formulation described by [Ireson et al. (2023), openRE](https://doi.org/10.5194/gmd-16-659-2023); the implementation here has its own storage variables, boundary coupling and time integrator.

![Total-head differences and interface conductivities controlling downward drainage and upward capillary redistribution](assets/interlayer-flux.png)

*Figure 3. Signed Darcy–Buckingham flux at a cell face. Gravity and matric-pressure differences can reinforce or oppose each other; equal total heads give zero flux. Different-material interfaces use the combined half-cell resistance. Values are explanatory equation examples, not field measurements.*

The spatial equations are integrated with adaptive **BDF1 (implicit Euler)**. A full step and two half steps estimate temporal error, and the accepted update uses the two half steps. Newton iterations solve tridiagonal systems. Vertical integration and hydraulic routing are coupled in successive steps: boundary inputs remain fixed during a routing interval, then accepted port transfers are committed once. Cell-count, time-tolerance and routing-step sensitivity all matter; a small continuity error alone does not establish an accurate wetting-front position.

### Local pressure connects the column to active controls

A buried link uses the total head of the cell intercepted by its port; a surface overflow uses ponding above its physical crest. For a fully submerged orifice without a flap gate:

> Q = C_d Aₒ(u) sign(ΔH) √(2g |ΔH|)

The valve setting **u** controls the opening, while **ΔH** uses heads at both ends. Rising downstream head can suppress drainage or reverse flow. SWMM also handles partial wetting and structure-specific limits. [EPA Hydraulics Reference Manual, Chapters 3 and 6](https://nepis.epa.gov/Exe/ZyPURL.cgi?Dockey=P100S9AS.txt).

![Animation of forward flow, rising tailwater, reversal and valve isolation with the signed orifice response curve](assets/hydraulic-control.gif)

*Figure 4. Analytical submerged-orifice illustration with a 0.10-ft opening and C_d = 0.6. It explains the mechanism rather than replaying the tutorial controller. Playback is slowed; the displayed heads and ideal discharge remain unchanged. [Static figure](assets/hydraulic-control-static.png).*

Reverse inflow adds water to the intercepted porous cell. Its pressure changes through the storage law, and subsequent Richards fluxes redistribute that water through the column. A later storm therefore meets the resulting moisture/pressure profile, without clearing or reconstructing a Green–Ampt front. In Richards mode, **node HEAD and DEPTH describe the surface reservoir**, not a subsurface water table; inspect the cell pressure profile for buried-port conditions.

![Native Richards simulation of rising tailwater, subsequent recession and a later surface pulse](assets/resaturation.gif)

*Figure 5. Native Richards pressure/moisture profiles in a separate six-minute backwater-and-second-event test. The diagram and time cursor come from sampled engine states; arrows indicate flow direction rather than tracked particles. This small fixture differs from the 24-hour chain. [Static figure](assets/resaturation-static.png).*

### What is retained—and what remains simplified

This is a spatial approximation of the **one-dimensional Richards equation**, including the matric term; it is no longer the previous unit-gradient approximation. It assumes constant-density, isothermal matrix flow, a vertical column, atmospheric air pressure, local hydraulic equilibrium and single-valued retention/conductivity curves. Lateral unsaturated gradients, preferential/macropore flow, retention hysteresis and a resolved root-uptake field are not represented. Engineered media still require measured or defensible retention data. Richards can address matrix wetting, drying and capillary redistribution; that does not make every application more accurate without suitable parameters and discretization checks.

The existing model remains available when its reduced parameter demand and drainage approximation suit the question. More cells in that model refine its gravity-drainage balance; they do not add the missing matric term. [EPA LID source](https://github.com/USEPA/Stormwater-Management-Model/blob/develop/src/solver/lidproc.c).

Tu, Wadzuk and Traver (2020) represent gravity and matric-driven redistribution through connected, internally uniform blocks implemented with SWMM pumps and controls. Their diffusivity form uses **D(θ) = K(θ) dψ/dθ**, with moisture-to-matric-head conversion across textures. The new native column instead solves signed total-head fluxes directly between numerical cells and uses explicit interface conductances. Both retain the gravity and matric contributions of vertical matrix flow; their discretization, boundary treatment and calibration differ. The paper's HYDRUS comparisons are not validation of this native implementation. [Tu et al., 2020](https://doi.org/10.1371/journal.pone.0235528).

![Comparison of the existing gravity/front model and the new semi-discrete Richards column](assets/richards-approximation.png)

*Figure 6. The optional models use different water ownership and constitutive laws. Their results must be labeled by formulation; a previously passing gravity-model test is not a Richards verification.*

### Treatment follows accepted directional water transfers

Each wet porous cell carries pollutant mass **Mᵢ** and concentration **Cᵢ = Mᵢ/Wᵢ**. An accepted transfer removes its donor concentration times the transferred volume, bounded by available mass, and credits the receiving cell after any authored-layer exit treatment. Surface ponding has its own mixed concentration. Evaporation removes water and retains solute. Standard pollutant advection follows the accepted directional water ledger; hydrodynamic dispersion, sorption and species-specific reactive transport are not added by selecting Richards.

For the first-order reaction update:

> M_after = M_before exp[−(k_bg + k_layer) Δt]

Fixed removal acts once when water leaves an authored layer, rather than at every internal numerical face. A surface bypass does not inherit all buried treatment rules. The existing flow model retains its shared mobile-water reactor; Richards saturated cells keep their individual inventories. [EPA Water Quality Reference Manual, Chapter 5](https://nepis.epa.gov/Exe/ZyPURL.cgi?Dockey=P100P2NY.txt).

![Cell pollutant inventories, directional transfers and the assumed first-order exposure-time response](assets/treatment-formulation.png)

*Figure 7. Closed-parcel decay with k = 2/day has a half-life of ln(2)/k = **8.3 hours**. Longer exposure necessarily produces more modeled reaction under this assumption. The network determines exposure, bypass, release and remaining mass; this uncalibrated rate does not establish a field treatment mechanism.*

## Six small experiments with one common system

The accompanying tutorial uses two 1,000 ft² facilities, **A** and **B**, connected in series. Each has a six-inch surface layer, twelve inches of media and twelve inches of aggregate. Valve **V_AB** connects A to B; **V_BR** connects B to receiving boundary R. Separate emergency weirs connect each surface to an outfall, so bypasses remain visible in the assessment.

A two-hour triangular inflow peaks at 0.30 cfs and delivers 1,080 ft³ to A. It contains 20 mg/L of a conservative tracer and 20 mg/L of a hypothetical reactive constituent. The reactive constituent has a first-order rate of 2/day in each porous layer, with no fixed percentage removal. The specified porous-layer rate corresponds to a half-life of **8.3 hours**; each Richards porous cell uses its layer rate, while surface ponding has no porous-layer reaction. This deliberately simple assumption isolates the effect of exposure time: longer exposure gives more reaction by construction. It is not a calibrated prediction for sediment, nitrogen or phosphorus.

For the backwater cases, R rises to a head of 1.60 ft. Reverse water is explicitly assigned zero pollutant concentration, making it a clean hydraulic boundary rather than an unreported pollutant source. Separate engine regression cases test both clean and held-concentration backflow.

| Experiment | Change from the common setup | Question |
|---|---|---|
| 01 Passive / free outlet | Open valves; low receiving stage | What is the baseline detention and export? |
| 02 Passive / backwater | Open valves; rising receiving stage | How much does the boundary change the whole train? |
| 03 Timed hold | Open A at 6 h; B at 8 h | Can a planned hold delay export and increase calculated treatment? |
| 04 Hold + head guard | Add downstream-head isolation and a reopening band | Can control limit reverse flow without trapping the system indefinitely? |
| 05 Second storm | Add a 0.45 cfs pulse peaking at 10 h | What happens when the first event has occupied storage? |
| 06 Stuck closed | Both valves remain closed during both storms | Where do the water and pollutant go when control fails? |

The Richards head guard for V_BR closes above a receiving stage of 1.25 ft and holds until it falls below 1.05 ft. V_AB instead monitors B's surface HEAD at 2.25/2.05 ft: its media top is at elevation 2.0 ft, so these thresholds refer to 0.25/0.05 ft of ponding. The reopening band helps avoid rapid switching. A higher-priority depth-relief rule opens a valve above 2.20 ft **only when downstream head is low**. The emergency weirs remain available throughout. These are transparent test rules, not an optimized controller. In Richards mode, node HEAD and DEPTH track surface ponding, while buried links use their local cell pressure. These surface-based rules are intentionally distinct from the earlier mobile-water-table guards. Emergency overflow remains independent of controller operation.

![Outlet hydrographs showing how backwater and active control shift the release](assets/release-hydrograph.png)

*Figure 8. Receiving-outlet flow for the first-storm cases. Negative discharge represents backflow. The plot uses an outlet scale; the inlet peaks at 0.30 cfs. Emergency bypass is evaluated separately.*

### Comparing formulations, with explicit limits

| First-storm strategy | Corrected gravity/front sampled peak (cfs; 0.5 s) | Richards sampled peak (cfs; 1.25 s) | Corrected gravity/front reacted | Richards reacted |
|---|---|---|---|---|
| Passive / free outlet | 0.0313 | 0.0038 | 32.2% | 46.3% |
| Passive / backwater | 0.0374 | 0.0155 | 41.9% | 47.8% |
| Hold + head guard | 0.0345 | 0.0074 | 43.7% | 48.3% |

The gravity/front results above were rerun after correcting the retained/mobile partition bug described below; the earlier comparison values are withdrawn. This column uses legacy exponential drainage and Green–Ampt entry. Richards adds explicit, uncalibrated retention/specific-storage properties and has different initial water ownership; the inter-facility head guards also use surface-ponding thresholds. The routing steps differ and are identified in the table. These are illustrative configurations, not a calibrated equivalence experiment or proof that either formulation is superior. The Richards grid/timestep sensitivity below limits quantitative comparisons.

## Longer detention can help treatment—and change the risk

<!-- START RESULTS_TABLE -->
| Richards first-storm strategy (1.25 s, 8 cells/material) | Sampled V_BR peak (cfs) | Tracer 50% export (h) | Reacted by 24 h |
|---|---|---|---|
| Passive / free outlet | 0.0038 | 14.88 | 46.3% |
| Passive / backwater | 0.0155 | 19.27 | 47.8% |
| Timed hold | 0.0074 | 20.82 | 48.3% |
| Hold + head guard | 0.0074 | 20.82 | 48.3% |
<!-- END RESULTS_TABLE -->


The tracer timing is the elapsed time when sampled discharge through **all three outfalls** has exported half of the event's input tracer mass. It is not a mean residence time or a residence-time distribution. Reported reaction percentages use the engine's cumulative mass budget at 24 hours.

**The tendency for longer exposure to increase reaction follows directly from the assumed first-order decay law.** The experiments assess how network hydraulics and controls change exposure, export and inventory. They do not independently establish a field treatment benefit. In the illustrated Richards runs, holding changes release timing and modeled reaction, but does not guarantee a smaller receiving-outlet peak. Count emergency bypass and stored mass alongside controlled discharge.

These Richards examples are diagnostic experiments, not an optimized design or a converged ranking of controllers. The cell-count and routing-step checks below show why a richer equation must be accompanied by numerical verification. Other storms can also shift a delayed release into another tributary’s peak. Active control requires a system objective—such as pollutant export under a downstream flow limit—together with explicit accounting for bypass and available storage.

![Animation comparing cumulative tracer export and final pollutant fate](assets/pollutant-fate.gif)

*Figure 9. Curves progressively reveal cumulative tracer export from sampled link flows and concentrations. The adjacent bars always show the final 24-hour reactive-mass budget (second-storm step 0.625 s; other displayed strategies 1.25 s); they are not time-varying inventories. All exits and any flooding loss are included. A low export at an intermediate time can mean temporary storage. [Static figure](assets/pollutant-fate-static.png).*

The repeated-storm and stuck-valve cases make that distinction concrete. A zero controlled-outlet flow does not mean zero discharge to the environment. Emergency overflow and flooding can carry pollutant out, while an inventory remains in the facilities. Count every destination before reporting cumulative treatment. In the corrected two-storm guarded case, the engine records approximately 1,743 ft³ of emergency-weir discharge; with both valves stuck closed, that volume rises to approximately 2,016 ft³. Neither case has a flooding loss in these runs. The emergency weirs drain perched surface water at the intended crest. These totals are from the Richards runs. These cumulative totals include brief overflows that coarse snapshots can miss.

## A pollutant balance before a performance claim

For each constituent, the assessment checks:

> Initial mass + incoming mass = discharged mass + flooding and seepage losses + reacted mass + final stored mass.

Storage includes all porous-cell and surface inventories, including elastic water in the Richards saturated branch. In a reversing system, incoming mass includes any pollutant carried from the receiving boundary. Concentration alone is insufficient: exported load depends on integrating flow multiplied by concentration over time. The [EPA Water Quality Reference Manual](https://nepis.epa.gov/Exe/ZyPURL.cgi?Dockey=P100P2NY.txt) describes SWMM's pollutant routing and treatment framework.

Developing these tests exposed two accounting problems: returning boundary pollutant was not booked as an external source, and a nearly dry LID could lose the load of water that percolated and drained during the same routing step. The correction carries the same accepted transfer concentration to the donating and receiving compartments, resolves the connected mobile mixtures consistently and books treatment once. Overflow ports also use their physical weir crest when selecting the supplying layer. A separate roundoff error at a nonzero node invert could place a crest intended at the surface/media interface in the media cell, causing the weir to miss perched ponding. Corrected elevation arithmetic and a shared, roundoff-tolerant interface rule now select the intended head, water source and pollutant treatment layer.

<!-- START VALIDATION -->
The six examples were run with Richards explicitly enabled. The first four illustrated strategies use eight cells per porous material and a common 1.25-second fixed routing step; the second-storm figure uses 0.625 s and the stuck-closed case 1.25 s. The publication package contains 14 checkpointed complete case/numerics combinations, including finer routing, 4/16-cell and tighter ODE-tolerance checks. All these completed runs passed the 0.5% water/tracer/reactive continuity criterion without engine warnings; maximum reported error was 5.03e-11%. **The two-storm case failed at 1.25 s with a Richards adaptive-step-limit error; it is retained as a failed check, not counted as a pass.** The pressure-profile fixture passed at 0.1 and 0.05 s. **These are not numerically converged performance predictions:** routing-step and cell-count changes materially affect outlet peaks, reverse volumes and treatment exposure. The recorded Richards publication verification passed 9 kernel tests and 49 of 50 LID-node integration tests; the US/SI storage-equivalence fixture remains failing. The older 241-test/20-run evidence applies to the previous gravity/front implementation, not Richards.

| Guarded Richards case at 1.25 s | Sampled V_BR peak (cfs) | Reacted by 24 h | Emergency bypass (ft³) |
|---|---|---|---|
| 4 cells per porous material | 0.0120 | 39.87% | 555.4 |
| 8 cells per porous material | 0.0074 | 48.33% | 449.4 |
| 16 cells per porous material | 0.0014 | 53.97% | 394.3 |

The grid dependence is large enough that these runs cannot establish a robust ranking of control strategies. The guarded case’s tighter ODE tolerance changes reacted fraction by 0.000 percentage points; this does not resolve the distinct cell-size/routing coupling sensitivity. Further numerical verification and measured retention properties are needed before design use. The 02_passive_backwater peak changes from 0.0155 to 0.0199 cfs at 0.625 s. The 04_head_guard peak changes from 0.0074 to 0.0115 cfs at 0.625 s.
<!-- END VALIDATION -->

A small water-balance error alone is not sufficient evidence of correct pollutant routing. We check a conservative tracer, the reacting constituent and routing-step sensitivity before interpreting treatment. The supplied kinetics, footprints, conductivities, storm pulses and boundary stages remain illustrative. Field applications require site data and pollutant-process calibration.

<!-- START PARTITION_VALIDATION -->
Independent testing identified another problem in the earlier gravity/front model: an inlet above the reported water table could keep filling a partially submerged cell as retained moisture, while that cell stopped free drainage as soon as its bottom was submerged. At porosity, its remaining mobile capacity collapsed and its water inventory was abruptly reclassified. Water was conserved, but the resulting head and hydrograph were wrong. The reproduced free-outlet case rose 0.993 ft in 30 seconds while total water fell from about 517 to 514 ft³; the backwater case rose 0.631 ft at about hour 2.675. **The correction keeps partially submerged cells draining retained excess and routes incoming water to mobile storage when the receiving cell intersects the water table**, even if its inlet is above that table. Only fully submerged cells skip free drainage. This is the reduced model’s cell-level coupling approximation, not a resolved local Richards pressure field. After correction, the largest 30-second rise in B is about 0.0031 ft in both examples, and the free-outlet case no longer has the artificial reversal into A. All six legacy examples passed water/tracer/reactive balances at 0.5 and 0.25 s (12 completed runs); the current LID-node suite passes 52 of 53 tests, including three new regressions for partial-cell drainage, inlet ownership and per-step routed-head continuity. The previously reported Richards US/SI fixture still fails. These results replace the affected legacy performance comparison; the Richards figures retain their separately identified run provenance. **Continuity is necessary, but head continuity and a physically consistent storage partition must also be checked.**
<!-- END PARTITION_VALIDATION -->

## Configure the same example in SWMMVis

Open **richards_04_head_guard.inp** from the Richards example bundle. Inspect storage A or B: **LID Control** is **Train** and **LID Initial Saturation (%)** is **10**. Open **Model → LID Control**, select Train, and choose **Richards 1D** under **Flow model**. The two authored porous layers remain MEDIA and AGGREGATE; numerical subdivisions are configured separately.

Under **Physical properties**, verify the thicknesses, porosities and saturated conductivities, then enter the explicit retention properties:

| Material | Residual water content | Retention alpha (1/m) | Retention n | Pore connectivity l | Specific storage (1/m) |
|---|---|---|---|---|---|
| MEDIA | 0.03 | 4 | 1.8 | 0.5 | 0.0001 |
| AGGREGATE | 0.01 | 20 | 2.5 | 0.5 | 0.0001 |

These are illustrative properties, not a conversion from the older conductivity slope. Alpha and specific storage remain in inverse metres even though this CFS project displays thickness in inches and conductivity in inches/hour. Expand **Numerical settings**: use **8 numerical cells per porous layer**, absolute water-content tolerance **1e-7**, relative tolerance **1e-5**, and maximum internal step **30 s**. Inspect the moisture/conductivity curve preview.

Under **Pollutant treatment**, assign REACTIVE to each porous layer, set **Removal (%) = 0**, **Decay (1/day) = 2**, and leave **Expression** empty. Use **Apply layers and treatment** and save. Set a fixed routing step of **1.25 s** for the four first-storm strategies and the stuck-closed case, and **0.625 s** for the second-storm case; use variable-step factor **0**. The download copies already carry these tested steps. The validation companion reports routing, cell-count and ODE-tolerance checks, including the failed second-storm run at 1.25 s.

Inspect V_BR's layer-3 bottom anchor, both surface overflows, and V_AB's explicit offsets. Review the surface-HEAD thresholds in **Model → Data Objects → Control Rules…**. Run the six files separately, compare all signed flows and mass destinations, and inspect the live section's numerical-cell moisture/pressure. Saved profile time histories are pending; the figure runner records the live API explicitly.

The [T10 tutorial](tutorial.html) supplies the complete workflow. [Download the six Richards models and short profile fixture](models/lid_richards_chain.zip); the [original gravity/front examples](models/lid_active_chain.zip) remain available for a clearly labeled comparison. Richards hotstart extension 11 preserves complete cell water and material identity. Runtime aquifer-bed exchange, water age, heat and MSX are not yet supported with Richards nodes.

## From distributed LIDs to receiving waters—and groundwater

The larger objective is to move from discrete LID performance to the system-wide implications of distributed LIDs for receiving water bodies: event peaks, delayed releases, cumulative loads and the pathways connecting facilities across a watershed.

The new **spatially explicit groundwater model** extends that perspective below the surface. Its mesh-based two-zone representation supports spatial aquifer properties, lateral groundwater flow, recharge, saturation-excess return to the surface and configurable exchange with drainage infrastructure. It provides a basis for asking where infiltrated water travels and when it returns to the drainage network or receiving waters. The demonstrations use a closed bottom boundary. The Richards LID–aquifer bottom-interface adapter remains pending, so these runs do not demonstrate coupled groundwater exchange or groundwater pollutant treatment.

Future articles will explore that connection: distributed recharge and changing water tables, surface-water–groundwater feedbacks, and how those pathways alter the combined benefits and constraints of LID placement and active control. **The question is becoming what the distributed system delivers to the receiving water, over time.**

## Acknowledgments

I thank Dr. Rob Traver for discussions that helped refine the ideas behind the LID Storage node implementation and for his support of its development and implementation. I also thank Corinne Wiesner-Friedman and Robert Dickinson for reviewing this article.

## Acknowledgment of AI assistance

OpenAI Codex assisted with drafting and editing this article and developing the scripts used to create its figures and GIF animations. The network performance results come from the documented SWMM simulations; the formulation diagrams and orifice animation are explanatory illustrations. Caleb Buahin is responsible for the technical interpretation and final content.

## References

- Ireson, A. M., Spiteri, R. J., Clark, M. P., and Mathias, S. A. (2023). *A simple, efficient, mass-conservative approach to solving Richards' equation (openRE, v1.0).* Geoscientific Model Development, 16, 659–677. [Published paper](https://doi.org/10.5194/gmd-16-659-2023).
- U.S. EPA (2018). *SWMM Build 5.1.013 release notes*, underdrain control additions. [EPA release notes](https://github.com/USEPA/Stormwater-Management-Model/releases/tag/v5.1.13).
- U.S. EPA (2022). *Storm Water Management Model User's Manual, Version 5.2*. EPA/600/R-22/030. [EPA manual](https://nepis.epa.gov/Exe/ZyPURL.cgi?Dockey=P10145M6.TXT).
- U.S. EPA. *SWMM solver source: LID fluxes and infiltration*. [lidproc.c](https://github.com/USEPA/Stormwater-Management-Model/blob/develop/src/solver/lidproc.c); [infil.c](https://github.com/USEPA/Stormwater-Management-Model/blob/develop/src/solver/infil.c).
- Rossman, L. A. (2017). *Storm Water Management Model Reference Manual, Volume II: Hydraulics*. EPA/600/R-17/111. [EPA hydraulics reference](https://nepis.epa.gov/Exe/ZyPURL.cgi?Dockey=P100S9AS.txt).
- Tu, M.-c., Wadzuk, B., and Traver, R. (2020). Methodology to simulate unsaturated zone hydrology in Storm Water Management Model (SWMM) for green infrastructure design and evaluation. *PLOS ONE*, 15(7), e0235528. [doi:10.1371/journal.pone.0235528](https://doi.org/10.1371/journal.pone.0235528).
- Rossman, L. A., and Huber, W. C. (2016). *Storm Water Management Model Reference Manual, Volume III: Water Quality*. EPA/600/R-16/093. [EPA reference](https://nepis.epa.gov/Exe/ZyPURL.cgi?Dockey=P100P2NY.txt).
- Sharior, S., McDonald, W., and Parolari, A. J. (2019). Improved reliability of stormwater detention basin performance through water quality data-informed real-time control. *Journal of Hydrology*, 573, 422–431. [doi:10.1016/j.jhydrol.2019.03.012](https://doi.org/10.1016/j.jhydrol.2019.03.012).
- Gomes Junior, M. N., Giacomoni, M. H., Taha, A. F., and Mendiondo, E. M. (2022). Flood Risk Mitigation and Valve Control in Stormwater Systems: State-Space Modeling, Control Algorithms, and Case Studies. *Journal of Water Resources Planning and Management*, 148(12), 04022067. [doi:10.1061/(ASCE)WR.1943-5452.0001588](https://doi.org/10.1061/%28ASCE%29WR.1943-5452.0001588).
