# Symbology Smoke Test Checklist — 2026-06-04

**Against commit:** `7b719f6`

## Setup (do first — this is the whole point)

- [ ] **S0.** Rebuild: `cmake --build build/darwin-debug` (one file was edited
  after the last build). Launch **`build/darwin-debug/SWMMVis.app`** —
  NOT `build/test/SWMMVis.app` (May 29, predates everything; delete it).
- [ ] Load any model with junctions, conduits, and subcatchments.

## T1 — Binary sanity / dialog routing (proves you're running the new code)

- [ ] **T1.1** Right-click model layer → Properties. Tabs are exactly:
  Information, Source, Symbology, Labels, Rendering, Metadata (VS.5).
- [ ] **T1.2** Symbology tab shows a **kind tree** on the left
  (Junctions/Outfalls/…/Subcatchments/RainGages) with a renderer editor on
  the right — not a flat grid or rules list.
  **If T1.1/T1.2 fail → stop. You are still running an old binary.**

## T2 — Dialog reflects reality (M1/M2)

- [ ] **T2.1** Note the junction color on canvas. Open Properties → Symbology
  → Junctions. Editor shows that exact color/size/shape (no stale defaults).
- [ ] **T2.2** Change junction color → Apply → canvas updates without closing.
- [ ] **T2.3** Close (OK), reopen → editor shows the new color (no revert).
- [ ] **T2.4** Cancel semantics: change opacity on Rendering tab → Cancel →
  opacity reverts. (Symbology edits applied live do NOT revert — by design.)

## T3 — Graduated renderer (F1–F4, VS.3)

- [ ] **T3.1** Junctions → renderer: Graduated, attribute `invertElev` (or any
  numeric field) → Apply. Expect a **real color spread** across the network,
  not one flat color.
- [ ] **T3.2** Breaks table shows bounds in **elevation units** (e.g. 95–112),
  not 0–1.
- [ ] **T3.3** Method combo lists: Equal interval, Quantile, Manual,
  **Natural breaks (Jenks), Standard deviation, Logarithmic, Exponential**.
- [ ] **T3.4** Switch Equal interval → Quantile → Apply. Break values change;
  data range unchanged.
- [ ] **T3.5** Bin count 5 → 7 → Apply. Seven distinct classes appear in
  table, legend, and canvas.
- [ ] **T3.6** Manual mode: type explicit upper bounds → Apply. Exactly those
  breaks in legend + canvas.
- [ ] **T3.7** Conduits: graduate by `diameter` or `length` — same checks
  (verifies the link/width path, X2 risk area).

## T4 — Legend (VS.9, X4)

- [ ] **T4.1** With the dialog open, change a class color → legend dock rows
  update without panning the map.
- [ ] **T4.2** Edit a 2D-results sublayer style → legend dock refreshes (VS.9).
- [ ] **T4.3** *Expected-fail:* click a class color swatch in the legend dock
  for the **model layer** and try to edit — known gap X4; note actual
  behavior but don't debug it.

## T5 — 1D results layer

- [ ] **T5.1** Run/load results → results layer Properties → Symbology routes
  to the kind tree.
- [ ] **T5.2** Animate timesteps → node/link colors restyle per frame.
- [ ] **T5.3** *Expected-limit:* no variable picker in UI (O1-1, planned);
  range frozen after first frame unless auto-stretch (O1-3).

## T6 — 2D results layer

- [ ] **T6.1** Properties on 2D results layer → a dedicated 2D style panel
  appears (`Swmm2DResultsStylePanel`).
- [ ] **T6.2** Change a control (e.g. ramp or dry depth) → Apply → canvas
  changes. *If edits do nothing, that's O2-2 (sublayers dormant) — record
  which controls are live vs dead; this calibrates the master plan.*
- [ ] **T6.3** Velocity arrows: enable color-by-magnitude in the sublayer
  style dialog → arrow shafts ramp by speed (VS.7 CPU path). Note whether
  the GPU/QSG glyphs also ramp (unverified path).

## T7 — Raster layer (VS.6)

- [ ] **T7.1** Load a DTM → ramp editor shows a **hillshade toggle**; enabling
  it shades the relief.
- [ ] **T7.2** Change the color ramp → Apply → raster recolors (tests whether
  renderer drives paint — R-1 may already be fixed; record result).

## T8 — Labels (VS.10)

- [ ] **T8.1** Model layer → Labels tab → enable, pick attribute, set font →
  labels paint on canvas.
- [ ] **T8.2** 2D results layer → Labels tab present; per-cell value labels
  paint.
- [ ] **T8.3** *Expected-fail:* 1D results layer Labels tab exists but nothing
  paints (L-1, known).

## T9 — Tree/opacity (VS.8)

- [ ] **T9.1** Edit opacity in the layer tree column → canvas dims live.
- [ ] **T9.2** Change opacity in dialog Rendering tab → Apply → tree row
  reflects it.

## T10 — Persistence

- [ ] **T10.1** Save project (.oswp) with a graduated junction style → close →
  reopen → style survives (color spread + breaks). *Partial loss is X5/M6
  (known); record exactly what survives.*

## Reporting

For each failure record: test ID, layer type, what you did, expected,
observed, and a screenshot. T1 failures = wrong binary; T4.3/T5.3/T8.3 =
known gaps (don't debug); everything else = file against the crosswalk
(`IMPLEMENTATION_CROSSWALK_2026-06-04.md` §3) so we re-baseline the plan.

`ctest --test-dir build/darwin-debug` afterward for the unit suite.
