# HANDOFF — 2D Mesh BC Edge Colouring & Cell-Attribute Colouring

**Date:** 2026-08-19
**Plan:** `workplans/MESH_BC_AND_CELL_ATTRIBUTE_STYLING_PLAN_2026-08-19.md`
**State:** Code written, **NOT COMPILED**. Your job: build, verify, fix.

---

## 0. What you are picking up

An implementation written in one pass without a compiler available. Treat
every file below as unverified. Expect the first build to fail on something
small (a missing include, a `const` mismatch, an unused-variable warning
promoted to an error by `/W4`-equivalent flags).

**The implementation deviates from the plan in four places.** Each deviation
is deliberate and documented in §4 — read that section before you "fix"
something back toward the plan text.

---

## 1. Build

An existing macOS build tree is at `build/` (the `default` preset).

```bash
cd /Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui

# App
cmake --build build --target SWMMVis 2>&1 | tee workplans/artifacts/mesh-bc-styling/build.log

# Leaf test that directly compiles the two style bags I changed
cmake --build build --target test_2d_sublayers

# Full-app tests that exercise the mesh layer
cmake --build build --target test_meshattributetable test_meshboundarypath \
                             test_meshcommands_vertex_edge test_render_invalidation

ctest --test-dir build -R "2d_sublayers|mesh|render_invalidation" --output-on-failure
```

If the configure step is stale after the `tests/gui/CMakeLists.txt` edit:

```bash
cmake --preset default && cmake --build build --target test_2d_sublayers
```

Per `CLAUDE.md` §4.1, write build logs and any test artefacts to
`workplans/artifacts/mesh-bc-styling/` — create it, do not use `/tmp`.

---

## 2. Files changed

| File | Change |
|---|---|
| `include/render/sublayers/meshedgesublayer.h` | `colorByBC`, `bcWidthPx`, 7 `QColor` props, `kBcTypeCount`, `bcColorForType()`, `MeshEdgeSublayer::setBcTypesPresent()` |
| `src/render/sublayers/meshedgesublayer.cpp` | ctor defaults, `setBcColor`, `setBcWidthPx`, JSON round-trip, BC-aware `legendSymbolItems()`, 7 `static_assert`s locking the enum order |
| `include/render/sublayers/meshfillsublayer.h` | `CellAttribute` Q_ENUM, `colorByAttribute`, `noDataColor`, `attributeKey()`/`attributeFromKey()`, `colorByAttributeKey()`, `colorsByElevation()`, `setAttributeHasData()`, `setDepthUnitLabel()` |
| `src/render/sublayers/meshfillsublayer.cpp` | `kAttrKeys` mapping table, setters, JSON round-trip, attribute-aware + no-data `legendSymbolItems()` |
| `include/layers/swmm2dmeshlayer.h` | `m_sceneEdgeSlot`, `attrRevision()`, `bcRevision()`, `cellAttributeValues/Samples/Range()`, attr cache members, `refreshSublayerLegendInputs()`, `scheduleLegendInputRefresh()` |
| `src/layers/swmm2dmeshlayer.cpp` | BC slots in `buildMeshHeavyGeom`, slot adoption in both build paths, revision bumps, the three attribute accessors, legend-input push |
| `include/map/swmm2dmeshqsgrenderer.h` | `m_fillCacheAttrKey`, `<QByteArray>` |
| `src/map/swmm2dmeshqsgrenderer.cpp` | 6 BC nodes in the tree + sibling walk + off-screen early-out, Pass 2 bucketing, Pass 1 attribute colour source, widened fill-cache key |
| `src/project/projectserializer.cpp` | `kMeshSublayers` — mesh sublayer styles now round-trip (see §4.4) |
| `tests/gui/CMakeLists.txt` | `meshbctype.cpp` + `meshcellparams.cpp` added to `test_2d_sublayers` |
| `include/render/attributecandidates.h` | comment only (see §4.2) |

`src/map/swmm2dresultsqsgrenderer.cpp` and everything under
`openswmm.engine/` are deliberately untouched.

---

## 3. Highest-risk spots — check these first

**3.1 The positional sibling walk.** `updatePaintNode()` builds the node tree
in one block and re-walks it with `firstChild()`/`nextSibling()` in another.
I inserted six `edgeBcNode[]` entries between `edgeWideNode` and
`contourNode` in **both**. If they drift, a `QSGNode*` gets `static_cast` to
`QSGGeometryNode*` and you corrupt memory rather than crashing cleanly.
Re-read both blocks side by side before trusting any visual result.

**3.2 `kEmptyAttrVals` static init order.** Pass 1 binds
`const QVector<float> &attrVals` to either the layer's cache or a
file-scope `kEmptyAttrVals`. That's a non-trivial static in an anonymous
namespace; if the toolchain complains, make it a function-local `static const`.

**3.3 Dangling reference in Pass 1.** `attrVals` references the layer's
single-slot mutable cache. It is bound, then `cellAttributeRange()` is called
with **the same key** (cache hit, no reallocation). If you refactor so a
different key is requested between the bind and the last use, `attrVals`
silently points at refilled storage. Keep the same-key invariant.

**3.4 `emitShadedTri` call sites.** It grew a third parameter (`attrIdx`).
There are three call sites; all must pass the right index space:
- main loop, exact fill → `idx` (indexes `m_sceneTris`)
- main loop, overview → `-1` (overview quads have no attribute)
- far-zoom hybrid → `idx` (native `m_sceneTris` index)

Passing an overview index as an attribute index reads the wrong cell's value.

**3.5 `Q_PROPERTY` on a nested enum.** `MeshFillStyle::colorByAttribute` is
declared as `OpenSWMM::Render::MeshFillStyle::CellAttribute`. moc is fussy
about fully-qualified nested enum types in `Q_PROPERTY`. If moc errors or the
property comes back as an unregistered type at runtime, the fallback is to
declare the enum in the enclosing namespace instead of inside the class.
**Verify the style dialog actually renders it as a combo** — that is the whole
reason it is an enum (see §4.2).

---

## 4. Deviations from the plan — do not "fix" these back

### 4.1 No `m_bcRevision`-driven invalidation was needed for repaint

The plan flagged as unresolved whether a BC-only edit survives the
`insideCoverage` short-circuit. It does. `applyMeshEdgeBC()` emits
`repaintRequested()` → `Qsg2DDirtyState::noteExternalChanged()` →
`resolve()` sees no geometry/selection/time diff and classifies it as
`Style` (`qsg2ddirtystate.h:142-145`) → `rebuildStatic` includes `Style`, so
Pass 2 re-runs. No extra plumbing.

`m_bcRevision` still exists, but only to version the BC-types-present set.
`m_attrRevision` **is** load-bearing: it is XORed into the Pass 1 fill-colour
cache key, without which a Manning's edit serves stale cached colours.

### 4.2 `colorByAttribute` is a `Q_ENUM`, not a `QString` + candidate list

The plan proposed an `AttributeCandidate` struct and
`AttributeCandidates::meshCellNumeric()`. On reading `layerstyledialog.cpp`,
the generic editor is a `QPropertyModel` tree — a `QString` property would
render as a free-text box the user has to type `"mannings"` into. An enum
gets a combo for free, exactly as `dashPattern` (`Qt::PenStyle`) and
`MeshNodeStyle::shape` already do.

So `meshCellNumeric()` was written and then **deleted** rather than left as
speculative code (`CLAUDE.md` §2). `attributecandidates.h` carries only a
comment saying why mesh cells are absent.

**Accepted cost:** an auto-generated enum combo cannot grey the five pending
`gw.*` rows. They are selectable, and selecting one produces the honest
no-data render plus an "engine support pending" legend row. If you want them
greyed, that needs a custom `QStyledItemDelegate` keyed on the property name
— a separate, larger change.

### 4.3 The BC-slot "non-Wall preference" was dropped

The plan proposed preferring a non-Wall slot when a deduplicated interior edge
has two candidates. That machinery is unnecessary: **a boundary edge belongs
to exactly one triangle**, so it is pushed once and keeps its own slot;
interior edges are pushed twice but both slots are Wall by construction. Plain
first-writer-wins is correct. The upgrade pass would have been error handling
for an impossible state (`CLAUDE.md` §2).

### 4.4 Two things the plan did not cover, added because the feature is broken without them

**Coalesced legend refresh.** `refreshSublayerLegendInputs()` is O(n_slots +
n_triangles). Calling it from `applyMeshEdgeBC` / `applyMeshTriangleMannings`
directly makes a bulk edit (MeshCommands assigning Manning's to 50k cells)
O(n²) — on a 1M-cell mesh that is ~5×10¹⁰ operations. Write paths now call
`scheduleLegendInputRefresh()`, which coalesces a whole batch into one queued
refresh via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
**Verification target: a bulk cell-attribute assign must not regress.**

**Project persistence.** `projectserializer.cpp` saves mesh-layer state as a
hand-rolled field list (`showEdges`, hillshade, contours) that never touched
sublayer styles — so the new BC colours and `colorByAttribute`, and the
pre-existing fill `ClassificationScheme`, were all lost on reopen. Added
`kMeshSublayers` using the same `ISublayerHost::saveSublayersToJson` /
`loadSublayersFromJson` pair `SWMMResultsLayer` already uses. Applied **after**
the hand-rolled fields so the richer state wins where they overlap.

---

## 5. Verification

Create `workplans/artifacts/mesh-bc-styling/` and keep everything there.

### Phase A — compiles and nothing regressed

| # | Check | Pass condition |
|---|---|---|
| A1 | `cmake --build build --target SWMMVis` | clean, no new warnings |
| A2 | `ctest -R "2d_sublayers\|mesh\|render_invalidation"` | all green |
| A3 | Open an existing 2D project, screenshot the mesh | **pixel-identical** to a pre-change screenshot. `colorByBC` defaults false and `colorByAttribute` defaults Elevation, so this is the strongest regression signal you have. Take the "before" shot from `git stash` first. |
| A4 | Load a pre-change `.oswp` and a pre-change `.swmm-style.json` | no warnings; render unchanged |

### Phase B — BC edge colouring

| # | Check | Pass condition |
|---|---|---|
| B1 | Fixture mesh with ≥1 edge of each of the 7 BC types; Layer style → Mesh Edges → Boundary conditions → `colorByBC` on | boundary ring shows distinct colours per type; interior wireframe unchanged |
| B2 | Change one edge's BC via the Mesh Editing toolbar **without panning or zooming** | map recolours on the next frame (this is the §4.1 path) |
| B3 | LegendDock | one row per BC type **present**, not seven; Wall always present |
| B4 | Edit `bcFlowTSColor`, `bcWidthPx` | live update, legend follows |
| B5 | Progressive-load mesh, toggle `colorByBC` before `sceneGeometryReady` | legacy slope colouring, no crash — the `slotsUsable` guard |
| B6 | `colorByBC` off | byte-identical to A3 |

### Phase C — cell attribute colouring

| # | Check | Pass condition |
|---|---|---|
| C1 | Fill style → `colorByAttribute` | renders as a **combo**, 8 entries (§3.5) |
| C2 | Select Manning's n | mesh recolours by roughness; hillshade relief still readable |
| C3 | Elevation → Manning's → Initial Depth → Elevation | each switch recolours immediately; **no stale frame** (this is the `m_fillCacheAttrKey` + `attrRevision` path) |
| C4 | Select `gw.Ks` | whole mesh renders `noDataColor`; legend reads "… — no data (engine support pending)" |
| C5 | Edit one cell's Manning's while coloured by Manning's | that cell recolours |
| C6 | Zoom out to overview LOD while coloured by Manning's | overview quads use the flat fill colour, real large cells keep attribute colours. Confirm this reads acceptably — if it looks wrong, that is a design call to bring back, not a bug. |
| C7 | Classification tab: switch attribute, check the scheme range | range re-derives from the new attribute (elevation ~10¹ vs Manning's ~10⁻² must not collapse to one class) |

### Phase D — persistence and perf

| # | Check | Pass condition |
|---|---|---|
| D1 | Set BC colours + `colorByAttribute`, save project, reopen | both survive (§4.4) |
| D2 | `StyleFileIO::exportStyle` → `importStyle` | both survive |
| D3 | ≥1M-cell mesh, `colorByBC` off | frame time and peak RSS within 5% of pre-change |
| D4 | Same mesh, `colorByBC` on | <5% added |
| D5 | Bulk-assign Manning's to a large selection | **linear**, not quadratic — the §4.4 coalescing |
| D6 | Results view mesh edges | unchanged; `grep -n "colorByBC" src/map/swmm2dresultsqsgrenderer.cpp` returns nothing |

---

## 6. Known gaps (intentional, not bugs)

1. **Quantile / natural-breaks / std-dev classification of a cell attribute
   is not wired.** Pass 1 passes `{}` for samples to `scheme.levelEdges()`,
   matching what the elevation path has always done. `cellAttributeSamples()`
   exists and is correct — wiring it is a follow-up, and doing it for
   attributes but not elevation would make the two behave differently.
2. **`gw.*` rows are selectable, not greyed** — see §4.2.
3. **Results layer diverges** — the user chose model-view-only.
4. **The >2-class limitation in the slope classification path**
   (`swmm2dmeshqsgrenderer.cpp`, the `schemeCustomized` block) is untouched
   and still stands.
5. **`useElevationRamp` is now misnamed** — it gates the ramp for whatever
   attribute is selected. Not renamed: the key ships in `.swmm-style.json`
   files and in `projectserializer.cpp`. Documented in the header.

---

## 7. If you need to back out

Everything is additive and default-off. Reverting is:

```bash
git checkout -- include/render/sublayers/meshedgesublayer.h \
                src/render/sublayers/meshedgesublayer.cpp \
                include/render/sublayers/meshfillsublayer.h \
                src/render/sublayers/meshfillsublayer.cpp \
                include/layers/swmm2dmeshlayer.h \
                src/layers/swmm2dmeshlayer.cpp \
                include/map/swmm2dmeshqsgrenderer.h \
                src/map/swmm2dmeshqsgrenderer.cpp \
                src/project/projectserializer.cpp \
                include/render/attributecandidates.h \
                tests/gui/CMakeLists.txt
```

Partial back-out is also viable: the edge-BC work (§4.1, §4.3) and the
fill-attribute work (§4.2) touch disjoint code apart from the shared node-tree
edit, so one can ship without the other.

---

## 8. Before you call it done

- [ ] Every Phase A–D row has a recorded result in
      `workplans/artifacts/mesh-bc-styling/`
- [ ] A3 and B6 confirmed **pixel-identical**, not "looks the same"
- [ ] `CHANGELOG.md` updated if this lands in a release (`CLAUDE.md` §5.2)
- [ ] Tests added for the parts that are unit-testable without a GPU:
      `MeshEdgeStyle` / `MeshFillStyle` JSON round-trip and enum-key mapping
      in `test_2d_sublayers.cpp`; `m_sceneEdgeSlot` correctness and
      `cellAttributeValues/Range` in a mesh-layer test (`m_sceneEdgeSlot.size()
      == m_sceneEdges.size()` after **both** the full and progressive build
      paths, and every boundary `SceneEdge` mapping to a slot where
      `isBoundaryEdge(slot/3, slot%3)` is true)
