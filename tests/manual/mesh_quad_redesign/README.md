# Quad regions — gates 8 and 9 by hand

Companion to `workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md` §7. Gates 1–7
and 8(a)–(f) are automated (`tests/gui/test_meshquad*`, `test_meshcrossfield`,
`test_meshsubmap`, `test_meshquadregion_e2e`; run `ctest -L gui -R 'meshquad|crossfield|submap'`).
This page is the click-through for the rest: the dialog path (gate 9) and the
end-to-end cases the unit harness cannot cover (gate 8 (b)–(d): curved channel,
Bellinge street block, subcatchment with a conduit crossing it).

Everything you generate here goes under `tests/output/quad_redesign_2026-09-06/`
(git-ignored, see its README); save the `.inp` / `.2dm` and a copy of the
Message Log next to it so a reviewer can reproduce the run.

## 0. Build and automated gates first

```bash
cmake --build build --target test_meshquadquality test_meshquadregion test_meshcrossfield \
      test_meshquadpoints test_meshquadmatch test_meshquadcleanup test_meshsubmap \
      test_meshquadregion_e2e test_meshpatch test_meshgenerator
ctest --test-dir build -L gui -R 'meshquad|meshcross|meshsubmap|meshpatch|meshgenerator' --output-on-failure
```

`test_meshquadregion_e2e` writes `tests/output/quad_redesign_2026-09-06/e2e_report.txt`;
compare it with `e2e_report.sandbox-2026-09-06.txt` in the same folder (the
numbers a build is expected to reproduce). Its `knownGaps_*` function carries
one `QEXPECT_FAIL` (a junction pinned inside a Free region leaves ≈ 12 %
triangles; ≥ 85 % is asserted). The other two gaps the suite found (exterior
terrain cloud splitting the ring segments; smoothing dropping a quad below the
SJ floor) were fixed on 2026-09-06 and are plain assertions now. If the
remaining one turns into an `XPASS`, delete that `QEXPECT_FAIL`.

## 1. Where the controls are (gate 9)

Mesh generation dialog → **Quality** page → group **Quad regions (PSLG)**:

| Control | Meaning |
|---|---|
| Region layer | Polygon layer whose exterior rings become regions. Optional per-feature attributes `quad_mode` (`auto`/`mapped`/`submapped`/`free`/`triangles`), `quad_spacing`, `quad_aspect`, `quad_angle`, `tag`/`name` override the defaults below. |
| Subcatchments | Comma-separated subcatchment IDs; each polygon becomes one region tagged `subcatch_<ID>`. An unknown ID stops generation with an error. |
| Default mode | Auto (Mapped → Submapped → Free by outline), or force one. |
| Default spacing | Target quad edge `h` (map units). 0 = derive from max area. |
| Default max aspect | Longest/shortest side accepted for Free regions (plan default 2.0). |
| Default alignment | Constant cross-field angle for Free regions; special value = align to the ring. |

Two neighbouring groups on the same page are NOT part of this: **Quad quality**
holds the legacy triangle-pair merge (experimental, off) and **Structured quad
patches** the G3 transfinite / swept patches. Leave both alone for these checks
so every quad you see comes from a region.

## 2. Log lines to expect

Message Log (or `QT_LOGGING_RULES="openswmm.mesh.perf.info=true"` on the console):

```
[Mesh][quad] N regions (L from layers, S from subcatchments; default mode Auto, spacing 5, aspect <= 2)
[Mesh][quad] region 0: Auto -> Mapped | h 5 | 200 quads + 0 tris (0 template, 0 gap) | points 0 generated, 0 terrain dropped | min SJ 1 | median rect 1
[Mesh][quad] region 1: Free -> Free | h 5 | 200 quads + 0 tris (200 template, 0 gap) | points 171 generated, 264 terrain dropped | min SJ 1 | median rect 1
[Mesh] Skipped quad region 2 — quad region vertex 0 lies outside every domain ring
```

One `region i:` line per region, in the order they were added (layer features
first, then subcatchments). A skipped region additionally raises the
`[Mesh] Skipped quad region …` warning, the same channel skipped hole rings use.

Acceptance per region (plan §7.8): quads ≥ 90 % of the cells inside, min SJ ≥
0.866, median rect ≥ 0.85, and for Mapped/Submapped exactly 0 triangles.

## 3. Cases

### 8(a) — rectangle in a triangular catchment, h = 5 m
1. New project, draw (or import) a 300 × 150 m polygon as the domain layer.
2. Draw a 200 × 50 m rectangle inside it as a second polygon layer; pick it as
   *Region layer*, default mode **Free**, spacing 5, no DTM.
3. Generate. Expect `Free -> Free | h 5 | 400 quads + 0 tris`, min SJ 1 —
   4 × 20 × 10 squares of 5 m; triangles only outside the rectangle.
4. Switch default mode to **Auto** → `Auto -> Mapped`, same 400 quads.
5. Attribute table of the mesh layer: sort by *Cells* — no cell straddles the
   rectangle; Metadata tab QuadStats: nonConvex 0, irregular vertices 0.

### 8(b) — curved channel: Swept and Free with a guide
1. Draw a curved channel polygon (two offset arcs closed at the ends) ~10 m
   wide; add its centreline as a polyline layer.
2. Region layer = the channel polygon, mode **Free**, spacing 2.5. The dialog
   does not yet expose `QuadRegion::alignGuide` (the field exists in
   `mesh/meshquadregion.h`; the worker only reads `quad_angle`), so either
   leave alignment on *(from boundary)* — the cross field follows the two
   walls — or set *Default alignment* to the channel's mean bearing.
3. Expect ≥ 90 % quads, median rect ≥ 0.85, and quad rows following the bend
   (no lattice "fighting" the walls). Compare with the legacy Swept patch on
   the same centreline (Quad patches group): cell counts should be similar and
   the Free version must not be worse than 0.866 in min SJ. (Swept patches
   live in the **Structured quad patches** group on the same page.)

### 8(c) — Bellinge: one street block as a Free region
1. Open the Bellinge model (`tests/gui/data/…` or your copy), DTM attached.
2. Region layer = a hand-drawn polygon around one street block, mode **Auto**,
   spacing 4.
3. Expect the block to resolve to Free (the outline is not rectilinear), the
   `terrain dropped` count > 0 (the DTM thinner's points inside the block are
   replaced by the lattice), and everything outside unchanged versus a run
   without the region (compare vertex / cell counts of the two runs outside
   the block in the attribute table).

### 8(d) — subcatchment as region with a conduit crossing it
1. Any model with a subcatchment polygon and a conduit (+ two junctions)
   crossing it. Enter the subcatchment ID under *Subcatchments*.
2. Expect `Auto -> Free` with a note that a constraint segment crosses the
   region (Mapped/Submapped fall back to Free when a segment crosses), the
   junctions kept as pinned lattice vertices, the conduit a locked edge (no quad
   straddles it — check *Cells* in the attribute table along the conduit).
3. Known gap (see `knownGaps_*` in `test_meshquadregion_e2e.cpp`): a pinned
   junction leaves a ring of triangles around it; expect ~85–90 % quads rather
   than ≥ 90 % until the frontal placement handles seeds better.

### Engine round trip (all cases)
Save the model; the `.inp` has `[2D_TRIANGLES]` then `[2D_QUADS]`; reopen and
run a 30-min storm slice: lake-at-rest stays at rest (max |Δh| ≤ 1e-10 after
the first step), 2D continuity error < 0.01 %. Paint an edge-3 BC on a quad and
confirm it survives save/reload.

## 4. What to record

`tests/output/quad_redesign_2026-09-06/<case>/`: the `.inp`, the Message Log
excerpt with the `[Mesh][quad]` lines, and a screenshot of the region boundary.
