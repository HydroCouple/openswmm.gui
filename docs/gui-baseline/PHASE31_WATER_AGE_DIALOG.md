# Phase 31 — Water Age Sources validation and accessibility

Status: implementation and focused regression verification complete; GUI build, signature verification and fresh launch complete. Native acceptance remains pending.

## Behavior and ownership

The Water Age Sources dialog buffers seven global source ages and per-node dry-weather/external-inflow overrides until OK. The engine stores overrides by source/node key, while the table allowed duplicates: the last authored row silently replaced an earlier row. An empty combo selection also became integer zero during write-back, allowing a missing node selection to target node zero. Global changes and removal of old overrides could already occur before a bad override was encountered.

The complete override draft is now checked before any global, removal or override write. Missing/unsupported source selections, missing or changed node identities and duplicate keys refuse OK with an inline message. The affected cell is revealed and focused, and all draft values are retained. Corrections can be retried; Cancel/Escape leave engine values unchanged. Negative extraction ages remain legal.

Global and override tables have meaningful accessible names. Global spins identify their source; override controls identify their current row and field, refreshed after deletion. Row/field context is also included in accessible descriptions because the native combo interface on the tested Qt build exposes the selected value as its name. Selection/value semantics are retained. Add/Remove cannot become automatic default Enter actions.

The modal editor is opened from the main-window Water Age Sources action and from Simulation Options → Quality → Reserved Species. Committed data belongs to the engine; the main-window launcher marks the project dirty after accepted changes. The nested options launcher follow-up is now covered by [Phase 32](PHASE32_NESTED_EDITOR_DIRTY.md).

## Verification

The original suite passed 8 checks. New regressions reproduced five failures: duplicate acceptance, missing source acceptance, missing node acceptance, unnamed accessible tables and auxiliary default buttons. The baseline had 10 passes / 5 failures, including setup/cleanup.

The first implementation run had 14 passes / 1 accessibility failure. Diagnostics showed a combo interface name equal to its selected value despite the widget's explicit accessible name. The final contract verifies row/field descriptions through Qt accessibility interfaces, preserves native combo names/values, and verifies spin names and renumbering after deletion. Initial diagnostics remain in the artifact directory.

Final focused build and CTest run exited 0: **15 passed, 0 failed, 0 skipped**, including setup/cleanup and all seven new cases. Final test/build/launch evidence is recorded in the [testing guide](../../workplans/artifacts/phase_31_water_age_dialog/README.md). Independent production review found no preflight blocker. This phase edits no meshing, engine, preferences or build-registration files.

## Application checkpoint

Application build exited 0 and deep/strict signature verification passed. Installed and bundled arm64 engine UUIDs match `E31718A1-72CE-3B04-A7DA-AE5AA2C0A4A1`. Fresh GUI PID 54801 launched 2026-09-30 at 14:16:01 EDT; startup is process-verified. Native acceptance and existing optional Mimer/ODBC/PostgreSQL packaging dependencies remain open. This shared-working-tree build includes the separate meshing overhaul and does not certify its acceptance gates.

## Remaining work

This is draft preflight, not atomic multi-call engine authoring. Engine read/write error propagation, lifecycle/concurrent edits, three-decimal display precision require separate work. Nested Simulation Options dirty tracking was completed in Phase 32. Native VoiceOver, keyboard, theme and enlarged-font acceptance is pending. The separate meshing overhaul retains its own acceptance gates.
