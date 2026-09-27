# Phase 20 — 2D groundwater dialog consistency and accessibility

This is a bounded W3 checkpoint covering `Mesh2DGroundwaterDialog` and its Options, Aquifer, Node Beds and State pages. It does not close the remaining dialog inventory.

## Verified source findings

- OK called `onApply()` and then unconditionally accepted, even after the engine refused a row. The dialog discarded the visible draft when the warning was dismissed.
- The editability check called the MODE setter with the current value. That setter marks groundwater options authored, so opening and cancelling the dialog could change subsequent model serialization.
- The no-model and read-only states left Apply/OK visible, although the authoring API refuses writes outside BUILDING/OPENED.
- Options had no keyboard mnemonics; aquifer/node tables and state output had no effective explicit accessible names. The dialog had no object name for the shared geometry-persistence mechanism.
- Add/remove/refresh buttons used Qt auto-default behavior, allowing focused auxiliary actions to become the Enter action.
- The `openGwDialog` launcher did not mark the project dirty after Apply, including partial writes on a later row failure.

## Ownership and interaction contract

The engine owns committed groundwater options/rows. The two table models and option widgets buffer edits inside the modal dialog; Apply writes them and keeps the dialog open, and OK accepts only after successful application. Cancel/Escape discards unapplied drafts but does not undo successful or partial writes from an earlier Apply. Read-only mode allows state inspection/refresh and Close. Displayed authoring units remain the project's units; the State page explicitly retains SI diagnostics.

Before writes, Apply rechecks the engine lifecycle so a run started after the dialog opened does not emit a write notification. `changesMayHaveBeenApplied()` marks the owning project dirty before the multi-call write sequence, including a later failure; `applied()` is emitted only when the complete sequence succeeds. This is conservative dirty tracking, not atomic authoring. The launcher connects the notification to the specific project that opened the modal dialog.

## Implementation and verification

The shared form labels now expose seven distinct mnemonics through their field buddies. Both tables and the read-only state output have explicit accessible names; table descriptions explain keyboard editing. Auxiliary Add/Remove/Refresh buttons cannot take over Qt's default Enter action. A stable dialog object name enables the existing geometry persistence mechanism. No shared theme or style overrides were introduced.

The pre-fix run reproduced four failures: Close-only read-only behavior, mnemonic labels, failure-safe OK and auxiliary default-button behavior. Six existing/new checks passed in that baseline. Evidence: `workplans/artifacts/phase_20_parallel/dialogs/baseline_tests.log` and `baseline_build.log`.

Additional regression coverage exercises byte-for-byte model serialization before open and after Cancel, initialized-state controls, lifecycle change while the dialog remains open, failed Apply/repaired retry, success-only notification, conservative write notification, and Escape discarding an unapplied row. Before/after INP files and the engine report remain under `workplans/artifacts/phase_20_parallel/dialogs/output` (overridable with `SWMMVIS_GROUNDWATER_DIALOG_TEST_OUTPUT`).

The final `test_mesh2dgroundwaterdialog` target rebuilt successfully and passed **12 QtTest checks (including fixture initialization/cleanup), with no failures or skips**. Evidence: `workplans/artifacts/phase_20_parallel/dialogs/final_tests.log` and `refreshed_build.log`. No new target registration was required. The parent integration owns the combined GUI build and launch; passing this offscreen target is not native UI acceptance.

## Native acceptance steps

1. Open a model, then the 2D mesh Aquifer dialog. Tab through the Options fields, table controls and bottom action buttons. Check named controls in VoiceOver.
2. On Node Beds, add a bed with positive conductivity and zero thickness. Choose OK; after dismissing the validation warning the dialog and values must remain available. Correct thickness and retry.
3. On Aquifer, add a draft row, press Escape, and reopen: the unapplied row must be absent. After a successful Apply, Cancel must retain the applied row and the project must remain marked unsaved.
4. Open the State page without a model and while a simulation is initialized/running. Only Close should remain in the bottom action area; editing stays disabled and state output remains selectable.
5. Repeat both themes and enlarged system font; resize/reopen the dialog to inspect geometry restoration. These steps require native acceptance; offscreen QtTest does not certify them.

## Remaining acceptance

Native keyboard/VoiceOver inspection, both themes and enlarged-font layout acceptance remain pending. This checkpoint does not make multi-row engine commits atomic, add undo, or implement the new species/forcing architecture. Current table commits may write earlier rows before a later engine rejection; preserving the dialog draft does not itself roll those engine changes back. These larger operation/undo requirements remain W1/W3 work.
