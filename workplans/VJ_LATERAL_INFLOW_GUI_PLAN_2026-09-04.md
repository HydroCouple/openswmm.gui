# Lateral Inflows at Virtual Junctions — GUI Work Plan

**Status:** Approved 2026-09-04 — executing after engine Phase E1
**Companion:** `openswmm.engine/plans/VJ_LATERAL_INFLOW_PLAN_2026-09-04.md` (engine E-phases, viability verdict, decisions)
**Parent plan:** `workplans/VIRTUAL_JUNCTION_GUI_PLAN_2026-08-01.md` (Phase 4 "no Inflows/DWF/RDII/Treatment compound rows" and Phase 5 outlet-picker exclusion are superseded)

## Context

The engine lifts rule 5 (error 617) so a virtual junction may receive every point lateral
source (`[INFLOWS]`, `[DWF]`, RDII, subcatchment outlets, LID drains, runtime API); only 2D
surface coupling stays prohibited under 617. The GUI currently suppresses the four compound rows
on `SWMMVirtualJunctionPropertyAdapter`, excludes VJs from the subcatchment-outlet picker, and
words the 617 message as "cannot receive lateral inflow". The attribute table already exposes
the compound cells on VJ rows, so this change makes the two views consistent.

Decisions confirmed with the user (2026-09-04): expose Inflows, DWF, RDII **and Treatment** on
the VJ property sheet (depth/ponding rows stay hidden); re-fusing or deleting a fed VJ keeps the
engine's delete cascade (sources dropped / outlets unassigned) and the delete prompts say what
will be dropped; Insert-undo stays silent (matches the existing undo-AddNode-after-DWF precedent).

## Phase G1 — GUI

| File | Change |
|---|---|
| `include/ui/properties/swmmnodepropertyadapter.h` `SWMMVirtualJunctionPropertyAdapter` | Add `inflows`, `dwf`, `rdii`, `treatment` `Q_PROPERTY`s (copy the junction adapter's; base getters already exist in `src/ui/properties/swmmnodepropertyadapter.cpp`); rewrite the doc comment. |
| `src/ui/panels/propertiespanel.cpp` adapter switch | Comment only. |
| `src/ui/properties/dataobjectpickereditor.cpp` | Remove the VJ exclusion from subcatchment-outlet targets. |
| `src/layers/swmmmodellayer.cpp` `virtualJunctionRuleText` | 617 text → "A virtual junction cannot be coupled to a 2D surface mesh." |
| `src/map/tools/maptoolselect.cpp`, `src/ui/panels/attributetablepanel.cpp` | Reword the "no inflows" tooltip comment; Convert-To → Virtual Junction becomes enabled for fed junctions automatically via `swmm_node_virtual_eligible`. |
| New `include/layers/vjsourcesummary.h` + `src/layers/vjsourcesummary.cpp` | Pure helper `QString vjSourceSummary(SWMM_Engine, int nodeIdx)`: counts `[INFLOWS]`, DWF, RDII rows and subcatchments whose outlet is the node; empty when none. (LID `drain_to` has no C accessor; omitted.) |
| `src/map/tools/maptoolselect.cpp` delete prompts | Append the summary: "Re-fusing or deleting will remove: …; subcatchment(s) … will lose their outlet." |

Tests: `tests/gui/test_nodepropertyadapter.cpp` add `SWMMVirtualJunctionPropertyAdapter` to
`compoundRefsAdvertisedOnAllNodeKinds`; new `tests/gui/test_vjsourcesummary.cpp` (fixture
`tests/gui/data/vj_fed_fixture.inp`: fed VJ → summary names counts; unfed → empty) registered in
`tests/gui/CMakeLists.txt`, source `git add`ed. Manual click-through: VJ property sheet shows the
four "Edit…" rows; outlet picker lists VJs; delete prompt shows the warning; Convert-To → Virtual
Junction works on a DWF-fed junction.

## Phase G2 — Python / MCP docstrings

`openswmm.engine/python/openswmm/engine/_nodes.pyx` (`virtual_rule_violation`) and
`openswmm.mcp/src/openswmm_mcp/tools/nodes.py` (`virtual_eligible`): replace "no lateral inflow
sources" with "not 2D-coupled" and the 617 gloss.

## Verification

`tests/gui` green; the GUI builds against the installed engine (`find_package`), so install the
engine after E1 before the manual round-trip: split a conduit into a VJ, add a DWF in the
Property Browser, save `.inp` (row lands in `[DWF]`), reopen, run under DW and FV, confirm the
Virtual Junction Summary marker; delete the VJ and confirm the prompt lists the DWF row.
