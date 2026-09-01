# 2D↔1D decoupling viability study — explicit marcher (2026-07-30)

**Question (owner):** can the 2D run further decoupled from the 1D — accumulate
and interpolate the exchange across longer spans — and would it help?

**Setup.** Bellinge storm slice (06/29 04:15 + 30 min, ×100 rain), serial
marcher, model levers already applied (`LENGTHENING_STEP 6`, `MINIMUM_STEP 1.0`,
`MAX_TIMESTEP 60`). Two env-gated probes in the engine worktree:

- `OPENSWMM_2D_SYNC_SPAN=<s>` — overrides the sync-batch span cap (normally
  `clamp(MAX_TIMESTEP, routing_step, 60)`), `SurfaceRouter2D::advancePostRouting`.
- `OPENSWMM_2D_HEAD_RAMP=1` — linear extrapolation of each coupled node's 1D
  head across the batch from its batch-over-batch trend (router computes
  slopes → `ExplicitInertialSolver::setExchangeHeadSlopes`; exchange evaluates
  `h_1d + slope·τ`). Ramp OFF verified byte-identical to the pre-probe binary.

**Results** (reference span-60 exchange totals: spill 29.9k / drain 29.6k m³):

| span | held: wall / routing-cont / drain | ramp: wall / routing-cont / drain |
|-----:|----------------------------------:|----------------------------------:|
|  60  | 7.97 s / 22.6% / 29.6k            | 8.68 s / 21.3% / 29.5k            |
| 120  | 7.42 s / 26.4% / 25.7k            | **7.05 s** / 26.6% / 24.8k        |
| 300  | 8.01 s / 43.4% / 43.0k            | 7.88 s / 23.3% / **44.8k**        |
| 600  | 9.11 s / 70.2% / 84.2k            | —                                 |

**Findings.**
1. **Wall gain from longer decoupling is ≤ 11 % and exhausted by ~120 s.** The
   wall ceiling is the marcher's CFL substep count, which no sync policy
   changes; past 120 s the 1D churn from stale exchange *costs* wall.
2. **Held heads break physically at long spans**: the orifice drains against a
   stale low node head all batch (node fills in seconds in reality) — drain
   volume +45 % at 300 s, +185 % at 600 s. The 2D ledger stays ~0 % (it books
   what it sent) while the 1D floods and spills back — routing continuity 43→70 %.
3. **Linear head extrapolation does not rescue long spans.** At 300 s it fixes
   the *continuity statistic* (43→23 %) but not the physics — drain is still
   +50 % vs reference. Within-batch feedback (drain → node fills → gradient
   dies) is invisible to any extrapolation from past batches, as predicted.
4. At span 120 both variants distort exchange totals ~10–15 % for ≤ 0.9 s of
   wall on the worst slice.

**Verdict: not viable beyond the current policy.** The co-advance at
`MAX_TIMESTEP 60` (batch ≤ 60 s, held heads, live substep exchange, queue-spread
delivery) is the sweet spot; it already delivered 1.85× on the worst slice vs
the model's original settings. Anything longer needs a per-node 0-D companion
model *inside* the batch (integrate node head against the running exchange),
not interpolation — and the ≤ 11 % wall ceiling doesn't justify it. Better
levers if more speed is wanted: omp plugin threading, per-batch overhead trim,
and SCF=1 physical rain for non-stress-test runs.

**Probe code status:** uncommitted in the engine worktree
(`SurfaceRouter2D.{cpp,hpp}`, `ExplicitInertialSolver.{cpp,hpp}`,
`NodeCoupling.{cpp,hpp}`), env-gated, off-by-default byte-identical. Owner to
decide: commit as experiment tooling or revert.
