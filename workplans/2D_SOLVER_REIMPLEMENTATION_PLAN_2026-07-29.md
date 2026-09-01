# 2D Solver Reimplementation — Explicit Multiscale FV Engine + Windowless 1D↔2D Coupling

**Date:** 2026-07-29 · **Status: FOR REVIEW** · Supersedes all prior 2D plans per owner directive.
**Engine repo:** `openswmm.engine` (swmm6_rel @ f714abd6, clean). GUI consumes the prebuilt install.
All interface claims below were verified against the code (file:line cited).

---

## A. Plain-language summary — what is being tested, in one page

**What we have today (implicit).** The 2D surface is written as one giant system of
ordinary differential equations — one equation per cell, 228,410 of them for Bellinge —
and handed to **CVODE** (the SUNDIALS implicit BDF integrator). Every time step, CVODE
solves a global nonlinear system over the whole mesh (Newton iterations → GMRES linear
solver → **hypre BoomerAMG** preconditioner). "Implicit" buys big, stable timesteps in
smooth conditions, but every step costs a whole-mesh matrix-like solve, and the step size
is set by the *worst* cell in the domain (a wetting front on one small cell throttles all
228k). The 1D↔2D exchange is frozen over "coupling windows," which adds a large layer of
failure/retry machinery.

**What this plan builds (explicit, no linear solver).** A purpose-built **explicit
time-marching solver** — the family used by LISFLOOD-FP, TUFLOW HPC, and InfoWorks ICM for
exactly this class of problem. Each face of the mesh updates its flow directly from its two
neighboring cells using a small time step; no global system is ever assembled, so there is
**no Newton, no GMRES, no AMG, no hypre in the hot path** — just tight loops over faces and
cells. The one implicit ingredient is **friction**, which is solved per-face with a single
algebraic division (this is what makes the scheme stable in shallow water; it needs no
matrix). So strictly: *explicit marcher with pointwise semi-implicit friction*.

**The physics ("local inertial").** Still finite volume, still exactly conservative. The
momentum equation is the diffusive wave you chose, **plus a small inertia (memory) term**.
That term is the trick: pure explicit diffusive wave forces Δt ∝ Δx² (tiny steps on fine
cells — this is why the implicit route was chosen originally); adding inertia relaxes the
limit to the familiar CFL condition Δt ∝ Δx. At low Froude numbers — ponds, streets,
floodplains — it behaves like the diffusive wave. This is the standard industrial answer.

**Why explicit wins here (the arithmetic).** At 228k cells, any implicit scheme (BDF or a
θ-scheme) needs on the order of 10⁵ preconditioned linear-solver sweeps over the whole mesh
for a 48 h run — hours of wall time serially, before physics. The explicit marcher's cost
is (number of wet cells) × (their own local steps). With **dry/thin-film cells skipped**
and **cells stepping at their own local pace** (fine urban cells take small steps, big
rural cells take large steps — "tiered local timestepping"), the 48 h Bellinge run prices
out at roughly 150–250 s serial. That local-pacing property is also exactly your
multiscale requirement: a 2 m urban cell and a 200 m watershed cell coexist without one
punishing the other.

**Why not implicit, like HEC-RAS?** Implicit is the right tool when two assumptions hold:
(1) the flow varies slowly enough that steps of minutes are *accurate*, and (2) the Newton
solve converges at those big steps. Both fail for flashy urban/pluvial flooding. During the
event that matters, accuracy and wetting-front motion pull even HEC-RAS down to seconds-
scale steps (Courant ≈ 1–2 is its own guidance for events) — so the "long timestep" payoff
largely evaporates exactly when the domain is active, and each of those now-small steps
still costs a full 228k-cell Newton + preconditioned linear solve. Worse, moving wet/dry
fronts make the residual non-smooth, so Newton fails and the controller cuts Δt — then you
pay small steps AND global solves; that is precisely the measured 128k corrector-failure
storm in our CVODE path. Implicit's advantage is biggest when nothing is happening — and
in that regime our explicit engine costs ~zero, because dry/quiescent cells are simply not
updated. Implicit must still touch all 228k unknowns every step (the matrix is global);
explicit + active sets + local tiers touches only wet cells, each at its own pace — which
is also the multiscale requirement. The measured arithmetic in this repo: the implicit
path needed 60–170k full-mesh linear-solver sweeps per 48 h — hours serial at 228k cells
before physics — versus ~1–2×10⁹ own-rate cell-updates ≈ 150–250 s for the explicit
design. HEC-RAS's home regime (riverine/estuarine: deep, smooth, mostly-wet everywhere,
genuinely quasi-steady for hours) is the one place implicit shines — and if that use case
becomes primary, the plan keeps a θ-implicit backend as a future option behind the same
interface. We also keep implicitness wherever it is free: the friction term is solved
implicitly per face (one division, no matrix).

**Is anything hybrid?** At the system level, yes: the **1D engine is untouched** (SWMM
dynamic-wave Picard solver, as today). The 2D becomes explicit. Coupling becomes simple:
each 1D step, the 2D takes many small internal steps and exchanges water through the
existing orifice law continuously (no windows, no held forcing, no claw-back), plus one
small stabilizing derivative term added to the 1D node equation so nodes stop oscillating
between drain and spill.

**What happens to CVODE / hypre / AMG?** They are not deleted. The whole current solver
remains in the tree as a runtime-selectable **reference mode** (one line in `[2D_OPTIONS]`:
`INTEGRATOR CVODE` vs `EXPLICIT`) used to cross-check the new engine's answers during
validation. hypre/AMG stays available for a possible future semi-implicit (θ) backend.
The honest answer on AMG scaling: AMG makes big matrix solves cheaper — the winning move
at this cell count is to have no matrix at all.

**What is being tested, concretely:** can an explicit local-inertial FV marcher with
active-cell tracking + tiered local timestepping run coupled Bellinge (228k cells, 48 h,
rain-on-grid) in **< 300 s serial** while conserving mass exactly and agreeing with the
CVODE reference in the regimes where both are valid — yes or no, with staged gates along
the way.

## B. Isolation & revert strategy (nothing is burned)

- **Baseline tag first:** tag `swmm6_rel` @ `f714abd6` as `2d-baseline-2026-07-29` before
  any work (plus the same for the GUI repo's current commit). Reverting the whole
  experiment = `git checkout swmm6_rel` — mainline is never touched.
- **All work on one experiment branch** in the engine repo: `feat/2d-explicit-marcher`,
  branched from f714abd6. No pushes by the agent (owner pushes); no force operations;
  merges to `swmm6_rel` only after the Phase-4 acceptance gate and owner review.
- **The new engine is additive, not destructive:** new files + a factory branch. The
  CVODE path stays the **default** until acceptance passes; even on the experiment branch
  every model can flip back per-run with `INTEGRATOR CVODE` (or by omitting the key).
  Window machinery and the old coupling path are bypassed on the new path, **not deleted**,
  until the owner retires them (decision D2).
- The only shared-code edits before acceptance are small and reversible (factory/options
  plumbing, two safety fixes, a bit-identical closure de-duplication); each lands as its
  own commit so any single piece can be reverted independently.
- Benchmarks/artifacts live in repo-visible dirs (`tools/bench_2d/out/`), so every claimed
  number stays reviewable.

---

## 0. Mandate

Re-attack the 2D model and its 1D coupling from first principles. The semi-discrete
CVODE/BDF diffusive-wave architecture — even after the full 2026-07 campaign (analytic
Jacobian, tangent-exact AMG preconditioner, hmin=0, partial windows) — is capped ~3–4×
above where it stands by its own documentation. The target requires ~300×. This plan
replaces the numerical engine and the coupling architecture. It reuses only solver-agnostic
infrastructure: mesh loader, SoA geometry, rainfall interpolation weights, options plumbing,
the orifice exchange law, and the exact-volume exchange ledger.

## 1. Hard requirements

- **R1 Conservative finite volume** (non-negotiable): exact local mass conservation by
  construction — across faces, wet/dry, all limiters, LTS tier interfaces, and coupling.
- **R2 Performance:** `BellingeSWMM_v021_nopervious.inp` — 48 h coupled, 1,015 conduits /
  995 junctions, 1,020 coupling points, **228,410-cell** mesh (~12 m over 34.89 km²),
  rain-on-grid (`NATURAL_NEIGHBOUR`) — **< 5 min (300 s) wall, serial CPU**.
- **R3 Multiscale by design:** one mesh spans watershed-scale assessment and fine-scale
  urban detail — cell-area disparities of 10²–10⁴ (historically 0.04 → 29,414 m²) must cost
  *proportionally* (each cell pays its own rate), never set a global timestep or a global
  stiffness penalty. This is where every prior attempt died: CVODE paid a measured 1,850×
  graded-mesh penalty; the old windowed inertial prototype diverged outright.
- **R4 Parallel-ready:** flat, race-free, deterministic kernels; Kokkos port is a later
  phase (owner-deferred) but nothing may preclude it.
- **R5 Fresh test suite** designed for this engine (analytic, property, multiscale,
  coupling, benchmark, acceptance). Legacy tests are not the definition of correctness.

## 2. Why the current architecture cannot meet R2/R3 (measured evidence)

1. **Global implicit BDF ties every cell to the stiffest cell.** Wetting-front RHS kinks
   scale ∝ 1/cell-area (`docs/2D_MULTISCALE_FORMULATION_REVIEW.md` F1) — structurally
   anti-multiscale; one fine cell drags 228k cells to sub-second steps.
2. **The linear-algebra bill alone busts the budget.** 48 h of BDF or θ-implicit stepping
   needs 60–170k preconditioned Krylov iterations; each is a full-mesh SpMV + AMG V-cycle
   (~75–150 ms at 228k cells) → hours serial before physics. No preconditioner fixes this.
3. **Windowed coupling multiplied the cost:** 33,580 integrator re-entries/run; AMG
   hierarchy rebuilt per reinit (~6,000/8 h probe); a 458-line failure state machine
   (frozen windows, claw-back, partial-carry, lag leash) exists only to reconcile an
   adaptive ODE solver with discretely-held forcing.
4. **The coupled 1D was collateral damage:** the 2D CFL hint clamped the 1D step
   1.55 s → 0.22 s; exchange enters the 1D with zero head-sensitivity (`sumdqdh`) →
   drain/spill Picard oscillation → 98.7 % non-converging steps on Bellinge.
5. Campaign ceiling on the *13k-cell* mesh: coupled 48 h ≈ 3,805–5,516 s; the target mesh
   is 17.4× larger. The engine's own verdict: "3 minutes is not reachable with trustworthy
   results" (`bench_3min/FAST_RUN_RECIPE.md`).

## 3. Architecture decision

### 3.1 The serial budget (what 300 s buys at 228k cells)

| Approach | Cost model | 48 h serial estimate |
|---|---|---|
| BDF / θ-implicit (any global solve) | 60–170k Krylov × 75–150 ms | **hours** — excluded |
| Explicit, global dt (~1–2 s CFL), full domain | ~1.7×10¹⁰ cell-updates × 150 ns | 2,000–3,000 s — excluded |
| **Explicit + flux-active sets + tiered LTS** | ~1–2×10⁹ own-rate updates | **~150–250 s — design point** |

Rain-on-grid makes tiers mandatory, not optional: during the storm all 228k cells are
nominally wet, but thin-film cells (sub-mm–mm depths, slow celerity) legitimately take
8–32 s steps while the deep flood minority steps at ~1–2 s. Storm phase ≈ 800M updates
≈ 120 s; 36 h drainage tail at ~5 % wet ≈ 75 s. This is the only architecture whose
arithmetic fits — and it is what industrial urban-flood engines do (TUFLOW HPC tiered
"control numbers", InfoWorks ICM, LISFLOOD-FP lineage).

### 3.2 Chosen architecture

**A. 2D engine — explicit unstructured FV local-inertial marcher:** volume V per cell,
normal discharge q per *unique face* (`InertialEdges` layout reused), semi-implicit
friction (unconditionally stable), CFL-driven **tiered local timestepping**, **flux-active
sets** with a source-only (lazy) mode for rain-on-grid thin films, **positivity limiter**
guaranteeing V ≥ 0 (deletes the negative-volume-debt/resync concept). Local-inertial keeps
the transient term that converts the diffusive-wave parabolic Δt ∝ Δx² limit into a
hyperbolic Δt ∝ Δx CFL — and degenerates to diffusive-wave behavior at low Froude,
preserving the original modeling intent.

**B. Coupling — windowless co-advancement:** the 1D runs at its own adaptive step (2D CFL
clamp removed). After each 1D step [t, t+dt₁], the marcher subcycles to t+dt₁ on its own
tiers; the existing orifice law (kept verbatim) is evaluated **per 2D substep** with live
2D heads; ∫Q dt accrues to the existing exact-volume ledger, consumed by
`assembleLateralInflows` unchanged. The 1D node solve gains an exchange conductance
Σ|∂Q/∂h₁d| in the `sumdqdh` denominator — provably stabilizing (it only grows the
denominator) — the mirror of the measured Phase-3d win (nonlinear failures 1,229 → 4).

**C. Multiscale is the organizing principle (R3):** tiers come from *local* CFL, so a 2 m
urban cell steps at ~0.2 s while a 200 m rural cell steps at ~30 s in the same mesh; cost
∝ Σ own-rate. Faces always integrate at the finer incident cell's cadence with an inbox
accumulator on the coarser side — arbitrary tier jumps across a face stay conservation-
exact with no mesh-grading requirement. Face-normal projected distances (replacing the
centroid-chord shortcut) keep gradients honest across size jumps. Fine cells at coupling
points — the historic killer — simply live in fine tiers.

### 3.3 Alternatives considered

- **θ-method / Casulli semi-implicit** (externally proposed; prototype previously
  approved): sound physics, but the linear-solve arithmetic excludes 300 s serial at 228k
  (§3.1). Not built now; remains a possible future third backend behind `ISurfaceSolver`.
- **Full shallow-water explicit (Godunov/HLL):** same tier/active-set machinery, 2–3×
  per-face cost → ~300–700 s serial; over budget serially, fine post-Kokkos. Deferred —
  kernels are structured so an SWE flux can drop in later (decision D1).
- **Keep fixing CVODE-MOL:** capped ~3–4×; structurally anti-multiscale (F1). Excluded as
  primary; retained short-term as independent cross-check only (decision D2).
- **The in-tree ARKODE-IMEX "inertial" path is not a counter-example:** it ran explicit
  physics inside 60 s held-forcing windows (~66× over gravity CFL) and diverged; its
  unique-edge/CSR data layer is reused, its integrator harness is not. Likewise the 07-21
  plan "eliminated" local-inertial only at full-domain global-dt cost — precisely what
  active sets + LTS remove — and its Δx² argument applies to explicit *diffusive* wave,
  not LI.

## 4. Numerical scheme (implementable as written)

State: `V_i` (m³) per cell (`SurfaceStateData::volume`, unchanged); `q_e` (m²/s, unit-width
discharge normal to the face, positive cL→cR per the `InertialEdges.hpp:15-18` orientation
contract) per unique interior face. SI internally; g = 9.80665.

**4.1 Face flow depth.** `h_f = max(η_L, η_R) − z_face`, `z_face = max(z_c,L, z_c,R)`
(precomputed, `InertialEdges.hpp:50`). η from the closure — FLAT (`η = z_c + V/A`) or VFR
(`vfrEtaFromMeanDepth`, `mesh/VfrClosure.hpp:141`) — both pure h(V), single-sourced in the
new kernels header (also de-duplicating the closure currently cloned across 4 sites).
If `h_f ≤ h_wet` (= `DRY_DEPTH`, 1 mm): `q_e := 0` (hard wall, no friction division).

**4.2 Face slope (non-orthogonality fix).** `S_e = (η_R − η_L)·inv_dx_normal_e`, with
`dx_normal = |(c⃗_R − c⃗_L)·n̂_e|` floored at 0.3·|c⃗_R − c⃗_L| — face-normal projection,
correct under size disparity; fixes the centroid-chord shortcut in the DW kernel
(`SurfaceFluxCalculator.cpp:382-384`). Precomputed into the `InertialEdges` extension.

**4.3 Face update (de Almeida–Bates on unstructured unique faces), at the face's tier Δt:**

```
q̂_e     = θ·q_e + (1−θ)·½·(q⃗_L·n̂_e + q⃗_R·n̂_e)            θ default 0.8  ([2D_OPTIONS] THETA)
q_e*    = ( q̂_e − g·h_f·Δt·S_e ) / ( 1 + g·Δt·n_f²·|q_e| / h_f^{7/3} )     n_f = ½(n_L+n_R)
q_e^{n+1} = clamp( q_e*, ±Fr_max·h_f·√(g·h_f) )              Fr_max default 1.5 (FROUDE_MAX)
```

- Friction is the exact semi-implicit form already validated at `ArkodeSurfaceSolver.cpp:276`
  (g·h·Δt·n²·|q|/h^{10/3} ≡ g·Δt·n²·|q|/h^{7/3}). Unconditionally stable.
- The structured scheme's lateral q-neighbors are replaced by the face-normal component of
  the incident cells' **Perot-reconstructed** unit-discharge vectors
  `q⃗_c = (1/A_c)·Σ_{e∈c} sgn_c(e)·q_e·ξ_e·(m⃗_e − c⃗_c)` (exact for linear q fields; one
  pass over active cells per tier step, reused for velocity output).
- θ = 1 recovers Bates 2010; θ < 1 damps thin-film checkerboarding on steep urban faces.
  θ + Froude clamp are the steep-face stability levers.

**4.4 Cell update (conservative gather + positivity), at cell i's tier Δt_i:**

```
O_i  = Σ_{e: sgn_i(e)·q_e > 0} sgn_i(e)·q_e·ξ_e                     (outgoing, m³/s)
λ_i  = min(1, β·max(V_i,0)/(Δt_i·O_i)),   β = 0.8                    (positivity)
q_e ← λ_i·q_e  for every face leaving cell i (stored value scaled; shared face takes min λ)
V_i ← V_i + Δt_i·( S_i·A_i − Σ_e sgn_i(e)·q_e·ξ_e ) + inbox_i ;  inbox_i ← 0
S_i  = rain_i − min(evap_i, V_i/(A_i·Δt_i)) + coupling & boundary per-area terms
```

One shared flux value updates both sides ⇒ conservation exact under limiting; V ≥ 0 always
⇒ no debt machinery. Per-cell CSR gather from `InertialEdges` — race-free, bit-deterministic
(R4). Evap floored so sinks cannot drive V negative.

**4.5 Well-balancedness + dry-neighbor wall (C-property, unit-tested).** Lake at rest:
η uniform ⇒ S_e = 0 and q̂_e = 0 ⇒ q stays exactly 0 for any bathymetry, both closures.
Dry neighbor standing higher: `h_f ≤ 0` ⇒ wall — automatic sill gating, no uphill creep.

**4.6 Tiered LTS (power-of-two, TUFLOW-HPC-style).**
- Per-cell stable step `dt_i = α·L_char,i/√(g·h_i + ε)`, `L_char,i = 2A_i/ξ_max,i`,
  α = `CFL_NUMBER` default 0.7. Base `dt_0 = min over flux-active cells`; tier
  `k_i = clamp(floor(log2(dt_i/dt_0)), 0, K−1)`, K = `LTS_TIERS` default 4 (range 1–8;
  K=1 ≡ global-dt debug/equivalence mode kept forever). Sync macro-step = 2^{K−1}·dt_0,
  classic halving order (tier k fires every 2^k substeps).
- **Face tier = min(k_L, k_R)** — faces integrate at the finer incident cadence, reading
  the coarser cell's η frozen at its last own-update. No mesh-grading constraint needed.
- **Conservation-exact interface rule:** each face update books `ΔM_e = q_e·ξ_e·dt(t_e)`
  once — subtracted from one cell, added to the other (via `inbox` when the receiver is
  coarser). Identical FP products on both sides ⇒ ΣV closes to reduction roundoff; LTS
  ledger equals global-dt ledger at sync boundaries (property test).
- Hysteresis: demote-to-finer immediately (stability never deferred); promote-to-coarser
  only after the condition holds 2 consecutive syncs.
- For extreme disparity meshes (10⁴:1 area ⇒ ~100:1 dt), raise K toward 7–8; the Gate-2
  equivalence test re-runs at the chosen K.

**4.7 Flux-active set + rain-on-grid.** Flux-active iff `h_i ≥ h_on` (H_MOVE + 1 mm) ∪
1-ring halo ∪ coupling-stencil ∪ non-wall-BC cells; exit at `h_i < h_off` (H_MOVE − 1 mm,
default H_MOVE 3 mm) with no active neighbor, sustained 2 syncs. Inactive cells carry no
face q (walls) and integrate **sources only, lazily at sync boundaries**
(`V += S_i·A_i·dt_sync`) — rain-on-grid over thin films costs ~nothing until water must
move; a source-only cell crossing h_on self-activates at its next sync (latency ≤ dt_sync).
Frontier-incremental rebuild O(active); full rescan every 64 syncs as a safety net.
(Replaces — does not reuse — the CVODE `ActiveSetBuilder`, which is entangled with
ydot-masking and window-breach semantics.)

**4.8 Boundaries.** Interior-only q DOFs; boundary edges evaluate the existing 5 BC types
(`boundaryEdgeFlux` flux-form) per substep of the owning cell's tier, booked to the same
ledgers. Bellinge is walls + outfalls.

## 5. Coupling spec (windowless co-advancement)

**5.1 Loop shape (1D order unchanged):** `updateOutfallsPreRouting` (SWMMEngine.cpp:2566) →
1D Picard `router_.step` (:2674) → `advancePostRouting(ctx, dt_routing, t)` (:2685). On the
marcher path `advancePostRouting` subcycles 2D across [t, t+dt_routing] directly — no
`pending_dt_`, no window fire, no failure/carry/claw-back states (dt is CFL-known a
priori; the F8 failed-window rainfall drop and partial-lag freeze cease to exist).

**5.2 Per-substep exchange.** Marcher reads `state.node_coupling`/`state.nodes_1d`
(`SurfaceStateData.hpp:64-65` — hooks already in place). At each substep of a coupled
cell's tier (coupled stencils pinned flux-active):
- `Q_k = computeNodeCouplingQ(...)` (`NodeCoupling.cpp:163`) — **formulation verbatim**:
  `Q = Cd·A_eff·sign(Δh)·√(2g)·φ(|Δh|)`, C¹ `orificePhi` (2 cm reg.), crown Hermite gate
  (5 cm band), wet/dry ramps. Live 2D heads (this substep); 1D heads frozen at the routing
  step's Picard result. The `provisional_vol_m3` drawdown-faking path becomes unreachable.
- **Limiter (availability, per substep):** drain (Q>0): `Q_k ≤ β·V_avail/dt_sub` with a
  shared per-cell debit ledger for co-located points; spill (Q<0): running per-routing-step
  cap `Σ|Q_k|·dt_sub ≤ max(0, node stored volume)` — fill-and-spill thrash becomes
  structurally impossible. Optional `EXCHANGE_RELAX` EMA (default off) held in reserve.
- Apply `−Q_k·dt_sub` to V via the existing upwind-HGL stencil scatter weights; accumulate
  `exch_k += Q_k·dt_sub` (m³, + = 2D→1D).

**5.3 Ledger booking (kept verbatim).** At routing-step end the router reads
`solver->last_coupling_exchange()` (`ISurfaceSolver.hpp:84` — **zero interface changes
needed**) and books `nodes.coupling_volume[ni] += exch_k·flow_2d_to_1d` (ft³, per
`NodeData.hpp:248-261`). Consumption untouched: `assembleLateralInflows` re-derives
rate = volume/dt at the consuming step (`SWMMEngine.cpp:5437-5441`) — VARIABLE_STEP-safe.
Claw-back/queue-spread stays for the CVODE path; never populated on the marcher path.

**5.4 1D exchange conductance (per Picard iteration; the churn fix).**

```
G_k  = Cd·A_eff·√(2g)·φ'(|Δh|)·(crown gate)·(wet ramp)   ≥ 0     [SI]
G_n  = Σ_k G_k · flow_2d_to_1d · len_1d_to_2d            ≥ 0     [1D units]
dw.nodeSumDqdh(n) += G_n
```

φ′ bounded and positive (φ′(0) = 3/(2√ε)); gate/ramp ∈ [0,1]; ∂A_eff/∂h terms deliberately
dropped to guarantee sign. Scattered inside the per-iteration non-conduit lambda
(`SWMMEngine.cpp:2661` pattern) because `sumdqdh` re-zeroes each Picard iteration
(`DynamicWave.cpp:1057`); `nodeSumDqdh` accessor already exists (`DynamicWave.hpp:534`) —
**no DynamicWave structural changes**. Under default `EXPLICIT` node continuity the term
damps the surcharge/hover-at-crown regime (`DynamicWave.cpp:2991-3002`) — exactly where
churn was measured. **Flagged pre-existing issue (owner ruling requested, D5):** the
SEMI_IMPLICIT branch writes `denom = surf_area − 0.5·dt·sumdqdh` (`DynamicWave.cpp:2932`)
while all producers accumulate positive dqdh and the branch's own comment defines it as
d(net outflow)/dH ≥ 0 — Crank–Nicolson algebra says `+`. Until ruled, the conductance
scatter is gated to `node_continuity == EXPLICIT` (the default).

**5.5 Outfalls unchanged:** live `head_2d`/`ramp_2d` blend per Picard iteration
(`Outfall.cpp:335-347`); outfall discharge → 2D becomes a constant-rate source over the
subcycle, same ledgers.

**5.6 CFL hint: removed, replaced by nothing.** Gate the clamp at `SWMMEngine.cpp:926-932`
off on the marcher path. The hint existed to protect window-frozen exchange by shortening
the 1D step (measured cost: 1.55 s → 0.22 s avg 1D step). Windowless evaluation + limiter +
conductance own that stability now; the 1D returns to its own adaptive CFL.

**5.7 Exchange-area default derived, not constant.** Phase 0 makes the parsed coupling-row
area distinguishable (absent → sentinel −1; today the engine default 1.0 is filled at parse
and explicit-vs-default is lost). At coupling-point resolve: absent or
`[2D_OPTIONS] COUPLING_AREA AUTO` ⇒ `A = clamp(1.25 × largest connected conduit full area,
0.05, 2.0) m²`. Explicit .inp values honored. Bellinge acceptance runs AUTO (781/1,020
nodes currently sit at >10× conduit area from the GUI's 2.0 m² default). GUI follow-up
(separate repo, one line): stop writing `kCellCouplingDefaultArea = 2.0`.

## 6. Deleted / carried / untouched

- **Deleted now:** dead `computeCouplingExchange` + `transferOutfallDischarges`
  (`NodeCoupling.cpp:370-587, :689-750` — zero callers, −390 LOC); function-local static
  env caches (flux-eps/tangent-clamp/precond-tangent) → per-run options fields.
- **Bypassed on the marcher path, retained for CVODE until D2 resolves:** the entire
  window state machine (`fireAdvanceWindow` :866-1324), claw-back, partial-carry, lag
  leash, clock-resync, CFL hint, `ActiveSetBuilder`.
- **Carried (solver-agnostic):** section parsers + units prescan, `MeshBuilder` SoA
  geometry (+ unique-face normal-distance extension), `RainfallInterpolator` (sparse
  per-cell gage weights), `VfrClosure.hpp`, coupling-point resolution + median-dual
  ponded-area override, orifice law, volume ledger, mass-balance accounting, HDF5/UGRID
  output, `OPENSWMM_PERF` + report-stats spine (new marcher stats block), `InertialEdges`.
- **Untouched:** all 1D hydraulics except the two named touch points; CVODE/ARKODE/GPU
  solver internals (except two Phase-0 safety fixes and the bit-identical closure hoist).

## 7. Implementation map (verified against code)

New (~2,000 LOC + ~1,370 tests):

| File | Responsibility | LOC |
|---|---|---|
| `src/engine/2d/solver/InertialKernels.hpp` | Single-source flat kernels: face flow depth, inertial face update (θ + gravity + semi-implicit friction), Froude clamp, positivity scale, FLAT/VFR closure dispatch, Perot reconstruction. Raw-pointer args, no std:: in kernels — Kokkos-annotatable (R4). Also de-duplicates `reconstructFromVolume` (cloned at `CvodeSurfaceSolver.cpp:65`, `ArkodeSurfaceSolver.cpp:47`) — both solvers include it, bit-identical. | ~320 |
| `src/engine/2d/solver/ExplicitInertialSolver.{hpp,cpp}` | `ISurfaceSolver` impl (all virtuals incl. `run_stats()`, `last_coupling_exchange()`): tier scheduler + halving-order marching, flux-active set (frontier-incremental), inbox accumulators, per-substep exchange + limiter, telemetry (substeps, active-fraction series, tier occupancy, min-dt cell). `RunStats` mapping: nsteps=substeps, netfails/nncfails=0 structurally. | ~250 + ~1,100 |
| `tests/unit/engine/test_2d_inertial_marcher.cpp` | Analytic + property suite (§8.1–8.2). | ~450 |
| `tests/unit/engine/test_2d_lts_equivalence.cpp` | LTS gates + multiscale gates (§8.3). | ~300 |
| `tests/unit/engine/test_2d_windowless_coupling.cpp` | Coupling gates (§8.4). | ~400 |
| `tools/bench_2d/` (`run_one.py`, `run_bellinge.sh`, `README.md`) | Vendored harness, staged probes, env pinning, CSV + telemetry capture. Artifacts in `tools/bench_2d/out/` (repo-visible; no temp dirs). | ~220 |

Modified:

| File | Change | Δ |
|---|---|---|
| `solver/InertialEdges.{hpp,cpp}` | Add per-face `inv_dx_normal`, `n2_face`, per-cell `L_char`; existing layout untouched (ARKODE path unaffected). | +70 |
| `data/SolverOptions2D.hpp` | `IntegratorType::EXPLICIT_LTS`; fields: theta 0.8, cfl_number 0.7, h_move 0.003, lts_tiers 4, froude_max 1.5, exchange_beta 0.8, exchange_relax off, coupling_area_auto; env-cache replacement fields. | +60 |
| `input/SectionHandlers2D.cpp` | Parse/serialize `INTEGRATOR` (CVODE\|ARKODE\|EXPLICIT), `MOMENTUM`, `THETA`, `CFL_NUMBER`, `H_MOVE`, `LTS_TIERS`, `FROUDE_MAX`, `COUPLING_AREA`; area-token-absent sentinel. (`INTEGRATOR`/`MOMENTUM` are env-only today despite doc comments.) | +130 |
| `solver/SurfaceSolverFactory.cpp` | `EXPLICIT` branch ahead of the inertial-ARKODE block (:205): CPU-only, skip plugin discovery, implies inertial momentum. | +40 |
| `SurfaceRouter2D.{cpp,hpp}` | `advancePostRouting` (:760-824) branches on `usesCoAdvance()`: rainfall update + BC resolve + `advance(t, t+dt_routing)` + ledger booking + mass balance per routing step; vertex/output reconstruction at REPORT cadence only; `computeCflHint` → +∞ on marcher path; area derivation in coupling-point resolve (:419-514). Window machinery bypassed, not deleted. | +200 |
| `coupling/NodeCoupling.{cpp,hpp}` | Add `computeNodeCouplingDQdh1d()` beside `computeNodeCouplingQ` (reuses `orificePhi`/`effectiveArea`); delete dead pair. | +80/−390 |
| `core/SWMMEngine.cpp` | CFL-hint gate (:926-932); conductance scatter in the per-iteration non-conduit lambda (:2661 pattern). `assembleLateralInflows` untouched. | +45 |
| CMake (2d + tests) | Register new sources/tests. | +15 |

## 8. Test program (new, for the new engine)

**8.1 Analytic (gating):** lake-at-rest, uneven bed, FLAT & VFR — every q bit-zero and ΣV
bit-unchanged after 1,000 substeps; dry-wall C-property — puddle below sill leaks zero
(bit) over 10 sim-minutes; Manning steady uniform slope — q vs h^{5/3}√S/n within 1 %;
positivity — pathological drawdown keeps V ≥ 0 with exact conservation under λ-scaling;
steep-face limiter — 10 % slope dam-break-onto-street: no face sign-alternates >3
consecutive steps, |u| ≤ Fr_max·√(gh_f). Non-gating characterization: dam-break vs Ritter.

**8.2 Property (machine-exact conservation):** ΣV + boundary + exchange ledger closes
≤ 1e-10 relative under every combination of {positivity active, wet/dry cycling, LTS on/off,
active sets on/off, coupling on/off}; LTS ≡ global-dt: probe max|Δη| ≤ 5 mm; inbox
bit-test — coarse cell receives exactly the FP-sum of fine-face increments.

**8.3 Multiscale gates (R3):** (a) graded meshes at 10²:1 and 10⁴:1 cell-area ratio —
stable, conservative, wall cost ≤ 2× the same-count uniform mesh (cost ∝ own-rate proof);
(b) fine-urban-patch-in-coarse-watershed (~2 m patch in ~200 m mesh — the configuration
that diverged the old prototype): stable, cost dominated by the patch, K raised as needed.

**8.4 Coupling gates:** conservation |1D received − 2D given| ≤ 1e-6 relative under
VARIABLE_STEP (contract of the existing conservation test, marcher variant); conductance
damping — hover-at-crown fixture shows strictly smaller node-depth oscillation and
exchange sign-flip count vs conductance-off; limiter — near-dry drain never books more
than the cell held; 1D convergence non-degradation — Picard trials/step ≤ baseline + 20 %;
no sustained exchange sign-alternation (< 5 % of steps at any node in any 10-min window).

**8.5 A/B parity vs CVODE reference** (different momentum physics — gates target regimes
where they must agree): weir-overtopping + road-crown fixtures (subcritical): steady probe
depths max|Δη| ≤ 2 cm, overtopping onset within 15 %; Bellinge 30-min coupled slice,
20 probes (streets/ponds, Fr < 1): RMS|Δη| ≤ 2 cm, max ≤ 10 cm, exchanged volume within
5 %. Supercritical faces excluded from parity claims.

**8.6 Industry benchmarks:** UK EA 2D benchmark subset — tests 1, 2, 4 (speed), 8A
(rain-on-grid urban) — pass = within published inter-model spread. Scheduled inside P4 as
capacity allows; full suite in P5.

**8.7 Bellinge acceptance ladder** (serial pinned: `OPENSWMM_2D_BACKEND=cpu`, `THREADS 1`,
`INTEGRATOR EXPLICIT`, `COUPLING_AREA AUTO`; harness asserts all pins): 30-min slice →
8 h probe → 48 h full. **Final gate: 48 h coupled < 300 s wall, 1D flow continuity < 0.5 %,
2D mass-balance closure < 0.5 % (target 0.1 %), zero aborts/NaN/negative-V (bit-assert).**

**8.8 Legacy policy:** existing `test_2d_*` suite stays green on the untouched CVODE path
(guards shared infrastructure); marcher variants parameterize the conservation/junction/
outfall/decoupled-stepping/surface-routing fixtures over `INTEGRATOR`.

## 9. Phases & gates

- **P0 — Branch + harness + de-mining (0.5–1 d):** tag baseline, cut
  `feat/2d-explicit-marcher`; vendor `tools/bench_2d/`; make `INTEGRATOR`/`MOMENTUM` real
  `[2D_OPTIONS]` keys; kill static env caches; GPU-plugin closure guard (warn + CPU
  fallback on `CELL_CLOSURE` mismatch — today it silently substitutes VFR,
  `KokkosSurfaceKernels.hpp:377`); libomp @rpath deploy fix (acceptance: `otool -L` clean,
  no duplicate-runtime abort); loud warning on `.inp` `MIN_TIMESTEP > 0` (CVODE path);
  coupling-area sentinel; delete dead coupling pair; record baselines (13k coupled
  reference + 228k 30-min slices). *Gate: legacy suite green; baseline CSV committed.*
- **P1 — Marcher core, solo 2D, global dt (3–5 d):** kernels + solver + factory/options;
  active set; positivity; rain lazy mode; BCs; stats. *Gate: §8.1 green; conservation
  ≤ 1e-10; §8.5 fixture parity; **mandatory telemetry: active-fraction vs time on the
  Bellinge 30-min storm slice — the load-bearing budget assumption (expected ≤ ~40 % at
  peak; equilibrium sheet-flow films ≈ 0.1–0.5 mm ≪ H_MOVE) verified here before tiers
  are judged.***
- **P2 — Tiered LTS (2–4 d):** halving-order scheduler, face-at-finer-tier rule, inbox,
  hysteresis. *Gate: §8.2 LTS gates; §8.3 multiscale gates; ≥ 3× wall over P1 on the storm
  slice; no tier-thrash (median migrations < 2/cell/100 syncs).*
- **P3 — Windowless coupling (3–5 d):** co-advance branch, substep exchange + limiter,
  ledger, conductance (EXPLICIT-gated), CFL-clamp removal, area derivation. *Gate: §8.4;
  coupled weir/road/road-culvert examples stable + conservative; CVODE path bit-unchanged.*
- **P4 — Bellinge acceptance (2–3 d):** ladder runs; levers (H_MOVE, K, α); profile.
  *Gate: §8.7 — the R2 target.* EA subset (§8.6) as capacity allows.
- **P5 — deferred:** Kokkos port note is the deliverable now (kernels already flat/
  deterministic; parallel order: per-tier face loop → per-cell gather → scan compaction);
  full EA suite; optional SWE flux (D1); CVODE retirement (D2).

## 10. Risks & contingencies

| Risk | Assessment | Mitigation / levers (in order) |
|---|---|---|
| Serial 48 h lands > 300 s | Projected 150–400 s | (1) H_MOVE 3→5 mm; (2) K 4→5 (re-run Gate-2); (3) α 0.7→0.9 with Froude clamp; (4) 16-cell tile blocking of the active set. **Fallback: the already-planned OpenMP/Kokkos phase closes any residual gap (owner accepted parallelism later; kernels are ready by construction) — subject to D3.** |
| Thin-film assumption fails (flat areas keep most cells flux-active in storm) | Biggest budget unknown | P1 telemetry decides *before* tiers are judged; thin cells are slow cells ⇒ coarse tiers absorb them — LTS is co-mandatory with active sets for exactly this case |
| LI limits: supercritical, small-depth oscillation | Known scheme property | θ-averaging + Froude clamp; §8.1 ringing gate; parity claims Fr < 1; CVODE cross-check always available; no division below h_wet |
| Per-substep exchange perturbs 1D Picard | Historical churn driver | Conductance (provably stabilizing) + availability limiter + ledger delivery + `EXCHANGE_RELAX` in reserve; §8.4 non-degradation gate |
| SEMI_IMPLICIT `sumdqdh` sign question | Pre-existing, non-default branch | Conductance gated to EXPLICIT until D5 ruling |
| Multiscale beyond tested range | R3 core | §8.3 gates at 10²/10⁴:1; K raisable to 8; face-normal distances + floor |
| GPU plugin pollutes serial benchmarks (auto-loads ≥ 20k cells) | Certain without pinning | Harness hard-pins `OPENSWMM_2D_BACKEND=cpu`; P0 closure guard warns loudly |
| Area derivation changes calibrated legacy models | Opt-in only | Explicit .inp values always honored; AUTO is deliberate on Bellinge |
| Rain double-counting (subcatchments AND rain-on-grid, same footprint) | Model-configuration question, not solver | Flagged to owner; engine honors the .inp |

## 11. Decisions (ruled by owner, 2026-07-29)

- **D1 Formulation — DECIDED: local-inertial explicit.** SWE flux remains a later drop-in
  behind the same kernels.
- **D2 Legacy CVODE stack — DECIDED: independent cross-check through P4, then retire**
  in a follow-up change (avoids dual-path divergence rot; cf. the GPU backend).
- **D3 If serial lands 300–450 s — DECIDED: accept `THREADS 4` interim**; the Kokkos/OpenMP
  phase closes the serial gap properly.
- **D4 `MIN_TIMESTEP` .inp trap (CVODE path):** loud warning (planned default; escalate to
  hard error only if it bites again).
- **D5 `DynamicWave.cpp:2932` SEMI_IMPLICIT denominator sign:** investigate/rule as a
  separate item; conductance stays EXPLICIT-gated meanwhile (planned default).

## 12. Explicitly out of scope

θ-core build-out; subgrid bathymetry tables; monolithic 1D–2D matrix coupling; mesh
generation/adaptation changes (the 228k mesh is healthy); GPU/Kokkos work beyond the two
P0 safety fixes and the P5 notes; 1D hydraulics beyond the two named touch points; GUI
changes beyond the exchange-area default note.
