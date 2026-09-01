# Minimum Cell Size Enforcement V2 + Graded Sizing — GUI Plan (2026-09-01)

**Status:** PLANNED. Successor to `MIN_CELL_SIZE_ENFORCEMENT_PLAN_2026-08-17.md`,
driven by the measurements in `MIN_CELL_SIZE_TESTING_RESULTS_2026-08-18.md`.

**Mandate (user, 2026-09-01):**
1. The minimum cell size must be **enforced**, not advisory — even where that
   means changing the shape of constraining edges and moving constraining
   points, provided every move stays within a stated tolerance.
2. Mesh size transitions must be **smooth** (graded), reducing the total cell
   count as much as possible.

**Scope:** `openswmm.gui` (SWMMVis) mesh subsystem only. No engine changes; the
engine consumes whatever mesh it is given.

---

## 1. Where V1 stands — what the measurements said

V1 (conditioning + cleanup, shipped from the 2026-08-17 plan) is safe and
well-tested, but the 2026-08-18 results doc §4 is unambiguous:

| Finding | Evidence |
|---|---|
| The **premise holds decisively**: small output cells track input local-feature-size (lfs) violations; dominant causes are `ShortSegment` and `CloseFeatures`; `SmallAngle` matched **zero** of the 300 smallest cells | Results §"Phase 0" — 100/100 smallest cells within one `h` of a violation on all three real models |
| **Welding almost never fires** — 3–200 welds against thousands of violations. Structural cause: on a SWMM model nearly every crowded vertex is a **protected identity** (tagged node Steiner or tagged-conduit endpoint), and the conditioner may never merge identities. The crowding that forces the small cells is exactly the crowding it is forbidden to remove | Results §"…but conditioning does not act on it", item 3 |
| **The floor is not achieved anywhere** in the model table; at `h = 8` on the 2383 model conditioning produced a **6× vertex explosion with a *smaller* minimum cell** (5.2e-11), and the response is non-monotonic in `h` — undiagnosed | Results table, items 1–2 |
| One model (`3440_H&H_Elements (1)`) **abandons at every h** (census fail-safe) | Results table |
| **No grading exists.** `RefineHook::targetAreaAt` is installed but returns a spatial constant (`meshgenerationdialog.cpp:1775`); sizing is otherwise a single global `-a` cap. Cell size is emergent, area ranges span 5–8 orders of magnitude, and the only way to refine near features is to refine everywhere | Survey 2026-09-01; `trirefinehook.cpp:89-97` |

Levers that already exist and are reused (not duplicated) here:

- **Demotion idiom**: `greedyMinSeparation` (`pslgprep.cpp`) demotes a crowded
  SWMM node from pinned vertex to *cell* coupling; `mapNodesToMesh` resolves it
  by containing triangle. This is the proven answer to "identity in the way".
- **`MinSizePolicy::allowIdentityMerge`** (`pslgminsize.h:138`,
  `pslgminsize.cpp:1084-1150`): implemented, tested (`test_pslgminsize`
  identity-merge opt-in slot), **off by default and never set by the dialog**.
  Exactly the lever Results §"Suggested next step" asked to decide on.
- **`MinSizePolicy::maxDeviation`** (default `h/2`): the "tolerance" for shape
  change already has a home; V2 makes it govern *all* permitted moves.
- The `-u` hook machinery, the region-bound activation clamp
  (`meshgenerationdialog.cpp:860-876`), and the switch-string tests from V1
  Phase 4 are the substrate the size field rides on.

---

## 2. Design position

**Enforcement (Track A).** V1 treated protected identities as immovable and
unmergeable; the measurements show that makes the feature inert on real SWMM
models. V2's rule: **an identity is a *coupling contract*, not a pinned
coordinate.** The contract is honoured if, after meshing, every SWMM node/link
still couples to the mesh — by vertex where possible, by containing cell where
the geometry had to give (the demotion idiom). Within the user's tolerance the
conditioner may therefore move, weld, or demote identities; what it may never
do is *drop* one.

**Grading (Track B).** Replace the constant `targetAreaAt` with a
Lipschitz-limited size field:

```
h(x) = min(h_max, h_feat + g · d(x))          A(x) = (√3/4) · h(x)²
```

where `d(x)` is distance to the nearest constrained feature, `h_feat` is the
near-feature size (≥ the enforced minimum `h`), `h_max` derives from `maxArea`,
and `g` is the gradation slope (one new user knob). This is what makes the mesh
fine only where features demand it and coarse everywhere else — the cell-count
reduction — while the Lipschitz bound is precisely the "smooth transition"
guarantee: adjacent cells differ in size by at most a factor ≈ `1 + g·(edge/h)`.

The two tracks compose: A raises the true floor (input lfs), B stops
`minAngle`/`maxArea` refinement from wasting cells far from features and
smooths the seam between the enforced fine scale and the background scale.

---

## 3. Track A — make enforcement real

### A1. Couple node separation to `h`

`nodeMinSeparation` / `greedyMinSeparation` runs upstream of conditioning and
already handles node–node crowding — but it is an independent knob (default
2 m) with no relation to `h`. When enforcement is on, default the effective
separation to `max(nodeMinSeparation, h)` in `collectInputs`, so requesting
`h = 8` cannot leave two pinned nodes 2 m apart for the conditioner to choke
on. (Likely a direct contributor to the `h = 8` explosion; Phase 0 confirms.)

### A2. Identity welds and demotions inside the conditioner

Enable and extend the existing machinery under a new enforcement mode:

- **Identity–identity within `identityMergeRadius`** → weld via
  `allowIdentityMerge` (exists). The survivor keeps its marker; the loser's
  SWMM id is recorded on a new `PipelineInputs` demotion list and coupled by
  containing cell post-mesh — the same bookkeeping `greedyMinSeparation`
  demotion already feeds. Verify the mapper resolves *both* ids.
- **Tagged node vs non-incident segment closer than `h`** → project the node
  onto the segment; if the move is ≤ `maxDeviation`, weld it into a split
  vertex on the segment (node keeps its marker, segment gains a vertex);
  otherwise demote the node to cell coupling. Today this pair is only a
  reported `CloseFeatures` residual.
- **Tagged conduit endpoint vs identity within `weldRadius`** → move the
  endpoint onto the survivor when ≤ `maxDeviation` (edge markers and tags ride
  along); otherwise demote per above. Endpoints stop being categorically
  immovable — that is the user's explicit tolerance grant.

All moves accumulate into `ConditionReport::maxDisplacement`; every demotion is
counted and listed. The census fail-safe (`PslgCensus`) stays the outer guard.

### A3. Diagnose the `h = 8` explosion and the 3440 abandonment — Phase 0 gate

Non-monotonic vertex counts mean conditioning at some `h` *creates* new
sub-scale features (weld/splice interactions, or A1's gap). Instrument: after
conditioning, rerun `analyseLocalFeatureSize` on the conditioned PSLG and log
the violation census before vs after. Conditioning that *increases* the
violation count at any severity must abandon (extend the fail-safe), not mesh.
For 3440, classify which census family trips at every `h` and whether A2's
welds resolve it.

### A4. The enforcement contract

After conditioning: `predictedMinLfs ≥ c·h` (c ≈ 0.5) **or** every surviving
violation appears in `residuals` with a cause and coordinates. After meshing:
the completion summary states *requested `h` / achieved min cell side /
residual count*, from `computeCellAreaStats`. "Enforced" means the user can see
exactly what was achieved and why anything fell short — never a silent miss.

### A5. Post-mesh cleanup, matching relaxation

`collapseSubScaleCells` currently protects every marker-bearing edge and
coupled vertex (`meshminsizecleanup.h` defaults; `allowIdentityCollapse =
false`, `beta = 0.35`, neither surfaced). Under enforcement mode set
`allowIdentityCollapse = true` (with the existing rollback-on-lost-coupling
guard) and raise `beta` toward 0.5; expose both in the advanced expander.
Results doc measured 48k–4.25M sub-scale cells protected in these runs — this
is the second-largest untapped pool after A2.

---

## 4. Track B — graded size field

### B1. Distance field

Build once per generation, worker-side, after constraint assembly (all
conditioned segments + mandatory Steiner points known) and before
`g.generate()`:

- Uniform background grid over the domain bbox, pitch `p = max(h, h_max/8)`
  (bounded cell count; clamp total cells to a budget ~4M with pitch growth).
- Seed cells intersecting constrained segments / mandatory vertices at
  distance 0 — reuse the segment spatial-hash idiom
  (`meshgenerationdialog.cpp:1521` area) for rasterisation.
- Two-pass chamfer distance transform (deterministic, O(grid)).
- `targetAreaAt(x,y)`: bilinear-sample `d`, apply the formula, clamp to
  `[A_min, refinementAreaCap(maxArea)]`. The existing
  `refinementAreaCap` rule (floor never forces refinement when no cap) stays
  the boundary condition — its regression tests must keep passing.

### B2. Knobs and defaults

- `sizeGradation g` — one spin, default **0.25** (≈ area ratio 1.6 between
  neighbours at the fine scale; typical mesher growth rates 1.1–1.3 map to
  g ≈ 0.1–0.3). `0` = off → constant field (today's behaviour).
- `h_feat` defaults to `h` when enforcement is on, else to the side of the
  equilateral triangle of `maxArea/16` (feature refinement without enforcement).
- Grading with `maxArea = 0` (no cap): field is unbounded above → return ≤ 0
  (unconstrained) beyond the distance where `h(x)` exceeds the domain scale;
  do **not** silently impose a cap the user didn't set.

### B3. Known traps (all previously measured — do not rediscover)

1. Installing any `targetAreaAt` drops the numeric `-a` and **activates
   per-region bounds** (`meshgenerator.cpp:514-518`; bare `a` sets `vararea`).
   The clamp at `meshgenerationdialog.cpp:860-876` and V1's Phase 4
   switch-string tests already cover this — extend them for the graded lambda,
   don't regress them.
2. The field only affects the `-u` hook → **no `MeshStageCache` change**
   (neither Stage A nor B payload depends on it). If a later phase makes ring
   densification grading-aware, that parameter goes **into `boundaryKey()`**,
   not a `kFormatVersion` bump (house position,
   `MESH_SCALABILITY_PLAN_CRITICAL_REVIEW_2026-08-03.md` §C3).
3. Determinism: grid + chamfer is deterministic; keep it serial or
   band-parallel with fixed merge order. `test_pslgminsize` /
   `test_meshminsizecleanup` determinism slots are the pattern.
4. `h = 0` **and** `g = 0` must reproduce the unconditioned mesh bit-for-bit —
   `minCellSizeOff_reproducesTheUnconditionedMesh` (`test_pslgminsize.cpp`)
   plus a new `gradingOff_reproducesTheConstantHookMesh`.

### B4. Terrain Steiner coupling (secondary)

The Poisson-disk `minSpacing` filter and `terrainBoundaryBuffer`
(`meshgenerationdialog.cpp:1388-1530`) are the *other* density control and will
fight the field (dense terrain points in a region the field wants coarse forces
refinement anyway). Once B1 lands, thin terrain candidates against the local
`h(x)` (spacing ≥ k·h(x)). Separate phase; measurable cell-count win on
DEM-driven projects.

---

## 5. Cell-count levers, ranked

1. **Grading (B)** — the structural win: fine near features only.
2. **`minAngle` preference default 33° → 26°** (`preferencesmanager.h:506`;
   `meshgenerator.h:68-70` documents 2–4× vertices at 33° for no practical
   benefit). **Open question §8.1 — reverses the user's 2026-07-31 decision;
   needs explicit sign-off, not a silent default flip.**
3. **A2/A5 identity relaxation** — removes forced tiny-cell clusters (Bellinge
   mechanism: `bellinge-mesh-tinycell-stiffness` — 290 cells < 10 m² from
   ~1,020 forced coupling nodes).
4. **B4 terrain coupling.**

---

## 6. Phases and verification gates

Each phase independently shippable and revertable. Fixtures and artifacts go to
`tests/output/mesh_minsize2/` (CLAUDE.md §4.1 — user-reviewable, never temp).
Models: `2383_H&H_Link_Elements`, `2300_H&H_Elements`, `3440_H&H_Elements (1)`
(the V1 measurement set) + Bellinge for the coupled-runtime check.
`tests/manual/mesh_minsize/` probes (`lfs_premise_probe`,
`conditioning_effect_probe`) are the harness — extend, don't fork.

### Phase 0 — Diagnosis. **Gate for Track A.**
Instrument before/after violation census (A3). Reproduce the `h = 8` explosion
and attribute it; classify the 3440 abandonment.
**Verify:** a written explanation of the non-monotonicity with the census
numbers; the abandon-on-worse-census guard in place and tested. If the
explosion is *not* explainable as conditioning-created features or the A1 gap,
stop and re-plan.

### Phase 1 — A1 + A2 identity welds/demotions.
**Verify:** unit — both SWMM ids couple after an identity weld; node-onto-
segment weld respects `maxDeviation`; endpoint moves carry markers/tags;
demotion list flows to the mapper; determinism; census fail-safe still trips on
planarity damage. End-to-end on the three models: weld+demotion count now the
same order as the violation count (vs 3–200 before), and **minArea rises
monotonically with `h`** on 2383 and 2300; 3440 either conditions or every
residual is explained.

### Phase 2 — A4 contract + A5 cleanup relaxation.
**Verify:** `predictedMinLfs ≥ 0.5·h` or enumerated residuals on all three
models; achieved-vs-requested line in the completion summary; cleanup with
`allowIdentityCollapse` keeps `collapseIsDeterministicAcrossProcesses` and the
rollback tests green; protected-cell count drops materially from the 48k–4.25M
baseline.

### Phase 3 — B1–B3 size field.
**Verify:** switch-string net extended (graded hook: no numeric `-a`, bare `a`
with regions, `h=0,g=0` byte-identical string); field unit tests (chamfer
error ≤ 8% vs exact on fixtures, Lipschitz bound holds, budget clamp);
end-to-end on 2300: **total cell count drops ≥ 30% at equal min cell size and
equal near-feature size**, and neighbour-area-ratio p99 ≤ 2.5 (new
`meshcellstats` metric — add it first, it is the smoothness acceptance number).

### Phase 4 — B4 terrain coupling.
**Verify:** DEM project cell count drops further with unchanged near-feature
density; stage-B cache behaviour unchanged (`keys_changeWithEveryInput`
extended for any new key input).

### Phase 5 — UI, docs, CHANGELOG.
Mode control in the min-size group: **Off / Advisory (V1 behaviour) / Enforce
(may move constraining geometry within tolerance)** — Enforce surfaces
`maxDeviation` as "Geometry tolerance" with the unit label, plus the gradation
spin. Advisory remains the default: existing projects reproduce their meshes.
**Verify:** `test_meshmincelldialog` extended (mode enablement, tolerance
round-trip via the derived label, gradation default); `h=0` bit-identical
regression re-run on two real projects; CHANGELOG per CLAUDE.md §5.2.

### Acceptance (whole plan)
On 2383 and 2300 at a sensible `h`: achieved min cell side ≥ 0.5·h with all
shortfalls enumerated; vertex count ≤ the unconditioned baseline (the `h=8`
class of regression impossible by the Phase 0 guard); ≥ 30% fewer cells at
equal fidelity with grading on; neighbour-area-ratio p99 ≤ 2.5. On Bellinge:
min cell ≥ 1 m² and coupled 8 h probe wall time ≤ the re-mesh spike's 237 s
class.

---

## 7. Risks and explicit tradeoffs

1. **Moving coupling points changes hydraulics slightly.** A node moved ≤
   `maxDeviation` couples at a slightly different location; a demoted node
   couples by cell (as `nodeMinSeparation` already does). Bounded, counted,
   reported — never silent. `domainAreaBefore/After` still reported for ring
   moves.
2. **Demotion changes coupling type.** Cell coupling is an established,
   shipped path, but a model comparison across V1→V2 will show different
   `[2D_TRIANGLE_NODE_MAP]` entries. CHANGELOG + report line.
3. **The abandon-on-worse-census guard may make some models refuse
   conditioning** where V1 "applied" (uselessly). That is correct behaviour
   but reads as a regression; the report's `abandonReason` must say why.
4. **Grading interacts with `-q`**: Triangle refines for angle regardless of
   the field; near-feature cells stay governed by lfs. The field bounds size
   from above only — Track A remains the only true floor mechanism.
5. **Bit-exactness commitments**: `h=0` no-op, cleanup no-op, cache-hit ring
   read-only (`ringsReadOnly`) all must survive; they are the regression
   contract with every existing project.

---

## 8. Open questions for user review

1. **`minAngle` default 33° → 26°?** Biggest free cell-count win; reverses the
   2026-07-31 decision. Recommend 26° *only in Enforce mode* as a compromise.
2. **Default `maxDeviation` = `h/2`** — is half a cell the right tolerance
   grant, or should Enforce mode ask for it explicitly the first time?
3. **May Enforce move domain-boundary ring vertices** (wetted-area change) or
   only interior constraints? V1 allowed ring moves up to `weldRadius`;
   keeping that plus reporting is the recommendation.
4. **Gradation default 0.25** — accept, or calibrate on Bellinge in Phase 3?
5. Ship Phase 3 (grading) before Phases 1–2 (enforcement)? They are
   independent; grading alone already delivers the cell-count goal on models
   whose floor problems are tolerable.
