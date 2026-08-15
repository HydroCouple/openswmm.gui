@page manual_hydrograph_groups Unit Hydrograph Editor

The Unit Hydrograph Editor is a non-modal three-pane dialog for creating,
deleting, renaming, and editing the unit hydrographs that drive RDII
(rainfall-dependent infiltration / inflow). It edits the engine's
`[HYDROGRAPHS]` and `[RDII_DECAY]` sections via the GUI's MVC layer, so any
change you make here is reflected immediately in the Object Browser, the
property panel, and the RDII page of the per-node compound editor — without
any manual refresh.

## Opening the editor

You can open the editor from any of these surfaces:

- **Object Browser** — double-click a unit-hydrograph entry under the
  *Unit Hydrographs* section.
- **Node compound editor** — open a node's *RDII* page and click
  **Edit UH group…** next to the UH-name picker.
- **Property panel** — when a unit hydrograph is selected, the *Edit…*
  button on the Parameters row opens the editor pre-selected on that group.

The editor is non-modal. You can keep it open while you click around the
network, swap the active node, or open other editors for calibration
context. Geometry and the three-pane splitter sizes persist across
sessions.

## Layout

### Left pane — Group list

The left pane lists every unit-hydrograph group defined in the model.

- The **search** field at the top filters the list as you type (case
  insensitive).
- The **+** button at the bottom opens *New Unit Hydrograph…* — pick a
  rain gage and an initial response (Short / Medium / Long), and an
  ALL-month placeholder row is created so the editor immediately has a
  selectable group.
- The **−** button deletes the selected group. A confirmation prompt
  warns that the delete cascades to:
  - All `[HYDROGRAPHS]` parameter rows for the group
  - The rain-gage assignment
  - All `[RDII_DECAY]` rows referencing the group
  - All `[RDII]` node assignments referencing the group
- The **Rename…** button (or pressing F2 on the selected row) opens an
  inline edit. The rename propagates to all four engine containers in a
  single atomic operation.

### Middle pane — Group details

The middle pane shows the editable fields for the currently-selected
group, stacked top to bottom:

- **Name** — the group identifier, shown in bold. Use **Rename…** on the
  left pane to change it.
- **Rain Gage** — the rain gage that drives this UH's convolution. Select
  *(none)* to clear the assignment.
- **Season** — pick **All** to edit a single year-round parameter row, or
  one of the twelve calendar months to edit a per-month override. The
  RTK and Linear-IA tables below follow this selection; the Exponential
  Decay table does not — its parameters are season-agnostic.

#### Tab 1 — RTK

A 3-row × 4-column table — one row per response time scale
(Short-Term / Medium-Term / Long-Term) — with the engine's `R`, `T`, and
`K` parameters:

| Column | Meaning |
|--------|---------|
| **R**  | Fraction of rainfall volume that becomes I&I (unitless). |
| **T**  | Time to peak, in hours. |
| **K**  | Ratio of base time to peak time (≥ 1). The falling limb spans `(K − 1) · T` hours past the peak. |

Empty cells mean the engine has no parameter row for that
(response, season) combination — distinct from an explicit 0. Type a value
to upsert it through the engine.

If the season selector is on **All** but per-month rows already exist for
this group, you will be prompted before overwriting. Accepting clears all
per-month entries first; declining keeps the per-month entries and
discards your edit.

#### Tab 2 — Initial Abstraction

Two grouped sections:

1. **Linear IA (per season)** — follows the season selector. One row per
   response, columns `Dmax` (max abstraction depth, project depth units),
   `Drec` (recovery rate, depth/day), and `Do` (initial depth already
   used at simulation start).

2. **Exponential IA decay (season-agnostic)** — these parameters apply
   year-round; temperature drives recovery variation. Each row has an
   **Active** checkbox plus six numeric columns
   (`k_dep`, `k_0`, `k_T`, `T_ref`, `theta_rec`, `T_freeze`). Active
   existence is the engine's "active" flag — unchecking a row removes its
   `[RDII_DECAY]` entry and falls back to the linear IA model above.
   Checking a previously-blank row seeds defaults (`T_ref = 10 °C`,
   others 0); type new values to overwrite. Numeric columns are greyed
   out while Active is unchecked.

### Right pane — UH Preview Plot

The right pane plots the currently-selected group's triangular unit
hydrographs and their summation.

- **Y axis** — Unit flow.
- **X axis** — Time in hours.
- **Three filled triangles** — Short-Term (blue), Medium-Term (orange),
  Long-Term (green). For row `(R, T, K)`, the triangle has vertices
  `(0, 0) → (T, peak) → (K·T, 0)` with `peak = 2 · R / (K · T)` so the
  area integrates to `R` (the response fraction).
- **Composite (dashed black)** — the summation of the three triangles,
  sampled at 200 points across the plot's extent.

Empty response rows drop out of both the plot and the legend.

Standard plot affordances are inherited from the InteractiveChartView
used elsewhere in the GUI:

- **Rubber-band zoom** — drag a rectangle to zoom in.
- **Wheel zoom** — scroll up/down to zoom around the cursor.
- **Pan** — middle-button drag, or hold *Space* + left-button drag.
- **Zoom to extent** — toolbar button (or right-click → *Zoom to extent*).
- **Reset zoom** — toolbar button.

## Status strip

A short single-line strip along the bottom of the dialog shows:

> *N groups defined · "current group" has K parameter rows · Active decay
> rows: D*

It updates live as you edit.

## Synchronization with other UIs

Every edit you make in the editor routes through
`SWMMModelLayer::applyHydrograph*` (or `applyRdiiDecay*`) and fires a
`hydrographChanged(uhName)` signal on the layer. All subscribed surfaces
listen for this signal and refresh automatically:

- Object Browser — *Unit Hydrographs* branch.
- Property panel — `SWMMHydrographPropertyAdapter` row summary.
- Node compound editor — UH-name picker on the RDII page.

You don't need to close and re-open any other window to see changes; the
GUI is MVC-correct end-to-end.
