# Flat Road-Culvert Demo (1D–2D coupled)

A flat, higher-resolution variant of `demo_weir_culvert` showing a circular
culvert at grade (crown flush with the surface) passing **under a roadway
embankment**, with the buried 1D pipe 2D-coupled to the surface mesh. Its main
teaching point is the **alternating-diagonal mesh** that removes the spurious
north-edge pooling of the legacy demo (see below).

Open `road_culvert.oswp` in the GUI and animate `road_culvert.2d.h5`.

## Model

| Item | Value |
|---|---|
| Domain | 1000 m (X, along-channel) × 100 m (Y, cross) |
| Terrain | **Flat**, z = 1.0 m everywhere (zero slope) |
| Roadway | Transverse embankment 0.5 m high at X = 500 ± 10 m (cell-wide plateau so the crest truly blocks to 1.5 m), full width |
| Mesh | 10 m cells (101 × 11 vertices, 2000 triangles), **alternating diagonal** |
| Culvert | 0.6 m circular, Y = 50, X = 470→530 (60 m — just the road crossing), **crown at grade** (invert one diameter below) |
| Coupling | 4 nodes: ends J_P00 / J_P03 (J_P03 a FREE outfall) + road flanks J_P01 / J_P02; exchange AREA = barrel area (0.283 m²) |
| Rainfall | SCS Type II 24-h, 900 mm, broadcast uniformly to every 2D cell (sized to overtop the road) |
| Outflow | NORMAL_FLOW on the X = 1000 edge (nonzero outlet slope so the flat basin drains) |

> **2026-07-18 adjustments (conveyance + performance).** The original layout
> (Ø0.15 m × 500 m at zero slope, coupling AREA 0.4 m²) could only convey
> ~0.003 m³/s — a straw. ~17 ML drained into the coupled junctions, could not
> pass, and flooded straight back onto the mesh (no downstream discharge ever
> appeared, and the drain/spill churn dominated the run time). The culvert is
> now Ø0.6 m over just the 60 m road crossing, the exchange AREA is matched to
> the barrel cross-section, the downstream end is a free outfall, and the model
> runs with THREADS 4, a 10 m mesh, FLUX_DH_EPS 0.01 and MAX_TIMESTEP 10.
> Together with the engine's smoothed coupling-volume delivery (spread over the
> COUPLING_WINDOW instead of a single-routing-step pulse), the upstream pond
> genuinely drains through the barrel and discharges visibly on the lee side.

The pipe **crown sits at the ground surface** (invert one diameter, 0.15 m, below
grade), so the buried pipe reads as lying at grade under the road fill. `MaxDepth`
is set to the pipe diameter, so the coupling spill threshold
(`z_top = invert + MaxDepth`) lands **exactly on the surface**: ponded water enters
the barrel as soon as it reaches grade, and a surcharged pipe spills back onto the
mesh.

> **Why crown-at-grade, not invert-at-grade?** A literal invert-at-surface
> (`PIPE_BURIAL = 0`) puts `z_top` a full diameter *above* grade; at the
> original 100 mm rainfall the ~0.1 m pond never reached it and the culvert
> stayed **inert** (0 flow). Crown-at-grade drops the gate to the surface so
> the culvert engages. Set `PIPE_BURIAL = 0` in `generate_model.py` to
> reproduce the inert case.

**Behavior on flat terrain.** Rain ponds **symmetrically across Y** (the
alternating-mesh fix below — no north/south bias), water sheet-flows east to the
NORMAL_FLOW outlet, and the culvert engages and fills once ponding reaches grade.
The upstream half is walled on three sides with only the tiny culvert as an
outlet, so the pond tracks the accumulated rainfall almost directly. At the
900 mm depth the upstream water surface reaches the **1.5 m road crest at the
SCS Type II peak (hour ~12) and the road overtops for the rest of the run**,
sheeting up to ~8 cm deep across the crest onto the lee side (the original
100 mm event only ever ponded ~0.10 m and the road acted as a pure dam). The road must be a cell-wide plateau for this to work: a
single raised vertex column leaves the flanking cell beds at ~1.17 m and the
dam leaks numerically from that much lower effective crest, so it can never
hold the nominal 0.5 m head. Before overtopping the culvert *equilibrates* the two
sides rather than carrying large net through-flow — with zero terrain slope the
early-storm driving head across the road is small. The 2D mass balance closes
tightly (≈ 0%); the 1D routing continuity reads a few-percent "error" that is a
divide-by-near-zero artifact on the small pipe-fill volume (the pipe is a
coupled dead end with no 1D outfall).

## Why the legacy demo pooled water on the north edge

In `demo_weir_culvert` water drifts toward the **north** edge even though the
terrain is flat in Y and rainfall is uniform. That is a **mesh-orientation
artifact, not physics**:

- The 2D solver's flux depends only on cell state + edge geometry (no directional
  bias), and rainfall is applied uniformly to every cell.
- The legacy mesh splits **every** quad along the **same SW→NE diagonal**. A
  north↔south reflection turns each SW→NE diagonal into an NW→SE diagonal — a
  *different* mesh. So the discretization is **not** N–S symmetric even though the
  physical problem is, and the numerical solution develops a spurious cross-slope
  drift that piles water against one edge.
- (The east–west ponding against the weir/road *is* physical — water is dammed and
  the domain is walled on three sides.)

**The fix used here** is an **alternating ("union-jack") diagonal**: even
`(ix+iy)` cells split SW→NE, odd cells split NW→SE. The two orientations cancel,
restoring N–S symmetry, so a uniform problem gives a symmetric response.

To *prove* the cause, temporarily force a single diagonal in `generate_model.py`
(remove the `(ix+iy) % 2` branch in `[2D_TRIANGLES]` and the boundary emission):
the northern pooling reappears, and flipping the single diagonal to NW→SE moves
it to the south.

## Regenerate / run

```bash
python3 generate_model.py        # writes road_culvert.inp
python3 _validate_mesh.py         # structural checks (counts, diagonal, BC edges)
<openswmm-cli> road_culvert.inp   # writes road_culvert.{out,rpt,2d.h5}
```

The 5 m / full-domain run takes ~35 min (Apple Silicon, serial CVODE): the
storm's flooded phase integrates at ~30 ms implicit steps, which dominates the
wall time. `generate_model.py` exposes `DX`/`DY` (raise for a faster, coarser
run — keep `ROAD_HALF_WIDTH >= DX` so the crest stays cell-wide) and the
flat-terrain / alternating-diagonal / at-grade-culvert parameters at the top.
`[2D_OPTIONS] COUPLING_WINDOW` and `MAX_CVODE_STEPS` matter: the window must be
large enough to amortize CVODE restarts, and the step budget large enough that
windows COMPLETE — a failed window freezes the 2D surface and drops that
window's rainfall (a 60-step budget silently lost ~half this storm).

## Files

| File | Role |
|---|---|
| `generate_model.py` | Generator (source of truth for `road_culvert.inp`) |
| `_validate_mesh.py` | Structural validation of the generated mesh |
| `road_culvert.inp` | SWMM model (1D network + inline 2D mesh + rainfall) |
| `road_culvert.oswp` | GUI project file (relative paths) |
| `road_culvert.{out,rpt,2d.h5}` | Engine outputs (generated) |
