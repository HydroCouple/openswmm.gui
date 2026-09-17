/*!
 * \file   meshnodemapper.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * MESH_DECOUPLED_1D2D_REMAP_PLAN Part C.1 — node→mesh remapping.
 */
#include "mesh/meshnodemapper.h"

#include "mesh/meshautocouple.h"
#include "mesh/meshcellgeom.h"
#include "layers/meshspatialgrid.h"

#include <QSet>

#include <cmath>
#include <limits>

namespace mesh {

namespace {

/*! Edge-inclusive point-in-triangle via sign-consistent cross products.
 *  eps scales with the triangle so boundary points (nodes snapped onto a
 *  constraint edge) land inside rather than falling through the crack
 *  between two adjacent cells. */
bool pointInTriangle(const QPointF &p, const QPointF &a, const QPointF &b,
                     const QPointF &c)
{
    const double d1 = (p.x() - b.x()) * (a.y() - b.y())
                    - (a.x() - b.x()) * (p.y() - b.y());
    const double d2 = (p.x() - c.x()) * (b.y() - c.y())
                    - (b.x() - c.x()) * (p.y() - c.y());
    const double d3 = (p.x() - a.x()) * (c.y() - a.y())
                    - (c.x() - a.x()) * (p.y() - a.y());

    // Scale-aware tolerance: |cross| is ~ 2 × area contribution, so eps
    // proportional to the triangle's own scale keeps edge points inside.
    const double scale = std::abs(d1) + std::abs(d2) + std::abs(d3);
    const double eps   = 1e-12 * std::max(scale, 1.0);

    const bool hasNeg = (d1 < -eps) || (d2 < -eps) || (d3 < -eps);
    const bool hasPos = (d1 > eps) || (d2 > eps) || (d3 > eps);
    return !(hasNeg && hasPos);
}

} // namespace

NodeMapResult mapNodesToMesh(const MeshResult &mesh,
                             const QVector<QPair<QString, QPointF>> &nodes,
                             double tol,
                             bool preserveExisting)
{
    NodeMapResult r;
    if (mesh.vertices.isEmpty() || mesh.triangles.isEmpty() || nodes.isEmpty())
    {
        for (const auto &n : nodes) r.unmatched.append(n.first);
        return r;
    }

    if (tol <= 0.0)
        tol = defaultCoincidenceTol(mesh);

    // Nodes already coupled somewhere on this mesh (vertex or cell rows).
    QSet<QString> existing;
    if (preserveExisting)
    {
        for (const MeshVertex &v : mesh.vertices)
            if (!v.coupledNode.isEmpty()) existing.insert(v.coupledNode);
        for (const CellCoupling &cc : mesh.cellCouplings)
            existing.insert(cc.nodeId);
    }

    // ── Vertex grid for the coincidence pass ─────────────────────────────
    // Boxes are inflated by tol: MeshSpatialGrid drops zero-area bboxes
    // (QRectF::isValid) on rebuild and rejects zero-area query rects, so a
    // degenerate point box would index — and match — nothing. The 2·tol
    // reach is a superset; the distance test below is the exact filter.
    QVector<QRectF> vboxes;
    vboxes.reserve(mesh.vertices.size());
    for (const MeshVertex &v : mesh.vertices)
        vboxes.append(QRectF(v.xy.x() - tol, v.xy.y() - tol, 2.0 * tol, 2.0 * tol));
    MeshSpatialGrid vGrid;
    vGrid.rebuild(vboxes);

    // ── Triangle grid for the containment pass ───────────────────────────
    QVector<QRectF> tboxes;
    tboxes.reserve(mesh.triangles.size());
    const int nv = mesh.vertices.size();
    // A cell's vertices are all in range, or it never matches.
    auto cellValid = [nv](const MeshTriangle &t) {
        for (int k = 0; k < t.vertexCount(); ++k)
            if (t.vertex(k) < 0 || t.vertex(k) >= nv) return false;
        return true;
    };
    for (const MeshTriangle &t : mesh.triangles)
    {
        if (!cellValid(t))
        {
            tboxes.append(QRectF());   // keep indices aligned; never matches
            continue;
        }
        double xmin = std::numeric_limits<double>::max(), xmax = -xmin;
        double ymin = xmin, ymax = -xmin;
        for (int k = 0; k < t.vertexCount(); ++k)
        {
            const QPointF &p = mesh.vertices[t.vertex(k)].xy;
            xmin = std::min(xmin, p.x()); xmax = std::max(xmax, p.x());
            ymin = std::min(ymin, p.y()); ymax = std::max(ymax, p.y());
        }
        tboxes.append(QRectF(QPointF(xmin, ymin), QPointF(xmax, ymax)));
    }
    MeshSpatialGrid tGrid;
    tGrid.rebuild(tboxes);

    // Vertices claimed during THIS mapping run (first node wins).
    QSet<int> claimed;
    QHash<int, int> newPerCell;   // tri → count of new couplings this run

    for (const auto &n : nodes)
    {
        const QString &id = n.first;
        const QPointF &p  = n.second;

        if (preserveExisting && existing.contains(id))
        {
            r.skippedExisting.append(id);
            continue;
        }

        // 1) Coincident vertex — nearest uncoupled, unclaimed vertex in tol.
        const QRectF probe(p.x() - tol, p.y() - tol, 2.0 * tol, 2.0 * tol);
        int    bestV    = -1;
        double bestDist = tol;
        bool   alreadyOnVertex = false;
        for (const int vi : vGrid.query(probe))
        {
            const QPointF d = mesh.vertices[vi].xy - p;
            const double dist = std::hypot(d.x(), d.y());
            if (dist > tol) continue;          // grid reach is 2·tol
            const QString &cn = mesh.vertices[vi].coupledNode;
            if (!cn.isEmpty())
            {
                // Already this node's vertex — re-mapping it into a cell as
                // well would give the engine two coupling points for one
                // node (double exchange). Reachable on a full re-map, which
                // clears cell rows but keeps vertex couplings.
                if (cn == id) { alreadyOnVertex = true; break; }
                continue;                      // held by another node
            }
            if (claimed.contains(vi)) continue;
            if (dist <= bestDist)
            {
                bestDist = dist;
                bestV    = vi;
            }
        }
        if (alreadyOnVertex)
        {
            r.skippedExisting.append(id);
            continue;
        }
        if (bestV >= 0)
        {
            r.vertexMatches.insert(bestV, id);
            claimed.insert(bestV);
            continue;
        }

        // 2) Containing cell — lowest triangle index wins for determinism
        //    when the node sits exactly on a shared edge. The probe box is
        //    inflated (the grid rejects zero-area query rects); the exact
        //    point-in-triangle test below filters the candidates.
        int bestT = -1;
        QVector<int> cand =
            tGrid.query(QRectF(p.x() - tol, p.y() - tol, 2.0 * tol, 2.0 * tol));
        std::sort(cand.begin(), cand.end());
        for (const int ti : cand)
        {
            const MeshTriangle &t = mesh.triangles[ti];
            if (!cellValid(t)) continue;
            // A quad is tested as its sub-triangle fan (the engine's
            // storage split, mesh::cellGeom); a triangle is its own fan.
            const CellGeom g = cellGeom(mesh.vertices, t);
            bool inside = false;
            for (int s = 0; s < g.nSub && !inside; ++s)
                inside = pointInTriangle(p, mesh.vertices[g.sub[s][0]].xy,
                                         mesh.vertices[g.sub[s][1]].xy,
                                         mesh.vertices[g.sub[s][2]].xy);
            if (inside)
            {
                bestT = ti;
                break;
            }
        }
        if (bestT >= 0)
        {
            CellCoupling cc;
            cc.tri    = bestT;
            cc.nodeId = id;
            cc.cd     = kCellCouplingDefaultCd;
            cc.area   = kCellCouplingDefaultArea;
            r.cellMatches.append(cc);
            ++newPerCell[bestT];
            continue;
        }

        // 3) Outside the mesh.
        r.unmatched.append(id);
    }

    for (auto it = newPerCell.constBegin(); it != newPerCell.constEnd(); ++it)
        if (it.value() > 1) ++r.sharedCells;

    return r;
}

} // namespace mesh
