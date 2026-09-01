# P5 — Kokkos Port of the Explicit Marcher (one-page design note)

**Status:** DoD package for owner review (Task 5, 2026-07-29). Not committed.
**Premise:** the marcher was built parallel-ready by construction (plan R4): every kernel
in `InertialKernels.hpp` is a plain inline function over scalars/raw pointers — no
allocation, no exceptions, no `std::` containers in the hot bodies. The port is a
restructuring of the *loops*, not the *math*.

## 1. Kernel annotation list (`KOKKOS_INLINE_FUNCTION`)

All in `src/engine/2d/solver/InertialKernels.hpp`, annotated as-is (only
`std::cbrt/sqrt/clamp/fabs` → `Kokkos::` math equivalents behind an alias macro):

| Kernel | Role | Notes |
|---|---|---|
| `cellEtaDepth` / `cellVolumeFromEta` | V↔η closure (FLAT/VFR) | VFR needs `vfrSort3`/`vfrEtaFromMeanDepth` from `VfrClosure.hpp` annotated too (pure scalar, trivial) |
| `faceFlowDepth` | face wet test | — |
| `inertialFaceUpdate` | θ-average + gravity + semi-implicit friction | — |
| `froudeCap` | supercritical clamp | — |
| `cellCflDt` | per-cell stable step | — |
| `positivityScale` | export limiter λ | — |

`MeshData`/`InertialEdges` arrays (already SoA) become `Kokkos::View` mirrors created
once at `initialize()`; `SolverOptions2D` scalars are captured by value into a small
POD `KernelParams` (the same pattern `KokkosSurfaceKernels.hpp` already uses).

## 2. Parallel loop structure (per tier, inside the macro cycle)

`runMacroCycle`'s halving-order schedule is unchanged host-side control flow; each
substep launches, for every tier k firing at that substep:

1. **Face sweep** — `parallel_for` over `edges_by_tier_[k]` (`fireFaces` body): reads
   incident-cell η (coarser side frozen at its last own-update — already the CSR
   contract), writes `q_e` and the per-side accumulators `facc_L/facc_R`. Face-parallel
   is race-free by layout: one thread per unique face, cells untouched.
2. **Positivity pass** — `parallel_for` over `cells_by_tier_[k]`: compute λ_i from the
   gathered outgoing sum (CSR `cell_ptr` gather, read-only over faces), then scale the
   faces leaving i. Shared face takes `min λ` — implemented as `Kokkos::atomic_min` on a
   per-face λ view (or a two-pass gather: cell λ pass, then face λ = min of two loads —
   preferred, fully deterministic).
3. **Cell gather** — `parallel_for` over `cells_by_tier_[k]` (`fireCells` body): V update
   from the CSR gather + sources + inbox consume, Perot refresh at own cadence,
   coupling/BC source terms. One thread per cell, faces read-only: race-free,
   bit-deterministic (fixed CSR order ⇒ fixed FP summation order).

Reductions (ΣV telemetry, min dt for retier) use `parallel_reduce`; the tree order is
deterministic for a fixed league size — record the team size in `results.csv` runs.

## 3. Device-resident active-set compaction

`syncAndRebuild` currently rebuilds `active_cells_` / `cells_by_tier_` / `edges_by_tier_`
with host `std::vector` pushes. On device this becomes the standard scan pattern, no
host round-trip:

- flag pass: `parallel_for` cells → `flag[i] = 1` if flux-active (h ≥ h_on ∪ halo ∪
  pinned coupling/BC stencil), tier id from `cellCflDt`;
- `Kokkos::parallel_scan` (exclusive) over flags → write offsets; scatter compacts the
  active list per tier; same two passes for faces (`face tier = min(kL,kR)`, both-active
  test — trap #1 stays enforced on device);
- `settleAccumulators()` **must run before any reassignment** (trap #3) — it is itself a
  flat `parallel_for` over cells consuming `facc_*` via the CSR gather.

Hysteresis counters (promote-after-2-syncs) live in a small per-cell `int8` view.

## 4. Host↔device sync points (the whole contract)

Data crosses the boundary ONLY at co-advance batch boundaries (`coAdvanceStep`):

- **Host → device** (batch start): rainfall/evap/coupling forcing arrays (only on the
  30 s forcing cadence), boundary values, 1D node heads for the coupled stencils.
- **Device → host** (batch end): `last_coupling_exchange()` (1,020 doubles), boundary
  applied-flux slots, and — only on the report-scale refresh cadence — the full state
  (`volume`, `head`, `edge_flux`) for output reconstruction. Between batches nothing
  moves; the marcher's tiers/active sets/accumulators are device-resident.

The per-substep exchange evaluation (`computeNodeCouplingQ` on live 2D heads) runs
device-side over the pinned coupled stencil (tier-0 cadence, ~1k points) into a device
accumulator; `orificePhi`/`effectiveArea` get the same annotation treatment.

## 5. ABI entry

`GpuPluginAbi.h` gains a third factory mirroring the existing pair
(`openswmm_gpu_probe`, `openswmm_make_gpu_surface_solver`):

```c
OPENSWMM_GPU_ABI openswmm_gpu_solver_handle
openswmm_make_gpu_explicit_solver(const openswmm_gpu_mesh_desc*,
                                  const openswmm_gpu_options*);
```

ABI version bumps 2 → 3 (the core already refuses mismatched plugins). The factory
returns the same vtable-style handle as the inertial one; `SurfaceSolverFactory`'s
`EXPLICIT` branch tries the plugin (unless `OPENSWMM_2D_BACKEND=cpu`) and falls back to
the serial marcher. The ≥20k-cell auto-gate and the FLAT-closure refusal guard carry
over unchanged.

## 6. Order of work & gates

1. OpenMP backend first (`Kokkos::OpenMP`, zero data-motion risk) — gate: bit-identical
   to serial on the 30-min slice (fixed-order sums preserved), then THREADS scaling
   curve into `results.csv`.
2. Metal/CUDA second — gate: conservation ≤ 1e-10 relative, LTS-equivalence functionals
   (decay + bulk-arrival, trap #6) vs serial; bit-parity NOT promised across devices.
3. Acceptance: coupled Bellinge 48 h < 60 s on an M-series GPU or 4-core OpenMP < 150 s.
