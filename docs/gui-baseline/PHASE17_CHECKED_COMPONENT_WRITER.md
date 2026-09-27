# Phase 17 — checked process-component configuration writes

## Behavior

W1/ENG-1 now covers attempted process-component configuration writes and copies. Previously a rendered configuration could be truncated, or a carry-alongside copy could fail, while the engine returned success. The GUI consequently cleared pending changes. A failed rendered write could also fall back to copying the original configuration, losing in-memory edits.

The engine now writes rendered configurations and copy fallbacks through its checked adjacent temporary-file writer. It checks copy reads, write lengths, flush, sync, close and replacement; ordinary failures discard the owned temporary file, return failure and name the component/configuration. A missing copy source fails. A failed rendered write never falls back to older source data. Successful replacement preserves the existing notice when destination content changes; repeated identical saves stay quiet.

Absolute configuration references previously could be rebased for the INP but written relative to the process working directory. Publication now resolves the emitted token against the destination model directory, matching the saved reference.

The existing GUI error path correctly surfaces these failures and retains pending state and Save As identity; this phase adds integration coverage rather than another GUI error mechanism.

## Validation

- Regression-first engine run: six failures reproduced before implementation (rendered destination refusal, copy destination refusal, missing copy source, absolute-reference destination, rendered short write and copied short write).
- The installed prior runtime also reproduced a false-success GUI Save As with a blocked reaction configuration destination.
- Five engine writer suites pass: **97 tests**, including eight new component cases. Coverage includes exact byte preservation on failure, successful retry, source preservation, same-source copy behavior, correct destination and repeated-save warnings. Tests check temporary-file cleanup recursively. POSIX short-write limits run only in child processes.
- The real-engine GUI regression confirms error propagation, unchanged previous model/settings bytes, dirty state and original identity, followed by successful repair/retry and reopen.
- Both complete GUI suites pass against the installed runtime: mesh writer **72** + Save **102** QtTest passes including fixtures (**174 total**).

Inputs, outputs and logs are retained under [Phase 17 evidence](../../workplans/artifacts/phase_17_checked_component_writer/README.md). The manual guide includes a safe owned fixture with a deliberately blocked configuration destination. Native manual acceptance remains pending.

## Runtime

The engine and matching GPU runtime were rebuilt from the current working tree, which also contains separate work. Prior installed binaries and before/after hashes are retained. Installed public headers were checked against staged headers and match; this phase adds no public API. The known generated GPU installer quoting problem was avoided by staging the built plugin and applying its loader paths/signature directly. This does not repair that separate installer.

The complete GUI build/deployment exited 0. A new instance launched as PID **84676** at **2026-09-26 07:56:59 EDT**, alongside the untouched earlier instance. Installed and bundled engine UUIDs both equal **BFAD6A25-1A44-3F6D-A657-EB95FCC2C88D**. Launch is process-verified; native manual acceptance has not been performed. The existing optional Mimer/ODBC/PostgreSQL dependency warnings remain a W9 packaging limitation.

## Remaining W1 work

This is an individual-file guarantee. An earlier component, mesh or main-model publication is not rolled back if a later output fails. Existing absolute references for components that decline rendering remain input references, not copied outputs; this phase does not certify their availability. Component destination ownership/alias checks, explicit output manifests/redirection, whole-project staging/publication/rollback, interrupted-save recovery, generation/import staging and live-engine topology/remapping remain open. Tests were run on macOS; other platforms, native interaction and release packaging remain separate acceptance gates.
