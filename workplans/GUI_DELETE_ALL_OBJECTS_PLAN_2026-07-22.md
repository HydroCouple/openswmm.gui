# GUI Delete for All Object Types — Object Browser · Map · Attribute Table

Date: 2026-07-22
Status: **DRAFT — for review. Do not implement until approved.**
Repo: `openswmm.gui` (with a flagged dependency on `openswmm.engine`)
Builds on: `workplans/OBJECT_DELETION_AND_UNDO_PLAN_2026-07-16.md` (the vetted master plan). This document is a focused, current-state execution slice of that plan's Phases 1–2, not a new strategy.

---

## 1. Goal

Let the user delete **any** SWMM object from three entry points, with identical behavior and undo everywhere:

1. **Object browser** (`objectbrowserpanel.cpp`) — right-click "Delete…" and the Delete key.
2. **Graphically on the map** (`maptoolselect.cpp`) — already exists for spatial types; extend/verify.
3. **Attribute table** (`attributetablepanel.cpp`) — already exists for spatial types; extend to all types.

"All object types" = the spatial network (junction/outfall/storage/divider nodes, conduit/pump/orifice/weir/outlet links, subcatchments, rain gages) **plus** the non-spatial data objects (curves, time series, time patterns, transects, pollutants, aquifers, snowpacks, LID controls, streets, inlet designs, land uses, hydrograph groups, control rules).

---

## 2. Current state (audited from source at git HEAD "Work in progress", 2026-07-22)

### Already working — reuse, do not rebuild
- **`DeleteObjectCommand`** (`include/map/mapundostack.h:549`, `src/map/mapundostack.cpp:700`) — an undo command on the canvas `MapUndoStack` handling the four spatial `TargetKind`s: `DeleteNode`, `DeleteLink`, `DeleteSubcatch`, `DeleteGage`. Node delete cascades its links. `redo()` → `SWMMModelLayer::apply*Delete` → engine `swmm_*_delete`; `undo()` re-adds.
- **Map delete** — `maptoolselect.cpp` `keyPressEvent` (:609) handles both `Key_Delete` **and** `Key_Backspace`; right-click delete builds `DeleteObjectCommand`s in a macro (:721, :1742).
- **Attribute-table delete** — `attributetablepanel.cpp` already binds Delete + Backspace (:414–422) and adds a context-menu "Delete" (:1281–1293), routing selected rows to `deleteSelectedRows()` → `DeleteObjectCommand`. **But it early-returns for non-spatial categories** (`deleteObjects`, :1624: "data-object categories have no spatial delete path").
- **Engine delete + impact APIs** exist for exactly six types (`openswmm_edit.h`): `swmm_node_delete`, `swmm_link_delete`, `swmm_subcatch_delete`, `swmm_gage_delete`, `swmm_table_delete` (**covers both curves and time series**), `swmm_transect_delete` — each with a matching `swmm_*_analyze_impact` and a `SWMM_ImpactReport`. `SWMM_RefType` currently enumerates NODE/LINK/SUBCATCH/GAGE/TABLE/TRANSECT/INLET_USAGE.

### Gaps this plan closes
| # | Gap | Where |
|---|-----|-------|
| A | Object browser has **no Delete action at all**, for any type | `objectbrowserpanel.cpp` context menu (~:268–306) |
| B | Attribute table & map delete only cover the 4 spatial types; **curves, time series, transects are engine-deletable today but not wired** | GUI |
| C | No generic delete path for non-spatial data objects (needs a `DeleteDataObjectCommand`) | GUI |
| D | ~10 data types (patterns, pollutants, aquifers, snowpacks, LID controls, streets, inlets, land uses, hydrograph groups, control rules) have **no engine delete API** | `openswmm.engine` — blocks true "all types" |
| E | Delete uses a count-only confirm; no impact preview of cascades/cleared refs | GUI |
| F | Layer-tree per-object rows have no Delete | `layertreepanel.cpp` |
| G | **Windows parity** of delete behavior is unverified (user-reported gaps) | cross-platform |

---

## 3. Key decision: engine coverage gates "all types"

Only 6 types can be deleted through the engine today. The remaining ~10 data types **cannot be deleted from any surface** until `openswmm.engine` gains their `swmm_*_delete` + `analyze_impact` APIs (that's Phase 0.2 of the master plan, in a separate repo not connected to this session).

**Assumed approach (change if you disagree):** build the full GUI framework now — generic command, one shared delete flow, all three entry points — and light it up for every type that has engine support (spatial 4 + curves + time series + transects). Each remaining data type becomes a ~10-line dispatch-table entry that drops in the moment its engine API ships. This delivers visible progress across all three surfaces immediately without waiting on the engine repo, and avoids per-type GUI rework later.

The alternative — do the engine Phase 0.2 work in tandem — requires connecting the `openswmm.engine` repo to this session; say the word and I'll fold it in as Phase 0.

---

## 4. Architecture (unchanged from the vetted plan)

- **Model** = the engine (single source of truth); `SWMMModelLayer` stays a thin cache/adapter.
- **Controller** = `QUndoCommand`s on the existing canvas `MapUndoStack`. No second stack.
- **Identity** = commands store object **names** (`SWMMObjectRef`), never engine indices (SoA compaction shifts indices on every delete); resolve name→index at execution time.
- **One flow, three menus.** All three entry points collect `SWMMObjectRef`s → run the same confirm/preview → push the same command macro. Zero surface-specific delete logic.

---

## 5. Work plan

### Phase 1 — Generic delete command + one shared flow (GUI)
1. **`DeleteDataObjectCommand : MapCommand`** keyed on `SWMMObjectRef`, with a per-`ObjectType` dispatch table of `{ snapshot-fn, engine delete-fn, re-add-fn, change-signal }`. Spatial refs continue to route to the existing `DeleteObjectCommand`, so map/browser/table behavior is identical regardless of where delete starts.
2. **`SWMMModelLayer` delete adapters** for the data types with engine support: `applyCurveDelete`, `applyTimeSeriesDelete` (both via `swmm_table_delete`), `applyTransectDelete` — mirroring the existing `applyNodeDelete` family, emitting the matching per-domain change signal (`curveChanged`, `timeSeriesChanged`, `transectChanged`) so open editors/tables refresh.
3. **Shared helper** `requestDeleteObjects(QList<SWMMObjectRef>, MapCanvas*)` that: confirms → builds the macro → pushes to `canvas->undoStack()`. Both the browser and the table call this; the map tool is refactored to call it too, so there is a single code path.

*Exit:* deleting a curve/time series/transect from the attribute table works and undoes; spatial delete behavior is byte-for-byte unchanged.

### Phase 2 — Object browser delete (Gap A)
1. Add **"Delete…"** to the `objectbrowserpanel.cpp` context menu (after "Edit…"), multi-select aware, and accept the Delete/Backspace keys on the tree.
2. Collect selected refs → `requestDeleteObjects(...)`. Disable/omit the action for types without an engine delete path (greyed with a tooltip: "Delete not yet supported for this type"), so the menu is honest about Gap D.
3. Give the **layer-tree** per-object rows (`layertreepanel.cpp`) the same action delegating to the same helper (Gap F).

*Exit:* every browser category with engine support offers right-click + keyboard Delete; unsupported types show a clearly-disabled action; map, browser, and table produce identical results and identical undo for the same object.

### Phase 3 — Attribute-table coverage (Gap B)
1. Remove the non-spatial early-return in `attributetablepanel.cpp:deleteObjects` and route data-object categories through `requestDeleteObjects(...)`.
2. Keep the Delete key guarded to **row-selection state only** (never steal Delete from an open cell editor).
3. Verify row removal under "Show selected only" and mid-sort leaves no phantom rows; undo re-inserts and re-selects.

*Exit:* the attribute table deletes every engine-supported type via context menu and Delete key.

### Phase 4 — Impact preview (Gap E) — optional, follows the vetted plan
Introduce `DeleteImpactDialog` (`src/ui/dialogs/`) that runs `analyze_impact` per ref and groups consequences: **"Will also be deleted"** (cascades, e.g. links on a node) / **"References will be cleared"** (nullified fields) / **"Control rules affected"**. Batch deletes aggregate into one dialog. All three entry points switch from the count-only confirm to this dialog. *If you'd rather ship Phases 1–3 first and add the dialog later, this phase is cleanly separable.*

### Phase 5 — Windows parity + verification (Gap G)
1. **Windows behavioral pass** for every entry point: Delete and Backspace keys, context menus, focus rules (map canvas vs. table vs. tree), and multi-select. Reconcile against your reported gaps (please see the note below).
2. **Round-trip tests** per type: build model → delete → undo → assert INP (`building_write_model`) matches pre-delete; and delete→redo stable. Written under `workplans/test_output/deletion_undo/` per CLAUDE.md §4.1.
3. Confirm deletes stay guarded while a simulation session is open (engine `CHECK_EDITABLE`); disable Delete actions during a run.
4. Update `CHANGELOG.md` (CLAUDE.md §5.2).

---

## 6. Sequencing

| Phase | Delivers | Depends on | Risk |
|---|---|---|---|
| 1 Generic command + flow | curves/TS/transects deletable; unified path | — | Low–Med (snapshot completeness) |
| 2 Object browser | Gap A + F closed | 1 | Low |
| 3 Attribute-table coverage | Gap B closed | 1 | Low |
| 4 Impact dialog | Gap E | 1 | Med (optional/separable) |
| 5 Windows + tests | Gap G, hardening | 1–3 | Med (parity unknowns) |
| 0 Engine APIs (if opted in) | Gap D — the other ~10 types | separate repo | Med |

Phases 2 and 3 can proceed in parallel once Phase 1 lands.

---

## 7. Open questions for you

1. **Windows gaps** — you're hitting specific gaps on Windows. Which are they: existing spatial delete misbehaving, Delete/Backspace key or context menu not firing, types that simply have no delete path, or an engine/link mismatch on the Windows build? Concrete symptoms will let me target Phase 5 (and possibly reprioritize).
2. **Engine scope** — GUI-now-plus-framework for the unsupported types (my default), or should I also take on the `openswmm.engine` Phase 0.2 delete APIs this round (needs that repo connected)?
3. **Impact dialog** — include the preview dialog in this slice (Phase 4), or ship delete + undo first and add it after?

## 8. Assumptions (flag if wrong)
- One shared undo stack (the canvas `MapUndoStack`) is the right home for browser/table/map deletes.
- `swmm_table_delete` is the correct API for both curves and time series (they share the engine "table" store).
- `building_write_model` INP output (canonical ordering) is an acceptable round-trip oracle.
- Deletes remain blocked during an active simulation session.

## 9. Out of scope
- Engine referential-integrity fixes beyond enabling delete (master-plan Phase 0.1) unless opted into.
- Undoable attribute *editing* (master-plan Phase 3) — separate slice.
- Symbology undo — separate existing plan.
