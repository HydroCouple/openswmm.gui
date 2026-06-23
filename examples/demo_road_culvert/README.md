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
| Roadway | Transverse embankment 0.5 m high at X = 500, full width |
| Mesh | 5 m cells (201 × 21 vertices, 8000 triangles), **alternating diagonal** |
| Culvert | 0.15 m circular, Y = 50, X = 250→750, **crown at grade** (invert one diameter below) |
| Coupling | 4 nodes: ends J_P00 / J_P50 + road flanks J_P24 / J_P26 |
| Rainfall | SCS Type II 24-h, 100 mm, broadcast uniformly to every 2D cell |
| Outflow | NORMAL_FLOW on the X = 1000 edge (nonzero outlet slope so the flat basin drains) |

The pipe **crown sits at the ground surface** (invert one diameter, 0.15 m, below
grade), so the buried pipe reads as lying at grade under the road fill. `MaxDepth`
is set to the pipe diameter, so the coupling spill threshold
(`z_top = invert + MaxDepth`) lands **exactly on the surface**: ponded water enters
the barrel as soon as it reaches grade, and a surcharged pipe spills back onto the
mesh.

> **Why crown-at-grade, not invert-at-grade?** A literal invert-at-surface
> (`PIPE_BURIAL = 0`) puts `z_top` a full diameter *above* grade; on this flat
> terrain the ~0.1 m pond never reaches it and the culvert stays **inert** (0 flow).
> Crown-at-grade drops the gate to the surface so the culvert engages. Set
> `PIPE_BURIAL = 0` in `generate_model.py` to reproduce the inert case.

**Behavior on flat terrain.** Rain ponds **symmetrically across Y** (the
alternating-mesh fix below — no north/south bias), water sheet-flows east to the
NORMAL_FLOW outlet, and the culvert engages and fills once ponding reaches grade.
Because the terrain is **truly flat (zero slope)**, both sides of the road pond
equally, so there is **no driving head** across the road — the culvert
*equilibrates* the two sides rather than carrying large net through-flow. The 2D
mass balance closes tightly (≈ 0%); the 1D routing continuity reads a few-percent
"error" that is a divide-by-near-zero artifact on the small pipe-fill volume (the
pipe is a coupled dead end with no 1D outfall). **For visible net through-flow**
add a gentle longitudinal slope (set `SLOPE = 0.001`) so water collects upstream
and drives flow through the culvert, and/or a downstream 1D outfall.

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

The 5 m / full-domain run is multi-hour (≈4× the cells of the 10 m legacy demo).
`generate_model.py` exposes `DX`/`DY` (raise for a faster, coarser run) and the
flat-terrain / alternating-diagonal / at-grade-culvert parameters at the top.

## Files

| File | Role |
|---|---|
| `generate_model.py` | Generator (source of truth for `road_culvert.inp`) |
| `_validate_mesh.py` | Structural validation of the generated mesh |
| `road_culvert.inp` | SWMM model (1D network + inline 2D mesh + rainfall) |
| `road_culvert.oswp` | GUI project file (relative paths) |
| `road_culvert.{out,rpt,2d.h5}` | Engine outputs (generated) |
