# Integrated2D Groundwater & Infiltration — GUI Plan (2026-08-15)

**Status:** Proposal — for review. No implementation yet. **Blocked on** the
engine plan `openswmm.engine/plans/TWO_ZONE_GROUNDWATER_EXPLICIT_LTS_PLAN_2026-08-15.md`
(config: steps 1, 11b; API: §8.5 with steps 8/19; outputs: HDF5 sidecar
variables) and, for transport variables,
`openswmm.engine/plans/transport/TWOD_TRANSPORT_PLAN.md` phases S1–S7.
**Companion:** `workplans/TRANSPORT_QUALITY_GUI_PLAN_2026-08-12.md` — this
plan extends its `model.i2d` scope (its §4.1 note) with the groundwater and
infiltration configuration and the subsurface visualization; the dynamic
result-descriptor decision **D-G1 there is reused unchanged** here.
**Decisions (engine-side, carried in):** D-UT8/D-UT9 (one `integrated2d`
component, one `model.i2d` config), D-GW1..4 (explicit LTS, two closures,
σ column), §3.0 G-A/G-B (tier independence + accumulator conservation —
surfaced in diagnostics, §5.4 below).

---

## AMENDMENT 2026-08-20 — expedited per-cell infiltration (phase GG0)

Per-cell 2D infiltration is pulled ahead of the groundwater work, matching
the engine's new **track I** (engine plan §5.5, steps I1–I8, decisions
D-I1…D-I6). GG0 below ships against track I and is **not blocked on the GW
kernel, the `integrated2d` component, or `model.i2d`** — the engine writes
`[2D_INFILTRATION_OPTIONS/_DEFAULTS]` and `[2D_INFILTRATION]` into the
`.2dm`/`.inp` `[2D_*]` family for v1 (engine D-I5).

Scope added by user decision, over and above the §3.2 table editor already
planned here:

1. **Meshing dialog** — assign initial infiltration parameters per region at
   generation time (§3.3).
2. **Raster / shapefile assignment dialog** — four new mapping modes:
   classified lookup, multi-field numeric, area-weighted/majority overlay,
   and natural-neighbour interpolation (§3.4).
3. **Interactive selection + attribute table** — select cells on the map and
   assign the whole method+parameter set through the attribute table or a
   selection action, as one undo entry (§3.5).

Engine data model carried in (**D-I3**): parameters resolve
`per-cell override > tag row > '*' row > none`, with provenance retained so
region-level edits round-trip compactly. The GUI must preserve that
distinction rather than flattening everything to per-cell rows — §3.3 and
§3.5 are the two places that could accidentally flatten it.

---

## 1. User-facing behavior

1. **Configuration.** The Simulation Options **2D Surface Routing** page
   grows into the integrated2d family (views over `model.i2d`):
   - **Groundwater group**: `GROUNDWATER` toggle; soil characteristic combo
     (*Russo\*/Gardner/Brooks–Corey/van Genuchten*); closure combo
     (*Auto\*/Closed form/Enslaved/σ column*); `M_LAYERS` spin;
     capillary-diffusion check; `C_GW`/`C_COL` spins; *Force closed form*
     check. An **Edit Aquifer Properties…** button opens the per-cell/tag
     table editor (§3.1).
   - **Infiltration group** (enabled only when `GROUNDWATER` is off, per
     the mutual-exclusion validation): `INFIL_STEP` field, default method
     combo (*None\*/Horton/Modified Horton/Green-Ampt/Modified Green-Ampt/
     Curve Number/Constant* — engine D-I6), destination combo
     (*Lost\** only in v1; *2D aquifer* and *Subcatchment aquifer* appear
     disabled with a "available from the groundwater release" tooltip per
     engine D-I4), **Edit Per-Cell Infiltration…** button (§3.2).
     **Ships in GG0, ahead of the groundwater group** — until then the
     group is not gated on `GROUNDWATER` because no such toggle exists yet.
   - Validation errors from the engine (ENSLAVED refused at αL>2,
     GROUNDWATER+2D_INFILTRATION on one cell, toggle/registration
     mismatch) surface in the existing options validation panel.
2. **Mesh-side editing.** Aquifer parameters and infiltration methods are
   per-triangle/tag data — editable from the map: selecting mesh triangles
   exposes an *Aquifer* page in the properties panel (adapter over
   `swmm_gw2d_get/set_cell_params`), and the mesh attribute-table tooling
   gains the aquifer/infiltration columns.
3. **Visualization** (all gated on run outputs; legacy `.out`-only runs
   show nothing new):
   - Map layers: **water-table elevation / depth-to-water scalar fill**,
     saturated-thickness fill, recharge (±, diverging palette showing
     capillary rise), infiltration rate, node-exchange markers
     (RDII in / exfiltration out, sign-colored), GW species/temperature/
     age fills (per zone).
   - **σ-column inspector**: click a closure-B cell → docked panel with
     the θ(σ) profile (plus per-layer species/temperature when transport
     is on), animated over time.
   - Time-series/profile plots: `hg`, `hu`, water-table elevation,
     recharge, node exchange per element via the standard pickers.
   - Animation: water-table surface animates with the existing 2D
     animation controls alongside surface depth.

## 2. Architecture (MVC per CLAUDE.md §5.1)

- **Model** — the engine handle via `openswmm_gw2d.h` (§8.5 of the engine
  plan) for configuration/state; the 2D HDF5 sidecar via `IMesh2DSource`
  extensions for results (`hg`, `wt_elev`, `q0`, `node_exchange`, optional
  `theta_sigma`), enumerated dynamically from the sidecar's name/units
  attributes — no hard-coded variable list (D-G1 descriptor mechanism).
- **Controller** — apply-as-you-go table models bound to the engine
  (`QualityFunctionTableModel` pattern); config persistence through the
  integrated2d component's `saveData()` to `model.i2d`
  (TRANSPORT_QUALITY_GUI_PLAN §4.1 binding rules, incl. "Revert to file").
- **Views** — options groups, two table editors, properties-panel adapter,
  new sublayers + style panel rows, σ-column inspector panel, result
  pickers. Singleton dialogs `static QPointer<T>` +
  `[[feedback_mvc_synchronized_uis]]`.

## 3. New files

```
include/ui/dialogs/aquiferparamseditordialog.h    AquiferParamsEditorDialog (§3.1)
include/ui/models/aquifercelltablemodel.h         rows: cell/tag scope, Ks, zs, θs, θr,
                                                  soil_char, closure, m, law params
include/ui/models/infiltration2dtablemodel.h      rows: cell/tag, method, params, dest (§3.2)
include/ui/panels/sigmacolumninspectorpanel.h     θ(σ) profile plot (InteractiveChartView),
                                                  species/temp overlays, time scrubber hook
include/render/sublayers/watertablesublayer.h     WT elevation / depth-to-water fill
                                                  (ScalarFillSublayer specialization w/
                                                  diverging-palette support for ±recharge)
include/render/sublayers/nodeexchangesublayer.h   signed marker symbols at coupled nodes
include/ui/properties/aquifercellpropertyadapter.h
```

§3.1/§3.2 editors follow the comprehensive-editor idiom (list/tag pane +
table, `EnumComboDelegate` columns, engine-validated cells); both write
through the C API and are reachable from the options page and the mesh
context menu. Sublayers carry `SublayerStyle` `Q_PROPERTY` structs
(`toJson/fromJson` for `.oswp`) and rows in `Swmm2DResultsStylePanel`.
Existing `VelocityVectorSublayer` gains an optional GW-Darcy-flux vector
source (edge fluxes → Perot-style cell vectors) rather than a new class.

---

### 3.2a Data model for infiltration in the GUI layer (prerequisite for §3.3–§3.5)

All three surfaces below write the same model, so it is specified once.

**Value types** — `include/mesh/meshresult.h`:

```cpp
struct InfilRow {                 // one method + its parameters + destination
    InfilMethod method = InfilMethod::None;   // None|Horton|ModHorton|GreenAmpt|
                                              // ModGreenAmpt|CurveNumber|Constant
    double p[5] = {NaN,NaN,NaN,NaN,NaN};      // legacy [INFILTRATION] positional
    InfilDest dest = InfilDest::Lost;         // Lost only in v1 (engine D-I4)
};
struct InfilDefaultRow { QString tag; InfilRow row; };   // tag "*" = mesh-wide
```

These live **on `MeshResult`, not on `MeshTriangle`**:

```cpp
QVector<InfilDefaultRow>  infilDefaults;   // ordered; '*' row may appear anywhere
QHash<int, InfilRow>      infilOverrides;  // sparse, keyed by triangle index
```

This follows the precedent already documented at `meshresult.h:58-65` for
`CellCoupling` — rows that are sparse or multi-valued live on `MeshResult`
rather than bloating the per-triangle POD. It also directly preserves engine
**D-I3**: the GUI never flattens tag inheritance into per-cell rows, so the
writer emits the same compact file the engine parses.

**Resolution helper** — `mesh::resolveInfil(const MeshResult&, int tri)` →
`{InfilRow row, bool isOverride, QString sourceTag}`. Every read path (table
cell rendering, map styling, property panel) goes through it so the
"inherited vs overridden" distinction is displayed consistently — inherited
values render in the muted/italic style the property panels already use for
defaults, overridden values render normally.

**Persistence — the hazard that must be handled first.** `[2D_TRIANGLES]`
rows are positional (`V1 V2 V3 MANNINGS_N [INIT_DEPTH] [TAG]`, numeric 5th
token = `INIT_DEPTH`), so infiltration must **not** be appended there. Three
edits are mandatory or the data is silently lost on every save:

1. `mesh::InpMeshWriter` — add `formatInfilDefaults()` / `formatInfilOverrides()`
   and emit `[2D_INFILTRATION_OPTIONS]`, `[2D_INFILTRATION_DEFAULTS]`,
   `[2D_INFILTRATION]`.
2. Add those three section names to the `ours` strip list in
   `stripExistingMeshSections` (`src/mesh/inpmeshwriter.cpp:203-218`, list
   literal :207-215) **and** to `patchAttributeSections()`
   (`include/mesh/inpmeshwriter.h:271`, `src/mesh/inpmeshwriter.cpp:804`).
   The save path at `src/swmmvisprojectwindow.cpp:1330-1440` snapshots the
   `.2dm`, lets the engine clobber it, restores the snapshot, then re-emits
   GUI edits through `patchAttributeSections()`. A section missing from that
   function is discarded on every save — the code comment at :1372-1377
   documents exactly this failure mode for vertex-Z.
3. `mesh::InpMeshReader::read()` — parse the three sections into
   `infilDefaults`/`infilOverrides`.

A round-trip unit test (author → save → reload → compare, including a mesh
with tags, a `*` row and sparse overrides) is the GG0 gate for this.

### 3.3 Mesh generation dialog — region-level initial parameters

**Files:** `include/ui/dialogs/meshgenerationdialog.h`,
`src/ui/dialogs/meshgenerationdialog.cpp` (`buildUi()` spans :2368–:3101).

Today the dialog has an *"Initial cell values"* group (`QGroupBox` title at
`meshgenerationdialog.cpp:2987`) with two scalars —
`m_manningsValueSpin` (:2991) → `PipelineInputs::manningsN` and
`m_initDepthSpin` → `PipelineInputs::initDepth` — stamped onto every triangle
at `meshgenerationdialog.cpp:2268-2269`. There is **no per-region defaults
concept anywhere in the GUI today**; regions exist only as
`mesh::RegionMarker{xy, attribute, maxArea, tag}`
(`include/mesh/meshgenerator.h:53-59`), whose `tag` is mapped through
`MeshGenerator::m_triangleTagByRegionId` (:155) onto `MeshTriangle::tag`.

**Change:** grow that group into a **region defaults table** — one row per
region tag plus a `*` fallback row — with columns *Region · Manning's n ·
Initial depth · Infiltration method · parameters · destination*. Rows are
seeded from `PipelineInputs::subcatchSeeds`, which `collectInputs()` fills at
`meshgenerationdialog.cpp:3857` when the *"Subcatchments → triangle regions"*
checkbox `m_includeSubcatch` (:2509) is on, and which the worker turns into
`in.regionMarkers` at :661-667. When no region source is selected the table
degenerates to the single `*` row and the dialog behaves exactly as it does
today.

Output: `PipelineInputs` gains `QVector<InfilDefaultRow> infilDefaults;` and
the pipeline writes it to `MeshResult::infilDefaults` — it does **not** stamp
per-cell rows. Manning's n and initial depth keep their existing per-triangle
stamping so nothing about the current behaviour moves; only the infiltration
block uses the inheritance model.

Two constraints inherited from the dialog's architecture:

- `collectInputs()` runs on the GUI thread and `runMeshPipeline()` on a
  `QtConcurrent::run` worker guarded by `QFutureWatcher<PipelineResult>` with
  `QPromise::isCanceled()` checkpoints. The table is read in `collectInputs()`
  and its contents copied by value into `PipelineInputs` — the worker must
  never touch the widget.
- `RegionMarker::maxArea` is silently inert when a refinement size-function
  hook is installed (comment at `meshgenerationdialog.cpp:855`). Unrelated to
  this change, but the new table sits next to it, so do not wire anything to
  `maxArea` expecting it to bind.

### 3.4 Raster / shapefile assignment — four new mapping modes

**Files:** `include/ui/dialogs/meshattributeassigndialog.h`,
`src/ui/dialogs/meshattributeassigndialog.cpp`. Launched from
`src/swmmvis.cpp:1116-1160` (`actionMeshAssignFromRaster`,
`actionMeshAssignFromVector`).

Current behaviour: one numeric raster band or one numeric vector field →
one numeric `mesh::cellParamSpecs()` target (:93), sampled at the triangle
**centroid only** (`centroidsFor()` :320), bilinear for rasters via
`mesh::DTMSampler` (`sampleRaster()` :345, sampler at :366) and
first-containing-polygon for vectors via `GISVectorLayer::identifyAt`
(`sampleVector()` :403, `identifyAt` :438, "first containing polygon wins"
:456). Scope filter `scopeTriangles()` :296. Applied through
`mesh::pushCellParamEdits` (:513) as one undo entry (`onApply()` :491).

All four modes below were requested; they are independent and can land in the
order listed.

**(a) Classified lookup — the one that matters most for infiltration.**
A categorical key field (landuse code, hydrologic soil group) maps to a whole
`InfilRow` through an editable lookup table: *key value → method + parameters
+ destination*, with an "unmatched" fallback row. Curve Number needs a
**two-key** cross-lookup (landuse × HSG), so the mode accepts an optional
second key field and the table becomes a matrix. Lookup tables save/load as
CSV so an agency's standard table is reusable across projects. This is how
infiltration parameters are actually assigned in practice — a single numeric
field cannot express a method plus five parameters.

**(b) Multi-field numeric.** N raster bands or N vector fields → N parameters
in one pass (e.g. `f0`, `fmin`, `decay` together), still one undo entry. A
straightforward generalization of the existing path: the target combo becomes
a target *list* with a field/band pairing per row.

**(c) Area-weighted / majority overlay.** Replaces centroid point-sampling.
For polygons, majority = the feature covering the largest share of the
triangle; for continuous rasters, an area-weighted mean. `identifyAt` cannot
do this. Recommended implementation: `OGRGeometry::Intersection` between the
triangle ring and candidate features (GDAL/OGR is already a dependency, so
this needs no new third-party code) for the vector case; for rasters, v1
accumulates pixels whose centre falls inside the triangle — cheap, no
clipping — with exact pixel-clip recorded as a follow-up. Mode selection
matters: majority is correct for categorical layers, area-weighted mean for
continuous ones, and offering the wrong one silently produces plausible
nonsense — so the mode is bound to the layer's declared type with an explicit
override.

**(d) Natural-neighbour interpolation.** For scattered point sources (soil
samples, borehole logs) rather than coverages. Reuse
`mesh::NaturalNeighbourInterpolator` (`include/mesh/naturalnbinterpolator.h`,
`Variant::{Sibson, Laplace}`) — the same class the generation dialog already
offers for elevation via `ElevInterpMethod`/`NNVariant`, so the UI wording and
the variant choice should match that page rather than inventing new labels.

**Write target (preserves D-I3).** When the source layer carries a field that
matches region tags, the dialog offers *"Write as: per-cell overrides |
region defaults (by tag)"*. Region-defaults writing is what keeps a
landuse-derived assignment editable as regions afterward instead of freezing
it into N per-cell rows. Default to per-cell overrides only when no tag
correspondence exists.

**Threading (required, not optional).** `onApply()` is synchronous and
blocking today — acceptable for a centroid point-sample, not for overlay
clipping or NN interpolation over a large mesh. Move sampling to the
generation dialog's proven pattern: `QtConcurrent::run` +
`QFutureWatcher` + `QPromise::isCanceled()`, with the existing Preview path
reporting counts from the same worker. GDAL/OGR handles must be re-opened by
path on the worker thread — the generation dialog does this deliberately and
the same rule applies here.

### 3.5 Attribute table + interactive map selection

**Selection already works and needs no new tooling.** `MapToolPick2DCells`
(`include/map/tools/maptoolpick2dcells.h`) supports box and lasso picking
(`Mode{Box, Lasso}`, **B**/**L** to switch) plus single-click; selection lands
in `SelectionManager` (`include/selection/selectionmanager.h`) as
`SWMMObjectRef::ObjectType::MeshCell` (= 20, :67) refs encoded by
`mesh::MeshObjectRef` (`mesh::<layerKey>#c<triIdx>`);
`AttributeTablePanel::onSelectionManagerChanged()`
(`src/ui/panels/attributetablepanel.cpp:1549`) already syncs the table both
ways behind the `m_applyingFromBus` reentrancy guard. Preserve the
right-click-on-*release* handling documented at `maptoolpick2dcells.h:130-136`
(`m_pendingRightPlot` :137) — it works around an AppKit modal/implicit-grab
freeze.

**What must be built:**

**(1) `CellParamSpec` needs an enum kind.** `mesh::CellParamSpec`
(`include/mesh/meshcellparams.h`, table in `src/mesh/meshcellparams.cpp`)
describes a single `double` with `min/max/step/decimals`. The infiltration
*method* is an enumeration, so the struct gains
`Kind kind = Kind::Double;` plus `QStringList enumLabels`, and the table model
picks `EnumComboDelegate` for `Kind::Enum` columns. This is worth doing
properly rather than encoding the method as a magic double, because the
registry is the single insertion point that feeds the attribute table, the
`MeshEditingToolbar` param combo, the property adapter, and the §3.4 assign
dialog at once.

**(2) Register the infiltration columns.** One `Kind::Enum` spec
`infil.method`, then named parameter specs — `infil.f0`, `infil.fmin`,
`infil.decay`, `infil.dryTime`, `infil.Fmax`, `infil.suction`, `infil.Ks`,
`infil.IMD`, `infil.CN`, `infil.regen`, `infil.rate` — each **masked by the
row's method**: cells whose method does not use a parameter render `—` and
refuse edits. This is exactly the idiom `MeshAttributeTableModel` already uses
for BC columns on interior edges (`rowIsBoundaryEdge()`,
`include/ui/panels/meshattributetablemodel.h:125`, used in `flags()` at
`meshattributetablemodel.cpp:671`), so it needs no new concept.

Add the matching read/write cases routing through `mesh::resolveInfil`
(§3.2a): `mesh::cellParamValue()` is public in
`include/mesh/meshcellparams.h`, but its write-side counterpart
`applyCellParam()` is **file-local in an anonymous namespace** at
`src/map/meshcommands.cpp:23` — it is not a `mesh::` symbol despite the
comment at `include/map/meshcommands.h:37` calling it one. Either extend it
in place or promote it to a real header symbol first; do not write code
against `mesh::applyCellParam` expecting it to resolve.

While in that table: the five `gw.*` disabled placeholders cite
`plans/TWO_ZONE_GROUNDWATER_FV_INTEGRATION_PLAN.md`, which is superseded —
repoint the comment at `TWO_ZONE_GROUNDWATER_EXPLICIT_LTS_PLAN_2026-08-15.md`.

**(3) A compound undo command for set-the-whole-row edits.**
`MeshSetTriangleAttributeCommand` (id 43, `include/map/meshcommands.h:85`)
carries one key and parallel new/old `double` vectors — fine for
single-column table edits, wrong for "assign this method and its five
parameters to 400 selected cells" as one atomic action. Add
`MeshSetTriangleInfilCommand` with **id 49** holding the target
triangles, the new `InfilRow`, and per-triangle old state. **The old state
must record whether the cell was previously *inheriting* from a tag, not just
its resolved values** — otherwise undo silently converts an inherited cell
into a materialized override with identical numbers, and the next region-level
edit stops reaching it. This is the single subtlest correctness point in GG0.
Expose it as `mesh::pushCellInfilEdit(layer, tris, row, canvas)` beside the
existing `pushCellParamEdit`/`pushCellParamEdits` helpers so all four editing
surfaces keep funnelling through one place.

**(4) An assign-to-selection action.** `MeshEditingToolbar` already has
`m_cellParamCombo` + `m_cellValueSpin` for one-param-to-many-cells. Add an
*"Assign Infiltration to Selection…"* action (toolbar + mesh context menu)
opening a small modal with the method/parameter form, applying via
`pushCellInfilEdit` to the current `SelectionManager` cell refs filtered to
the active layer's `layerKey` — the same filtering
`MeshEditingToolbar::onSelectionChanged()` does at
`src/ui/toolbars/mesheditingtoolbar.cpp` (`onSelectionChanged()` opens :1053;
`layerKey` filtering :1061-1090). It also offers *"apply to
region tag instead"* when every selected cell shares one tag, which is the
in-map route to editing a default rather than creating overrides.

**(5) Styling.** Per `MESH_BC_AND_CELL_ATTRIBUTE_STYLING_PLAN_2026-08-19.md`,
add categorical fill by `infil.method` and graduated fill by any numeric
infiltration parameter, so an assignment can be checked visually before a run.

### 3.6 New/changed files for GG0

```
include/mesh/meshresult.h                 + InfilRow, InfilDefaultRow, InfilMethod,
                                            InfilDest; MeshResult::infilDefaults/
                                            infilOverrides  (§3.2a)
include/mesh/meshcellparams.h             + CellParamSpec::Kind + enumLabels;
                                            infil.* spec registrations
include/mesh/meshinfil.h                  NEW — mesh::resolveInfil(), method↔param
                                            masks, CSV lookup-table load/save
include/mesh/inpmeshwriter.h              + [2D_INFILTRATION*] emit + strip list
                                            + patchAttributeSections coverage
include/mesh/inpmeshreader.h              + [2D_INFILTRATION*] parse
include/map/meshcommands.h                + MeshSetTriangleInfilCommand (id 49 —
                                            47/48 are taken), pushCellInfilEdit()
src/map/meshcommands.cpp                  applyCellParam() (file-local, :23)
                                            extended or promoted to a header symbol
include/ui/models/infiltration2dtablemodel.h   (already listed in §3) — now also
                                            backs the §3.3 region-defaults table
include/ui/dialogs/infilassigntoselectiondialog.h  NEW — §3.5(4)
include/ui/dialogs/meshgenerationdialog.h  region-defaults table (§3.3)
include/ui/dialogs/meshattributeassigndialog.h  four mapping modes + threading (§3.4)
```

## 4. Results & descriptors

GW variables enter every picker through the D-G1 dynamic descriptors
(kind = 2D-cell / 2D-node-exchange, label+units from sidecar attributes).
`theta_sigma` renders only in the σ-column inspector (not a map theme).
Statistics dashboard and tabular results enumerate the new descriptors like
any other. Mass-balance dashboard gains the `gw_*_2d` ledger rows
(recharge/lateral/deep/node/dunne/caprise) and the `2d_infil` row.

### 5.4 LTS diagnostics (G-A/G-B surfacing)

A diagnostics tab (existing run-statistics dialog) charts the
`swmm_gw2d_get_tier_histogram` occupancy — the user-visible proof that GW
cells sit on slow tiers and are not being dragged by the surface model —
plus the per-channel conservation residuals from the mass-balance API.

## 6. Implementation phases

```
GG0  EXPEDITED INFILTRATION (§3.2a–§3.6) — ships before GG1, tracks engine
     track I. Sub-phases, each independently gated:
     GG0a  MeshResult value types + mesh::resolveInfil + reader/writer/
           patchAttributeSections coverage.        [after engine I2]
           → gate: author→save→reload→compare round-trip incl. tags, '*'
             row and sparse overrides; a save cycle loses nothing.
     GG0b  CellParamSpec::Kind::Enum + infil.* columns in the attribute
           table + method-masked parameter cells.  [after engine I2]
           → gate: masked cells refuse edits; inherited vs overridden
             render distinctly; edits reach the layer via the existing funnel.
     GG0c  MeshSetTriangleInfilCommand + pushCellInfilEdit + "Assign
           Infiltration to Selection…" from map selection.
           → gate: undo of an assignment over an inheriting cell restores
             INHERITANCE, not a materialized copy (§3.5(3)); 400-cell
             assignment is one undo entry.
     GG0d  Mesh generation dialog region-defaults table (§3.3).
           → gate: no region source selected ⇒ behaviour identical to today;
             region rows land in infilDefaults, not per-cell rows.
     GG0e  Assign dialog: classified lookup (+ CN two-key), multi-field
           numeric, area-weighted/majority overlay, natural-neighbour;
           worker-thread + cancel (§3.4).          [after engine I6]
           → gate: each mode verified against a hand-computed fixture;
             cancel mid-run leaves the mesh untouched; large-mesh run does
             not block the UI.
     GG0f  Categorical/graduated styling by infil.* + engine-bound config
           via openswmm_infil2d.h + docs/CHANGELOG. [after engine I6, I8]
GG1  Options groups + validation surfacing + PreferencesManager defaults +
     hydration-contract rows.                      [after engine step 1]
GG2  Aquifer + infiltration table editors + properties-panel adapter +
     mesh attribute-table columns.                 [after engine steps 1, 11b, 19]
     (infiltration portions land in GG0; GG2 adds the aquifer half and the
      GROUNDWATER/2D_INFILTRATION mutual-exclusion surfacing)
GG3  Result sidecar enumeration + water-table/recharge/infiltration
     sublayers + node-exchange markers + .oswp style round-trip.
                                                   [after engine §8.5 outputs]
GG4  σ-column inspector + animation hook.          [after engine GW_DETAILED_OUTPUT]
GG5  Plots/tabular/statistics descriptors + mass-balance rows + LTS
     diagnostics tab.                              [after engine steps 15, 19]
GG6  GW transport variables (species/temp/age per zone) in map + inspector.
                                                   [after engine T7 / S7]
GG7  docs/manual page + CHANGELOG (CLAUDE.md §5.2).
     → each phase: QtTest per new model/panel; artifacts to the repo
       test-output directory (CLAUDE.md §4.1).
```

## 7. Risks / open notes

**GG0-specific:**

- **Silent loss on save is the top risk.** Any `[2D_INFILTRATION*]` section
  missing from the strip list (`src/mesh/inpmeshwriter.cpp:206-216`) *or*
  from `patchAttributeSections()` disappears on every project save, because
  the save path deliberately restores a pre-engine-write `.2dm` snapshot and
  re-emits GUI edits (`src/swmmvisprojectwindow.cpp:1330-1440`). GG0a's
  round-trip gate exists specifically to catch this.
- **Undo must restore inheritance, not values** (§3.5(3)). The failure is
  invisible — numbers look right, but the cell has quietly become an override
  and stops tracking its region.
- **Mode/layer-type mismatch in §3.4(c)**: area-weighted mean over a
  categorical landuse raster produces plausible, meaningless numbers. Bind
  the mode to the layer's declared type and require an explicit override.
- **Double-counting is not prevented in v1** — engine track I ships without
  subcatchment/mesh arbitration (engine §9, note at I8). If the GUI lets a
  user mesh an area whose subcatchment still has `[INFILTRATION]` configured,
  water infiltrates twice. Recommend a non-blocking warning banner on the
  mesh page until engine G1 step 8 lands; the engine-side question of whether
  this should be a validation warning is still open.
- `meshgenerationdialog.cpp` is 4142 lines with a ~734-line hand-coded
  `buildUi()` (:2368-3101) and no `.ui` form; the region-defaults table adds
  to that. Extract the new group into its own widget class rather than
  growing `buildUi()` further.
- **Undo-command id collision:** ids 0,1,10-17,21-23,41-48 are taken
  (`include/map/mapundostack.h` — 47 is `AssignSubcatchGagesCommand` :577,
  48 is `ConfigureGageCommand` :627). GG0c uses **49**; re-check before
  implementing in case another plan claims it first.

**GW phases:**

- `theta_sigma` output volume (m×Ntri per step) — inspector should lazy-load
  per-cell series from the sidecar, never the full variable.
- Multi-engine degradation: all groups hidden against engines without
  `swmm_gw2d_enabled` (probe pattern per MULTI_ENGINE_VERSION_SUPPORT plan).
- Legacy `[2D_*]`-embedded projects: editors operate only once the
  integrated2d component is registered; the one-click externalize migration
  (TRANSPORT_QUALITY_GUI_PLAN §4.1) is the on-ramp.
- Diverging-palette recharge rendering needs a signed-scale option in the
  scalar-fill style — small render change, flagged for the style-panel
  owner.
