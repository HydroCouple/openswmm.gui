# Phase 28 — GIS bank-pair corridors

Status: implemented, tested, compiled, signature-verified and launched. Native manual acceptance remains pending.

## Scope and correspondence

This bounded W6a checkpoint follows the approved plan's left/right-bank authoring route. A bank-pair source captures exactly two selected, distinct LineString feature IDs from one GIS layer. These are the actual corridor boundaries; a symmetric centreline buffer is not required. Existing centreline sources remain supported.

Correspondence uses normalized arc length on each bank. Endpoints are paired using the shorter total connection distance; ambiguous alignment is refused. Stations include the union of original vertex fractions from both banks, with additional subdivisions for positive Along spacing. Across controls the number of cells between the banks. Along 0 preserves the combined bank stations. This allows varying and asymmetric geometry and retains bank bends; normalized fractions are not surveyed cross-section station matching.

The builder checks finite open lines, station/index capacity, cell convexity, boundary simplicity and connected topology. Invalid geometry must return an error without a partial patch. Existing corridor placement, protected-feature crossing, whole-domain background exclusion and stitching checks continue to apply. Elevations retain the existing terrain pipeline; selecting banks does not enable burn-in.

The accessible GIS corridor widget gains a source mode and bank-pair guidance. Width controls are inapplicable to bank geometry. The recipe records the bank-pair role and exact feature IDs in version 2; old centreline recipes remain version 1. Decoding supports both and rejects unsupported roles or malformed pairs. Source identities, geometry digests, successful-adoption guards and staged Save publication remain in force.

Bank source reads include a role-specific digest prefix without changing existing centreline digests. A failed feature read, invalid selected geometry or changed dependency rejects the whole request. Retained width metadata must remain finite and positive for recipe consistency, although it does not affect bank geometry; a width-field reference on a bank pair is refused. Malformed restored rows direct the user to remove and re-add them.

## Verification

Geometry, worker-reader and UI/recipe work are parallelized with explicit file ownership and coordinated builds. Tests cover reversals, swapped banks, rotation, large coordinates, differing vertex densities, varying/asymmetric widths, invalid/folded outlines, both background modes, source changes, cancellation, recipe compatibility and Save As rebasing.

The baseline build exited 0. Running against the geometry stub and prior reader/UI/codec reproduced **85 failures**: geometry 60, reader 11, widget 7, codec 2, actual worker 4 and Save roundtrip 1. Existing controls passed. Logs are retained as `baseline_build.log` and `baseline_tests.log` in the Phase 28 artifact directory.

Independent code review found no geometry blocker. Review added retained-width metadata consistency checks and near-coincident station/reversal invariance coverage. Cancellation discards results safely at helper-call boundaries; the geometry helper itself is not interruptible. Large-corridor performance remains an acceptance gate.

The final run passes **584 QtTest checks across eight suites**, including setup/cleanup: patch geometry 180, generator integration 77, production Save 138, actual worker 53, placement/stitching 28, source reader 45, recipe codec 35 and widget 28. No unexpected failures or skips remain; the existing pinned-junction expected failure is retained. The test build exited 0 and the scoped code whitespace check passed.

The first production run reused baseline output folders, exposing two old Save tests that assume their destination files do not already exist. Their baseline successful-retry step had left those files behind. `production_tests.log` retains this fixture-isolation evidence. The final run uses a fresh `final/` output tree and passes without code changes for those failures.

Build, launch and native steps are recorded in [Phase 28 testing](../../workplans/artifacts/phase_28_bank_pairs/README.md).

The Release GUI build/deployment exited 0 using the pinned macOS 26.5 SDK. Strict deep signature verification passed; installed and bundled engine UUIDs match `E31718A1-72CE-3B04-A7DA-AE5AA2C0A4A1`. New GUI PID **40839** launched **2026-09-27 17:44:57 EDT**. Launch is process-verified. Known optional Mimer/ODBC/PostgreSQL driver dependency diagnostics remain W9 work.

## Remaining scope

Cross-layer bank pairing, station-controlled/surveyed correspondence, multi-part bank assembly, centerline offset profiles, junctions/transitions, anisotropic Free meshing and representative hydraulic/performance acceptance remain later work. Native keyboard, theme, enlarged-font and screen-reader acceptance remain separate checks. General source snapshot ownership and optional database-driver packaging retain their earlier open gates.
