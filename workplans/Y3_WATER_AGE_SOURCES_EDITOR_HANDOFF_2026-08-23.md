# Y3 Validation Handoff — Water Age Sources Editor (GUI plan §3.4 / G3g)

**Date:** 2026-08-23 · **Repo:** `openswmm.gui` · **Base:** `dcc20e6` (Y2a) ·
**Engine:** install carrying `openswmm_water_age.h` (engine `d7b6c079`, X5) ·
**Plan:** `TRANSPORT_QUALITY_GUI_PLAN_2026-08-12.md` §3.4 / §7 G3.

> **⚠ NEVER COMPILED** (no Qt toolchain here) — same caveat as Y1/Y2a.
> One API-shape defect was already caught and fixed by reading the engine
> header: `swmm_node_add` takes three arguments, not four.

> **✅ THIS ROUND CLOSES THE OBSERVER HOLE.** Y1's page and Y2a's wiring had
> no widget-level test because `SimulationOptionsDialog` / `SWMMResultsLayer`
> cannot be linked into a test. **This dialog was deliberately built
> dependency-light** — Qt Widgets plus one engine ABI, no project window, no
> layers, no registries — so the test constructs it, hydrates it, clicks OK,
> and reads the engine back. That is the `ClimatologyDialog` precedent, and
> it is the reason the six gates below can exist at all.

---

## 0. Hunk-presence check

| grep (repo root) | expected |
|---|---|
| `grep -c "wateragesourcesdialog" CMakeLists.txt` | **2** |
| `grep -c "wateragesourcesdialog" tests/gui/CMakeLists.txt` | **4** |
| `grep -c "void Test" tests/gui/test_wateragesourcesdialog.cpp` | **6** |
| `grep -c "swmm_water_age_" src/ui/dialogs/wateragesourcesdialog.cpp` | **≥ 6** |

## 1. Changeset

| File | Change |
|---|---|
| `include/ui/dialogs/wateragesourcesdialog.h` + `src/ui/dialogs/wateragesourcesdialog.cpp` | **NEW.** `WaterAgeSourcesDialog`: a fixed 7-row global-age table (one per `SWMM_WaterAgeSource`) and an add/remove per-node override table, read from and written to `openswmm_water_age.h`. `wroteAnyChanges()` / `lastWriteCount()` for the caller and for the no-op gate |
| `tests/gui/test_wateragesourcesdialog.cpp` | **NEW** — 6 cases, driven through the widgets |
| `CMakeLists.txt`, `tests/gui/CMakeLists.txt` | registered |

## 2. Design decisions (challenge in this order)

1. **Dependency-light on purpose** (see the banner). Any future addition
   that pulls in `ProjectWindow`, `SWMMModelLayer` or the registries would
   *silently cost this round's entire observer story* — the test would stop
   linking. If you need project context here, add it behind an interface,
   not by including the world.
2. **`QTableWidget`, not a `QAbstractTableModel`.** GUI plan §3.4 names a
   `WaterAgeSourceTableModel`. Deviation, flagged: the engine handle is
   already the model (plan §2 — "not separate registries … option-like
   keyed tables read/written through `openswmm_water_age.h`"), this table
   has exactly one editor and no second view to keep in sync, and a model
   class would add indirection without buying MVC synchronisation
   (CLAUDE.md §5.1's actual concern). Overturn if a second view appears.
3. **Negative ages are accepted and explained**, not validated away — the
   spin range is ±1e6 h and the hint text says what a negative does (engine
   D-NS1: extraction, clamped so age never goes below zero). A validator
   that rejected them would make a legal engine configuration unreachable
   from the GUI.
4. **Only DWF and EXTERNAL_INFLOW are offered as override sources** — the
   parser's A1a scope rule, which `swmm_water_age_set_override` enforces
   with `SWMM_ERR_BADPARAM`. Gate 6 pins this: an editor that offered more
   would let a user author a table that fails to reload.
5. **Write-back skips unchanged values** (globals and overrides both), so a
   no-op OK writes nothing — gate 3. Without this, opening and closing the
   dialog dirties the project and defeats change tracking.
6. **Overrides are reconciled remove-then-set.** Engine rows the table no
   longer carries are removed first; every table row is then `set`
   (add-or-update). A row the user deleted must not survive as a stale key
   still applying an age — gate 4's second half.
7. **The dialog is not yet reachable from a menu.** Wiring the action
   (`actionEditWaterAgeSources`, GUI plan §4) touches `swmmvisactions.cpp`
   and the Model ribbon — deliberately deferred so this round stays
   dependency-light and reviewable. **The editor is therefore complete but
   unreachable until Y3b**; see §7.

## 3. Anticipated failure modes, likelihood order

1. ⚠ **Compile errors** — the usual for a never-compiled Qt round. Watch
   `QT_TRANSLATE_NOOP` + `QCoreApplication::translate` pairing (needs
   `<QCoreApplication>`; it arrives via other Qt headers but add it if the
   compiler disagrees), and `QVector::contains` on `QPair`.
2. ⚠ **`swmm_node_add`'s node-type code.** The test passes `0` for
   JUNCTION. If the enum differs, the test's engine has no nodes and gate 6
   fails on `nodeCombo->count()`. Check `openswmm_nodes.h` and fix the
   test, not the dialog.
3. **BUILDING-state CRUD.** X5 §2.6 chose no lifecycle guard beyond the
   handle check, so the age API should work on a fresh `swmm_engine_new()`.
   If a call returns `SWMM_ERR_LIFECYCLE`, that decision needs revisiting
   (and it is an engine-side change, not a GUI one).
4. **`qFuzzyCompare(1.0 + a, 1.0 + b)`** is the Qt idiom for
   possibly-zero doubles; it makes 0.0 vs 1e-13 "equal". Deliberate — the
   alternative churns.
5. **`WA_DeleteOnClose` is false** so a caller may keep the instance and
   re-`readFromEngine()`. If the comprehensive-editor registry expects
   delete-on-close, adjust when Y3b wires the action.

## 4. Gates

`constructsWithNullEngine` · `hydratesGlobalsIncludingNegatives` ·
`writesEditedGlobalOnOk` · **`noOpOkWritesNothing`** ·
**`overridesAddUpdateAndRemove`** (update-in-place, then delete → engine
key gone) · **`overrideSourcesAreParserScoped`**.

## 5. Falsifier sweep

**Every row here has an observer — the first GUI round of the subplan for
which that is true.**

| # | Falsifier | Must fail |
|---|---|---|
| i | `readFromEngine` skips the globals loop | `hydratesGlobals…` |
| ii | clamp negatives to 0 on read (or set the spin minimum to 0) | `hydratesGlobals…` (the −2 h leg) |
| iii | drop the unchanged-value skip in `writeToEngine` | `noOpOkWritesNothing` |
| iv | drop the remove-then-set reconciliation (never remove) | `overridesAddUpdateAndRemove` (count stays 1 after delete) |
| v | `set_override` on every row unconditionally *appending* a duplicate key | `overridesAddUpdateAndRemove` (count becomes 2) |
| vi | populate the source combo from all seven pathways | `overrideSourcesAreParserScoped` |
| vii | leave the node combo empty (skip `swmm_node_count`) | `overrideSourcesAreParserScoped` (node-combo leg) |
| viii | `onAccept` sets `m_wroteAnyChanges = true` unconditionally | `noOpOkWritesNothing` |

## 6. Standing verification

Full GUI ctest — **and per Y2a's correction, do a FULL app build before
trusting any verdict**; the stale-partial-build ABI trap produced a phantom
"standing failure" in Y1's report. Every prior suite must stay green (this
round is purely additive; the only shared files are the two CMakeLists).

## 7. Not claimed / owed

**Y3b — reachability**: register the action, the Model-menu row, and the
comprehensive-editor entry so a user can open this (GUI plan §4). Small,
but it touches shared UI files, hence split. · The "Edit Source Ages…"
button on Y1's options page (same wiring round). · `swmm_water_age_save`
is unused here — the engine holds the edits and the project's save path
persists them; a "Save to component file…" affordance is a Y3b question. ·
Heat's equivalent editor (G4g, needs `openswmm_heat.h`).

**Re-scope carried from Y2a (please record, it changes a plan estimate):**
**Y2b is not one round.** The plot surface is a fixed `PlotAttribute` enum
(37 values) consumed by `attributesForKind`, the picker menus, the
comparison plot and the tabular/statistics views; D-G1 forbids extending it
with base+index, so species plotting needs the `ResultDescriptor`-through-
`IRunLayer` refactor. Suggested split: **Y2b-1** descriptor plumbing in
`IRunLayer`, **Y2b-2** pickers, **Y2b-3** tabular/statistics + `.oswp`
round-trip and D-G1's warn-on-miss.

## 8. On acceptance

Commit; update the subplan's Y3 row and GUI plan §7 G3 → partial (editor
landed, reachability owed); record the Y2b re-scope in the plan so the
estimate is not inherited wrong; then **Y3b** (reachability — small) and
**Y2b-1**.
---

## 9. Validation results (2026-08-23, validating agent)

**Committed `f5e0d9b`** on `dcc20e6`, branch `swmm6_gui`. Five files;
both CMakeLists as clean blobs again (the worktree copies still carry the
other sessions' comparison-plot-export and batch-transform registrations);
the clean-blob configuration was configured, built and run alone (8/8)
before restoring. All four §0 greps passed.

### One compile error — §3.1's exact prediction

`QCoreApplication::translate` needed `<QCoreApplication>` ("add it if the
compiler disagrees" — it did). One include added; everything else compiled
as authored, including the pre-fixed `swmm_node_add` 3-arg call (§3.2's
JUNCTION=0 verified against `openswmm_nodes.h` before building).

### Gates — the observer hole is closed, as the banner claimed

6/6 widget-driven cases pass: construct-with-null, hydrate (including the
−2 h D-NS1 leg), edited-global write, no-op OK writes NOTHING, override
add/update-in-place/remove (the stale-key half verified against the
engine), and the parser-scoped source combo (2 entries, GW/INITIAL_STATE
absent) with the node combo populated from the engine.

### Falsifier sweep — 8/8 bite

The first GUI round of the subplan where EVERY row has an automated
observer, exactly as designed (dependency-light dialog, the
ClimatologyDialog precedent): i hydration, ii negative-clamp, iii
unconditional write, iv stale keys survive, v duplicate key, vi
seven-pathway combo, vii empty node combo, viii unconditional
wroteAnyChanges — each on its predicted gate.

### Standing verification

Full app build BEFORE the verdict (Y2a's stale-binary rule) and full GUI
ctest: 218/218 (the suite grew by one target; zero failures; the wide rebuild cost came from the clean-blob reconfigure cycles, accepted). Clean-blob configure/build/test cycle green.

## 10. After this round

Y3b (reachability: action + Model menu + comprehensive-editor entry + the
options-page button + the save-to-component-file question) · Y2b-1..3 per
§7's re-scope, now recorded in the GUI plan · heat's editor (G4g).
