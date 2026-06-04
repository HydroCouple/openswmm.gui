# Editor & Attribute Parity — Gap Analysis vs Legacy SWMM-GUI

**Date:** 2026-05-22
**Scope:** Identify gaps between (a) legacy SWMM-GUI (Borland Delphi / EPA SWMM 5.2) editor coverage, (b) current Qt GUI implementation, and (c) commitments already made in `GUI_IMPLEMENTATION_PLAN.md`. Used to drive surgical plan amendments and to scope follow-on implementation work.

This document is a planning artefact. It does not change code. Plan amendments derived from it land separately in `GUI_IMPLEMENTATION_PLAN.md`.

---

## 1. Method

Three parallel inventories were taken:

1. **Legacy inventory** — every editor dialog under `SWMM-GUI/Epaswmm5/` (`D*.pas`, `Fproped.pas`, `PropEdit.pas`), every object class (`Uproject.pas` MAXCLASS = 34), and every attribute slot (`MAX*PROPS` constants). Captured in `outputs/LEGACY_SWMM_GUI_EDITOR_INVENTORY.md`.
2. **Current GUI inventory** — `AttributePanel`, `SWMMAttributeTableModel`, the `attributedelegates.h` family, every dialog under `include/ui/dialogs/`, every property adapter under `include/ui/properties/`, and the engine-setter dispatch table in `swmmattributetablemodel.cpp` (lines 302–409).
3. **Plan inventory** — every section of `GUI_IMPLEMENTATION_PLAN.md` that mentions object editors, property browsers, compound editors, attribute tables, inline editing, or the cross-section dialog. Slices Z, Z.5, AG, AW, BM, BN, BO, BP, BQ, BR, BS, BT, BU, BV plus the BA–CT dependency table at line 8264.

Cross-referencing those three views produces the matrix in §3 and the gap list in §4.

---

## 2. State of the existing plan (summary)

The plan **already covers most of the attribute-hook-up work** in a reasonable structure. The relevant slices and their states:

| Slice | Subject | State | What it gives us |
|-------|---------|-------|------------------|
| Z.5 | Inline edit + shared delegates | **Shipped (first cut, 2026-05-11)** | Numeric / int / enum delegates for ~50 columns across all 11 mapped object kinds. Engine-backed via `swmm_*_set_*` dispatch table. Undo stack. Lock-out while simulation running. |
| AG | Compound property editors | DRAFT 2026-05-11 (awaiting approval) | Inflows / DWF / RDII sub-dialogs; per-type property adapters; Outfall / Storage / Divider conditional sub-editors; engine-API batch (AG.0). |
| AW | User flags + per-category attribute tables + tagging | DRAFTED (awaiting approval) | `[USER_FLAGS]` schema editor; bulk-edit attribute tables with `QUndoStack` macros; `[TAGS]` Tag column. Engine ABI batch listed (AW.4). |
| BM | `PropertyEditorRegistry` framework | ⏳ planned | `IPropertyEditor` + registry + form helpers + sidebar/dialog seam. Substrate for BN–BS. |
| BN | Node + link editors (10 + XSection) | ⏳ planned | One editor each for Junction / Outfall / Storage / Divider / Conduit / Pump / Orifice / Weir / Outlet, plus the `XSectionEditor` sub-editor. |
| BO | Subcatchment + RainGage + infiltration + LID | ⏳ planned | `SubcatchmentEditor`, `RainGageEditor`, `LIDControlEditor`, `LIDUsageEditor`, `LIDGroupEditor`. |
| BP | Pollutants / land-use / treatment / aquifer / snowpack / groundwater | ⏳ planned | 6 editors covering the water-quality / subsurface stack. |
| BQ | Curves / transect / xsection / culvert / patterns / time-series / unit hydrograph | ⏳ planned | The "tables" family. Closes AH placeholder. Includes `TransectEditor` with live cross-section preview. |
| BR | Controls / rules editor | ⏳ planned | Syntax-highlighted rule editor + linter. |
| BS | Climatology + hydrograph (RDII) group | ⏳ planned | 5-tab climate dialog + RDII group editor (closes AG.2). |
| BT | Defaults / project notes / map dims / vertex editor / map labels | ⏳ planned | Misc editors. |
| BU | Group edit + find/replace + select-by-attribute | ⏳ planned | Bulk-edit UX from legacy `Dgrouped.pas` / `Dfind.pas` / `Dquery.pas`. |

Conclusion: the plan **conceptually covers** the legacy editor surface. What's missing is parity-level detail in a few places, plus several legacy editor concepts that have no slice (or are buried in a slice without being named).

---

## 3. Coverage matrix — legacy object → plan slice → status

Status legend: ✅ shipped · 🟦 planned with adequate detail · 🟨 planned but light on detail · ❌ gap (not in plan)

### Nodes

| Legacy object / sub-dialog | Source unit | Plan slice | Status |
|---|---|---|---|
| Junction property grid | Fproped + Uedit | BN.6.4.1 + Z.5 (shipped) | 🟦 |
| Outfall property grid + conditional fields | Fproped + Uedit | BN.6.4.1 + AG.4 | 🟦 |
| Divider property grid + 3 conditional groups (cutoff/tabular/weir) | Fproped + Uedit | BN.6.4.1 + AG.4 | 🟦 |
| Storage property grid + functional-vs-tabular shape | Dstorage + Fproped | BN.6.4.1 + AG.4 | 🟦 |
| External Inflows sub-dialog (per node) | Dinflows | AG.1 | 🟦 |
| Pollutant Treatment sub-dialog (per node) | Dtreat | BP.6.6.3 (TreatmentEditor as expression editor) | 🟨 — plan treats `TreatmentEditor` as a per-pollutant expression editor; legacy `Dtreat.pas` is a per-node × per-pollutant **grid**. Plan doesn't say where the "Pollutant Treatment" sub-dialog is reached from the node editor. **Gap.** |
| DWF sub-dialog (per node) | Dinflows tab 2 | AG.1 | 🟦 |
| RDII sub-dialog (per junction) | Implicit (Uhydraul) | AG.2 + BS.6.9.2 | 🟦 |

### Links

| Legacy object / sub-dialog | Source unit | Plan slice | Status |
|---|---|---|---|
| Conduit property grid | Fproped + Uedit | BN.6.4.2 | 🟦 |
| Pump property grid + curve picker | Fproped + Uedit | BN.6.4.3 + AG.5 | 🟦 |
| Orifice property grid + side/bottom + circ/rect | Fproped + Uedit | BN.6.4.3 + AG.4 | 🟦 |
| Weir property grid + 6 weir types incl. Roadway | Fproped + Uedit | BN.6.4.3 | 🟨 — plan mentions "legacy Roadway weir" but no detail on Roadway-specific fields (Roadway Width, Road Surface enum, Weir Shape). **Minor gap.** |
| Outlet property grid + functional/tabular | Fproped + Uedit | BN.6.4.3 + AG.4 | 🟦 |
| Cross-section sub-editor | **Dxsect** | **BN.6.4.4** | 🟨 — see §5 below: shape count understated (plan says 20, legacy is 26+1 dummy), missing Darchpipe sub-dialog, missing sidewalls/force-main-roughness/filled-depth handling, **modernized viz panel under-specified**. |
| Culvert code sub-dialog | Dculvert | BQ.6.7.5 | 🟦 |
| Inlet loss specification | Dinlet → conduit | BO.6.5.* (loose) | ❌ **Gap** — no named editor for inlet loss / inlet design / inlet-usage in plan. |
| Street section sub-editor | Dstreet | BN.6.4.4 (street is listed as a shape) | 🟨 — plan lists `street` as one of the 20 shapes but says nothing about the `Dstreet`-shaped sub-form (Crown Width / Curb Height / Cross Slope / Manning's / Gutter / Sides / Back panels). **Gap.** |

### Subcatchments

| Legacy object / sub-dialog | Source unit | Plan slice | Status |
|---|---|---|---|
| Subcatchment property grid (27 props) | Fproped + Uedit | BO.6.5.1 | 🟦 |
| Infiltration model picker → 5 sub-forms | **Dinfil** | BO.6.5.1 ("expands to model-specific sub-form") | 🟨 — five infiltration models (Horton / Mod-Horton / Green-Ampt / Mod-Green-Ampt / Curve Number) each with 3–5 params; plan implies sub-forms but doesn't list them. **Light.** |
| Groundwater aquifer assignment | Dgwater | BP.6.6.6 (GroundwaterEditor) | 🟦 |
| Custom GW flow equations (GwFlowEq1/2) | Dgweqn | BP.6.6.6 ("custom flow-equation editor") | 🟨 — needs syntax-highlighter + token completion details (legacy uses Pascal-expression input). |
| Snowpack assignment | Dsnow | BP.6.6.5 | 🟦 |
| LID Usage assignment (per subcatch) | Dlidusage | BO.6.5.4 | 🟦 |
| Land-use breakdown grid | Dsubland | BO.6.5.6 | 🟦 |
| Initial pollutant loads grid | Dloads | ❌ not in plan | ❌ **Gap** — legacy `Dloads.pas` covers two flavours: (a) initial pollutant loads on a subcatch, and (b) infiltration table for storage. Plan doesn't name either. |
| Manning's-N pattern / Perv-DS pattern (monthly) | Fproped | Z.5 / AG | 🟨 — engine pattern accessor exists (`swmm_subcatch_get_pattern`?); plan should confirm. |

### Rain gage

| Legacy object | Source unit | Plan slice | Status |
|---|---|---|---|
| Rain Gage (15 props, file vs timeseries) | Uedit + Uglobals | BO.6.5.2 | 🟦 |

### Curves / data tables (the "AH" family)

| Legacy object | Source unit | Plan slice | Status |
|---|---|---|---|
| Curve editor (8 types) | Dcurve | BQ.6.7.1 | 🟦 |
| Pattern editor (4 types) | Dpattern | BQ.6.7.2 | 🟦 |
| Time-series editor (inline + file) | Dtseries | BQ.6.7.3 | 🟦 |
| Transect editor + plot | Dtsect | BQ.6.7.4 | 🟦 |
| Hydrograph / RDII unit hydrograph | Dunithyd | BQ.6.7.6 + BS.6.9.2 | 🟦 |

### Climatology / water quality

| Legacy object | Source unit | Plan slice | Status |
|---|---|---|---|
| Climatology (5 tabs) | Dclimate | BS.6.9.1 | 🟦 |
| Pollutant | Dpollut | BP.6.6.1 | 🟨 — plan lists fields but doesn't call out co-pollutant + fraction (a sub-row with picker). **Minor gap.** |
| Land-use | Dlanduse | BP.6.6.2 | 🟦 |
| Buildup/Washoff (per land-use × pollutant) | Dlanduse (sub-tabs) | BP.6.6.2 | 🟦 |
| Aquifer | Daquifer | BP.6.6.4 | 🟦 |
| Snowpack (Plowable / Impervious / Pervious / ADC tabs) | Dsnow | BP.6.6.5 | 🟨 — plan covers snowpack at a high level; missing: ATIs (Antecedent Temperature Index), Negative Melt Ratio, Snow Removal sub-table per surface, Areal Depletion Curve assignment. **Light.** |

### LID

| Legacy object | Source unit | Plan slice | Status |
|---|---|---|---|
| LID Control (multi-tab process layers) | Dlid | BO.6.5.3 | 🟦 |
| LID Usage (per subcatch placement) | Dlidusage | BO.6.5.4 | 🟦 |
| LID Group | Dlidgroup | BO.6.5.5 | 🟦 |

### Controls / rules

| Legacy object | Source unit | Plan slice | Status |
|---|---|---|---|
| Rule editor + syntax help | Dcontrol | BR | 🟦 |

### Map / annotation / project

| Legacy object | Source unit | Plan slice | Status |
|---|---|---|---|
| Default properties (templates) | Ddefault | BT | 🟨 — needs explicit reference; plan currently bundles "Defaults" without spelling out per-class default templates. |
| Project notes | Dnotes | BT | 🟦 |
| Map dimensions | Dmapdim | BT | 🟦 |
| Map labels | Dlabel | BT | 🟦 |
| Map backdrop | Dbackdrp / Dbackdim | AN (placeholder) | 🟨 — slice AN is one paragraph; needs spec when scheduled. |
| Legend / colour ramp | Dlegend / Dcolramp | BB | 🟦 |
| Interface file editor | Diface | ❌ not in plan | ❌ **Gap** — legacy `[FILES]` interface-file routing has a dedicated dialog; plan covers `[FILES]` via Slice BV but doesn't surface this editor. |
| Group edit / delete | Dgrouped / Dgrpdel | BU | 🟦 |
| Find | Dfind | AI placeholder | 🟦 |
| Query | Dquery | Z.2 (shipped) + BU | 🟦 |

### Inlet / Street (new in SWMM 5.2)

| Legacy object | Source unit | Plan slice | Status |
|---|---|---|---|
| Street section | Dstreet | (in BN as a shape, no editor) | ❌ **Gap** — STREET is shape index 5; needs a `StreetEditor` (Crown Width / Curb / Cross-slope / Manning's / Gutter / Sides / Back panels). |
| Inlet design | Dinlet | ❌ not named | ❌ **Gap.** |
| Inlet usage (per conduit / per node) | Dinletusage | ❌ not named | ❌ **Gap.** |

---

## 4. Gap list (consolidated)

These are the items not adequately covered by current shipped code + current plan text. Each gets a slice assignment in §6.

**G1.** Cross-section dialog: shape count understated (20 → 26). Plan amendment expands list to match legacy. → BN.6.4.4 expand.

**G2.** Cross-section dialog: Darchpipe.pas standard-size selector for HORIZ_ELLIPSE / VERT_ELLIPSE / ARCH is absent. → BN.6.4.4 expand.

**G3.** Cross-section dialog: per-shape extras (sidewalls combo for RECT_OPEN; filled depth for FILLED_CIRCULAR; force-main roughness; barrels) not spelled out. → BN.6.4.4 expand.

**G4.** Cross-section dialog: **modernized visualization panel** under-specified — the plan says "live preview SVG" in one phrase. User explicitly asked for design detail here. → BN.6.4.4 expand (see §5).

**G5.** Street section editor (`Dstreet`) — needed both as a shape sub-dialog (when CONDUIT shape = STREET) and as a standalone STREET object class (legacy class 33). → New BN.6.4.6.

**G6.** Inlet design editor (`Dinlet`) and Inlet usage editor (`Dinletusage`) — legacy classes 34 and (paired) — not in plan. → New BO.6.5.7 (Inlet) and BO.6.5.8 (Inlet Usage).

**G7.** Pollutant Treatment sub-dialog (`Dtreat`) — plan's BP.6.6.3 is a per-pollutant **expression editor**; legacy `Dtreat.pas` is a per-node × per-pollutant **grid** reached as a sub-dialog from the node editor. Need both surfaces. → BN clarification + BP.6.6.3 dual-surface note.

**G8.** Roadway weir extras — plan mentions "legacy Roadway weir" but no detail on Roadway Width / Road Surface enum / Weir Shape fields. → BN.6.4.3 expand.

**G9.** Infiltration sub-forms (`Dinfil`) — five infiltration models, each with 3–5 parameters; plan implies via "expands to model-specific sub-form" but doesn't enumerate. → BO.6.5.1 expand.

**G10.** Snowpack tab structure — legacy has Plowable / Impervious / Pervious / ADC tabs with ATI, Negative Melt Ratio, Snow Removal sub-table. Plan is light. → BP.6.6.5 expand.

**G11.** Custom GW flow equations (`Dgweqn`) — Pascal-expression syntax with token-completion. → BP.6.6.6 expand.

**G12.** Initial pollutant loads sub-dialog (`Dloads`) — per-subcatchment grid. → New BO.6.5.9 (or fold into BO.6.5.6 Land-use sub-editor).

**G13.** Defaults dialog (`Ddefault`) — per-class default templates feeding object creation. → BT explicit sub-phase.

**G14.** Co-pollutant + fraction picker — minor; fold into BP.6.6.1.

**G15.** Property adapters for **non-spatial** objects (Curves, Patterns, Time-series, Transects, Pollutants, Aquifers, Snowpacks, Hydrographs, LID Controls, LID Usage, Land-uses, Controls, Inlets, Streets) — current AG.3 lists 12 adapters covering only mapped objects. The Object Browser Data section (BM.0) needs adapters too. → BM.0.7 (new) — list of non-spatial property adapters.

**G16.** Engine-setter dispatch gaps — current `SWMMAttributeTableModel` dispatch table is missing entries for: storage curve, outlet type/coefficients, weir type, orifice type, pump curve picker, outfall stage data, rain gage source/interval, conduit cross-section. → Z.5 wave-2 (or fold into AG.4 / BN.6.4.* as each editor lands).

**G17.** Z.5 wave-1 shipped only Junction / Outfall / Storage / Divider / Conduit / Pump / Orifice / Weir / Outlet / Subcatchment / Rain Gage as mapped categories. Non-mapped categories (Pollutants, Land-uses, Aquifers, Snowpacks, Curves, Patterns, Timeseries, Transects, LID Controls, LID Usage, Hydrographs, Controls, Inlets, Streets) have no Attribute Table yet because Slice BM.0 hasn't shipped. → Confirm BM.0 is the prerequisite for inline-edit parity on data objects, and that BM.0 is on the critical path.

---

## 5. Cross-section dialog — modernized visualization panel design

The legacy `Dxsect.pas` dialog has two visual elements that the user has explicitly asked us to preserve in modernized form:

- **Shape thumbnail picker** — a `TListView` (`ShapeListView`) showing all 26 shapes as small icons, sourced from `Xsects.res` (binary Delphi resource), with each list item labelled by display name.
- **Implicit / illustrative figure region** — selected shape's parameters are illustrated by the labels (`Label1–4`) next to numeric edits (`NumEdit1–4`). The legacy app does *not* draw a live parametric figure; the visual cue is just the thumbnail-row highlight plus parameter-label changes.

The user's wording — "leave panels for providing figures of the cross sections for visualization and illustrative purposes as in the legacy, but in a modernized way" — implies we should (a) keep figure panels in the dialog's visual contract, (b) upgrade what they show, and (c) follow the new GUI's visual language.

Recommended modernized layout (added to BN.6.4.4 below):

```
┌─── XSectionEditor (modal sub-dialog opened from Conduit's "Geometry…" row) ───────┐
│                                                                                    │
│ ┌── Left pane: Shape picker ──┐  ┌── Right pane: Parameters + figures ──────────┐ │
│ │                              │  │                                              │ │
│ │  [QListView, IconMode,       │  │  Selected: Trapezoidal                       │ │
│ │   GridSize ~96×112,          │  │  ─────────────────────────────────────────  │ │
│ │   scrollable, 26 entries]    │  │                                              │ │
│ │                              │  │  ┌── Live parametric figure ─────────────┐  │ │
│ │  [SVG icon] Circular         │  │  │                                       │  │ │
│ │  [SVG icon] Force Main       │  │  │   (procedurally-drawn QGraphicsScene  │  │ │
│ │  [SVG icon] Filled Circular  │  │  │    or QSvgGenerator preview, updated  │  │ │
│ │  [SVG icon] Rect. Closed     │  │  │    on every parameter change; cross-  │  │ │
│ │  [SVG icon] Rect. Open       │  │  │    section drawn to scale with        │  │ │
│ │  [SVG icon] Trapezoidal  ◄── │  │  │    parameter arrows + label overlays) │  │ │
│ │  [SVG icon] Triangular       │  │  │                                       │  │ │
│ │  [SVG icon] Parabolic        │  │  │   for shapes that are parametric      │  │ │
│ │  [SVG icon] Power            │  │  │   (circular, rect, trapezoid, etc.).  │  │ │
│ │  [SVG icon] Horiz Ellipse    │  │  │                                       │  │ │
│ │  [SVG icon] Vert Ellipse     │  │  │   for fixed-geometry standard shapes  │  │ │
│ │  [SVG icon] Arch             │  │  │   (egg, gothic, catenary, etc.) it    │  │ │
│ │  [SVG icon] Rect Triangular  │  │  │   becomes a styled illustration       │  │ │
│ │  [SVG icon] Rect Round       │  │  │   matching app theme.                 │  │ │
│ │  [SVG icon] Mod Baskethandle │  │  │                                       │  │ │
│ │  [SVG icon] Baskethandle     │  │  └───────────────────────────────────────┘  │ │
│ │  [SVG icon] Egg              │  │                                              │ │
│ │  [SVG icon] Horseshoe        │  │  ┌── Parameters ─────────────────────────┐  │ │
│ │  [SVG icon] Gothic           │  │  │  Max. Depth (Geom1):  [   2.50 ] ft   │  │ │
│ │  [SVG icon] Catenary         │  │  │  Bottom Width (Geom2):[   3.00 ] ft   │  │ │
│ │  [SVG icon] Semielliptical   │  │  │  Left Slope (Geom3):  [   2.00 ]      │  │ │
│ │  [SVG icon] Semicircular     │  │  │  Right Slope (Geom4): [   2.00 ]      │  │ │
│ │  [SVG icon] Irregular        │  │  │                                       │  │ │
│ │  [SVG icon] Custom           │  │  │  Barrels:             [     1  ]      │  │ │
│ │  [SVG icon] Street           │  │  │  Manning's N:         [  0.013 ]      │  │ │
│ │  [SVG icon] Dummy            │  │  │  Sidewalls (RECT_OPEN only): [BOTH ▾] │  │ │
│ │                              │  │  └───────────────────────────────────────┘  │ │
│ │  [Search box]                │  │                                              │ │
│ └──────────────────────────────┘  │  ┌── Derived (read-only) ────────────────┐  │ │
│                                   │  │  Full-flow capacity:  12.4 cfs        │  │ │
│                                   │  │  Cross-section area:  8.75 ft²        │  │ │
│                                   │  │  Hydraulic radius:    0.97 ft         │  │ │
│                                   │  └───────────────────────────────────────┘  │ │
│                                   │                                              │ │
│                                   │  [Standard sizes…] (arch/ellipse only)       │ │
│                                   │  [Edit transect…]  (IRREGULAR only)          │ │
│                                   │  [Edit shape curve…] (CUSTOM only)           │ │
│                                   │                                              │ │
│                                   │  [OK]  [Cancel]  [Apply]                     │ │
│                                   └──────────────────────────────────────────────┘ │
└────────────────────────────────────────────────────────────────────────────────────┘
```

Key design decisions for the visualization panel:

- **Two illustrative surfaces, not one.** Left pane keeps the legacy thumbnail-grid metaphor in a modern Qt `QListView::IconMode`. Right pane adds a **live parametric figure** that the legacy never had — when the user changes Geom1–4 the figure redraws to reflect the new geometry. This is the "modernized" delta.
- **Asset strategy.** Extract 26 thumbnails from `SWMM-GUI/Epaswmm5/Xsects.res` once (offline tooling, not at build time). Convert to themed SVG under `resources/xsects/<shape>.svg` so they reskin under Slice CJ (theme system). For the live figure, parametric shapes are drawn from formulas (closed-form for circular / rect / trapezoid / triangular / parabolic / power); fixed-geometry standard shapes (egg / horseshoe / gothic / catenary / semielliptical / basket-handle / semicircular) reuse the same SVG asset rendered at full size.
- **Annotations.** The live figure draws dimension arrows for each visible Geom parameter, with the current value shown in-figure. Hovering a parameter spinbox highlights the corresponding arrow.
- **Water-surface overlay (optional toggle).** A checkbox "Show design flow water surface" overlays a horizontal line at the depth corresponding to full-flow capacity or a user-set value, so users see what the section looks like loaded.
- **Standard-size sub-dialog.** Replaces `Darchpipe.pas`. Single-page modal `StandardSizeDialog` with a `QTableView` of code → dimensions, filterable. Reached via the **[Standard sizes…]** button only when shape is HORIZ_ELLIPSE / VERT_ELLIPSE / ARCH.
- **Custom-shape branch.** When shape = CUSTOM, the figure pane swaps to a placeholder + link "Edit shape curve…" that opens the BQ `CurveEditor` filtered to type SHAPE.
- **Irregular-shape branch.** When shape = IRREGULAR, the figure pane embeds a read-only copy of the assigned transect's profile plot (from `TransectEditor`'s preview widget, see BQ.6.7.4) and a "Edit transect…" button.
- **Street-shape branch.** When shape = STREET, the figure pane shows a stylised street cross-section (crown / gutter / curb / back) and offers "Edit street section…" launching the new `StreetEditor` (see G5).
- **Dummy.** Greyed-out figure with text "DUMMY conduit — no hydraulic effect". This matches legacy semantics.

Layout choice rationale: a left thumbnail grid + right parameters/figure split is the canonical modern equivalent of the legacy `TListView` + `Label1..4 + NumEdit1..4` form. It puts the parametric figure where users look (centre-right) without removing the thumbnail picker they're already used to.

Performance / threading: figure redraws on every spinbox `valueChanged` — debounce at 50 ms. Computation is closed-form, well under 1 ms per shape — no threading concerns.

Test coverage: `test_xsection_editor.cpp` adds (a) a roundtrip case per shape (26 cases), (b) parameter-update → figure redraw signal asserted via `QSignalSpy`, (c) Darchpipe size table application sets Geom1/Geom2 correctly, (d) IRREGULAR with missing transect shows the "no transect assigned" placeholder.

---

## 6. Proposed plan amendments (surgical)

These edits land in `GUI_IMPLEMENTATION_PLAN.md`. Each one is small and traceable to a gap above.

1. **Progress-header amendment line** noting the 2026-05-22 gap-closure pass + XSectionEditor visualization-panel detailing.
2. **Append a "Gap-closure addendum (2026-05-22)" sub-section** at the end of the BA–CT block listing the gap items G1–G17 with one-line slice assignments. This preserves existing slice descriptions verbatim and adds the deltas in one place.
3. **Expand BN Phase 6.4.4** in-place to cover G1–G4 (shape list to 26, Darchpipe sub-dialog, per-shape extras, modernized visualization panel spec).
4. **Add BN Phase 6.4.6** for `StreetEditor` (G5).
5. **Expand BN Phase 6.4.3** with Roadway weir field list (G8).
6. **Add BO Phase 6.5.7** for `InletEditor` and **BO Phase 6.5.8** for `InletUsageEditor` (G6).
7. **Add BO Phase 6.5.9** for `InitialPollutantLoadsEditor` (G12).
8. **Annotate BP.6.6.3** clarifying treatment dual-surface (per-pollutant expression vs per-node grid) (G7).
9. **Annotate BO.6.5.1** with the five infiltration sub-forms enumerated (G9).
10. **Annotate BP.6.6.5** with the Plowable/Impervious/Pervious/ADC tab structure (G10).
11. **Annotate BP.6.6.6** with custom GW flow-equation syntax-help notes (G11).
12. **Annotate BP.6.6.1** with co-pollutant + fraction picker (G14).
13. **Add BM.0 sub-phase** for non-spatial property adapters (G15).

None of these change shipped slices (Z.5, Slice X, etc.). They only refine planned slices that are still `⏳ planned` or `DRAFT`.

---

## 7. Implementation sequencing recommendation

The user asked to "hook up all attributes of objects and editors for custom items". That spans ~25 dialogs and ~150 attributes across ~15 object classes. It is not a single landable change. The plan already sequences the work:

1. **AG.0 engine APIs** — Inflows / DWF / RDII CRUD + type-change helpers. This unblocks both AG and BN.
2. **AW.4 engine APIs** — User flags + tags. Unblocks AW.
3. **BM** — PropertyEditorRegistry framework. Substrate for every concrete editor.
4. **BM.0** — Non-spatial Data Objects browser surface + Data menu CRUD entry points. Substrate for BP and BQ.
5. **AG.1 + AG.2 + AG.3** — Inflows / RDII sub-dialogs + per-type adapters. Closes the in-property-browser story for nodes.
6. **AG.4 + AG.5** — Conditional sub-editors for Outfall / Storage / Divider + NameRef pickers reaching BQ.
7. **BN** — 10 hydraulic node + link editors + XSectionEditor (the user's cross-section focus).
8. **BO** — Subcatchment + Raingage + Infiltration + LID.
9. **BP** — Pollutants + Land-use + Treatment + Aquifer + Snowpack + Groundwater.
10. **BQ** — Curves / Transects / XSection table / Culvert / Patterns / Timeseries / Hydrograph.
11. **BR + BS + BT + BU** — Controls / Climatology / Defaults / Group operations.

The right next implementation step depends on user priority. Three reasonable orderings, listed in §8 as a user-decision question.

---

## 8. Open questions for the user

These need user input before any code lands.

**Q1. Which slice to land first?** Three reasonable starts:

- **Option A — Land AG first** (Inflows / DWF / RDII compound editors + per-type adapters). Highest user-visible value for existing models; engine work (AG.0) is the gating dependency.
- **Option B — Land BM + BN first** (Property editor framework + 10 node/link editors + XSectionEditor with modernized viz panel). Directly addresses the user's stated focus on attributes + cross-section dialog. Larger surface area; depends on AG.1 ABI for some sub-editors.
- **Option C — Land XSectionEditor as a standalone, ahead of BM** (using ad-hoc framework that BM later subsumes). Fastest path to the modernized viz panel the user described. Some rework later when BM lands.

**Q2.** Are the AG.0 and AW.4 engine ABI batches (filed in `openswmm.engine/docs/`) currently being worked on by the engine side, or do they need to be re-prioritised before any of this can land?

**Q3.** For the cross-section dialog visualization panel — should we (a) extract & convert the legacy `Xsects.res` thumbnails to SVG up-front as a one-time tooling task, or (b) design fresh SVG illustrations in the new GUI's visual style without reusing legacy assets? The user said "modernized way that aligns with the new GUI's style" which could be read either way.

**Q4.** Inlet / Street editors (legacy classes 33–34, new in SWMM 5.2) — confirm these are in scope for the parity work. They're not in the current plan at all.

---

## 9. Files referenced

- `GUI_IMPLEMENTATION_PLAN.md` — slices Z.5, AG, AW, BM, BN, BO, BP, BQ, BR, BS, BT, BU.
- `outputs/LEGACY_SWMM_GUI_EDITOR_INVENTORY.md` — full legacy editor inventory.
- `include/ui/panels/attributepanel.{h,cpp}` — current property browser.
- `include/ui/panels/swmmattributetablemodel.{h,cpp}` — current attribute table + dispatch.
- `include/ui/panels/attributedelegates.{h,cpp}` — Numeric / Integer / Enum delegates (Z.5.1).
- `include/ui/properties/swmm{node,link,subcatch}propertyadapter.h` — current adapters.
- `SWMM-GUI/Epaswmm5/Dxsect.pas`, `Darchpipe.pas`, `Dlid.pas`, `Dclimate.pas`, `Dcontrol.pas`, `Dtreat.pas`, `Dinflows.pas`, `Dinfil.pas`, `Dgwater.pas`, `Dgweqn.pas`, `Dpollut.pas`, `Dlanduse.pas`, `Daquifer.pas`, `Dsnow.pas`, `Dcurve.pas`, `Dpattern.pas`, `Dtseries.pas`, `Dtsect.pas`, `Dunithyd.pas`, `Dlidusage.pas`, `Dlidgroup.pas`, `Dstreet.pas`, `Dinlet.pas`, `Dinletusage.pas`, `Dloads.pas`, `Ddefault.pas` — legacy editor sources.
