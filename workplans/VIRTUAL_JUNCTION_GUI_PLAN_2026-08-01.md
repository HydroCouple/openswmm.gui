# Virtual Junction — GUI Work Plan

## Status: IMPLEMENTED (core), 2026-08-03 — see "Implementation status" below

## Implementation status (2026-08-03)

Engine prerequisites all landed (`swmm_node_is_virtual` / `swmm_node_set_virtual` /
`swmm_conduit_split` / `swmm_virtual_junction_fuse`, distinct rule codes 609–621;
engine plan updated in openswmm.engine/plans/VIRTUAL_JUNCTION_IMPLEMENTATION_PLAN.md §0).
Note the engine-plan format correction: `[VIRTUAL_JUNCTIONS]` carries **Name + Elev**
(the invert is independent data — it fixes the grade break and both slopes).

- **Phase 1 DONE** — `NodeGeom.isVirtual` mirror (loader + applyNodeAdd/Convert);
  `applySetVirtual` / `applyInsertVirtualJunction` / `applyFuseVirtualJunction` +
  `virtualJunctionRuleText()` on SWMMModelLayer; `InsertVirtualJunctionCommand` (id 22,
  undo = engine fuse — exact inverse, byte-identical round-trip proven engine-side) and
  `FuseVirtualJunctionCommand` (id 23, ctor snapshots names/t/invert/coordinate; undo
  re-splits and restores invert + coordinate).
- **Phase 2 DONE** — `OpenSWMMVisMapToolAddVirtualNode` (pickAt conduit hit-test,
  polyline-t split point, hover marker preview, D-G3 empty-canvas status hint, VJ name
  prefix in PreferencesManager); action `actionAddVirtualJunction` wired through
  actioncatalog / swmmvis.ui / toolbar Nodes group / project-window activator /
  toolActionKeys / project-only gating; user-supplied `virtual_junction.svg` under qrc
  alias `VirtualJunction`.
- **Phase 3 DONE (core)** — per D-G1 a 5th paint bucket keyed off `isVirtual` renders
  virtual junctions with `m_virtualJunctionSym` (gray diamond) inside CatJunctions;
  identify popup reports "Virtual Junction". *Deferred:* object-browser sub-count row,
  legend row, NodeRenderingPrefs key.
- **Phase 4 DONE (core)** — `SWMMVirtualJunctionPropertyAdapter` (invert editable —
  with zero offsets the conduit end elevations follow the node invert directly, so the
  invert edit IS the write-through; no depth/ponding rows; no Inflows/DWF/RDII/Treatment
  compound rows); adapter switch keys off `swmm_node_is_virtual` before nodeKind.
  *Deferred:* "Convert To ▸ Virtual Junction/Junction" in typeconversionflow (the
  applySetVirtual API + rule-text mapping are ready for it).
- **Phase 5 PARTIAL** — link-draw tool refuses virtual endpoints (snap skips them);
  Select-tool delete of a virtual junction prompts Re-fuse (default) | Delete node &
  conduits | Cancel; deleting one pair conduit prompts Re-fuse | Delete-and-demote |
  Cancel; SubcatchOutlet picker excludes virtual nodes. *Deferred:* the remaining
  pick-list sites (GW/LID drain targets, 2D coupling, GIS import, attribute-table
  pickers) — the engine's validation + distinct rule codes backstop all of them.
- **Phase 6 NOT STARTED** — manual sections, release notes.
- Verification: engine tests green (11/11 `test_virtual_junction`); GUI builds clean;
  Python round-trip (parse → fuse → split → rule errors) verified. Interactive
  click-through (insert / undo / redo / fuse on the canvas) still to be done by hand.

## Context

Companion to the engine plan `openswmm.engine/plans/VIRTUAL_JUNCTION_IMPLEMENTATION_PLAN.md`. A virtual junction is a zero-storage, momentum-conserving node connecting exactly two conduits of identical cross-section; invert/crown auto-derived from the pipes; lateral inflows prohibited; represented in the `.inp` as `[VIRTUAL_JUNCTIONS]` and internally as an `is_virtual` flag on `NodeType::JUNCTION` (the binary `.out` is unchanged — virtual junctions report as junctions).

The GUI has no domain model of its own — the engine SoA is the model, `SWMMModelLayer` is the single mutation surface, and views sync via its signals (`geometryChanged`, `attributeChanged`, `repaintRequested`). All work below follows that MVC contract (CLAUDE.md §5.1): every mutation goes through a `SWMMModelLayer::apply*` method wrapped in a `MapCommand`, so map, property panel, attribute table, and object browser stay synchronized.

## Decisions (confirmed with user 2026-08-01)

| Topic | Decision |
|---|---|
| `.out` file | **Unchanged** — virtual junctions carry the JUNCTION type code; no output-format work in the GUI |
| Add action | New toolbar action + map tool; user-supplied icon |
| Insertion | Click a conduit → split it at the picked point, inserting a virtual junction |
| Invert editing | Editable in the property panel; write-through adjusts the two conduit end elevations (slopes recomputed by the engine) |
| Deletion | Default delete of a virtual junction **re-fuses** the two conduits into one; cascade delete (node + both conduits) remains available |
| Criteria enforcement | GUI enforces usage rules at every entry point (tool gestures, pickers, dialogs) with engine-side validation as the backstop |

## Design decisions proposed for review

- **D-G1 — Rendering: styled sub-class of `CatJunctions`, not a 5th category.** The `Category` enum order is persisted in project JSON (append-only) and `CatJunctions..CatDividers` assumptions are hardcoded in ~10 sites in `swmmmodellayer.cpp` (`:1895-1900`, `:3647-3649`, `:1254`, `:1296`, `:1654`, `:2708`, `:3147`, `:3706`, `:6267`). Recommend: virtual junctions stay in `CatJunctions` for bucketing/renderer purposes, with a per-node marker override keyed off `is_virtual` (distinct `MarkerShape`/symbol from the prepared icon, own `NodeRenderingPrefs` key `"virtual_junction"`, own object-browser sub-count and legend row). Alternative (5th category) is cleaner taxonomically but touches every hardcoded site plus persisted project JSON.
- **D-G2 — Engine-side split/re-fuse operations.** Conduit split and re-fusion are implemented **in the engine's edit module** (alongside `TypeConverter`/`ObjectDeleter`) and exposed through the C API, not composed client-side in the GUI. Rationale: MVC — the same operations must be available to the CLI, Python, and MCP surfaces with identical semantics (xsect copy, length split, vertex handling, name policy), and GUI-side composition cannot be made atomic against engine validation. Requires engine-plan additions (see "Engine prerequisites").
- **D-G3 — Free placement disabled.** The add tool only fires on a conduit hit (a virtual junction cannot exist without exactly two conduits); clicking empty canvas shows a status-bar hint instead of placing a node.

## Exploration findings (file:line anchors)

- Action registration chain: `forms/swmmvis.ui:1456` (Designer action) → `include/ui/actioncatalog.h:98-101` (catalog row with icon alias) → `src/swmmvisactions.cpp:160-175` (toolbar row) → `src/swmmvisprojectwindow.cpp:279-282, 1341, 1381-1400` (tool instantiation/activation) → `src/swmmvis.cpp:3525-3527, 761, 1430` (connects + gating).
- Tool enumeration lists that must learn the new tool: `swmmvisprojectwindow.cpp:331, 1517, 1540, 1587`.
- Link picking precedent: `OpenSWMMVisMapToolEditVertex::pickLink()` (`src/map/tools/maptooleditvertex.cpp`) — hit-test + closest-point-on-polyline; geometry helpers belong in `include/core/editgeometry.h`.
- Undo: `include/map/mapundostack.h` — `AddNodeCommand` (id 12) / `AddLinkCommand` (id 13) rely on **tail rollback** (`rollbackTailNodeAdd/LinkAdd`, header comments h:406-419) and cannot compose a split; `DeleteObjectCommand` (id 16) has the `LinkSnapshot` machinery to reuse. Macro precedent: `maptoolselect.cpp:719-721`.
- Property adapters: `include/ui/properties/swmmnodepropertyadapter.h` (base + 4 subclasses at :344/:378/:428/:483); adapter selection switch `src/ui/panels/propertiespanel.cpp:548-570`; conditional row editability precedent `:589-605` (`setRowEditable`).
- Type conversion flow (not undoable, confirm → mutate → summary): `include/ui/dialogs/typeconversionflow.h`, invoked from `maptoolselect.cpp:1230-1270`.
- Icons: SVG in `resources/images/`, alias in `resources/swmmvis.qrc:52-70`, theme recoloring via `IconFactory` (`src/ui/theme/iconfactory.cpp`) — glyph must use the `#777777` gray family; catalog row carries the alias.
- Node pick-lists to filter (lateral-inflow prohibition): `dataobjectpickereditor.cpp:122-135`, `swmmsubcatchpropertyadapter.cpp:247`, `subcatchcompoundeditdialog.cpp:331-335`, `featurelayerimporter.cpp:79-118`, `meshgenerationdialog.cpp:2925` (+ `meshnodemapper.cpp`, `meshautocouple.cpp`), `swmmvis.cpp:909-930`, `swmmattributetablemodel.cpp:1996, 2187`. The per-node Inflows/DWF/RDII compound rows are suppressed on the adapter (`swmmnodepropertyadapter.h:408`, `nodecompoundeditbutton.h`) rather than filtered.

## Engine prerequisites (additions requested to the engine plan)

1. C API: `swmm_node_is_virtual(engine, idx)` / `swmm_node_set_virtual(engine, idx, flag)` (the latter runs full validation and returns specific error codes).
2. C API: `swmm_conduit_split(engine, link_idx, t, new_node_name, new_link_name, make_virtual)` — splits at parameter `t` along the (vertex-aware) polyline length; copies xsect/roughness/barrels; interpolates end inverts; returns new node + link indices. Non-tail-safe undo data returned or achievable via snapshot/re-add.
3. C API: `swmm_virtual_junction_fuse(engine, node_idx)` — deletes the node, merges the two conduits (upstream conduit's name survives; lengths sum; the node coordinate becomes an interior vertex of the merged link so map alignment is preserved; downstream conduit's identifiers retired).
4. Validation surface: engine returns distinct error codes for each §6 rule (wrong link count, xsect mismatch, nonzero offsets, lateral inflow present) so the GUI can show actionable messages.

## Work plan

```
Phase 0  Review this plan; resolve D-G1..D-G3; engine C API additions accepted
         into the engine plan
         → verify: sign-off recorded here; engine plan updated

Phase 1  Engine API adoption layer
         - SWMMModelLayer: mirror is_virtual in node cache; new
           applyInsertVirtualJunction(link, t) / applyFuseVirtualJunction(node) /
           applySetVirtual(node, flag) calling the new C API; emit
           geometryChanged/attributeChanged per contract
         - New undo commands InsertVirtualJunctionCommand /
           FuseVirtualJunctionCommand (snapshot-based, reuse LinkSnapshot;
           new ids appended after 16)
         → verify: unit tests on a headless SWMMModelLayer — insert/undo/redo/
           fuse round-trip leaves engine object counts and geometry identical

Phase 2  Map tool + action + icon
         - resources/images/virtual_junction.svg (user-supplied icon, gray-family
           check), qrc alias "VirtualJunction", actioncatalog.h row
           {"model.addVirtualJunction", "actionAddVirtualJunction", ...}
         - forms/swmmvis.ui action; toolbar row; activator; the four tool-list
           enumerations; enable/disable gating lists
         - OpenSWMMVisMapToolAddVirtualNode: pickLink()-style conduit hit-test,
           closest-point marker preview, D-G3 empty-canvas hint, name prefix
           "VJ" registered in PreferencesManager (:344-348)
         → verify: manual + QtTest UI test — click conduit inserts node with
           split links; undo restores original conduit byte-identically in a
           written .inp; empty-canvas click is a no-op

Phase 3  Rendering + browser (per D-G1)
         - is_virtual marker override in the CatJunctions renderer path;
           NodeRenderingPrefs key "virtual_junction"; object-browser sub-count;
           identify-popup kind label "Virtual Junction" (swmmmodellayer.cpp:2983)
         → verify: symbol distinct in light/dark themes; project JSON round-trip
           stable; legend/browser counts correct

Phase 4  Property panel + editing rules
         - SWMMVirtualJunctionPropertyAdapter (subclass in
           swmmnodepropertyadapter.h): invertElev editable with write-through to
           both conduit end elevations via a single undoable command; derived
           fields (max depth, ponded area, surcharge depth) hidden; Inflows/DWF/
           RDII/Treatment compound rows suppressed; adapter switch in
           propertiespanel.cpp:548-570 keys off is_virtual before nodeKind
         - Convert To ▸ "Virtual Junction" / "Junction" via typeconversionflow
           (criteria check first; failure dialog lists specific violations from
           engine error codes)
         → verify: invert edit updates both conduit slopes and undoes cleanly;
           conversion of a non-conforming junction is refused with the right
           message; attribute table stays in sync (attributeChanged)

Phase 5  Criteria enforcement across entry points
         - Link-draw tool refuses a virtual node as endpoint when it already has
           2 conduits, or when the new link is a non-conduit (status-bar reason)
         - Filter the eight pick-list sites listed above (exclude virtual nodes
           from subcatchment outlets, GW/LID drain targets, 2D coupling, GIS
           import endpoint resolution where a non-conduit link would result)
         - Deleting one of the pair's conduits prompts: re-fuse | cascade |
           cancel
         → verify: each entry point tested; engine validation never fires from
           GUI-initiated edits (GUI catches everything first); engine backstop
           still tested via a deliberately corrupted call

Phase 6  Docs + release
         - docs/manual/12_map_editing.md "Adding a virtual junction" section;
           06_object_browser.md counts; release notes; CHANGELOG.md on release
           (CLAUDE.md §5.2)
         → verify: manual builds; feature demo checklist passes
```

## Risks / notes

- Phase 1 is blocked on the engine C API (engine plan Phase 2/4); Phases 2–3 can proceed against a stubbed layer if needed.
- Multi-engine version support (`workplans/MULTI_ENGINE_VERSION_SUPPORT_PLAN_2026-08-01.md`): when the loaded engine predates the API, the action must be hidden/disabled gracefully (probe for the symbols/capability at load).
- `typeconversionflow` conversions are historically not undoable — virtual↔regular conversion follows that precedent (documented in the confirm dialog) rather than inventing partial undo.
- Re-fusion name policy (upstream conduit survives) must match the engine implementation exactly; it is asserted in the Phase 1 round-trip test.
