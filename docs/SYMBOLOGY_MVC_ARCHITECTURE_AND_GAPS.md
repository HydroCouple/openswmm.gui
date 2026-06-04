# Symbology MVC Architecture — Gap Analysis & Target Design

**Status:** ⏳ Evaluation for review (no code in this document)
**Date:** 2026-05-31
**Supersedes:** the *remaining* phases of `SYMBOL_EDITOR_WIRING_PLAN.md` (SE.5–SE.7)
and the editor-wiring portions of `VISUALIZATION_STYLING_OVERHAUL_PLAN.md`. The
editors, adapters, and registry fix built so far are **kept** (see §6).

> This is the "step back" document. It names why the symbology editing has felt
> like a mish-mash, defines the single architectural fix (one model, many
> observing views), and specifies the UI required to finish — covering **both**
> static model styling and dynamic results styling.

---

## 1. Why it's a mish-mash (root cause)

There is **no single source of truth** for a layer's symbology. For the SWMM
model layer alone, the *same* style lives in **three** places, edited by
**three** different views, read by a **fourth**:

| Representation | Who writes it | Who reads it |
|---|---|---|
| Legacy `SWMMElementSymbol` structs (`m_junctionSym`, …) | `set*Symbol()`, the one-way bridge | **The painter** (`SWMMLayerItem::paint` reads `b.sym->markerShape`, color, …) |
| Per-kind `IFeatureRenderer` (`m_kindRenderers[c]`) | legend class-edit command, `setKindRenderer` | the lazy RuleList builder; `rebuildKindFeatureColors` |
| `RuleList` mirror (`m_ruleList`, **built lazily once, never rebuilt**) | **the properties dialog** (`RuleSymbologyTab` → adapters) | nothing — it's a mirror |

The bridges between them are **one-way and lossy**:

- Dialog edits a Rule → `notifyRendererStateChanged` → `setKindRenderer` →
  `elementSymbolFromStyle` copies *a subset* of fields (color/size/shape/outline)
  onto the legacy struct → repaint. Anything not in that subset is dropped.
- Nothing copies the *other* direction. The legacy struct is initialised from the
  `.inp` / preferences at load; the RuleList mirror is cloned from the kind
  renderers **once**. If they start out different, or anything later mutates one
  but not the others, they silently drift.

### This directly produces the symptoms you reported

- **"On opening, the editors don't match what's drawn."** The dialog reads the
  frozen `m_ruleList` mirror; the canvas reads the legacy struct. They were never
  guaranteed equal and there's no refresh, so the dialog shows stale values.
- **"Some work, some don't."** Color/size/shape are in the lossy bridge's subset,
  so they (sometimes) propagate; anything outside it doesn't. Editors whose
  registration didn't match the namespaced class name never loaded at all (fixed
  in SE.1). Kinds whose paint reads a field the bridge doesn't write never update.
- **"Multiple viewers don't synchronise."** The legend dock edits the *renderer*,
  the dialog edits the *RuleList mirror*, the tree edits the *layer*, the painter
  reads the *struct*. No shared observable model → no propagation between them.

### The same disease across layer types

- **GIS vector:** `GISVectorSymbol` struct (painter) + `IFeatureRenderer` +
  `RuleList` + `LabelConfig` — same triple-truth.
- **1D results:** per-`FeatureSublayer` `FeatureSublayerStyle` bags **and** an
  `IFeatureRenderer` **and** per-feature override caches.
- **2D results / mesh:** per-sublayer style bags read by a bespoke paint item;
  separate from the renderer/rule path the dialog edits.
- **Static vs dynamic** is not modelled at all: static model attributes and
  dynamic per-timestep result values flow through *different* code
  (renderer/struct vs the override caches + sublayer bags), so "style by an
  attribute" and "style by a result" are two unrelated mechanisms.

---

## 2. The architectural fix — one Model, many observing Views (MVC)

**Principle:** each layer has exactly **one** style model. Every view (properties
dialog, legend dock, layer tree, on-canvas legend) is a *pure observer* of that
model and edits it only through a controller that emits change notifications all
views receive. The painter is *also* just a view of the model.

```
                       ┌──────────────────────────────┐
                       │   Layer Style Model (1 per     │
                       │   layer): RuleList of          │
                       │   IFeatureRenderers, the       │
                       │   single source of truth.      │
                       └───────────────┬────────────────┘
        styleChanged(scope)            │  (Qt signals)
   ┌───────────────┬───────────────────┼───────────────┬────────────────┐
   ▼               ▼                   ▼               ▼                ▼
Properties      Legend dock        Layer tree      On-canvas        Painter
dialog          (per-class         (opacity/        legend          (reads model
(edit)          color/size edit)   visibility)      (read)          via symbolFor)
```

Four concrete changes deliver this:

**F1 — Eliminate the parallel "truth."** The renderer/RuleList becomes the *only*
authored state. The legacy `SWMMElementSymbol` / `GISVectorSymbol` structs are
demoted to a **pure derived cache** that is *always* regenerated from the model
and *never edited directly* — or removed entirely once the painter reads the
model. No view writes the struct; no view reads a frozen mirror.

**F2 — Painter reads the model.** `SWMMLayerItem` / GIS / results painters call
`renderer->symbolFor(feature, attrs)` (the contract that already exists) instead
of reading `m_*Sym`. This is the "renderer-drives-paint" step that collapses the
last divergence. (Transitional option in §5.)

**F3 — Kill the lazy frozen mirror.** The `RuleList` *is* the model (owned, live),
not a snapshot cloned once. Opening the dialog binds to the live model; there is
nothing to go stale. All edits route through it.

**F4 — One change channel.** A single `styleChanged(scope)` notification (scope =
which kind/sublayer changed) that the canvas, legend model, tree, and any open
dialog all connect to and re-read. Edits are applied via undoable commands on the
model so multi-view + undo/redo stay coherent.

### Static **and** dynamic under one grammar

The renderer already maps *an attribute value* → a symbol. Make the **attribute
source** explicit and dual-mode:

- **Static source:** an engine/GIS field (diameter, elevation, land-use, area…).
  Classified once; repaint only on edit.
- **Dynamic source:** an output variable (depth, flow, velocity, …) sampled at the
  **current animation timestep**. The painter passes the current frame's values
  into `symbolFor`; the renderer re-bins per frame when configured to.

This folds today's separate "results override caches" and "sublayer style bags"
into the *same* renderer model. "Style by depth (dynamic)" and "style by diameter
(static)" become the same UI with a source toggle. 2D field rendering
(ramp/contours/arrows) becomes a renderer kind on the same model, not a parallel
system.

---

## 3. Gap inventory (what must exist to finish)

| # | Gap | Today | Target |
|---|---|---|---|
| G1 | Single source of truth | 3 representations per layer | 1 live RuleList/renderer model; struct is derived cache or gone |
| G2 | Dialog reflects reality on open | edits frozen mirror | binds to live model; no mirror |
| G3 | Cross-view sync | each view edits a different object | all views observe one `styleChanged(scope)` |
| G4 | Painter source | reads legacy struct | reads `symbolFor()` (F2) |
| G5 | Legend ↔ model | legend command can't even resolve `SWMMModelLayer` renderer | legend reads/writes the one model for every layer type |
| G6 | Static vs dynamic | two mechanisms | one renderer, `AttributeSource{static|dynamic}` |
| G7 | Full round-trip persistence | lossy struct subset in `.oswp` | model serialised whole |
| G8 | Undo/redo + multi-dialog | ad-hoc | command stack on the model |
| G9 | Renderer-class coverage in paint | only SingleSymbol paints from struct; Graduated/Categorized go through override caches | all renderer classes paint via `symbolFor` |

---

## 4. UI required to push to the finish line

One **Layer Properties → Symbology** surface, identical grammar for every layer,
driven by the one model:

```
┌ Symbology ─────────────────────────────────────────────────────────────┐
│ Kind / sublayer tree (left)     │  Renderer editor (right)               │
│  ▸ Junctions                    │  Renderer: [ Single ▼ ]                │
│  ▸ Conduits            ◀ select │     Categorized · Graduated · Rule     │
│  ▸ Subcatchments                │  ┌ Source ────────────────────────┐    │
│  ▸ (2D) Depth field             │  │ ( ) Static field  [ diameter ▼]│    │
│  ▸ (2D) Velocity                │  │ (•) Dynamic result[ depth    ▼]│    │
│                                 │  └────────────────────────────────┘    │
│                                 │  ┌ Symbol (archetype editor) ──────┐   │
│                                 │  │ Marker shape / size / fill /     │   │
│                                 │  │ outline / opacity  (live preview)│   │
│                                 │  └──────────────────────────────────┘   │
│                                 │  ┌ Classes (graduated/categorized) ─┐   │
│                                 │  │ method · classes · ramp · table  │   │
│                                 │  └──────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────────────┘
   [Live preview]   [Apply] [OK] [Cancel]      ← all edits = commands on model
```

- **Left tree** lists every styleable scope of the layer — model kinds, GIS
  features, AND the dynamic result fields (depth/velocity/contours) — so static
  and dynamic styling sit in one place.
- **Renderer selector** (Single / Categorized / Graduated / Rule) + **Source**
  toggle (static field vs dynamic result) is the unifying control.
- **Archetype editor** = the dedicated Point/Line/Polygon/raster/mesh editors
  already built (SE.1–SE.4) — they stay; they just bind to the live model.
- **Legend dock** and **layer tree** become thin views of the same model
  (per-class color/size edits, opacity, visibility), and a **live preview** swatch
  reflects edits immediately.
- Every edit is a **command** → model emits `styleChanged` → canvas + legend +
  tree + preview + any other open dialog refresh together (the MVC sync you want).

---

## 4a. Navigation decision — kind/sublayer tree, NOT a rules-list editor

The QGIS-style "rules list" editor (`RuleSymbologyTab`) is **retired from the
properties dialog** (removed 2026-05-31). For a SWMM model layer the rules are a
*fixed taxonomy* (one per kind), so a generic add/remove/reorder rules list is a
confusing way to present junctions / conduits / subcatchments. Decisions:

- **Single symbology surface = the kind/sublayer tree** (`KindTreeSymbologyPanel`):
  left = kinds + result fields; right = the selected scope's renderer editor.
- **Model is the per-kind/per-sublayer renderer.** The `RuleList` is demoted to an
  internal container / `.qml`-IO detail — never surfaced as a user editor.
- **Rule-based (filter) rendering is preserved as a renderer *type*** selectable
  per kind (Single / Categorized / Graduated / Rule-based in the dropdown), not as
  top-level navigation.
- `buildSymbologyTab` routes **all** feature-bearing layers to the kind/sublayer
  tree (multi-kind → kinds; single-renderer layers like GIS vector/raster → a
  one-node tree or the direct renderer editor); the `if (rules) RuleSymbologyTab`
  branch is removed from the primary path.

This is consistent with §4's wireframe — the wireframe's left pane *is* this tree.

## 4b. Implemented (2026-05-31)

- **Kind-tree routing (done).** `buildSymbologyTab` now routes multi-kind layers
  to `KindTreeSymbologyPanel` (the rules-list `RuleSymbologyTab` is retired from
  the dialog; include removed, class file now unused → deletion candidate).
- **Kind tree → renderer editors (done).** `mountEditorForCategory` sets
  `ctx.rule = ruleList()->at(int(cat))`, so the panel mounts the SE.1–4
  renderer-based editors (single source of truth) instead of the legacy
  per-kind struct editor.
- **Persist on close (done).** `onCancel` no longer restores the parallel,
  stale `styleSubjects()` snapshot over live-applied symbology — that snapshot
  clobber was the "reverts to default on reopen" bug. Name/visibility/opacity
  still roll back on Cancel.
- **Marker shapes (done).** Canonical `MarkerShape` extended to 19; the picker
  renders previews via `drawMarkerShape` and `populateCanonical()` lists all 19
  for canonical-enum editors.

## 5. How to get there (proposed phases, replaces SE.5–SE.7)

Ordered to make the dialog *correct* first, then unify, then enrich.

- **M1 — Single live model + kind-tree binding (kills stale-on-open, G2/G3).**
  The **per-kind/per-sublayer renderer set is the single model** (the `RuleList`
  becomes internal). Route the Symbology tab to the kind/sublayer tree
  (`KindTreeSymbologyPanel`, per §4a) and wire it to edit each scope's *renderer*
  (the SE.1–SE.4 adapter editors over `kindRenderer(c)`), not a frozen mirror or
  the legacy struct. Emit one `styleChanged(scope)` that the canvas + legend +
  tree + open dialog all observe. Eliminate `buildRuleListLazy`'s build-once mirror.
  *Verifiable: open dialog → values equal the drawn style; edit in dialog → legend
  + canvas update live; edit color in legend → dialog updates live.*
- **M2 — Single source of truth (G1).** Demote the legacy structs to a derived
  cache regenerated from the model on every `styleChanged`; forbid direct writes.
  Reconcile initial state so model == what's drawn at load.
- **M3 — Painter reads the model (G4/G9).** Switch `SWMMLayerItem` (and GIS /
  results painters) to `symbolFor()`. *This is the biggest change*; it removes the
  struct entirely and makes Graduated/Categorized paint correctly for all kinds.
  (Transitional fallback: keep the derived cache from M2 until M3 is proven.)
- **M4 — Static/dynamic source model (G6).** Add `AttributeSource{static|dynamic}`
  to the renderer; sample dynamic values per frame; fold the results override
  caches + 2D sublayer styling into the one model.
- **M5 — Legend + tree as pure views (G5).** Route legend per-class edits and tree
  opacity/visibility through model commands for every layer type (fix
  `featureRendererFor` to cover `SWMMModelLayer`).
- **M6 — Undo/redo + whole-model persistence (G7/G8).** Commands on the model;
  serialise the model (not the struct subset) to `.oswp`.
- **M7 — Tests + acceptance.** A propagation test (edit via view A → views B/C +
  paint reflect it), a stale-on-open regression test, and a static-vs-dynamic
  round-trip.

---

## 6. What we keep vs replace

**Keep (already built, still correct under MVC):**
- The dedicated archetype editors (SE.1–SE.4) — Point/Line/Polygon/raster/mesh.
- The `createEditorFor` tail-match fix (SE.1) — required for any editor to load.
- The `*SymbolStyleAdapter` family — but they bind to the **live** model (M1).
- The classification engine, fill primitive, color ramps, paletted raster,
  arrow color-by-magnitude, hillshade, labeling (VS work) — these are model
  *capabilities*; MVC is about how they're edited/observed.

**Replace / remove:**
- The lazy frozen `RuleList` mirror semantics (M1).
- The legacy per-kind structs as an authored truth (M2/M3).
- The one-way lossy bridge `elementSymbolFromStyle` (subsumed by M3).
- The per-feature override caches + parallel 2D styling as separate mechanisms
  (folded into the model in M4).

---

## 7a. Implementation progress

Full switch approved (option B). Driving M1→M7; the dialog routing
(`buildSymbologyTab`) is left to the user (uncommitted working-tree edit), so
backend work stays in `swmmmodellayer`.

| Step | Status | Notes |
|---|---|---|
| M1.a Lossless struct↔SymbolStyle | 🔧 done — pending build | `styleFromElementSymbol` / `elementSymbolFromStyle` now round-trip **all** fields (labels + flow arrows), not just color/outline/size/shape. Prerequisite for making the renderer the single source of truth without dropping arrow/label data. Also fixes a standalone bug: arrow/label edits now propagate through the renderer model. |
| M1.b RuleList never stale on open | 🔧 done — pending build | Added `m_ruleListDirty`; `setKindRenderer` marks it, `ruleList()` rebuilds from the live per-kind renderers, `buildRuleListLazy` clears it. Fixes "dialog opens showing values that don't match what's drawn." Rebuild only at read time (dialog open), never mid-session. |
| M1.c Single-owner collapse | ↩︎ folded | Decided **not** to collapse rule+kindRenderer into one object — the working rule→`setKindRenderer` funnel is left intact (no recursion exists there; it updates `m_kindRenderers`, not the rule). The "single source of truth" is achieved instead by M2 making the renderer canonical from every edit path. Avoids a risky object-lifetime rewrite. |
| M2 Renderer canonical from all paths | 🔧 done — pending build | New `syncSingleRendererFromStruct(c,s)` called by every `set*Symbol`: when the kind's renderer is SingleSymbol it mirrors the struct onto the renderer's `SymbolStyle` in place (classified renderers untouched) + marks the RuleList dirty. So preferences / project-load / dialog edits all converge — the struct is now a **consistent derived cache** of the renderer, and the dialog (rebuilt from the renderer) always matches. No recursion (verified against `setKindRenderer`'s regen path). |
| M3 Painter fully renderer-driven | 🔧 done (core) — pending build | The CPU painter (`SWMMLayerItem`) already sourced per-feature **color / size / offset** from the renderer via `rebuildKindFeatureColors` (which samples `symbolFor`). Closed the last gap: per-feature **marker shape** — new `m_kindFeatureShapes` cache + `featureShape()` accessor, populated from the style's `"shape"` prop, consumed in the node paint loop. So a Categorized/Rule-based renderer now varies glyph shape per feature, not just colour. **Deliberately deferred (NOT safe blind):** (a) literal deletion of the `SWMMElementSymbol` type — 111 refs across IO (`swmmvisprojectwindow`), the **GL paint path** (`swmmlayerglrenderer`), the preferences adapter, and spec converters; deleting it would break the build. M1+M2 make the struct a faithful derived cache, so it is harmless to keep. (b) The **GL renderer** paint path — must mirror the per-feature-shape read; do it alongside the type cleanup when compiling. These are tracked as **M3-cleanup** below. |
| M3-cleanup Delete struct type + GL path | ⏳ deferred | Remove `SWMMElementSymbol` everywhere and route the GL paint path through the renderer. Large mechanical change across 9 files / 111 refs — do it with the compiler in the loop (a few iterative passes), not blind. No functional change (the struct is already a consistent cache), so safe to schedule after the deadline. |
| M4 Static/dynamic source | ⏳ | `AttributeSource{static|dynamic}` on the renderer. |
| M5 Legend + tree as pure views | ⏳ | Fix `featureRendererFor` to cover `SWMMModelLayer`. |
| M6 Undo + whole-model persistence | ⏳ | |
| M7 Tests + acceptance | ⏳ | |

## 7. Decisions needed before implementation

1. **M3 scope/appetite.** "Painter reads the renderer" is the principled fix but
   the largest change (touches the hot paint loops for model/GIS/results). Approve
   the full switch, or do M1+M2 first (correct + synced via the derived cache) and
   schedule M3 separately?
2. **Struct removal vs derived-cache.** Remove the legacy structs outright (M3), or
   keep them permanently as a render-time cache regenerated from the model? (Cache
   is lower-risk; outright removal is cleaner.)
3. **Dynamic-source UI placement.** Source toggle inside the renderer editor (as
   drawn in §4), or a separate "Output styling" tab? (Recommend inline — one
   grammar.)
4. **Undo granularity.** Per-property commands, or coalesced per editing session?

Once you pick on these, M1 is the first concrete slice and it alone removes the
"dialog doesn't match / views don't sync" problems you hit.
