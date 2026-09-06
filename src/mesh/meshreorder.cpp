/*!
 * \file   meshreorder.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "mesh/meshreorder.h"

#include "mesh/meshcellgeom.h"

#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace mesh {

namespace {

// Hilbert curve distance for (x, y) on a 2^order × 2^order grid — the
// classic bit-interleave-with-rotation form (order 16 → 32-bit keys).
quint64 hilbertXY2D(quint32 order, quint32 x, quint32 y)
{
    quint64 d = 0;
    for (quint32 s = 1u << (order - 1); s > 0; s >>= 1)
    {
        const quint32 rx = (x & s) ? 1 : 0;
        const quint32 ry = (y & s) ? 1 : 0;
        d += quint64(s) * quint64(s) * quint64((3 * rx) ^ ry);
        // Rotate the quadrant so the curve stays continuous.
        if (ry == 0)
        {
            if (rx == 1)
            {
                x = s - 1 - x;
                y = s - 1 - y;
            }
            std::swap(x, y);
        }
    }
    return d;
}

} // namespace

void reorderMeshHilbert(MeshResult *m)
{
    if (!m) return;
    const int nt = m->triangles.size();
    const int nv = m->vertices.size();
    if (nt == 0 || nv == 0) return;

    // Vertex bbox → 16-bit quantisation frame for the centroid keys.
    double x0 = std::numeric_limits<double>::max(),  y0 = x0;
    double x1 = std::numeric_limits<double>::lowest(), y1 = x1;
    for (const MeshVertex &v : m->vertices)
    {
        x0 = std::min(x0, v.xy.x());  x1 = std::max(x1, v.xy.x());
        y0 = std::min(y0, v.xy.y());  y1 = std::max(y1, v.xy.y());
    }
    constexpr quint32 kOrder = 16;
    constexpr double  kMaxQ  = 65535.0;
    const double sx = (x1 > x0) ? kMaxQ / (x1 - x0) : 0.0;
    const double sy = (y1 > y0) ? kMaxQ / (y1 - y0) : 0.0;

    QVector<quint64> key(nt);
    for (int t = 0; t < nt; ++t)
    {
        const MeshTriangle &tr = m->triangles[t];
        // Area centroid (cellGeom): the vertex mean for a triangle, the
        // area-weighted sub-triangle centroid for a quad.
        const QPointF c = cellGeom(m->vertices, tr).centroid;
        const double cx = c.x();
        const double cy = c.y();
        const quint32 qx = quint32(std::clamp((cx - x0) * sx, 0.0, kMaxQ));
        const quint32 qy = quint32(std::clamp((cy - y0) * sy, 0.0, kMaxQ));
        key[t] = hilbertXY2D(kOrder, qx, qy);
    }

    // stable_sort keeps equal-key triangles in Triangle's output order, so
    // the permutation is deterministic. Triangles sort before quads: the
    // engine's cell order is [2D_TRIANGLES] then [2D_QUADS], so a mixed mesh
    // is Hilbert-ordered within each class and never interleaved.
    QVector<int> order(nt);
    std::iota(order.begin(), order.end(), 0);
    const QVector<MeshTriangle> &tris = m->triangles;
    std::stable_sort(order.begin(), order.end(),
                     [&key, &tris](int a, int b) {
                         const bool qa = tris[a].isQuad(), qb = tris[b].isQuad();
                         if (qa != qb) return !qa;
                         return key[a] < key[b];
                     });

    // Permute triangles; record old→new for the cell couplings.
    QVector<MeshTriangle> newTris;
    newTris.reserve(nt);
    QVector<int> triMap(nt);
    for (int i = 0; i < nt; ++i)
    {
        triMap[order[i]] = i;
        newTris.append(std::move(m->triangles[order[i]]));
    }

    // Vertices: renumber by first appearance in the new triangle order;
    // any vertex referenced by no triangle keeps its relative order at the
    // end (boundary bookkeeping may still point at it).
    QVector<int> vmap(nv, -1);
    int next = 0;
    for (const MeshTriangle &tr : newTris)
    {
        const int nvc = tr.vertexCount();
        for (int k = 0; k < nvc; ++k)
            if (vmap[tr.vertex(k)] < 0) vmap[tr.vertex(k)] = next++;
    }
    for (int v = 0; v < nv; ++v)
        if (vmap[v] < 0) vmap[v] = next++;

    QVector<MeshVertex> newVerts(nv);
    for (int v = 0; v < nv; ++v)
        newVerts[vmap[v]] = std::move(m->vertices[v]);

    for (MeshTriangle &tr : newTris)
    {
        const int nvc = tr.vertexCount();
        for (int k = 0; k < nvc; ++k)
            tr.setVertex(k, vmap[tr.vertex(k)]);
    }
    for (MeshEdge &e : m->boundaryEdges)
    {
        e.v0 = vmap[e.v0];
        e.v1 = vmap[e.v1];
    }
    for (CellCoupling &c : m->cellCouplings)
        if (c.tri >= 0 && c.tri < nt)
            c.tri = triMap[c.tri];

    m->triangles = std::move(newTris);
    m->vertices  = std::move(newVerts);
}

double meanVertexIndexSpread(const MeshResult &m)
{
    if (m.triangles.isEmpty()) return 0.0;
    double sum = 0.0;
    double edges = 0.0;
    for (const MeshTriangle &t : m.triangles)
    {
        const int nvc = t.vertexCount();
        for (int k = 0; k < nvc; ++k)
            sum += std::abs(t.vertex(k) - t.vertex((k + 1) % nvc));
        edges += double(nvc);
    }
    return sum / edges;
}

} // namespace mesh
