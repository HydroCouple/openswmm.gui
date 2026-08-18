/*!
 * \file   meshcellstats.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * MESH_DECOUPLED_1D2D_REMAP_PLAN Part A — cell-area statistics.
 */
#include "mesh/meshcellstats.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace mesh {

double triangleArea(const MeshResult &mesh, int tri)
{
    if (tri < 0 || tri >= mesh.triangles.size()) return 0.0;
    const MeshTriangle &t = mesh.triangles[tri];
    const int nv = mesh.vertices.size();
    if (t.v0 < 0 || t.v0 >= nv || t.v1 < 0 || t.v1 >= nv
        || t.v2 < 0 || t.v2 >= nv)
        return 0.0;
    const QPointF &a = mesh.vertices[t.v0].xy;
    const QPointF &b = mesh.vertices[t.v1].xy;
    const QPointF &c = mesh.vertices[t.v2].xy;
    return 0.5 * std::abs((b.x() - a.x()) * (c.y() - a.y())
                        - (c.x() - a.x()) * (b.y() - a.y()));
}

CellAreaStats computeCellAreaStats(const MeshResult &mesh)
{
    CellAreaStats s;
    const int nv = mesh.vertices.size();

    std::vector<double> areas;
    areas.reserve(static_cast<size_t>(mesh.triangles.size()));

    double sum = 0.0;
    for (int i = 0; i < mesh.triangles.size(); ++i)
    {
        const MeshTriangle &t = mesh.triangles[i];
        // Degenerate references are SKIPPED here rather than counted as zero,
        // so they don't drag the min down to 0 and mask a real sliver.
        if (t.v0 < 0 || t.v0 >= nv || t.v1 < 0 || t.v1 >= nv
            || t.v2 < 0 || t.v2 >= nv)
            continue;
        const double area = triangleArea(mesh, i);
        areas.push_back(area);
        sum += area;
    }

    s.count = static_cast<int>(areas.size());
    if (s.count == 0)
        return s;

    const auto [mnIt, mxIt] = std::minmax_element(areas.begin(), areas.end());
    s.min  = *mnIt;
    s.max  = *mxIt;
    s.mean = sum / s.count;

    const size_t mid = areas.size() / 2;
    std::nth_element(areas.begin(), areas.begin() + mid, areas.end());
    if (areas.size() % 2 == 1)
    {
        s.median = areas[mid];
    }
    else
    {
        // Lower middle = max of the left partition (nth_element guarantees
        // everything left of mid is <= areas[mid]).
        const double upper = areas[mid];
        const double lower = *std::max_element(areas.begin(),
                                               areas.begin() + mid);
        s.median = 0.5 * (lower + upper);
    }
    return s;
}

// ---------------------------------------------------------------------------
// CFL characteristic length — see meshcellstats.h for the engine references.
// ---------------------------------------------------------------------------

namespace {

/*! Local edge \p e of a triangle runs between the two vertices that are NOT
 *  \p e — i.e. edge e is opposite vertex e (MeshBuilder.cpp:44-54). */
inline EdgeVertexPair localEdge(const MeshTriangle &t, int e)
{
    const int v[3] = {t.v0, t.v1, t.v2};
    const int a = v[(e + 1) % 3];
    const int b = v[(e + 2) % 3];
    return a < b ? qMakePair(a, b) : qMakePair(b, a);
}

inline bool triIndicesValid(const MeshTriangle &t, int nv)
{
    return t.v0 >= 0 && t.v0 < nv && t.v1 >= 0 && t.v1 < nv
        && t.v2 >= 0 && t.v2 < nv;
}

} // namespace

QHash<EdgeVertexPair, QVector<int>> buildEdgeTriangles(const MeshResult &mesh)
{
    QHash<EdgeVertexPair, QVector<int>> out;
    const int nv = mesh.vertices.size();
    out.reserve(mesh.triangles.size() * 2);
    for (int ti = 0; ti < mesh.triangles.size(); ++ti)
    {
        const MeshTriangle &t = mesh.triangles[ti];
        if (!triIndicesValid(t, nv)) continue;
        for (int e = 0; e < 3; ++e) out[localEdge(t, e)].append(ti);
    }
    return out;
}

QVector<double> computeCellLchar(const MeshResult &mesh)
{
    const int nt = mesh.triangles.size();
    const int nv = mesh.vertices.size();
    QVector<double> lchar(nt, 0.0);
    if (nt == 0) return lchar;

    // Centroids, areas and the per-cell face-conductance sum S.
    QVector<QPointF> centroid(nt);
    QVector<double>  area(nt, 0.0);
    QVector<double>  S(nt, 0.0);
    QVector<double>  xiMax(nt, 0.0);
    QVector<bool>    ok(nt, false);

    for (int ti = 0; ti < nt; ++ti)
    {
        const MeshTriangle &t = mesh.triangles[ti];
        if (!triIndicesValid(t, nv)) continue;
        const QPointF &a = mesh.vertices[t.v0].xy;
        const QPointF &b = mesh.vertices[t.v1].xy;
        const QPointF &c = mesh.vertices[t.v2].xy;
        centroid[ti] = QPointF((a.x() + b.x() + c.x()) / 3.0,
                               (a.y() + b.y() + c.y()) / 3.0);
        area[ti] = triangleArea(mesh, ti);
        for (int e = 0; e < 3; ++e)
        {
            const EdgeVertexPair ev = localEdge(t, e);
            const QPointF &p = mesh.vertices[ev.first].xy;
            const QPointF &q = mesh.vertices[ev.second].xy;
            xiMax[ti] = std::max(xiMax[ti], std::hypot(q.x() - p.x(),
                                                       q.y() - p.y()));
        }
        ok[ti] = true;
    }

    // Interior faces, each counted once, exactly as InertialEdges.cpp:41-96.
    const QHash<EdgeVertexPair, QVector<int>> edgeTris = buildEdgeTriangles(mesh);
    int faces = 0;
    for (auto it = edgeTris.constBegin(); it != edgeTris.constEnd(); ++it)
    {
        const QVector<int> &inc = it.value();
        if (inc.size() != 2) continue;              // boundary or non-manifold
        const int t0 = inc[0], t1 = inc[1];
        if (!ok[t0] || !ok[t1]) continue;

        const QPointF &p = mesh.vertices[it.key().first].xy;
        const QPointF &q = mesh.vertices[it.key().second].xy;
        const double ex = q.x() - p.x(), ey = q.y() - p.y();
        const double xi = std::hypot(ex, ey);
        if (!(xi > 0.0)) continue;

        // Unit normal to the shared edge. dn takes |dc . n|, so the outward
        // orientation MeshBuilder resolves is irrelevant here.
        const double nx = ey / xi, ny = -ex / xi;

        const double dcx = centroid[t1].x() - centroid[t0].x();
        const double dcy = centroid[t1].y() - centroid[t0].y();
        const double chord = std::hypot(dcx, dcy);
        double dn = std::abs(dcx * nx + dcy * ny);
        dn = std::max(dn, 0.3 * chord);             // near-degenerate floor
        const double inv = (dn > 1.0e-12) ? 1.0 / dn : 0.0;

        const double s = xi * inv;
        S[t0] += s;
        S[t1] += s;
        ++faces;
    }

    for (int ti = 0; ti < nt; ++ti)
    {
        if (!ok[ti]) continue;
        if (S[ti] > 1.0e-30)
            lchar[ti] = std::sqrt(2.0 * area[ti] / S[ti]);
        else
            lchar[ti] = (xiMax[ti] > 0.0) ? 2.0 * area[ti] / xiMax[ti] : 0.0;
    }
    return lchar;
}

CflStats computeCflStats(const MeshResult &mesh, int ltsTiers)
{
    CflStats s;
    const QVector<double> lchar = computeCellLchar(mesh);
    const int nv = mesh.vertices.size();

    std::vector<double> v;
    v.reserve(static_cast<size_t>(lchar.size()));
    for (int ti = 0; ti < lchar.size(); ++ti)
    {
        if (!triIndicesValid(mesh.triangles[ti], nv)) continue;
        if (!(lchar[ti] > 0.0)) continue;   // degenerate: excluded, not zeroed
        v.push_back(lchar[ti]);
    }
    s.count = static_cast<int>(v.size());
    if (s.count == 0) return s;

    // interiorFaces / isolatedCells are diagnostics for the mirror itself: a
    // mesh with no interior faces is measuring the fallback, not the formula.
    const QHash<EdgeVertexPair, QVector<int>> edgeTris = buildEdgeTriangles(mesh);
    for (auto it = edgeTris.constBegin(); it != edgeTris.constEnd(); ++it)
        if (it.value().size() == 2) ++s.interiorFaces;
    {
        QVector<int> deg(mesh.triangles.size(), 0);
        for (auto it = edgeTris.constBegin(); it != edgeTris.constEnd(); ++it)
            if (it.value().size() == 2)
                for (const int ti : it.value()) ++deg[ti];
        for (int ti = 0; ti < deg.size(); ++ti)
            if (triIndicesValid(mesh.triangles[ti], nv) && deg[ti] == 0)
                ++s.isolatedCells;
    }

    std::sort(v.begin(), v.end());
    auto pct = [&v](double f) {
        const size_t i = static_cast<size_t>(f * double(v.size() - 1) + 0.5);
        return v[std::min(i, v.size() - 1)];
    };
    s.min = v.front();
    s.max = v.back();
    s.p10 = pct(0.10);
    s.p50 = pct(0.50);
    s.p90 = pct(0.90);
    s.ratio = (s.min > 0.0) ? s.p50 / s.min : 0.0;
    s.impliedTiers =
        (s.ratio > 1.0)
            ? static_cast<int>(std::ceil(std::log2(s.ratio))) + 1
            : 1;

    // Work proxy. The engine's tier rule is
    //   tier = min(K-1, floor(log2(dt_cell / dt0)))
    // and at uniform depth dt scales with L_char, so tier depends only on
    // L_char / min (ExplicitInertialSolver.cpp:300-312).
    const int K = std::clamp(ltsTiers, 1, 8);
    double work = 0.0;
    for (const double L : v)
    {
        const int tier = std::min(K - 1,
            static_cast<int>(std::floor(std::log2(L / s.min))));
        work += std::pow(2.0, -std::max(0, tier));
    }
    s.ltsWork = work / s.min;
    return s;
}

} // namespace mesh
