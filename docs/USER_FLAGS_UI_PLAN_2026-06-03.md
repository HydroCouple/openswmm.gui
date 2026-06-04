# User Flags UI Plan (2026-06-03)

Plan of attack for surfacing the engine's user-flags mechanism (R28, InfoWorks-ICM style)
in the GUI: a flag-definition dialog plus optional per-object flag values editable in both
the Attribute Table and the Attribute Panel (attributes dialog), kept synchronized via the
existing MVC patterns.

Status: **IN PROGRESS** — proceeding with the recommended options for all §10
decisions (string-form C API, no GUI scoping, compound-ref panel row pending
feasibility check).

Progress log:

- 2026-06-03 Phase 0 implemented in openswmm.engine (`swmm_userflag_def_count/
  def_get/define/undefine/value_get/value_set/value_clear`, `UserFlags::
  undefine/unset`, `test_user_flags_capi.cpp`). Also fixed the pre-existing
  MODEL-level `swmm_userflag_get/set_*` to uppercase flag names (case-
  insensitivity now uniform with the INP handlers).
- 2026-06-04 Phase 1 implemented: `openswmmvis::ui::UserFlagsModel`
  (include/ui/models/userflagsmodel.h + src/ui/models/userflagsmodel.cpp),
  owned by `SWMMModelLayer::ensureUserFlagsModel()` (same lazy-init +
  engine-handle-guard pattern as `ensureTimeseriesRegistry()`); test
  `tests/gui/test_userflagsmodel.cpp` (define/undefine/value round-trips,
  QSignalSpy on defsChanged/valueChanged, INP round-trip written to
  tests/gui/data/). Design refinements vs. §3.1 (to match vetted repo
  patterns): the layer accessor is `ensureUserFlagsModel()` returning the
  concrete type (forward-declared), and dirty-marking stays with consumers
  (dialogs/panels), mirroring how `setOption()`/SimulationOptionsDialog
  handle it — the model itself only emits change signals.

---

## 1. Engine Background (source of truth)

The engine (`openswmm.engine`) already implements user flags end to end:

- Core container: `src/engine/core/UserFlags.hpp` — `UserFlags` class holding
  `UserFlagDef { name, type, description }` plus per-object values keyed by composite key
  `"OBJECTTYPE:OBJECTNAME:FLAGNAME"` (`make_key()`).
- Types: `UserFlagType` enum — `BOOLEAN(0)`, `INTEGER(1)`, `REAL(2)`, `STRING(3)`.
  Flag names stored UPPERCASE, lookups case-insensitive. Object names case-preserved.
- Object-type agnostic: any flag may be assigned to any object type
  (`NODE`, `LINK`, `SUBCATCHMENT`, `GAGE`, `POLLUTANT`, …). The engine does not restrict
  applicability per object type — the GUI is the place to scope that, if at all.
- INP persistence (round-trips via `InpWriter.cpp` L1332–1355):
  - `[USER_FLAGS]` — `Name  Type  "Description"` (schema definitions)
  - `[USER_FLAG_VALUES]` — `ObjectType  ObjectName  FlagName  Value`
    (BOOLEAN → `YES`/`NO`, INTEGER → `%d`, REAL → `%g`, STRING quoted if spaces)
- Tests: `tests/unit/engine/test_user_flags.cpp` (30+ cases).

## 2. C API Gap — Prerequisite Work in the Engine Repo

The GUI talks to the engine exclusively through the C API
(`include/openswmm/engine/openswmm_model.h`). The current flag surface
(`swmm_userflag_get/set_{bool,int,real}`, L554–591) is **schema-level value access only**.
The GUI needs, and the C API does not yet provide:

| Needed capability | Proposed C API addition |
|---|---|
| Enumerate flag definitions | `swmm_userflag_def_count(engine, int*)`, `swmm_userflag_def_get(engine, index, name_buf, type*, desc_buf)` |
| Define / update a definition | `swmm_userflag_define(engine, name, type, description)` |
| Remove a definition (+ its values) | `swmm_userflag_undefine(engine, name)` |
| Per-object value get | `swmm_userflag_value_get(engine, obj_type, obj_name, flag_name, value_str_buf)` (string form; GUI converts by declared type) or typed variants |
| Per-object value set | `swmm_userflag_value_set(engine, obj_type, obj_name, flag_name, value_str)` |
| Per-object value clear ("unset") | `swmm_userflag_value_clear(engine, obj_type, obj_name, flag_name)` |
| Enumerate values for one object | `swmm_userflag_value_count(...)` / iterate — needed for the Attribute Panel and INP-loaded models |

> **Decision needed (string-form vs typed per-object accessors).** String-form keeps the
> API surface small (3 functions) and mirrors INP formatting; typed variants
> (`..._get_bool/int/real/string`) match the existing schema-level naming. Recommendation:
> string-form with the declared type used for parsing/validation engine-side, because the
> GUI already knows the type from the definition and one code path is simpler. Review
> before implementing.

This is **Phase 0** and lands in `openswmm.engine` (with unit tests extending
`test_user_flags.cpp` and Python binding parity left as a follow-up, out of scope here).
Without Phase 0 the GUI phases cannot proceed.

## 3. GUI Architecture (MVC)

Per CLAUDE.md §5.1 and `SYMBOLOGY_MVC_ARCHITECTURE_AND_GAPS.md`: one live model, many
observing views, all edits routed through the model, one change signal.

### 3.1 Model: `UserFlagsModel` (new)

- Files: `include/ui/models/userflagsmodel.h`, `src/ui/models/userflagsmodel.cpp`.
- Thin per-project QObject wrapper over the engine's flag store via the Phase 0 C API.
  Owned by `SWMMModelLayer` (same pattern as options: `setOption()` → `optionsChanged()`).
- Responsibilities:
  - `defs()` — read-through list of `{name, type, description}` (cached; invalidated on edit).
  - `define(name, type, desc)` / `undefine(name)` — write to engine, emit `defsChanged()`.
  - `value(objType, objName, flagName)` / `setValue(...)` / `clearValue(...)` — write to
    engine, emit `valueChanged(objType, objName, flagName)`.
  - GUI-side **applicability scoping** (optional, see §4 decision): which object
    categories a flag applies to. If adopted, stored GUI-side (e.g., .oswp sidecar),
    since the engine is intentionally unscoped.
- All edits mark the project dirty (same as options edits).

### 3.2 Views/Controllers

1. **User Flags Manager dialog** (new) — defines the schema.
2. **Attribute Table** — extra per-category columns for flag values.
3. **Attribute Panel** — extra rows for the bound object's flag values.

All three observe `defsChanged()` / `valueChanged()`. Cross-view object-edit sync reuses
the existing `objectEdited(name)` forwarding between `AttributeTablePanel` and
`AttributePanel` (reentrancy-guarded via `m_suppressEditForward`).

## 4. User Flags Manager Dialog

- Files: `include/ui/dialogs/userflagsdialog.h`, `src/ui/dialogs/userflagsdialog.cpp`
  (programmatic UI — no .ui form file, matching `SimulationOptionsDialog` conventions).
- Constructor: `UserFlagsDialog(SWMMModelLayer *layer, QWidget *parent = nullptr)`.
- Launch: new `actionUserFlags` ("User Flags…") in the main window's Project/Edit menu,
  wired in `swmmvis.cpp` next to `onSimulationOptions` (fresh dialog per invocation).
- UI: single table (`QTableView` + small `QAbstractTableModel` over `UserFlagsModel::defs()`):

  | Column | Editor |
  |---|---|
  | Name | Line edit; uppercased on commit; uniqueness validated (engine overwrites silently — GUI should warn) |
  | Type | Combo: Boolean / Integer / Real / String |
  | Description | Line edit |
  | Applies To (optional, see decision) | Multi-check combo of object categories |

  Add / Remove buttons. Removing a definition warns that existing per-object values for
  that flag will also be removed.
- Type changes on a flag that already has values: warn and re-validate/convert or clear
  incompatible values (simplest: warn + clear; matches engine "overwrite" semantics).

> **Decision needed (applicability scoping).** The engine allows any flag on any object
> type. Options:
> (a) **No scoping** — every defined flag appears for every category. Simplest, zero extra
> persistence, matches engine exactly.
> (b) **GUI-side "Applies To" list** per flag — keeps the attribute table uncluttered when
> many flags exist, but requires .oswp sidecar persistence and divergence from engine.
> Recommendation: start with (a) for the first slice; add (b) only if column clutter is a
> real problem. Confirm before implementation.

## 5. Attribute Table Integration

- `SWMMAttributeTableModel::rebuildColumnSchema()` appends one `ColumnSpec` per defined
  flag after the built-in columns:
  - `EditorKind` by type: BOOLEAN → Enum (`Yes`/`No`/`(unset)`), INTEGER → Integer,
    REAL → Numeric, STRING → Text.
  - `key` namespaced (e.g., `"userflag:INSPECTED"`) so flag columns are distinguishable
    from identify-map keys; `setter` tag routed to `UserFlagsModel::setValue()` instead of
    the engine-setter dispatch table.
  - Header label = flag name (description as tooltip). No unit suffix.
- Category → engine object-type mapping: Junctions/Outfalls/Storage/Dividers → `NODE`;
  Conduits/Pumps/Orifices/Weirs/Outlets → `LINK`; Subcatchments → `SUBCATCHMENT`;
  RainGages → `GAGE`. (Data-object categories such as Pollutants are out of scope for the
  table in this slice; the panel covers them later if needed.)
- **Optionality / unset state:** flags are optional per object. Display blank for unset.
  Clearing a cell (empty commit) calls `clearValue()` — the row must NOT be written to
  `[USER_FLAG_VALUES]` when unset. Boolean editor includes an explicit `(unset)` entry.
- Read path: flag values are fetched via `UserFlagsModel` and merged into the existing
  per-row `QVariantMap` cache; `refreshObject(name)` invalidation works unchanged.
- `defsChanged()` → full `rebuildColumnSchema()` + `reload()` + re-run
  `installColumnDelegates()`.
- Undo: wrap `setValue`/`clearValue` in the existing `EditCommand` pattern on the model's
  `QUndoStack`.

## 6. Attribute Panel Integration

`AttributePanel` is driven by QPropertyModel over static `Q_PROPERTY` declarations, so
flags (dynamic, user-defined) cannot be static properties.

> **Decision needed (panel mechanism).** Options:
> (a) **Qt dynamic properties** on the existing adapters (`setProperty()` per flag in the
> adapter constructor/refresh). Works only if QPropertyModel surfaces dynamic properties —
> must be verified first; editor typing and the unset state are awkward.
> (b) **Compound-ref row** — a single "User Flags…" row using the existing
> `NodeCompoundEditRef`/`CompoundEditDelegate` pattern (as Inflows/DWF/RDII do) that opens
> a small per-object flag editor (flag, value, set/clear). Proven pattern, handles unset
> cleanly, works identically for node/link/subcatch adapters, no QPropertyModel changes.
> Recommendation: **(b)**. Verify (a)'s feasibility in 30 minutes first; if dynamic
> properties render and edit cleanly, (a) gives nicer inline UX.

- Adapter changes: `SWMMNodePropertyAdapter`, `SWMMLinkPropertyAdapter`,
  `SWMMSubcatchPropertyAdapter` (and `SWMMRainGagePropertyAdapter`) gain a
  `userFlagsRef()` compound ref (option b) wired to `UserFlagsModel`.
- Edits emit the adapter's `changed()` → panel emits `objectEdited(name)` → table row
  refreshes (existing path, no new sync code).

## 7. Persistence

No new GUI persistence work for definitions/values: the engine round-trips
`[USER_FLAGS]` / `[USER_FLAG_VALUES]` in INP read/write already. GUI only marks the
project dirty on flag edits. (If decision §4(b) is adopted, "Applies To" goes in the
.oswp sidecar like notes.)

## 8. Phased Execution

Each phase is independently verifiable; do not start a phase until the previous one's
verification passes.

```
Phase 0  (engine repo): C API for flag defs + per-object values
         → verify: new unit tests in test_user_flags.cpp / a new test_user_flags_capi.cpp
           pass; INP round-trip unchanged (existing tests green).

Phase 1  (gui): UserFlagsModel wrapper + ownership in SWMMModelLayer
         → verify: unit test driving define/set/clear via the model against a small INP
           fixture written to a reviewable test-output folder (per CLAUDE.md §4
           Transparent File IO), confirming signals fire and INP save reflects edits.

Phase 2  (gui): User Flags Manager dialog + menu action
         → verify: manually define BOOLEAN/INTEGER/REAL/STRING flags, save project,
           reopen, definitions persist; removing a definition removes its values.

Phase 3  (gui): Attribute Table columns + delegates + undo
         → verify: flag columns appear per category; set/clear values; blank = unset and
           absent from saved [USER_FLAG_VALUES]; Ctrl+Z works; defsChanged() rebuilds
           columns live.

Phase 4  (gui): Attribute Panel integration (per §6 decision)
         → verify: edit a flag in the panel, table row updates; edit in table, panel
           updates; no feedback loop (m_suppressEditForward path exercised).

Phase 5  Round-trip + regression pass
         → verify: load an INP with both sections (e.g., adapted from engine test
           fixtures), confirm display/edit/save fidelity, including quoted strings,
           negative integers, %g reals, case-insensitivity of flag names.
```

## 9. Explicitly Out of Scope

- Python-binding parity for new C API functions (engine follow-up).
- GeoPackage persistence of flags (no engine spec exists yet).
- Flag values for non-spatial data objects (pollutants, curves, …) in the attribute
  table — engine supports it; defer until a need is demonstrated.
- Using flags in query-bar predicates or symbology — natural follow-ups, not this slice.

## 10. Open Questions (answer before coding)

1. §2 — string-form vs typed per-object C API accessors? (rec: string-form)
2. §4 — applicability scoping in GUI, or all flags on all categories? (rec: no scoping first)
3. §6 — panel mechanism: dynamic properties vs compound-ref row? (rec: compound-ref,
   pending a quick feasibility check on dynamic properties)
4. Should the Manager dialog be reachable from the attribute table header context menu
   ("Manage user flags…") in addition to the main menu? (cheap, suggest yes)
