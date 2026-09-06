/*!
 * \file   meshcellgeom.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Mixed triangle/quad cell helpers (workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md,
 * phase G1). ONE place decides how a cell's edges are numbered, how its
 * flat edge slots are addressed, and how a quad is split into the two
 * sub-triangles the engine's storage model uses — rendering, hit-testing,
 * profiles, contours and statistics all draw the same picture the solver
 * stores.
 *
 * Engine contract (plans/2D_TRI_QUAD_MESH_PLAN_2026-09-06.md):
 *   - local edge k of a cell has endpoints v[(k+1)%nv], v[(k+2)%nv]
 *     (for a triangle: the historical "edge k is opposite vertex k");
 *   - per-edge SoAs are padded to kEdgeStride = 4 slots per cell
 *     (`cell * 4 + k`; slot 3 of a triangle is unused);
 *   - a quad's storage (VFR) model is two planar sub-triangles split along
 *     the diagonal chosen from the elevation ordering of its vertices
 *     (Begnudelli & Sanders 2007, Cases 1–3): the same split is used here.
 */
#ifndef OPENSWMMVIS_MESH_MESHCELLGEOM_H
#define OPENSWMMVIS_MESH_MESHCELLGEOM_H

#include "mesh/meshresult.h"

#include <QPointF>
#include <QVector>

#include <array>
#include <cmath>

namespace mesh {

/*! Edge slots per cell in every flat per-edge array (== engine kMaxCellVerts). */
constexpr int kEdgeStride = 4;

/*! Flat edge slot of local edge \p e of cell \p cell. */
constexpr int edgeSlot(int cell, int e) noexcept { return cell * kEdgeStride + e; }
/*! Cell / local edge of a flat slot. */
constexpr int slotCell(int slot) noexcept { return slot / kEdgeStride; }
constexpr int slotLocal(int slot) noexcept { return slot % kEdgeStride; }
/*! Number of flat edge slots for \p nCells cells. */
constexpr int edgeSlotCount(int nCells) noexcept { return nCells * kEdgeStride; }

/*! Endpoints (a, b) of local edge \p k of cell \p t: v[(k+1)%nv], v[(k+2)%nv]. */
inline void edgeEndpoints(const MeshTriangle &t, int k, int &a, int &b) noexcept
{
    const int nv = t.vertexCount();
    a = t.vertex((k + 1) % nv);
    b = t.vertex((k + 2) % nv);
}

/*! \brief Per-cell derived geometry shared by every consumer. */
struct CellGeom
{
    double area = 0.0;                 ///< planimetric (map units²)
    QPointF centroid;                  ///< AREA centroid (not the vertex mean for a quad)
    double zMean = 0.0;                ///< mean of the vertex elevations
    int    nSub = 1;                   ///< sub-triangles: 1 (triangle) or 2 (quad)
    std::array<std::array<int, 3>, 2> sub{};   ///< sub-triangle vertex indices
    int    diagCase = 0;               ///< B&S 2007 case (1..3) for a quad; 0 for a triangle
};

/*! \brief Geometry of cell \p t against \p vertices.
 *
 *  A quad is split along the Begnudelli & Sanders (2007) diagonal: with the
 *  vertices ordered by elevation n1 ≤ n2 ≤ n3 ≤ n4 — Case 1: n1,n4 opposite
 *  → sub-triangles (n1,n2,n4),(n1,n3,n4); Case 2: n1,n4 adjacent and n2
 *  adjacent to n1 → (n1,n2,n4),(n2,n3,n4); Case 3 → (n1,n3,n4),(n2,n3,n4).
 *  The centroid is the area-weighted centroid of the two sub-triangles (a
 *  property of the polygon, independent of the diagonal).
 */
inline CellGeom cellGeom(const QVector<MeshVertex> &vertices, const MeshTriangle &t)
{
    CellGeom g;
    auto triArea = [&](int a, int b, int c) {
        const QPointF &A = vertices[a].xy, &B = vertices[b].xy, &C = vertices[c].xy;
        return 0.5 * std::abs((B.x() - A.x()) * (C.y() - A.y()) - (C.x() - A.x()) * (B.y() - A.y()));
    };
    auto triCentroid = [&](int a, int b, int c) {
        const QPointF &A = vertices[a].xy, &B = vertices[b].xy, &C = vertices[c].xy;
        return QPointF((A.x() + B.x() + C.x()) / 3.0, (A.y() + B.y() + C.y()) / 3.0);
    };
    if (!t.isQuad()) {
        g.nSub = 1;
        g.sub[0] = {t.v0, t.v1, t.v2};
        g.area = triArea(t.v0, t.v1, t.v2);
        g.centroid = triCentroid(t.v0, t.v1, t.v2);
        g.zMean = (vertices[t.v0].z + vertices[t.v1].z + vertices[t.v2].z) / 3.0;
        return g;
    }
    const int v[4] = {t.v0, t.v1, t.v2, t.v3};
    // Elevation ordering of the cyclic positions (stable insertion sort).
    int p[4] = {0, 1, 2, 3};
    for (int i = 1; i < 4; ++i) {
        const int key = p[i];
        int j = i - 1;
        while (j >= 0 && vertices[v[p[j]]].z > vertices[v[key]].z) { p[j + 1] = p[j]; --j; }
        p[j + 1] = key;
    }
    const int n1 = p[0], n2 = p[1], n3 = p[2], n4 = p[3];
    auto adjacent = [](int a, int b) { const int d = (a - b + 4) % 4; return d == 1 || d == 3; };
    int t1[3], t2[3];
    if (!adjacent(n1, n4))      { g.diagCase = 1; t1[0]=n1; t1[1]=n2; t1[2]=n4; t2[0]=n1; t2[1]=n3; t2[2]=n4; }
    else if (adjacent(n2, n1))  { g.diagCase = 2; t1[0]=n1; t1[1]=n2; t1[2]=n4; t2[0]=n2; t2[1]=n3; t2[2]=n4; }
    else                        { g.diagCase = 3; t1[0]=n1; t1[1]=n3; t1[2]=n4; t2[0]=n2; t2[1]=n3; t2[2]=n4; }
    g.nSub = 2;
    g.sub[0] = {v[t1[0]], v[t1[1]], v[t1[2]]};
    g.sub[1] = {v[t2[0]], v[t2[1]], v[t2[2]]};
    const double a1 = triArea(g.sub[0][0], g.sub[0][1], g.sub[0][2]);
    const double a2 = triArea(g.sub[1][0], g.sub[1][1], g.sub[1][2]);
    g.area = a1 + a2;
    const QPointF c1 = triCentroid(g.sub[0][0], g.sub[0][1], g.sub[0][2]);
    const QPointF c2 = triCentroid(g.sub[1][0], g.sub[1][1], g.sub[1][2]);
    g.centroid = (g.area > 0.0)
        ? QPointF((a1 * c1.x() + a2 * c2.x()) / g.area, (a1 * c1.y() + a2 * c2.y()) / g.area)
        : QPointF((vertices[t.v0].xy + vertices[t.v1].xy + vertices[t.v2].xy + vertices[t.v3].xy) / 4.0);
    g.zMean = (vertices[t.v0].z + vertices[t.v1].z + vertices[t.v2].z + vertices[t.v3].z) / 4.0;
    return g;
}

/*! \brief Signed area of the polygon (positive = counter-clockwise). */
inline double cellSignedArea(const QVector<MeshVertex> &vertices, const MeshTriangle &t) noexcept
{
    const int nv = t.vertexCount();
    double s = 0.0;
    for (int k = 0; k < nv; ++k) {
        const QPointF &a = vertices[t.vertex(k)].xy, &b = vertices[t.vertex((k + 1) % nv)].xy;
        s += a.x() * b.y() - b.x() * a.y();
    }
    return 0.5 * s;
}

/*! \brief Convexity test for a quad (every consecutive cross product has the
 *  same non-zero sign). Triangles are trivially convex. */
inline bool cellIsConvex(const QVector<MeshVertex> &vertices, const MeshTriangle &t) noexcept
{
    if (!t.isQuad()) return true;
    int sign = 0;
    for (int k = 0; k < 4; ++k) {
        const QPointF &p = vertices[t.vertex(k)].xy;
        const QPointF &q = vertices[t.vertex((k + 1) % 4)].xy;
        const QPointF &r = vertices[t.vertex((k + 2) % 4)].xy;
        const double cr = (q.x() - p.x()) * (r.y() - q.y()) - (q.y() - p.y()) * (r.x() - q.x());
        const int s = (cr > 0.0) ? 1 : (cr < 0.0) ? -1 : 0;
        if (s == 0 || (sign != 0 && s != sign)) return false;
        sign = s;
    }
    return true;
}

/*! \brief Point-in-cell test on the sub-triangle fan (barycentric per sub-triangle). */
inline bool cellContains(const QVector<MeshVertex> &vertices, const MeshTriangle &t,
                         const QPointF &p, double eps = 0.0) noexcept
{
    const CellGeom g = cellGeom(vertices, t);
    for (int s = 0; s < g.nSub; ++s) {
        const QPointF &a = vertices[g.sub[s][0]].xy, &b = vertices[g.sub[s][1]].xy,
                      &c = vertices[g.sub[s][2]].xy;
        const double d = (b.y() - c.y()) * (a.x() - c.x()) + (c.x() - b.x()) * (a.y() - c.y());
        if (std::abs(d) < 1e-300) continue;
        const double l1 = ((b.y() - c.y()) * (p.x() - c.x()) + (c.x() - b.x()) * (p.y() - c.y())) / d;
        const double l2 = ((c.y() - a.y()) * (p.x() - c.x()) + (a.x() - c.x()) * (p.y() - c.y())) / d;
        const double l3 = 1.0 - l1 - l2;
        if (l1 >= -eps && l2 >= -eps && l3 >= -eps) return true;
    }
    return false;
}

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHCELLGEOM_H
