#!/usr/bin/env python3
"""
Generate the SWMM .inp for the flat 1D-2D coupled road-culvert demo.

This is the flat / high-resolution / at-grade variant of the
demo_weir_culvert MVP. It is patterned after that model's generator but
makes three deliberate changes (see Context in the plan):

  1. FLAT TERRAIN. The plain is a single elevation z = Z0 everywhere
     (SLOPE = 0). The only relief is the transverse ROADWAY embankment.
  2. HIGHER RESOLUTION. DX = DY = 5 m (vs 10 m).
  3. CULVERT AT GRADE. The pipe CROWN sits at the ground surface (invert
     one diameter below, PIPE_BURIAL = PIPE_DIAMETER), so the buried pipe
     reads as lying at grade under a roadway embankment AND the coupling
     gate sits on the surface so the culvert engages at any ponding.

Domain (X = along the channel / flow direction, Y = cross-flow):
    100 m (Y) x 1000 m (X) flat plain, surface z = Z0 (no slope).

Roadway embankment at X = 500 m, ROAD_HEIGHT high, spanning the full
width (Y). At the mesh resolution the road is the single raised vertex
column at X = 500 - a transverse dam that ponds water on the upstream
side and forces it through the culvert to the lee side.

Culvert at grade under the road (crown flush with the surface):
    51 junctions along Y = 50 at X = 250, 260, ..., 750 (50 segments of
    10 m each, centered on the road). Circular PIPE_DIAMETER, with the
    pipe CROWN at grade (invert one diameter below, PIPE_BURIAL =
    PIPE_DIAMETER) so the coupling gate z_top = invert + MaxDepth lands on
    the surface and the culvert engages at any ponding. Four junctions are
    2D-coupled: the two ends (J_P00 / J_P50) and the two road-flanking
    nodes (J_P24 / J_P26). NOTE: with truly-flat terrain there is no head
    across the road, so the culvert equilibrates the two sides rather than
    carrying net through-flow; add SLOPE > 0 for visible conveyance.

Mesh diagonal (the fix for the north-pooling artifact):
    Each quad is split with an ALTERNATING ("union-jack") diagonal -
    SW-NE on even (ix+iy) cells, NW-SE on odd cells. The legacy demo
    used a single SW-NE diagonal everywhere; that breaks the mesh's
    north-south symmetry and drifts water toward one edge under uniform
    rainfall (the continuum problem is N-S symmetric but a single-
    diagonal mesh is not). Alternating the diagonal cancels the bias so
    a uniform problem gives a symmetric response.

Rainfall:
    SCS Type II 24-hour distribution, 100 mm total depth (~5-year event
    for many mid-Atlantic US sites). Auto-broadcast to every 2D cell by
    the engine (SurfaceRouter2D updateRainfall) - no [SUBCATCHMENTS]
    needed.

Right-edge drainage:
    A NORMAL_FLOW boundary on every mesh edge along X = 1000, emitted as
    rows in [2D_BOUNDARY_CONDITIONS]. On FLAT terrain the Manning outlet
    flux q = (1/n) h^(5/3) sqrt(S) is zero when the slope param is zero,
    so the rows carry a nonzero OUTLET_SLOPE (an outlet energy slope,
    independent of the now-zero terrain slope) or the basin would never
    drain. The alternating diagonal moves which triangle/edge of the
    rightmost quad lies on X = LX, so the emission is parity-aware.
"""

from pathlib import Path

# Domain geometry. DX/DY drive both mesh resolution and runtime; the road
# crest is captured by the single vertex column at X = ROAD_X, so a
# coarser mesh just gives a wider ramp. 5 m over the full plain is ~4x the
# cells of the 10 m legacy demo - expect a multi-hour run.
LX = 1000.0
LY = 100.0
DX = 5.0
DY = 5.0
SLOPE = 0.0          # flat terrain
Z0 = 1.0

# Mesh diagonal. True = alternating ("union-jack") split that cancels the
# north-south mesh anisotropy (the fix). False = a single SW-NE diagonal
# everywhere (the legacy pattern) which drifts water toward the north edge
# under uniform rainfall - set False to REPRODUCE the artifact.
ALTERNATE_DIAGONAL = True

# Roadway embankment (the transverse dam the culvert passes under).
ROAD_X = 500.0
ROAD_HEIGHT = 0.5

# Culvert. The pipe CROWN sits at the ground surface (invert one diameter
# below grade) -> a pipe at grade passing under the road embankment. With
# MaxDepth = PIPE_DIAMETER the coupling spill threshold
# z_top = invert + MaxDepth lands exactly on the surface, so ponded water
# enters the barrel as soon as it reaches grade and the culvert conveys.
# (A literal invert-AT-surface, PIPE_BURIAL = 0, puts z_top a full diameter
# ABOVE grade; the flat-terrain ~0.1 m pond never reaches it and the culvert
# stays inert -- see README. Set PIPE_BURIAL = 0 to reproduce that.)
# The pipe is still discretised in 10 m segments along Y = 50.
PIPE_Y = 50.0
PIPE_X_START = 250.0
PIPE_X_END = 750.0
PIPE_SEGMENT = 10.0
PIPE_DIAMETER = 0.15
PIPE_BURIAL = PIPE_DIAMETER   # crown at grade
PIPE_ROUGHNESS = 0.013

# Outlet energy slope for the X = LX NORMAL_FLOW boundary. Decoupled from
# the (zero) terrain SLOPE so the east edge still drains on flat ground.
OUTLET_SLOPE = 0.001

# Rainfall
RAIN_DEPTH_MM = 100.0

# Coupling
COUPLING_CD = 0.65
COUPLING_AREA_PIPE = 0.4

# Simulation window. SCS Type II peaks at hour ~12; 18 h covers the
# ascending limb, the peak, and a ~6 h recession.
SIM_HOURS = 18
ROUTING_STEP_SEC = 2

# NRCS SCS Type II 24-hour dimensionless cumulative distribution.
# Source: NRCS NEH Part 630, Chapter 4, Table 4-1 (standard table).
SCS_TYPE_II = [
    (0.00, 0.0000),
    (1.00, 0.0110),
    (2.00, 0.0220),
    (3.00, 0.0345),
    (4.00, 0.0485),
    (5.00, 0.0640),
    (6.00, 0.0800),
    (6.50, 0.0890),
    (7.00, 0.0980),
    (7.50, 0.1080),
    (8.00, 0.1200),
    (8.50, 0.1330),
    (9.00, 0.1470),
    (9.25, 0.1550),
    (9.50, 0.1630),
    (9.75, 0.1720),
    (10.00, 0.1810),
    (10.25, 0.1930),
    (10.50, 0.2040),
    (10.75, 0.2190),
    (11.00, 0.2350),
    (11.25, 0.2540),
    (11.50, 0.2830),
    (11.75, 0.3570),
    (12.00, 0.6630),
    (12.50, 0.7350),
    (13.00, 0.7720),
    (13.50, 0.7990),
    (14.00, 0.8200),
    (16.00, 0.8800),
    (20.00, 0.9520),
    (24.00, 1.0000),
]


def base_elev(x):
    return Z0 - SLOPE * x


def vertex_elev(x, y):
    z = base_elev(x)
    if abs(x - ROAD_X) < 1e-6:
        z += ROAD_HEIGHT
    return z


def vidx(ix, iy, nx):
    return iy * nx + ix


def scs_intensity_rows(_unused=None):
    """Convert SCS Type II cumulative table to INTENSITY rows (mm/hr) at
    uniform 15-minute spacing.

    Why uniform spacing: with INTENSITY format and a 15-min RAINGAGE
    Interval, SWMM treats each TIMESERIES row's value as the intensity
    held for *exactly one Interval* (15 min), regardless of the gap to
    the next row. The raw SCS table has rows at irregular times, so
    emitting it as-is drops ~30% of the design depth. Resampling to a
    regular 15-min grid keeps the total depth correct.
    """
    step_min = 15
    n_steps = (24 * 60) // step_min  # 96 intervals over 24 h

    def cum_fraction(t_hr):
        if t_hr <= SCS_TYPE_II[0][0]:
            return SCS_TYPE_II[0][1]
        if t_hr >= SCS_TYPE_II[-1][0]:
            return SCS_TYPE_II[-1][1]
        for i in range(len(SCS_TYPE_II) - 1):
            t0, p0 = SCS_TYPE_II[i]
            t1, p1 = SCS_TYPE_II[i + 1]
            if t0 <= t_hr <= t1:
                w = (t_hr - t0) / (t1 - t0)
                return p0 + w * (p1 - p0)
        return SCS_TYPE_II[-1][1]

    rows = []
    for k in range(n_steps):
        t0_hr = k * step_min / 60.0
        t1_hr = (k + 1) * step_min / 60.0
        p0 = cum_fraction(t0_hr) * RAIN_DEPTH_MM
        p1 = cum_fraction(t1_hr) * RAIN_DEPTH_MM
        intensity = (p1 - p0) / (step_min / 60.0)
        h = int(t0_hr)
        m = int(round((t0_hr - h) * 60))
        rows.append((h, m, intensity))
    rows.append((24, 0, 0.0))
    return rows


def build_inp():
    L = []
    nxv = int(LX / DX) + 1
    nyv = int(LY / DY) + 1

    # Pipe junction X positions
    pipe_xs = []
    x = PIPE_X_START
    while x <= PIPE_X_END + 1e-6:
        pipe_xs.append(round(x, 6))
        x += PIPE_SEGMENT

    # No subcatchments: the engine applies the first raingage's rainfall
    # directly to every 2D cell, so the storm hits the mesh without any
    # [SUBCATCHMENTS] -> junction -> coupling plumbing.

    # Coupling indices along the pipe: both ends plus the two junctions
    # flanking the road (one cell upstream, one cell downstream). The
    # downstream end is coupled so the culvert discharges back onto the
    # mesh on the lee side, where it sheet-flows toward the right-edge
    # NORMAL_FLOW boundary.
    road_idx = int(round((ROAD_X - PIPE_X_START) / PIPE_SEGMENT))
    last_idx = len(pipe_xs) - 1
    pipe_couple_offsets = sorted({0, road_idx - 1, road_idx + 1, last_idx})

    # ------- [TITLE]
    L += [
        "[TITLE]",
        "Flat 1D-2D Coupled Road-Culvert Demo",
        "100 m (Y) x 1000 m (X) FLAT plain (zero slope). Transverse roadway",
        f"embankment {ROAD_HEIGHT:.1f} m high at X = {ROAD_X:.0f}, spanning the full width.",
        "A 500 m circular culvert (50 x 10 m segments) runs along Y = 50 with",
        "its invert AT the ground surface, passing under the road like a pipe",
        "at grade. Four pipe junctions are 2D-coupled: the ends J_P00 / J_P50",
        "and the two road-flanking nodes J_P24 / J_P26, so ponded water enters",
        "the culvert upstream of the road and discharges back onto the mesh on",
        "the lee side. Rainfall (SCS Type II 24-hour, 100 mm) is auto-broadcast",
        "to every 2D cell. The mesh uses an ALTERNATING (union-jack) diagonal so",
        "a uniform problem gives a symmetric response (a single fixed diagonal",
        "breaks N-S symmetry and drifts water toward one edge). Every mesh edge",
        "along X = 1000 carries a NORMAL_FLOW boundary with a nonzero outlet",
        "slope so the flat basin still drains. 2D state is written to",
        "road_culvert.2d.h5 for animation in the GUI.",
        "",
    ]

    # ------- [OPTIONS]
    end_total_min = SIM_HOURS * 60
    end_day = 1 + end_total_min // (24 * 60)
    end_min_in_day = end_total_min % (24 * 60)
    end_h = end_min_in_day // 60
    end_m = end_min_in_day % 60
    L += [
        "[OPTIONS]",
        "FLOW_UNITS           CMS",
        "INFILTRATION         HORTON",
        "FLOW_ROUTING         DYNWAVE",
        "LINK_OFFSETS         DEPTH",
        "MIN_SLOPE            0",
        "ALLOW_PONDING        NO",
        "SKIP_STEADY_STATE    NO",
        "",
        "START_DATE           01/01/2026",
        "START_TIME           00:00:00",
        "REPORT_START_DATE    01/01/2026",
        "REPORT_START_TIME    00:00:00",
        f"END_DATE             01/{end_day:02d}/2026",
        f"END_TIME             {end_h:02d}:{end_m:02d}:00",
        "SWEEP_START          01/01",
        "SWEEP_END            12/31",
        "DRY_DAYS             0",
        "REPORT_STEP          00:05:00",
        "WET_STEP              00:00:30",
        "DRY_STEP              00:05:00",
        f"ROUTING_STEP         00:00:{ROUTING_STEP_SEC:02d}",
        "",
        "INERTIAL_DAMPING     PARTIAL",
        "NORMAL_FLOW_LIMITED  BOTH",
        "FORCE_MAIN_EQUATION  H-W",
        "VARIABLE_STEP        0.75",
        "LENGTHENING_STEP     0",
        "MIN_SURFAREA         0",
        "NUMBER_OF_THREADS    0",
        "MAX_TRIALS           8",
        "HEAD_TOLERANCE       0.0015",
        "SYS_FLOW_TOL         5",
        "LAT_FLOW_TOL         5",
        "MINIMUM_STEP         0.5",
        "",
    ]

    # ------- [EVAPORATION]
    L += [
        "[EVAPORATION]",
        "CONSTANT             0.0",
        "DRY_ONLY             NO",
        "",
    ]

    # ------- [RAINGAGES]
    L += [
        "[RAINGAGES]",
        ";;Name           Format    Interval SCF      Source",
        ";;-------------- --------- ------   ------   ----------",
        "GAGE1            INTENSITY 0:15     1.0      TIMESERIES STORM",
        "",
    ]

    # ------- [JUNCTIONS]
    L += [
        "[JUNCTIONS]",
        ";;Name           Elev     MaxDepth   InitDepth  SurDepth   Aponded",
        ";;-------------- -------- ---------- ---------- ---------- ----------",
    ]
    # Culvert junctions. The pipe crown sits at grade (invert one diameter
    # below, PIPE_BURIAL = PIPE_DIAMETER). MaxDepth = PIPE_DIAMETER sizes
    # the node to the pipe, so the coupling spill threshold
    # z_top = invert + MaxDepth + SurDepth (NodeCoupling) lands on the
    # surface: 2D water enters the barrel as soon as it reaches grade.
    # Uncoupled junctions reuse the same geometry.
    for k, x in enumerate(pipe_xs):
        z_surf = base_elev(x)
        z_invert = z_surf - PIPE_BURIAL
        L.append(f"{('J_P%02d' % k):<16} {z_invert:<8.3f} "
                 f"{PIPE_DIAMETER:<10.3f} 0          0          0")
    L.append("")

    # ------- [CONDUITS]
    L += [
        "[CONDUITS]",
        ";;Name           From       To         Length     Roughness  InOffset   OutOffset  InitFlow   MaxFlow",
        ";;-------------- ---------- ---------- ---------- ---------- ---------- ---------- ---------- ----------",
    ]
    for k in range(len(pipe_xs) - 1):
        name = f"C_P{k:02d}"
        L.append(f"{name:<16} {('J_P%02d' % k):<10} {('J_P%02d' % (k + 1)):<10} "
                 f"{PIPE_SEGMENT:<10.2f} {PIPE_ROUGHNESS:<10.4f} 0          0          0          0")
    L.append("")

    # ------- [XSECTIONS]
    L += [
        "[XSECTIONS]",
        ";;Link           Shape        Geom1            Geom2      Geom3      Geom4      Barrels",
        ";;-------------- ------------ ---------------- ---------- ---------- ---------- -------",
    ]
    for k in range(len(pipe_xs) - 1):
        name = f"C_P{k:02d}"
        L.append(f"{name:<16} CIRCULAR     {PIPE_DIAMETER:<16.3f} 0          0          0          1")
    L.append("")

    # ------- [TIMESERIES]  SCS Type II hyetograph
    L += [
        "[TIMESERIES]",
        ";;Name           Date       Time       Value",
        ";;-------------- ---------- ---------- ----------",
        ";; SCS Type II 24-hour, total depth = "
        f"{RAIN_DEPTH_MM:.0f} mm. INTENSITY rows hold",
        ";; until the next timestamp. Peak intensity ~"
        f"{(SCS_TYPE_II[24][1] - SCS_TYPE_II[23][1]) * RAIN_DEPTH_MM / (SCS_TYPE_II[24][0] - SCS_TYPE_II[23][0]):.1f} mm/hr "
        f"at t = {SCS_TYPE_II[23][0]:.2f} h.",
    ]
    for h, m, intensity in scs_intensity_rows("01/01/2026"):
        L.append(f"STORM            01/01/2026 {h:02d}:{m:02d}      {intensity:<10.3f}")
    L.append("")

    # ------- [REPORT]
    L += [
        "[REPORT]",
        "INPUT      NO",
        "CONTROLS   NO",
        "SUBCATCHMENTS ALL",
        "NODES         ALL",
        "LINKS         ALL",
        "",
    ]

    # ------- [MAP]
    L += [
        "[MAP]",
        f"DIMENSIONS -50.0 -20.0 {LX + 50.0:.1f} {LY + 20.0:.1f}",
        "UNITS      Meters",
        "",
    ]

    # ------- [COORDINATES]
    L += [
        "[COORDINATES]",
        ";;Node           X-Coord            Y-Coord",
    ]
    for k, x in enumerate(pipe_xs):
        L.append(f"{('J_P%02d' % k):<16} {x:<18.3f} {PIPE_Y:<18.3f}")
    L.append("")

    # ------- [VERTICES]
    L += [
        "[VERTICES]",
        ";;Link           X-Coord            Y-Coord",
        "",
    ]

    # ------- [2D_OPTIONS]
    L += [
        "[2D_OPTIONS]",
        ";;  Solver and timestepping for the 2D surface-routing module.",
        "MAX_TIMESTEP              5.0",
        "MIN_TIMESTEP              0.001",
        "REL_TOLERANCE             1.0e-3",
        "ABS_TOLERANCE             1.0e-5",
        "DRY_DEPTH                 1.0e-4",
        "COUPLING_INTERVAL         0",
        f"COUPLING_CD               {COUPLING_CD:.3f}",
        "LINEAR_SOLVER             GMRES",
        "PRECONDITIONER            AMG",
        "REPORT_2D                 YES",
        "OUTPUT_FILE               road_culvert.2d.h5",
        "",
    ]

    # ------- [2D_VERTICES]
    L += [
        "[2D_VERTICES]",
        ";;  X            Y            Z",
        f";;  {nxv} x {nyv} grid ({nxv * nyv} vertices) over a {LX:.0f} x {LY:.0f} m FLAT plain.",
        f";;  Base surface z = {Z0:.3f} (no slope).  Roadway crest is the single",
        f";;  vertex column at X = {ROAD_X:.0f}, raised {ROAD_HEIGHT:.2f} m above base.",
    ]
    for iy in range(nyv):
        y = iy * DY
        for ix in range(nxv):
            x = ix * DX
            z = vertex_elev(x, y)
            L.append(f"  {x:10.3f}   {y:10.3f}   {z:10.4f}")
    L.append("")

    # ------- [2D_TRIANGLES]
    # ALTERNATING diagonal (union-jack): even (ix+iy) cells split SW-NE,
    # odd cells split NW-SE. This cancels the north-south mesh anisotropy
    # of a single fixed diagonal. All four triangles are CCW.
    L += [
        "[2D_TRIANGLES]",
        ";;  V1   V2   V3   MANNINGS_N",
        f";;  {(nxv - 1) * (nyv - 1) * 2} triangles - each {DX:.0f} x {DY:.0f} m quad split with an",
        ";;  ALTERNATING diagonal (SW-NE on even ix+iy, NW-SE on odd) and CCW",
        ";;  vertex ordering.  Manning's n = 0.030 (grass).",
    ]
    for iy in range(nyv - 1):
        for ix in range(nxv - 1):
            v_sw = vidx(ix, iy, nxv)
            v_se = vidx(ix + 1, iy, nxv)
            v_ne = vidx(ix + 1, iy + 1, nxv)
            v_nw = vidx(ix, iy + 1, nxv)
            if not ALTERNATE_DIAGONAL or (ix + iy) % 2 == 0:
                # SW-NE diagonal
                L.append(f"  {v_sw:5d} {v_se:5d} {v_ne:5d}   0.030")
                L.append(f"  {v_sw:5d} {v_ne:5d} {v_nw:5d}   0.030")
            else:
                # NW-SE diagonal
                L.append(f"  {v_sw:5d} {v_se:5d} {v_nw:5d}   0.030")
                L.append(f"  {v_se:5d} {v_ne:5d} {v_nw:5d}   0.030")
    L.append("")

    # ------- [2D_VERTEX_NODE_MAP]
    L += [
        "[2D_VERTEX_NODE_MAP]",
        ";;  VERTEX  NODE       CD     AREA",
        ";;  Culvert coupling points: pipe endpoints J_P00/J_P50 plus the",
        ";;  two junctions flanking the road crest. Bidirectional - 2D water",
        ";;  above the culvert crown drains into the pipe, surcharged pipe",
        ";;  water spills back out onto the mesh.",
    ]
    for k in pipe_couple_offsets:
        x = pipe_xs[k]
        ix = max(0, min(nxv - 1, int(round(x / DX))))
        iy = max(0, min(nyv - 1, int(round(PIPE_Y / DY))))
        v = vidx(ix, iy, nxv)
        L.append(f"  {v:5d}   {('J_P%02d' % k):<10} "
                 f"{COUPLING_CD:.3f}  {COUPLING_AREA_PIPE:.3f}")
    L.append("")

    # ------- [2D_BOUNDARY_CONDITIONS]
    # NORMAL_FLOW on every mesh edge along X = LX. The alternating diagonal
    # changes which triangle/edge of the rightmost quad (ix = nxv-2) lies on
    # X = LX. Local edge e is opposite local vertex e (edge 0 = V1-V2):
    #   even cell (SW-NE): right edge v_se-v_ne is on the LOWER triangle
    #                      (sw,se,ne) as edge 0.
    #   odd  cell (NW-SE): right edge v_se-v_ne is on the UPPER triangle
    #                      (se,ne,nw) as edge 2.
    # The slope param is a nonzero OUTLET energy slope (terrain is flat, and
    # the Manning outlet flux q ~ sqrt(S) is zero for S = 0).
    L += [
        "[2D_BOUNDARY_CONDITIONS]",
        ";;TRI  EDGE TYPE          PARAM_1",
        f";;  NORMAL_FLOW (Manning outflow, outlet slope {OUTLET_SLOPE:.4f}) on every",
        f";;  mesh edge along X = {LX:.0f}. Parity-aware emission for the alternating",
        ";;  diagonal. Outlet slope is nonzero so the flat basin drains.",
    ]
    ix = nxv - 2  # rightmost quad column
    for iy in range(nyv - 1):
        base_tri = 2 * (iy * (nxv - 1) + ix)
        if not ALTERNATE_DIAGONAL or (ix + iy) % 2 == 0:
            tri, edge = base_tri, 0
        else:
            tri, edge = base_tri + 1, 2
        L.append(f"{tri:<6d} {edge}    NORMAL_FLOW   {OUTLET_SLOPE:.4f}")
    L.append("")

    return "\n".join(L) + "\n"


def main():
    out = Path(__file__).parent / "road_culvert.inp"
    out.write_text(build_inp())
    print(f"Wrote {out} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
