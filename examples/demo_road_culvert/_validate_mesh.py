#!/usr/bin/env python3
"""Structural validation of road_culvert.inp.

Checks the generated 2D mesh is self-consistent before the engine run:
  - vertex / triangle / boundary counts match the DX/DY grid,
  - the diagonal alternates (union-jack),
  - every triangle is CCW (positive signed area),
  - every [2D_BOUNDARY_CONDITIONS] (tri, edge) names an edge whose two
    endpoints are on X = LX AND which is a true boundary edge (appears in
    exactly one triangle),
  - coupling vertices sit at the expected pipe/road locations.

Run:  python3 _validate_mesh.py
"""
from pathlib import Path
from collections import defaultdict

import generate_model as g

INP = Path(__file__).parent / "road_culvert.inp"


def read_sections(text):
    secs, cur = {}, None
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("[") and s.endswith("]"):
            cur = s[1:-1]
            secs[cur] = []
        elif cur is not None and s and not s.startswith(";"):
            secs[cur].append(s)
    return secs


def main():
    secs = read_sections(INP.read_text())

    nxv = int(g.LX / g.DX) + 1
    nyv = int(g.LY / g.DY) + 1
    exp_verts = nxv * nyv
    exp_tris = (nxv - 1) * (nyv - 1) * 2
    exp_bc = nyv - 1

    verts = [tuple(map(float, r.split()[:3])) for r in secs["2D_VERTICES"]]
    tris = [tuple(map(int, r.split()[:3])) for r in secs["2D_TRIANGLES"]]
    bcs = [r.split() for r in secs["2D_BOUNDARY_CONDITIONS"]]

    ok = True

    def check(name, cond):
        nonlocal ok
        ok = ok and cond
        print(f"  [{'PASS' if cond else 'FAIL'}] {name}")

    print("Counts:")
    check(f"vertices == {exp_verts}", len(verts) == exp_verts)
    check(f"triangles == {exp_tris}", len(tris) == exp_tris)
    check(f"boundary rows == {exp_bc}", len(bcs) == exp_bc)

    # CCW + alternating diagonal
    print("Triangle winding + diagonal pattern:")
    all_ccw = True
    diag_ok = True
    for t, (a, b, c) in enumerate(tris):
        ax, ay, _ = verts[a]
        bx, by, _ = verts[b]
        cx, cy, _ = verts[c]
        cross = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay)
        if cross <= 0:
            all_ccw = False
        # quad index = t // 2; recover (ix, iy)
        q = t // 2
        iy, ix = divmod(q, nxv - 1)
        v_sw = g.vidx(ix, iy, nxv)
        v_se = g.vidx(ix + 1, iy, nxv)
        v_ne = g.vidx(ix + 1, iy + 1, nxv)
        v_nw = g.vidx(ix, iy + 1, nxv)
        even = (ix + iy) % 2 == 0
        expect = ([(v_sw, v_se, v_ne), (v_sw, v_ne, v_nw)] if even
                  else [(v_sw, v_se, v_nw), (v_se, v_ne, v_nw)])
        if (a, b, c) != expect[t % 2]:
            diag_ok = False
    check("all triangles CCW (area > 0)", all_ccw)
    check("alternating diagonal matches generator", diag_ok)

    # Build edge -> triangle count to identify true boundary edges.
    edge_tris = defaultdict(list)
    for t, (a, b, c) in enumerate(tris):
        for u, v in ((a, b), (b, c), (c, a)):
            edge_tris[frozenset((u, v))].append(t)

    # Opposite-vertex local edge convention: edge e is opposite local
    # vertex e  ->  edge0 = V1-V2, edge1 = V2-V0, edge2 = V0-V1.
    def local_edge(tri_v, e):
        a, b, c = tri_v
        return {0: (b, c), 1: (c, a), 2: (a, b)}[e]

    print("Boundary-condition edges:")
    bc_edges_on_lx = True
    bc_edges_are_boundary = True
    for row in bcs:
        tri = int(row[0])
        edge = int(row[1])
        u, v = local_edge(tris[tri], edge)
        xu = verts[u][0]
        xv = verts[v][0]
        if not (abs(xu - g.LX) < 1e-6 and abs(xv - g.LX) < 1e-6):
            bc_edges_on_lx = False
        if len(edge_tris[frozenset((u, v))]) != 1:
            bc_edges_are_boundary = False
    check("every BC edge has both endpoints on X = LX", bc_edges_on_lx)
    check("every BC edge is a true boundary edge (1 triangle)", bc_edges_are_boundary)

    # Coupling vertices
    print("Coupling vertices:")
    cmap = {r.split()[1]: int(r.split()[0]) for r in secs["2D_VERTEX_NODE_MAP"]}
    coupling_ok = True
    for node, vtx in cmap.items():
        vy, vx = divmod(vtx, nxv)  # vtx = iy*nxv + ix
        x = vx * g.DX
        y = vy * g.DY
        if abs(y - g.PIPE_Y) > 1e-6:
            coupling_ok = False
        print(f"    {node} -> vertex {vtx} at (X={x:.0f}, Y={y:.0f})")
    check("all coupling vertices on Y = PIPE_Y", coupling_ok)

    print()
    print("RESULT:", "ALL CHECKS PASS" if ok else "FAILURES PRESENT")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
