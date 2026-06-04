# Graduated Symbology — Deep Review & Gap Analysis

**Scope:** Why the graduated renderer "is all screwed up" on the SWMM **model** layer —
can't change class bins, Apply doesn't reflect, can't sample-from-data to generate bins.

**Method:** Traced the full classify → paint pipeline for both the model layer and the
results layer and compared. The model path is missing the single step that turns user
intent into data-derived breaks. Everything downstream then degrades predictably.

---

## TL;DR (the one root cause + three knock-on gaps)

The classification step (`GraduatedRenderer::autoClassify`) **is never invoked on the
model layer**. The results layer calls it; the model layer does not. Because the breaks
(`m_lastBreaks`) and the value range (`m_ramp.min/maxValue`) are *only* ever populated by
`autoClassify`, the model renderer is permanently stuck at its construction defaults
(range `[0,1]`, empty breaks). Every reported symptom falls out of that.

---

## The pipeline, and where it breaks

### How a graduated render is *supposed* to flow

```
editor changes attr / method / bin count / manual breaks
        │
        ▼
renderer mutated + reinstalled on the layer  (setKindRenderer → clone)
        │
        ▼
layer rebuilds its per-feature color cache
        │   ── gathers attribute samples across features
        │   ── calls g->autoClassify(samples)   ← sets range + breaks from DATA
        ▼
per feature: g->symbolFor(ref, attrs) → bin = binFor(value, breaks) → color
        │
        ▼
painter / QSG reads m_kindFeatureColors → pixels
```

### What actually happens on the **model** layer

`SWMMModelLayer::rebuildKindFeatureColors` (src/layers/swmmmodellayer.cpp:2350) runs the
per-feature `symbolFor` loop (line 2425) **but never gathers samples and never calls
`autoClassify`.** So when `symbolFor` runs (graduatedrenderer.cpp:235):

- `m_lastBreaks` is empty → it can't use `binFor` (the real classification).
- It falls back to the `m_ramp` range, which is still the default `[0,1]`
  (graduatedrenderer.cpp:250-252).
- Every model attribute value (invert elev ≈ 100s, diameter ≈ 1, length ≈ 100s) is
  `>> 1.0`, so `t` clamps and **every feature lands in the last bin → one flat color.**

### What the **results** layer does (the working contrast)

`SWMMResultsLayer` rebuild (src/layers/swmmresultslayer.cpp:1998-2001):

```cpp
if (auto *g = dynamic_cast<GraduatedRenderer*>(kr)) {
    if (g->lastBreaks().isEmpty() && !values.isEmpty())
        g->autoClassify(values);          // ← model path has no equivalent
}
```

This is the entire asymmetry. The results layer samples its per-feature values and
classifies; the model layer skips it.

---

## Gap list

### G1 — Model never classifies *(root cause)*
`rebuildKindFeatureColors` has the feature loop and `identifyByName(name)` attribute access
(line 2431) — all the raw material — but no `autoClassify` call. The renderer's breaks and
range are never data-derived. **Fixes "can't sample from data" directly.**

### G2 — Re-classify is impossible after the first time *(blocks bin-count / method change)*
`autoClassify` is the *only* writer of `m_lastBreaks` (graduatedrenderer.cpp:132). But:

- `setBinner(...)` (line 78-81) swaps the binner and **does not clear `m_lastBreaks`**.
- The results rebuild gates on `lastBreaks().isEmpty()` (line 1999), so once breaks exist
  it will **never re-sample** — changing the bin count or method has no effect.
- The editor (`kindrendererpanel.cpp`) mutates the binner and reinstalls the renderer
  expecting the rebuild to re-classify, but nothing clears the stale breaks, so the old
  breaks survive the clone. **This is why "applying does not work."**

There is no `clearBreaks()` / "needs reclassify" signal anywhere. The code conflates
"first classify" with "re-classify."

### G3 — Manual breaks never reach `m_lastBreaks` *(blocks "specify bins manually")*
Manual mode sets `manualBreaks` on the binner. Those only become live breaks when
`autoClassify`/`computeBreaks(Manual)` runs — which (G1) never happens on the model and
(G2) won't re-run on results. So manually entered upper bounds are silently ignored.

### G4 — Breaks table shows `[0,1]`, not data units *(makes manual entry unusable)*
`legendSymbolItems` / the editor's breaks table derive low/high from `m_ramp.min/maxValue`
(graduatedrenderer.cpp:292-304). Until G1 is fixed that range is `[0,1]`, so the user is
asked to type "Upper" bounds against a 0–1 scale that has nothing to do with the actual
attribute. Even the displayed defaults look wrong before any edit.

### G5 — Two divergent classify implementations
The model and results layers each hand-roll their own sample-gather + classify. They've
already drifted (one classifies, one doesn't). This is the structural reason the bug
existed and will recur. A shared helper (`classifyRendererFromSamples(g, samples)`) used by
both removes the divergence.

---

## Recommended fix (minimal, legacy-aligned)

**F1 — Classify in the model rebuild (closes G1).**
In `rebuildKindFeatureColors`, before the per-feature loop, if `r` is a `GraduatedRenderer`
*and* `g->classifyAttribute()` is a static field, gather that attribute across features via
the existing `identifyByName` and call `g->autoClassify(samples)`. (Reuse the same
`soaIndexFor`/`objectNameAt` iteration already present.) The renderer mutates a *clone* the
layer owns, so this is safe and surgical.

**F2 — Make re-classify explicit (closes G2/G3).**
Add `GraduatedRenderer::clearBreaks()` (sets `m_lastBreaks.clear()`), and have `setBinner`
clear breaks when bin count or method changes. The editor's "Auto-classify", bin-count, and
method handlers call `clearBreaks()` before reinstalling, so the rebuild re-samples. For
Manual mode, the rebuild computes breaks from `manualBreaks` (i.e. drop the
`lastBreaks().isEmpty()` gate, or clear first) so manual bounds take effect.

**F3 — Build the editor breaks table from the renderer's *live* state (closes G4).**
After F1, populate the table from `lastBreaks()` + data range so "Upper" values are in real
units and round-trip correctly.

**F4 — Extract one shared classify helper (closes G5).**
Both layers call the same `autoClassify`-driven helper; eliminates future drift.

---

## Verification plan (build-pending — sandbox has no Qt6)

1. Load a model. Graduate junctions by `invertElev` → expect a real color spread (not one
   flat color), and the legend's bin bounds in elevation units.
2. Change bin count 5 → 7 and Apply → expect 7 distinct classes re-sampled from data.
3. Switch method Equal-Interval → Quantile → expect different break values, same data range.
4. Manual mode: type explicit upper bounds → expect those exact breaks in legend + paint.
5. Confirm results-layer graduation still works (no regression from the shared helper).

---

---

## Implementation (applied — build-pending)

**F1 — classify in the model path.** New `SWMMModelLayer::classifyGraduatedIfNeeded(c, g)`
gathers `g->classifyAttribute()` across the kind's features (via `identifyByName` /
`objectNameAt`) and calls the shared classify gate when breaks are empty. Called from
`rebuildKindFeatureColors` (after fetching the renderer) so the paint cache is data-derived.

**F2 — explicit re-classify.** Added `GraduatedRenderer::clearBreaks()`. `setBinner` now
clears `m_lastBreaks` (any method/count/manual change invalidates them; load paths set
`m_binner` directly in `fromJson`, so saved breaks survive). The editor's attribute-change
and Auto-classify handlers call `clearBreaks()` so the next rebuild re-samples.

**F3 — editor table from live state.** Because classification now reaches the renderer the
editor reads (see the Rule note below), the existing `rebuildBreaksTable` (which already
reads `lastBreaks()` + ramp range) shows real data-unit bounds with no further change. The
`rendererReplaced` handler runs synchronously, so the table reads fresh breaks right after
install.

**F4 — one shared gate.** `GraduatedRenderer::classifyIfNeeded(g, samples)` (static)
encapsulates the `breaks-empty && samples-nonempty → autoClassify` rule. Both the model
helper and the results-layer rebuild call it, so the two paths can't drift again.

**Rule-binding note (the subtlety that made the editor table stale):** the model symbology
tab always binds via the per-kind *Rule* (`KindTreeSymbologyPanel` sets `ctx.rule`), so
`currentRenderer()` returns the Rule's renderer while the layer classifies a downstream
clone. Fix: the Rule→layer `rendererReplaced` handler classifies the Rule's renderer *in
place* before cloning, so the editor (reads the Rule renderer) and paint (reads the clone)
share one data-derived classification. The `PerFrameAutoStretch` results path is unaffected
(it calls `autoClassify` directly each frame).

**Files touched:** `include/render/renderers/graduatedrenderer.h`,
`src/render/renderers/graduatedrenderer.cpp`, `include/layers/swmmmodellayer.h`,
`src/layers/swmmmodellayer.cpp`, `src/layers/swmmresultslayer.cpp`,
`src/ui/dialogs/editors/kindrendererpanel.cpp`.

---

## Out of scope here (tracked separately)
- Per-kind opacity `.oswp` persistence (`m_categoryOpacity` not serialized).
- #33 Stage 2 SWMMElementSymbol struct removal (architectural, gated on a verified build).
