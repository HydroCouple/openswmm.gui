# [Feature] Surface TPA closure + unsteady-friction options in Simulation Options

<!-- Paste title above as the GitHub issue title; body below. -->

## Summary

Companion to openswmm.engine issue: <engine-issue-URL> (TPA closure + unsteady friction).
Expose the three new engine option groups in `SimulationOptionsDialog` (Routing &
Hydraulics page), following the established 5-touchpoint pattern.

## Scope

- [ ] `m_surchargeCombo`: add **TPA** item (itemData `"TPA"`, marked experimental) +
      `TPA_CELERITY` spin; extend `updateSurchargeFieldsEnabled()` (DPS spins stay
      DYNAMIC_SLOT-only; TPA celerity spin TPA-only)
- [ ] FV group: `FV_PRESSURE_CLOSURE` combo (SLOT | TPA), gated by
      `updateFvFieldsEnabled()` on `FLOW_ROUTING == FV`
- [ ] New "Unsteady friction" sub-group: `UNSTEADY_FRICTION` combo (NONE | VITKOVSKY) +
      `UF_K3` double spin (0–0.05, 3 decimals); enabled for
      `FLOW_ROUTING ∈ {DYNWAVE, FV}`; k3 enabled only when method ≠ NONE
- [ ] Engine-capability gating in `applyEngineConstraints()` — probe
      `getOption("FV_PRESSURE_CLOSURE")` / `getOption("UNSTEADY_FRICTION")` /
      `getOption("TPA_CELERITY")` (probe the option surface, never parse the version)
- [ ] Hydration contract rows + bad-enum rejection in
      `tests/gui/test_options_hydration_contract.cpp`
- [ ] Defaults: `PreferencesManager::SimulationDefaults` + Preferences page +
      `SWMMModelLayer::createBlankEngine()` seeding for the new keys

## Acceptance criteria

- Round-trip: set in dialog → written to engine → re-open dialog shows identical values
- Old engine (without the keys): new controls disabled with explanatory tooltip; no writes
- Hydration contract test green; no behavior change for existing options

## Notes

- Build order: engine must be rebuilt AND installed to
  `../openswmm.engine/install/<System>` before the GUI sees the new keys.
- Known pre-existing gap (file separately, do not fix here): FV keys absent from
  `SimulationDefaults` and `createBlankEngine()` seeding.
- Workplan to author before implementation: `workplans/TPA_UF_OPTIONS_GUI_PLAN_<date>.md`
  (template: `HANDOFF_FV_2D_OPTIONS_VERIFICATION_2026-08-11.md`).
