# Phase 22 — saving generated replacement topology

Generated mesh previews now have explicit topology ownership. This is independent of the dirty flag: after Save succeeds, the layer is clean but remains the source of geometry until the model is reloaded. The engine still holds its previously parsed topology, so generated layers never push attributes through its old vertex/cell indices, even when counts happen to match.

## Save behavior

Built-in INP Save fully serializes a generated layer into the existing transaction's staging files. This covers geometry, triangles/quads, roughness, depth, tags, vertex/cell coupling, infiltration, boundaries, conveyance and unit metadata. New external destinations need not exist. Existing external content is read and checked before replacement; ordinary file-backed edits retain their existing patch behavior. A prepared-reference writer checks the physical stage while emitting the final logical mesh name. Inline Save As serializes the new source identity into the project sidecar and adopts it only after successful publication.

Prepared-project validation, rollback and recovery remain the same production path used for ordinary Save. Failure preserves the generated payload and dirty state for correction/retry. First and repeated Saves reopen through the real engine. Plugin export is refused for pending generated topology because that writer cannot promise to use the layer's replacement data.

## Remapping boundary

A replacement can invalidate more than geometry. The new guard refuses existing explicit cell/edge/tag assignments in supported groundwater, transport, initial velocity, coverage/loading and quality sections, plus groundwater initial-quality FILE references that may contain such assignments. The error identifies the section and asks for removal or remapping. Equal counts do not make old indices valid. Global, XY and AUTO configurations remain available where their syntax is independent of cell identity.

This is a bounded guard for known authoring sections, not a complete semantic remapper or solver validation. Source/plugin data outside these known sections, live-engine refresh and import ownership remain follow-up work. The ordinary Run command saves pending changes and opens the saved INP through its run engine; native workflow acceptance remains pending.

## Tests and evidence

The baseline production Save test reproduced four failures for new or changed-count generated meshes. The final Save suite passes **120 QtTest checks**, including six inline/external new/equal/changed-count cases with preparation failure/retry, repeated Save, full data readback and engine reopen. Two additional cases reject old cell-quality assignments while preserving both saved resources and the working mesh. The layer's inline source identity is checked after Save As, and old live-engine elevations remain unchanged.

The writer suite passes **102 checks**, including full mixed-topology replacement, preserved hydraulic/coupling/infiltration/BC payloads, 24 unsupported remapping cases, safe global/XY/AUTO configuration, new logical references and invalid physical stages. The pre-fix writer baseline had 72 passes and 30 failures. Existing recovery and staged-validation suites pass 30 and 26 checks respectively.

All results are in `workplans/artifacts/phase_22_parallel/final_tests.log`. Seven suites total **343 passes**, no failures/skips, including Initial Quality/property notifications and the actual mesh worker. Earlier direct-run logs omit the fixture environment and are retained only as diagnostic evidence; the final CTest run supplies the required fixture location.

The phase testing guide records GUI build, signature, runtime and launch evidence. Burn outputs, import staging, full remapping, outside-file fingerprints during generation, native accessibility and packaging acceptance remain separate work.
