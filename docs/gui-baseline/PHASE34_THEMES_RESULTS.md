# Phase 34 — Themes, icons and configurable results

Status: implementation, automated verification, compilation, signature verification and GUI launch complete. This is the second consolidated delivery. It establishes file-backed scalar/species configuration and closes tested style-session defects; it does not certify every visualization or engine transport path.

## Delivered behavior

| Area | Change |
| --- | --- |
| Surface and groundwater results | A metadata catalog identifies variables by domain, dataset, species name and explicit sigma layer. Reordered species resolve by name; an unavailable saved variable is retained without substitution. Rank, shape, scalar metadata, fill values and unallocated HDF5 chunks are validated. |
| Independent visualization | Additional Results supports multiple scalar fields with separate visibility, opacity, classification and status colours. Shared CPU/QSG colours, legends and selected-cell inspection use immutable frames. Groundwater remains visible beneath a dry surface. |
| Time and classification | Current-frame ranges are the default. Whole-run extrema and bounded deterministic sample summaries are available for completed sources; explicit ranges avoid scans. Asynchronous rendering retains the displayed frame for its legend until replacement geometry is ready. |
| Units and export | Native units and distinct valid, missing, waterless and not-applicable states survive inspection and current-frame CSV export. Unknown groundwater concentration units are labelled; scientific export waits for declared units. CSV publication is atomic and protects the input source against path aliases. |
| Persistence and style sessions | Dynamic sublayers, order, settings and semantic keys round-trip through host/style serialization. Cancel, Escape and window close restore previews; Apply retains its state and the session contributes one undo action. Untouched numeric precision and general fields are preserved. Malformed structural payloads are rejected before mutation. |
| Classification controls | Invalid ranges, logarithmic bounds and manual breaks retain an explanatory draft instead of silently changing the model. Theme changes preserve scientific colours. Built-in ramp names and aliases resolve consistently. Legacy hydraulic legends use actual values and scheme intervals. |
| Icons and accessibility | Five revised SVGs clarify groundwater parameters/initial state, cell/polygon selection and GIS import. Cell selection has an effective catalog binding and meaningful action name. Light/dark, disabled/selected, native-size and high-DPI icon sheets accompany the resource audit. |

## Verification record

**215 checks pass across 11 suites, with no failures or skips.** Counts include QtTest setup/cleanup. All tests ran after the final binaries linked; both asynchronous suites explicitly enabled asynchronous contour processing. See the [machine-readable summary](../../workplans/artifacts/phase_34_themes_results/final/verification_summary.json).

| Suite | Passing checks |
| --- | ---: |
| Classification model | 19 |
| Style controls | 33 |
| Style-dialog transactions | 18 |
| Result source/layer/render integration | 9 |
| Hydraulic asynchronous rendering | 21 |
| Additional scalar sublayers | 16 |
| HDF5 reader | 32 |
| Scalar CSV export | 14 |
| Existing 2D sublayers | 29 |
| Mesh boundary/coupling styles | 13 |
| Icon integrity and bindings | 11 |

Chrome lint and the affected source diff check passed. The workspace-wide diff check additionally reports existing whitespace in unrelated generated INP fixtures; those shared outputs were preserved. The final Additional Results screenshot was inspected: controls, selected-cell values and export remain visible, and informational classification text uses normal readable foreground colour. Icon sheets were reviewed at the recorded sizes/states.

Release build/deployment exited 0 and strict deep signature verification passed. Installed and bundled engine UUIDs match `E31718A1-72CE-3B04-A7DA-AE5AA2C0A4A1`. New GUI PID **31998** launched **2026-09-30 20:12:40 EDT**. Native accessibility state and screenshot confirmed the main window/welcome page; no project or preferences were changed. The separate animation-verification instance 73913 was preserved. This build contains shared workspace HEAD `2ea06319` plus uncommitted changes. Existing optional Mimer/ODBC/Postgres dependencies remain unresolved in deployment diagnostics.

All fixtures, diagnostic failures and final logs are retained in [the evidence directory](../../workplans/artifacts/phase_34_themes_results/README.md). The integration fixture connects an actual HDF5 source to the catalog, frame cache, style, reopening and exported CSV; it is a small UI regression fixture, not a hydraulic benchmark.

Baseline tests reproduced reader, icon, classification, control and transaction gaps before their corresponding corrections. Integration also exposed a teardown crash caused by disconnecting Qt's internal destruction bookkeeping; teardown now blocks editor signals while preserving those connections. The original lifetime assertions remain. A final control regression exposed case-sensitive display-name matching for the internal `viridis` ramp alias; the authoritative built-in table now resolves both and additional data rows cover aliases, whitespace and unknown names.

Independent reader, style and icon/render tasks used separate ownership. One coordinator integrated the result source/layer contracts, shared build registration and application checkpoint. The shared workspace includes other agents' meshing and hydraulic-rendering work; their existing implementation and acceptance ownership are preserved.

## Completion boundary and next batch

The [configuration coverage matrix](PHASE34_CONFIGURATION_COVERAGE.md) traces model/editor, rendering/legend/probe, persistence/reopen and export for each family. It distinguishes existing paths, implemented changes and remaining requirements. Arbitrary species contours/labels, generic GIS raster/vector export and live species streaming remain requirements. The installed API and groundwater unit/runtime/conservation evidence constrain those paths; raw-file inspection does not prove transport advancement.

Native keyboard/VoiceOver/theme/scaling, large-run scan latency, complete scientific export/print and hydraulic/species ledger checks remain acceptance work. Offscreen widget screenshots, scene-graph geometry tests and successful launch do not substitute for those checks. Optional database-driver packaging dependencies remain tracked with release readiness.

Phase 35 combines groundwater/surface sections and common manual, rectangular/polygon, feature-layer and raster forcing workflows. Phase 36 combines outstanding integration, native/performance and release acceptance. The [testing guide](../../workplans/artifacts/phase_34_themes_results/README.md) gives a short review path for this checkpoint.
