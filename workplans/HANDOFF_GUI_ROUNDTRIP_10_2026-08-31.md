# HANDOFF — Manual GUI Round-Trip for the #10 Options (agent-guided)

For the agent guiding/executing the manual verification the FINAL #156 handoff
left open (its task 5). Workplan of record:
`workplans/TPA_UF_OPTIONS_GUI_PLAN_2026-08-30.md` (read its RESULTS first — the
static/automated half is DONE; this handoff is the executed half). Append your
findings as `## RESULTS (ROUND-TRIP)` at the bottom of THIS file.

## State you inherit (2026-08-31)

- GUI at `04f3ee5` (`feat(options): expose TPA closure, unsteady friction, and
  signed heads (#10)`) — the 8 workplan files are committed. Uncommitted and
  deliberately left: `tests/gui/data/mesh_savedirty_*.inp` /
  `userflags_roundtrip_out.inp` BACKEND-row edits (workplan task 7 — an owner
  decision, not yours; leave them).
- Engine at `9cda9cd3` on `swmm6_rel`. The GUI links the INSTALLED engine —
  a stale install is the #1 false-failure source in this checklist.
- `test_options_hydration_contract` last measured 21/21 warning-clean.

## Setup (macOS, owner machine)

1. Engine: clean-build `9cda9cd3` and INSTALL to
   `../openswmm.engine/install/<System>`. Verify the install is current:
   the installed CLI must accept a deck containing all five keys
   (`FV_PRESSURE_CLOSURE TPA`, `TPA_CELERITY`, `UNSTEADY_FRICTION VITKOVSKY`,
   `UF_K3`, `REPORT_SIGNED_HEADS YES`) with no "unknown option" warning in
   the .rpt. Do this BEFORE building the GUI.
2. GUI: configure + build (Qt via vcpkg). Confirm warning-clean for the 8
   committed files.
3. Automated gate first: run `test_options_hydration_contract` — 21/21.
   A failure in the two mixedFlowOptions cases = stale installed engine
   (they are row-identical to the sandbox ABI probe); fix the install, not
   the test.

## The round-trip itself

Work in a scratch folder the owner can inspect (no /tmp). Use a trivial
2-junction FV-routable project or File → New.

### R1 — Set every new control, save, inspect bytes

Simulation Options → Routing & Hydraulics:
- FLOW_ROUTING = FV (Dynamic Wave for the DW half below)
- FV pressure closure = TPA
- SURCHARGE_METHOD = TPA, TPA celerity = 250
- Unsteady friction = VITKOVSKY, k3 = 0.010

Reporting: check signed heads. OK → Save As `roundtrip_a.inp`.

Verify in the FILE (not the dialog): `[OPTIONS]` contains exactly

    FV_PRESSURE_CLOSURE  TPA
    SURCHARGE_METHOD     TPA
    TPA_CELERITY         250
    UNSTEADY_FRICTION    VITKOVSKY
    UF_K3                0.010
    REPORT_SIGNED_HEADS  YES

(whitespace per house writer; values exact). Also verify the file runs
through the installed CLI warning-free.

### R2 — Reopen and compare

Close, reopen `roundtrip_a.inp`, open both dialogs: every control shows the
value set in R1. Then Save As `roundtrip_b.inp` WITHOUT touching anything —
`diff roundtrip_a.inp roundtrip_b.inp` must be empty (writeIfChanged means an
untouched reopen adds nothing). **Falsifier:** any diff line = hydration or
conditional-seeding defect; capture it verbatim.

### R3 — Defaults stay invisible

File → New (factory preferences), touch NO new control, Save As
`defaults.inp`. Grep: none of the five keys may appear — defaults are never
written (byte-clean round-trip promise for untouched models). Reopen: dialog
shows SLOT / method default / celerity 100 greyed / UF NONE / k3 default /
signed heads off.

### R4 — Enable/disable cascades (live, the part static tracing couldn't do)

- TPA celerity spin greys whenever SURCHARGE_METHOD ≠ TPA (flip through
  EXTRAN/SLOT/DYNAMIC_SLOT/TPA and back).
- UF group greys under STEADY and KINWAVE routing; k3 greys at UF = NONE.
- FV closure combo enables/disables with the FV routing group.

### R5 — Old-engine gate

Point the GUI at a legacy 5.x (or pre-#156 6.x) engine install. Open any
project: TPA combo item, celerity spin, FV closure combo, UF group, and the
signed-heads box are DISABLED with the "requires a newer engine" tooltips.
OK, save: the file gains none of the five keys. (Known nuance from the
workplan: this holds via probe-guarded writeIfChanged, not a hard guard —
if you observe an attempted-set warning in logs, note it; it is accepted
behavior unless a key actually lands in the file.)

### R6 — Preferences seeding

Preferences → Dynamic Wave defaults: UF = Vitkovsky, k3 = 0.020. OK, reopen
Preferences (values persist). File → New on the new engine: the blank
project's `[OPTIONS]` carries `UNSTEADY_FRICTION VITKOVSKY` / `UF_K3 0.020`
(seeding writes only off-default prefs). Reset restores NONE / 0.015 and a
subsequent File → New seeds nothing.

## Report back

Append `## RESULTS (ROUND-TRIP)` here: per-step verdicts, the R2 diff
output (expected empty), R3 grep output, any cascade or tooltip miss with a
screenshot path, and engine/GUI build hashes used. Defects found are fixed
in THIS repo referencing `(#10)` only if they trace to the 8 committed
files; anything else is documented for the owner, not patched.

---

## RESULTS (ROUND-TRIP) — 2026-08-31, PARTIAL (paused by owner)

Engine `803d5cbc` clean-installed from a detached worktree (five-key probe
green on installed CLI and app-bundled engine; dylibs codesigned). GUI
built in its own `build-rt10/` (a peer session owned `build/`); the 8
committed #10 files compile warning-clean. Automated gate: hydration
contract **21/21**. Artifacts: `tests/output/roundtrip10/`.

### Verdicts

| step | verdict |
|---|---|
| Setup 1–3 | ✅ all green |
| R1 (deck→engine→save) | ✅ **PASS** — `roundtrip_c.inp.inp` carries all six keys exactly, CLI-clean (`verify.sh r1`) |
| R1 (set via dialog) | ⚠ **UNRESOLVED** — see "the two silent controls" |
| R2–R6 | ⬜ not executed (owner paused the round) |

### Findings

1. **Save As offers only the `.oswp` filter and double-appends extensions**
   — typing `roundtrip_c.inp` produced `roundtrip_c.inp.inp` (+`.oswp`).
   Cost half the round's confusion (saves landing on unexpected files).
   Not one of the 8 committed files → documented for the owner, not patched.
2. **The handoff's R3 grep is unsatisfiable as written**: the ENGINE writer
   emits `SURCHARGE_METHOD` / `UNSTEADY_FRICTION NONE` / `UF_K3 0.015`
   unconditionally (measured via C-API probe on a keyless deck), like the
   rest of its standard block; only FV_PRESSURE_CLOSURE / TPA_CELERITY /
   REPORT_SIGNED_HEADS are set-only. `verify.sh` r3/r6 carry the corrected
   contract.
3. **Stale-instance hazard bit live**: the owner's first session ran an app
   launched before the fresh install; it produced `TPA_CELERITY 150` (a
   value never entered) and dropped keys. All anomalies ceased in the
   `build-rt10` instance except finding 4.
4. **The two silent controls (OPEN)**: with instrumentation, an OK that
   changed FV_MIN_CELLS/FV_ORDER in the same group logged the closure combo
   still `SLOT` and signed-heads still `NO` — the owner's clicks on exactly
   those two controls do not change them. Signature of disabled widgets
   (probe-disable or a cascade). **Resume point: hover both controls and
   read their tooltips** — the text names the gate that disabled them. All
   engine/GUI code paths beneath them are individually proven clean
   (hydration snapshot `r1_full_dialog.png`; API set→get→write probes;
   layer passthrough; built-in writer emits all six).

### Cleanliness

The RT10 write-path instrumentation added to
`simulationoptionsdialog.cpp` during diagnosis was REVERTED to `04f3ee5`
(diff empty). `build-rt10/` and `tests/output/roundtrip10/` are local
artifacts. Nothing was committed. Verification harness: `verify.sh`
(r1/r2/r3/r6), probes `_writer_probe` / `_setdrift_probe` /
`_wp_plugin_probe`, evidence `r0_usersave_evidence.inp`, instrumented log
`rt10_app.log`.
