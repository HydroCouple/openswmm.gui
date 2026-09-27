# Phase 29 — rectangle-aware quad quality

Status: implementation, automated mesh verification, application build/signature and fresh launch complete. Native acceptance remains pending.

## Scope

This bounded W6/E6 checkpoint addresses the approved plan's M4 diagonal-ranking defect. The ranking score becomes scaled Jacobian × rectangularity × the existing aspect penalty. A perfect rectangle within the configured aspect limit receives the same shape score as a square; a rhombus or kite is penalized for corner-angle distortion. The diagonal-cosine `skew` metric remains available as a diagnostic but is no longer used to rank candidates.

Hard acceptance defaults remain 60°–120° corner angles, scaled Jacobian at least 0.866 and aspect at most 2. A larger configured aspect limit does not itself create an aligned corridor or anisotropic point layout. Structured centreline, bank-pair and mapped patches remain the explicit corridor-authoring routes.

The score affects greedy triangle merging, template ordering and local four-cycle polishing of residual pairing. Maximum-cardinality matching is still unweighted. Audit corrected an earlier plan/header claim: smoothing uses guarded minimum scaled Jacobian, not this score. This phase retains the smoothing objective, movable-vertex constraints and aspect safeguards.

Quality and shared signed-area calculations use local coordinates to avoid cancellation at large projected origins. Quad quality also normalizes angular calculations and rescales area through exponents to avoid intermediate overflow. Nonfinite, degenerate or unrepresentable quad geometry cannot pass acceptance or receive positive quality scores.

## Verification

Parallel work has separate ownership for the shared quality metric, actual pairing/merge regressions and a read-only consumer audit. The parent owns signed-area and smoothing regressions and coordinates all builds. Tests cover permitted rectangle aspect ratios, distorted alternatives, cyclic ordering, winding, rotation, translation, finite-value safety, locked edges, region tags, existing acceptance limits and end-to-end generation.

The baseline build exited 0. Four suites reproduced **222 failures** before implementation: quality 207, greedy merge 5, template pairing 5 and signed-area/smoothing 5. The competing-candidate cases use actual shared triangles rather than merely comparing metric values. Logs are retained as `baseline_build.log` and `baseline_tests.log`.

The production build and all **10 CTest targets passed**, reporting **679 passing checks** (669 Qt checks and 10 Google Test cases), zero failures and zero skips. Coverage includes quality, greedy merging, matching, cleanup, point placement, region generation, cell statistics, corridor patches, patch placement and the terrain worker. The existing pinned-junction expected failure remains documented in the region-generation suite; it is not resolved by this phase. Logs are `production_build.log` and `final_tests.log`.

Native testing steps and application verification are recorded in [Phase 29 evidence](../../workplans/artifacts/phase_29_rectangle_quality/README.md).

The GUI build exited 0 and deep/strict signature verification passed. Installed and bundled engine UUIDs match `E31718A1-72CE-3B04-A7DA-AE5AA2C0A4A1` (arm64). A fresh GUI process launched on 2026-09-27 at 18:02:03 EDT, PID 63672; the prior instance was preserved. Launch evidence confirms process startup, not completion of native visual/accessibility acceptance. Existing optional Mimer/ODBC/PostgreSQL packaging dependency diagnostics remain a W9 release gate.

## Remaining scope

This is a geometric ranking correction, not a hydraulic-quality certification or a directional Free-meshing implementation. Corridor-specific target-aspect/alignment policy, surveyed station correspondence, junctions/transitions and representative hydraulic/performance acceptance remain open. Triangle-only metric extreme-value handling and the smoothing displacement threshold's dependence on coordinate magnitude are separate follow-up concerns. Native and optional database-driver packaging gates retain their earlier status.
