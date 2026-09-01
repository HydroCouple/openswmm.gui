# Handoff — Object Creation Defaults (2026-08-03)

**To the verifying agent:** the feature below is implemented but **not built
and not tested** — the authoring environment had no Qt/CMake toolchain. Your
job: build, run the suite, run the manual smoke, and commit. **Never push.**

Plan of record: `workplans/OBJECT_CREATION_DEFAULTS_PLAN_2026-08-03.md`
(defaults table there is user-approved; do not change values without asking).

---

## SCOPE — read this first

The working tree on `swmm6_gui` contains **multiple unrelated bodies of
uncommitted work** (virtual-junction symbology/rendering, mesh work,
node-rendering prefs, type-conversion flow). Only the files and hunks below
are yours.

### New files (commit all)

```
include/map/objectdefaultsapplier.h
src/map/objectdefaultsapplier.cpp
include/ui/dialogs/objectdefaultspage.h
src/ui/dialogs/objectdefaultspage.cpp
tests/gui/test_object_defaults_prefs.cpp
tests/gui/test_object_creation_defaults.cpp
tests/gui/data/object_defaults_cfs.inp
tests/gui/data/object_defaults_cms.inp
```

### Modified files that are ENTIRELY this feature (stage whole file)

```
include/ui/dialogs/preferencesdialog.h   (2 hunks: buildObjectDefaultsPage decl,
                                          m_objectDefaultsPage member)
src/map/mapundostack.cpp                 (5 hunks: include + one per Add*Command::redo)
tests/gui/CMakeLists.txt                 (2 hunks: test_object_defaults_prefs,
                                          test_object_creation_defaults)
CMakeLists.txt                           (2 hunks: objectdefaultsapplier.cpp,
                                          objectdefaultspage.cpp)
```

Verify with `git diff <file> | grep ^@@` that the hunk counts still match; if
they don't, other work has landed in the meantime — fall back to `git add -p`.

### Modified files with FOREIGN hunks — stage with `git add -p`

| File | Yours | NOT yours (leave uncommitted) |
|---|---|---|
| `include/core/preferencesmanager.h` | hunk at ~:491 (ObjectDefaults struct + accessors, ~112 lines) | hunk at ~:386 |
| `src/core/preferencesmanager.cpp` | hunk at ~:1608 (ObjectDefaults impl, ~192 lines) | hunks at ~:82, ~:697, ~:705 (node-style defaults) |
| `src/ui/dialogs/preferencesdialog.cpp` | hunks at ~:6 (include), ~:89 (addCategory), ~:1475 (loadFrom), ~:1528 (buildObjectDefaultsPage fn), ~:1722 (applyTo), ~:1947 (resetToSeeds) | hunks at ~:462 (buildRenderingPage), ~:1870 |

Everything else modified in the tree (mesh, virtual junction, typeconversion,
attributetable, swmmvisactions, swmmvisprojectwindow, …) is other people's
in-flight work. Do not commit, revert, or tidy it.

---

## What was built

1. **`PreferencesManager::ObjectDefaults`** — per-type creation defaults for
   all 11 object types; TWO parallel sets (US customary / SI) because weir Cd
   and outlet C are unit-semantic, persisted under
   `SWMMVis/Preferences/ObjectDefaults/<US|SI>/<Type>/<prop>`. Seeds live in
   `usSeed()` / `siSeed()` (deviation from the member-initializer convention,
   documented in the header). `objectDefaults(bool si)` /
   `setObjectDefaults(d, si)`; emits `preferenceChanged("Defaults",
   "ObjectDefaults")`.

2. **`ObjectDefaultsApplier`** (`src/map/objectdefaultsapplier.{h,cpp}`) —
   free functions writing defaults into a fresh engine object via direct
   `swmm_*_set_*` calls; picks the set via `UnitSystem::instance()->isSI()`.
   Subcatchment infiltration writes only the family matching the
   subcatchment's engine-side infil model.

3. **Command wiring** (`src/map/mapundostack.cpp`) — each
   `Add{Node,Link,Gage,Subcatchment}Command::redo()` applies defaults
   immediately after the add and BEFORE auto-length / auto-area / terrain
   inverts, so geometry-derived values win. Conduit length and subcatchment
   area defaults are suppressed when the canvas `autoLength` flag is on.
   `applyXxxAdd` (the undo-of-delete replay primitive) is untouched — delete→
   undo restores captured properties, never defaults.

4. **GIS import** — no importer change needed: it pushes the same
   `Add*Command`s, and its `SetAdapterPropertiesCommand` runs after, so
   mapped attributes win. The planned `kKindCatch` adapter branch was NOT
   added: `TargetKind` has no subcatchment entry, so it would have no
   consumer. Follow-up for when subcatchment import lands.

5. **Preferences → Object Defaults page** — self-contained
   `ObjectDefaultsPage` widget (unit-system selector + Nodes / Links /
   Subcatchments / Rain Gages tabs); `PreferencesDialog` only forwards
   `loadFrom` / `applyTo` / `resetToSeeds`. Apply writes BOTH sets; the
   selector initialises to the open project's unit system.

---

## Verification

### 1. Build

```
cmake --build build/Darwin --parallel     # or your platform preset
```

Likely first-build issues to fix mechanically if they surface (all were
authored blind):
- Missing `#include <QPair>` in `objectdefaultspage.cpp` (used in the
  `makeCombo` parameter type).
- `AddNodeCommand` ctor arg order in the new test
  (`layer, name, nodeType, x, y, canvas, invertElev`).
- `swmm_subcatch_index` / `swmm_gage_index` linkage (both confirmed present
  in the engine headers).

### 2. Full GUI suite

```
ctest --test-dir build/Darwin -L gui --output-on-failure
```

New tests, all must be green:

- `test_object_defaults_prefs` — pins the signed-off table (US + SI seeds),
  round-trip persistence, US/SI set independence, `preferenceChanged` emit.
- `test_object_creation_defaults` —
  `usDefaults_appliedToDrawnObjects` (storage 15 ft / A=1000; conduit
  circular 1 ft, n 0.013, len 400; subcatch width 500, slope 0.5; gage 900 s,
  SCF 1), `siDefaults_selectedBySiProject` (0.3 m, 120 m, 4.5 m),
  `terrainInvert_winsOverDefaults`, `editedPreference_flowsThroughToCreation`,
  `undo_rollsTheWholeObjectBack`.

**Sanity-check the tests are real:** comment out the
`ObjectDefaultsApplier::applyLinkDefaults` call in `AddLinkCommand::redo()`,
confirm `usDefaults_appliedToDrawnObjects` fails on the conduit block,
restore.

### 3. Engine-contract checks the suite may not cover

Verify during smoke (these are the assumptions most likely to be wrong):

- `swmm_link_set_xsect` on **orifice and weir** link types — the applier
  assumes it is accepted for non-conduit links. If the engine rejects it,
  the orifice diameter / weir height-length defaults silently no-op; check
  the property panel after drawing one of each.
- `swmm_node_set_storage_functional` while the engine is in OPENED (editing)
  state — the header says BUILDING or OPENED, but confirm.
- A **Curve-Number project**: draw a subcatchment and confirm the CN default
  (80) landed and Horton params were NOT written. The automated test only
  covers a HORTON fixture. Fixture: set `INFILTRATION CURVE_NUMBER`.
- Weir `end_contractions` setter takes a double; the struct stores int —
  conversion is explicit, just confirm the value reads back as 0.

### 4. Manual smoke

Open Preferences → Object Defaults:
1. Page renders; four tabs; unit selector defaults to US with no project.
2. Change conduit roughness to 0.02, Apply, reopen — persists.
3. Switch selector US↔SI — values swap (1.0 ↔ 0.3 diameter); edits to both
   sets survive one Apply.
4. Reset to Defaults restores the table values on this page too.

In a US project (File → New):
5. Draw one of EACH of the 11 types (junction, outfall, storage, divider,
   conduit, pump, orifice, weir, outlet, subcatchment, rain gage) and check
   the property panel matches the table for each.
6. With auto-length ON, a drawn conduit gets its geometric length, not 400.
7. Delete an object you've hand-edited, undo — edited values return, not
   defaults.
8. GIS import (any point layer → junctions): unmapped max depth = default;
   a mapped field wins over the default.

Repeat 5 (spot-check conduit + storage) in a CMS project → SI values.

### 5. Commit

Two commits if you prefer (prefs bundle+page / applier+wiring+tests) or one;
conventional style matching `swmm6_gui` history. Suggested single message:

```
feat(gui): per-object-type creation defaults, configurable in Preferences

Newly drawn objects landed with engine zeros for every hydraulic property
(0-diameter conduits, 0-depth storage, 0-width subcatchments), so a field
the modeler forgot to edit produced a silently wrong simulation.

- PreferencesManager::ObjectDefaults: creation defaults for all 11 object
  types, in parallel US-customary and SI sets (weir Cd and outlet rating C
  are unit-semantic), persisted per set and seeded from usSeed()/siSeed().
- ObjectDefaultsApplier writes the active set into a fresh engine object;
  Add{Node,Link,Gage,Subcatchment}Command::redo() applies it before
  auto-length/auto-area/terrain overrides so geometry-derived values win.
  The applyXxxAdd replay primitives stay defaults-free, keeping
  delete->undo lossless. GIS import inherits the defaults through the same
  commands; mapped attributes still win.
- New Preferences page (Object Defaults) edits both unit-system sets with
  a selector initialised from the active project.
- Subcatchment infiltration defaults follow the subcatchment's engine-side
  infiltration model (Horton / Green-Ampt / Curve Number families).

Adds tests/gui/test_object_defaults_prefs.cpp,
tests/gui/test_object_creation_defaults.cpp and two .inp fixtures.
```

**Author as the repository's configured user.** No AI attribution anywhere.
Standing conventions: `workplans/`, `test_artifacts/`, `Testing/` stay
untracked; `CHANGELOG.md` only at release (§5.2). **Never push.**
