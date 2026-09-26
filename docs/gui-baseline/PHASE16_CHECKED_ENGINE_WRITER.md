# Phase 16 — checked engine model and mesh writes

## Reconciled handoff

Phases 01–15 were already committed locally, ending at GUI `0e7c897d`. The interrupted Phase 16 implementation and subsequent agent work had passing engine and GUI tests, but no completed runtime installation, application launch or phase report. This checkpoint finishes that work. Separate rainfall/plotting implementation and unrelated staged documentation are outside this commit.

## Behavior

The engine writes each main INP and engine-owned external mesh to an exclusively created sibling file. It checks stream errors, flush, file synchronization and close before atomically replacing that destination. Ordinary failures discard owned temporary files and return failure. Existing output bytes survive failed serialization. Successful symlink saves update the resolved target and retain the link; dangling links are refused with the requested path in the diagnostic. Existing file permission bits are retained, and read-only/non-regular outputs are refused.

Engine mesh-file failures now propagate to the caller instead of becoming warning-only Save successes. The GUI includes the engine's new diagnostics in its failure message, keeps pending changes and the previous Save As identity, and permits retry.

The existing `Swmm5Stock` declaration prerequisite was committed separately as engine `4895765e` during this review. Phase 16 engine changes are committed as `2daef06b`. Other stock-profile, annotation, rainfall and plotting edits remain separate.

## Validation

- The prior installed engine reproduced a false-success GUI Save under a child-process file-size limit. That run cleared pending state after incomplete output; retained evidence is under `workplans/artifacts/phase_16_checked_engine_writer/regression_output` and `red_installed_engine`.
- Current engine validation: **five suites, 89 tests passed**, including **12 atomic-output tests**. Coverage includes existing/Unicode destinations, permissions, missing directories, abandoned writes, replacement failure, symbolic links, buffered flush failure, main-model short writes, sidecar failures, cleanup and retry. Existing 2D, relative-path, Save As and model round-trip suites also pass.
- GUI regression covers both Save and Save As, byte preservation, diagnostic propagation, retained identity/dirty state, successful retry and reopen. Both GUI suites pass against the installed runtime: writer 72 + Save 101 QtTest passes including fixtures (173 total).
- The 2D writer suite accepts `OPENSWMM_WRITER_TEST_OUTPUT` to retain its cases. Atomic tests use `OPENSWMM_ATOMIC_WRITE_TEST_OUTPUT`. The phase runner copies other source fixtures into an owned workspace. Reviewable logs, inputs and outputs live under the phase evidence directory. The regenerated whole-working-tree inventory was validated and retained in `working_tree_inventory`; it includes separate plotting changes, so it is not folded into this phase's committed baseline.
- Tests run on macOS. Windows and Linux acceptance, ACL/ownership preservation, abrupt termination and power-loss recovery are not certified by these tests.

## Runtime and testing

The engine runtime and matching GPU plugin were rebuilt from the current engine working tree, including its existing separate changes. They are not an isolated build of only the phase commit. Public engine headers were installed with the runtime; prior installed files and before/after hashes are retained in `installed_engine_before` and `runtime_install_manifest.json`.

The GPU install script's post-processing emitted a quoting error; dependency inspection confirmed its OpenMP reference already used `@rpath`. The missing loader-relative search path and local signatures were applied to the staged copies before publication. This does not fix that separate installer script.

Final GUI build/deployment exited 0. PID **50539** launched at **2026-09-26 07:35:10 EDT**. Launch is process-verified. Installed and bundled engine UUIDs both equal **54B524F6-1D03-3212-9EA9-33D129B5B2A9**, confirming deployment of the updated runtime. The packager still reports the known optional Mimer/ODBC/PostgreSQL dependencies; release packaging and native manual acceptance remain open.

To check the GUI, open an owned copy of a small model, edit a value, Save, then Save As to another filename and reopen it. Verify the edit in both saved models. Use the automated failure tests for short writes; no disk filling or machine-wide limit changes are needed. Native manual acceptance remains pending.

## Remaining W1 work

These are individual-file guarantees. If the engine publishes a mesh and its subsequent main-file publication fails, the mesh is not rolled back. The GUI's later mesh/reference/settings stages can also fail after the model was committed. Component-config copying remains a separate side effect. Full output manifests/redirection, coordinated staging/publication, rollback, crash recovery, generation/import staging and live-engine topology/remapping remain open. Directory synchronization and recovery after abrupt termination require that larger transaction design.
