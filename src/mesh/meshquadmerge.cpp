/*!
 * \file   meshquadmerge.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Tri-pair merge (TRI_QUAD_MESHING_PLAN §3.1, phase G2). "Blossom-quad"-style
 * greedy pairing: score every mergeable interior edge by the quality of the
 * quad it would produce, then accept pairs best-first while neither triangle
 * has been taken. The perfect-matching refinement is out of scope.
 */
#include "mesh/meshquadmerge.h"

#include "mesh/meshcellgeom.h"

#include <QHash>

#include <algorithm>
#include <cmath>

namespace mesh {

namespace {

struct Candidate
{
    double       score = 0.0;
    int          t1 = -1, t2 = -1;
    MeshTriangle quad;
};

bool sameValue(double a, double b) noexcept
{
    return (std::isnan(a) && std::isnan(b)) || a == b;
}

/*! Interior angles (degrees) and side lengths of a convex quad. */
void quadMetrics(const QVector<MeshVertex> &V, const MeshTriangle &q,
                 double &minAng, double &maxAng, double &minSide, double &maxSide)
{
    minAng = 360.0; maxAng = 0.0;
    minSide = std::numeric_limits<double>::max(); maxSide = 0.0;
    for (int k = 0; k < 4; ++k)
    {
        const QPointF &prev = V[q.vertex((k + 3) % 4)].xy;
        const QPointF &cur  = V[q.vertex(k)].xy;
        const QPointF &next = V[q.vertex((k + 1) % 4)].xy;
        const double ax = prev.x() - cur.x(), ay = prev.y() - cur.y();
        const double bx = next.x() - cur.x(), by = next.y() - cur.y();
        const double la = std::hypot(ax, ay), lb = std::hypot(bx, by);
        if (la <= 0.0 || lb <= 0.0) { minAng = 0.0; maxAng = 180.0; return; }
        double c = (ax * bx + ay * by) / (la * lb);
        c = std::clamp(c, -1.0, 1.0);
        const double ang = std::acos(c) * 180.0 / M_PI;
        minAng = std::min(minAng, ang);
        maxAng = std::max(maxAng, ang);
        minSide = std::min(minSide, lb);
        maxSide = std::max(maxSide, lb);
    }
}

/*! Largest distance of any vertex's z from the plane through the other three. */
double bedNonPlanarity(const QVector<MeshVertex> &V, const MeshTriangle &q)
{
    double worst = 0.0;
    for (int k = 0; k < 4; ++k)
    {
        const MeshVertex &a = V[q.vertex((k + 1) % 4)];
        const MeshVertex &b = V[q.vertex((k + 2) % 4)];
        const MeshVertex &c = V[q.vertex((k + 3) % 4)];
        const MeshVertex &p = V[q.vertex(k)];
        const double ux = b.xy.x() - a.xy.x(), uy = b.xy.y() - a.xy.y(), uz = b.z - a.z;
        const double vx = c.xy.x() - a.xy.x(), vy = c.xy.y() - a.xy.y(), vz = c.z - a.z;
        const double nx = uy * vz - uz * vy;
        const double ny = uz * vx - ux * vz;
        const double nz = ux * vy - uy * vx;
        if (std::abs(nz) < 1e-300) continue;   // degenerate in plan — skip
        const double zPlane = a.z - (nx * (p.xy.x() - a.xy.x()) + ny * (p.xy.y() - a.xy.y())) / nz;
        worst = std::max(worst, std::abs(p.z - zPlane));
    }
    return worst;
}

} // namespace

int mergeTrianglePairs(MeshResult &mesh, const QuadMergeOptions &opts,
                       const QSet<QPair<int, int>> &lockedEdges,
                       QVector<int> *oldToNew)
{
    const int nCells = mesh.triangles.size();
    if (oldToNew)
    {
        oldToNew->resize(nCells);
        for (int i = 0; i < nCells; ++i) (*oldToNew)[i] = i;
    }
    if (nCells < 2) return 0;

    // ── Edge → incident triangles (quads never take part) ─────────────
    // value: (first triangle, second triangle | -1 none | -2 non-manifold)
    QHash<QPair<int, int>, QPair<int, int>> edgeTris;
    edgeTris.reserve(nCells * 3);
    for (int t = 0; t < nCells; ++t)
    {
        const MeshTriangle &c = mesh.triangles[t];
        if (c.isQuad()) continue;
        for (int k = 0; k < 3; ++k)
        {
            int a, b;
            edgeEndpoints(c, k, a, b);
            auto it = edgeTris.find(edgeKey(a, b));
            if (it == edgeTris.end())          edgeTris.insert(edgeKey(a, b), qMakePair(t, -1));
            else if (it.value().second == -1)  it.value().second = t;
            else                               it.value().second = -2;
        }
    }

    // ── Score every mergeable pair ────────────────────────────────────
    QVector<Candidate> cands;
    for (auto it = edgeTris.cbegin(); it != edgeTris.cend(); ++it)
    {
        const int t1 = it.value().first, t2 = it.value().second;
        if (t2 < 0) continue;
        if (lockedEdges.contains(it.key())) continue;

        const MeshTriangle &c1 = mesh.triangles[t1];
        const MeshTriangle &c2 = mesh.triangles[t2];
        if (c1.tag != c2.tag) continue;
        if (!sameValue(c1.mannings, c2.mannings)) continue;
        if (!sameValue(c1.initDepth, c2.initDepth)) continue;
        {
            const auto i1 = mesh.infilOverrides.constFind(t1);
            const auto i2 = mesh.infilOverrides.constFind(t2);
            const bool h1 = i1 != mesh.infilOverrides.constEnd();
            const bool h2 = i2 != mesh.infilOverrides.constEnd();
            if (h1 != h2) continue;
            if (h1 && !(i1.value() == i2.value())) continue;
        }

        // Shared edge (p, q) is local edge k of c1 with opposite vertex r;
        // d is c2's vertex off the shared edge. The union, walked in c1's
        // orientation, is p → d → q → r.
        const int p = it.key().first, q = it.key().second;
        int r = -1;
        for (int k = 0; k < 3; ++k)
            if (c1.vertex(k) != p && c1.vertex(k) != q) { r = c1.vertex(k); break; }
        int d = -1;
        for (int k = 0; k < 3; ++k)
            if (c2.vertex(k) != p && c2.vertex(k) != q) { d = c2.vertex(k); break; }
        if (r < 0 || d < 0 || r == d) continue;

        // Orient the walk along c1: the shared edge must run p→q or q→p in
        // c1's cycle; p→d→q→r is right when c1's cycle visits q right after p.
        int pPos = -1;
        for (int k = 0; k < 3; ++k) if (c1.vertex(k) == p) pPos = k;
        const bool pThenQ = c1.vertex((pPos + 1) % 3) == q;

        Candidate cd;
        cd.t1 = t1; cd.t2 = t2;
        cd.quad.tag       = c1.tag;
        cd.quad.mannings  = c1.mannings;
        cd.quad.initDepth = c1.initDepth;
        if (pThenQ) { cd.quad.v0 = p; cd.quad.v1 = d; cd.quad.v2 = q; cd.quad.v3 = r; }
        else        { cd.quad.v0 = q; cd.quad.v1 = d; cd.quad.v2 = p; cd.quad.v3 = r; }

        // A bowtie (inconsistently oriented pair) or a re-entrant union
        // fails the convexity test.
        if (!cellIsConvex(mesh.vertices, cd.quad)) continue;

        double minAng, maxAng, minSide, maxSide;
        quadMetrics(mesh.vertices, cd.quad, minAng, maxAng, minSide, maxSide);
        if (minAng < opts.minAngleDeg || maxAng > opts.maxAngleDeg) continue;
        if (opts.maxBedNonPlanarity > 0.0
            && bedNonPlanarity(mesh.vertices, cd.quad) > opts.maxBedNonPlanarity)
            continue;

        cd.score = (minAng / std::max(maxAng, 1e-12))
                 * (maxSide > 0.0 ? minSide / maxSide : 0.0);
        cands.append(cd);
    }
    if (cands.isEmpty()) return 0;

    std::stable_sort(cands.begin(), cands.end(), [](const Candidate &a, const Candidate &b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.t1 != b.t1) return a.t1 < b.t1;
        return a.t2 < b.t2;
    });

    // ── Greedy acceptance ─────────────────────────────────────────────
    QVector<bool> used(nCells, false);
    QVector<int>  accepted;
    for (int i = 0; i < cands.size(); ++i)
    {
        const Candidate &cd = cands[i];
        if (used[cd.t1] || used[cd.t2]) continue;
        used[cd.t1] = used[cd.t2] = true;
        accepted.append(i);
    }
    if (accepted.isEmpty()) return 0;

    // ── Rebuild in engine order: triangles, existing quads, new quads ──
    QVector<int> map(nCells, -1);
    QVector<MeshTriangle> cells;
    cells.reserve(nCells - accepted.size());
    for (int t = 0; t < nCells; ++t)
        if (!used[t] && !mesh.triangles[t].isQuad())
        { map[t] = cells.size(); cells.append(mesh.triangles[t]); }
    for (int t = 0; t < nCells; ++t)
        if (mesh.triangles[t].isQuad())
        { map[t] = cells.size(); cells.append(mesh.triangles[t]); }
    for (int i : accepted)
    {
        const Candidate &cd = cands[i];
        map[cd.t1] = map[cd.t2] = cells.size();
        cells.append(cd.quad);
    }
    mesh.triangles = std::move(cells);

    for (CellCoupling &c : mesh.cellCouplings)
        if (c.tri >= 0 && c.tri < nCells) c.tri = map[c.tri];

    if (!mesh.infilOverrides.isEmpty())
    {
        QHash<int, InfilRow> remapped;
        remapped.reserve(mesh.infilOverrides.size());
        for (auto it = mesh.infilOverrides.cbegin(); it != mesh.infilOverrides.cend(); ++it)
        {
            const int key = (it.key() >= 0 && it.key() < nCells) ? map[it.key()] : it.key();
            remapped.insert(key, it.value());
        }
        mesh.infilOverrides = std::move(remapped);
    }

    if (oldToNew) *oldToNew = map;
    return accepted.size();
}

} // namespace mesh
