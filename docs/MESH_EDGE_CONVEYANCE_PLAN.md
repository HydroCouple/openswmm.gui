# Mesh Edge Conveyance — Implementation Plan (DRAFT v2, for review)

Slice ref: §V.VC follow-up · Engine ref: §11A (`[2D_EDGE_CONVEYANCE]`)

> v2 update: rewritten against the current toolbar (post §V.VC, post-cell-selection). The toolbar now hides BC widgets contextually instead of merely disabling them, the Browse action is text "…", and a third selection mode (Cells) coexists with Vertex / Edge. Engine §11A is unchanged.

## 0. What's already in the engine

Per-edge conveyance factor (a.k.a. flux attenuation) is fully wired engine-side:

- `MeshData::edge_conveyance` — flat `[tri*3 + edge]`, `double` in `[0, 1]`, default `1.0`.
- Parser: `[2D_EDGE_CONVEYANCE]` rows `FROM_VERTEX TO_VERTEX CONVEYANCE` (`SectionHandlers2D.cpp`).
- Solver: `SurfaceFluxCalculator` applies `F_e *= edge_conveyance[idx]` after every other flux term.
- C API: `swmm_2d_{get,set}_edge_conveyance`, `_bulk`, `_reset` (`openswmm_2d.h` §V.11).
- Semantics: applies to **every** edge (interior + boundary). `1.0 = unrestricted`, `0.0 = closed`. Interior edges store the same value in both neighbour slots — `SurfaceRouter2D::initialize` enforces this when draining pending rows.

GUI side: nothing surfaces this yet. `MeshEdgeBC` has no `conveyance` field, `InpMeshReader` ignores `[2D_EDGE_CONVEYANCE]`, `InpMeshWriter` doesn't emit it, `MeshEdgePropertyAdapter` has no Q_PROPERTY for it, and `MeshEditingToolbar` has no widget for it.

## 1. UX shape — push back before coding

The request says: *"an option in the boundary specification combobox to keep things streamlined."* I want to flag a semantic problem with that exact shape before we touch code. The current toolbar makes the conflict sharper than it was a week ago:

|                          | BC type (combo today)                                        | Conveyance factor                                |
|--------------------------|--------------------------------------------------------------|--------------------------------------------------|
| Applies to               | Boundary edges only                                          | All edges (interior + boundary)                  |
| Visibility today         | Hidden unless `edgeMode && haveBoundaryEdge` (lines 800–803) | n/a                                              |
| Meaning                  | Replaces the missing-neighbour flux                          | Multiplier on the computed flux                  |
| Engine field             | `BoundaryData::type`                                         | `mesh.edge_conveyance[...]` (separate vector)    |
| Independent of the other | —                                                            | Yes — `Wall + ψ=0.5` is a legal, useful state    |

The "hide unless boundary edge selected" pattern in `updateEnabledState()` is the part that makes putting conveyance inside the BC combo actively bad: interior-edge selections hide the whole BC group, so the conveyance control would disappear at exactly the moment the user wants it most. Conveyance needs its own gate — `edgeMode && !edges.isEmpty()` — independent of `haveBoundaryEdge`.

### Option A — Recommended: a dedicated, contextually-shown widget (smallest change)

Add one `QDoubleSpinBox` ("ψ:" with the conveyance tooltip explaining it's the flux attenuation), inserted on the toolbar **after** `m_actBrowseObj`, with a `QAction` handle (`m_actConveySpin`) so it follows the same `setVisible()` pattern the BC widgets just adopted. Visibility: shown whenever `edgeMode && !edges.isEmpty()` (boundary OR interior). Range `[0, 1]`, decimals 3, step 0.05, default 1.000.

Layout, on top of the current edge row:

```
[Edge label] [BC combo*]  [BC param*]  [… browse*]   [ψ: 1.000]
                                                       ^^^^^^^^
                                                       visible when ≥1 edge is selected,
                                                       even with no boundary edges
                                                       (* hidden when no boundary edge selected)
```

Pros: matches engine 1:1; works on interior edges; no changes to BC code paths; reuses the new contextual-hide pattern (no new mechanism); smallest diff.
Cons: one more visible widget when both controls are showing (~75 px).

### Option B — As requested: combo row "Conveyance"

Add `MeshBCTypes::Type::Conveyance` (8th row) whose param page is the ψ spinbox. Requires:
- Either a synthetic engine BC type that writes `[2D_EDGE_CONVEYANCE]` instead of `[2D_BOUNDARY_CONDITIONS]`, or a write-time fork — both fragile.
- Suppressing the boundary-only visibility gate when row = Conveyance (because conveyance is valid on interior edges) — i.e. the combo would have to be visible for interior-only selections only when row = Conveyance, which means re-architecting the gate around the *current* combo value, an awkward coupling.
- A combo selection forces an exclusive choice — you cannot have `Wall + ψ=0.5` because picking Conveyance overwrites the type.

Net: works only if we redefine the combo to mean "what to write to .inp for this edge" rather than "what BC type applies", which loses information.

### Option C — Compromise: spinbox embedded inside every page of `m_bcParamStack`

Each page gets `[type-specific param] + [ψ spinbox]`. Wall page becomes just `[ψ]`. Same data model as A.

Pros: zero extra top-level widgets.
Cons: doesn't work for interior edges (whole stack is hidden when no boundary edge); duplicates the ψ spinbox 7 times; param-page indices stop being a clean enum.

**My recommendation: Option A.** Rest of the plan assumes A. §3.3 is the only section that changes if you pick B or C.

---

## 2. Success criteria

1. After load, GUI's per-edge ψ matches what the engine parsed — round-trip via `[2D_EDGE_CONVEYANCE]` (extend `tests/unit/test_meshinpbcroundtrip.cpp`).
2. Editing ψ with one **interior** edge selected updates both `MeshEdgeBC::conveyance` slots that share the edge (mirror), and `attributeChanged` fires.
3. Editing ψ with one **boundary** edge selected updates the single slot; BC type widgets remain untouched.
4. Multi-edge selection: spinbox seeds with the first selected edge's ψ; commit writes to every selected slot (mirroring interior edges).
5. Default value (1.0) omitted from the written .inp — only non-default rows appear in `[2D_EDGE_CONVEYANCE]`, mirroring the engine's omit-defaults convention.
6. The new ψ widget is **visible for interior-only selections** (the existing BC widgets stay hidden in that case).
7. `MeshEdgePropertyAdapter::bcConveyance` round-trips through the Property Browser.
8. Existing BC round-trip test still passes (no regressions in `[2D_BOUNDARY_CONDITIONS]`).

## 3. Steps

### 3.1 Data model — `MeshEdgeBC`
File: `include/mesh/meshedgebc.h`
- Add `double conveyance = 1.0;` field.
- Update `operator==` to include it.

Verify: code compiles; the (currently 1) usage site of `MeshEdgeBC{}` default ctor still produces ψ=1.0.

### 3.2 Layer write path
File: `src/layers/swmm2dmeshlayer.cpp` + header.
- `applyMeshEdgeBC` already replaces the whole slot — that covers ψ when it ships via the BC adapter.
- Add `bool applyMeshEdgeConveyance(int tri, int e, double v)`:
  - Reject (or clamp + warn) if outside `[0, 1]`, matching the engine's strict validation.
  - Write `m_bc[flat].conveyance = v` and, when the edge is interior (`mesh.triangles[tri].nbrN >= 0`), mirror to the neighbour slot.
  - Emit `attributeChanged` with the edge ref name.
- The convenience exists so toolbar, property adapter, and `InpMeshReader` don't each re-implement the mirror.

Verify: new unit test in `tests/unit/` — mirror correctness for interior edges, no mirror for boundary edges, out-of-range rejection.

### 3.3 Toolbar UI (Option A — contextual widget)
File: `src/ui/toolbars/mesheditingtoolbar.cpp` + header.

Additions:
- New members `QDoubleSpinBox *m_conveySpin = nullptr;` and `QAction *m_actConveySpin = nullptr;` (mirrors the `m_actBCTypeCombo` / `m_actBCParamStack` pattern).
- In the ctor, **after** the Browse action is added: build the spinbox (`[0, 1]`, 3 decimals, step 0.05, suffix " ×"), wrap a small page widget with label "ψ:" (matches the BC pages' `[label, spin]` layout), and `m_actConveySpin = addWidget(page);`. Tooltip: "Per-edge conveyance / flux attenuation. 1.0 = unrestricted (default); 0.0 = closed. Applies to interior edges too."
- Wire `valueChanged` to a new `commitConveyance()` slot (similar shape to `commitBCParam`):
  - Iterate `currentSelectedEdges()`, call `applyMeshEdgeConveyance(tri, e, m_conveySpin->value())` for each. **No** `isBoundaryEdge` gate.
- In `refreshEdgeEditor()`, after the existing BC seed block, seed `m_conveySpin` from `bcs[flat0].conveyance` (under a `QSignalBlocker` to avoid recommit). Do this in both the single-edge and multi-edge branches.
- In `updateEnabledState()`, add:
  ```cpp
  const bool haveEdges = !currentSelectedEdges().isEmpty();
  const bool showPsi   = edgeMode && haveEdges;     // independent of haveBoundaryEdge
  if (m_actConveySpin) m_actConveySpin->setVisible(showPsi);
  m_conveySpin->setEnabled(showPsi);
  ```
- No changes to `commitBCParam()` — ψ is committed by its own slot.

Verify: manual smoke against `examples/demo_capped_street` — select an interior edge, ψ widget visible; select a boundary edge, BC widgets AND ψ visible; deselect, all hidden; toggle to vertex mode, ψ hidden.

### 3.4 Property adapter
File: `include/ui/properties/meshedgepropertyadapter.h` + cpp.
- Add `Q_PROPERTY(double bcConveyance READ bcConveyance WRITE setBCConveyance NOTIFY changed)` (the `bc*` naming convention matches the existing properties; the adapter is the per-edge view, not the BC view, so the prefix is consistent within the file even though ψ isn't a BC).
- Getter reads `m_bc[flat].conveyance`; setter calls `applyMeshEdgeConveyance` through the layer.
- No new state, no new signal.

### 3.5 INP read / write
Files: `src/mesh/inpmeshreader.cpp`, `src/mesh/inpmeshwriter.cpp`.

Reader:
- Add `[2D_EDGE_CONVEYANCE]` to the section dispatch (next to `kSecBC`). Per row `FROM_VERTEX TO_VERTEX CONVEYANCE`:
  - Look up the (tri, edge) for the vertex pair. The BC reader already does a vertex-pair → (tri, e) lookup for boundary edges; conveyance applies to interior edges too, so the lookup must walk all edges. Likely refactor: extract the existing lookup into a helper that takes a `boundaryOnly` flag, and call it with `false` here.
  - Clamp + warn on out-of-range. Apply via `applyMeshEdgeConveyance` (post-construction so the mirror is correct).

Writer:
- Add `[2D_EDGE_CONVEYANCE]` to the section order list (`kSecOrder`-equivalent at line ~152). Emit it after `[2D_BOUNDARY_CONDITIONS]` for adjacency with the other 2D mesh sections.
- Iterate `m_bc`; for each slot with `conveyance != 1.0` (default), emit `FROM_VERTEX TO_VERTEX CONVEYANCE`. Canonicalise interior edges on `tri < nbr_tri` to avoid duplicate rows.
- Section header written only when ≥1 non-default row exists (matches what the writer does for empty `[2D_BOUNDARY_CONDITIONS]` — confirm by reading lines 372–395).

Verify: extend `test_meshinpbcroundtrip.cpp` with a fixture that has (a) one interior ψ=0.3, (b) one boundary ψ=0.7, (c) defaults everywhere else. Write → re-read → values match; no duplicate rows for the interior edge.

### 3.6 Tests
- §3.5 round-trip extension (covers reader + writer).
- §3.2 unit test for `applyMeshEdgeConveyance` (mirror + clamp + signal).
- Existing `test_meshinpbcroundtrip.cpp` BC cases unchanged (regression guard).
- No new toolbar GUI test — the toolbar is exercised manually per §3.3 (matches the §V.VC slice's testing posture).

## 4. Out of scope (call out, don't do)

- Visualising ψ on the renderer (colour edges by attenuation). Future slice.
- Group-level "set ψ for all edges in group X" — ships via the §V.VD Group submenu, not this slice.
- A ψ control on the future Property Browser's multi-edge view. Property Browser is single-edge today; multi-edge editing remains on the toolbar.
- A "mixed value" indeterminate state for the toolbar's ψ spinbox under mixed multi-edge ψ. Will land in the same §V.VC.2 slice that adds it for BC params.
- Changes to `.oswp` serialization. If `.oswp` snapshots `MeshEdgeBC` whole, this rides; if it serializes field-by-field, that's a one-line addition — flag once we look. (Not blocked on this slice.)

## 5. Sequencing

1. §3.1 (struct field) → verify: compiles, no behavioural change.
2. §3.2 (layer helper + test) → verify: green unit test.
3. §3.5 (reader + writer + round-trip test) → verify: round-trip green.
4. §3.3 (toolbar widget) → verify: manual smoke against demo_capped_street.
5. §3.4 (property adapter) → verify: existing Property Browser tests still pass; ψ surfaces in the per-edge view.

Each step is independently mergeable. §3.3 and §3.4 both depend on §3.1 and §3.2.

## 6. Open questions

1. **Suffix glyph** — " ×" (multiplier feel) vs no suffix vs "ψ" inside the spinbox? `QDoubleSpinBox::setSuffix` takes plain text; ψ inside the spinbox value field reads oddly. Recommend " ×".
2. **Reject vs clamp** for out-of-range values — engine `swmm_2d_set_edge_conveyance` returns `SWMM_ERR_BADPARAM` (strict reject). INP parser warns and clamps. Symmetric strict-reject in `applyMeshEdgeConveyance` keeps the GUI in line with the C API; clamp would be friendlier in the toolbar (the QDoubleSpinBox range already prevents the issue from the toolbar). My vote: strict reject in the helper, rely on the spinbox range to prevent OOB at the UI.
3. **Insertion point on the toolbar** — after Browse "…" (right of BC controls) reads as "and finally, conveyance". Alternative: directly after the edge info label, before the BC combo. The "after Browse" placement groups ψ with the BC controls visually but lets it survive when BC is hidden — recommended.

---

*Per `CLAUDE.md` §1, please confirm Option A vs B vs C before I touch code. Per `CLAUDE.md` §4, the verifications in §3 will drive the loop without further clarification.*
