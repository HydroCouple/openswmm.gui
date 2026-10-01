# Phase 33 — Combined dialogs and operation safety

Status: combined implementation, automated verification, build, signature verification and GUI launch complete. This is the first consolidated delivery after Phase 32. Native acceptance remains separate.

## Delivery scope

This batch combines related W1/W3 tasks that previously would have required many small phases. Parallel agents own disjoint editor families and runner safety; one coordinator integrates and builds. The active meshing overhaul, preferences, result rendering and profiles remain owned by their other agents.

| Area | Behavior addressed |
| --- | --- |
| Water Age, Heat, Initial Quality and Pollutants | Preserve exact stored numbers when rounded controls are untouched; changes in another field cannot silently round existing values. Source editors preflight drafts and report backend refusal while retaining drafts and accounting for successful partial writes. |
| Reaction System | Surviving rows remain bound to their actual entries after deletion; draft deletion cannot remove unrelated engine data; changing an override identity replaces the original entry and duplicate identities are refused. |
| Curves, Patterns, Time Series and Transects | Invalidate when a registry/undo owner disappears; guard post-modal accesses; new curve/pattern drafts do not bind and alter an existing object; tables identify their purpose and immediate-edit behavior. |
| Node, Link, Subcatchment and Groundwater compound editors | Stop borrowed-engine callbacks before the engine closes. Subcatchment editable values retain precision and reject nonfinite numbers. Groundwater preserves exact values and full expressions, checks missing node selections and changed source data before writing, and reports setter failures. |
| Simulation Options and nested editors | Invalidate contexts before owner/engine teardown. Successful writes mark the owning project immediately, including writes retained after child or outer cancellation. |
| Street, Inlet and Hydrograph editors | Nested ownership and post-modal access guards integrated with the same engine-close contract. |
| Simulation runner and launch | Single start per runner; owner teardown cancels and waits for workers; local input paths no longer depend on changing the GUI process working directory; output/input aliases and active run destinations are checked before overwrite preparation. |
| Asynchronous project opening | Worker-owned engine cleanup, duplicate refusal, stale destination checks, exception reporting/retry and guarded CRS prompts; worker parsing no longer writes into GUI-owned model arrays. |
| Shared accessibility verification | Validate the actual accessible name of textless controls, including custom painted controls without a QIcon. Qt internal helper controls retain explicit exclusions. |

The engine-close notification is emitted before the editing engine is destroyed, allowing dialogs to stop timers and clear borrowed handles synchronously. This is also tested through actual model layers; an explicit invalidation unit test alone is not the entire lifetime evidence.

## Run and persistence safety

Run preflight protects the INP, known referenced source files, open model/settings/mesh paths, report, binary results, HDF5, run logs, declared SAVE outputs and the legacy compatibility deck. Alias checks include existing hard links, symlinks and canonical parent directories. Recheck after modal overwrite confirmation protects against an intervening GUI run or changed project. Existing result handles are matched with the same alias rules.

The same project/engine cannot reuse an active status row. Cancellation retains output reservations until the worker finishes, including after a project is closed. A failed save of an automatically chosen HDF5 output cancels the run and retains unsaved state.

Legacy compatibility output is written to a sibling staging file and published through checked QSaveFile replacement. Existing result handles are released only after compatibility preparation succeeds; no early HDF5 deletion is required. These protections do not establish a transaction for all solver outputs after a run begins.

## Evidence and verification

See [phase artifacts and manual guide](../../workplans/artifacts/phase_33_dialogs_safety/README.md), [chemistry notes](../../workplans/phase33_chemistry_notes.md), [model-editor notes](../../workplans/phase33_model_editors_notes.md), [operation notes](../../workplans/phase33_operation_safety_notes.md), and [shared accessibility notes](../../workplans/phase33_shared_accessibility_notes.md).

Baseline evidence reproduced numeric loss, incorrect row mutation, missing selections/duplicate acceptance, missing accessible names, owner invalidation gaps, duplicate runner starts, process-directory changes and output aliases. The initial runner owner-destruction baseline case had a child-test argument error and is not counted as a reproduced production lifetime failure. The corrected child test verifies safe paused-worker destruction in the final implementation.

The old programmatically constructed initial-loading fixture crashes in the installed engine even in its unchanged test. Compound GUI tests now open a real INP fixture and retain the original round-trip assertions; the programmatic engine-allocation defect remains separate evidence. An early test invocation before the combined build linked ran older binaries; those logs are labelled stale and are not final verification.

**439 checks pass across 24 suites; two known engine-validator failures and two skips remain.** Counts include setup/cleanup. The standalone accessibility suite also has two intentional expected failures that prove the helper catches unnamed controls. Only the latest applicable result for each suite is counted; intermediate and superseded runs are excluded. See [machine-readable summary](../../workplans/artifacts/phase_33_dialogs_safety/final/verification_summary.json).

| Suite | Pass | Fail | Skip | Evidence log under `final/` |
| --- | ---: | ---: | ---: | --- |
| TestSimRunnerLegacy524 | 5 | 0 | 0 | `model_operations_tests.log` |
| TestSubcatchCoverageLoadings | 12 | 0 | 0 | `chemistry_operations_tests.log` |
| TestWaterAgeSourcesDialog | 19 | 0 | 0 | `chemistry_operations_tests.log` |
| TestInitialQualityDialog | 30 | 0 | 0 | `chemistry_operations_tests.log` |
| TestReactionSystemEditor | 14 | 0 | 0 | `chemistry_operations_tests.log` |
| TestSimRunnerFailures | 35 | 0 | 0 | `chemistry_operations_tests.log` |
| TestHeatConfigDialog | 22 | 0 | 0 | `heat_timeseries_tests.log` |
| TestPollutantEditorDialog | 10 | 0 | 0 | `pollutant_accessibility_tests.log` |
| TestPlotVariablePickerDialog | 14 | 0 | 0 | `accessibility_consumers_tests.log` |
| TestTimeseriesEditorDialog | 44 | 0 | 0 | `accessibility_consumers_tests.log` |
| TestGwfExpressionEdit | 5 | 1 | 0 | `accessibility_consumers_tests.log` |
| TestPatternEditorDialog | 34 | 0 | 0 | `accessibility_consumers_tests.log` |
| TestCurveEditorDialog | 20 | 0 | 0 | `accessibility_consumers_tests.log` |
| TestTransectEditorDialog | 42 | 0 | 0 | `accessibility_consumers_tests.log` |
| TestUserFlagsDialog | 6 | 0 | 0 | `accessibility_consumers_tests.log` |
| TestUserFlagValuesDialog | 6 | 0 | 0 | `accessibility_consumers_tests.log` |
| TestCRSChangeDialog | 9 | 0 | 0 | `accessibility_consumers_tests.log` |
| TestClimatologyDialog | 9 | 0 | 0 | `accessibility_consumers_tests.log` |
| TestDialogA11yStandalone | 10 | 0 | 0 | `accessibility_consumers_tests.log` |
| TestMesh2DResultsExportDialog | 9 | 0 | 0 | `accessibility_consumers_tests.log` |
| TestGroundwaterExchangeDialog | 17 | 1 | 0 | `groundwater_final_tests.log` |
| TestSimulationOptionsRoundTrip | 37 | 0 | 0 | `integration_final_tests.log` |
| TestAsyncLoad | 22 | 0 | 2 | `async_final_tests.log` |
| TestRainfallVisualizationDialog | 8 | 0 | 0 | `rainfall_tests.log` |

The two skips are offscreen QSG rendering/readback and the optional external-model load profiler. Both remain acceptance items; they do not hide failures in the new ownership/loading cases. Release build and deployment exited 0. Strict deep signature verification passed. Installed and bundled engine UUIDs match `E31718A1-72CE-3B04-A7DA-AE5AA2C0A4A1`. The new GUI process **51593** launched **2026-09-30 19:11:05 EDT**; existing process 73377 was preserved. Launch is process-verified, not full native workflow acceptance. The build includes the shared workspace at HEAD `2ea06319` plus uncommitted work. Existing optional Mimer/ODBC/Postgres driver dependencies remain unresolved in deployment diagnostics.


The expanded integration fixtures were corrected after the first run: engine-close observers now use independent receiver objects so dialog cleanup cannot disconnect the probe; inlet assertions inspect the borrowed provider list rather than static dropdown choices. The accessibility helper excludes Qt's internal QTableCornerButton while continuing to require names on application-owned painted controls. Earlier logs remain retained as diagnostic history.

## Limits and acceptance accounting

- Native keyboard, VoiceOver, light/dark theme and enlarged-font checks remain pending; offscreen Qt tests and successful startup do not certify them.
- The installed engine's groundwater expression validator still accepts unknown `HGWW`; the existing failing assertion is retained. GUI validation cannot certify semantics the engine fails to reject.
- Multi-call source setters report partial success rather than promising rollback. General engine transactions, file-import parsing semantics and all possible concurrent external edits are not newly certified.
- Worker destruction waits for noninterruptible engine/library calls; safe lifetime handling does not imply a one-second shutdown bound. Async parsing remains in a worker, but array/cache preparation now occurs on the GUI thread for safe ownership. Large-model adoption responsiveness requires measurement; a detached transfer API is deferred.
- Run reference scanning covers known file grammar, not arbitrary plugin configuration internals. Filesystem substitution after preflight is not locked out. Solver output publication after Run begins is not transactional.
- W1 mesh remapping/live editing-engine synchronization and mesh-overhaul acceptance remain with the mesh work. Visualization/style/profile and forcing workflow work follows the consolidated batches 34/35.
- This report records the families actually changed and tested. It does not mark every entry in the application-wide dialog inventory verified. Remaining manifest entries and native acceptance retain their explicit status.
