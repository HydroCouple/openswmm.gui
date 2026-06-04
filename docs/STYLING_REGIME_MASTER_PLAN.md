# Styling Regime — Master Remediation Plan

**Status:** ⏳ Draft for review — 2026-06-04
**Owner:** GUI / Rendering
**Prefix:** `SR` (Styling Regime) for new slices; existing IDs (M*, VS.*, X*, O1-*, O2-*, R-*, G-*, L-*, A-*) retained.

> **Role of this document.** This is the **index and sequencing plan** that
> consolidates the four active styling plans into one executable program, maps
> the reported gaps to concrete slices, and adds the slices no existing plan
> covers. Per `CLAUDE.md` §5.0, it **follows the vetted plans** — it does not
> replace their designs.
>
> | Plan | Authority here |
> |---|---|
> | `SYMBOLOGY_MVC_ARCHITECTURE_AND_GAPS.md` | Architecture spine (M1–M7). Authoritative for MVC. |
> | `STYLING_GAP_REVIEW.md` | Gap inventory (X/M/G/R/O1/O2/A) + recommended order. Authoritative for gap definitions. |
> | `SYMBOLOGY_GRADUATED_DEEP_REVIEW.md` | Graduated classify pipeline (F1–F4). Implemented, pending build. |
> | `VISUALIZATION_STYLING_OVERHAUL_PLAN.md` | Capabilities (VS.1–VS.11). Mostly implemented, pending build. |
> | `RENDERING_RULE_MODEL_PLAN.md` | Foundational rule model. Not superseded. |

---

## 1. Reported gaps → traceability

The driving complaints (2026-06-04), each mapped to its slice(s):

| # | Reported gap | Covered by | Status |
|---|---|---|---|
| RG-1 | Symbology tab doesn't surface **context-specific** options per layer type (static vector, raster, 1D results, 2D results) | R-2 (raster panel), O1-1 (1D variable selector), O2-1/O2-2 (2D panel + sublayers-drive-paint), M4 (one grammar) | Planned, not built |
| RG-2 | Graduated renderer: **can't edit lower/upper class bin bounds** | Partially F2/F3 (manual breaks reach live state; table in data units). Full per-bin bound editing: **NEW → SR.1** | F2/F3 pending build; SR.1 new |
| RG-3 | Graduated renderer: **no class-label formatting** (significant digits / decimals) | **NEW → SR.2** | New |
| RG-4 | **No sampling of results** to inform range estimation | Partially O1-3/M4 (range modes). Explicit sample-from-data UI (timestep scope, percentile trim): **NEW → SR.3** | New |
| RG-5 | **Labeling is confusing** | L-1 (1D label paint), L-2 (GIS unification), VS.10 finishing; UX pass: **NEW → SR.4** | Partial |
| RG-6 | **MVC tenuous** — edits don't reflect across viewers + legend | M1–M3 (done, pending build), M5, G3 `styleChanged(scope)` channel | Pending build / planned |
| RG-7 | **Legend appearance limited** — attributes, sizing, fonts | VS.9 (live wiring, pending build), X4 (model-layer legend edit). Appearance config: **NEW → SR.5** | New |

---

## 2. The architecture spine (recap, normative)

One style model per layer; every surface — properties dialog, legend dock,
layer tree, on-canvas legend, painter — is an observer/editor of that one model
via `styleChanged(scope)` and undoable commands (`SYMBOLOGY_MVC_ARCHITECTURE_AND_GAPS.md` §2).

The renderer's **AttributeSource** is dual-mode (M4): a *static* field or a
*dynamic* output variable sampled at the current timestep, with a **range mode**
(fixed-over-run / per-frame auto-stretch / fixed-user-range) and per-source
ramp + classification. This single grammar is what makes the Symbology tab
"context-specific" for all four layer families without parallel systems.

---

## 3. New slices (not covered by any existing plan)

### SR.1 — Editable class-bin bounds (closes RG-2 fully)
The breaks table in `KindRendererPanel` becomes fully editable per bin:

- Each row shows **Lower** and **Upper** in data units (lower derived: previous
  row's upper; first row's lower = data/user minimum, editable).
- Editing any bound switches the binner to **Manual** mode, writes
  `manualBreaks`, calls `clearBreaks()`, reinstalls → rebuild reclassifies
  (rides the F2 machinery; no new pipeline).
- Validation: bounds strictly increasing; reject otherwise with row highlight.
- Bin add/remove row buttons (Manual mode only).
- Round-trip: bounds persist via existing binner JSON (`manualBreaks`).

*Files:* `src/ui/dialogs/editors/kindrendererpanel.cpp`,
`include/render/intervalbinner.h` (only if min-edge storage is missing).
*Verify:* type bounds → Apply → legend + canvas show exactly those classes;
reopen dialog → same bounds; switch method away/back → manual bounds preserved
until reclassified.

### SR.2 — Class-label number formatting (closes RG-3)
Add a `LabelFormat` value type used by `GraduatedRenderer::legendSymbolItems()`:

- Fields: `mode` (decimals | significant digits), `precision` (int),
  `trimTrailingZeros` (bool), `thousandsSeparator` (bool),
  `template` (default `"%1 – %2"`).
- Editor: small "Label format" group in the graduated panel (mode combo,
  precision spin, live preview of the first class label).
- Applied wherever class labels are derived: legend dock rows, on-canvas
  legend, breaks-table display. One formatter, three consumers — no per-view
  formatting code.
- JSON round-trip on the renderer.

*Files:* `include/render/renderers/graduatedrenderer.h/.cpp` (+ `LabelFormat`),
`kindrendererpanel.cpp`, `test_ifeaturerenderer.cpp`.
*Verify:* set 2 sig figs → legend shows e.g. "1.2 – 3.4"; set 0 decimals →
"1 – 3"; persists across save/load.

### SR.3 — Sample-from-data for range/class estimation (closes RG-4)
An explicit "Sample" affordance in the graduated editor, for both sources:

- **Static field:** "Classify from data" button (exists conceptually via F1/F2)
  gains a visible **summary readout**: n, min, max, mean, p5/p25/p50/p75/p95.
- **Dynamic result variable:** sampling scope combo —
  *current timestep* | *entire run* | *every Nth timestep* (stride for large
  runs) — plus optional **percentile trim** (e.g. clip to [p2, p98]) so
  outliers don't flatten the ramp.
- Implementation: one shared `SampleStatistics` helper
  (`include/render/samplestatistics.h`) computing the readout from a
  `QVector<double>`; results layers feed it from the output reader (stride
  iteration to bound cost); model/GIS feed it from `identifyByName` gathering.
  Feeds `autoClassify(samples)` through the existing
  `GraduatedRenderer::classifyIfNeeded` gate — no second classify path (F4
  invariant preserved).
- The sampled min/max pre-populate the FixedUserRange fields (O1-3/M4 range
  modes), so "sample → inspect → pin range" is one flow.

*Files:* new `samplestatistics.{h,cpp}` + test, `kindrendererpanel.cpp`,
`swmmresultslayer.cpp` (stride sampler), `swmm2dresultslayer.cpp` (mesh field
sampler, after O2-2).
*Verify:* sample a depth run → readout matches a hand-checked min/max;
percentile trim changes class breaks accordingly; stride sampling on a long
run completes < 2 s.

### SR.4 — Labeling UX consolidation (closes RG-5 with L-1/L-2)
Once L-1 (1D results label paint) and L-2 (GIS unification) land, do a single
UX pass on `LabelsTab`:

- One enable checkbox at top; everything below disabled until checked.
- Group as **Text** (expression/attribute, font, color, number format reusing
  SR.2's `LabelFormat` for numeric expressions), **Placement** (placement,
  offset), **Readability** (halo, scale gating with min/max zoom).
- Identical tab for every layer family (base `labelConfig()` per VS.10); the
  expression picker lists static fields *and* (for results layers) output
  variables, consistent with the M4 source model.
- Remove/disable any control that has no paint-path effect for the current
  layer type (no dead knobs — that is the current confusion).

*Verify:* every visible control demonstrably changes the canvas for each of
the four layer families; no control present that does nothing.

### SR.5 — Legend appearance configuration (closes RG-7)
A `LegendStyle` config (app-level defaults + per-layer override):

- Fields: item font (family/size/weight), group/layer-title font, patch size
  (w×h px), patch outline on/off, row spacing, max label width/elide.
- Consumed by **both** `LegendDock` (delegate sizing/fonts) and the on-canvas
  `LegendOverlay` so the two stay visually consistent.
- Editor: "Legend" section on the dialog's existing structure (or the legend
  dock's context menu → Legend Settings…) — small dialog, app defaults in
  preferences.
- Class-row labels themselves come from SR.2's formatter; SR.5 is purely
  typography/geometry.
- Persist in preferences (+ per-layer override in `.oswp` when set).

*Files:* new `include/render/legendstyle.h`, `legenddock.cpp`,
`legendoverlay.cpp`, preferences page, JSON IO.
*Verify:* change item font size → dock and overlay update live (via the same
`styleChanged`/invalidation channel); restart → settings retained.

---

## 4. Phased execution order

Ordered so correctness precedes capability, and each phase has a build-verified
gate before the next (CLAUDE.md §4: verifiable success criteria).

### P0 — Build + verify the pending backlog *(gate for everything)*
A large body of work is implemented but unverified ("pending build" — the
sandbox has no Qt6): M1.a/M1.b, M2, M3-core, graduated F1–F4, VS.2–VS.10.

- Build on the dev machine; run `ctest`; walk the verification lists already
  written in `SYMBOLOGY_GRADUATED_DEEP_REVIEW.md` (5 manual checks) and the
  VS progress log.
- Fix fallout until green. **Nothing below starts before this gate** — every
  later slice assumes the M1/M2 single-source semantics actually hold.

*Exit criteria:* dialog opens matching the drawn style; graduated junctions by
invertElev show a real spread with data-unit bounds; bin-count/method changes
take effect on Apply; `ctest` green.

### P1 — Trust the wire: X1 + X2 (color canonicalization, link color)
Per `STYLING_GAP_REVIEW.md` §5 step 1. One key + one encoding (recommend hex)
across adapters, specs, converters, `.oswp` IO. Kills the residual
"edit doesn't reflect" class before new editors are layered on.

*Exit criteria:* node fill, node outline, link color/width/dash each round-trip
editor → canvas → reopen for all 11 kinds; grep shows one canonical color key.

### P2 — Graduated editor completion: SR.1 + SR.2 + SR.3(static half)
Directly resolves RG-2/RG-3 and the static half of RG-4 on the machinery P0
just verified. Small, surgical, high user-visible value.

*Exit criteria:* SR.1/SR.2 verify lists pass; manual bounds + label format
survive save/reload.

### P3 — The spine: M4 AttributeSource + range modes
Per the MVC plan. `AttributeSource{static|dynamic}` on the renderer; per-frame
sampling hook; range mode (fixed-over-run default / per-frame auto-stretch /
fixed-user). Folds the results override caches + 2D sublayer bags toward the
one model. Connect `rebinDynamicRulesIfNeeded()` to `currentTimeStepChanged`
when mode = per-frame (closes A-1).

*Exit criteria:* a graduated renderer on a results layer re-bins per frame in
auto-stretch mode and stays fixed in fixed mode; M7-style propagation test for
the source toggle.

### P4 — Context-specific symbology surfaces (RG-1)
Now the dialog can surface what each layer type actually needs, on one grammar:

1. **2D results:** O2-2 (sublayer styles drive paint — biggest visible win,
   makes SE.4/VS.6/VS.7 editing real) then O2-1 (2D Symbology panel: dry/max
   depth, ramp, classes, isolines, contour bands, velocity arrows). O2-3
   (variable concept) and O2-4 (range control) ride the M4 spine.
2. **1D results:** O1-1 (variable selector in the kind tree per the
   open-question recommendation), O1-2 (paint consults renderer), O1-3 (range
   mode UI), O1-4 (per-variable ramp + classification, persisted).
   SR.3(dynamic half) lands here — the sampler needs the variable selector.
3. **Raster:** R-1 (warp colourisation through `rasterRenderer()`), R-2
   (raster Symbology panel: ramp + classification + hillshade controls;
   palette + class table for categorical). R-3 azimuth check.

*Exit criteria:* for each of the four layer families, opening Properties →
Symbology shows only and exactly the controls relevant to that family, and
every control changes the canvas; 2D sublayer edits visibly repaint; 1D
variable switch restyles; paletted raster renders via its renderer.

### P5 — Legend regime (RG-7)
- X4: route legend per-class edits to the per-kind renderer for
  `SWMMModelLayer` (fix `featureRendererFor` coverage) — legend becomes a true
  editor for every layer type (M5).
- Verify VS.9 live wiring (sublayer `invalidated()` → legend refresh) on the
  P0 build.
- SR.5 legend appearance configuration.

*Exit criteria:* edit a class color in the legend dock → dialog + canvas +
on-canvas legend update live, for model, GIS, raster, 1D and 2D layers; legend
fonts/patch sizes configurable and persistent.

### P6 — Labeling (RG-5)
L-1 (1D results label painting, mirroring `SWMMLayerItem`), L-2 (GIS labels
through the unified base), then SR.4 UX pass.

*Exit criteria:* SR.4 verify list (no dead knobs, all four families paint
labels).

### P7 — MVC closure + hardening (RG-6 finish line)
- M6: undo/redo command stack on the model + whole-model `.oswp` persistence
  (X5). The SR.1/SR.2 state rides the renderer JSON so it persists for free.
- X3/M-3: GL paint path through the renderer + `SWMMElementSymbol` struct
  removal (compiler-in-the-loop, per M3-cleanup; coordinate with
  `P6_STRUCT_REMOVAL_AND_UNDO_PLAN.md`).
- G-1/G-2: GIS vector onto canonical shapes + single-source pattern.
- X6: opacity end-to-end check.
- M7: acceptance tests — propagation (edit view A → views B/C + paint),
  stale-on-open regression, static↔dynamic round-trip, undo across views.

*Exit criteria:* M7 suite green; struct type deleted; `.oswp` round-trips the
full model losslessly.

---

## 5. Test matrix (delta on the existing suite)

| Slice | Test |
|---|---|
| SR.1 | `test_intervalbinner` + new: manual bounds round-trip, monotonic validation, method-switch preservation |
| SR.2 | `test_ifeaturerenderer`: label format JSON + formatting cases (sig figs, decimals, trim) |
| SR.3 | new `test_samplestatistics`: stats vs hand values; percentile trim; stride sampler bounds |
| SR.4 | per-family manual checklist (controls→paint), integration-level |
| SR.5 | `test_legendstyle` JSON; dock/overlay sizing smoke (integration, needs build) |
| P3 | range-mode rebin per frame vs fixed (extend results tests) |
| P7 | M7 acceptance set (propagation, stale-on-open, round-trip, undo) |

Existing unit coverage (binner, renderers, ramps, palettes, specs) stays the
regression floor; integration tests authored against the P0 build per the VS
plan's §8 strategy.

---

## 6. Decisions needed (consolidated; recommendation first)

1. **Color canonicalization (X1):** hex strings (recommended) vs QColor variants.
2. **Range-mode default** for animated outputs: fixed-over-run (recommended)
   with opt-in per-frame auto-stretch.
3. **Variable selection placement (O1-1):** layer-level control above the kind
   tree (recommended) vs per-kind.
4. **2D field scope (O2-3):** depth + velocity first, arbitrary mesh fields
   (WSE/hazard/pollutants) behind the same combo as a follow-up (recommended).
5. **SR.5 scope:** app-level legend style only (recommended first pass) vs
   per-layer overrides immediately.
6. **SR.3 dynamic sampling default scope:** entire run with stride (recommended)
   vs current timestep.
7. **Undo granularity (M6):** coalesced per editing session (recommended) vs
   per-property.

---

## 7. Governance

- This document is the **single sequencing authority**; the four source plans
  remain authoritative for their designs. Update both on material change.
- Each phase ends with its exit criteria demonstrated on a dev-machine build
  before the next phase starts (no further "pending build" stacking beyond P0).
- New gaps discovered en route get an `SR.n` entry here, not a new standalone
  plan, unless they are architectural.
