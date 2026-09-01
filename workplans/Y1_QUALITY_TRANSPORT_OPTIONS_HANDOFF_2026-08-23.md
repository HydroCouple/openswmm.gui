# Y1 Validation Handoff — "Quality & Transport" Simulation-Options Page (GUI plan G1g)

**Date:** 2026-08-23 · **Repo:** `openswmm.gui` (the subplan's first GUI
round) · **Engine base:** `948b2840` (Y0) in `openswmm.engine` ·
**Plan:** `workplans/TRANSPORT_QUALITY_GUI_PLAN_2026-08-12.md` §1.1/§3.1/§7
G1; subplan `openswmm.engine/plans/transport/LARD_AGE_EXPEDITE_SUBPLAN_2026-08-23.md`
row Y1.

> **⚠ WEAKER PRE-VERIFICATION THAN THE ENGINE ROUNDS.** Those handoffs
> could say "every touched TU passes `-fsyntax-only`". **There is no Qt
> toolchain in the authoring sandbox, so this changeset has been
> code-reviewed only — never compiled.** Treat a compile error as expected
> traffic, not as a surprise. What *was* checked by hand: brace balance of
> the new function, every referenced member exists in the header, every
> lambda used (`selectComboByData`, `optInt`, `optDouble`) is in scope at
> the insertion point, and every Qt class used is already `#include`d in
> the TU.

---

## 0. Hunk-presence check

| grep (repo root) | expected |
|---|---|
| `grep -c "buildQualityTransportTab\|updateQualitySolverFieldsEnabled" src/ui/dialogs/simulationoptionsdialog.cpp` | **7** |
| `grep -c "m_qualitySolverCombo\|m_lardGroup\|m_ardGroup\|m_waterAgeBox\|m_heatTransportBox\|m_dispersionCombo\|m_rwptSeedSpin\|m_qualityStepSpin\|m_maxSegmentsSpin" include/ui/dialogs/simulationoptionsdialog.h` | **9** |
| `grep -c "QUALITY_SOLVER\|QUALITY_STEP\|MAX_SEGMENTS_PER_LINK\|DISPERSION\|RWPT_SEED\|WATER_AGE\|HEAT_TRANSPORT" src/ui/dialogs/simulationoptionsdialog.cpp` | **28** |
| `grep -c "transportOptions_" tests/gui/test_options_hydration_contract.cpp` | **4** |
| `grep -c "Quality & Transport" src/ui/dialogs/simulationoptionsdialog.cpp` | **5** |

## 1. Changeset

| File | Change |
|---|---|
| `include/ui/dialogs/simulationoptionsdialog.h` | `buildQualityTransportTab()` + `updateQualitySolverFieldsEnabled()` declarations; nine widget members (solver combo, ARD/LARD groups, step/segments/dispersion/seed, age + heat checkboxes) |
| `src/ui/dialogs/simulationoptionsdialog.cpp` | (a) `addCategory(tr("Quality & Transport"), …)` between Routing & Hydraulics and System / Performance — the plan's stated slot; (b) the page builder + gating helper (`updateFvFieldsEnabled` idiom); (c) hydration block in `readFromEngine()` with the ENGINE's defaults as fallbacks; (d) seven `writeIfChanged` rows in `writeToEngine()`; (e) capability gate in `applyEngineConstraints()` probing `QUALITY_SOLVER` |
| `tests/gui/test_options_hydration_contract.cpp` | two new cases: `transportOptions_engineRoundTripsValues` (defaults + set→get both directions), `transportOptions_rejectBadEnumTokens` (junk rejected, previous value survives, **canonical-token claim**: aliases go in but never come out, so a combo carrying `LARD` would never match on hydration) |

**No CMake change** — the new cases live in an existing target, and the
file deliberately stays engine-ABI-only (see §2.5).

## 2. Design decisions (challenge in this order)

1. **The page edits only keys the C API actually exposes** (Y0's seven).
   The GUI plan §1.1 also lists `ARD_SCALAR_SCHEME` / `ARD_DISPERSION` /
   `ARD_TARGET_DX` — **none of those exist in `swmm_options_get/set`**;
   the ARD engine reads them from its `transport.ard` component file
   (engine D-UT8). So the ARD group is a labelled pointer to that binding
   rather than duplicate widgets. **This is the Y0 trap one layer deeper,
   and it is recorded, not silently skipped** — §7 carries the owed round.
2. **`IGNORE_QUALITY` was NOT moved here**, though GUI plan §1.1 says it
   should be ("single home"). It is one of six sibling checkboxes in the
   Models page's "Active processes" group (rainfall / snowmelt /
   groundwater / RDII / quality / routing); pulling one out leaves five in
   one place and one elsewhere. **This deviates from a vetted plan
   (CLAUDE.md §5.0), so it is flagged loudly rather than quietly taken** —
   overturn it if you disagree; the move is a widget-construction
   relocation plus one `vlay->addWidget` line, and no test binds to the
   widget (checked).
3. **Every key is written regardless of the selected solver.** The engine
   accepts them under any solver (Y0 §2.1), so a user can configure LARD
   before switching to it and the settings survive an OK/reopen cycle.
4. **The capability probe is `QUALITY_SOLVER`, not the version string** —
   the FV block's own rule, for its own reason (string-keyed ABI; an
   engine built before Y0 is only detectable by asking). When unsupported
   the groups are disabled **directly** rather than through the gating
   helper, which would re-enable whichever group matched a stale
   selection.
5. **The hydration-contract file stays engine-ABI-only.** Including the
   dialog header there would pull AUTOMOC into a target that cannot
   satisfy the dialog's link closure — the trap this repo already records
   at `tests/gui/CMakeLists.txt:1996`. The `QUALITY_STEP` churn guard
   Y0 §3.1 warned about needs no new assertion: `optionValueEquals` is
   already numeric-aware and `test_simulationoptionsdialog`'s
   `optionValueEqualsNumericForms` **already pins `eq("0.000000","0.00")`**.
   Verified by reading, not assumed.

## 3. Anticipated failure modes, likelihood order

1. ⚠ **Compile errors** — see the banner. Most likely spots: a member
   type needing `class` forward-declaration in the header (I used
   `class QGroupBox` for the two groups, matching `m_fvGroup`), or a
   `tr()` string with an unescaped `&`.
2. ⚠ **`selectComboByData` scope.** It is a lambda local to
   `readFromEngine()`, defined near its top; my block sits after the FV
   block, well inside. If the compiler disagrees, the block moved — put it
   back inside the same function rather than hoisting the lambda.
3. **Combo data vs engine tokens.** The combos carry `LEGACY` /
   `EULERIAN_ARD` / `LAGRANGIAN` and `OFF` / `RWPT` — the canonical forms
   the getter emits. If hydration silently lands on index 0, the token
   drifted; the new `rejectBadEnumTokens` case is the observer.
4. **Page order / a11y.** A new sidebar row shifts every later category's
   index. If a test or a saved UI state pins category indices, it will
   move — that is a real finding, not a nuisance; report it.
5. **`QUALITY_STEP` spin range** is 0–3600 s. A deck with a larger value
   would clamp silently on hydration. Deliberate (a substep longer than an
   hour is a misconfiguration) — say so if you disagree.
6. **Translation files** may need regeneration for the new `tr()` strings;
   out of scope here, note it if the build warns.

## 4. Gates

`transportOptions_engineRoundTripsValues` · `transportOptions_rejectBadEnumTokens`
— plus the whole existing hydration/dialog suites, which must stay green
(the page is additive; nothing existing was edited except the three
insertion points).

## 5. Falsifier sweep

| # | Falsifier | Must fail |
|---|---|---|
| i | drop the `writeIfChanged` block | round-trip through the DIALOG (manual: set LARD, OK, reopen — it reads LEGACY). **No automated observer exists; see §6** |
| ii | give the solver combo item data `LARD` instead of `LAGRANGIAN` | `rejectBadEnumTokens`' canonical-token leg |
| iii | drop the `DISPERSION` enum rejection in the engine | `rejectBadEnumTokens` |
| iv | change a default fallback in `readFromEngine` (e.g. 100 → 64) | `engineRoundTripsValues`' defaults block (the resync tripwire) |
| v | remove the capability probe | **no observer — §6's owed gate** |
| vi | make `updateQualitySolverFieldsEnabled` enable both groups | **no observer — §6's owed gate** |

Rows v/vi are honestly empty: see §6.

## 6. ⛔ What has NO automated observer (and why)

**No test in this repo instantiates `SimulationOptionsDialog`** — checked
by grep. The dialog test links only the helpers TU precisely because the
full dialog's link closure is heavy (`CMakeLists.txt:1996`). Consequences,
stated plainly rather than papered over:

- the capability gate (§2.4) has no automated observer — **and neither
  does FV's, which this one mirrors**; the precedent is a hole, not a
  standard;
- the enable/disable gating has none;
- the read→write round trip through the widgets has none.

The hydration contract pins the *engine* half (the keys, their defaults,
their round-trip, their rejection), which is what it was built to do.
**Owed: a dialog-instantiating GUI harness** — one round, and it would
close the same hole for the FV page. Recommend scheduling it before Y3
(the Water Age Sources editor), which will have far more widget logic
worth observing. Until then, please **manually exercise** the page once:
open a model, switch to LARD, set QUALITY_STEP 5 / DISPERSION RWPT, OK,
reopen the dialog, confirm the values survived, and save-as to confirm the
`.inp` carries `QUALITY_SOLVER LAGRANGIAN`.

## 7. Not claimed / owed

ARD group's real controls (needs an engine round exposing `ARD_*` or a
component-file editor — the Y0 trap one layer deeper) · heat sub-options
(`[HEAT_OPTIONS]`, needs `openswmm_heat.h`, GUI plan prereq 5's other
half) · the Water Age Sources editor button (Y3) · `IGNORE_QUALITY`
relocation (§2.2) · PreferencesManager defaults + `createBlankEngine`
seeding (GUI plan §3.1 — the page falls back to the engine's own defaults
instead, which is arguably better and is what the tripwire in §5.iv pins) ·
2D transport group (GUI plan §1.2, needs the 2D keys) · docs/manual pages.

## 8. On acceptance

Commit; update the subplan's Y1 row and the GUI plan's §7 G1 status;
record the §2.2 deviation decision (upheld or overturned); schedule the
dialog-harness round from §6. Then **Y2** (result descriptors — engine
side already satisfied by the species-ID reader) and **Y3** (Water Age
Sources editor, unblocked by X5's C API).

---

## 9. Validation results (2026-08-23, validating agent)

**Committed `ebf28ae`** on `ab12c36`, branch `swmm6_gui`. Three files, all
hunks Y1's (audited vs HEAD). **The never-compiled changeset compiles
clean** — both test targets and the dialog TU in the app target; none of
§3.1's anticipated compile errors materialized.

### Gates

Both new hydration cases pass (17/17 in the contract suite, 17/17 in the
dialog suite). Full GUI ctest: **215/216** — the one failure,
`test_selectionbeacon::beaconSizeIsIndependentOfZoom` (ink 428/476/624
across zooms, deterministic 3/3), was RECLASSIFIED in the Y2a round — it passed 217/217 after the full app
rebuild with byte-identical sources, i.e. a STALE TEST BINARY (the
recorded partial-target-build ABI gotcha), not a standing failure. As
observed during Y1 it appeared to be a standing failure in unrelated
territory: the test target is self-contained and its three sources are
byte-identical to HEAD, so no uncommitted work (Y1's included) is in its
compile closure. Reported, not touched.

### Falsifier sweep — the honest observer matrix

| # | result |
|---|---|
| iii | **BITES** — the one automatable row, run CROSS-REPO: engine mutated, rebuilt, reinstalled; `rejectBadEnumTokens` fails on the exact FISCHER assertion; engine restored and re-verified green |
| i, v, vi | **EMPTY, as §6 declared** |
| ii, iv | **ALSO EMPTY — §5's table over-claimed these two rows.** Proven at the link level: neither test target compiles `simulationoptionsdialog.cpp` (the contract target's sources are the test + unitsystem; the dialog target deliberately links only the helpers TU), so a combo-data or fallback mutation cannot reach any assertion. The cited cases pin the CONTRACT those widgets depend on, not the widgets. §6's hole is therefore five rows wide, not three |

**The owed dialog-instantiating harness round is upgraded from
"recommended" to the only thing that can close five of six rows.**
Still scheduled before Y3, which §6 already argued.

### The §6 manual exercise — what was and wasn't done

The non-widget half ran end-to-end as an automated probe
(`test_artifacts/y1_2026-08-23/y1_saveas_probe.py`): the seven keys set
through the page's exact API path, `swmm_model_write` save-as, the .inp
carries all seven (`QUALITY_SOLVER LAGRANGIAN`, `QUALITY_STEP 0:00:05`,
`WATER_AGE ON` — the InpWriter's own renderings), and the saved file
reopens to canonical values. **The widget-click half (open dialog, set
LARD, OK, reopen) remains genuinely manual** — this validation cannot
click; it is the same five-row hole above, and the harness round closes
it. Until then that click-through is still owed to a human.

### §2.2 decision: UPHELD

`IGNORE_QUALITY` stays with its five siblings. Splitting one checkbox out
of the Models page's six-sibling "Active processes" group trades a
labelled inconsistency for a structural one. The GUI plan's §1.1 line
should be amended when that plan is next touched.

### Round gotchas worth keeping

Two recorded engine-side traps both fired during the cross-repo
falsifier: `cmake --install` served a STALE dylib (fresh timestamp,
un-mutated behavior — force-copy from the build tree was required), and
the reinstall invalidated signatures (exit 137 SIGKILL until re-signed).

## 10. After this round

Y2 (result descriptors — engine side already satisfied), the
dialog-harness round (before Y3), Y3 (Water Age Sources editor, C API
ready since X5). Owed from §7 unchanged.
