# Phase 36 — integrated acceptance checkpoint

Status: implementation checkpoint passed; application compiled, packaged, signature/dependency verified and launched. Full release/native gates remain open below. This document distinguishes the completed checkpoint from remaining release requirements; a passing build alone is not release certification.

## Corrections and evidence

Restoring saved engine-version and terrain settings no longer marks a clean project edited. Terrain unit conversion and view updates still occur; genuine user changes still mark edits and pre-existing dirty state remains intact. Static, maximum-envelope, unavailable and missing-report profile states have distinct labels and accessible descriptions that survive keyboard cursor movement. Common groundwater assignment has correct label focus, named preview/species tables, row/field identities that update after deletion, and auxiliary buttons that cannot unexpectedly become Enter defaults. The old generic groundwater cell fields direct users to the supported assignment dialog instead of claiming the engine kernel is absent.

Baseline application-object regressions reproduced **12 GUI failures** (three project metadata, five profile/status, four forcing accessibility). All regressions pass after integration, including a further full-session terrain-restoration regression and queued-event checks. Tests retain actual application classes, not replacement dialogs.

The engine now indexes exact source names and complete initial-quality identities without changing row order or values. It retains first-existing-match behavior, SCALE, source terms, parser/FILE invalidation and suffix undo. A separate reproduced lifecycle bug retained groundwater records when a closed engine opened another model; a fresh open now resets groundwater authoring while leaving initialization/hotstart behavior intact. **All six new checks and 102 existing groundwater/restart checks pass.** One million source rows append in about **0.65 s**, and initial-quality rows in **0.81 s** on this host. These are public setter microbenchmarks; full GUI transaction, file parsing/writing and simulation costs are not included.

Live groundwater snapshots now include table/base elevations and SAT/UNSAT species, captured on the simulation worker using actual runtime identities and units. They retain native MG/L, UG/L, #/L, declared MSX units, age seconds and degrees C. Groundwater-specific water masks are independent of surface wetness. Values remain usable after engine destruction. Chemical payloads count toward history memory and thin with their hydraulic tick. A separate content revision refreshes scalar caches without invalidating pinned export indices. Static base lookup works when the first hydraulic tick predates the first groundwater snapshot. Complete whole-run ranges require file-backed output; finished/thinned live history is refused for that claim.

The live fixture initially used incorrect 1-based cell references and aquifer-thickness expectations. A retained public-API probe established actual seeded values before correcting the fixture; strict concentration/temperature assertions remain. The test also now checks changed water age rather than assuming monotonic increase in a system receiving young recharge. These were test expectation corrections, not production value changes.

The packaging baseline independently found three unresolved vendor library dependencies in unused Qt Mimer/ODBC/PostgreSQL drivers. Deployment now retains required SQLite support and removes only those unsupported Qt drivers before signing. The installation path checks deployment failures; build-time signing now also performs strict verification. A separate arm64 Mach-O dependency audit requires every non-system library to resolve inside the bundle. This does not disable GDAL's independent data-source capabilities.

## Build and acceptance evidence

Evidence: `workplans/artifacts/phase_36_acceptance/`; engine setter scaling: `workplans/artifacts/phase_36_engine_authoring/`.

- Isolated build tree: `build-phase35` (name retained from its creation; compiled sources identify this checkpoint).
- Engine installed for this checkpoint: arm64 UUID `60A33D33-0C0C-3BCF-93A6-28CED42EF229`.
- **313 GUI checks across 27 suites pass, with no failures or skips:** 208 checks in 18 CTest suites and 105 in nine real-application-object suites. Asynchronous QSG frame publication is explicitly enabled for its regression. These include real-engine selection/forcing Save/reopen/conservation, combined HDF sections/exports, project restoration, live runtime snapshots, pinned GIS export, style transactions and save failure validation.
- **108 engine checks pass:** six new authoring/lifecycle/benchmark checks and 102 across five groundwater/restart suites. Final bundle identity, dependency/signature verification and native observations are appended below after packaging.
- Existing release portability warning: GUI objects target macOS 26.0 while this installed engine targets 26.4. Successful testing on this host does not establish support for older macOS versions.

## Remaining release gates

| Requirement | Current disposition |
| --- | --- |
| Conservative raster flux-density integration | Not implemented. Current feature/raster inputs are centroid samples or explicitly per-cell rates. Region-total manual/series flow is area weighted. |
| Groundwater boundary chemistry, sigma-layer chemistry, coupled RK2 | Explicitly unsupported and rejected. They require separate physical solver implementations and numerical acceptance, not enabled GUI controls. |
| Generic species contours/labels and GIS raster/vector export | Configurable cell fills, samples, combined sections and native-unit CSV are supported. General chemical contour/label/GIS export remains incomplete. |
| Full million-cell authoring workflow | Setter scaling addressed; complete preview/apply/undo/parse/write, memory and cancellation on valid huge models still require evidence. |
| Native accessibility/themes/platforms | Automated accessibility names, focus contracts, keyboard charts and theme contrast are tested. Full VoiceOver, display scaling, visual review and supported-platform certification remain distinct acceptance tasks. |
| Meshing | The separate owner is still changing the implementation. Recorded adaptive-terrain comparisons and two still-water controls are useful evidence; the latest complete quality, hydraulic, large-model and cancellation report is still required. Historical superseded quadtree failures are not treated as failures of the new algorithm. |

See `workplans/phase36_acceptance_audit.md` for the evidence reconciliation and `workplans/artifacts/phase_36_acceptance/TESTING.md` for native review steps. Manuals now describe operational groundwater assignment and combined profiles, replacing obsolete preview-only guidance.

## Final packaged application and native observation

The final `build-phase35/SWMMVis.app` linked and deployed successfully (exit 0). Packaging compiled no further source objects after the tested production-object checkpoint. Strict deep signature verification passes. The dependency audit checked **152 arm64 Mach-O binaries with zero unresolved non-system dependencies**. SQLite remains present; unused Mimer/ODBC/PostgreSQL Qt drivers are absent. Deployment still prints missing-vendor diagnostics while scanning those drivers before the explicit exclusion step; the final bundle audit, rather than that intermediate scan, establishes the corrected contents.

The bundled engine UUID matches the tested installed engine: **60A33D33-0C0C-3BCF-93A6-28CED42EF229**. The GUI launched on **2026-10-01 at 06:16:56 EDT**, PID **33098**. Loaded-library evidence confirms its bundled engine. Native accessibility inspection reached the welcome screen and file picker. During the fixture-opening attempt the active window changed to an edited Bellinge model; further interaction stopped. No save/close or changes to that working model were performed. This verifies native launch, not a completed fixture/VoiceOver/theme walkthrough. The combined figure generated by the real-engine journey was visually inspected and correctly labels the aquifer base as Static.

Final records: `final/package_build.log`, `final/codesign_verify.log`, `final/bundle_dependencies.json`, `final/bundled_engine_uuid.log`, `final/native_processes.log`, `final/native_loaded_libraries.log`, `final/focused_final.log` and `final/integration_results.json`. Earlier failing integration logs and corrected-fixture probes remain retained diagnostic evidence.
