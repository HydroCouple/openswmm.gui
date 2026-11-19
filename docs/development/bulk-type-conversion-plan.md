# Bulk node and link type conversion

Proposed implementation for review, November 10, 2026.

## Findings

| Path | Current behavior | Effect |
| --- | --- | --- |
| `AttributeTablePanel::onChangeTypeTriggered` | Reads `selectedRows().first()`. Its node picker detects the virtual flag and omits the inlet target. | Only one selected row reaches conversion. An inlet junction can appear as a virtual junction. |
| `OpenSWMMVisMapToolSelect::showContextMenu` | Builds targets and dispatches the conversion using the object under the cursor. | Other selected objects are never considered. The clicked object's eligibility controls the menu. |
| `TypeConversionFlow::run` and `runToInletJunction` | Confirm and report one object at a time. Some promotions change the base type or flags before all checks succeed. Inlet usage validation requires an already-promoted host. | A later failure can leave a different node type, cleared subtype data, or a virtual flag. Repeating this flow in a loop would prompt for each object. |
| `SWMMModelLayer::applyNodeConvert` and `applyLinkConvert` | Each successful base conversion rebuilds the category index and sends repaint, geometry and named attribute signals. | A loop repeats model-wide work and can invalidate table rows during iteration. The existing bulk-edit guard alone suppresses signals but does not defer those index rebuilds or mark these conversions dirty for its closing refresh. |

An in-memory probe against the engine installed for this GUI reproduced a dependent-object failure. With two conduits meeting at a valid virtual junction, converting one conduit to a pump returned success. The junction retained its virtual flag and its eligibility changed from 0 to 609 (requires exactly two attached conduits). The existing GUI conversion test checks formatting and individual engine calls with a stub layer. It does not exercise either multi-selection entry point.

## Selection and reporting

1. **Capture the selection before editing.** On the map, right-clicking a selected node or link targets the project selection, with the clicked object's family determining the target menu. Right-clicking an unselected object targets that object alone. In the table, use selected objects belonging to the active category when the clicked row is selected. Include query-hidden selected rows counted by the table footer and show that count in the confirmation. Otherwise target just the clicked row. Resolve view-local selections through the sort/filter proxy. Store the model and deduplicated typed object IDs so row movement and a node/link name collision cannot redirect a write.
2. **Use one target picker and one eligibility policy for both surfaces.** Offer the four base node types plus Virtual Junction and, when supported, Inlet Junction. Offer the five base link types. For a mixed selection, enable a target when at least one matching object can change. Keep reasons accessible when none qualify. Defer design-specific inlet checks until the user supplies the design, capture node and placement. A selection that contains the eventual capture node must retain it as a valid receiver.
3. **Confirm once.** Show the destination, eligible count, already-target count, and skipped count with expandable IDs and reasons. Show applicable attribute-loss and non-undoable warnings for eligible objects. If none qualify, present the reasons directly. Cancellation before applying makes zero writes.
4. **Report actual outcomes once.** Every selected ID has exactly one outcome: converted, already the target, or skipped with a reason. An unexpected engine refusal is a skip with its specific error. Attach cleared fields and post-conversion advisories to successful IDs. Group repeated messages by reason with inspectable, copyable IDs. For example, an unassigned pump curve is a successful conversion requiring follow-up. A conduit required by a virtual junction is skipped with the junction ID and rule.

## Implementation order

1. **Separate preview, apply and presentation.** Extract a dialog-free planner and executor shared by the map and table. Read current kinds from the engine, testing the inlet flag before the virtual flag. Classify other families, stale IDs, same-type requests, unsupported targets and edit-locked models before mutation. Use a stable processing order and re-resolve IDs after any dialog, including the modeless inlet setup. Recheck editability and affected dependencies immediately before each write. If the model has closed or been replaced, discard the pending operation.
2. **Enforce eligibility at the engine boundary.** Reuse the existing base converters for subtype defaults and cleared-field reporting. Use the virtual and design-specific inlet rule checks for promotions, including drop-inlet cross-sections. Check existing dependencies that the requested change would invalidate, including virtual/inlet endpoints, conduit-hosted inlet usages and nodes receiving inlet capture. Check affected relationships only. Preserve existing advisory behavior for accepted base conversions such as a new pump requiring a curve. Editing an otherwise unrelated object should still be possible in a model with a pre-existing validation issue.
3. **Make each multi-step conversion atomic.** Give the engine a compound operation for promotion to virtual/inlet junction that can validate the proposed final state before changing the base subtype, flags, derived geometry or usage row. Share the same rules between preview and apply. Reuse the engine's demotion cascade where it already owns these changes. If a later operation can still fail, stage the affected records or restore them in full before returning failure. Simply converting back to the old type recreates defaults and loses original values. Update GUI caches only after a successful commit. A refusal for one ID must allow the other eligible IDs to proceed.
4. **Batch the expensive work.** Update each successful object's cached type and flags immediately. At the end, rebuild category membership at most once and publish one coherent refresh for the map, attribute table, Object Browser and Properties panel. Send the changed typed IDs so a bound editor can rebind and stale vertex handles can close. Mark the project edited once for any success, including flag-only changes. A type-only edit preserves endpoints and coordinates, so it can bypass endpoint resynchronization and spatial-index rebuilding. Keep structural add/delete handling intact when bulk operations nest. Build reusable adjacency, inlet-use and coupling lookups once where profiling shows repeated scans, and use the same engine rule implementation for individual and batch checks.

## Verification and acceptance

| Test | Acceptance |
| --- | --- |
| Real map and table actions | Multiple nodes convert to storage and multiple links convert to pumps. Sorted, filtered and sparse row selections target the intended IDs. Clicking outside the selection converts only the clicked object. Node and link IDs with the same spelling stay distinct. |
| Mixed and empty eligible sets | The first selected object may be ineligible. Eligible peers still convert, already-target objects count as no-ops, and wrong-family or missing objects have named reasons. Cancelled and all-skipped batches leave the project clean. |
| Dependent link regression | Select a conduit incident to a virtual junction and an independent conduit. Request Pump. Only the independent conduit changes. The report names the blocked conduit and junction, and the junction still passes its two-conduit rule. Repeat with an inlet-host dependency. |
| Virtual and inlet transitions | Exercise base-to-virtual, base-to-inlet, inlet-to-virtual and inlet-to-base transitions. Check street/drop designs, capture conflicts, rejected usage and a stale setup dialog. Force failure after a possible intermediate write and compare original subtype fields, flags, derived depths, usage and affected references. |
| Persistence and UI | Save and reopen a mixed-success batch. Confirm subtype membership, IDs, coordinates, link endpoints and vertices, inlet usages and skipped objects. Verify map symbols, selection, table categories, property fields and project dirty state against the engine. |
| Large selection | Measure preview and apply separately for 100, 1,000 and 10,000 objects. Record classification, engine mutation, cache rebuild and view-refresh time. Instrument the real layer and assert at most one category rebuild and one refresh cycle for a changed batch, zero for a no-op, and no endpoint or spatial-index rebuild for type-only edits. Compare with repeated individual conversions without dialogs. |

Deliver the engine dependency checks and atomicity regressions first, then the layer batch path and both GUI entry points. Update the map and attribute-table manual sections with the selection scope, report meanings and one mixed-success example.

## Implementation and verification

Both GUI entry points now call the same preview and apply operation. The engine checks the routing model, coupled nodes, adjacent conduit sections, capture receivers, inlet usage and the requested inlet design before writing a role change. The GUI reports each selected ID and batches category updates, property rebinding and repaint notifications.

The companion engine change is in `patches/openswmm-engine-bulk-conversion.patch`. It applies to the sibling `openswmm.engine` checkout at `d8c0a1ee`. To integrate it from the GUI repository root, run the following commands, then rebuild and install the engine with the project's normal preset before configuring the GUI against that installation.

```sh
git -C ../openswmm.engine apply --check "$PWD/patches/openswmm-engine-bulk-conversion.patch"
git -C ../openswmm.engine apply "$PWD/patches/openswmm-engine-bulk-conversion.patch"
```

The focused CTest targets are `test_typeconversionflow` and `test_bulktypeconversion`. The latter exercises the actual map and sorted, filtered table menus with a real engine, cancellation, dependent and missing objects, same-name node and link IDs, a click outside the selection, INP save/reopen and the single-refresh contract at 100, 1,000 and 10,000 nodes.

In one local release-build comparison on a synthetic model, converting 10,000 junctions to storage took about 23 seconds with repeated calls to the original single-object layer method. The batch took under 0.1 seconds. Both measurements exclude dialogs, and the batch includes the category rebuild. Interactive times also depend on the model and open views.
