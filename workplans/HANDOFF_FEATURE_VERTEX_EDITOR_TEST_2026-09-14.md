# Handoff — Verify the feature vertex editor (2026-09-14)

**To the testing agent:** commit `d401c92`
(*feat(features): type exact X, Y and Z for a feature's vertices*) adds a
**Vertices** grid to the Features dock so a feature's coordinates can be typed
instead of only dragged. **It has never been compiled or run** — the authoring
environment had no Qt6 toolchain. A full static review against the real headers
found no compile errors and five logic bugs, all fixed before the commit, but a
clean review is not a green build.

Your job: build it, run the tests, drive the panel by hand against the checklist
below, fix what breaks, and report. **Do not push.**

---

## SCOPE — read this first

### Yours

`d401c92` only: `src/ui/panels/featurelayerpanel.cpp` (the `Vertices` group box
in `buildUi()`, `refreshVertexTable()`, `onVertexCellChanged()`,
`restoreZExcept()` and the anonymous-namespace helpers),
`include/ui/panels/featurelayerpanel.h`, and the five new tests in
`tests/unit/test_featuregeometry.cpp`.

### NOT yours — leave alone

`swmm6_gui` carries substantial uncommitted work that is not part of this
change. At the time of writing:

```
CHANGELOG.md                         (an unfinished "EPA SWMM 5.2.4" entry)
CMakeLists.txt
src/simulation/simulationrunner.cpp
src/swmmvis.cpp
src/ui/dialogs/preferencesdialog.cpp
tests/gui/CMakeLists.txt
tests/gui/data/*.oswp, tests/gui/data/*.inp
workplans/GW_TRANSPORT_HYDROCOUPLE_COMPONENT_PROGRAM_PLAN_2026-09-05.md
```

plus ~18 untracked workplans and test-data directories. **Commit with an
explicit pathspec. Never `git add -A`, never `git commit -a`.** The CHANGELOG in
particular has a staged/unstaged split: `d401c92` deliberately committed only
its own entry and left the 5.2.4 entry in the working tree.

### Out of scope

- `onFeatureCellChanged()` rebuilds the feature grid synchronously from inside
  `cellChanged`, the same pattern `onVertexCellChanged()` was changed to avoid.
  It is pre-existing and much less frequent. **Do not fix it here** — note it if
  you see misbehaviour and leave it for its own commit.
- Building the missing `test_featureviews` rig (see "The coverage hole").
- `FeaturePropertyAdapter`, still unbuilt; see the B6 note in the plan §7.

---

## What the feature does

In the Features dock, below the feature grid. Select **exactly one** feature and
the grid lists its vertices, one row each:

| Part | Ring | # | X | Y | Z |
|---|---|---|---|---|---|

- `Ring` is `exterior` for the outline and `hole 1`…`hole N` for interior rings.
- X, Y and Z are editable only while the layer is in an edit session.
- Rows carry their `(part, ring, index)` address in `Qt::UserRole…+2` on the
  Part cell.
- Each accepted edit is one non-mergeable `EditFeatureGeometryCommand`.

**Ring-index trap:** this grid uses ring `0` = exterior, `1..n` = holes.
`FeatureVertexRef` in `include/map/tools/maptoolfeatureedit.h` uses the
*opposite* convention (`-1` = exterior, `0..` = holes). The two never meet, but
do not "harmonise" them casually while debugging.

---

## Step 1 — build and run the leaf tests

```bash
cmake --build build --target test_featuregeometry -j
ctest --test-dir build -R featuregeometry --output-on-failure
```

Five new tests, all in `tests/unit/test_featuregeometry.cpp`:

| Test | Asserts |
|---|---|
| `FeatureGeometryRing.MoveVertexKeepsZOnItsVertex` | `moveVertex` writes only `pts`, so Z rides with its vertex and a NaN stays NaN |
| `FeatureGeometryRing.MoveVertexOutOfRangeIsANoOp` | index `-1` and `size()` change nothing |
| `FeatureGeometryValidate.MovingAHoleVertexOutOfTheExteriorIsRejected` | hole corner dragged to (500,500) fails containment |
| `FeatureGeometryValidate.MovingAVertexIntoSelfIntersectionIsRejected` | corner moved to (-50,100) bow-ties the ring; crossing is at (0, 66.7) |
| `FeatureGeometryValidate.AnOrdinaryVertexMoveStaysValid` | a benign move still validates |

These were hand-traced through `mesh::ringIsSimple` and `pointInRing` and are
expected to pass. If one fails, the geometry predicate is the interesting part,
not the test — read the failure before "fixing" the expectation.

## Step 2 — build the app

```bash
cmake --build build --target SWMMVis -j
```

The static review found no compile errors, but it is a review. Likely trouble
spots if it does not build, in order of suspicion:

1. `QMetaObject::invokeMethod(this, &FeatureLayerPanel::refreshVertexTable, Qt::QueuedConnection)`
   — the pointer-to-member overload; needs the slot to be accessible (it is
   private, called from a member, so this should be fine).
2. `qScopeGuard` / `<QScopeGuard>` — five precedents exist in the tree.
3. `QItemSelectionModel::NoUpdate` in `setCurrentCell` — include was added.
4. `restoreZExcept`'s `std::min` on `qsizetype` from `QList::size()` — if this
   warns or fails, cast explicitly.

---

## Step 3 — manual verification (the part no test covers)

You need a feature layer with a polygon that **has at least one hole**, and a
second **3D** layer with a raster Z source. Create them through the Features
ribbon: *New feature layer…* (the project must be saved first — PLAN §9 Q5
refuses a `.gpkg` for an unsaved model), then draw with the Polygon tool and
*Add hole*.

Work the checklist and record actual behaviour for each:

**Display**

1. Select one feature → rows appear; part/ring/# read sensibly; the hole's rows
   say `hole 1` and come after the exterior's.
2. Select two features → grid clears, hint says to select exactly one.
3. Select none → grid clears with the "Select one feature" hint.
4. Remove the active layer → grid clears rather than showing the dead layer's
   coordinates.
5. Outside an edit session the cells are read-only and the hint says so.
6. On a 2D layer the Z column shows `—` and never becomes editable.

**Editing — the high-risk behaviours**

7. **Focus and Tab.** In an edit session, change an X and press Enter. The table
   must keep focus and Tab must still move to the next cell. Watch the console
   for `QAbstractItemView::closeEditor called with an editor that does not
   belong to this view` — that warning is exactly what the queued rebuild exists
   to prevent, and its return means the deferral is not working.
8. **One undo step per edit.** After one X edit, a single Ctrl+Z restores the
   old value. Not two, not none.
9. **Precision.** Set an X to something with more than six decimals via the map
   or a fresh draw, then edit only that row's **Y**. Confirm the X is unchanged
   at full precision (check the `.gpkg`, or undo and compare) — it must not be
   rounded to the displayed six decimals.
10. **Small edits are not swallowed.** On a layer with large coordinates (a
    projected CRS with a northing in the millions), change an X by `0.000001`.
    It must take effect, not silently revert.
11. **Rejection.** Move a hole vertex far outside the exterior. Expect a status
    message and the cell reverting to the stored value, with no write.
12. **Self-intersection.** Move an exterior vertex across the opposite edge.
    Same: message, revert.
13. **Garbage input.** Type `abc` → "is not a number", cell reverts.

**Z**

14. On the 3D layer, type a Z. It sticks, and the dock's hint says a resample
    will overwrite it.
15. Clear a Z to blank → the vertex becomes unsampled. Confirm via the dock's
    status line ("N vertices with no elevation"), and confirm it did **not**
    become `0`.
16. **The bug most likely to still be wrong:** with *Re-sample when a vertex
    moves* ON, type a distinctive Z on vertex 5, then edit vertex 2's X. Vertex
    5's typed Z **must survive**. That is what `restoreZExcept()` is for; if it
    is wiped, that function is not doing its job.
17. *Resample Z now* overwrites typed Z values — that is intended, and one
    Ctrl+Z reverts the lot.

**Sync (CLAUDE.md §5.1 — the same data from several views)**

18. Drag a vertex on the map with the vertex tool → the grid updates to the new
    coordinates.
19. Edit an X in the grid → the map redraws in the new position.
20. Undo/redo → both views follow.
21. Edit vertex 400 of a long ring: the grid must not scroll back to the top,
    and a column width you dragged must survive.

**Scale**

22. Import or draw a feature with more than 10,000 vertices → the grid refuses
    with the "too many to list" hint instead of freezing. Confirm the dock stays
    responsive.

---

## The coverage hole (context, not a task)

**Nothing in the tree constructs a `FeatureLayer` or a `FeatureLayerPanel`**, so
none of Step 3 is automated. The plan's verification gates 5, 6 and 7
(`test_featuretools`, `test_featurez`, `test_featureviews`) were never built.
That is why this handoff is a manual checklist and why the commit's tests sit at
the geometry layer.

If Step 3 finds real bugs, the right follow-up is the rig, not more manual
passes: a GUI test that makes a `.gpkg`-backed `FeatureLayer`, a `MapCanvas`
with an undo stack, and a `FeatureLayerPanel`, then drives cells with
`QTest::keyClicks`. `tests/gui/test_featurelayer_hostwiring.cpp` shows how to
stand up the real window; `tests/unit/test_featurestore.cpp` shows the `.gpkg`
fixture pattern. Fixtures go under `tests/gui/data/` per CLAUDE.md §4.1 — not a
temp dir.

---

## Report back

State, plainly: whether it built; whether the five tests passed; and the
checklist item numbers that failed with what actually happened. If item 7 or 16
fails, say so prominently — those are the two behaviours the commit specifically
claims to have fixed and the two most likely to be wrong.

## Commit

Conventional commits with a scope, a body explaining mechanism and consequence,
and a `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`
trailer — see `d401c92` and `1e59f38`. Explicit pathspec. **Do not push.** If
you change behaviour the plan describes, amend
`workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md` §4.4 with a
dated note rather than letting the document drift.

## Working agreements (from CLAUDE.md)

- Surgical: every changed line traces to a checklist failure. Do not refactor
  the panel, restyle the dock, or tidy `onFeatureCellChanged`.
- Simplicity first: no speculative configurability.
- State assumptions. If the behaviour contradicts this document, say so rather
  than bending the diagnosis to fit.
