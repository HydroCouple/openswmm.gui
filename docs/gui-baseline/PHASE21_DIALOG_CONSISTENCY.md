# Phase 21 — Initial Quality dialog row identity and accessibility

This bounded W3 checkpoint covers the Initial Quality editor for node/link pollutant, reserved and reaction-species concentrations. It does not add groundwater or surface-mesh initial-quality authoring, nor close the entire dialog family.

## Source findings and intended contract

- Scope selectors captured their original table row index. Removing an earlier row left the surviving selector connected to the wrong row, so switching Node to Link could keep node IDs in the element list or alter another row.
- CSV-backed rows were tracked by integer positions that were not shifted after deletion. Adding a row after removing an earlier inline row could skip the new inline row at OK and accidentally treat a CSV-backed row as authored data.
- Remove accepted CSV-backed rows even though the editor describes them as read-only.
- The CSV field had no label buddy; the table and embedded cell editors had no explicit accessible names. Add/Remove/Browse/Import buttons used Qt auto-default behavior.

The engine is the owner of committed initial-quality values. Dialog fields buffer changes until OK; Cancel/Escape must leave the engine untouched. FILE-backed rows remain owned by their CSV and must remain read-only when surrounding inline rows are edited. Scope changes must operate on the same surviving row after any earlier row deletion.

## Implemented behavior

Scope-change callbacks now keep a persistent model index, which follows a surviving row when an earlier row is removed. Deletion shifts the retained CSV-row index set before table signals run. CSV-backed rows cannot be removed; selecting one disables Remove. New inline rows remain eligible for write-back after adjacent removals.

The table has a meaningful accessible name, every embedded editor is named by its current row and column, and CSV-backed editors include an origin description. These names refresh when rows move. The CSV label is a mnemonic buddy, and Add/Remove/Browse/Import cannot take over the default Enter action. The existing object name continues to provide the shared dialog persistence hook.

## Verification

Regression tests are in the existing `test_initialqualitydialog` target; no build registration is needed. A real INP and CSV fixture and test logs are retained under `workplans/artifacts/phase_21_parallel/dialogs/` (fixture output can be overridden with `SWMMVIS_IQ_DIALOG_TEST_OUTPUT`). The pre-fix run reproduced **four failures** (scope-row identity, CSV provenance/data loss, CSV label buddy and auxiliary defaults); 12 checks passed. The CSV regression showed only one retained engine row instead of the expected two. Evidence: `dialogs/baseline_tests.log` and `baseline_build.log`. **Final integrated result: 16 passed, 0 failed or skipped**, including QtTest fixtures. Evidence: `workplans/artifacts/phase_21_parallel/final_focused_tests.log`. The application build/launch is recorded in the consolidated phase guide.

## Explicit remaining work

This checkpoint does not change `writeToEngine()`'s existing multi-call mutation/return contract, transactional failure handling, stale-model protection or launcher dirty integration. That writer currently counts successful calls while ignoring individual engine errors; fixing partial writes and acceptance together requires the editor and its main-window/property-editor launchers to be covered as one later W1/W3 slice. It also does not certify units/import parsing, theme contrast, enlarged fonts or native assistive-technology interaction.

## Native acceptance steps

1. Open Initial Quality with two inline rows. Delete the first row, switch the surviving row from Node to Link, and verify that its element choices are links. Commit and reopen to verify the chosen link/value.
2. Open a model with inline and FILE-backed rows. Remove an inline row before a CSV row, add a replacement inline row, commit and reopen. Both the replacement value and original CSV value must remain; selecting a CSV row must disable Remove.
3. Tab through the CSV field, table cells and auxiliary actions. Inspect row/column names and CSV-origin descriptions using VoiceOver; verify that Escape discards unapplied drafts.
4. Repeat with both themes and enlarged fonts. Native focus, screen-reader and layout acceptance remain pending; offscreen automated checks are not certification of these behaviors.
