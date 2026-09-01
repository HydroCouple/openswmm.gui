# Flow Balance Tracing — GUI Plan (2026-08-03)

**Status:** Proposal — for review. No implementation yet.
**Companion:** `openswmm.engine/plans/FLOW_TRACE_ENGINE_PLAN_2026-08-03.md` (engine computes and persists; the GUI orchestrates, renders, and styles).
**Decisions (user-approved 2026-08-03):**
- Engine C API (`openswmm_trace.h`) does the precompute, sidecar (CF-HDF5 `<out>.trace.nc`), and sparse trace math.
- The existing placeholder actions `actionFlowBalanceDownstream/Upstream` and `actionTravelTimeDownstream/Upstream` (QMessageBox implementations at `src/swmmvis.cpp:6460–6535`) are **replaced** by this feature.
- V1 averages over the whole run; time windows later.
- Visualization must include **dominant flow-direction arrows** driven by average flows, configurable in the style.

## 1. User-facing behavior

- Right-click a node or link on the map (`OpenSWMMVisMapToolSelect::showContextMenu`) → new submenu **"Trace Flow ▸ Downstream from here / Upstream to here"** (multi-results-layer variant mirrors the existing Plot Time Series two-level pattern).
- Ribbon (Analysis tab): the four existing actions are rewired. Flow Balance ↓/↑ run a trace seeded from the current selection on the active 1D results layer (first selected node/link; multiple seeds: run per seed is out of scope for v1 — use the first, log a note). Travel Time ↓/↑ run the same trace but apply the travel-time-forward style preset (see §4). Old QMessageBox code and `Subnetwork`/`buildSubnetwork` helpers in `src/swmmvis.cpp` are removed (orphans of this change only).
- First trace on an output file triggers the engine precompute: status-bar progress bar shows determinate progress with stage text, Message Log receives start/finish/stage entries. Subsequent traces load the sidecar instantly.
- Results appear as **two new stylable sub-layers** of the SWMM 1D Outputs layer: **"Trace — Downstream"** and **"Trace — Upstream"** (empty/hidden until a trace runs). Default: link thickness = flow fraction, color bands = travel time; swappable to vice versa in the style dialog. Dominant-direction arrows overlaid per link, oriented by sign of average flow.
- Selecting a new seed replaces that sublayer's content. Sublayer context menu in the layer tree gains **"Clear Trace"**.

## 2. Architecture (MVC per CLAUDE.md §5.1)

- **Model:** `TraceResultModel` (new, `include/output/traceresultmodel.h`) — holds one trace: seed ref, direction, per-link fraction/time, per-node fraction/time, mapped **model-layer row indices** (resolved by name from engine indices, as the codebase already mandates), plus the per-link dominant-direction sign and average-flow magnitude for arrows. Owned by `SWMMResultsLayer` (one per direction). Emits `changed()`.
- **Controller:** `TraceController` (new, `include/output/tracecontroller.h`) — owns the `SWMM_Trace` handle lifecycle for a results layer, runs open/trace off-thread, marshals progress to the GUI. Both ribbon and context menu call it; both sublayers observe the model → views stay synchronized.
- **Views:** the two sublayers (§3), legend rows, and Message Log summary line (totals: fraction reaching outfalls, lost to flooding, max travel time — the useful part of the old QMessageBox, now logged instead of modal).

## 3. Sublayers

New `TraceSublayer : public OpenSWMM::Render::ISublayer` (LineKind) + `TraceSublayerStyle : SublayerStyle`, in `include/render/sublayers/trace/` + `src/render/sublayers/trace/`, following the 2D-results heterogeneous-sublayer precedent (`IsolineSublayer` etc.).

- Instantiated (×2) in the `SWMMResultsLayer` ctor; appended to `sublayers()` / `m_sublayerOrder`; `isDynamic() == false` (static overlay — no animation invalidation).
- Rendering goes through the existing QGraphicsScene path (`populateScene`/`restyleScene`): trace items are separate `QGraphicsItem`s drawn above the feature sublayers (cosmetic pens via `makeCosmeticPen`), geometry taken from the model layer's link polylines; node fractions optionally as scaled markers.
- Free-of-charge integrations (already generic over `ISublayerHost`): layer-tree third-tier rows + visibility/opacity (`LayerTreeModel::rebuildSublayerRows`), paint-order reorder, JSON persistence (`saveSublayersToJson`), legend (`aggregatedLegendSymbolItems`), style dialog (§4). Add an icon branch in `iconAliasForSublayer` and a "Clear Trace" entry in the sublayer context menu (`layertreepanel.cpp:1664–1727`).

### `TraceSublayerStyle` (Q_PROPERTYs → auto-generated editor)

```
widthBy        enum { FlowFraction, TravelTime }      default FlowFraction
colorBy        enum { TravelTime, FlowFraction }      default TravelTime
minWidthPx, maxWidthPx   double                       width mapping range
widthScale     enum { Linear, Sqrt, Log }             fraction→width curve
colorRamp      ramp ref                               travel-time bands
bandCount      int                                    classification bins (IntervalBinner)
timeUnit       enum { Minutes, Hours, Days }          display/legend unit
minFraction    double                                 cutoff: hide links below threshold
showNodes      bool + nodeMarker size/color           node fraction markers
labelLinks     bool                                   fraction/time labels (LabelConfig)
-- arrows (group "Arrows") --
showArrows     bool                                   default true
arrowSizeBy    enum { Fixed, AvgFlowMagnitude }       configurable per user requirement
arrowLengthPx, arrowWidthPx, arrowColor, arrowSpacing
```

`widthBy`/`colorBy` are independent enums, so "vice versa" styling needs no renderer changes — the sublayer computes both channels itself from the `TraceResultModel` (it does not go through `GraduatedRenderer`, sidestepping its single-classify-attribute limitation). `toJson()/fromJson()` for `.oswp` round-trip and Cancel rollback. Trace *results* are session-transient; only style + visibility persist (documented in the manual page).

Style dialog: push two `LayerStyleSubject`s (section **"Trace"**) in `SWMMResultsLayer::styleSubjects()`; `Q_CLASSINFO("group:…")` tabs: General / Width & Color / Arrows / Labels.

## 4. Actions & wiring

- `TraceController::runTrace(direction, seedRef)`:
  1. Resolve active results layer (`SWMMVisProjectWindow::activeResultsLayer()`); no layer → log warning, return.
  2. Off-thread (`QtConcurrent::run` + `QPromise`, per the mesh-generation template at `meshgenerationdialog.cpp:100–120`): `swmm_trace_open(model, outPath, progressCb, …)` — progressCb forwards fraction+stage via queued signal — then `swmm_trace_downstream/upstream`.
  3. GUI thread: populate `TraceResultModel` (name→row mapping via `SWMMModelLayer`), make sublayer visible, `emit repaintRequested()`, log summary.
  - Model handle pairing: `swmm_trace_open` needs the *matching* open model for first-time precompute; `SWMMResultsLayer` already pairs with `m_modelLayer` — pass its engine handle. Sidecar-only reopen needs no model.
- Progress: `beginFileOpen`-style idiom — determinate `mProgressBar` (0–100 via `updateSimulationProgressBar`-adjacent path or a dedicated slot), stage text in status bar ("Precomputing average flows… 40%", "Writing trace sidecar…"), `onLogMessage` Information entries at start/stages/finish, Error on failure. (Note: "taskbar" in this codebase = the status-bar progress widget; no OS-level taskbar API exists.)
- Ribbon rewiring in `src/swmmvis.cpp:3662–3675`: FlowBalance↓/↑ → `runTrace(dir, firstSelected)` with fraction-forward defaults; TravelTime↓/↑ → same trace, then set the sublayer's `widthBy=TravelTime, colorBy=FlowFraction` preset. Fix the `TraveTimeUpstream` icon-alias typo only if touched by this wiring (it is — the four actions are re-labelled per `UI_REDESIGN_ITER3_WORKPLAN.md:61–62`).
- Context menu: new block in `maptoolselect.cpp` `showContextMenu` next to Plot Time Series, enabled only when the hit element is a Node or Link and ≥1 results layer is registered in `OutputStatsRegistry`.

## 5. Implementation phases

```
1. TraceController + engine-API wrapper, async open + progress→statusbar/log
   → verify: first-open precompute shows progress; second open loads sidecar instantly (log timestamps)
2. TraceResultModel + name→row mapping
   → verify: headless gtest with fixture .out + model (tests/gui/test_traceresultmodel.cpp)
3. TraceSublayer + TraceSublayerStyle + scene painting (width/color/threshold)
   → verify: manual on example model; style JSON round-trip gtest
4. Dominant-direction arrows (sign of avg flow; Fixed vs AvgFlowMagnitude sizing)
   → verify: reversed-flow fixture link draws arrow opposite to drawn geometry direction
5. Context menu + ribbon rewiring; delete old onFlowBalance/onTravelTime + helpers
   → verify: all four ribbon actions and both context entries drive the sublayers; grep confirms no orphaned helpers
6. Layer-tree icon, "Clear Trace", legend rows, style-dialog subjects, .oswp persistence of style
   → verify: save/reopen project preserves style+visibility; legend shows band rows
7. docs/manual page (limitations: steady-average approximation, dir=0 exclusions) + CHANGELOG.md
   → verify: page builds in Doxygen manual
```

All test artifacts written under the repo's test-output directory, not temp folders (CLAUDE.md §4.1).

## 6. Risks / open notes

- **Model↔output mismatch:** if the loaded model doesn't match the `.out` (renamed elements), name resolution drops unmatched elements — log a warning with counts rather than failing.
- **Sublayer count assumptions:** `SWMMResultsLayer` currently sizes some arrays to `NumCategories`; the two trace sublayers are additional non-feature sublayers — audit `sublayers()` consumers (legend, tree, persistence) like the 2D layer already exercises.
- **Multiple seeds / multiple simultaneous traces** (compare two sources): out of scope v1; the model/sublayer split makes adding N-trace later straightforward.
- **Engine built without HDF5:** trace API returns a distinct error; GUI logs a clear message and disables the menu entries for that session.
