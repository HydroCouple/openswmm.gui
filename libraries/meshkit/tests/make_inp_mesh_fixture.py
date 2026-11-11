#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Write a synthetic structured-grid [2D_*] mesh as <name>.inp + <name>.2dm.

Fixture generator for meshkit_inp_read_probe. Number formatting follows the
GUI writer (17 significant digits) so bytes-per-row match real saved meshes.

  make_inp_mesh_fixture.py OUT_DIR NAME CELLS [--features] [--quad-rows R]

CELLS is the approximate cell count; the grid is rounded to whole rows.
--features adds vertex/cell tags, tag-form [2D_VERTEX_NODE_MAP] rows,
[2D_BOUNDARY_CONDITIONS] and [2D_EDGE_CONVEYANCE] rows. Those rows exercise
the GUI reader only; that variant is not checked as an engine-runnable model.
"""
import argparse
import math
import os

INP = """[TITLE]
Synthetic mesh open I/O fixture ({cells} cells)
[OPTIONS]
FLOW_UNITS CMS
FLOW_ROUTING DYNWAVE
START_DATE 01/01/2026
START_TIME 00:00:00
END_DATE 01/01/2026
END_TIME 00:05:00
REPORT_STEP 00:01:00
ROUTING_STEP 5
[JUNCTIONS]
J1 -2 4 0 0 0
[OUTFALLS]
O1 -2.5 FREE NO
[CONDUITS]
C1 J1 O1 30 .013 0 0 0
[XSECTIONS]
C1 CIRCULAR .5 0 0 0 1
[2D_OPTIONS]
INTEGRATOR EXPLICIT
MAX_TIMESTEP 5
REPORT_2D NO
[2D_MESH_FILE]
FILE {name}.2dm
"""


def g(v):
    return "%.17g" % v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir")
    ap.add_argument("name")
    ap.add_argument("cells", type=int)
    ap.add_argument("--features", action="store_true")
    ap.add_argument("--quad-rows", type=int, default=0)
    a = ap.parse_args()

    # nx*ny grid squares; a square is two triangles, or one quad in a quad row.
    nx = max(2, int(round(math.sqrt(a.cells / 2.0))))
    ny = max(2 + a.quad_rows, int(round(a.cells / (2.0 * nx))) + a.quad_rows // 2)
    tri_rows = ny - a.quad_rows
    n_tris = 2 * nx * tri_rows
    n_quads = nx * a.quad_rows
    n_verts = (nx + 1) * (ny + 1)
    x0, y0, dx = 579466.31199999992, 6132060.1391500002, 2.1300000000000003

    os.makedirs(a.out_dir, exist_ok=True)
    with open(os.path.join(a.out_dir, a.name + ".inp"), "w", newline="\n") as f:
        f.write(INP.format(cells=n_tris + n_quads, name=a.name))

    with open(os.path.join(a.out_dir, a.name + ".2dm"), "w", newline="\n", buffering=1 << 22) as f:
        f.write(";; UNITS: SI (m)\n;; SOURCE_CRS: EPSG:25832\n;; OpenSWMM 2D Mesh File\n")
        f.write(";; Synthetic fixture from make_inp_mesh_fixture.py\n")
        f.write("[2D_VERTICES]\n;; X            Y            Z            TAG\n")
        tag_every = 97 if a.features else 0
        for j in range(ny + 1):
            y = g(y0 + j * dx)
            rows = []
            for i in range(nx + 1):
                v = j * (nx + 1) + i
                z = 30.0 + 0.001 * i + 0.0007 * j + 0.31 * math.sin(0.01 * v)
                row = "%s  %s  %s" % (g(x0 + i * dx), y, g(z))
                if tag_every and v % tag_every == 0:
                    row += "  N%d" % v
                rows.append(row)
            f.write("\n".join(rows) + "\n")

        f.write("[2D_TRIANGLES]\n;; V1   V2   V3   MANNINGS_N   TAG\n")
        n = g(0.035)
        for j in range(tri_rows):
            rows = []
            base = j * (nx + 1)
            for i in range(nx):
                v00 = base + i
                v10 = v00 + 1
                v01 = v00 + nx + 1
                v11 = v01 + 1
                if a.features and i % 211 == 0:
                    rows.append("%d  %d  %d  %s  0.10000000000000001  zoneA" % (v00, v10, v11, n))
                else:
                    rows.append("%d  %d  %d  %s" % (v00, v10, v11, n))
                rows.append("%d  %d  %d  %s" % (v00, v11, v01, n))
            f.write("\n".join(rows) + "\n")

        if n_quads:
            f.write("[2D_QUADS]\n;; V1   V2   V3   V4   MANNINGS_N   TAG\n")
            for j in range(tri_rows, ny):
                base = j * (nx + 1)
                f.write("\n".join("%d  %d  %d  %d  %s" % (base + i, base + i + 1,
                                                        base + i + nx + 2, base + i + nx + 1, n)
                                  for i in range(nx)) + "\n")

        if a.features:
            f.write("[2D_VERTEX_NODE_MAP]\n;; VERTEX_INDEX_OR_TAG    SWMM_NODE_NAME    CD    AREA\n")
            for v in range(0, n_verts, tag_every):
                f.write("N%d        N%d  0.65000000000000002  1\n" % (v, v))
            f.write("[2D_BOUNDARY_CONDITIONS]\n;; TRI EDGE TYPE PARAM_1 PARAM_2 GROUP\n")
            for i in range(nx):  # bottom row: first triangle of each square, edge 0
                f.write("%d  0  NORMAL_FLOW  0.001  *  outlet\n" % (2 * i))
            f.write("[2D_EDGE_CONVEYANCE]\n;; FROM_VERTEX TO_VERTEX CONVEYANCE\n")
            mid = (tri_rows // 2) * (nx + 1)
            for i in range(nx):  # one interior horizontal line of edges
                f.write("%d  %d  0.5\n" % (mid + i, mid + i + 1))

    print("%s: %d vertices, %d triangles, %d quads" % (a.name, n_verts, n_tris, n_quads))


if __name__ == "__main__":
    main()
