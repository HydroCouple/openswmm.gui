# Multi-Engine-Version Support — Amendment v2 (2026-09-03)

**Status:** DRAFT FOR REVIEW. Amends `MULTI_ENGINE_VERSION_SUPPORT_PLAN_2026-08-01.md`
(the "base plan"); every base-plan decision stands unless changed below.
Companion: `LIVE_1D_RESULTS_PLAN_V2_2026-09-03.md` (live 1D results by tailing
the `.out`), which is what makes live rendering possible for 5.x engines at all.

Three additions: (§1) audit of what has landed since 2026-08-16, (§2) live
rendering for 5.2.4 and 5.3.0, (§3) CI/CD so the 5.2.4 engine is retrieved,
built and bundled in the GUI build as a selectable comparison engine.

---

## 1. Audit (2026-09-03)

| Base-plan item | State | Evidence |
|---|---|---|
| Phase 1 step 0 — 5.2.4 build modernisation (presets, vcpkg, GoogleTest, OpenMP gate, CI) | **DONE** | `openswmm.engine` branch `build-v5.2.4` @ `359ca277`; `CMakePresets.json`, `vcpkg.json` (`swmm-solver` 5.2.4, `tests`→`gtest`), `.github/workflows/build-and-test.yml` (4-leg matrix, triggers only on `build-v5.2.4`). Sibling checkout exists at `../swmm5.2.4/openswmm.engine.5.2.4` — **not** the base plan's `../openswmm.engine.v524` name (§3.4). |
| Phase 1 steps 1-6 — backports (unknown section/option skip, warning callback, running mass balance, worker, tag) | **NOT DONE** | 5.2.4 `src/solver/include/swmm5.h` lacks `swmm_setWarningCallback`, `swmm_getRunningMassBalErr`; no `src/worker/`; no `v5.2.4-swmmvis.*` tag. |
| Phase 2 — GUI builds/packages versioned workers | **NOT DONE** | no `cmake/EngineVersions.cmake`, no `cmake/LegacyEngineWorker.cmake`; GUI bundles the engine install's single `openswmm-legacy-worker` (`CMakeLists.txt:1866-1898` macOS, `:2090-2113` Windows, `:2230-2250` Linux). |
| Phase 3 — `engines.json` + `EngineRegistry` | **NOT DONE** | two hardcoded combo items (`src/swmmvis.cpp:1882-1899`, `preferencesdialog.cpp:150-160`); dispatch `useLegacy = engineVersion.startsWith("5.")` (`simulationrunner.cpp:268`); `findLegacyWorker()` single name (`:70-113`). |
| Phase 4 — compat `.inp` write | **NOT DONE** | — |
| GUI CI | engine fetched from `HydroCouple/openswmm.engine` @ `swmm6_rel`, built with `2d;gpu;hypre`, installed and `find_package`d (`.github/workflows/build_and_test.yml:56-57, 323-329, 525-559, 624`); install-tree cache keyed on engine SHA (`:504-516`); gates assert the single legacy worker is present (`:582-594`, `:794-816`). No 5.2.4 anywhere. |

Base-plan line citations that moved: status-bar combo `swmmvis.cpp:1882-1899`
(was 1577-1595); prefs combo `preferencesdialog.cpp:150-160`; QSettings key
code `preferencesmanager.cpp:1470-1485`; dispatch `simulationrunner.cpp:268`;
serializer `projectserializer.cpp:75,306,667,1191`; bundling blocks as above.

Two facts the base plan did not record that matter for §2:
- The 5.3.0 legacy library is versioned `libopenswmm.legacy.engine.6.dylib`
  (`SOVERSION ${PROJECT_VERSION_MAJOR}` = 6, `src/legacy/engine/CMakeLists.txt:87-96`).
  The base plan's static-link-the-workers decision (§6.2) avoids this name
  colliding with anything 5.2.4 — keep it.
- The 5.2.4 API returns `void` from `swmm_getName`/`swmm_setValue` where 5.3.0
  returns `int`; the worker only uses the stepwise core, which is identical.

---

## 2. Live rendering for 5.2.4 and 5.3.0

### 2.1 Mechanism

Live 1D results come from tailing the `.out` the engine is writing
(`LIVE_1D_RESULTS_PLAN_V2_2026-09-03.md` §1-2). The GUI side is engine-agnostic:
`SimulationRunner` emits `resultsFileGrew` on every parsed worker `progress`
line and `SWMMResultsLayer::refreshLive()` re-counts periods from the file
size. A 5.x engine therefore needs exactly one thing to render live: **its
writer must flush at every report period.**

### 2.2 5.3.0 (engine `src/legacy`)

`output_saveResults()` in `src/legacy/engine/output.c` gains
`fflush(Fout.file)` after the `SysResults` write — this is Phase E1 of the
live plan and is implemented there. No worker change: `progress` lines already
arrive at `tickIntervalMs` (`src/legacy/worker/main.cpp:100-130`).

### 2.3 5.2.4 (branch `build-v5.2.4`)

Add the same one-line `fflush` to `src/solver/output.c::output_saveResults`
as **backport step 4b** in the base plan's Phase 1 list. It is IO-only: the
bytes, their order and the numerics are unchanged, so the base plan's
byte-identical `.out` gate (`tools/verify_524_parity.sh`) still passes — that
gate is the proof the backport is safe. Everything else needed for live
rendering (worker `progress` cadence) comes from the Phase 1 step 5 worker.

### 2.4 What the user sees per engine (the "options available")

| Capability during a run | 6.x | 5.3.0 | 5.2.4 (after Phase 1) |
|---|---|---|---|
| Live 1D map animation + profile/comparison plots | yes | yes | yes |
| Running continuity in the job row | yes | yes (`swmm_getRunningMassBalErr`) | yes once step 4 is backported; **omitted (null) until then** — the worker must tolerate its absence, and the status model shows "—" |
| Warnings streamed to the log | yes | yes (`swmm_setWarningCallback`) | yes once step 3 is backported; until then only the `.rpt` |
| Unknown v6 sections skipped | yes | yes | yes once steps 1-2 are backported; until then a v6-flavoured `.inp` **fails** — so Phase 4 compat write is *required* for 5.2.4, not belt-and-braces |
| Pause/resume, 2D, plugins, dynamic slot | yes | no | no |
| Live 2D | yes | n/a | n/a |

Add to the manifest capability set (base plan §7.1): `"liveResults": true`
for all three, and `"runningContinuity": false` for 5.2.4 until step 4 lands
(the manifest is generated from CMake facts, so this is a per-version
constant in `EngineVersions.cmake`).

`SimulationStatusModel` already keys rows by `(projectWindow, engineVersion)`,
so the same model run on 6.x / 5.3.0 / 5.2.4 shows three rows; with live
results each row's results layer can be the primary of a comparison plot,
which is the comparison use case this is for. No new GUI comparison UI is
needed beyond the existing ComparisonPlotDialog multi-run sources.

### 2.5 Ordering change to the base plan's Phase 1

To get live 5.2.4 rendering as early as possible, do the steps in this order:
**0 (done) → 5 (worker, minimal: stepwise core + `progress`/`dates`/`error`
lines, continuity omitted) → 4b (fflush) → tag `v5.2.4-swmmvis.1`** →
then 1, 2, 3, 4 (backports) → tag `-swmmvis.2`. The GUI build (§3) pins the
tag, so each increment is a one-line pin bump.

---

## 3. CI/CD: retrieve, build and bundle 5.2.4 in the GUI build

### 3.1 GUI workflow (`.github/workflows/build_and_test.yml`)

Add, parallel to the existing engine variables:

```yaml
env:
  OPENSWMM_ENGINE_REPO: HydroCouple/openswmm.engine
  OPENSWMM_ENGINE_REF:  swmm6_rel
  OPENSWMM_ENGINE_524_REF: v5.2.4-swmmvis.1     # tag from §2.5; branch build-v5.2.4 until it exists
```

Steps (inserted after the existing engine checkout at `:323-329`):

1. **Checkout 5.2.4 source** — `actions/checkout` of `${OPENSWMM_ENGINE_REPO}`
   at `${OPENSWMM_ENGINE_524_REF}` into `openswmm.engine.v524` (sibling of the
   GUI checkout, the base plan §4.1 name), `fetch-depth: 1`.
2. **Resolve + cache** — record the 5.2.4 commit SHA (`git rev-parse HEAD` in
   that dir) and add it to the engine install-tree cache key (`:504-516`) so a
   5.2.4 bump invalidates the cache like a 6.x bump does. Nothing is built from
   the branch's own CMake in CI: the GUI configure compiles `src/solver/*.c`
   directly (base plan §6.2), so no separate 5.2.4 build/install step and no
   vcpkg for it.
3. **Configure** — pass
   `-DFETCHCONTENT_SOURCE_DIR_OPENSWMM_ENGINE_524=$GITHUB_WORKSPACE/openswmm.engine.v524`
   (so `FetchContent` never touches the network in CI) and
   `-DSWMMVIS_ENABLE_ENGINE_524=ON`. Configure log must print which source
   dir and which tag/SHA each worker was built from (base plan §6.1).
4. **Gate: bundle contents** — extend the existing "Verify GUI bundles the
   legacy worker" step (`:794-816`) to require *both*
   `openswmm-legacy-worker-5.3.0` and `openswmm-legacy-worker-5.2.4` (+ `.exe`
   on Windows) and `engines.json` beside the executable, and to fail if the
   unversioned `openswmm-legacy-worker` is still present (base plan §12.3).
   Drop the engine-side single-worker assertion at `:582-594` once the engine
   install no longer ships a worker (base plan §6.3).
5. **Gate: each worker runs** — for each versioned worker, run it on
   `examples/Example1.inp` from the engine checkout with a 200 ms tick, assert
   the first stdout line is the `hello` JSON with the matching
   `engineVersion`, at least one `progress` line, a `continuity` line
   (5.3.0) or its documented absence (5.2.4 pre-step-4), exit code 0, and
   that `swmm_output_open` (via the bundled `openswmm` CLI or a tiny check
   tool) reports the expected `swmm_output_get_version()`. Keep the three
   `Example1.out` files as a workflow artifact `engine-comparison-<triplet>`
   so a reviewer can diff them.
6. **Gate: live tail** — same worker run, but the check tool opens the `.out`
   with `swmm_output_open_live` *while the worker runs* and asserts the period
   count grows before the process exits (proves §2.3's flush on every
   platform). Skippable with `if:` until the live plan's Phase E2 lands.

Deployment (`deployment.yml` in the engine repo) is unaffected: the GUI, not
the engine release, carries the 5.2.4 worker.

### 3.2 GUI CMake

`cmake/EngineVersions.cmake` as in the base plan §6.1, with the pin line the
only thing CI overrides:

```cmake
set(OPENSWMM_ENGINE_524_TAG "v5.2.4-swmmvis.1" CACHE STRING "pinned 5.2.4 tag")
# sibling override for local dev — either name, base plan §4.1 or the
# checkout this machine already has:
foreach(_cand "${CMAKE_SOURCE_DIR}/../openswmm.engine.v524"
              "${CMAKE_SOURCE_DIR}/../swmm5.2.4/openswmm.engine.5.2.4")
  if(EXISTS "${_cand}/src/solver" AND NOT DEFINED FETCHCONTENT_SOURCE_DIR_OPENSWMM_ENGINE_524)
    set(FETCHCONTENT_SOURCE_DIR_OPENSWMM_ENGINE_524 "${_cand}")
  endif()
endforeach()
```

`add_legacy_engine_worker(VERSION 5.2.4 …)` compiles `src/solver/*.c` +
`src/worker/main.cpp` statically with the GUI's FP flags (base-plan amendment
2 — decision: **apply `-ffp-contract=off` / `/fp:precise`** so the bundled
worker reproduces the benchmark numbers), `OpenMP::OpenMP_C` when found
(5.2.4 hard-requires it standalone; in the worker it is optional — a
`SWMM_WITH_OPENMP`-style toggle, default ON when found).

### 3.3 Engine-side CI (branch `build-v5.2.4`)

The branch workflow already builds and unit-tests. Add: (a) the parity gate
job (`tools/verify_524_parity.sh` stock-tag vs branch, `.out` byte-identical)
so a backport can never merge unverified; (b) a job that builds the worker
target from the branch's own CMake (so the branch stays self-contained even
though the GUI compiles the sources itself); (c) a tag-triggered publish of a
source tarball `openswmm-engine-5.2.4-swmmvis.N-src.tar.gz` as a fallback
fetch source for the GUI when GitHub shallow-tag fetch is unavailable
(base plan §2.2 risk 2).

### 3.4 Local layout

The base plan assumes `../openswmm.engine.v524`; this machine uses
`../swmm5.2.4/openswmm.engine.5.2.4`. §3.2's `foreach` accepts both; document
both in `docs/adding-engine-versions.md` (base plan Phase 5).

---

## 4. Updated sequencing

```
L-E1/E2  live plan engine phases (fflush in 6.x + 5.3.0 writers; live reader)   ← engine repo
L-G1..G6 live plan GUI phases                                                    ← GUI repo
M-1      5.2.4: worker (minimal) + fflush backport + tag -swmmvis.1  (§2.5)       ← build-v5.2.4
M-2      GUI cmake: EngineVersions + add_legacy_engine_worker ×2 + manifest      ← base §6
M-3      GUI: EngineRegistry, combos, dispatch, capability gating               ← base §7
M-4      CI: §3.1 steps 1-5 (+6 once L-E2 is in)                                 ← GUI workflow
M-5      5.2.4 backports 1-4 + compat write (base Phase 1 rest + Phase 4), tag -swmmvis.2
M-6      docs + CHANGELOGs
```

L-* and M-1/M-2 are independent; M-3 depends on M-2; M-4 depends on M-2/M-3;
live 5.2.4 rendering is available as soon as L-G2 and M-1..M-3 are in.

## 5. Decisions needed

1. Accept §2.5's reordering (minimal worker + flush first, backports after)?
2. FP flags for the bundled 5.2.4 worker: match the branch presets
   (`-ffp-contract=off` / `/fp:precise`) — proposed yes.
3. Keep the 5.2.4 job row's continuity as "—" until step 4 is backported, or
   block 5.2.4 from the UI until all four backports land?
4. Sibling-dir name: standardise on `openswmm.engine.v524` (rename the local
   checkout) or keep accepting both?
