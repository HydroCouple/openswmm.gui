# Phase 18 — controlled save outputs and staging

## Behavior

Built-in GUI Save now prepares an explicit set of outputs before publication: the model, selected GUI mesh, engine-owned mesh when applicable, written component configurations and project settings. The engine's new synchronous `swmm_model_write_staged` API reports each actual output to a caller-supplied mapper. Reference tokens continue to use final destination paths. Existing direct/plugin write APIs retain their previous semantics.

The GUI mapper reserves private staging files and detects duplicate destinations, role collisions and aliases with protected model/settings/mesh inputs. Model and settings staging files are true siblings so existing relative-path serializers keep the correct final-directory anchor. New nested component directories are not created during preparation; those stages use the nearest existing ancestor. Ordinary completion/failure removes only the operation's staging files.

Mesh patches, model mesh-reference patches and settings serialization finish before publication starts. A preparation error therefore leaves final files unchanged, retains pending edits and the original Save As identity, and supports retry. The GUI no longer warns that final files may already have changed when only staging has run.

Before publication, the manifest rechecks destination types, known aliases and protected inputs. Components, meshes and settings publish before the main model. Individual publications still use checked replacement. A failure after publication starts can leave a mixed file set; rollback and crash recovery are the next step, not a guarantee of this checkpoint.

## Validation

- Regression-first engine tests reproduced three cases in which redirection was ignored and final outputs changed or new final directories appeared.
- The old GUI flow reproduced three settings-save failures after the model had already been changed/created: Save, Save As and untitled Save As.
- Engine writer tests cover all three mapped output kinds, correct final references even with a staging directory elsewhere, no final directory creation for redirected nested configurations, callback refusal, refusal of a final-destination hard-link alias, and child-process short writes preserving all final outputs.
- GUI tests add output-manifest publication/cleanup, path/hard-link/symbolic-link/protected-input/duplicate-output conflicts, and alias rechecking immediately before publication. Existing failure cases now additionally compare saved model bytes, including mesh patch mismatches. Existing metric/US-unit, mixed-mesh, repeated-save, relative settings and reopen tests remain part of the full suites.

Final engine validation: five suites, **102 tests passed**. An independently compiled Qt Core manifest probe also passes staging/publication/collision/cleanup checks. Both full GUI suites pass against the reviewed installed runtime: mesh writer **72** + Save **109** QtTest passes including fixtures (**181 total**, no failures or skips). The extended component failure test includes an edited external mesh and verifies reference restoration, unchanged saved model/settings/mesh bytes, repair/retry, final references and reopen. A post-suite scan found **zero remaining GUI staging files**. The final GUI build/deployment exited 0. Launch and packaging details are recorded below. [Retained evidence and manual guide](../../workplans/artifacts/phase_18_controlled_save_staging/README.md) include a safe settings-failure fixture.

## Runtime and review scope

The GUI requires the updated engine C API. The final reviewed installation is recorded under `reviewed_runtime/runtime_install_manifest.json`; the top-level install manifest records the initial installation before the comparison guard review. Runtime installation retains the prior engine/GPU binaries and changed public header, with recorded hashes. Builds use the current working trees, including separate rainfall/plotting, HDF5 and packaging work. This phase preserves those changes. An overlapping GUI build was detected; this phase's build was stopped and deferred to avoid competing writes to the same build directory.

The component comparison guard skips non-regular final destinations when staging redirects physical writes; their publication validation belongs to the caller. Legacy direct writes still validate their destination through the checked output writer.

## Build and launch

The final build completed successfully. Verification found that the cached build recipe still copied `libQPropertyModel.1.1.0.dylib` into `Contents/MacOS` after bundle signing. The existing CMake source fix from the separate packaging work was preserved. The copied library and bundle were re-signed; **strict, deep signature verification passed**. Configuration was explicitly refreshed with the pinned SDK and existing dependency installation setting, and the regenerated recipe no longer contains that post-sign copy. No new source change to the other agent's packaging fix was needed.

The verified app launched as PID **95013** at **2026-09-26 08:50:53 EDT**, alongside the untouched prior instances. Installed and bundled engine UUIDs both equal **49D9E75A-CC1C-3EDC-BE4E-DFF1724ABFE9**. Launch is process-verified; native manual acceptance remains pending. Known optional Mimer/ODBC/PostgreSQL deployment warnings remain a W9 packaging limitation.

## Remaining work

This in-memory manifest is not a recovery journal. Full old/new file identity snapshots, stale revision handling, staged-model/mesh semantic reopen validation before publication, rollback of earlier publications, directory durability and restart recovery remain open. A process crash may leave orphan staging files. The selected external-mesh path still uses temporary engine inline serialization followed by GUI patching; a reference-only mesh mode and copy/rename optimizations remain relevant to performance. Successful external Save now involves staging and publication copies (three mesh read/write passes), rather than Phase 15's single final mesh commit alone. Generation/import staging, live-engine topology/remapping and plugin/export atomicity remain separate W1 items. Native UI acceptance, non-macOS platforms and release packaging are not certified by these tests.
