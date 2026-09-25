# Generation-report contract and acceptance fixtures

W0 defines the reporting contract below. W2/W5 add instrumentation as each pipeline stage changes; no runtime report implementation is claimed in Phase 07. Reuse existing `ConditionReport`, `CleanupReport`, `QuadRegionReport`, boundary-filter counters and field diagnostics. Do not duplicate those algorithms to generate telemetry.

## Version 1 report fields

| Field | Contract |
|---|---|
| `schemaVersion`, `policyVersion` | Separate report layout from meshing/conditioning policy; both participate in cache interpretation. |
| `jobId`, `projectRevision`, `meshRevision` | Bind worker inputs/results to their owning project snapshot. Stale jobs cannot publish into a newer mesh. |
| `sources[]` | Stable source/layer/feature IDs, source and target CRS, horizontal/vertical units, geometry/raster revisions, role and supplied/inferred status. Paths may be diagnostic references; never include credentials. |
| `options` | Effective normalized settings, along/across spacing, feature-protection rules, region/global cap precedence, sampler units, minimum-size/conflict policy and alignment choices. |
| `stages[]` | Named stage, status (`completed`, `cancelled`, `failed`, `skipped`), input/output counts with declared entity type, removals/moves by reason, elapsed time, estimated/measured resource values and cache hit/key. Unknown resource measurements remain absent, not zero. |
| `featureEvents[]` | Source/feature ID, stage, operation (kept/dropped/simplified/split/merged/moved), reason, affected count/length, locations and tolerances in declared units. Recoverable protected-feature losses produce explicit warnings. |
| `regions[]` | Existing region identity/mode/fallback and field solve status, sweeps/update measure, quad/triangle counts, acceptance/quality, effective aspect limit and transition reason. Include explicit guide/angle vs fallback distinction. |
| `quality` | Positive area, topology/conformity, Jacobian/angle/aspect, boundary coverage, constraint connectivity, terrain residual metrics, protected-feature coverage. Square-biased skew cannot be the universal rectangle acceptance criterion. |
| `outputs[]` | Owned staging/final paths and role, format, validation result and publication state. A generated preview is distinct from a saved model. |
| `warnings[]`, `errors[]` | Stable code, affected source/feature/stage, readable summary and recovery action. Details feed both accessible tables and overlays/logs. |
| `completion` | Completed/cancelled/failed/stale; never infer success from nonempty geometry. Partial output is not an accepted mesh. |

Example diagnostic: feature `river-centreline:17` is accepted by conditioning, but 82 terrain candidates are removed by the spacing filter inside its corridor. Record the filter and counts, not the unsupported claim “the thinner missed the river.” The first stage where required geometry/error bounds fail determines the correction.

## Reproducible fixture ledger

| Fixture / existing test foundation | Measurement and next package |
|---|---|
| Phase 01 axial, oblique, reversed, crossing, touching and disjoint lines; `test_meshquadregion_e2e` | Constraint edge connectivity/length, positive cells; retain baseline while adding road/river sources in W6a. |
| Phase 02 elongated candidate and global/region cap tests; `test_meshquadcleanup`, `test_meshquadmatch` | 3:1 geometry accepted only under appropriate cap; smoothing/late merge cannot introduce violations. W6 changes rectangle ranking independently. |
| Phase 03 field uniform/nonuniform/cancel/cap fixtures; `test_meshcrossfield` | Distinguish converged/iteration limit/grid limit/cancel and explicit-angle behavior. W2 adds resolution/domain/guide tests. |
| New Phase 07 mixed-cell selection fixture; `test_meshselectionjourney` | Two triangles plus a 2:1 rectangular quad; all three cell identities remain selectable without results. Reuse for W4c. |
| Synthetic sloped plane, road embankment, ditch/thalweg and paired river banks | W2/W5: source/target units, retained feature points, final surface residuals, first-loss stage and protected geometry. Include narrow corridors below configured minimum size with an explicit conflict outcome. |
| High-coordinate, sub-metre grid and feet/metres equivalence | W2: precision and unit invariants across DTM sampling, coordinate transform, thinning, size-field and final elevation assignment. |
| Straight/bent variable-width road and river corridors with long sparse segments | W6a/W6: independent along/across counts, alignment, width variation, stable feature identity and unmodified DEM. Compare GIS-driven construction with existing typed swept patches. |
| River confluence, road crossing, bank termination, holes and neighboring strips | W7: shared interface nodes, no cracks/T-junctions, protected banks and declared valid local fallback. Crossing is not automatically a hydraulic connection. |
| Rainfall, lateral flow, wet/dry, 1D–2D coupling and groundwater exchange over elongated cells | W8: water balance, stability, accuracy vs refined/reference grid and computational cost. No recommended aspect default before this evidence. |
| Runtime groundwater decay-on/off owned compatibility decks | W0/W4a–c: this installed build fails transport-consumption acceptance. Keep files/reports/API returns; do not label successful process exit as passing physics. |
| Save refusal/short write/retry fixtures from Phase 04 | W1: extend to INP, sidecar, patches and interruption at each publication boundary with byte-level preservation checks. |

New synthetic geometry and numerical files belong under reviewable repository test/evidence locations. Actual user road/river data has not been supplied; real-data acceptance stays explicitly outstanding. A schema/fixture ledger is not equivalent to executed fidelity or hydraulic benchmarks.
