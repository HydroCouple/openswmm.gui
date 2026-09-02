# Offset Mode & Link Direction Save Fix — Workplan (2026-09-02)

Status: **Implemented + verified 2026-09-02.** Engine: 4 new
`InpWriterRoundTrip` tests green, full ctest 189/190 (sole red =
`test_engine_2d_transport_s4`, the S4 session's in-flight work). GUI:
`test_offsetmode_roundtrip` green (its live-context InitFlow assertion was
dropped — `swmm_link_get_initial_flow` reads live flow, not parsed q0; the
file assertion covers it), full ctest 233/234 (sole red =
`test_objectbrowser_tree_refresh`, pins pre-bulk-delete-fix semantics).
Manual §4 checks in the handoff still owed. See
`OFFSET_MODE_AND_LINK_DIRECTION_HANDOFF_2026-09-02.md` for build/verify.
Decisions taken: §3.1 (invertible writer) + §3.4(b) via new
`swmm_links_restore_authored_orientation` called after open; crest setter
`swmm_link_set_crest_height` already existed.

Reference: legacy EPA SWMM-GUI (`SWMM-GUI/Epaswmm5`).

## 1. Confirmed defects

### 1.1 Offset-mode toggle is visually inverted

`openswmm.gui/src/swmmvis.cpp:1916-1967` lays out the status bar as
`Offset Mode:  Elevation  [toggle]  Depth`, but the toggle is **checked = ELEVATION,
unchecked = DEPTH** (`:1937 pw->setElevationOffsetMode(on)`; header
`include/swmmvisprojectwindow.h:75-76`). So the OFF/left position sits beside the
"Elevation" label while actually meaning DEPTH, and ON/right sits beside "Depth"
while meaning ELEVATION. The bolding in `updateOffsetModeLabels()` (`:2042-2054`)
is correct, which is why the labels and the switch position disagree on screen.

Legacy: `objprops.txt:97-98` `LinkOffsetsOptions = ('DEPTH','ELEVATION')`, default
`DEPTH` (`objprops.txt:560-572`). Default should be the OFF position.

### 1.2 Offset *values* are corrupted on save in ELEVATION mode (the real "IO" bug)

Not a key-ordering problem — `[OPTIONS]` order is fixed in both writers
(`InpWriter.cpp:822-909`, LINK_OFFSETS 4th, same as `Uexport.pas:121-126`).
The corruption is semantic:

- Engine **normalises offsets to depth at open**:
  `openswmm.engine/src/engine/input/PostParseResolver.cpp:2208-2217` (conduits,
  `offset −= invert`, clamped ≥ 0) and `:1738-1769` (orifice/weir/outlet).
- `InpWriter.cpp:849` writes `LINK_OFFSETS ELEVATION` when the flag is set, but
  `:1738` (conduits), `:1777` (orifices), `:1799`/`:1822` (weir/outlet crest)
  write the **internal depths verbatim**. No inverse conversion exists anywhere
  in the writer.
- Result: **Open an ELEVATION deck → Save (no edits) → every offset is now a
  small depth under an ELEVATION header → on reload `depth − invert < 0` →
  clamped to 0 with WARN_NEGATIVE_OFFSET.** Offsets silently destroyed.
- `SWMMModelLayer::convertLinkOffsets()` (`swmmmodellayer.cpp:4575-4628`)
  compounds this: converting "to elevation" writes `depth + invert` into the
  engine's *depth* slots, so hydraulics/profile/attribute table now hold
  elevations the engine treats as depths. Converting "to depth" on an
  already-depth store subtracts invert a second time → zeros. Weir/outlet crest
  lives in side tables (`LinkSubtypes.hpp:275,325`) that `swmm_link_set_offset_up`
  never touches (`openswmm_links_impl.cpp:250-258`), so their conversion is lost.
- `tests/gui/test_options_hydration_contract.cpp:307-382` round-trips only the
  LINK_OFFSETS *token*, never the values — which is why this is untested.

Legacy avoids all of this by storing offset strings verbatim
(`Uimport.pas`, `Uexport.pas:1035`), never normalising; the only transform is
the user-triggered `UpdateOffsets` (`Uupdate.pas:901-1052`,
`depth = elev − invert` clamped ≥ 0 / `elev = depth + invert`).

### 1.3 Link direction is swapped on save for adverse-slope conduits

- `PostParseResolver.cpp:2262-2276`: under DYNWAVE/FV routing, any conduit with
  negative slope has `node1/node2`, `offset1/offset2`, `q0` (negated),
  `loss_inlet/loss_outlet` swapped in place and `direction *= -1`
  (mirrors legacy `conduit_reverse` in link.c).
- The GUI open path runs this resolver
  (`swmmmodellayer.cpp:795-796` → `SWMMEngine.cpp:496`).
- `InpWriter.cpp:1738` emits `node1/node2` verbatim and **never reads
  `ctx.links.direction`**. `[VERTICES]` (`:2399-2410`) is emitted in the
  original order, which is now reversed relative to the swapped endpoints.
- Result: **Open → Save (no edits) rewrites every adverse-slope conduit with
  From/To swapped, offsets swapped, InitFlow negated, losses swapped, and
  vertices backwards.** One-time per conduit (it now has positive slope), so it
  compounds with 1.2 (depth measured from the wrong invert).
- GUI-side flip (`applyLinkFlip`, `FlipLinkCommand`, `maptooladdlink.cpp`) and
  `test_linkflip.cpp` are internally consistent; no GUI swap found. No GUI or
  engine test saves after open and re-reads `[CONDUITS]`.

Legacy never persists engine-side state: it exports from its own
`TLink.Node1/Node2` (`Uexport.pas:1035-1036`) and runs the engine on a temp
file, so the engine's reversal never reaches the `.inp`.

## 2. Root cause (shared)

The Qt GUI **saves the live engine context**, and the engine applies two
non-invertible, parse-time normalisations (elevation→depth, adverse-slope
reversal) that the INP writer does not undo. Anything that runs at open and
mutates authored data must be reversed at write, or deferred to run-prep.

## 3. Proposed fix

### 3.1 Engine — make `InpWriter` emit authored form (recommended)

The engine already tracks what it did (`options.link_offsets`,
`links.direction`), so the write can be made invertible without changing the
internal canonical form:

1. `[CONDUITS]`/`[ORIFICES]`/`[WEIRS]`/`[OUTLETS]`: when `link_offsets == 1`,
   write `offset + invert_elev[node]` (up against node1, dn against node2;
   crest against node1). Apply *before* the direction un-swap below so the
   invert matches the node.
2. Conduits with `direction == -1`: write `node2, node1`, swapped offsets,
   swapped `loss_inlet/loss_outlet` in `[LOSSES]`, `−q0`. Vertices need no
   change (they were never touched).
3. Unit tests in `tests/unit/engine/test_inp_writer_roundtrip.cpp`:
   (a) ELEVATION deck round-trips byte-equivalent offsets;
   (b) adverse-slope deck round-trips From/To, offsets, losses, InitFlow;
   (c) reload of the written file produces zero WARN_NEGATIVE_OFFSET.

Alternative (B): add an engine "edit mode" open that skips both
normalisations and defers them to run-prep. Cleaner long-term but touches the
solver preparation path; larger blast radius. Not recommended for this fix.

### 3.2 GUI — offset conversion semantics

With 3.1, the engine's internal depths are already mode-independent, so:

- Toggle + **Yes (convert)** = legacy "same physics, new representation" →
  set the option only; **no value change**.
- Toggle + **No** = legacy "keep the numbers, meaning changes" →
  reinterpret: to ELEVATION → `depth' = max(0, depth − invert)`;
  to DEPTH → `depth' = depth + invert`.
- Rewrite `convertLinkOffsets()` accordingly (invert the current Yes/No
  mapping). Extend to weir/outlet crest via the proper side-table setters
  (or add `swmm_link_set_crest` if none exists — verify first).
- Display layer (`swmmattributetablemodel.cpp:681-683,1609-1611`,
  `swmmlinkpropertyadapter.cpp`, profile adapter) must show
  `depth + invert` in ELEVATION mode so the user sees what the file will
  contain. Legacy shows the raw authored string.

### 3.3 GUI — toggle layout

Reorder to `Offset Mode:  Depth  [toggle]  Elevation` in `swmmvis.cpp:1919-1967`
so OFF/left = DEPTH (default) and ON/right = ELEVATION, matching the
checked-state semantics already in code. Update
`swmmvis_hydration_audit.h:39` and any docs screenshots
(`docs/manual/02_interface.md`).

### 3.4 GUI — link direction

After 3.1 the file is correct, but the map/attribute table still shows the
engine-reversed direction for adverse conduits (legacy shows authored
direction). Options:

- (a) Accept: display engine orientation; file is authored orientation.
  Confusing — a user flipping a reversed conduit will see the file not change.
- (b) On open, GUI calls a new engine API `swmm_link_un_reverse_all()` (or
  the writer's un-swap logic exposed) so the edit session holds authored
  orientation; re-reversal happens at run-prep. Requires confirming the solver
  re-runs `resolve_cross_references` / reversal at `swmm_start`.

Recommend (b) if run-prep already re-derives slope/direction; otherwise (a)
for this iteration with a follow-up. **Needs a decision.**

### 3.5 GUI tests

- `tests/gui/test_offsetmode_roundtrip.cpp`: load
  `tests/gui/data/profile_offset/elevation_mode.inp`, save, diff
  `[CONDUITS]` offsets; toggle Yes/No paths; verify attribute-table display.
- `tests/gui/test_linkflip.cpp`: add save-after-open and save-after-flip
  cases on an adverse-slope fixture, re-read `[CONDUITS]`/`[VERTICES]`.
- All test outputs written under `tests/gui/data/out/` (reviewable, not tmp).

## 4. Execution order

```
1. Engine writer un-conversion + tests          → verify: roundtrip tests pass
2. Engine direction un-swap in writer + tests   → verify: adverse fixture roundtrips
3. GUI convertLinkOffsets rewrite + display     → verify: test_offsetmode_roundtrip
4. GUI toggle label reorder                     → verify: screenshot, hydration audit
5. GUI direction decision (3.4)                 → verify: test_linkflip save cases
6. CHANGELOG.md entries (engine + gui)
```

## 5. Open questions

1. 3.4 (a) vs (b) for on-screen direction of adverse-slope conduits.
2. Does `swmm_start` re-run the reversal, or is it parse-only? Determines 3.4(b).
3. Is there a public setter for weir/outlet crest? If not, add one in the engine.
