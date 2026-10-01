# Phase 30 — Groundwater Exchange draft validation and accessibility

Status: scoped implementation and regressions complete; GUI build, signature verification and fresh launch complete. Two pre-existing runtime-validator failures remain open. Native acceptance is pending.

## Behavior

The modeless subcatchment Groundwater Exchange editor previously trusted delayed validation flags when Apply was clicked. Normal editing waits 250 ms before validation; Apply during that window could write invalid lateral or deep text and other draft values. The engine expression setter stores text without parsing it.

Apply now validates both current expression drafts before the first node, parameter or expression write. Refusal leaves the dialog and all draft values intact, scrolls to the first invalid expression and focuses it. Correcting the draft restores Apply through the existing delayed validator. Close/Escape retains existing discard-without-Apply behavior.

Lateral and deep editors have distinct accessible names, descriptive context and mnemonic label buddies. Variable-insertion buttons and validation statuses are named by expression role. Current validation diagnostics are included in the editor's accessible description and removed when corrected. Captions and error text are explicitly plain text.

This is W3 work on a compound subcatchment editor. No mesh-generation, preferences, meshing tests, engine or build-registration files were edited by this phase.

## Verification and known runtime gap

The independent regression author added five cases to `test_groundwaterexchangedialog`: immediate invalid lateral/deep Apply and valid retry, accessible identities, Close and Escape. Before the fix, the two immediate-Apply cases falsely emitted success and the accessibility case failed. Afterward all five new cases pass. Tests verify that refusal preserves the receiving node, all eight parameters and both engine expressions, while retaining draft controls and focus.

Both test targets build. Final full focused results are **16 passed, 2 failed, 0 skipped**: dialog 11/1 and expression widget 5/1, including setup/cleanup. Both remaining failures reproduce before this phase: the installed engine validator accepts the unknown name `HGWW`. A direct C API probe confirms success for `HGWW`, rejection for `HGW + (` and success for a valid expression. Existing assertions are unchanged; these suites are not reported green. The GUI still depends on engine validation and cannot reject errors that the engine incorrectly accepts.

Independent implementation review found no blocker. Scoped whitespace checks pass. See [evidence and manual checks](../../workplans/artifacts/phase_30_exchange_dialog/README.md).

## Build and launch

The application build exited 0; deep/strict signature verification passed. Installed and bundled arm64 engine UUIDs match `E31718A1-72CE-3B04-A7DA-AE5AA2C0A4A1`. Fresh GUI PID 31456 launched 2026-09-30 at 13:46:58 EDT. This is a build of the shared working tree, including the separate meshing overhaul; it does not certify that overhaul. Startup is process-verified. Existing optional Mimer/ODBC/PostgreSQL packaging dependency diagnostics remain a W9 release gate.

## Remaining work

Unknown-variable engine validation, unchecked multi-call setter failures, atomic Apply/undo, stale engine/model ownership and fixed-buffer loading of long expressions remain outside this checkpoint. Native screen-reader, keyboard, both-theme and enlarged-font acceptance is pending. This phase does not complete W3 or the groundwater forcing package.
