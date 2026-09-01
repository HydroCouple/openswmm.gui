# Y2a Validation Handoff — Species as Themeable Result Attributes (GUI plan D-G1 / G5, first slice)

**Date:** 2026-08-23 · **Repo:** `openswmm.gui` · **Base:** `ebf28ae` (Y1) ·
**Engine:** any install carrying `swmm_output_get_pollut_id` (engine
`06580dd6`, well before this session) · **Plan:**
`TRANSPORT_QUALITY_GUI_PLAN_2026-08-12.md` §3.6 (D-G1) / §7 G5.

> **⚠ NEVER COMPILED** — no Qt toolchain in the authoring sandbox, same as
> Y1. What *was* verified mechanically: every function that uses the new
> `species` local also declares it (scripted check across the whole TU, not
> eyeballed), the sed-driven call-site rewrite left exactly the three
> intended self-calls untouched, and the diff is 60 lines.

**Y2 is split** (the E5a/E5b, X3a/X3b precedent): **Y2a = the model + map
theming**, which is the surface that makes water age and species *visible*.
**Y2b = plots / tabular / statistics / `.oswp` persistence round-trip**.

---

## 0. Hunk-presence check

| grep (repo root) | expected |
|---|---|
| `grep -c "OpenSWMMVis::Species\|speciesattributes.h" src/layers/swmmresultslayer.cpp` | **8** |
| `grep -c "speciesNames" src/layers/swmmresultslayer.cpp` | **8** |
| `grep -c "speciesNames" include/layers/swmmresultslayer.h` | **1** |
| `grep -c "OutCodeForAttribute(attr, species)\|OutCodeForAttribute(attribute, species)" src/layers/swmmresultslayer.cpp` | **21** |
| `grep -c "void Test" tests/gui/test_species_attributes.cpp` | **4** |
| `grep -c "speciesattributes" CMakeLists.txt` | **2** |
| `grep -c "speciesattributes" tests/gui/CMakeLists.txt` | **2** |

**Engine prerequisite:** the GUI must build against an engine install that
exports `swmm_output_get_pollut_id`. (The stale CPack artifact under
`packages/` does **not** have it — that is a packaging leftover, not the
build dependency, but if you see an undefined symbol, check which install
the build points at before suspecting the code. This is the Y0 trap's
shape again: verify the layer that will consume it.)

## 1. Changeset

| File | Change |
|---|---|
| `include/layers/speciesattributes.h` + `src/layers/speciesattributes.cpp` | **NEW.** Pure helpers: token build/parse (`qual:<name>`), reserved-species predicate, display label, unit label, and `speciesOutCode(attr, species, base)` |
| `include/layers/swmmresultslayer.h` | `speciesNames()` declaration |
| `src/layers/swmmresultslayer.cpp` | species-aware overloads of the three `*OutCodeForAttribute` mappers; `speciesNames()`; `availableAttributes()` appends one field per species for every node/link/subcatch category; **21 call sites** now pass the run's species list |
| `tests/gui/test_species_attributes.cpp` | **NEW** — 4 cases |
| `CMakeLists.txt`, `tests/gui/CMakeLists.txt` | registered |

## 2. Design decisions (challenge in this order)

1. **The persisted token is `"qual:<SpeciesName>"`, the resolved code is
   `POLLUT_BASE + index`.** D-G1 rejected "base+index encodings" — for the
   stated reason that indices would leak into `.oswp` and cap species
   count. Both concerns are honored: nothing persisted carries an index,
   and nothing is enumerated at compile time. The *transient* integer is
   the engine's own ABI convention (`SWMM_OUT_NODE_POLLUT_BASE = 6`, etc.),
   resolved fresh against the loaded run every time. **If review reads
   D-G1 as forbidding the transient code too, the alternative is the full
   `ResultDescriptor`-through-`IRunLayer` refactor** — much larger, and it
   would still resolve to these same ABI codes at the bottom.
2. **`speciesNames()` reads the reader live rather than caching.** The
   handle is reopened on reload; a stale cache would repoint every species
   theme by a slot — silently, and plausibly.
3. **No default argument on the species-aware mappers.** A defaulted empty
   list would compile everywhere and make species themes a silent no-op —
   the defect family this program keeps finding. Hence all 21 sites were
   updated explicitly.
4. **Reserved species get name-keyed labels and units** ("Water age
   (hours)", "Temperature (°C)", `h`, `°C`) — the consumer side of engine
   A2b's decision that the `.out` unit enum cannot express them, so the
   NAME is the discriminator.
5. **Unknown species → −1**, which every existing call site already skips.
   That converts "a saved theme names a species this run lacks" into "no
   theme" rather than "wrong column". The user-visible *warning* D-G1 asks
   for is **not** implemented — owed to Y2b (§7).
6. **Concentration unit is hardcoded `"mg/L"`** in `availableAttributes`
   for ordinary pollutants. The `.out` carries a per-species unit code the
   reader does not currently expose through this layer; the label is
   cosmetic and the reserved pair (the ones that would be *wrong*) are
   overridden. Recorded as owed rather than guessed at.

## 3. Anticipated failure modes, likelihood order

1. ⚠ **Compile errors** — see the banner. Most likely: the forward
   declarations I added ahead of the species-aware overloads not matching
   the later definitions' signatures, or `QStringList` needing an include
   in the results TU (it comes via `speciesattributes.h`).
2. ⚠ **`SWMM_OUT_*_POLLUT_BASE` spelling.** Taken from the current engine
   header (`SUBCATCH = 8`, `NODE = 6`, `LINK = 5`). If the install's header
   differs, the test will say so first — it asserts against the same
   symbols.
3. **Per-call `speciesNames()` cost.** It runs once per enclosing call, not
   per feature — but `restyleScene`/`populateScene` run per repaint. If a
   profile shows it, cache it against a handle-generation counter; do
   **not** cache it unconditionally (§2.2).
4. **`availableAttributes` is `const`** and now calls `speciesNames()`
   (also `const`) — fine, but it touches `m_handle`; if the handle can be
   mutated concurrently during a picker populate, that is a pre-existing
   concurrency question this round inherits rather than creates.
5. **Subcatchment species** are washoff *loads*, not concentrations, in
   SWMM's `.out` convention. The label says the species name and the unit
   says mg/L; if that reads wrong to you, it is a labelling fix in
   `speciesUnitLabel`, and worth doing before Y2b's tabular surface.

## 4. Gates

`attributeTokenRoundTrips` · `reservedSpeciesGetFriendlyLabelsAndUnits` ·
`outCodeResolvesAgainstTheRunsSpeciesList` (including **the persistence
claim**: reorder the run's species, the same saved token still finds its
column) · `outCodeRejectsUnknownAndMalformed`.

## 5. Falsifier sweep

| # | Falsifier | Must fail |
|---|---|---|
| i | make `speciesAttributeName` emit the index (`"qual:0"`) | `attributeTokenRoundTrips`, and the reorder leg of `outCodeResolves…` |
| ii | drop the empty-name guard so `"qual:"` parses | `attributeTokenRoundTrips`, `outCodeRejects…` (resolves to index 0) |
| iii | return the raw name from `speciesDisplayLabel` for reserved species | `reservedSpecies…` (`__` leaks into the picker) |
| iv | return `concentrationUnit` for the reserved pair | `reservedSpecies…` (water age labelled mg/L) |
| v | make `speciesOutCode` return `pollutBase` on an unknown name instead of −1 | `outCodeRejects…` (reads the first species instead of nothing) |
| vi | case-insensitive `indexOf` | `outCodeRejects…` (the `qual:tss` leg) |
| vii | give the species-aware mappers a defaulted empty list and revert the 21 call sites | **no unit observer** — species themes render nothing. This is §2.3's whole point; verify it manually (theme a node layer by a species and see it blank) and record |
| viii | `availableAttributes` appends species for `CatRainGages` too | **no observer** — pickers would offer a column the reader cannot serve; owed to a layer-level test (§6) |

## 6. ⛔ What has no automated observer

Same structural limit Y1 hit, and for the same recorded reason:
**`SWMMResultsLayer` cannot be linked into a test**. So the *wiring* —
`availableAttributes` actually appending, the 21 sites actually resolving,
the paint path actually reading the resolved column — is unobserved; only
the pure logic is gated. That is precisely why the logic was extracted into
its own TU rather than left inline.

**Please exercise manually once:** open a model with results that carry
pollutants and `WATER_AGE ON`, open the symbology/theming picker for a node
layer, confirm the species entries appear with friendly labels, theme by
"Water age (hours)", and confirm the map renders a plausible field that
animates with the time slider.

## 7. Not claimed / owed

Y2b: profile/time-series plots, tabular results, the statistics dashboard,
`.oswp` persistence round-trip **and the "warn on miss" message** D-G1 asks
for (§2.5) · per-species unit from the `.out` (§2.6) · 2D scalar fill
(GUI plan G6) · the `AttributePickerMenu` "Water Quality ▸" submenu
grouping (this round appends flat entries; grouping is presentation and
belongs with Y2b's picker work).

**Carried correction from Y1:** the "dialog-harness round" I recommended is
**not small** — `tests/gui/CMakeLists.txt:1996` records that instantiating
the dialog needs the `swmmvis_core` static-lib extraction (AUTOMOC →
OGR/GDAL cascade). Whoever schedules it should scope it as that refactor,
or as a policy-extraction round in the `simulationoptionshelpers.cpp`
idiom — which is exactly the pattern this round used for the species logic.

## 8. On acceptance

Commit; update the subplan's Y2 row (split recorded); GUI plan §7 G5
status → partial; then **Y2b**, and **Y3** (Water Age Sources editor,
C API ready since X5).

---

## 9. Validation results (2026-08-23, validating agent)

**Committed `dcc20e6`** on `ebf28ae`, branch `swmm6_gui`. Seven files; BOTH
CMakeLists went in as clean blobs (HEAD + only the speciesattributes
hunks — the worktree copies carry two other sessions' uncommitted test
registrations: comparison-plot export and batch-transform). The clean-blob
configuration was configured, built and run alone (6/6) before the
worktree was restored. All seven §0 greps passed. **The never-compiled
changeset compiled clean** — the test target, and the results-layer TU in
the app target; §3.1's anticipated errors did not materialize.

### Gates and the suite

4/4 new cases pass. Full GUI ctest **217/217** — including
`test_selectionbeacon`, whose Y1-round failure is hereby RECLASSIFIED:
it passed everywhere after the full app rebuild with byte-identical
sources, so it was a STALE TEST BINARY (the recorded partial-target-build
ABI gotcha), not a standing failure. Y1's §9 note is corrected beside
this.

### Falsifier sweep — 6/6 automatable rows bite

i (token round-trip + the reorder leg), ii (`"qual:"` → index 0 caught),
iii (`__` leaks), iv (age labelled mg/L), v (unknown → first species),
vi (case-insensitive) — each on exactly its predicted case. Rows vii/viii
are no-observer as §5 declared (the same SWMMResultsLayer link limit §6
names); the extracted-TU design is what made the other six automatable
at all.

### The premise, verified on a REAL run

`test_artifacts/y2a_2026-08-23/premise_probe.py`: a LARD + WATER_AGE run
through the installed engine yields `pollut_count=3,
ids=[TSS, Lead, __WATER_AGE__]` — names discriminate, pollutants first,
reserved trailing — the exact contract `speciesNames()` and the trailing-
reserved assumption in the tests rely on.

### Still owed to a human (§6)

The wiring walk-through: open a model with quality results, confirm the
species entries appear in the theming picker with friendly labels, theme
by "Water age (hours)", confirm the field animates. Same closure limit as
Y1's dialog; the §7-carried correction stands — the harness/extraction
round is the structural fix and this round's TU-extraction is its idiom.

## 10. After this round

Y2b (plots/tabular/statistics/.oswp round-trip + the warn-on-miss
message + per-species units) · Y3 (Water Age Sources editor). Owed list
§7 unchanged.
