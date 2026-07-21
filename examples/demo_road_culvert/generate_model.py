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
width (Y). The road is a CELL-WIDE raised plateau (vertex columns at
ROAD_X +/- ROAD_HALF_WIDTH), so the crest cells have ALL their vertices
at crest elevation and truly block flow up to Z0 + ROAD_HEIGHT. (A
single raised column leaves the flanking cell beds at ~Z0 + H/3, and
the "dam" leaks numerically from that much lower effective crest - it
never holds the nominal head.) The dam ponds water on the upstream side
and forces it through the culvert until the pond tops the crest.

Culvert at grade under the road (crown flush with the surface):
    Junctions along Y = 50 from X = PIPE_X_START to PIPE_X_END. A node is
    placed ONLY where one is needed - the two ends and the two road-flanking
    coupling points - and the long spans between them are subdivided to at
    most MAX_PIPE_SEGMENT. 10 m segments (the original discretisation) pin
    the dynamic-wave Courant time step at its floor for the whole run
    (the permanently-surcharged Ø0.15 m barrel makes every short segment
    time-step-critical), so the model is conduit-length-limited, not
    physics-limited; longer conduits raise the stable step ~linearly. All
    pipe junctions are circular PIPE_DIAMETER with the CROWN at grade
    (invert one diameter below, PIPE_BURIAL = PIPE_DIAMETER) so the coupling
    gate z_top = invert + MaxDepth lands on the surface and the culvert
    engages at any ponding. The end nodes and the two road-flanking nodes
    are 2D-coupled. NOTE: with truly-flat terrain there is no head across
    the road, so the culvert equilibrates the two sides rather than carrying
    net through-flow; add SLOPE > 0 for visible conveyance.

Mesh diagonal (the fix for the north-pooling artifact):
    Each quad is split with an ALTERNATING ("union-jack") diagonal -
    SW-NE on even (ix+iy) cells, NW-SE on odd cells. The legacy demo
    used a single SW-NE diagonal everywhere; that breaks the mesh's
    north-south symmetry and drifts water toward one edge under uniform
    rainfall (the continuum problem is N-S symmetric but a single-
    diagonal mesh is not). Alternating the diagonal cancels the bias so
    a uniform problem gives a symmetric response.

Rainfall:
    SCS Type II 24-hour distribution, 600 mm total depth (an extreme
    event chosen so the upstream pond overtops the 0.5 m road crest
    around the hyetograph peak). Auto-broadcast to every 2D cell by
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

import math
from pathlib import Path

# Domain geometry. DX/DY drive both mesh resolution and runtime; the road
# crest plateau spans ROAD_X +/- ROAD_HALF_WIDTH so the crest cells block
# at full crest elevation regardless of DX. 5 m over the full plain is ~4x
# the cells of the 10 m legacy demo.
LX = 1000.0
LY = 100.0
# 10 m cells (2000 triangles): the demo-speed resolution. 5 m (8000 tris)
# resolves the crest sheet flow more finely but costs ~4x the 2D wall time.
DX = 10.0
DY = 10.0
SLOPE = 0.0          # flat terrain
Z0 = 1.0

# Mesh diagonal. True = alternating ("union-jack") split that cancels the
# north-south mesh anisotropy (the fix). False = a single SW-NE diagonal
# everywhere (the legacy pattern) which drifts water toward the north edge
# under uniform rainfall - set False to REPRODUCE the artifact.
ALTERNATE_DIAGONAL = True

# Roadway embankment (the transverse dam the culvert passes under).
# ROAD_HALF_WIDTH = DX makes the crest one full cell wide (vertex columns
# at ROAD_X - DX / ROAD_X / ROAD_X + DX), so the cells between them sit
# entirely at crest elevation and the dam holds the nominal 0.5 m of head
# before overtopping. Keep ROAD_HALF_WIDTH >= DX.
ROAD_X = 500.0
ROAD_HEIGHT = 0.5
ROAD_HALF_WIDTH = 10.0

# Culvert. The pipe CROWN sits at the ground surface (invert one diameter
# below grade) -> a pipe at grade passing under the road embankment. With
# MaxDepth = PIPE_DIAMETER the coupling spill threshold
# z_top = invert + MaxDepth lands exactly on the surface, so ponded water
# enters the barrel as soon as it reaches grade and the culvert conveys.
# (A literal invert-AT-surface, PIPE_BURIAL = 0, puts z_top a full diameter
# ABOVE grade; the flat-terrain ~0.1 m pond never reaches it and the culvert
# stays inert -- see README. Set PIPE_BURIAL = 0 to reproduce that.)
# Barrel sizing: the culvert must actually CONVEY what the coupling can
# capture, or the coupled junctions just fill and spill back every window
# (the old 500 m x Ø0.15 m layout carried ~0.003 m3/s -- a straw -- so 99%
# of the drained water flooded straight back onto the mesh and no
# downstream jet ever appeared). Ø0.6 m over a short 60 m run under the
# road conveys ~0.5 m3/s at the ponded heads here, matching the orifice
# inlet capacity, so the upstream pond genuinely drains to the lee side.
PIPE_Y = 50.0
PIPE_X_START = 470.0
PIPE_X_END = 530.0
PIPE_DIAMETER = 0.6
PIPE_BURIAL = PIPE_DIAMETER   # crown at grade
PIPE_ROUGHNESS = 0.013

# Culvert discretisation. Junctions are placed only at the coupling points
# (the two ends and the two road-flanking nodes), and each span between them
# is split into conduits no longer than MAX_PIPE_SEGMENT. The road-flanking
# coupling nodes sit ROAD_FLANK upstream/downstream of the road crest.
# A LARGE MAX_PIPE_SEGMENT (few long conduits) is the performance fix: short
# conduits collapse the dynamic-wave Courant step (10 m segments pinned it at
# the ~0.15 s floor for the whole run). Drop MAX_PIPE_SEGMENT only if you
# need finer 1D resolution and accept the slowdown.
# ROAD_FLANK must clear the crest plateau (> ROAD_HALF_WIDTH): a flank
# coupling vertex ON a raised crest column would sit 0.5 m above the plain
# and only ever exchange when the crest itself is under water.
ROAD_FLANK = 20.0
MAX_PIPE_SEGMENT = 250.0

# Outlet energy slope for the X = LX NORMAL_FLOW boundary. Decoupled from
# the (zero) terrain SLOPE so the east edge still drains on flat ground.
OUTLET_SLOPE = 0.001

# Rainfall, sized to overtop the 0.5 m road crest around the SCS peak:
# 100 mm only ever ponded ~0.10 m (road inert as a weir); 600 mm peaked at
# ~0.39 m upstream (culvert equalization to the ponding lee side eats the
# rest); 900 mm clears the crest with margin.
RAIN_DEPTH_MM = 900.0

# Coupling. The exchange AREA is matched to the barrel cross-section so the
# orifice cannot inject more than the pipe can convey (a mismatched large
# area -- the old 0.4 m2 against a 0.018 m2 barrel -- guarantees the node
# fills within a coupling window and spills back: spiky lateral inflows,
# large node continuity "errors", and a churn the 2D solver grinds on).
COUPLING_CD = 0.65
COUPLING_AREA_PIPE = math.pi * PIPE_DIAMETER ** 2 / 4.0

# Simulation window. SCS Type II peaks at hour ~12; the road overtops
# ~12.0-14.1 h, so 15 h covers the ascending limb, the peak, the whole
# overtopping event, and ~1 h of recession. The long drain-down beyond
# that is uneventful AND by far the most expensive stretch to integrate
# (deep-water CVODE grind: hours 15.6-18 cost ~3x the rest combined).
SIM_HOURS = 15
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
    if abs(x - ROAD_X) <= ROAD_HALF_WIDTH + 1e-6:
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

    # Pipe junction X positions. Place a node only at each coupling point
    # (the two ends and the two road-flanking nodes) and subdivide the spans
    # between them into conduits no longer than MAX_PIPE_SEGMENT. The
    # coupling-node indices fall out of the construction (every breakpoint
    # becomes a node). Short conduits collapse the dynamic-wave Courant step,
    # so few long conduits is the performance fix.
    breakpoints = sorted({
        PIPE_X_START,
        ROAD_X - ROAD_FLANK,
        ROAD_X + ROAD_FLANK,
        PIPE_X_END,
    })
    pipe_xs = [round(breakpoints[0], 6)]
    pipe_couple_offsets = [0]
    for x0, x1 in zip(breakpoints[:-1], breakpoints[1:]):
        span = x1 - x0
        n = max(1, int(math.ceil(span / MAX_PIPE_SEGMENT - 1e-9)))
        for i in range(1, n + 1):
            pipe_xs.append(round(x0 + span * i / n, 6))
        pipe_couple_offsets.append(len(pipe_xs) - 1)  # x1 is a coupling node
    pipe_couple_offsets = sorted(set(pipe_couple_offsets))

    # No subcatchments: the engine applies the first raingage's rainfall
    # directly to every 2D cell, so the storm hits the mesh without any
    # [SUBCATCHMENTS] -> junction -> coupling plumbing.
    #
    # Coupling: both ends plus the two junctions flanking the road. The
    # downstream end is coupled so the culvert discharges back onto the mesh
    # on the lee side, where it sheet-flows toward the right-edge NORMAL_FLOW
    # boundary.

    # ------- [TITLE]
    couple_names = [f"J_P{o:02d}" for o in pipe_couple_offsets]
    L += [
        "[TITLE]",
        "Flat 1D-2D Coupled Road-Culvert Demo",
        "100 m (Y) x 1000 m (X) FLAT plain (zero slope). Transverse roadway",
        f"embankment {ROAD_HEIGHT:.1f} m high at X = {ROAD_X:.0f}, spanning the full width.",
        f"A {PIPE_X_END - PIPE_X_START:.0f} m circular culvert ({len(pipe_xs) - 1} conduit(s)) runs along Y = 50 with",
        "its crown AT the ground surface, passing under the road like a pipe",
        f"at grade. {len(couple_names)} pipe junctions are 2D-coupled: the ends "
        f"{couple_names[0]} / {couple_names[-1]}",
        f"and the two road-flanking nodes {couple_names[1]} / {couple_names[2]}, so ponded water enters",
        "the culvert upstream of the road and discharges back onto the mesh on",
        f"the lee side. Rainfall (SCS Type II 24-hour, {RAIN_DEPTH_MM:.0f} mm) is auto-broadcast",
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
        "SURCHARGE_METHOD     EXTRAN",
        "VARIABLE_STEP        0.75",
        "LENGTHENING_STEP     0",
        "MIN_SURFAREA         0",
        # THREADS is the key the engine parses (options.num_threads); it also
        # drives the 2D solver's OpenMP loops and CVODE vector ops.
        "THREADS              4",
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
    # Uncoupled junctions reuse the same geometry. The LAST pipe node is a
    # FREE outfall (emitted below), not a junction: outfall coupling releases
    # the barrel's discharge onto the mesh with no crown gate, so the culvert
    # keeps draining the upstream pond even when the pipe is not surcharged
    # (a coupled dead-end junction only spills above the crown).
    for k, x in enumerate(pipe_xs[:-1]):
        z_surf = base_elev(x)
        z_invert = z_surf - PIPE_BURIAL
        L.append(f"{('J_P%02d' % k):<16} {z_invert:<8.3f} "
                 f"{PIPE_DIAMETER:<10.3f} 0          0          0")
    L.append("")

    # ------- [OUTFALLS]  (downstream culvert end, 2D-coupled free outfall)
    k_out = len(pipe_xs) - 1
    z_out_invert = base_elev(pipe_xs[k_out]) - PIPE_BURIAL
    L += [
        "[OUTFALLS]",
        ";;Name           Elev     Type       Stage Data       Gated",
        ";;-------------- -------- ---------- ---------------- --------",
        f"{('J_P%02d' % k_out):<16} {z_out_invert:<8.3f} FREE                        NO",
        "",
    ]

    # ------- [CONDUITS]
    L += [
        "[CONDUITS]",
        ";;Name           From       To         Length     Roughness  InOffset   OutOffset  InitFlow   MaxFlow",
        ";;-------------- ---------- ---------- ---------- ---------- ---------- ---------- ---------- ----------",
    ]
    for k in range(len(pipe_xs) - 1):
        name = f"C_P{k:02d}"
        seg_len = pipe_xs[k + 1] - pipe_xs[k]
        L.append(f"{name:<16} {('J_P%02d' % k):<10} {('J_P%02d' % (k + 1)):<10} "
                 f"{seg_len:<10.2f} {PIPE_ROUGHNESS:<10.4f} 0          0          0          0")
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
        # >= COUPLING_WINDOW so a single CVODE internal step can span a quiet
        # window instead of being chopped at 5 s.
        "MAX_TIMESTEP              10.0",
        "MIN_TIMESTEP              0.001",
        "REL_TOLERANCE             1.0e-3",
        "ABS_TOLERANCE             1.0e-5",
        # 1 mm wet/dry threshold (the engine default). The previous 0.1 mm
        # made the thin-film front at the crest brutally stiff for CVODE.
        "DRY_DEPTH                 1.0e-3",
        # Per-advance CVODE internal-step budget. Must be large enough to
        # COMPLETE a COUPLING_WINDOW in the stiff deep-water phase (~30 ms
        # internal steps -> ~300+ steps per 10 s window): a failed window
        # freezes the 2D surface and DROPS that window's rainfall (the old
        # cap of 60 silently lost ~half the storm at high rain depths).
        "MAX_CVODE_STEPS           2000",
        "COUPLING_INTERVAL         0",
        # Time-based 2D macro-window: integrate the 2D solver across ~10 s of
        # routing time per advance instead of restarting CVODE every 2 s
        # ROUTING_STEP (the dominant cost once the plain is wet). Larger
        # windows are faster but the held-flux coupling leaks mass in the
        # thin-film overtopping regime (30 s cost ~-10% 2D continuity here).
        "COUPLING_WINDOW           10",
        # Head-difference floor for the diffusive-flux sqrt regularization.
        # The flooded phase of this storm is exactly the deep, near-level
        # ponding regime where the default 4 mm still lets the flux Jacobian
        # stiffen and CVODE grind at ~30 ms internal steps; 10 mm bounds the
        # transmissivity harder at millimetre gradients (bulk flow unchanged).
        "FLUX_DH_EPS               0.01",
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
        f";;  Base surface z = {Z0:.3f} (no slope).  Roadway crest is the vertex",
        f";;  columns at X = {ROAD_X:.0f} +/- {ROAD_HALF_WIDTH:.0f}, raised {ROAD_HEIGHT:.2f} m above base.",
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
        ";;  Culvert coupling points: the pipe endpoints plus the",
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
