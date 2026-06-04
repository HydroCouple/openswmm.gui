# Implementation Crosswalk — Claimed vs Actual vs UI-Reachable

**Date:** 2026-06-04
**Trigger:** A clean compile reportedly shows none of the claimed symbology
features surfacing. This crosswalk audits every claimed item against the
working tree: does the code exist, who calls it, and can a user reach it.

---

## 0. Root cause first: you are almost certainly running a stale binary

The code IS in the working tree and IS wired (evidence below). The problem is
**which binary runs and what it was built from**:

| Evidence | Finding |
|---|---|
| `build/test/SWMMVis.app/Contents/MacOS/SWMMVis` | Built **May 29 15:06** — predates ALL the symbology work (dated May 31 – Jun 4). Launching this shows none of the features. |
| `build/darwin-debug/SWMMVis.app/Contents/MacOS/SWMMVis` | Built **Jun 4 08:08** — contains the work (key `.o` files Jun 4 08:06–08:07). |
| `git diff HEAD --stat` | **51 files, +5,430 / −357 uncommitted.** The entire M1–M3 / F1–F4 / VS body of work is working-tree-only. Any build from a clone, another worktree, CI, or `git stash`-cleaned tree contains none of it. |
| `src/ui/dialogs/editors/singlesymbolrendererpanel.cpp` | Modified **Jun 4 08:26 — after the last build (08:07)**. Even `darwin-debug` is one file stale. |
| `git stash stash@{0}` "WIP symbology (set aside for SWMMVis rebuild)" | Contains older `symbologydialog`/pattern-editor work (~1,246 lines) — a different, legacy path; not the current blocker, but symbology code is parked there too. |

**Actions:**

1. Launch `build/darwin-debug/SWMMVis.app`, not `build/test/SWMMVis.app`
   (delete or rebuild the `test` bundle to remove the trap).
2. Rebuild `darwin-debug` to pick up the 08:26 edit, then retest.
3. **Commit the working tree.** 5,430 uncommitted lines is one `git checkout`
   away from vanishing, and guarantees any second machine/agent builds the
   wrong thing.

---

## 1. Crosswalk — MVC + Graduated fixes (SYMBOLOGY_MVC §7a, GRADUATED review)

Legend: **WIRED** = exists + called + UI-reachable · **BACKEND-ONLY** = exists
+ called, no UI exposure · **DEAD** = exists, zero call sites · **MISSING** =
not implemented.

| Item | Claim | Implementation (file:line) | Call sites / hookup | UI-reachable | Verdict |
|---|---|---|---|---|---|
| M1.a | Lossless struct↔SymbolStyle (labels + arrows) | `swmmmodellayer.cpp:267` (`styleFromElementSymbol`), `:340` (`elementSymbolFromStyle`); label/arrow fields `:287-299`, `:360-376` | Every `set*Symbol` setter `:2096-2103`; renderer seeding `:469-487` | Via Symbology tab editors | **WIRED** |
| M1.b | RuleList never stale (`m_ruleListDirty`) | `swmmmodellayer.h:1870`; dirty-mark `swmmmodellayer.cpp:2116`; rebuild gate `:2131-2135`; clear `:2233` | `ruleList()` read by `kindtreesymbologypanel.cpp:336` | Dialog open path | **WIRED** |
| M2 | Renderer canonical from all edit paths | `syncSingleRendererFromStruct` `swmmmodellayer.cpp:1705` | Called from all 11 kind setters `:1715-1724` | Indirect (any symbol edit) | **WIRED** |
| M3 core | Per-feature shape from renderer | `featureShape()` `swmmmodellayer.cpp:2348`; cache filled `:2454, :2530` | Consumed in `SWMMLayerItem` node paint loop | Canvas (CPU path) | **WIRED** |
| M3-cleanup | GL path + struct deletion | — | GL renderer still reads `SWMMElementSymbol` | — | **MISSING (deferred by design, X3)** |
| F1 | Model layer classifies | `classifyGraduatedIfNeeded` `swmmmodellayer.cpp:2370` | Called from `rebuildKindFeatureColors:2414` + `buildRuleListLazy:2271` | Graduated editor → Apply | **WIRED** |
| F2 | Explicit re-classify | `clearBreaks()` `graduatedrenderer.h:126`; `setBinner` clears | Editor calls at `kindrendererpanel.cpp:363, 386, 469, 489, 521` | Method/count/attr changes | **WIRED** |
| F3 | Breaks table from live state | `rebuildBreaksTable` `kindrendererpanel.cpp:565` | Called on every renderer change incl. `rendererReplaced:468` | Graduated editor table | **WIRED** |
| F4 | One shared classify gate | `classifyIfNeeded` `graduatedrenderer.h:135` | Model `swmmmodellayer.cpp:2396`; results `swmmresultslayer.cpp:2001` | — | **WIRED** |
| Routing | Symbology tab per layer type | `buildSymbologyTab` `layerstyledialog.cpp:499-571` | Model + 1D results → `KindTreeSymbologyPanel` `:535`; 2D → `Swmm2DResultsStylePanel` `:543`; GIS vector `:559`; raster/mesh → generic `SymbologyTab` `:569`. CMake: panel `:556/:968`, kindrendererpanel `:574/:985` | Properties dialog | **WIRED** |
| Editor mount | Rule-bound SE editors | `mountEditorForCategory` `kindtreesymbologypanel.cpp:316`; `ctx.rule` set `:336-337`; rule honored `symbologytab.cpp:44-46, 163-165` | — | Kind tree selection | **WIRED** |
| Cancel | No symbology snapshot clobber | `onCancel` `layerstyledialog.cpp:912-930` — symbology applied live, only name/vis/opacity revert | — | — | **WIRED (by design)** |

**Conclusion for §1:** every claimed MVC/graduated item is present and wired in
the **working tree**. None of it is in any commit (HEAD = `8a175c8 "Work in
progress"` predates much of it). A binary that doesn't show these was not
built from this tree — see §0.

---

## 2. Crosswalk — VS slices (VISUALIZATION_STYLING_OVERHAUL_PLAN)

| Slice | Claim | Code + CMake | Hookup status | UI-reachable | Verdict |
|---|---|---|---|---|---|
| VS.2/2b | Fill primitive into paint paths | `fillsymbollayer.{h,cpp}` ✓, CMake `:678` ✓, `test_fillsymbollayer` ✓ | **`drawFill()` has zero call sites outside its own file** — not invoked from `SWMMResultsLayer::paintPolygon`, `GISVectorLayer`, or `swmmlayeritem` | `fillStyle` Q_PROPERTY exists on `PolygonFeatureSublayerStyle` but no editor combo surfaces it | **DEAD in paint** — wire `drawFill()` into the three paint paths + expose `fillStyle` |
| VS.3 | 4 new classification methods | `intervalbinner.h:32-43` ✓ | Driven via `GraduatedRenderer` | Method combo INCLUDES them: `kindrendererpanel.cpp:159-162` (Jenks/StdDev/Log/Exp) | **WIRED** |
| VS.4 | Independent width output axis | `graduatedrenderer.h` (`outputWidthEnabled`, `widthForBin`) ✓, JSON migration ✓ | Renderer-side complete | **No UI** — `grep outputWidth src/ui/` returns nothing | **BACKEND-ONLY** — add width-axis controls to graduated editor |
| VS.5 | Dialog slimmed to 6 canonical tabs | `layerstyledialog.h:56-78` ✓; orphan tab files deleted from CMake | — | Dialog shows Information/Source/Symbology/Labels/Rendering/Metadata | **WIRED** |
| VS.6 | Paletted raster + hillshade | `palettedrasterrenderer.{h,cpp}` ✓ CMake `:196/:664`; `gisrasterlayer.cpp:152` says "`m_colorRamp` field is retired", `rasterRenderer()` accessor `:173` | Hillshade UI caller exists: `rastercolorrampeditor.cpp:131` (`setHillshadeEnabled`) | Raster ramp editor exists; **factory selection paletted-vs-pseudocolor and full raster panel still unverified on a live build** | **MOSTLY WIRED — verify on correct binary** (R-1 may already be closed; confirm) |
| VS.7 | Color-by-magnitude arrows | `FlowArrowStyle`/`VelocityVectorStyle` props ✓ CMake `:711-712`; `colorForSpeed()` ✓ | CPU flow-arrow loop wired; **QSG velocity-glyph pass not confirmed** to call `colorForSpeed()` | Q_PROPERTYs auto-surface in `SublayerStyleDialog` | **PARTIAL** — verify QSG pass |
| VS.8 | Opacity tree + dialog sync | Signal connects in tree model ✓ | `opacityChanged`/`visibilityChanged`/`nameChanged` → model refresh wired | Tree column 1 delegate works; **Rendering-tab slider ↔ tree live sync unverified** | **PARTIAL — verify on correct binary** |
| VS.9 | Live legend wiring | `legendlayertreemodel.cpp:164-172` — sublayer `invalidated()` → cache reset, `m_connectedSublayers` tracked | ✓ | Legend dock | **WIRED** |
| VS.10 | Unified labeling | `LabelConfig` on base ✓; `LabelsTab` generalized ✓ CMake `:552/:639/:965`; 2D label paint added | **1D results: config + tab but NO label painting (L-1, known)**; Labels capability flag for results/2D layers unconfirmed | LabelsTab in dialog | **PARTIAL** — L-1 open; confirm capability flags |
| VS.11 | Stale file cleanup | 10 deferred-tab files deleted ✓ | `rulesymbologytab.{h,cpp}` still present but unmounted (deletion candidate) | — | **PARTIAL (cosmetic)** |

---

## 3. What is actually still missing (true gaps after the stale-binary fix)

In expected priority order once you confirm features surface in the
`darwin-debug` binary:

1. **VS.2b** — `drawFill()` not called by any paint path (the only audited item
   that is genuinely dead code today) + no `fillStyle` editor exposure.
2. **VS.4 UI** — width-axis controls absent from the graduated editor.
3. **L-1 / VS.10** — 1D results label painting not implemented.
4. **VS.7 QSG** — velocity-glyph GPU pass may ignore `colorForSpeed()`.
5. **X3/M3-cleanup** — GL paint path still struct-driven (CPU/GPU divergence).
6. **VS.6 confirmation** — paletted-renderer factory selection + raster panel
   completeness (working tree suggests R-1/R-2 are further along than
   `STYLING_GAP_REVIEW.md` recorded — re-baseline that doc after testing).
7. **VS.8** — dialog Rendering-tab opacity ↔ tree live sync.

Items the master plan (`STYLING_REGIME_MASTER_PLAN.md`) adds on top (SR.1–SR.5:
bin-bound editing, class-label number formatting, sample-from-results,
labeling UX pass, legend typography) remain valid and unaffected.

---

## 4. Verification protocol (do this before any new code)

1. `cmake --build build/darwin-debug` (picks up the 08:26 edit) → launch
   **that** app bundle.
2. Smoke list, in order:
   - Properties → Symbology on the model layer shows the **kind tree**
     (Junctions/Conduits/…) — proves routing + new dialog code is in the binary.
   - Graduate junctions by `invertElev` → real color spread, data-unit bounds
     (F1–F4); method combo lists Jenks/StdDev/Log/Exp (VS.3).
   - Bin count 5→7 + Apply → 7 classes (F2).
   - Legend dock updates when a sublayer style is edited (VS.9).
   - Raster layer: hillshade toggle in the ramp editor (VS.6).
3. If step 2's first check fails **in that binary**, capture the dialog that
   does appear — that identifies an older `SymbologyTab` path still being hit
   for that layer type, and we fix the routing branch, not the backend.
4. `git add` + commit the working tree; tag pre/post so binaries are traceable
   to source state from now on.
5. Then run `ctest` and re-baseline `STYLING_GAP_REVIEW.md` statuses against
   observed reality.
