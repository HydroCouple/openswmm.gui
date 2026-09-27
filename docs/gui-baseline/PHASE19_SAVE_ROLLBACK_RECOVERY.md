# Phase 19 — save rollback and interrupted-save recovery

## Behavior

Built-in INP Save now keeps checked old/new copies of its output set and publishes a recovery journal before replacing final files. The journal is adjacent to the resolved model path (`model.inp.openswmm-save.json`); its private data directory contains numbered copies and SHA-256 fingerprints. Components and mesh/settings outputs still publish before the model. A final committed marker distinguishes a complete new save from an interrupted operation.

A detected publication error attempts to restore the complete previous file set. Outputs that did not previously exist are removed; existing files and their recorded permissions are restored. Pending GUI changes and the previous Save As identity remain intact. The failure message distinguishes successful rollback from a retained journal needing recovery.

Normal and background model opening check for interrupted saves before engine parsing. An uncommitted journal restores the previous files. A committed journal verifies the new files and finishes cleanup. Recovery reports a notice through the existing load-warning channel. Ordinary opening of a read-only project needs no write access when there is no pending recovery.

Recovery validates the complete manifest and rollback copies before making repairs. If a destination differs from both its recorded old and new contents, or a backup is damaged, opening stops with the affected path and retains the recovery evidence. Reopening after the conflict has been resolved can retry recovery; the application does not guess which outside edits to discard. Malformed journals and redirected backup-directory names are refused.

A per-model lock prevents cooperating saves/recovery operations from overlapping. Existing symlink destinations retain their links. Destination fingerprints and resolved paths are checked from staging through publication; a changed file is not silently replaced. Journal and lock paths are reserved from ordinary outputs. Cleanup removes only known transaction files, never a directory tree recursively.

## Validation

The dedicated Qt Core suite uses actual child-process termination, without destructors, at the journal-prepared boundary, after each of four file replacements, and after the committed marker. It covers existing projects and previously absent Save As model destinations. Assertions compare all final bytes, newly created output removal, retained committed results, repeat recovery and cleanup.

Additional checks cover deterministic late failures, an actual output-directory failure followed by repair/retry, outside edits, damaged backups, malformed/redirected journals, rollback conflicts, competing saves, read-only opening and symlink preservation. GUI integration opens a journaled invalid replacement through both synchronous and asynchronous paths, verifies recovery before engine parsing, checks the recovery notice, and saves the recovered model again.

Final validation: **30 recovery + 111 GUI Save + 72 mesh-writer QtTest passes (213 total, including suite fixtures)**, with zero failures or skips. The 12 process-interruption cases cover existing and new model destinations at all six boundaries. Both GUI Open recovery cases pass. The successful GUI/writer output scan finds zero remaining staging/journal artifacts.

The GUI build and final recovery-target build exited 0. Strict deep signature verification passed without the previous phase's signing repair. The new GUI launched as PID **27523** at **2026-09-26 09:26:53 EDT**; prior instances remain untouched. Installed and bundled engine UUIDs match (**49D9E75A-CC1C-3EDC-BE4E-DFF1724ABFE9**). This phase changes GUI code only and uses the previously installed engine runtime; engine suites were not rerun. The existing optional Mimer/ODBC/PostgreSQL deployment errors remain a W9 packaging limitation. Launch is process-verified; native manual acceptance remains pending. [Retained evidence and manual test guide](../../workplans/artifacts/phase_19_save_recovery/README.md).

## Scope and remaining work

This is a process-interruption recovery checkpoint, not certification against sudden power loss. Files and journals use checked `QSaveFile` replacement, but parent-directory durability, filesystem-specific failure behavior and non-macOS platforms still need validation. A crash before the journal is published can leave private preparation copies; a crash during cleanup can leave unused copies. Empty directories created for new outputs may remain after rollback. No arbitrary orphan-directory deletion is attempted.

The lock coordinates one resolved model path. Different models sharing a companion file, hard-link aliases of model paths, and noncooperating external writers are not fully serialized. Hash/path checks detect many conflicts but do not eliminate filesystem races. The snapshot starts when each output is registered, not when the project was originally opened; full project revision/worker ownership remains open.

Recovery copies and verification add disk usage and file passes, particularly for large meshes. Existing mesh read/write counters describe preparation only and do not include transaction hashing, snapshots or rollback. Performance measurement and copy/rename optimization remain W1 work.

Semantic reopen validation of the entire staged model/resource combination is still required before publication. Engine reference adoption after a filesystem commit can still fail and retain dirty state; it is outside this filesystem transaction. Generation/import staging, stale jobs, live-engine topology/remapping, plugin exports, application-wide journal discovery and native acceptance remain open. This phase does not change dialogs, meshing or visualization controls.
