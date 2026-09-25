# Save ownership and commit contract

Decision for W1/R3: adopt the approved plan's **stage generation, commit on Save** direction. This is an implementation decision within the user's phased authorization. Mesh generation must update the owning project's working state and preview without changing the last saved model. Close → Don't Save must preserve the last saved model and referenced mesh. An additional “Write mesh” command is not needed for the first implementation.

This document specifies the target; current generation and multi-file Save are not yet safe under this contract. Phase 08 now rejects missing/unreadable/empty external snapshots, propagates mesh restore/attribute/BC/reference failures and preserves dirty state and Save As identity. Individual mesh writes are checked; the engine may still have changed files before a later failure. Phase 09 stops before serialization when a mesh synchronization API rejects an edit, preserving dirty state and Save As identity; earlier in-memory setters may already have run. Phase 10 resolves one active mesh, rejects inactive pending edits/ambiguous ownership before mutation, and temporarily requests inline engine output for a selected mesh during built-in INP Save. This prevents engine writes to stale/rebased external paths; the GUI still uses checked snapshot/patch steps. Transactional rollback and generation staging remain unimplemented.

## Current write inventory

| Operation / source | Files or state affected today | Required ownership / failure contract |
|---|---|---|
| Generate, `src/ui/dialogs/meshgenerationdialog.cpp`, worker `InpMeshWriter::write` | `.2dm`, saved `.inp` reference or inline sections, before result attachment | W1: job-owned staging only; snapshot project/mesh revision and intended final paths. Validate before attaching. Cancellation/stale results remove only owned staging. |
| Built-in Save/Save As, `src/swmmvisprojectwindow.cpp::saveAs` | Engine state sync, `.inp`, external mesh, attribute/BC patches, `.oswp` | One checked project operation. Any failure leaves project/mesh dirty and preserves old identity. Clear dirty only after final publication. Phase 04 covers `.oswp` failure; Phase 08 adds mesh-stage failure propagation. Phase 09 checks mesh synchronization API rejections; Phase 10 enforces active ownership and suppresses external engine writes for the selected-mesh INP path. These corrections do not provide multi-file rollback. |
| Engine writer, sibling `src/engine/core/InpWriter.cpp` | Opens final INP and referenced mesh for write; unchecked output/close paths | ENG-1: checked serialization, error propagation, explicitly redirected outputs. A temporary INP alone does not isolate the mesh. |
| External-mesh snapshot/restore in `saveAs` | Phase 10 suppresses the engine sidecar write for selected-mesh INP Save; GUI still copies its snapshot and patches it | ENG-5: remove competing writers. GUI serializes its authoritative mesh once; engine serializes model sections and the final mesh reference without rewriting that sidecar. |
| Mesh import, `src/swmmvisprojectwindow.cpp::importMeshFileAsync` / import helpers | Copy/replace destination and reference update | Bring model-attached import under staging/Save. Keep original source immutable; preserve attributes, BCs, coupling, infiltration and active-mesh identity. |
| Standalone plugin writes via Save As path | Plugin-selected output (e.g. GPKG); current contract skips `.oswp` | Keep existing standalone-export semantics explicitly labelled; do not silently enlist unrelated files or mark another project saved. Plugin capability/atomicity must be checked before claiming transactional support. |
| Explicit results/map/style exports, `src/io/mesh2dresultsexport.cpp`, dialog and plot export callers | User-chosen export destinations | Independently checked export; do not clear project dirty state. Stage multi-file datasets where needed; preserve prior output on failure. Audit driver-owned sidecars as part of the output set. |
| Channel burn-in worker and caches | DEM copy, reports and cached intermediates | Original DEM remains input. Model-attached generated DEM/report outputs need job ownership and Save publication; explicit exports need named destinations and overwrite confirmation. Cache deletion may affect only owned cache entries. |
| Run preparation, `src/swmmvis.cpp`, simulation runner | Save-before-run, output defaults, reports/results and possibly hotstart output | Deliberate run writes must be identified. Save-before-run uses the same transaction. Result files are run-owned outputs; cancel/failure must not publish partial results as complete. W1 audits overwrite and stale attachment. |

The multiple-mesh gate remains partly open. Phase 10 synchronizes and cleans only the chosen active mesh; a sole unflagged legacy layer is accepted, while missing/multiple active flags among several layers fail before writes. Clean inactive layers are excluded from synchronization and patches. Inactive pending edits block Save rather than being discarded or marked clean. Selecting a mesh dirties the project; multi-layer saves force the selected layer's attribute push even if its mesh dirty flag is clear. Loaded/generated inline meshes now receive an active flag too.

For built-in INP Save with an identified mesh, Phase 10 temporarily clears the engine's MESH_FILE token while serializing inline, restores the previous token even on writer error, then adopts the chosen absolute external reference (or an empty inline reference) only after the file chain succeeds. Real-engine tests demonstrate no write to the old external file or a same-named mesh in a different Save As directory. This is a bounded use of the existing API, not ENG-5's complete reference-only/staged serializer: it still emits/discards inline mesh text and retains redundant snapshot/patch passes. Plugin exports, saves without an attached mesh, path aliases, full output manifests and crash recovery are not certified.

The `.oswp` stores display configuration, not mesh edit payloads. Simultaneously edited inactive meshes still need a coordinated persistence design; the new refusal is a safety guard, not that feature. Count equality is not topology identity. Engine APIs can accept invalid values (for example non-finite elevations), and count-mismatch fallbacks can skip fields; rejection checks do not certify complete validation or persistence in those paths.

The raw prompt inventory includes file-picker/save prompts outside these critical journeys. W1 must reconcile every output-producing call before declaring application-wide save safety. Hotstart/plugin/driver multi-file behavior remains an explicit audit item, not assumed atomic.

## Writer ownership and staging

1. Snapshot project revision, active mesh identity/revision, engine model state, style state and all final output paths. An operation belongs to that project, not whichever window becomes active later.
2. Build an explicit output manifest: model, GUI-owned mesh, project settings, generated model-attached resources, and any engine-owned companion outputs. Existing input datasets/results are references, not implicitly copied outputs.
3. Stage on the destination filesystem. Every writer must support destination redirection or a mode which omits externally owned resources. Do not rely on changing only the main INP filename. ENG-5's preferred addition is reference-only mesh serialization/output redirection; bulk engine topology replacement is deferred unless run consistency proves it necessary.
4. Write the mesh's complete topology and GUI-owned attributes/BCs/couplings/infiltration, then serialize the model referencing the **final** path. Preserve all sections the GUI does not own. Validate the staged model/mesh combination through reopen, including unit conversion and Save As rebasing. Never publish staging paths in the final project.
5. Check byte counts, flush/close and validation results. Surface the resource and recovery action on every error; warnings are not successful persistence. Engine-side failure injection is needed before relying on an engine success return.
6. Use a recovery journal with old/new paths, file identities and progress. Keep rollback copies until the output set is committed. A sequence of atomic renames is not a crash-atomic multi-file transaction. On restart, detect the journal and restore the last complete set or finish a verified commit; never silently combine versions.
7. Only after publication succeeds, adopt Save As identity, clear dirty flags and emit saved notifications. Failed Save keeps working edits and last successful identity, permitting retry. Clean up only outputs owned by the completed/abandoned operation.

## Acceptance matrix for W1

| Test | Required outcome |
|---|---|
| Generate then Don't Save; generated mesh preview remains visible until close | Reopen has the old disk mesh and model; no unrequested file replacement. |
| Cancel generation / close target / change geometry while worker runs | No stale attachment or final-file write; no effect on another project. |
| INP short write, full disk, external-mesh failure, patch failure, `.oswp` failure | Error names failed stage, old project usable, all dirty state retained, retry possible. |
| Interrupt after each publication step | Recovery reconstructs a coherent old or verified new file set. |
| Save As to another directory, external and inline mixed meshes | References resolve after reopen, no stale topology, complete attributes/BC/coupling/infiltration, correct units. |
| Multiple mesh layers and active mesh change | Explicit active mesh controls the model reference; unrelated layers/files preserved. |
| Run after unsaved generation | Explicitly save via the transaction or run an isolated staged model. Never execute stale engine topology or silently overwrite saved files. |
| Explicit exports / hotstarts / plugin output | Correct overwrite scope, checked errors, no accidental project-clean transition. |

Unit tests cover manifest/path/recovery decisions. Integration tests use actual engine serialization and owned files, with byte-level before/after checks and failure injection. Native close/save prompts and crash recovery are separate acceptance evidence.
