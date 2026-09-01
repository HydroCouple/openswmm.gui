# Workplan — Surface the #156 mixed-flow options in Simulation Options (issue #10)

**Status:** APPROVED for implementation — 2026-08-30.
**Engine counterpart:** openswmm.engine issue #156 (landed through Phase 5b; the option
surface below is verified engine-side across parse / InpWriter / C API / Python /
GeoPackage). **Commit convention:** `<summary> (#10)`.
**Build order:** the engine must be rebuilt AND installed to
`../openswmm.engine/install/<System>` before the GUI sees the new keys.

## The option surface to expose (exact engine semantics)

| Key | Values | Default | Engine notes |
|---|---|---|---|
| `SURCHARGE_METHOD` | EXTRAN\|SLOT\|DYNAMIC_SLOT\|**TPA** | EXTRAN | TPA is new (experimental, like DYNAMIC_SLOT). DW only. |
| `TPA_CELERITY` | double > 0 | 100 | Acoustic celerity a, PROJECT length units/s; meaningful only when SURCHARGE_METHOD == TPA. |
| `FV_PRESSURE_CLOSURE` | SLOT\|**TPA** | SLOT | FV only; accepted-and-inert under other routing (FV_* posture). |
| `UNSTEADY_FRICTION` | NONE\|VITKOVSKY | NONE | Consumed by BOTH DW and FV. |
| `UF_K3` | double 0–0.05 | 0.015 | Meaningful only when UNSTEADY_FRICTION != NONE. Paper range 0.005–0.020. |
| `REPORT_SIGNED_HEADS` | NO\|YES | NO | Output option (any routing): .out HEAD carries signed piezometric head (sub-atmospheric visible); DEPTH stays floored. |

`FV_TPA_FILTER` does **NOT** exist (implemented and reverted engine-side) — must not
appear anywhere in the GUI.

## Changes (the established 5-touchpoint pattern, `SimulationOptionsDialog`)

1. **Surcharge group** (`src/ui/dialogs/simulationoptionsdialog.cpp`):
   - `m_surchargeCombo`: add item "TPA (two-component pressure, experimental)" with
     itemData `"TPA"`.
   - New `m_tpaCeleritySpin` (double, range 1–5000, decimals 1, suffix in the dialog's
     unit-label convention if one exists; tooltip naming `TPA_CELERITY` and the
     w = g·A_full/a² role).
   - `updateSurchargeFieldsEnabled()`: DPS spins stay DYNAMIC_SLOT-only; the TPA
     celerity spin enabled only for TPA.
2. **FV group**: `m_fvPressureClosureCombo` (items SLOT, TPA; itemData strings; tooltip
   naming `FV_PRESSURE_CLOSURE` and "sub-atmospheric full-pipe flow") inside
   `m_fvGroup`, gated by the existing `updateFvFieldsEnabled()`.
3. **New "Unsteady friction" sub-group** on the Routing & Hydraulics page (applies to
   DYNWAVE and FV): `m_ufMethodCombo` (NONE, VITKOVSKY) + `m_ufK3Spin` (double,
   0–0.05, 3 decimals). k3 enabled only when method != NONE; whole group enabled for
   `FLOW_ROUTING ∈ {DYNWAVE, FV}`. Tooltip cites Pinto/Vasconcelos/Soares (2025).
4. **Output/reporting area**: `m_signedHeadsCheck` ("Report signed piezometric heads
   (sub-atmospheric)") wherever report-ish toggles live on this dialog; enabled for
   all routing models; tooltip: parity note (default keeps legacy bit-parity).
5. **Engine-capability gating** in `applyEngineConstraints()`: probe
   `getOption("FV_PRESSURE_CLOSURE")`, `getOption("UNSTEADY_FRICTION")`,
   `getOption("TPA_CELERITY")`, `getOption("REPORT_SIGNED_HEADS")` — disable the new
   controls (and hide the TPA combo item, per the dialog's existing pattern for absent
   enum values, if such a pattern exists — otherwise disable) with a "requires a newer
   engine" tooltip when a probe fails. Probe the surface, never parse the version.
6. **Read/write**: hydrate all six keys in the dialog's read path; write via the
   dialog's writeIfChanged convention. SURCHARGE_METHOD already round-trips — only the
   new enum value must survive (verify the combo lookup is by itemData, not index).
7. **Hydration contract** (`tests/gui/test_options_hydration_contract.cpp`): rows for
   the five NEW keys (defaults, `{key, written, read-back}`, bad-enum rejection where
   the engine returns BADPARAM: FV_PRESSURE_CLOSURE, SURCHARGE_METHOD, UNSTEADY_FRICTION
   via C API) plus the SURCHARGE_METHOD=TPA round-trip.
8. **Defaults seeding**: add the five new keys to `PreferencesManager::SimulationDefaults`,
   the Preferences page widgets, and `SWMMModelLayer::createBlankEngine()` seeding —
   FOLLOWING THE EXISTING PATTERN for keys that live there. (Known pre-existing gap:
   FV keys are absent from these — do NOT fix that gap here; add only the new keys if
   the pattern accommodates them cleanly, otherwise document which were skipped and why.)

## Acceptance

- Round-trip: set every new control → write → reopen → identical values (manual
  checklist + hydration contract test).
- Old engine (probe fails): new controls disabled with tooltip; no writes emitted.
- No behavior change for existing options; dialog builds warning-clean.
- Workplan updated with a RESULTS section (what changed, file list, test status,
  what could not be verified without a GUI build).

## Verification notes for the implementer

The GUI likely cannot be BUILT in the sandbox (Qt via vcpkg). Do everything statically
verifiable: match existing code patterns exactly (read neighboring options' plumbing
before writing yours), keep edits surgical, extend the hydration contract test, and
record an owner-side build/run checklist in the RESULTS section (engine install step
included). The engine CLI at `$HOME/work/build-engine/src/cli/openswmm` can verify the
option surface itself (write a deck with the keys, run, check the echo) if useful.

## RESULTS (2026-08-30)

Implemented in two passes: a first agent (cut off mid-task) landed the
SimulationOptionsDialog work plus the Preferences page-build; a second agent audited
every draft edit against the file's existing patterns, fixed the probe granularity,
and finished the Preferences data paths, blank-engine seeding, contract tests, and
this section. Nothing is committed.

### Files changed (uncommitted)

- `include/ui/dialogs/simulationoptionsdialog.h` — members: `m_tpaCeleritySpin`,
  `m_fvPressureClosureCombo`, UF group (`m_ufGroup`/`m_ufMethodCombo`/`m_ufK3Spin`/
  `m_ufSupported` flag), `m_signedHeadsCheck`.
- `src/ui/dialogs/simulationoptionsdialog.cpp` — TPA combo item + celerity spin,
  FV pressure-closure combo, Unsteady-friction group, signed-heads checkbox,
  four per-key capability probes in `applyEngineConstraints()`, read/write plumbing.
- `include/core/preferencesmanager.h` — `SimulationDefaults::unsteadyFriction` /
  `ufK3` (with a comment recording which #156 keys deliberately do NOT live here).
- `src/core/preferencesmanager.cpp` — QSettings read/write for the two new fields.
- `include/ui/dialogs/preferencesdialog.h` — `m_simUnsteadyFrictionCombo`,
  `m_simUfK3Spin`.
- `src/ui/dialogs/preferencesdialog.cpp` — DW-defaults page: TPA surcharge item,
  UF combo + k3 spin (k3 greyed unless method != NONE); hydrate / save / reset paths.
- `src/layers/swmmmodellayer.cpp` — `createBlankEngine()` seeds UNSTEADY_FRICTION +
  UF_K3, only when the preference departs from the engine default (see item 8 note).
- `tests/gui/test_options_hydration_contract.cpp` — two new cases (see below).

Also uncommitted in the tree but NOT part of this work: `tests/gui/data/
mesh_savedirty_clean.inp` / `mesh_savedirty_edited.inp` gained a `BACKEND AUTO`
row in [2D_OPTIONS] — pre-existing local edits from other work (BACKEND is not a
#156 key); left untouched, owner to confirm before committing.

### Per-item status

1. Surcharge group — DONE. TPA item ("TPA (two-component pressure, experimental)",
   itemData "TPA"); `m_tpaCeleritySpin` range 1–5000, 1 decimal, tooltip names
   TPA_CELERITY and w = g·A_full/a². No suffix: TPA_CELERITY is project-length-
   units/s, so it follows the FV_SLOT_CELERITY convention (units in tooltip), not
   the metre-fixed DPS_CELERITY " m/s" suffix. `updateSurchargeFieldsEnabled()`
   keeps DPS spins DYNAMIC_SLOT-only; celerity spin TPA-only.
2. FV group — DONE. `m_fvPressureClosureCombo` (SLOT/TPA itemData, tooltip names
   FV_PRESSURE_CLOSURE and sub-atmospheric full-pipe flow) inside `m_fvGroup`.
3. Unsteady-friction sub-group — DONE. Own QGroupBox on the Routing & Hydraulics
   tab; enabled for FLOW_ROUTING ∈ {DYNWAVE, FV} via `updateFvFieldsEnabled()`;
   k3 spin (0–0.05, 3 decimals) enabled only when method != NONE; tooltip cites
   Pinto, Vasconcelos & Soares (2025).
4. Output/reporting — DONE. `m_signedHeadsCheck` lives with the [REPORT] summary
   toggles but outside the RPT_DISABLED short-circuit (it shapes the .out, not the
   .rpt); tooltip carries the legacy bit-parity note.
5. Engine-capability gating — DONE, with one deliberate correction to the draft:
   the interrupted agent probed only UNSTEADY_FRICTION as a family sentinel; the
   plan (and the phase-wise engine landing) requires per-key probes, so
   `applyEngineConstraints()` now probes TPA_CELERITY (gates the TPA combo item —
   disabled via the QStandardItemModel pattern used for the FV routing item —
   plus the celerity spin), FV_PRESSURE_CLOSURE, UNSTEADY_FRICTION (carried
   through `m_ufSupported` so routing-combo changes cannot re-enable the group),
   and REPORT_SIGNED_HEADS, each with a "predates the mixed-flow option surface"
   tooltip (legacy-5.x text on legacy engines). No version parsing.
6. Read/write — DONE. Hydration: TPA_CELERITY via `optDouble` fallback 100.0
   (DPS_* rule); UNSTEADY_FRICTION/UF_K3 prefs-backed; FV_PRESSURE_CLOSURE
   fallback "SLOT"; REPORT_SIGNED_HEADS via the report-group `setBox`. Writes via
   the existing `writeIfChanged` conventions ('f',1 celerity; 'f',3 k3; combo
   itemData tokens; YES/NO). SURCHARGE_METHOD round-trips by itemData (verified:
   `selectComboByData` + `currentData()` on both paths), so TPA survives.
7. Hydration contract — DONE. `mixedFlowOptions_engineRoundTripsValues` (defaults
   + set→get for all five new keys + SURCHARGE_METHOD=TPA round-trip, both
   directions) and `mixedFlowOptions_rejectBadEnumTokens` (BADPARAM for
   SURCHARGE_METHOD/UNSTEADY_FRICTION/FV_PRESSURE_CLOSURE; rejected set leaves
   value untouched; REPORT_SIGNED_HEADS deliberately absent — lenient bool
   grammar, never rejects). Every row was proven against the built engine in the
   sandbox via a throwaway C++ ABI probe (all pass, incl. FV_TPA_FILTER absent
   from swmm_options_get).
8. Defaults seeding — DONE for the keys the pattern accommodates:
   UNSTEADY_FRICTION + UF_K3 added to `SimulationDefaults`, the Preferences DW
   page, and `createBlankEngine()`. Skipped, per the existing rules the struct
   comment now records: TPA_CELERITY (DPS_* rule — method-specific, engine-side
   default), FV_PRESSURE_CLOSURE (the known FV_* gap, per plan not fixed here),
   REPORT_SIGNED_HEADS (RPT_* rule — no report keys in SimulationDefaults).
   `createBlankEngine()` seeds only when the pref != NONE, because `set()`
   failures there are fatal to project creation and an unconditional seed would
   break blank projects on a pre-#156 6.x engine (the 2D block's warn-don't-fail
   escape hatch does not exist for [OPTIONS] keys).

`FV_TPA_FILTER` appears nowhere in the GUI (grepped; also proven absent from the
engine's option surface by the probe).

### Hydration-contract rows added

| Key | Default pinned | Round-trip (written → read-back) | Bad-enum |
|---|---|---|---|
| SURCHARGE_METHOD | EXTRAN | "TPA" → TPA; "EXTRAN" → EXTRAN | "PIPE" rejected, value kept |
| TPA_CELERITY | 100 | "250.0" → 250 | n/a (numeric) |
| FV_PRESSURE_CLOSURE | SLOT | "TPA" → TPA; "SLOT" → SLOT | "EXTRAN" rejected |
| UNSTEADY_FRICTION | NONE | "VITKOVSKY" → VITKOVSKY; "NONE" → NONE | "BRUNONE" rejected |
| UF_K3 | 0.015 | "0.010" → 0.01 | n/a (numeric) |
| REPORT_SIGNED_HEADS | NO | "YES" → YES; "NO" → NO | none — lenient bool, never rejects |

### Owner-side build & verify checklist

1. Rebuild the engine and INSTALL it to `../openswmm.engine/install/<System>` —
   the GUI links the installed copy and will not see the new keys otherwise.
2. Configure + build the GUI (Qt via vcpkg); confirm the build is warning-clean
   for the touched files.
3. Run `test_options_hydration_contract` — the two new mixedFlowOptions cases
   must pass (row-for-row identical to the sandbox ABI probe, so failures mean a
   stale installed engine).
4. Manual round-trip (new engine): Simulation Options → Routing & Hydraulics —
   set SURCHARGE_METHOD=TPA + celerity 250, FV closure TPA (with FLOW_ROUTING=FV),
   UF=VITKOVSKY + k3 0.010; Reporting — check signed heads. OK, save, reopen:
   identical values. Confirm the TPA celerity spin greys for non-TPA methods, the
   UF group greys under STEADY/KINWAVE, k3 greys at method NONE, and the FV
   closure combo follows the FV group.
5. Old-engine gate: open against a legacy 5.x (or a pre-#156 6.x) engine — the
   TPA combo item, celerity spin, FV closure combo, UF group, and signed-heads
   box are disabled with the "requires a newer engine" tooltips, and OK emits no
   writes for the new keys.
6. Preferences → Dynamic Wave defaults: set UF=Vitkovsky + k3, OK, reopen (values
   persist); File → New with those prefs on a new engine seeds UNSTEADY_FRICTION/
   UF_K3 into the blank project; Reset restores NONE/0.015.
7. Decide on the unrelated `BACKEND AUTO` fixture edits before committing.

### Not statically verifiable (needs the owner build)

- Qt layout/visual placement of the new group and rows; warning-cleanliness.
- The live enable/disable cascades (statically traced but not executed).
- The Preferences ↔ new-project seeding path end-to-end.
- Behavior of `writeIfChanged` on a probe-failed engine (the existing FV/DPS
  convention is an attempted set that fails harmlessly; acceptance's "no writes
  emitted" holds because the model never changes — flagged in case the owner
  prefers hard guards).
