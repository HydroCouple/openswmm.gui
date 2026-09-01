# HANDOFF — 2D Explicit Marcher: finish P4 + next work items (2026-07-29)

For the next agent. Full architecture/spec: `2D_SOLVER_REIMPLEMENTATION_PLAN_2026-07-29.md`
(same directory). Persistent context: memory file `2d-explicit-marcher-reattack.md`
(auto-memory) — read both before touching code. **Do NOT relitigate the architecture.**

## Ground rules (owner-set, non-negotiable)

- Engine repo: `~/Documents/Projects/cbuahin_github/openswmm.engine`, branch
  **`feat/2d-explicit-marcher`** (baseline tag `2d-baseline-2026-07-29` = f714abd6).
- Commit locally in small reversible pieces; **never push**; **no Claude/AI attribution
  lines in commit messages**; **never commit anything under `plans/`, `docs/`,
  `workplans/`, or loose benchmark artifacts** (only `tools/bench_2d/**` scripts +
  `out/**/results.csv` are committable).
- The full suite must stay green: `cd build/darwin-tests && ctest -j 6` → **110/110**.
  CVODE-path tests are bit-guards for shared infrastructure — if one breaks, your change
  leaked outside the marcher path.
- Serial benchmark discipline: every timing run pins `OPENSWMM_2D_BACKEND=cpu` and
  `THREADS 1` (the harness does this) — the ≥20k-cell Kokkos plugin auto-gate would
  otherwise contaminate results.
- Rebuild targets: `cmake --build build/darwin --target openswmm` (CLI),
  `cmake --build build/darwin-tests` (tests). Engine tree only; the GUI consumes installs.

## Current state (all committed on the branch)

| Datum | Value |
|---|---|
| CVODE baseline, coupled storm 30-min slice, 228,410-cell mesh, serial | 332.5 s wall |
| Marcher (full windowless stack), same slice | **10.2 s** (`[PERF] 2D=5.8s adv=4.65s, 1D=3.14s`) |
| Cross-domain conservation gate | 0.011 % measured (gate 0.05 %; window-path contract is 0.5 %) |
| Suite | 110/110 |
| 48 h formal acceptance run | **in flight** (see Task 1) |

Key files: `src/engine/2d/solver/{ExplicitInertialSolver.*,InertialKernels.hpp,InertialEdges.*}`,
`src/engine/2d/SurfaceRouter2D.cpp` (`coAdvanceStep`, `computeCouplingConductances`),
`src/engine/2d/coupling/NodeCoupling.cpp` (`computeNodeCouplingQ`, `computeNodeCouplingDQdh1d`),
`src/engine/core/SWMMEngine.cpp` (CFL-hint gate ~:926, conductance scatter in the
per-iteration lambda ~:2665). Harness: `tools/bench_2d/` (`run_one.py`, `make_slice.py`).

Bench invocation pattern (all artifacts under `tools/bench_2d/out/<dir>/`, reviewable):

```bash
cd tools/bench_2d
python3 make_slice.py ~/Downloads/7_SWMM/BellingeSWMM_v021_nopervious.inp \
    out/<dir>/<name>.inp --threads 1 --del2d MIN_TIMESTEP \
    --set2d INTEGRATOR=EXPLICIT --set2d LTS_TIERS=4 --set2d MAX_TIMESTEP=60 \
    --set2d COUPLING_AREA=AUTO [--start "06/29/2012 04:15" --hours 0.5]
python3 run_one.py <tag> out/<dir>/<name>.inp --outdir out/<dir>
```

## Task 1 — Record the P4 48 h acceptance result (first thing)

A formal 48 h serial run may still be running or just finished:
process `openswmm … p4_48h_acceptance…`; artifacts in
`tools/bench_2d/out/explicit_p3_20260729/` (`p4_48h_acceptance.{rpt,log,cverr}`,
row appended to `results.csv`). If it is not running and no row exists, relaunch with the
pattern above (no `--start/--hours` = full 48 h; tag `p4_48h_acceptance`).

Record in the summary you leave behind: wall s, `[PERF]` split (in `.cverr`), the
`2D Surface Routing Continuity` block, the `2D Solver Statistics` block, flow-routing
continuity, frozen windows (must be 0), any warnings. **Gate: < 300 s wall serial,
2D continuity ≤ 0.5 %, zero aborts/NaN.** Expected outcome based on progress sampling:
~35–50 min wall — OVER the gate, which activates Tasks 2 and 3 (in that order).

## Task 2 — The 1D step-shrink regression (highest-leverage item)

**Observed:** on the co-advance path the 1D averages ~0.95–1.6 s steps; on the old
clamped window path the same model averaged ~3.2 s. Removing the 2D CFL clamp should have
LENGTHENED 1D steps — something in the live-exchange dynamics is shrinking them. At ~90k
routing steps per simulated day, per-step 1D+engine overhead now dominates the whole run;
calming this is worth ~2–3× end-to-end.

Investigate (in order):
1. **Which constraint governs dt?** Instrument `Router::getAdaptiveStep` /
   `hydraulics::TimestepController::compute_next` (env-gated stderr, e.g.
   `OPENSWMM_DT_TRACE=1`, print governing link/node + dt each step for a 30-min slice).
   Compare a co-advance run vs a window-path run (`INTEGRATOR=CVODE`) on the same slice.
2. **Candidates:**
   a. Link Courant spikes in conduits adjacent to coupled nodes (live exchange raises
      local velocity/depth → variable-step CFL shrinks). Check whether governing links
      cluster at the 1,020 coupled nodes.
   b. The queue-spread delivery (`coupling_delivery_remaining`) producing sustained
      lateral inflow that keeps velocities elevated vs the old pulsed pattern.
   c. The conductance term changing node-depth trajectories (verify by disabling the
      scatter — add env `OPENSWMM_2D_NO_CONDUCTANCE=1` around the block in
      `SWMMEngine.cpp` — measure; delete the env before committing if not needed).
3. **Levers to A/B** (one at a time, storm slice, record in results.csv):
   sync batch length (currently `clamp(MAX_TIMESTEP, routing_step, 60)` — try 15/30 s);
   `EXCHANGE_RELAX` (EMA sub-relaxation, plumbed in `SolverOptions2D`, default off);
   delivery span (currently `max(remaining, dt)`).
4. Whatever fix lands must keep the windowless-coupling gates green
   (`ctest -R windowless`) and cross-domain conservation ≤ 0.05 %.

## Task 3 — THREADS 4 interim (owner ruling D3)

After the serial record exists: same 48 h input with `--threads 4` (make_slice flag),
tag `p4_48h_t4`. The marcher's loops are race-free/deterministic; expect near-linear
scaling on the 2D share, ~1.5–2× on the 1D (barrier-bound). Record wall + `[PERF]`.
Owner accepts this as the interim path to <5 min while P5 (Kokkos) matures.

## Task 4 — Polish queue (small, independent; commit each separately)

1. **Closure de-dup swap:** `CvodeSurfaceSolver.cpp:~65` and `ArkodeSurfaceSolver.cpp:~47`
   carry local copies of `reconstructFromVolume/volumeFromHead`; the canonical pair now
   lives in `InertialKernels.hpp` (`cellEtaDepth`/`cellVolumeFromEta`). Swap both solvers
   to include the kernels header. MUST be bit-identical: full suite + one
   `INTEGRATOR=CVODE` storm-slice rerun comparing the `.rpt` byte-for-byte (modulo title
   timestamps).
2. **Coupled-ledger reporting:** the flow-routing "continuity error" (~25 %) on coupled
   runs is a CLASSIFICATION artifact — spill books as Flooding Loss, drain returns as
   external inflow, so the loop double-counts. Add a small "1D↔2D Exchange" reconciliation
   block to the report (spill, drain, net, and flow continuity recomputed with exchange as
   internal transfer). Report-plugin-only change (`DefaultReportPlugin.cpp`); no solver
   edits.
3. **Marcher telemetry into RunStats:** tier occupancy histogram + active-fraction
   min/mean/max at finalize into the `2D Solver Statistics` block (fields exist as
   telemetry_ internally; extend the report block guardedly).
4. **`test_2d_decoupled_stepping` / `test_2d_junction_coupling` marcher variants:**
   parameterize those two fixtures over `INTEGRATOR` like the conservation test was.

## Task 5 — Definition-of-done packages for the owner (documents only, do not commit)

Write into THIS workplans directory:
- **EA benchmark subset plan** (tests 1, 2, 4, 8A): model sources, mesh generation via
  the harness, pass = within published inter-model spread.
- **P5 Kokkos port note** (one page): kernel annotation list, per-tier `parallel_for`
  structure, device-resident active-set compaction via scan, host↔device sync only at
  co-advance batch boundaries, new `openswmm_make_gpu_explicit_solver` ABI entry
  mirroring the inertial one (`GpuPluginAbi.h`).
- **CVODE retirement package (owner decision D2):** what deletes (window state machine,
  claw-back/partial-carry, ARKODE-IMEX path?), what stays (hypre for future θ-backend?),
  LOC counts, test impact.
- **D5 ruling package:** the `DynamicWave.cpp:2932` SEMI_IMPLICIT `sumdqdh` sign analysis
  (producers accumulate ≥0; CN algebra says `+`; one-character candidate fix + which
  tests would gate it).

## Traps that already burned time (do not re-learn)

1. Faces flow only when BOTH incident cells are active (one-sided faces destroy volume).
2. Face positivity budget divides by the refire ratio `2^(k_exp − k_face)`.
3. `settleAccumulators()` before ANY tier/active reassignment (stranded counterparted
   flux = volume creation).
4. `accumulateMassBalance` reads `coupling_volume` — it must run BEFORE the queue move.
5. Per-routing-step marcher advances degenerate to the settle+retier tail (a ~1 s step
   is smaller than an LTS macro cycle) — keep the sync-batch structure.
6. `MAX_TIMESTEP` caps film-cell CFL steps and thus the LTS tier spread — Bellinge runs
   use 60.
7. `grep -cE` exits 1 on zero matches — do not use it as a success gate in `&&` chains.
8. The `2d_complete_example` fixture must run from its own directory (relative sidecars).
9. Sliver meshes (aspect ≳10:1) are outside the scheme's mesh contract — don't "fix" the
   solver for them; fix the fixture.


---

## ADDENDUM (post-acceptance, 2026-07-29): formal 48 h record + CORRECTED Task 2

**Formal record (`p4_48h_acceptance`):** wall **3,457 s** (57.6 min) serial; zero frozen
windows/failures; 2D continuity −0.000 %; flow continuity −0.26 %; runoff −0.344 %.
`[PERF] 2D-window=3191 s (advance 3078.6, overhead 112.5) 1D-step=249.4 s`.
Marcher: 975,928 substeps, avg dt 0.177 s, 45.46e9 face evals (~46.6k active faces/substep).

**The earlier "1D dominates" diagnosis was WRONG** (it was inferred from routing-step
counts without a PERF split). The split shows the 2D marcher at 89 % of wall. Two coupled
pathologies explain both the cost and the hydrology:

1. **Batch-lag exchange oscillator.** Gross exchange churned ~748M m³ spill / ~744M m³
   drain (≈ 300× the physical event volume; 1D "Flooding Loss" 746.6M m³ is the same
   loop). The 60 s sync batches mean the 2D sees 1D heads up to 60 s stale and the 1D
   sees queue-spread inflows — a classic staggered-lag oscillation at whatever amplitude
   the caps permit. The per-Picard conductance damps iteration churn but NOT this
   batch-period mode. Net effect: coupling cells hold deep standing ponds all run.
2. **dt0 pinned at ~0.18 s** by those same deep, small, tier-0-pinned coupling cells
   (CFL + Froude-capped speed term), keeping ~47k faces firing every 0.18 s for 48 h.
   Fix the oscillator and dt0 recovers toward 1–3 s ⇒ the 2D bill collapses.

**Task 2 (REVISED, replaces the 1D-step framing):** kill the batch-lag oscillator.
Levers in order:
 a. Drain cap must arm PER BATCH, not per substep (currently β·V/dt_c re-arms every
    0.18 s ⇒ effectively unlimited): give drains the same advance-scoped ledger spills
    already have (extend node_drawn_ to a signed per-point budget).
 b. Shorter sync batches while exchange is active (adaptive: batch = 60 s quiescent,
    5–10 s when |exch| above a threshold), or interpolate the 1D head across the batch
    (linear from previous batch) so the marcher's orifice sees a moving target.
 c. `EXCHANGE_RELAX` EMA on per-point Q (plumbed, default off) — measure 0.5/0.2.
 d. Verify with: gross spill+drain ≤ ~5× physical volumes on the 30-min storm slice;
    2D final storage plausible (≪ 1M m³); wall; then rerun 48 h.
Keep: conservation gates green (`ctest -R windowless`), suite 110/110.

**Meaning for the gates:** with the oscillator fixed, the marcher's own arithmetic
(45.5e9 → ~3–5e9 face evals) puts serial ≈ 6–10 min, and THREADS 4 (D3) covers the rest
to < 5 min. The architecture verdict stands: zero failures, exact ledgers, CVODE same
mesh = hours-scale.

**Task 2 item (a0) — FIRST, before the oscillator: the 42× rainfall inflation.**
The 48 h run booked **Rainfall Inflow = 110.4M m³** onto the 2D mesh while the
subcatchment side saw the physically-correct 2.62M m³ of wet-weather runoff for the same
sky. 110M ≈ storm-PEAK intensity held for the entire 48 h. Both storm-slice runs (window
and co-advance) applied ~1.13–1.19M m³/30 min correctly — the inflation manifests only
across the long run, i.e. the co-advance rain refresh is holding a stale nonzero gage
value through the dry hours, or applying a wrong-cadence product. Reproduce cheaply: a
2 h PRE-storm slice (`--start "06/29/2012 00:01" --hours 2`) must book near-zero 2D
rainfall — if it books ~4.6M m³ (peak-rate-held), the bug is confirmed. Prime suspects:
`SurfaceRouter2D::coAdvanceStep` forcing-cadence block (`co_forcing_elapsed_`, 30 s
refresh; check `updateRainfall(ctx)` actually re-reads gage CURRENT values at batch time
and that a zero gage value OVERWRITES the held array), and the final-batch flush in
`finalize`. Note: the 110M is REAL water in cells (final stored 114.9M m³ = ~3.3 m over
the whole catchment) — it also feeds the exchange oscillator (deep ponds at coupling
cells pin dt0 ≈ 0.18 s), so fixing (a0) may collapse most of (1) and (2). Re-run the 48 h
after (a0) BEFORE investing in the oscillator levers.
