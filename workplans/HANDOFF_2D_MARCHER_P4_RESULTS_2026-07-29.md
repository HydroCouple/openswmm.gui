# P4 Results + The ×100 Rainfall Finding — 2026-07-29 (session 2)

Continues `HANDOFF_2D_MARCHER_P4_NEXT_2026-07-29.md`. Spec:
`2D_SOLVER_REIMPLEMENTATION_PLAN_2026-07-29.md`. Engine branch
`feat/2d-explicit-marcher`; this session's commits: `882b5a16` (closure de-dup,
bit-verified), `12528e8e` (marcher telemetry in report), `d1e74078` (exchange
reconciliation block), `575ba409` (marcher test variants), `bf568831`
(OPENSWMM_DT_TRACE + analyzer). Suite **110/110** after every commit.
(Note: `6958fe54` geopackage/MSVC landed mid-session from a parallel workstream.)

## 1. Task 1 — formal 48 h serial acceptance (RECORDED)

`p4_48h_acceptance`, 228,410-cell coupled Bellinge, serial, EXPLICIT/LTS 4/
MAX_TIMESTEP 60/COUPLING_AREA AUTO. Artifacts:
`tools/bench_2d/out/explicit_p3_20260729/`, row in `results.csv`.

| Metric | Value | Gate |
|---|---|---|
| Wall | **3,457 s** (57.6 min) | **< 300 s — FAILED (11.5×)** |
| `[PERF]` | 2D-window 3,191 s (advance 3,078.6 + overhead 112.5), 1D 249.4 s | — |
| Routing steps | 155,596 (avg 1.11 s; min 0.20; 45.95 % of steps in the 0.20–0.36 s bin) | — |
| 1D convergence | **95.26 % of steps NOT converging**, 19.4 avg iterations | — |
| Marcher | 975,928 substeps (avg 0.177 s); 45.5×10⁹ face-firings (~14.8 M faces/s) | — |
| Flow routing continuity | −0.260 % | ✓ |
| 2D continuity | −0.000 % | ✓ (≤ 0.5 %) |
| Frozen windows / aborts / NaN | 0 / 0 / 0 | ✓ |
| Gross exchange | spill 748.0M m³, drain 744.1M m³ (net +3.9M) vs 110.4M m³ rain | — |
| Warnings | 160 outfall-clamp windows; stale `COUPLING_INTERVAL` warning (inert on co-advance); model WARNING 04s | — |

The 25 % coupled-slice flow-continuity artifact vanishes over 48 h (−0.26 %) —
consistent with the classification explanation (spill/drain nearly cancel over the
full event). The interesting failure is WHY the run is slow — see §2.

## 2. Task 2 — the step-shrink investigation: root cause is the SCENARIO

**Finding 1 — the benchmark runs 100× rainfall.** The .inp's [RAINGAGES] FILE lines
end `... rg5425 MM * 100`: the engine parses the trailing token as a **rainfall scale
factor** (engine extension; `CatchmentHandler.cpp` STAN_PRCP branch, consumed at
`Gage.cpp:48` — GUI exposes it as gage "Rainfall Scale Factor"). Measured result:
**3,059 mm of rain in 48 h** (the unscaled file holds a ~30.6 mm event). Legacy SWMM
**ignores** the token, so legacy comparisons on this .inp silently run the physical
storm. Downstream consequences, all verified in the acceptance .rpt: ~3.3 m mean
standing water over the full 34.9 km² at t=48 h (2D final storage 114.9M m³), 12–24 m
max ponded HGLs at hundreds of coupled nodes, 30–40 CMS sustained per-node flooding,
and the 748/744M m³ gross exchange churn (~6.8× the rain volume cycling 1D↔2D).

**Finding 2 — there is NO co-advance step-shrink regression.** Same-slice A/B with the
new `OPENSWMM_DT_TRACE` (storm 30-min slice, `out/task2_dtshrink_20260729/`):

| Path | wall | 1D dt mean / median | governor |
|---|---|---|---|
| co-advance (EXPLICIT) | 10.1 s | 0.897 / 0.395 s | link Courant 83.5 %, floor 10.6 % |
| window path (CVODE) | 421.4 s | 0.946 / 0.383 s | link Courant 87.1 %, floor 6.5 % |
| co-advance @ **SCF=1** | **2.0 s** | **2.481 / 2.181 s** | link 86.8 %, floor 0 % |

Identical dt distributions on both paths at ×100; healthy steps at ×1. The handoff's
"~3.2 s window-path average" premise came from a different context; on equal inputs
the shrink is the storm, not the coupling rewrite. Top governors are 3 links
(G60F60Y_G60O220_l1, G72F061_G72K060_l1, G72F095_G72F090_l1 — 54 % of steps
combined), all beside heavy-exchange nodes. Analyzer:
`tools/bench_2d/dt_trace_summary.py TRACE.cverr model.inp mesh.2dm`.

**Finding 3 — the handoff's levers are inert.** A/B on the storm slice: conductance
disabled 9.1 s / dt median 0.40 (unchanged) — env toggle deleted after measurement per
instructions, conductance stays; sync batch 15 s → 13.3 s (worse), 30 s → 10.9 s
(neutral), dt distribution unchanged in every case. `EXCHANGE_RELAX` remains an
unwired options field — not worth wiring until the scenario question is ruled.

**Finding 4 — at ×100 the true bottleneck is the 2D advance, not the 1D.** 89 % of the
48 h wall is the marcher (3,079 s): under 3 m of standing water the mesh stays
60–100 % flux-active for the whole run (45.5G face-firings ≈ 46.6k faces/substep
average), and the storm-slice telemetry (new report rows) shows tier-3 holding 83.9 %
of assignments — LTS is working; there is simply no quiescent tail to skip at ×100.
A 4 h storm+tail slice (`storm4h`, 275.8 s) reproduces the full-run cost shape and is
the fast testbed for any tail-cost work.

## 3. Task 3 — THREADS 4 interim (D3)

`p4_48h_t4` (same 48 h input, `--threads 4`): **1,418 s wall = 2.44× over serial.**
`[PERF] 2D-window=1247.9 s (advance 1208.0) 1D-step=155.2 s` — 2.55× on the 2D
share, 1.61× on the barrier-bound 1D, matching the owner-briefed expectation.
Deterministic: identical substep count (975,928) and continuity string
(−0.344; −0.260; −0.000) to the serial run. Still over 300 s at ×100 rain;
at the D3 ruling's 300–450 s serial window a 2.4× would have closed the gap —
the scenario ruling (D6) decides whether that matters.

## 4. The 48 h probe at physical rainfall (SCF=1)

`bellinge_48h_scf1` (identical input, gage scale 100→1, serial): **853 s wall — 4.05×
faster than ×100, but the R2 gate is NOT yet met at physical rainfall either.**
`[PERF] 2D=661.7 s (advance 530.5, overhead 131.2) 1D=176.7 s`; 2D rainfall books
1.105M m³ = exactly 110.4M/100 (definitively kills the "co-advance rain-cadence
inflation" hypothesis — booking is proportional and correct). What the physical run
exposes, in priority order for the next session:

1. **2D per-batch overhead is now 20 % of the 2D bill** (131 s) — the O(nt)
   settle/retier/forcing work per 60 s batch no longer hides behind advance cost.
2. **The active set does not collapse in the tail:** mean 27 %, tail plateau
   ~22–31 % for a 31 mm event that should leave the mesh mostly dry — H_MOVE
   hysteresis / film retention lever, worth ~2× alone if the tail can reach ~5 %.
3. **Relative conservation degrades at small denominators:** 2D continuity
   **−1.866 %** (absolute ~51k m³ — FAILS the ≤0.5 % gate that ×100 masked),
   routing −2.03 %, exchange-internal recompute −12.95 %, 60 % of 1D steps
   non-converging. The same absolute residuals were invisible at ×100
   (−0.000 %/−0.26 %). Gross exchange is now sane (spill 918k / drain 960k m³
   ≈ 0.9× rainfall, vs 6.8× at ×100), but outfall→2D inflow (714k m³) is a
   surprisingly large term worth auditing (recall the 160 outfall-clamp warnings).

So neither scenario currently passes R2 end-to-end: ×100 passes conservation but is
11.5× over on wall; ×1 is 2.8× over on wall and fails the 2D continuity gate. The
×1 gaps look like ordinary engineering (overhead trim + active-set tail + small-
denominator conservation audit), not architecture.

## 5. Task 4 polish queue — ALL LANDED

1. **Closure de-dup** (`882b5a16`): CVODE/ARKODE forward to `InertialKernels`
   `cellEtaDepth`/`cellVolumeFromEta`. Verified bit-identical: suite green + CVODE
   storm-slice rerun — filtered .rpt AND binary .out byte-equal (`cvode_trace` vs
   `cvode_noswap`).
2. **Exchange reconciliation report block** (`d1e74078`): gross spill/drain/net +
   flow continuity recomputed with exchange internal. Prints only on coupled runs.
   Caveat for readers: on short slices the adjusted error can EXCEED the raw one —
   queued-but-undelivered exchange at slice end dominates the smaller adjusted
   denominator; over the full 48 h both converge (raw −0.26 %).
3. **Marcher telemetry in report** (`12528e8e`): active-fraction min/mean/max +
   LTS tier-occupancy histogram in "2D Solver Statistics" (guarded, marcher-only).
4. **Marcher test variants** (`575ba409`): junction-coupling spill/recapture and
   decoupled-stepping conservation re-asserted under `INTEGRATOR EXPLICIT`
   (positivity upgrades the storage floor to ≥ 0). Both pass.

## 6. Task 5 — DoD packages (documents only, in this directory)

- `EA_BENCHMARK_SUBSET_PLAN_2026-07-29.md` (tests 1/2/4/8A, SC120002 spread gates)
- `P5_KOKKOS_PORT_NOTE_2026-07-29.md` (annotation list, per-tier parallel_for, scan
  compaction, batch-boundary sync, `openswmm_make_gpu_explicit_solver` ABI)
- `CVODE_RETIREMENT_PACKAGE_D2_2026-07-29.md` (deletes ≈4.6k LOC, hypre/sundials
  recommendation, test impact, sequencing after P5-OpenMP)
- `D5_SEMI_IMPLICIT_SUMDQDH_SIGN_2026-07-29.md` (CN algebra says `+`; one-char fix;
  gating tests; recommendation)

## 7. OWNER DECISIONS NEEDED (new)

- **D6 — what storm defines R2?** The <300 s gate has always been priced on the
  ×100 rain bomb. Options: (a) rule the gate at SCF=1 (physical event) — the gap
  is then 2.8× on wall with three identified, ordinary engineering levers (§4),
  plus a conservation audit the small denominators now demand; (b) keep the gate
  at ×100 — then the remaining work is tail-cost engineering under 3 m of
  standing water plus P5 parallelism, and 300 s serial is 11.5× away.
  Recommendation: (a), with ×100 retained as a robustness case (its conservation
  is exemplary) under a relaxed wall budget or THREADS 4 (1,418 s).
- **D7 — engine warning for gage scale factors ≠ 1?** Legacy silently ignores the
  token; we honor it. A loud once-per-run warning ("rainfall scaled ×100 by gage
  SCF token — legacy ignores this") would have surfaced this months earlier.

## 8. Traps (additions to the standing list)

10. The [RAINGAGES] FILE trailing token is a rainfall multiplier in THIS engine and
    a no-op in legacy — check it before trusting any cross-engine or perf number.
11. `run_one.py`'s `steps`/`avg_internal_step_s` columns are the 2D solver's
    internal counters, not 1D routing steps — use the .log step counter or a DT
    trace for the 1D.
12. Background-launching `run_one.py` from another cwd: the harness `cd` persists
    per shell call only; use absolute paths (one t4 launch failed on this).

---

## 8. P5 Phase A — Kokkos marcher landed (same day, second session)

Owner directed immediate P5 implementation (threading + GPU support incl.
CUDA/AMD). Landed on `feat/2d-explicit-marcher` (commits `5068e39d`,
`ea98a201`, `0a0b23d4`, `3e2da4a0`), suite **111/111**:

- **Single-source kernels:** `InertialKernels.hpp` + `VfrClosure.hpp` scalar
  bodies annotated via `OPENSWMM_KERNEL_FN` (host = `inline`; plugin =
  `KOKKOS_INLINE_FUNCTION`). No math duplication; bit-inert on host paths.
- **`ExplicitKokkosSurfaceSolver`** (gpu/): device-resident port — per-tier
  `parallel_for` face/cell sweeps, `parallel_scan` active-set/tier compaction,
  exact-min dt0 reduction, single-thread device kernels for the order-dependent
  BC + live-exchange passes (serial semantics preserved exactly). Host↔device
  traffic only at co-advance batch boundaries. Implements BOTH closures — the
  VFR-only plugin restriction does NOT apply to the marcher path.
- **ABI:** optional symbol `openswmm_make_gpu_explicit_solver` exported from
  all four backend entries (omp/cuda/hip/sycl) — no ABI version bump; old
  plugins simply lack the symbol and the factory falls back to the serial
  marcher. Factory honors `OPENSWMM_2D_BACKEND` + the ≥20k-cell gate.
- **Verified (OpenMP backend, this Mac):** Bellinge 228k storm slice `.out`
  **byte-identical** to the serial marcher at 1/4/8 threads; 48 h SCF=1
  `.out` **byte-identical**, identical substep count (230,108) and
  continuities. Determinism is by construction (disjoint writes, exact-min
  reductions) and now measured.
- **Speed (OpenMP):** storm-slice 2D advance 4.27→2.24 s (T8); 48 h SCF=1
  **715 s vs 853 s serial** (2D advance 530→407 s). Scaling is
  **kernel-launch-granularity-bound** at ~0.75 s substeps (~2M+ small
  launches/run) — NOT memory- or algorithm-bound. Next P5 levers, in order:
  (1) fuse the per-tier fire loops into one launch per substep;
  (2) batch tier-0 BC/exchange into the cell kernel; (3) device backends.
- **Unverified (no device on this host):** CUDA/HIP/SYCL builds of the new
  solver compile-plumbed in all backend targets but never compiled or run
  against a real device — needs a CUDA/ROCm host or CI. `std::` math inside
  the annotated kernels is expected to resolve to device overloads under
  nvcc/hipcc (Kokkos relaxed-constexpr); verify on first device build.
- **New gate:** `test_engine_2d_omp_explicit` — INTEGRATOR EXPLICIT fixture
  must load the omp plugin under FLAT closure.
