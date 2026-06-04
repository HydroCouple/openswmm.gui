# Styling System — Gap Review (for implementation review)

**Status:** ⏳ Review — no code changes in this document
**Date:** 2026-05-31
**Scope:** End-to-end symbology/styling for all layer types — **static** model/GIS
layers **and** dynamic **SWMM output/results** layers (whose appearance must
animate over timesteps). Builds on `SYMBOLOGY_MVC_ARCHITECTURE_AND_GAPS.md`
(MVC) and `VISUALIZATION_STYLING_OVERHAUL_PLAN.md` (capabilities).

---

## 0. Where we are now (works / landed)

- **Model layer (static), single source of truth:** renderer is canonical from
  all edit paths; struct is a consistent derived cache; dialog opens on current
  values and edits persist on close (M1/M2 + onCancel fix).
- **Painter renderer-driven per feature** for the CPU path: color/size/offset +
  per-feature marker **shape** (M3). GL paint path still reads the struct.
- **Editors:** dedicated Point/Line/Polygon + raster/mesh/2D adapter editors
  (SE.1–SE.4); registry tail-match fix; generic grid removed from symbology.
- **Kind/sub-layer tree** is the symbology nav (rules-list editor retired);
  feeds each kind's `Rule` to the renderer editors.
- **Marker shapes:** canonical set expanded to 19, picker renders via the
  canonical draw helper for canonical-enum editors.
- **Color-read tolerance:** SWMM struct readers accept both QColor-variant and
  hex, and `"fillColor"`/`"color"` keys (just landed).

---

## 1. Cross-cutting gaps (affect every layer)

| # | Gap | Evidence | Severity | Fix |
|---|---|---|---|---|
| X1 | **Two color encodings + inconsistent keys** coexist: adapters write `"fillColor"` + QColor-variant; specs/converters variously read `"color"` + hex or `.value<QColor>()`. Patched defensively for the SWMM struct path only. | `symbolstyleadapter.cpp` `kFillColor="fillColor"`; `markersymbollayer.cpp` reads `"fillColor"`/`.value<QColor>()`; `styleFromElementSymbol` writes `"color"` hex; `linesymbollayer` reads `"color"` | High | **Canonicalize**: one key per attribute + one encoding (recommend hex string for JSON round-trip), applied across adapters, specs, converters, and `.oswp` IO. Removes the whole class of "edit doesn't reflect" bugs. |
| X2 | **Line (link) color path** likely mismatched the same way nodes were. Lines map editable color → `outlineColor` (pen); `LineSymbolStyleAdapter` key vs `LineSymbolLayerSpec` read vs `elementSymbolFromStyle` (maps `"color"`→`fillColor`, not the pen). | `applyLineSymbolToElement` maps `spec.color`→`sym.outlineColor`; `elementSymbolFromStyle` maps fill→`fillColor` | High | Verify/fix link color end-to-end (adapter key → spec → struct pen mapping), same tolerant treatment as nodes. |
| X3 | **GL paint path still reads the struct** (`swmmlayerglrenderer.cpp`), so the renderer-drives-paint switch (M3) is only half done; GL diverges from CPU. | `SWMMElementSymbol` used in `swmmlayerglrenderer.cpp` | Med | Route GL paint through the same per-feature resolution (M3-cleanup), or delete the struct type once both paths read the renderer (111 refs). |
| X4 | **Legend dock can't edit model-layer symbology** — `featureRendererFor()` returns null for `SWMMModelLayer` (it's multi-kind). Legend↔model edit/sync is broken for the model layer. | `legendclasseditcommands.cpp:28-31` has no `SWMMModelLayer` case | Med | Route legend per-class edits to the per-kind renderer; make legend a true observer/editor of the one model. |
| X5 | **No undo/redo for symbology; `.oswp` persists the lossy struct subset, not the renderer model.** | M6 in MVC doc (not started) | Med | Command stack on the renderer model; serialize the whole model. |
| X6 | **Opacity** (layer + per-sublayer) verify end-to-end: editor → effective alpha (parent × sublayer) at paint. Symbol-level opacity (SymbolStyle.opacity) not flowed to the struct/paint. | VS.8 wiring done for tree; symbol opacity not in struct | Low/Med | Confirm paint multiplies; surface symbol opacity. |

---

## 2. Static layers

### 2a. SWMM model layer
| # | Gap | Severity | Fix |
|---|---|---|---|
| M-1 | Link color (X2) — confirm conduits/pumps/orifices/weirs/outlets reflect color/width/dash/arrows. | High | Per X2. |
| M-2 | `PolygonSymbolStyleAdapter` lacks a **fill-style** (hatch) knob — VS.2b added it to `PolygonFeatureSublayerStyle` (results), not the model adapter. Subcatchment hatch fills not editable on the model layer. | Low | Add `fillStyle` to the polygon adapter + struct + paint. |
| M-3 | Struct type deletion + GL path (X3). | Med | M3-cleanup. |

### 2b. GIS vector
| # | Gap | Severity | Fix |
|---|---|---|---|
| G-1 | Uses its **own `GISVectorSymbol::MarkerShape`** (≈5 values) + own painter — not the canonical 19 shapes; editor still on `populateDefault`. | Med | Migrate GIS vector to canonical `MarkerShape` (enum + painter + picker), or map. |
| G-2 | Own symbol struct/dialect (`GisVectorSymbolAdapter`) parallel to the renderer model — same dual-truth smell as the SWMM struct. | Med | Fold into the MVC single-source pattern. |

### 2c. Raster / DTM
| # | Gap | Severity | Fix |
|---|---|---|---|
| R-1 | `GISRasterLayer::warpToCanvas` still colourises via `m_colorRamp`, **not** `rasterRenderer()` — so the new `PalettedRasterRenderer` (VS.6) and single-band renderer don't drive standalone rasters. | High | Route the warp colourisation through `rasterRenderer()->colorForValue()`. |
| R-2 | **No raster Symbology UI panel** (ramp + classification + hillshade controls; or palette + class table for categorical). Hillshade enable/params have no UI. | High | Build the raster Symbology panel; wire `setHillshadeEnabled/Params`. |
| R-3 | Hillshade azimuth orientation unverified vs a known DEM. | Low | Verify on build. |

---

## 3. SWMM OUTPUT / RESULTS animation styling — **the priority gap area**

Outputs are the weakest part: the styling knobs exist as state but are **not
wired to paint, not editable in UI, and don't update with the time axis.**

### 3a. 1D results (`SWMMResultsLayer`)
| # | Gap | Evidence | Severity | Fix |
|---|---|---|---|---|
| O1-1 | **No UI to choose the output variable** (depth/flow/velocity/head…). It's a `Q_PROPERTY` (`variable`) set only programmatically. | `swmmresultslayer.h:119`; no dialog references it | High | Add a variable selector to the results Symbology surface (per kind or per layer). |
| O1-2 | **Renderer not wired to paint** — paint reads `m_colorRamp`/`m_variable`; `renderer()` is "write-only" until the deferred 8.13.6.4 refactor. Per-kind override caches partly bridge it, but the layer renderer/ramp is the actual source. | `swmmresultslayer.cpp:1382-1385` ("renderer essentially write-only") | High | Make results paint consult the renderer (consistent with model M3). |
| O1-3 | **Range/classification frozen after first frame** — `m_colorRamp` min/max set once (or via one-shot `autoStretchColorRamp()`); bins computed once. `rebinDynamicRulesIfNeeded()` exists but is **never connected**. | `swmmresultslayer.cpp:1700` (dormant) | High | Add a **range mode**: fixed-over-run (default) vs per-frame auto-stretch vs fixed-user-range; wire dynamic re-bin to `currentTimeStepChanged`. |
| O1-4 | **No per-variable ramp** — all variables share one ramp; can't assign e.g. a diverging ramp to velocity and sequential to depth. | single `m_colorRamp` | Med | Per-variable ramp + classification, persisted. |

### 3b. 2D results (`SWMM2DResultsLayer`)
| # | Gap | Evidence | Severity | Fix |
|---|---|---|---|---|
| O2-1 | **No properties dialog** — dryDepth, maxDepth, rampStyle, colorClasses, isolines (count/color/width), filled contours (levels/opacity), velocity arrows (scale/max/opacity) all have setters but **no UI**. | `swmm2dresultslayer.h:315-382` accessors; no dialog | High | Build the 2D results Symbology panel exposing all these. |
| O2-2 | **Sublayers are dormant** — `DepthColorRampSublayer`, `ContourBandSublayer`, `IsolineSublayer`, `VelocityVector/FlowArrowSublayer`, mesh fill/edge/node are instantiated and editable (SE.4 editors) **but the paint pipeline ignores them** and uses hardcoded layer fields instead. So editing the sublayer styles (incl. VS.6/VS.7 work) doesn't reach paint. | `swmm2dresultslayer.h:579-610`; paint uses `dry_depth_`/`max_depth_`/`colorRampStyle` directly | High | Make the 2D paint consume the sublayer styles (the SE.4-edited bags) — connect the editable model to the paint. |
| O2-3 | **No output-variable concept** — depth + velocity hardcoded; can't render e.g. WSE, hazard, or a pollutant field. | `availableAttributes` returns a fixed mesh field set | Med | Add variable selection for 2D fields. |
| O2-4 | **maxDepth auto-grows per frame but isn't consumed by a renderer** and there's no fixed/auto/user range control surfaced. | `swmm2dresultslayer.cpp:1175-1191` | Med | Range mode (as O1-3) for 2D. |

### 3c. Animation ↔ styling interaction
| # | Gap | Evidence | Severity | Fix |
|---|---|---|---|---|
| A-1 | **Per-tick restyle is values-only restyle-in-place**; no per-tick re-classification/re-stretch hook is connected. Distribution-evolving data can't drive evolving classes. | `setCurrentTimeStep` → `SceneDirty::Values`; `rebinDynamicRulesIfNeeded` dormant | High | Connect optional per-tick rebin when range mode = per-frame. |
| A-2 | `dispatchAnimationTick()` (ISublayerHost) invalidates dynamic sublayers, but since 2D sublayers don't paint (O2-2), it's a no-op. | animationcontroller (S2.5a) | — | Resolved once O2-2 lands. |
| A-3 | **No TemporalSpec sub-range on results layers** — can't limit playback/styling to a window of the run. | results layers don't expose TemporalSpec | Low | Expose TemporalSpec range. |

### The unifying fix (MVC M4 — "static + dynamic, one grammar")
The above collapse to one design: a renderer whose **AttributeSource** is either
a **static** field (model/GIS attribute) or a **dynamic** output variable sampled
at the current timestep, with a **range mode** (fixed-over-run / per-frame
auto-stretch / fixed-user) and a per-source ramp + classification. Results
layers then style exactly like model layers — pick variable, pick renderer
(single/graduated/categorized), pick ramp/classes/range — and the painter +
animation tick consult that one model. This is the spine for O1-2, O1-3, O1-4,
O2-1, O2-2, O2-3, O2-4, A-1.

---

## 4. Labeling
| # | Gap | Severity | Fix |
|---|---|---|---|
| L-1 | Base `LabelConfig` unified (VS.10) + 2D value labels added, but **1D results layers carry config + Labels tab with no label painting**. | Med | Add results-layer label painting (mirror SWMMLayerItem). |
| L-2 | GIS vector labels still go through the `GISVectorSymbol` mirror, not the unified base path. | Low | Reconcile. |

---

## 5. Recommended implementation order

1. **X1 + X2 (canonicalize color keys/encoding; fix link color).** Removes the
   "edit doesn't reflect" class of bugs everywhere; prerequisite for trusting
   the rest.
2. **M4 AttributeSource + range mode (the unifying spine).** Unblocks all of §3.
3. **O2-2 (2D sublayers drive paint) + O2-1 (2D properties dialog).** Biggest
   visible win for results — makes the VS.6/VS.7 sublayer work actually render.
4. **O1-1/O1-2/O1-3 (1D variable selector + renderer-driven paint + range mode).**
5. **R-1/R-2 (raster renderer drives paint + raster panel).**
6. **X4 (legend as model editor), X3/M-3 (GL path + struct removal), G-1/G-2
   (GIS canonical shapes + MVC), L-1 (results labels), X5 (undo + persistence).**

Severity-High items (X1, X2, R-1, R-2, O1-1, O1-2, O1-3, O2-1, O2-2, A-1) are the
finish-line set; the rest are polish/consistency.

---

## 6. Open questions for review
1. **Color canonicalization (X1):** standardize on hex strings (JSON-friendly) or
   QColor variants? (Recommend hex.)
2. **Range mode default** for animated outputs: fixed-over-run, or per-frame
   auto-stretch? (Recommend fixed-over-run with an opt-in per-frame toggle.)
3. **Variable selection placement:** per-kind in the kind tree, or a layer-level
   "Variable" control above the kind tree?
4. **2D field set:** depth + velocity only for now, or wire arbitrary mesh result
   fields (WSE, hazard, pollutants) into O2-3 immediately?
