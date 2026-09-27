# Phase 25 — corridor patch safety and dialog clarity

Status: implemented, tested, compiled, signature-verified and launched. Full native acceptance remains pending.

This is a bounded W6a checkpoint in the approved GUI robustness and corridor mesh plan. It prepares the existing swept, transfinite and channel-lattice routes for broader corridor authoring. Independent geometry and dialog work uses separate file ownership and one shared build coordinator.

## Intended behavior

- Requested patches must have finite, positive, connected quad topology and a simple complete boundary. Local convexity alone does not establish that a swept corridor is globally valid.
- Station and directional subdivision counts must fit the mesh index capacity before allocation. Straight corridors retain independent along/across spacing; rectangles are not ranked against square-biased free-quad scores.
- Patches must remain within the domain, outside excluded holes, and clear of other patch interiors and required constraints. Valid touching patches need compatible subdivisions and shared topology.
- Welding must preserve valid cells and a conforming patch/background interface. A failed requested channel corridor must stop generation with a diagnostic instead of silently disappearing or becoming triangles.
- The existing structured-patch table must validate numeric input, identify the offending row and field, keep type identity independent of translated labels, explain directional spacing, and provide meaningful accessibility metadata and multi-row removal.

## Verification and acceptance

Regression fixtures cover invalid geometry and placement as well as valid straight rectangles, gentle bends, reversed directions, translated coordinates and abutting patches. Worker tests check that refused corridors preserve the saved model. Dialog tests exercise malformed fields, explicit zero along-spacing, translated display text and selection behavior.

The pre-fix baseline reproduced **40 failures**: 15 geometry, 11 placement, 12 dialog and two actual-worker cases. Four geometry failures were valid centimetre-scale rectangles at projected coordinates rejected through area cancellation. A later channel-lattice integration regression reproduced the same winding problem in that caller and prompted a local-origin calculation there too. Logs are retained under `workplans/artifacts/phase_25_corridor_safety/`.

All **192 checks across nine suites pass**, with no unexpected failures or skips: patch geometry 41, placement/stitching 28, patch dialog 25, actual worker 24, generator 13, submapping 8, quad-region integration 33, channel lattice 14 and channel-burn integration 6. Counts include QtTest setup/cleanup checks. The quad-region suite retains its existing expected failure for the pinned-junction triangle fraction. See `final_tests.log` in the artifact directory.

The first integration run caught the channel winding issue; after correction, the final run passed. Independent review added scalar-exact cavity-boundary checks, finite/range guards for snapping keys, and a high-origin fine-station regression. The build was interrupted before packaging to incorporate these final corrections; all final test executables were rebuilt afterward.

The final Release application/test build and deployment exited 0. Strict deep signature verification passed. Installed and bundled engine UUIDs match `E31718A1-72CE-3B04-A7DA-AE5AA2C0A4A1`. New GUI PID **66414** launched **2026-09-27 10:24:50 EDT**; read-only native inspection confirmed the main window and welcome page. Existing optional Mimer/ODBC/PostgreSQL driver dependency diagnostics remain W9 work. Native keyboard, screen-reader, theme and mesh inspection remain separate acceptance checks in the [manual guide](../../workplans/artifacts/phase_25_corridor_safety/README.md).

## Remaining scope

This checkpoint does not complete W6a. GIS line/bank selection, persisted feature references, variable widths, asymmetric banks, corridor junctions and directional mapped-region authoring remain open. Representative hydraulic and large-mesh performance acceptance remain W8 gates. Index-capacity checks are not a memory-budget policy. Complete topology/quality/groundwater remapping and live-engine topology refresh remain W1 work.

The generator exposes hole seeds and constraint paths rather than explicit hole faces. Placement checks infer the innermost closed constraint ring containing each seed and reject ambiguous intersecting enclosing rings. Fragmented/open boundary chains are not inferred, and islands inside an excluded annulus can be conservatively rejected. Explicit face ownership remains follow-up work. Invalid or skipped general quad regions retain their separate reporting/acceptance gate; this checkpoint tightens requested structured patches and channel corridors.
