# D2 — CVODE Stack Retirement Package (owner decision)

**Status:** DoD package for owner review (Task 5, 2026-07-29). Not committed.
**Owner ruling D2 (2026-07-29):** CVODE stays as independent cross-check through P4
acceptance, then retires in a follow-up change. This package enumerates exactly what
that follow-up deletes, what stays, and the test impact. Nothing is lost permanently —
the branch history and the `2d-baseline-2026-07-29` tag preserve every line.

## 1. What deletes (≈ 4,600 LOC engine + ≈ 1,000 LOC tests)

| Component | Files / location | LOC | Notes |
|---|---|---|---|
| CVODE BDF solver | `solver/CvodeSurfaceSolver.{cpp,hpp}` | 1,341 | Newton + GMRES + reinit machinery |
| ARKODE-IMEX inertial path | `solver/ArkodeSurfaceSolver.{cpp,hpp}` | 1,009 | Dead end (diverged in held-forcing windows); its semi-implicit friction form already lives on in `InertialKernels.hpp` |
| Analytic Jacobian | `solver/SurfaceJacobian.{cpp,hpp}` | 206 | CVODE-only |
| Tangent-exact preconditioner | `solver/SurfaceTangent.{cpp,hpp}` | 458 | CVODE-only (the 7.7× July win — recorded in results.csv history) |
| ydot-masking active set | `solver/ActiveSetBuilder.{cpp,hpp}` | 234 | Entangled with window-breach semantics; the marcher has its own frontier-incremental set |
| Window state machine | `SurfaceRouter2D.cpp` `fireAdvanceWindow` (~:1093–1560) + per-step decoupled-exchange branch + failure redelivery | ~640 | Frozen windows, catch-up leash, effective-window recovery |
| Claw-back / partial-carry | `SurfaceRouter2D.{cpp,hpp}` (`partial_carry_*`, `window_*_accum_` window-path uses) | ~120 | The delivery QUEUE itself **stays** (the marcher books batch volumes through it) |
| 2D CFL hint | `SurfaceRouter2D::computeCflHint` + the gate branch in `SWMMEngine.cpp:926-937` | ~40 | Whole concept retires with windows |
| DW face-flux RHS | `solver/SurfaceFluxCalculator.{cpp,hpp}` — **partial** | ~500 of 791 | The CVODE RHS kernels go; `evapSink` + `computeBoundaryEdgeFlux` are reused by the marcher (`ExplicitInertialSolver.cpp:39`) and move to a slim shared header or stay in a trimmed file |
| Factory branches | `SurfaceSolverFactory.cpp` CVODE/ARKODE arms | ~80 | `EXPLICIT` becomes the default `INTEGRATOR` |
| Tests | `test_2d_analytic_jv.cpp`, `test_2d_active_set.cpp`, CVODE arms of parameterized fixtures | ~1,000 | See §3 |

Also retires: the `MIN_TIMESTEP > 0` loud warning (CVODE-path trap, moot), CVODE-specific
`[2D_OPTIONS]` keys (kept parsing as deprecation warnings for one release so old .inp
files don't error).

## 2. What stays, and the open sub-decisions

- **The delivery queue + exact-volume ledger** (`coupling_queue`,
  `coupling_delivery_remaining`, `assembleLateralInflows` consumption): production
  mechanism for marcher batches. Untouched.
- **`InertialEdges`** (+ normal-distance extension): the marcher's data layer. Stays.
- **hypre / BoomerAMG** (`HypreAmgPreconditioner.{cpp,hpp}`, 279 LOC + the vcpkg `hypre`
  dependency): only consumer today is CVODE. **Recommendation: delete with the stack.**
  The hypothetical θ-implicit backend is not planned, the code is tag-preserved, and
  carrying an unused dependency invites the exact divergence rot D2 exists to avoid.
  (Owner may overrule to keep the vcpkg feature flag only.)
- **SUNDIALS vcpkg dependency:** drops from the base `2d` feature entirely once
  CVODE/ARKODE delete — a real build simplification (the 2d feature keeps only HDF5).
  **Caveat:** the current GPU plugin features (`gpu`, `gpu-cuda`) pin
  `sundials[kokkos]` — the Kokkos plugin implements the *CVODE-family volume-state*
  path. Retiring CVODE strands the current GPU plugin; the P5 explicit GPU port
  replaces it. **Sequencing recommendation: retire CVODE after the P5 OpenMP backend
  exists**, or accept a one-release window with no GPU plugin (serial marcher is
  already faster than GPU-CVODE on every measured model).
- **`test_2d_windowless_coupling` conservation gates**: become the load-bearing
  coupling tests (already INTEGRATOR EXPLICIT).

## 3. Test impact (current suite: 110)

- **Delete outright:** `test_2d_analytic_jv` (Jacobian), `test_2d_active_set`
  (ydot-masking) — their subject matter no longer exists.
- **Re-anchor to EXPLICIT:** `test_2d_decoupled_stepping`, `test_2d_surface_routing`,
  `test_2d_vfr_closure` reference CVODE arms; their physics assertions carry over
  (Task 4.4 already adds the EXPLICIT variants ahead of retirement, so the swap is a
  deletion of the CVODE arms, not a rewrite).
- **Bit-guard role transfers:** today the CVODE tests guard shared infrastructure
  against marcher leakage. After retirement that role falls to the marcher analytic
  gates (`test_2d_inertial_marcher`, `test_2d_lts_equivalence`) — no gap, the shared
  infrastructure (mesh, closure, ledger) is exactly what they exercise.
- **A/B parity tests (§8.5)** retire with the reference; the EA benchmark subset
  (external ground truth) replaces cross-solver parity as the correctness anchor.

## 4. Mechanics

One PR, three commits, in order: (1) tests — add EXPLICIT arms / delete CVODE-only
tests; (2) engine — delete solver files + window machinery, default `INTEGRATOR
EXPLICIT`, deprecation shims for retired keys; (3) build — vcpkg/CMake dependency
removal. Full suite green after each commit. Revert path: the tag.

## 5. Status — EXECUTED 2026-07-29

Landed on `feat/2d-explicit-marcher` (engine) + `swmm6_gui` (GUI):

- `7e65dffb` feat(2d)!: solvers/window machinery/report trim deleted; hard-error
  policy in .inp parser + GeoPackage reader + `swmm_options_set_ext` (shared
  `is2DRetiredOptionKey`); marcher/closure keys added to the GeoPackage
  round-trip (never previously serialized); EXPLICIT default; ABI v3; also a
  one-shot-forcing fix (`forcing_dirty` bypasses the 30 s co-advance forcing
  cadence so RESET keeps per-step semantics).
- `3fd113a7` build!: SUNDIALS + hypre out of vcpkg/CMake/overlays/wheels.
- `01af3d95` bench(2d): run_bellinge.sh single explicit mode + retired-key strip.
- GUI `2424ff8` feat(ui): marcher group + Time-stepping (Max timestep only);
  integrator selector / CVODE tolerances / linear solver / coupling interval
  removed; demo_road_culvert de-retired.

Verification: ctest 108/108; `otool -L` clean of sundials/hypre (engine, omp
plugin, install prefix, GUI bundle); unedited Bellinge base .inp refuses with
the retired-key message; regenerated storm slice `.out` byte-identical
pre-vs-post retirement binaries (serial and omp T8) — earlier P5 step-count
mismatch traced to `COUPLING_INTERVAL 10` in the old slices, a key that no
longer exists; pytest marcher-options 5/5 + 2d/mesh subset 19/19 (conda env
`openswmm`). Note: three commits landed as code/build/bench rather than the
§4 tests-first ordering — the tests+engine halves were interdependent after
the mid-flight history rewrite and landed together, suite green at each commit.
