# Phase 22: hold generated mesh changes until project Save

## Scope and behavior

The mesh-generation worker now returns its completed geometry and intended destination without writing the project's INP or final 2DM. Inline generation and regeneration of an existing external mesh leave saved bytes unchanged; a new external destination remains absent. The project Save path owns publication. Closing a project without saving can therefore discard its generated working mesh without having already replaced those saved files.

The adopted layer retains a persistent generated-topology ownership flag. Save must serialize that layer's complete replacement geometry rather than patching the previous source topology. Inline layers use the captured project INP as their source; the Save integration rebases that source on a successful Save As. External layers retain the intended mesh destination. Adoption no longer changes the live engine's MESH_FILE option.

Cell couplings formerly held only in the transient writer map are copied into the MeshResult with duplicate cell/node pairs removed. The pending payload retains selected roughness and initial depth, vertex coupling, region infiltration rows and generated cells. These fields remain available to the full Save writer without rereading a file that generation no longer publishes.

## Ownership and cancellation

Generation captures the project, model, engine identity, model path and edit revision. A latched invalidation guard observes model edits, options/data/geometry changes, layer additions/removals and mesh/coordinate-system changes. A second edit while the project is already dirty still invalidates its result. Project close, dialog rejection and dialog close cancel the future. QPointer ownership and the project's closing state prevent a late result from changing a closing or destroyed project.

Completion checks ownership before layer replacement or burn-related engine edits. It constructs the replacement layer first; allocation/setup failure preserves the previous mesh. Inline regeneration replaces the previous inline working layer, and external regeneration replaces a layer with the same destination. User-facing generation warnings follow adoption so their nested event loops cannot separate the ownership check from project changes.

This is a conservative guard: some unrelated layer changes can require regeneration. It does not fingerprint externally edited raster or feature-file contents while a worker is running.

## Automated checks

`test_meshterrainpipeline` calls the actual production worker. Its four generation-staging cases cover inline output, an existing external destination, a new explicit destination, and a new default destination. Each preserves an INP sentinel and either preserves a 2DM sentinel or verifies absence of the destination; each also checks generated geometry, coupling uniqueness, roughness and initial depth. Before the production change, the four file-preservation cases failed in the parent agent's baseline run.

The same executable checks guard invalidation for an already-dirty model edit, a real option edit, owner destruction, the owner-closing notification, dialog rejection and dialog close. Cancellation cases inspect the actual future cancellation flag. Existing terrain refinement and cached-versus-fresh mesh parity cases remain in the executable.

Artifacts remain under `SWMMVIS_TERRAIN_PIPELINE_OUTPUT`, or `terrain_pipeline_output` in the test working directory by default. Staging fixtures use its `generation_staging` subdirectory. The parent integration runs the build, regression executables and GUI launch; no separate build was run by this implementation agent. Final pass/fail evidence belongs in the integrated phase report.

## Explicit limits and manual acceptance

Burned raster/report output and mesh-stage caches are outside this bounded INP/2DM guarantee and can still be written during generation. Burn-related in-memory 1D edits run only after successful mesh adoption. Atomic publication of all generated auxiliary products is separate work.

For manual acceptance, generate both inline and external meshes, inspect the pending layer, and close with Don't Save: existing INP/2DM bytes must remain unchanged and a new destination must remain absent. Repeat generation followed by Save and Save As, reopen the saved model, and compare vertices, cells/quads, couplings and hydraulic attributes. Cancel a running generation and close its project before completion; neither action should add a late mesh or change saved INP/2DM files. Save staging, rollback and complete replacement-topology round trips are covered by the parent-owned Save integration.

## Integrated checkpoint result

The final production-worker suite passes **14 checks**, including the four no-publication cases, six owner/cancel cases and two terrain parity cases plus fixtures. All seven phase suites pass **343 checks**, no failures/skips. The Release GUI built, passed strict deep signature verification and launched as PID **41701** at **2026-09-26 21:50:02 EDT** with the matching tested engine. See [the testing guide](../../workplans/artifacts/phase_22_parallel/README.md) for logs and native steps.
