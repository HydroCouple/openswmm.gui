# Multispecies phases — 2026-10-20

All five implementation phases were checked between increments. These records
qualify the explicit CPU profiles below; they do not imply every shared-species
domain/backend combination is implemented.

| Phase | Delivered behavior | Gate |
| --- | --- | --- |
| 1 | Full 1D reaction checkpoint/runtime/output continuation; changed models/corrupt state refused transactionally | 24 solver × integrator × wet/dry combinations, source-clock/output byte equality; external component file also covered |
| 2 | Opt-in WALL geometry, area units, wetted/reference area, dry inventory | FT2/M2/CM2, barrels, half-full/dry/refill, bulk+wall invariant; LEGACY and ARD fallback |
| 3 | Explicit conventional and storage-node LID reactors; retained/mobile mass, transfers, exports, hotstart guard | Bioretention, wet permeable pavement, bucket/Richards storage nodes; conservation and exact checkpoint continuation |
| 4 | Named wet laws, independent dry land-use laws and conversion basis | 44 surface checks; project/area units, retained product, continuously fed wet-pool first-order convergence |
| 5 | FLOWPACED/SETPOINT, dedicated editor tables/options, examples and broader benchmarks | 6 booster/solver continuation combinations; 16 Qt checks including atomic multi-table rollback; 38 benchmark cells plus falsification controls |

The new conventional LID case exposed a species-only drain-reset gate. Adding
reaction species to that gate stops repeated accumulation. The published LID
case now has LEGACY routing continuity errors below 0.14%; its acceptance limit
is 0.25%. Retained-pool/transfer unit checks use tighter conservation tolerances.
The wet pavement test saturates the initial stack so pavement actually retains
water; a freely draining zero-storage layer correctly transports without
inventing a reactor.

Final focused results: **269 native, 56 Python, 16 Qt and 38 benchmark checks
passed**, plus three benchmark negative-control tests (zero chemistry, wrong
species, absent executable). See [checks](final/checks.json), [native/config log](final/test_engine_reactions_config.log),
[benchmark scores](final/benchmark-scores.json), and the individual final logs.
Initial phase records retain the actual intermediate outcomes and corrections.

A broader existing engine checkpoint run had 129 passes and 43 failures. A
representative mesh fresh-header failure was reproduced with the earlier saved
library. This is recorded separately in phase1; every failure has not been
independently classified. A trial mesh fix was removed, leaving mesh code
unchanged. The focused reaction continuation gates pass.

Remaining limits: partial LID capture, covered barrels, WALL porous chemistry,
2D-owned conventional LID chemistry, and heat/age/active-2D/subsurface reaction
checkpoint combinations remain refused or outside qualification. Native state-only
hotstarts do not recover porous reaction inventories. Wet/dry source splitting is
first order; exact continuously fed CSTR integration remains future work. The
engine ROADMAP and manuals specify these contracts.

Working hashes and prior commit identities are recorded in [provenance](final/provenance.json).
All eight example decks also run successfully; see [example results](final/examples-checks.json).

Implementation commits: engine `c5ca4cfd`, benchmarks `6c1cb2a9`.
No repository was pushed. Unrelated existing changes were retained.
