# Static-Attribute Styling Plan — SUPERSEDED 2026-05-27

> **Status:** ⛔ **withdrawn 2026-05-27.** Superseded by [`RENDERING_RULE_MODEL_PLAN.md`](RENDERING_RULE_MODEL_PLAN.md).
>
> **What changed:** This plan proposed extending the sublayer architecture (`ISublayer` / `ISublayerHost`) to static attributes by introducing a `KindSublayer` base class plus 11 concrete subclasses (`JunctionKindSublayer`, `OutfallKindSublayer`, `ConduitKindSublayer`, …). None of that code was shipped. The user pass on 2026-05-27 pivoted the styling vocabulary from per-sublayer rows to a layer-scope **Rule List** picked via an Active Rule combo in the Symbology tab. The same static-attribute styling capability (Single / Categorized / Graduated / Rule-based / Heatmap rendering driven by invert elevation, diameter, slope, material, install date, asset class, …) is now covered by the Rule Model and its Z.3–Z.5 slices.
>
> **What's preserved in the new plan:**
> - Full styling editor stack — Slice BI / BI.2 / BI.3 / BB — now packaged as Rule decorations + renderers.
> - Style I/O (`.qml` round-trip + `.swmm-rule.json`) — Z.17.
> - Per-kind defaults on `SWMMModelLayer` (11 categories: Junctions / Outfalls / Storage / Dividers / Conduits / Pumps / Orifices / Weirs / Outlets / Subcatchments / RainGages) — now seed Rules in the default Rule List, not subclasses.
> - `GISVectorLayer` styling — covered by Z.3 (Single / Categorized / Graduated / Rule-based) + Z.4 (Marker) + Z.5 (Line) + Fill Symbol Layer.

For the current plan and slice list, see **[`RENDERING_RULE_MODEL_PLAN.md`](RENDERING_RULE_MODEL_PLAN.md)** (slices Z.1–Z.18).

---

## Archaeology — what this plan originally proposed

Kept as a one-paragraph footer so future readers chasing why `KindSublayer` doesn't exist in the codebase have the trail:

The original plan (drafted 2026-05-25) proposed a `KindSublayer` abstract base on `OpenSWMMVisLayer` and 11 concrete derivations — one per `SWMMModelLayer::Category` — that would expose themselves through `ISublayerHost`. The layer tree would render each sublayer as a child row under `SWMMModelLayer`, and `MapSymbologyDialog` (a sibling of `SublayerStyleDialog`) would edit one sublayer at a time. The 2026-05-27 pivot deprecated `ISublayer` as a user-facing concept (it survives as a C++ rendering primitive only) and replaced the sublayer-row UX with the Rule List UX. The `KindSublayer` class hierarchy was never implemented.
