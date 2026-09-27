# Phase 24 — mesh imports publish on Save

## Working-state contract

Importing a standalone native or SMS mesh now prepares a private snapshot beside the working project. A pathless new project uses application temporary storage, so importing does not require write access beside its source. The map adopts the parsed snapshot as the active external mesh, with an unsaved marker. Import does not copy, delete, convert or replace final model/mesh files and does not retarget the editing engine's mesh reference. The imported layer owns its topology until reload, including on repeated Save.

The source is copied with checked reads/writes and before/after identity and content checks. Parsing uses only that snapshot. SMS conversion replaces only the private snapshot; a same-folder SMS source receives a separate `_imported` destination. Native meshes retain sections outside the GUI's ownership during Save. This preserves bytes; it is not a guarantee that every opaque section is supported by the engine's external-mesh reader.

Known file-bearing directives that cannot safely be relocated are refused with an actionable message. Dependency rebasing and full groundwater/quality topology remapping remain separate work. Existing model assignments indexed to old cells, edges or tags remain subject to the earlier topology-replacement guard.

## Save and ownership

The pending mesh joins the existing output transaction as a Mesh resource. Its sealed snapshot supplies the patch template, even when an old destination exists. Save validates the prepared model/mesh combination and publishes model, mesh and settings through the existing journal and recovery path. Failure retains the draft and snapshot for retry. Successful Save releases the snapshot; subsequent Saves patch the saved mesh while retaining the layer's topology ownership.

Overwrite and Keep Both still choose logical destinations. Destination identities and hashes are checked against the state captured when choosing the destination, so later changes stop Save. The original source and private staging directory are protected from conflicting output paths. An intentional same-folder native mesh can replace itself on Save; an SMS source cannot.

An untitled project's first Save As selects a free mesh filename in the chosen project directory, preserves existing files there, and records final references in the model and settings. Failed Save As restores the draft's prior path and retains the snapshot.

Workers return values and shared snapshot ownership, not QObjects. GUI adoption checks the originating project, model, engine, path, edit revision, close state and import sequence. Edit signals invalidate stale work. Closing the owner or abandoning a result releases its snapshot when the last owner finishes. Layer setup completes before existing layers are replaced.

## Verification

The pre-fix importer reproduced four regression failures: the engine reference changed before Save, a same-folder SMS source was rewritten, invalid Overwrite removed the incumbent file, and discarding an import left a final copy behind. Logs and final results are recorded in [the Phase 24 testing guide](../../workplans/artifacts/phase_24_import_staging/README.md).

Final automated results: **368 checks across seven suites**, no failures or skips:

| Suite | Passed |
|---|---:|
| Import workflow and Save/reopen | 24 |
| Import snapshots and publication | 15 |
| Mesh writer | 102 |
| Save transaction and recovery | 51 |
| Prepared project validation | 26 |
| Production Save | 128 |
| Mesh worker and artifact ownership | 22 |

Import tests cover external and same-folder native/SMS input, source preservation, invalid overwrite, Keep Both, two successive Saves, a 117-to-3-vertex topology replacement and fresh engine reopen, backed and pathless untitled Save As with a filename collision and failed-save path restoration, queued stale result rejection, owner deletion, private-stage cleanup, reference refusal (including BOM input), consent-to-dispatch destination changes and opaque-section retention. Snapshot tests cover cancellation, source changes during and after copying, sealed payload edits, changed destinations and rebasing protection. The first integration run exposed two stale test expectations about the in-memory mesh reference: Save intentionally adopts an absolute engine path while the saved INP retains a relative filename. The assertions now check both forms explicitly.

Release GUI build/deployment exited 0. Strict deep signature verification passed. Installed and bundled engine UUIDs match: E31718A1-72CE-3B04-A7DA-AE5AA2C0A4A1 (arm64). GUI launched as PID 21710 at 2026-09-26 22:42:53 EDT. Older PIDs 7653 and 41701 remained running. Launch is process-verified; native manual acceptance remains pending.

Qt deployment still reports unavailable optional Mimer/ODBC/PostgreSQL driver libraries. These did not prevent build, signature verification or launch and remain a W9 packaging limitation.

Native interaction, accessibility acceptance and large-import responsiveness are separate manual gates. This phase does not refresh the live editing engine's topology, relocate a complete imported dependency graph, remap old cell-indexed data, scavenge crash-orphaned job directories or resolve optional database-driver packaging.
