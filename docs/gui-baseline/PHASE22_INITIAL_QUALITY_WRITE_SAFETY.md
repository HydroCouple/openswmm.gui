# Phase 22 — Checked Initial Quality writes and dirty notifications

This W3 checkpoint follows the Phase 21 row-identity fixes. It covers the Initial Quality editor and its main-window, Simulation Options and property-editor entry points. It does not make the sequence of engine mutations atomic.

## Reproduced/source findings

`writeToEngine()` counted successful writes but ignored individual API refusals. `onAccept()` always accepted and overwrote the dialog's change flag with the most recent write count. A valid first edit followed by an unknown constituent could therefore partly modify the model and close the dialog without showing the error. Retry/Cancel and same-count property edits lacked reliable ownership notification. The property adapters also emitted their dirty signal for every focus-out assignment, even if no change occurred.

The pre-fix regression run reproduced **four failures**, with 16 checks passing: partial-write failure closed the editor, first-write refusal closed it, a lifecycle change still accepted, and no-model authoring actions remained present. Evidence is retained under `workplans/artifacts/phase_22_parallel/dialogs/baseline_tests.log`. The integrated run passed **27 Initial Quality checks and 24 node-adapter checks**, with no failures or skips (QtTest totals include initialization/cleanup). Evidence: `workplans/artifacts/phase_22_parallel/final_tests.log`. The Initial Quality target now also compiles its property button/ref implementation to exercise unchanged OK, untouched Cancel, same-count edits and partial-write Cancel.

## Contract

- Check reads and every file-reference, removal and upsert result. Stop on the first refusal, identify the affected field/row, keep the draft open and return focus for correction.
- Count actual successful mutations in the current attempt, retaining cumulative change ownership across attempts and Cancel.
- No-op OK, untouched Cancel and pre-write lifecycle refusal must not mark the model dirty.
- Partial writes remain applied; the dialog must say so and notify the owning project even if the user then cancels.
- Read-only lifecycle states allow inspection/Close; a lifecycle change after opening is rechecked before writes.

## Implementation and entry points

Every saved-row read, FILE reference get/set, row removal and row upsert now checks its return code. Incomplete element/constituent selections and duplicate element/constituent rows fail before mutation. Known pollutant IDs are canonicalized through the engine lookup for duplicate comparisons; case-sensitive reaction-species names retain their spelling. This prevents a duplicate final row from being incorrectly skipped against the pre-write snapshot. Refusals identify the affected row or file reference and retain the draft; the editor restores focus to the relevant selector/reference after the warning. A fresh engine snapshot is read for each retry, so already-applied rows are not counted or written again.

`lastWriteCount()` records the current attempt; `wroteAnyChanges()` remains true after any successful mutation, including a failed attempt followed by no-op retry or Cancel. `changesApplied()` fires only when an attempt made actual successful writes. No-op OK and lifecycle refusal do not notify.

The main-window launcher and Simulation Options Quality page connect this notification to their owning project's dirty flag immediately. The main-window launcher also refreshes the Attribute Table after partial-write Cancel. The property button retains a `wroteChanges` flag in its USER-property value, emits only after actual writes, and preserves the flag across Cancel. Node/link adapter setters consult this flag instead of dirtying the project for an unchanged focus-out assignment. A changed value with the same row-count summary still carries the flag.

Property integration uses the existing delegate's USER-property commit route. The adapter regression exercises the corresponding `QMetaProperty::write`; native timing/focus acceptance remains pending. The third-party QPropertyModel implementation is unchanged.

## Native acceptance

1. In Initial Quality, edit a valid row, then add/import a row with an unknown constituent. OK must report the failing row, keep both drafts visible and mark any earlier applied change unsaved. Correct the constituent and retry.
2. Repeat and Cancel after the failure. Earlier applied values must remain, and closing the project must prompt about unsaved changes. Cancel is not rollback.
3. Open/OK without changes, and open/Cancel without changes. Neither should dirty a clean project.
4. Repeat through Simulation Options → Quality and through node/link Initial Quality property cells. A concentration change that keeps the same row count must still mark the project dirty; cancelling the outer options dialog must not hide inner writes.
5. Open while initialized/running: inspect with Close-only controls. If the engine state changes after opening, OK must retain the draft and refuse writes. Verify keyboard correction focus and screen-reader naming in both themes.

Atomic rollback, cross-dialog stale-resource lifetime management and bulk undo remain future operation-safety work. The CSV reference setter only changes the stored path and does not reload rows or invalidate saved row indexes; this checkpoint does not claim it validates the referenced file or replaces the save-time resource-validation gate.
