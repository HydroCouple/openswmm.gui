# Multi-Engine-Version Support Plan (SWMMVis)

**Date:** 2026-08-01
**Status:** DRAFT — for review
**Repos:** `openswmm.gui` (primary), `openswmm.engine` (v5.2.4 branch backports)

> **⚠️ AMENDED 2026-08-16 — Phase 1 step 0 split out and extended.** The build-configuration
> work in §5 step 0 now has its own plan,
> `openswmm.engine/plans/ENGINE_524_BUILD_MODERNIZATION_PLAN_2026-08-16.md`, because it is also
> the prerequisite for the benchmark CI effort
> (`openswmm.engine/plans/BENCHMARK_REGRESSION_CI_PLAN_2026-08-16.md`), which uses 5.2.4 as a
> comparable performance/results baseline against 5.3.0 and v6. Three amendments to this plan
> (pending acceptance):
> 1. **GoogleTest replaces Boost.Test.** §5 step 0's `tests` feature maps to vcpkg `gtest`, not
>    `boost-test`, and `tests/outfile` is ported — aligning the branch with the refactored
>    engine's harness rather than preserving upstream's Boost dependency.
> 2. **FP policy aligned to the main repo** (`/fp:precise`, `-ffp-contract=off`), replacing
>    upstream's `/fp:fast`. The §5 "bit-identical numerics" gate is preserved in the sense that
>    matters — isolating *backport* effects — because `verify_524_parity.sh` builds stock and
>    modified with identical flags. Two consequences: (a) this branch's results differ in the
>    last bits from EPA's shipped `/fp:fast` binary, which is intended and is what makes the
>    benchmark comparison fair; (b) Phase 2 compiles `src/solver/*.c` with GUI-side flags via
>    `FetchContent_Populate`, so **the shipped worker's FP policy is not governed by the branch
>    presets** — if the GUI wants its 5.2.4 worker to reproduce benchmark numbers,
>    `add_legacy_engine_worker()` must apply the same FP flags. Decision needed from this plan's
>    owner.
> 3. **Four CI legs, not three** — macOS split arm64 / x86_64, matching the main repo's matrix
>    and the benchmark sweep's platform coverage.

## 1. Goal

Let SWMMVis run models against multiple SWMM engine versions, selectable in the UI:

- **v5.2.4** — last canonical EPA release. Exists as branch `hydrocouple/build-v5.2.4` (tag `v5.2.4`) in `openswmm.engine`. Must be modified to (a) skip unknown sections/options with warnings, (b) expose the warning callback, (c) expose running mass-balance, (d) ship a subprocess worker — all as already implemented in 5.3.0.
- **v5.3.0** — current legacy engine in `src/legacy/engine/` (final 5.3.x release). Already integrated via subprocess worker.
- **v6.x** — current C++20 engine, runs in-process. Owns all .inp/.out I/O for every 5.x version (5.x engines only *execute*; the GUI reads/writes models through the v6 API).
- **Future versions** — addable with no GUI code changes (manifest-driven).

## 2. Decisions (from 2026-08-01 review)

| Decision | Choice |
|---|---|
| v5.2.4 source integration | **CMake FetchContent** (pinned commit), *not* a git submodule |
| Build/packaging ownership | **GUI repo** builds the 5.2.4 worker; engine repo hosts the branch source |
| Version discovery in UI | **Runtime manifest** (`engines.json`) shipped beside the workers |
| 5.2.4 backport scope | All four items (§1), **numerics must stay bit-identical to EPA 5.2.4** |

### 2.1 Why not a submodule

The v5.2.4 code is a branch *of `openswmm.engine` itself*. A submodule from the engine repo would be self-referential (repo containing itself), which confuses tooling and vendors the full history. From the GUI repo a submodule would work, but FetchContent gives the same pinning with less ceremony and scales to future versions by adding a declaration, not a `.gitmodules` entry.

### 2.2 Risks of FetchContent (accepted, with mitigations)

1. **Pin visibility.** The pinned commit lives in CMake, not git metadata. *Mitigation:* pin an exact SHA (never a branch name) in one dedicated file, `cmake/EngineVersions.cmake`, so diffs to the pin are obvious in review.
2. **Shallow-clone of an arbitrary SHA** may fail on servers without `uploadpack.allowReachableSHA1InWant`. *Mitigation:* tag the pinned commit on the branch (e.g. `v5.2.4-swmmvis.1`) and fetch the tag with `GIT_SHALLOW TRUE`.
3. **Network at first configure.** Offline/CI builds need the FetchContent source cache. *Mitigation:* the local convention is a **sibling clone** (`../openswmm.engine.v524`, see §4.1) that FetchContent uses as its source dir when present; git fetch of the tag is only the fallback for fresh machines/CI (where `_deps/` is cached).
4. **Foreign build system.** The branch has the upstream OWA layout (`src/solver`, its own CMake) whose targets/options may collide with the GUI build (`swmm5`, generic option names, install rules). *Mitigation:* do **not** `add_subdirectory` the fetched tree's CMake. Use `FetchContent_Populate` (source only) and compile the solver sources directly into our own worker target (§5.2). This also sidesteps upstream install-rule pollution.
5. **Full repo weight.** The fetched repo is `openswmm.engine` itself. *Mitigation:* `GIT_SHALLOW` on the tag keeps it to one tree.

### 2.3 Risks of GUI ownership (accepted, with mitigations)

1. **Engine build logic in two places.** The 5.3.0 worker is built by the engine repo; the 5.2.4 worker by the GUI. *Mitigation:* the *source* of all engine code (including the 5.2.4 backports and its worker `main.cpp`) stays in `openswmm.engine` (on the branch); the GUI only compiles and packages. One `cmake/LegacyEngineWorker.cmake` function keeps the recipe reusable for future 5.x versions.
2. **GUI build time/toolchain burden** grows (compiling a C solver). *Mitigation:* it is ~80 C files, seconds of build time; gate behind `SWMMVIS_ENABLE_ENGINE_524` (default ON) so it can be switched off.
3. **Drift risk:** manifest generated by the GUI must agree with what the engine repo installs for 5.3.0/6.x. *Mitigation:* the GUI generates the *entire* manifest at build time (§6.1) from what it actually bundles — single writer, no merging.

## 3. Current state (verified 2026-08-01)

### Engine repo (`openswmm.engine`, branch `swmm6_rel`)
- `src/legacy/engine/` — 5.3.0 solver; version from root `CMakeLists.txt:42-45` → `legacy_version.h`.
- `src/legacy/worker/{main.cpp,worker_progress.h}` — subprocess worker; JSON-lines protocol on stdout: `dates`, `progress`, `warning`, `error`, `continuity`. Exists because the 5.x solver is process-global (`globals.h:174-179`) and not thread safe.
- 5.3.0-only additions (not in stock EPA 5.2.4):
  - Unknown-section skip + warn: `src/legacy/engine/input.c:113-123`, `input.c:225-235`.
  - Unknown-option skip + warn: `src/legacy/engine/project.c:471-480`.
  - Warning callback: `swmm_setWarningCallback` (`swmm5.c:1191-1195`), invoked via `report_invokeWarningCallback` / `report_writeWarningMsg` (`report.c:1485-1517`).
  - Running mass balance: `swmm_getRunningMassBalErr` (`openswmm_solver.h:941`).
- **Grammar parity:** `keywords.c`, `SectWords[]`, `OptionWords[]`, `enums.h` are *identical* between tag `v5.2.4` and 5.3.0. The "new sections" a 5.x engine must skip are the **v6-only sections** the GUI's writer emits: `[USER_FLAGS]`, `[USER_FLAG_VALUES]`, `[PLUGINS]`, `[2D_OPTIONS]`, `[2D_MESH_FILE]` (see `src/engine/core/InpWriter.hpp`, `DefaultInputPlugin.cpp:52-137`).
- v6 I/O layer (reads/writes for all 5.x): `src/engine/input/InputReader.*` + `SectionRegistry`, `src/engine/core/InpWriter.*`, `src/engine/output/OutputReader.*`.

### GUI repo (`openswmm.gui`, branch `swmm6_gui`)
- Engine consumed as prebuilt package: `cmake/FetchOpenSWMMEngine.cmake` → `find_package(OpenSWMMEngine)`, prefix `../openswmm.engine/install/<OS>`.
- Version selection surface (all to be generalized):
  - Status-bar combo, two hardcoded items: `src/swmmvis.cpp:1577-1595` (`mComboBoxEngineVersion`).
  - Prefs default-engine combo: `src/ui/dialogs/preferencesdialog.cpp:141-151`; QSettings key `SWMMVis/Preferences/Defaults/EngineMode` (`preferencesmanager.cpp:1365-1380`).
  - Dispatch: `src/simulation/simulationrunner.cpp:242` — `useLegacy = engineVersion.startsWith("5.")`.
  - Worker discovery: `simulationrunner.cpp:70-113` `findLegacyWorker()` — single hardcoded name `openswmm-legacy-worker[.exe]`.
  - Capability gating: `src/ui/dialogs/simulationoptionsdialog.cpp:168+` `applyEngineConstraints()` — keys off `startsWith("5.")`.
  - Persistence: `src/project/projectserializer.cpp` key `engineVersion` (write :292, read :650, :1159); per-window `SWMMVisProjectWindow::mEngineVersion`.
  - Job rows already keyed by `(projectWindow, engineVersion)`: `simulationstatusmodel.cpp:52-96`.
- Worker bundling: GUI `CMakeLists.txt:1606-1640` (macOS), `:1796-1820` (Windows), `:1915-1935` (Linux).

## 4. Target architecture

```
openswmm.engine repo
├── swmm6_rel ................. v6 engine + v5.3.0 legacy + 5.3.0 worker (unchanged flow)
└── build-v5.2.4 branch ....... EPA 5.2.4 + backports (warnings, callback,
                                running massbal, worker main.cpp)
                                tagged v5.2.4-swmmvis.N

openswmm.gui repo
├── cmake/EngineVersions.cmake  # pins: one FetchContent decl per 5.x version
├── cmake/LegacyEngineWorker.cmake  # add_legacy_engine_worker(VERSION TAG SOURCES...)
└── build → bundles:
    bin/openswmm-legacy-worker-5.3.0   (built by GUI via add_legacy_engine_worker)
    bin/openswmm-legacy-worker-5.2.4   (built by GUI via add_legacy_engine_worker)
    engines.json                       (generated manifest)

Runtime (GUI)
EngineRegistry (model) ── reads engines.json
    ├── status-bar combo (view)
    ├── prefs default-engine combo (view)
    ├── SimulationRunner dispatch: entry.runMode == subprocess → entry.workerPath
    └── SimulationOptionsDialog gating: entry.capabilities
```

### 4.1 Local checkout layout (sibling-folder convention)

```
cbuahin_github/
├── openswmm.engine/         # swmm6_rel — v6 + 5.3.0 legacy (unchanged)
├── openswmm.engine.v524/    # clone of the same repo, checked out to build-v5.2.4
└── openswmm.gui/            # consumes both: find_package prefix + FetchContent source
```

The v5.2.4 work lives in its own sibling clone so the two engine trees never share a working directory, build folders, or IDE state. `openswmm.gui`'s FetchContent uses this sibling as its source dir whenever it exists (§6.1); the pinned tag fetch is only the fallback. Pushes of the branch/tags go to the same remote as `openswmm.engine` — the sibling clone is a checkout convenience, not a fork.

Principles:
- **One canonical I/O layer.** The GUI always loads/edits/writes models through the v6 engine API regardless of the selected run engine. 5.x engines are *executors only*, fed a written .inp via their worker.
- **Belt and suspenders on v6-only sections.** For 5.x runs the v6 writer emits a *compat* .inp omitting v6-only sections (Phase 4); the 5.x engines' skip+warn logic remains as the safety net for hand-edited or foreign files. Compat output is a **run artifact only** — the user's saved model file always keeps full v6 fidelity (no silent data loss when a 5.x engine is selected).
- **One worker protocol.** Every 5.x worker speaks the existing `worker_progress.h` JSON-lines protocol. Add `{"type":"hello","engineVersion":"5.2.4","protocol":1}` as the first line so the GUI can verify it launched what the manifest promised.
- **One process per 5.x run.** Process isolation is the thread-safety strategy; nothing in-process links a 5.x solver.

## 5. Phase 1 — v5.2.4 branch backports (engine repo)

Work happens on `build-v5.2.4` in the sibling clone (§4.1):

```bash
cd cbuahin_github
git clone --branch build-v5.2.4 https://github.com/HydroCouple/openswmm.engine openswmm.engine.v524
```

**Hard constraint: bit-identical numerics vs stock EPA 5.2.4.** Backports may touch only reporting/IO/API surface — `input.c`/`project.c` unknown-handling (which by definition only executes on input stock 5.2.4 would reject), `report.c` warning plumbing, new exported functions, and additive worker files. No solver-path edits.

Steps (upstream layout: sources in `src/solver/`):
0. **Adopt the 5.3.x/6.0.0 vcpkg + presets build configuration.** The branch currently carries the upstream `swmm-solver` CMake (3.13, no manifest; test Boost fetched by `extern/boost.cmake`). Align it with the main repo's workflow:
   - Add a trimmed `vcpkg.json` manifest (`version-semver` tracking the `v5.2.4-swmmvis.N` tag): host tools `vcpkg-cmake`/`vcpkg-cmake-config`, plus a `tests` feature mapping upstream's Boost test dependency to vcpkg `boost-test` (retiring `extern/boost.cmake`). None of the main repo's heavy deps (hdf5/kokkos/sqlite3) apply — do not carry them or the `vcpkg-overlays/` (kokkos-only). OpenMP stays a system dependency, as in the main repo.
   - Add `CMakePresets.json` mirroring the main repo's preset names and vcpkg toolchain wiring (`Darwin`/`Linux`/`Windows`, `-debug`, `-tests`) so both engine trees build with identical `cmake --preset <OS>` invocations locally and in CI.
   - Update the branch's `.github/workflows/build-and-test.yml` to the preset/vcpkg flow.
   - Scope note: this governs standalone branch builds, the parity gate (below), and CI. Phase 2's populate-only compile in the GUI does not run the branch's CMake or vcpkg, so it is unaffected.
1. Backport unknown-section skip+warn into `input.c` (`countObjects` + `readData`), mirroring 5.3.0 `input.c:113-123`, `:225-235`.
2. Backport unknown-option skip+warn into `project.c` (`readOption`), mirroring 5.3.0 `project.c:471-480`.
3. Backport `swmm_setWarningCallback` (+ `WarningCallback`/`WarningCallbackData` globals) and `report_invokeWarningCallback`, wiring `report_writeWarningMsg` through it, mirroring `swmm5.c:1191-1195`, `report.c:1485-1517`, `globals.h:174-179`. Export in `swmm5.h`.
4. Backport `swmm_getRunningMassBalErr` (from 5.3.0 `massbal`/`swmm5.c` implementation). Export in `swmm5.h`.
5. Add `src/worker/{main.cpp,worker_progress.h}` — copy of the 5.3.0 worker adapted to `swmm5.h` includes, plus the `hello` line (also add `hello` to the 5.3.0 worker for symmetry).
6. Tag `v5.2.4-swmmvis.1`, push branch + tag.

**Verify (regression gate, bit-identical):**
- Preset builds succeed on all three OS presets (CI matrix on the branch).
- Build stock EPA at tag `v5.2.4` and the modified branch; run both CLIs over the engine repo's `examples/` .inp set (filtered to 5.x-grammar files); assert `.out` files byte-identical and `.rpt` identical modulo banner/timestamps. Script lives at `tools/verify_524_parity.sh` on the branch; outputs to a reviewable folder (not temp), e.g. `verification/524_parity/`.
- Feed a v6-flavored .inp (with `[USER_FLAGS]`, `[PLUGINS]`, `[2D_OPTIONS]`) — assert run completes with the expected `WARNING: Unknown section` lines and the callback fires.

## 6. Phase 2 — GUI builds & packages the 5.2.4 worker

1. `cmake/EngineVersions.cmake`:
   ```cmake
   # Local convention: use the sibling clone when present (§4.1).
   set(_engine524_sibling "${CMAKE_SOURCE_DIR}/../openswmm.engine.v524")
   if(EXISTS "${_engine524_sibling}/src/solver"
      AND NOT DEFINED FETCHCONTENT_SOURCE_DIR_OPENSWMM_ENGINE_524)
     set(FETCHCONTENT_SOURCE_DIR_OPENSWMM_ENGINE_524 "${_engine524_sibling}")
   endif()

   FetchContent_Declare(openswmm_engine_524
     GIT_REPOSITORY https://github.com/HydroCouple/openswmm.engine
     GIT_TAG        v5.2.4-swmmvis.1   # exact tag; SHA recorded in comment
     GIT_SHALLOW    TRUE)
   ```
   The declared URL is always the public https GitHub URL so CI/CD works with no local setup. The sibling-clone override (above) is a local-dev convenience only and never applies on CI. Configure logs which source was used; release builds assert they built from the pinned tag.
2. `cmake/LegacyEngineWorker.cmake` — `add_legacy_engine_worker(VERSION 5.2.4 SOURCE_DIR ...)`:
   - `FetchContent_Populate` only (no `add_subdirectory`).
   - Compile `src/solver/*.c` + `src/worker/main.cpp` **statically** into `openswmm-legacy-worker-5.2.4` (no shared lib → no dylib name collisions with `libopenswmm.legacy.engine`).
   - Reuse the function later for any future 5.x version.
3. **Symmetry: the 5.3.0 worker is built the same way.** A second `add_legacy_engine_worker(VERSION 5.3.0 ...)` call compiles `src/legacy/engine/*.c` + `src/legacy/worker/main.cpp` statically into `openswmm-legacy-worker-5.3.0`. Source resolution mirrors 5.2.4: sibling `../openswmm.engine` when present, else FetchContent of the pinned release tag from `https://github.com/HydroCouple/openswmm.engine`. (The 5.3.0 sources need the generated `legacy_version.h`; `add_legacy_engine_worker` configures it from `legacy_version.h.in` — for 5.2.4's upstream layout this step is a no-op.) The engine install prefix then no longer needs to ship a worker; the GUI's per-OS bundle blocks (`CMakeLists.txt:1606/1796/1915`) switch from copying `${OPENSWMMENGINE_INSTALL_DIR}/bin/openswmm-legacy-worker` to installing the two locally built versioned workers. The unversioned worker name disappears entirely — `findLegacyWorker()` searches only manifest-declared names.
4. Option `SWMMVIS_ENABLE_ENGINE_524` (default ON).

**Verify:** clean configure+build on macOS preset; `bin/` contains both workers; run `openswmm-legacy-worker-5.2.4 model.inp m.rpt m.out` by hand and confirm `hello`/`dates`/`progress`/`continuity` JSON lines.

## 7. Phase 3 — manifest + GUI version registry

### 7.1 Manifest (`engines.json`, generated at build time, installed beside the executable)

```json
{
  "schemaVersion": 1,
  "engines": [
    { "id": "6.0.0", "family": "engine6", "label": "OpenSWMM 6.0.0-alpha.3",
      "runMode": "inprocess",
      "capabilities": { "plugins": true, "dynamicSlot": true, "semiImplicit": true,
                        "twoD": true, "pauseResume": true, "runningContinuity": true } },
    { "id": "5.3.0", "family": "legacy5", "label": "SWMM 5.3.0 (Legacy)",
      "runMode": "subprocess", "worker": "openswmm-legacy-worker-5.3.0",
      "capabilities": { "plugins": false, "dynamicSlot": false, "semiImplicit": false,
                        "twoD": false, "pauseResume": false, "runningContinuity": true } },
    { "id": "5.2.4", "family": "legacy5", "label": "SWMM 5.2.4 (EPA canonical)",
      "runMode": "subprocess", "worker": "openswmm-legacy-worker-5.2.4",
      "capabilities": { "plugins": false, "dynamicSlot": false, "semiImplicit": false,
                        "twoD": false, "pauseResume": false, "runningContinuity": true } }
  ]
}
```

`capabilities` drives UI gating; unknown capability keys are ignored (forward compatible). The v6 entry's label/version comes from the engine package's version headers at generation time.

### 7.2 GUI changes (MVC per CLAUDE.md §5.1)

| Change | Where |
|---|---|
| New `EngineRegistry` (singleton model): parses `engines.json` from app dir; exposes entries, `find(id)`, fallback to the two built-in entries if manifest missing | `include/core/engineregistry.h`, `src/core/engineregistry.cpp` |
| Status-bar combo populated from registry (replace two `addItem`s); userData = engine `id` | `src/swmmvis.cpp:1577-1595` |
| Prefs default-engine combo populated from registry | `src/ui/dialogs/preferencesdialog.cpp:141-151` |
| Dispatch on `entry.runMode` instead of `startsWith("5.")`; worker path from `entry.worker` (keep `findLegacyWorker()` search dirs, parameterized by name; keep unversioned-name fallback) | `src/simulation/simulationrunner.cpp:242`, `:70-113` |
| Gating from `entry.capabilities` instead of `startsWith("5.")` | `src/ui/dialogs/simulationoptionsdialog.cpp:168+` |
| Persistence: keep the `engineVersion` string key (ids are version strings, so existing project files stay valid). On load, if id not in registry → warn + fall back to default engine | `src/project/projectserializer.cpp` |
| Verify worker `hello` line matches requested id; surface mismatch as job warning | `simulationrunner.cpp` handleLine |

No changes needed to `SimulationStatusModel` (already version-keyed) or the results path (all 5.x/6.x runs produce standard `.out` read by the v6 reader; `swmm_output_get_version()` is available if discrimination is ever needed).

**Verify:**
- Unit tests for `EngineRegistry` (parse, missing file, unknown keys, bad worker path).
- Manual matrix: same model run under 6.x / 5.3.0 / 5.2.4 → three job rows, correct progress/warnings/continuity for each; options dialog gating follows the selected engine; project save/reload restores selection; project referencing a removed engine id degrades gracefully.

## 8. Phase 4 — engine-targeted compat write (v6 InpWriter)

Make the v6 writer able to emit a 5.x-compatible .inp on request.

Engine repo (`swmm6_rel`):
1. Add a write-profile option to `openswmm::inp_writer::writeInpFile` (`src/engine/core/InpWriter.{hpp,cpp}`), e.g. `InpWriteOptions{ compat: Full | Swmm5 }`. `Swmm5` omits the v6-only sections: `[USER_FLAGS]`, `[USER_FLAG_VALUES]`, `[PLUGINS]`, `[2D_OPTIONS]`, `[2D_MESH_FILE]` (keep the list in one place next to the writer's section-order table so future v6-only sections join it by construction).

   **`[OPTIONS]`-level handling** (verified against `InpWriter.cpp:549-660`; the section mixes legacy and v6-only keys, and one is *fatal* to 5.x, not merely unknown):
   - **Omit v6-only keywords:** `IGNORE_2D` (`:590`), `DPS_CELERITY`/`DPS_ALPHA`/`DPS_DECAY_TIME` (`:645-649`), `NODE_CONTINUITY` (`:650`), `ANDERSON_ACCEL` (`:652`), `CRS` (`:654`), `WRITE_ABSOLUTE_PATHS` (`:656`).
   - **Map incompatible values:** `SURCHARGE_METHOD DYNAMIC_SLOT` (`:632`) — the *keyword* exists in 5.x but the *value* doesn't; 5.x `project_readOption` raises fatal `ERR_KEYWORD` for it (skip+warn doesn't apply to bad values of known options). Compat profile writes `SLOT` instead and the writer reports the substitution so the GUI can surface it as a run warning.
   - **Filter `ext_options` passthrough** (`:658`) against the legacy `OptionWords[]` list — drop keys 5.x doesn't know.
   - Maintain the omit/map tables adjacent to the option-writing code so new v6 options must declare their compat behavior when added.
2. Expose through the C API the GUI already uses (`swmm_model_write_with_plugin` gains a profile argument or a sibling `swmm_model_write_compat(engine, path, profile)`), preserving the existing signature's behavior (= Full).
3. Emit a header comment in compat output (`;; written by SWMMVis for SWMM 5.x — v6-only sections omitted`) so provenance of run artifacts is obvious.

GUI repo:
4. In the run orchestration (`SWMMVis::onRunSimulation()`, `src/swmmvis.cpp:6540`): when the selected engine's `family == "legacy5"`, write a compat .inp to the resolved run location as the worker input, leaving the project's canonical .inp untouched. Manifest gains a per-engine `"writeProfile": "swmm5" | "full"` so future engines choose their profile without GUI changes.

**Verify:** round-trip test — model with user flags/plugins/2D *and* DYNAMIC_SLOT + v6-only options set: (a) compat output contains none of the v6-only sections/option keys, `SURCHARGE_METHOD` reads `SLOT`, and stock EPA 5.2.4 parses it warning-free; (b) canonical save still contains everything, `DYNAMIC_SLOT` included; (c) diff of compat vs full output touches only the omitted/mapped lines; (d) the DYNAMIC_SLOT→SLOT substitution appears as a warning in the GUI run log; (e) a 5.x run of a *hand-edited* file with v6 sections still surfaces the skip warnings (suspenders intact).

## 9. Phase 5 — adding a future engine version (the recipe)

1. Create/branch the engine version in `openswmm.engine`; ensure worker + protocol parity; tag `vX.Y.Z-swmmvis.N`.
2. GUI: one `FetchContent_Declare` in `EngineVersions.cmake` + one `add_legacy_engine_worker()` call (subprocess engines), or a new manifest entry (in-process engines).
3. Manifest entry is generated automatically from the CMake declaration (including `writeProfile`).
4. No GUI C++ changes. Future engines with their own I/O (per the "future versions will have their own io" note) add a `family` value and, only then, a dispatch branch — the registry/manifest design leaves that seam open (`family` + `runMode` + `writeProfile` fields).

## 10. Risk register

| Risk | Likelihood | Mitigation |
|---|---|---|
| FetchContent pin drift / opaque provenance | Med | Tagged commits only; pins isolated in `EngineVersions.cmake`; SHA in comment |
| Upstream 5.2.4 CMake collides with GUI build | High if `add_subdirectory` used | Populate-only + own static worker target (§5.2 item 2) |
| Backports perturb 5.2.4 numerics | Low | IO/reporting-only rule + byte-identical `.out` regression gate (§5) |
| dylib/target name collisions between 5.x versions | Med | Static-link workers; no shared legacy libs for 5.2.4 |
| Manifest vs bundled binaries drift | Low | Manifest generated from the same CMake facts that do the bundling |
| Old project files with legacy `engineVersion` strings | Low | Ids remain version strings; registry fallback + warning |
| Compat write silently drops v6 data from user's model | Med | Compat output is a run artifact only; canonical save always Full; round-trip test in Phase 4 |
| Offline/CI configure failure (network fetch) | Med | Sibling clone is the default local source; CI caches `_deps/` |
| Local sibling clone drifts from pinned tag (builds differ dev vs CI) | Med | Configure-time log of source used; release builds forced to the pinned tag (§6.1) |
| GUI dirty working tree overlaps these files (~39 modified incl. `swmvis.cpp`, prefs, options dialog) | High | Land or stash current WIP before Phase 3 begins |

## 11. Sequencing & deliverables

```
Phase 1 (engine repo, branch build-v5.2.4) → verify: byte-identical .out gate + warning tests
Phase 2 (GUI cmake)                        → verify: both workers built, hand-run JSON check
Phase 3 (GUI manifest/registry)            → verify: unit tests + 3-engine manual matrix
Phase 4 (engine InpWriter compat + GUI)    → verify: round-trip test, EPA 5.2.4 parses warning-free
Phase 5 (docs)                             → verify: dry-run the recipe writing docs/adding-engine-versions.md
```

Phases 1–3 and Phase 4's engine half are independent and can proceed in parallel.

Changelog entries in both repos at release per CLAUDE.md §5.2.

## 12. Resolved decisions (was: open questions)

1. **FetchContent URL** (resolved 2026-08-01): `https://github.com/HydroCouple/openswmm.engine` — public https so CI/CD needs no credentials or local setup. Sibling-clone override is local-dev only.
2. **5.3.0 worker symmetry** (resolved 2026-08-01): built by the GUI via the same `add_legacy_engine_worker()` path (§6.3). Engine install prefix no longer ships a worker.
3. **Unversioned worker name** (resolved by #2): with both workers built and bundled by the GUI under versioned names, `openswmm-legacy-worker` is never produced — no transition period needed. `findLegacyWorker()` resolves only manifest-declared names.
4. **Compat write** (resolved 2026-08-01): in scope as Phase 4 — belt and suspenders alongside the 5.x skip+warn backports.
