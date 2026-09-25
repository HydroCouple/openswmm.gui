# GUI implementation baseline — W0, 2026-09-25

This is the maintained baseline for the approved GUI robustness, accessibility and corridor-mesh program. Phase 07 establishes source inventories, save ownership decisions, runtime evidence and mesh-selection coverage. It does **not** mark every surface audited, certify groundwater transport, or implement the new save transaction.

The umbrella plan and phase status are in `workplans/GUI_ROBUSTNESS_ACCESSIBILITY_AND_CORRIDOR_MESH_PLAN_2026-09-24.md` and `workplans/GUI_PHASED_IMPLEMENTATION_STATUS.md`. Generated inventories and this audit live outside the ignored workplans directory so they can be version-controlled with the implementation.

## Deliverables and ownership

| Deliverable | Location | Responsible package / current state |
|---|---|---|
| Dialog/page manifest, implementation coverage and launch/consumer candidates | [Index](INDEX.md), [dialog records](dialogs.json), [implementation disposition](dialog-files.json) | W0 source discovery; W3 owns per-surface audits and fixes. Unknown owners and entry points remain explicitly pending. |
| Platform/anonymous prompts | [Call-site inventory](standard-dialog-sites.json) | W3: inspect each reachable prompt, validation path and native behavior. Counts are references, not distinct dialogs. |
| Icon bindings and paint sites | [Catalog bindings](action-icons.json), [other icon references](icon-call-sites.json), [paint/style sites](theme-paint-sites.json) | W4/W4d: effective runtime icons, contact sheets, contrast and both-theme inspection pending. A raw QIcon reference is not proof of a theme bypass. |
| Editor registries | [Registry source sites](registry-sites.json); runtime `editor_registry.json` in Phase 07 evidence | All 13 compiled comprehensive-editor categories expose create and browse callbacks. Invocation, model lifetime and persistence are separate gates. Property/style/import factories remain source-mapped, not runtime-certified. |
| Workflow and visualization traceability | [Surface contracts](SURFACES.md) | W1/W3/W4 and W4a–d. Each row identifies a model owner, persistence and its next acceptance gate. |
| Save and non-Save writes | [Save contract](SAVE_CONTRACT.md) | W1/ENG-1/ENG-5; commit-on-Save selected from the approved plan. No extra write action is introduced. |
| Species, groundwater and forcing | [Capability map](CAPABILITIES.md) | W4a–c; distinguish source API, installed API, initialized state, actual advancement, file output and GUI consumption. |
| Mesh reporting and fixtures | [Report contract](MESH_REPORT.md) | W2/W5/W6a/W6/W7/W8; schema contract only, not implemented telemetry. |

## Source and executable baseline

GUI HEAD: `8bf44430c96b863c7fe3bc19633eb2e6925bfd03`. Engine checkout HEAD: `a0023c57474f0283ad8ca8e56799eb2a675e8929`. Both are working trees; HEAD is not a description of all local changes. Full status and installed-library SHA-256 are retained in `workplans/artifacts/phase_07_w0_baseline/provenance.json`.

The GUI links the installed Darwin engine, not the sibling checkout directly. Installed and bundled engine Mach-O UUID: `72B3E190-08B3-3733-A10B-9DAC2BC290C3`, advertised version `6.0.0-alpha.4`. Build: Release/arm64, Qt 6.9.3, Apple Clang 21 and macOS 26.5 SDK. That SDK selection avoids the previously observed dependency incompatibility with the newer SDK. Optional Mimer/ODBC/PostgreSQL deployment dependencies remain a W9 packaging gate.

The new `test_meshselectionjourney` uses the actual canvas, mixed-cell layer, selection bus and map tool, with no engine/results layer. It covers rectangular and polygonal picks, a quad as one selectable cell, reverse drag, add/toggle, missed clicks, Escape and active-mesh reference ownership. It is an event-handler integration test, not a native mouse/keyboard or rendering certification. Escape deliberately clears the selection, matching the current toolbar help. The initial test assumed preservation; that test expectation was corrected without changing the product.

Relevant existing baseline suites are `test_selectionmanager`, `test_mesh2dgroundwaterdialog`, `test_species_attributes` and `test_icon_factory`. Phase 07 evidence records final results separately from exploratory runs. Existing Phase 04 save-failure tests remain the starting gate for W1. No documentation-only unit tests are added.

## Regeneration and review discipline

Run `python3 scripts/audit_gui_baseline.py` to regenerate the discovery files. It scans declarations, transitive in-repository widget inheritance, source consumers, shared controls, test references, the action catalog and resource aliases. It preserves line references and excludes comments. It is a source scanner, not a C++ compiler: factory-generated anonymous pages, external plugin dialogs, conditional compilation and runtime reachability still require review. Base/interface surfaces are included deliberately and may be classified as non-instantiated in W3.

Keep human findings in these Markdown contracts; do not hand-edit generated JSON. Every future surface audit must record: constructor/launcher, data owner, persistence scope, live versus buffered editing, Apply/Cancel/reset, undo/dirty behavior, validation, worker lifetime, focus/keyboard/accessible values, theme/scaling, and evidence. A test reference, constructor call or inventory row is never sufficient for acceptance.

## Outstanding W0 acceptance

- Native mesh-only box/lasso journey, selection highlights and user-facing instructions; the automated gesture path is covered.
- Reproduce generation → Close → Don't Save on an owned model copy to document today's R3 behavior. The source write path is confirmed; W1 must make the desired contract pass before its acceptance.
- Actual groundwater hydraulic advancement/ledger evidence and transport consumption against the intended engine build. An active flag alone is insufficient; see the probe finding.
- Complete runtime reachability/ownership for dynamically assembled and plugin surfaces as their W3 families are implemented. This discovery inventory is a baseline, not a claim of complete UI consistency.
- Real road/river datasets and numerical corridor benchmarks in W5–W8. Synthetic fixtures can establish regressions, not real-model acceptance.

These are explicit gates. Save implementation can use the ownership contract now; species/forcing acceptance cannot use source capabilities as a substitute for executable evidence. W0 as a whole remains open until its outstanding gates have evidence.
