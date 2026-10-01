# Phase 32 — Nested Water Age editor save-state tracking

Status: implementation and focused regression verification complete; GUI build, signature verification and fresh launch complete. Native acceptance pending.

## Behavior

Simulation Options → Quality & Transport → Reserved Species opens Water Age Sources as an independent editor. Its OK commits source ages to the engine immediately. Previously, the nested launcher ignored the write result, leaving the project marked as saved even when accepted changes survived cancelling Simulation Options.

The launcher now captures its owning project with a guarded pointer before the child modal loop and marks that surviving owner as unsaved when the child reports writes. Unchanged acceptance, child cancellation and invalid drafts refused before writing do not mark the project. The launcher's tooltip explains that accepted source-age edits survive outer cancellation. No Save is performed by these actions.

This closes the nested-launch follow-up from Phase 31. It does not change the separate main-window launcher, Initial Quality editor, engine setters, or meshing implementation.

## Verification

The existing Simulation Options round-trip suite passed all 6 checks before additions. Four new cases use the actual Quality-page button and Water Age modal editor, with two real project owners: accepted edit, unchanged acceptance, child cancellation, and duplicate refusal followed by cancellation. They check engine values, owner dirty state before and after outer rejection, isolation from another project, and an unchanged project file.

Before the fix, the expanded suite had **9 passes / 1 failure**: the accepted edit left its owner clean. After the fix, it passes **10 checks**; the Water Age dialog suite passes **15 checks**. Total: **25 passed, 0 failed, 0 skipped**, including setup/cleanup. Independent read-only review found no blocker. Scoped whitespace checks pass.

The integration executable used offscreen Qt and the registered test environment with copied fixtures and output directories under the phase evidence folder, preventing alteration of shared fixture outputs. Tests exercise dialog and model behavior; offscreen QML/rendering warnings do not constitute native rendering verification.

See the [testing guide and evidence](../../workplans/artifacts/phase_32_nested_editor_dirty/README.md).

## Application checkpoint

Application build/deployment exited 0; deep/strict signature verification passed. Installed/bundled arm64 engine UUIDs match `E31718A1-72CE-3B04-A7DA-AE5AA2C0A4A1`. Fresh GUI PID **77319** launched **2026-09-30 14:30:25 EDT**, preserving the earlier instance. Startup is process-verified; native acceptance remains pending. Existing optional Mimer/ODBC/PostgreSQL dependency packaging remains a W9 gate. Evidence: `final/application_build.log`, `final/signature.log`, `final/engine_uuid.log` and `final/launch.log` in the phase artifact directory.

## Remaining work

Native keyboard, theme and accessibility acceptance remains pending. Water Age engine setter failure/partial-write reporting, numeric precision and general engine/editor lifetime handling remain separate. The guarded pointer protects the post-modal dirty notification; it does not establish a general engine-lifetime contract. The ownerless path is guarded but not separately tested. The shared build includes the separate active meshing overhaul and does not certify its acceptance gates.
