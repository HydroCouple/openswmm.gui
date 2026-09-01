# HANDOFF — Verify & compile: FV 1D / 2D options exposure (2026-08-11)

Instructions for the verifying agent. All edits are already made and uncommitted
in the two sibling checkouts. Your job: compile, run the test suites, do the
round-trip checks, and report pass/fail per section. Do NOT redesign anything;
if something fails, fix the smallest thing that makes the stated contract hold,
or report back.

## What changed (and why)

### openswmm.engine
| File | Change |
|---|---|
| `src/engine/2d/input/SectionHandlers2D.cpp` | `ADVECTION` added to `is2DOptionKey()` and `format2DOptionValue()` (returns `YES`/`NO`). Without this, `swmm_options_set_ext(e,"ADVECTION",…)` fell through to the generic `ext_options` map and leaked a stray `ADVECTION` line into `[OPTIONS]` on save; `get_ext` returned BADPARAM. This was the blocker for both GUI and Python exposure. |
| `src/engine/input/geopackage/GeoPackageWriter.cpp` | `2D_ADVECTION` row written with the other `[2D_OPTIONS]` keys. |
| `src/engine/input/geopackage/GeoPackageReader.cpp` | `2D_ADVECTION` restored into `SolverOptions2D::advection`. |
| `python/openswmm/engine/_solver.pyx` | Retired keys `FV_NODE_CELL_COUPLING` and `FV_JUNCTION_MODEL` removed from `SimulationOptions._KNOWN_KEYS` (they are accepted-and-ignored tombstones since engine commit `265eb727`; iterating them made `dict(solver.options)` round-trips re-write dead keys). |
| `python/tests/test_fv_routing_options.py` | Iterable-key set updated to the full 20-key FV vocabulary (adds `FV_LTS`, `FV_LTS_MAX_TIERS`, `FV_CFL_CENSUS_INTERVAL`, `FV_NODE_COUPLING`, `FV_NODE_DT`, `FV_NODE_PICARD`); round-trip test now sets/reads the six new keys. |
| `python/tests/test_2d_marcher_options.py` | `ADVECTION` added to defaults (`NO`), authored-INP parse, set round-trip, and invalid-value (`MAYBE` → raise) cases. |

Note: `ADVECTION` itself (struct field, INP parser, InpWriter emission) was
already in the user's uncommitted working tree — do not remove it.

### openswmm.gui
| File | Change |
|---|---|
| `include/ui/dialogs/simulationoptionsdialog.h` | New members: `m_fvNodeDtCombo`, `m_fvNodePicardSpin`, `m_advection2DBox`, `m_output2DFileEdit`. |
| `src/ui/dialogs/simulationoptionsdialog.cpp` | FV group: `FV_NODE_DT` combo (STABILITY/NONE) + `FV_NODE_PICARD` spin (1..100), hydrated in `readFromEngine()` (defaults STABILITY / 1), written in `writeToEngine()`; `updateFvFieldsEnabled()` gates the Picard spin on `FV_NODE_COUPLING == SEMI_IMPLICIT` (combo change reconnects the gate). 2D tab: `ADVECTION` checkbox in the marcher group; new "Output" group hosting `REPORT_2D` plus an `OUTPUT_FILE` line-edit with Browse… (empty = auto `<model>.2d.h5`; empty value legitimately clears the key — engine parser accepts an empty token and InpWriter omits unset). `RAINFALL_MODE` combo gains the `NONE` item (was silently rewriting NONE projects). Both hydrate in `read2DFromEngine()` / write in `write2DToEngine()`. |
| `include/core/preferencesmanager.h` | `TwoDDefaults::advection = false`. |
| `src/core/preferencesmanager.cpp` | `Advection` persisted in `twoDDefaults()` / `setTwoDDefaults()`. |
| `include/ui/dialogs/preferencesdialog.h` + `src/ui/dialogs/preferencesdialog.cpp` | `m_twoDAdvectionBox` on the 2D Defaults page, wired into `applyTwoDDefaultsToWidgets()` and the write path (reset path shares `applyTwoDDefaultsToWidgets`). Range fixes: `THETA` spin min 0.0 → 0.001 (engine requires (0,1]); `LTS_TIERS` spin 0..16 → 1..8 (engine rejects outside 1..8 — out-of-range prefs made File→New `set2d` fail). |
| `src/layers/swmmmodellayer.cpp` | `createBlankEngine` seeds `ADVECTION` from `TwoDDefaults`. |
| `tests/gui/test_options_hydration_contract.cpp` | FV defaults block pins `FV_NODE_DT=STABILITY`, `FV_NODE_PICARD=1`; round-trip rows add both; bad-enum case adds `FV_NODE_DT MAYBE`. |
| `tests/gui/test_twod_defaults_prefs.cpp` | `advection` pinned in compiled defaults (false) and the persist round-trip. |

Deliberately NOT added: `FV_NODE_CELL_COUPLING`, `FV_JUNCTION_MODEL` (retired
tombstones — junctions are unconditionally algebraic since `265eb727`).

## Build order (macOS host — the GUI links the sibling install prefix)

1. **Engine** (must come first; the GUI finds it at
   `../openswmm.engine/install/Darwin`, no version pin):
   ```sh
   cd openswmm.engine
   cmake --preset Darwin-tests
   cmake --build --preset Darwin-tests -j
   ctest --preset Darwin-tests --output-on-failure   # or the repo's usual test preset
   cmake --preset Darwin && cmake --build --preset Darwin -j && cmake --install build/Darwin
   ```
   (Use whatever build/install invocation the repo's CI or README prescribes if
   it differs — the essential contract is: tests pass, then the `install/Darwin`
   prefix is refreshed so the GUI links the new ABI.)

2. **Python bindings** (rebuild against the refreshed engine):
   ```sh
   cd openswmm.engine/python
   pip install -e . --force-reinstall --no-deps   # or: cmake --preset Darwin && cmake --build --preset Darwin
   python -m pytest tests/test_fv_routing_options.py tests/test_2d_marcher_options.py -v
   ```
   Also worth a smoke pass: `python -m pytest tests -x -q` if time allows.

3. **GUI** (reconfigure so the new engine exports are picked up):
   ```sh
   cd openswmm.gui
   cmake --preset Darwin
   cmake --build --preset Darwin -j
   ctest --test-dir build -R "options_hydration_contract|twod_defaults_prefs|2d_vfr_options_contract" --output-on-failure
   ```

## Targeted verification (beyond the suites)

### A. Engine ext-API contract (the original bug)
In a Python shell against a model with a 2D mesh open:
```python
s.options.ext["ADVECTION"]            # -> "NO" (not EngineError)
s.options.ext["ADVECTION"] = "YES"    # accepted
s.options.ext["ADVECTION"]            # -> "YES"
```
Then save the model and confirm the `.inp` has `ADVECTION YES` under
`[2D_OPTIONS]` and there is **no** `ADVECTION` line under `[OPTIONS]`.

### B. GeoPackage round-trip
Save a model with `advection=true` to `.gpkg`, reopen, confirm
`options.ext["ADVECTION"] == "YES"`. (Writer row `2D_ADVECTION`,
reader branch in `GeoPackageReader.cpp` `apply_2d_option`.)

### C. GUI manual round-trip (Transparent File IO — use a reviewable folder,
e.g. `openswmm.gui/test_artifacts/fv2d_options_check/`, not /tmp)
1. Open a 2D example (e.g. the bundled Bellinge 2D example). Simulation
   Options → Routing: pick FV; set Node time-step limit = None, Picard
   sweeps = 3. 2D tab: check Convective momentum flux, set 2D results file
   to `custom.2d.h5`, Rainfall mode = None. OK, save.
2. Inspect the saved `.inp`: `[OPTIONS]` has `FV_NODE_DT NONE` and
   `FV_NODE_PICARD 3` (both only written when non-default); `[2D_OPTIONS]`
   has `ADVECTION YES`, `OUTPUT_FILE custom.2d.h5`, `RAINFALL_MODE NONE`.
3. Reopen the project and the dialog — every control must hydrate to what
   was set (especially RAINFALL_MODE NONE, which previously fell back
   silently).
4. Clear the 2D results file field, save — `OUTPUT_FILE` line disappears
   and a run auto-defaults to `<model>.2d.h5`.
5. Preferences → 2D Defaults: toggle ADVECTION on, File → New with the 2D
   module enabled — the blank engine must carry `ADVECTION YES` (watch for
   `blank-engine 2D option refused` warnings in the log; there must be none,
   including for THETA/LTS_TIERS after the range fixes).
6. Node coupling combo → Explicit must grey out the Picard spin; back to
   Semi-implicit re-enables it. Non-FV routing greys both FV groups.

### D. Python retired-key hygiene
```python
keys = set(s.options)          # iterate
assert "FV_NODE_CELL_COUPLING" not in keys
assert "FV_JUNCTION_MODEL" not in keys
s.options["FV_JUNCTION_MODEL"]        # still returns "ALGEBRAIC" (tombstone get is fine)
```

## Acceptance
- All three engine/python/GUI test invocations green.
- A, B, C, D behave exactly as stated.
- `git diff` in both repos contains nothing beyond the files in the tables
  above (plus this handoff doc).
