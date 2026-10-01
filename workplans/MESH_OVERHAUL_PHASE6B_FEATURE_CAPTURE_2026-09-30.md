# Mesh Overhaul — Phase 6b: Feature Capture — 2026-09-30

Status: PROPOSED (owner review). Amends `workplans/MESH_OVERHAUL_PLAN_2026-09-29.md`; runs before
Phase 7. Nothing here is implemented.

## 1. Why

Owner objective restated 2026-09-30: *"coarse meshes where the resolution is unneeded and fine where
it is, and triangular where it will allow better representation … naturally capture streets, and the
details of curbs and buildings, with the same algorithm."*

Against the delivered Phases 0–6:

| Objective | State after Phase 6 | Gap |
| --- | --- | --- |
| Coarse where unneeded, fine where needed | Met by the size field (feature distance, terrain error, region size, clamped to [min cell, cell × coarsen]) | Range is set by *coarsen factor*, not derived from the terrain; features always refine to *cell size* even on flat ground |
| Smooth transitions | Met (gradation-limited field, 2:1 tree, fringe ≤ 2.1) | — |
| Triangles where they represent the surface better | **Not met.** D1 made cell shape a global switch (quads everywhere / triangles everywhere) | No mechanism puts triangles at features and quads on open ground |
| Capture streets, curbs, buildings | **Not met.** The terrain term shrinks cells near a curb but no mesh edge lies on it; the lattice phase is arbitrary | Break lines must be extracted from the DEM and fed in as constraints |

The generator already conforms to any polyline it is given (quadtree keeps clear, CDT fringe
conforms) and that fringe is triangles paired into quads only where the shape allows. So the
mechanism for "triangles along the feature, quads on the open surface" exists; what is missing is the
*source* of the feature lines and a size rule that lets open ground go genuinely coarse.

## 2. Design

Three additions, all inside the existing architecture (size field → quadtree core → CDT fringe →
assembly). No new dependency.

### 2.1 Terrain break-line extraction (new module `mesh/terrainbreaklines`)

Input: the DEM band already streamed by `TerrainSizeField` (same GDAL handle, same band loop, one
extra row of overlap). Output: polylines in DEM pixel coordinates, converted to mesh coordinates by
the worker's existing DEM→mesh transform, appended as `ConstraintSegment`s with a reserved marker
(`terrain`, non-coupling, never trimmed by the straightness pass beyond the tolerance below).

Algorithm (per band, O(pixels), no allocation beyond the band and the chains):

1. **Step response.** Central-difference gradient `g = (∂z/∂x, ∂z/∂y)` in elevation per metre.
   A pixel is a *step candidate* when `|g| · p ≥ tolerance` — the surface rises by at least the
   terrain tolerance across one pixel of size `p`. This catches curbs (≈0.12–0.20 m over one lidar
   pixel), building walls, channel banks, retaining walls. It does not catch gentle ridges by
   design (those are already sized by the error term; they carry no edge to align to).
2. **Thin to one pixel.** Non-maximum suppression along the gradient direction (Canny step), so a
   3-px wide curb response becomes a single line of pixels at the steepest point.
3. **Link.** Hysteresis (high = tolerance, low = ½ tolerance) and 8-connected chaining into
   polylines; junctions split chains. Chains shorter than `4 · min cell size` are dropped (noise,
   isolated rubble, vegetation).
4. **Simplify.** `trimByStraightness(chain, 5°, 0.25 · min cell size)` — the existing routine —
   then `resampleAtSize(h)` happens later in the generator like every other constraint.
5. **Merge with vector features.** A terrain chain within `min cell size` of a user-supplied
   breakline, conduit, corridor edge or hole ring is dropped (the vector feature is authoritative).

Building footprints from a vector layer keep working as they do today (hole rings or breaklines);
with a DSM the walls are found by step 1 without a layer.

### 2.2 Cell shape: quads on open ground, triangles at features

`Cell shape` gains a third choice, made the default when a DEM is present:

| Choice | Core (quadtree) | Fringe |
| --- | --- | --- |
| Quads | everywhere | triangles paired into quads where SJ ≥ 0.5 |
| Triangles | split squares | triangles |
| **Quads on open ground, triangles at features** | leaves kept only where the size is *not* terrain-bound (`h_terrain ≥ h_feature` at the leaf) and clear of constraints | triangles, **no pairing** within `2 · h` of a terrain breakline or where the terrain term binds |

The extra leaf predicate is one comparison in `buildQuadtreeCore` (the size field already knows
which term is binding; expose it as `SizeField::terrainBoundAt`). The pairing exclusion is a mask on
the blossom input. Everything else is unchanged.

### 2.3 Coarsening derived from the terrain

Today `h_max = cell size × coarsen factor`. Add: on open ground (farther than `2 · cell size` from
every constraint) the feature term no longer applies — `h = min(h_terrain, h_region, h_max)` — so a
flat field or a flat roof coarsens to `h_max` regardless of a curb 3 m away, and `coarsen factor`
becomes a ceiling rather than the whole story. Gradation limiting still bounds the transition. This
is the "thinning dimension": the terrain decides, the factor only caps it.

### 2.4 Option surface

Two changes, no new group: *Cell shape* gets the third entry (2.2); *Terrain tolerance* now also
drives break-line extraction (2.1) — one number, one meaning ("how much the surface may deviate
before the mesh must follow it"). No new checkbox. If extraction must be switchable, it is a
preference, not a dialog control.

## 3. Phases and gates

| Phase | Work | Gate |
| --- | --- | --- |
| 6b.1 Break lines (1 wk) | `terrainbreaklines` module, unit tests on synthetic DEMs | On a synthetic street (crown, two 0.15 m curbs, a 3 m building block, 0.5 m pixels): extracted lines within 1 px of the true curb and wall lines, ≥ 95 % of their length recovered, no line on the carriageway or roof; Bellinge SRTM (30 m) yields **no** lines at tolerance 0.5 m (nothing to find at that resolution — a false-positive check) |
| 6b.2 Mixed cell shape + terrain coarsening (1 wk) | leaf predicate, pairing mask, open-ground size rule, dialog entry, preference | Synthetic street: every curb and wall segment is a mesh edge; cells touching them are triangles; carriageway and roof are quads at `h_max`; grading gates from Phase 4 still hold (max ≤ 2.1, P50 ≤ 1.5) |
| 6b.3 Wire-up (3 d) | worker: extraction after the burn (a burned channel's banks are steps too), cache key includes tolerance, stage cache format 4 | `test_meshterrainpipeline` gains a street case; CHANGELOG |
| then Phase 7 | validation as in `MESH_OVERHAUL_PHASE7_HANDOFF_2026-09-30.md`, plus one urban case with a lidar DEM supplied by the owner | acceptance table |

Efficiency budget (objective "very efficient"): extraction ≤ 1 s per 100 M pixels single-thread,
streamed; no change to the 1 M-cell ≤ 10 s gate.

## 4. Decisions needed from the owner

1. **Approve 6b before Phase 7?** Recommended: Phase 7 would otherwise validate a design already
   known not to meet objective 4.
2. **DEM resolution assumption.** Curb capture needs pixels ≤ ~0.5 m (lidar DTM/DSM). At 1–2 m a
   curb is one pixel of mixed elevation and shows as a slope, not a step; the extraction still
   finds walls and banks. Confirm the DEMs in use.
3. **Buildings: hole or wall?** With a DSM the walls become break lines and the roof is meshed
   (coarse quads) — flow over the roof is then modelled. If buildings should be holes, supply a
   footprint layer (already supported). Default proposed: mesh them (no layer needed), holes when a
   layer is given.
4. **Streets with known geometry.** Where a street-centreline layer exists, corridors already give
   aligned quads with curb rows. 6b does not change corridors; it makes them optional.

## 5. Risks

- Noisy lidar → many short chains: the length filter and hysteresis are the controls; the Bellinge
  false-positive gate guards the coarse end, the synthetic street the fine end. A real lidar tile
  from the owner is the third case (Phase 7).
- Dense curb networks → many constraints: CDT cost is linear in vertices (100 k in 0.2 s); the
  fringe grows, the core shrinks. Measured, not assumed.
- Steps 2.1–2.3 each change the mesh on every project with a DEM; the stage-cache version bump
  makes old caches miss rather than mismatch.
