/*!
 * \file   meshautocouple.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "mesh/meshautocouple.h"

#include <QSet>

#include <cmath>
#include <limits>

namespace mesh {

namespace {

using GridKey = QPair<qint64, qint64>;

GridKey cellOf(const QPointF &p, double cell)
{
    return { static_cast<qint64>(std::floor(p.x() / cell)),
             static_cast<qint64>(std::floor(p.y() / cell)) };
}

} // namespace

double defaultCoincidenceTol(const MeshResult &mesh)
{
    if (mesh.vertices.isEmpty()) return 1e-9;
    double minX = mesh.vertices[0].xy.x(), maxX = minX;
    double minY = mesh.vertices[0].xy.y(), maxY = minY;
    for (const MeshVertex &v : mesh.vertices) {
        minX = std::min(minX, v.xy.x()); maxX = std::max(maxX, v.xy.x());
        minY = std::min(minY, v.xy.y()); maxY = std::max(maxY, v.xy.y());
    }
    const double diag = std::hypot(maxX - minX, maxY - minY);
    return std::max(1e-9, 1e-6 * diag);
}

AutoCoupleResult findCoincidentNodes(const MeshResult &mesh,
                                     const QVector<QPair<QString, QPointF>> &nodes,
                                     const QList<int> &targets,
                                     double tol)
{
    AutoCoupleResult out;
    if (mesh.vertices.isEmpty() || nodes.isEmpty()) {
        out.unmatchedNodes = nodes.size();
        return out;
    }
    if (tol <= 0.0) tol = defaultCoincidenceTol(mesh);

    // Hash the candidate vertices on a tol-sized grid so each node only
    // probes its 3×3 neighbourhood — O(V + N) instead of O(V × N) for the
    // whole-mesh scan.
    QHash<GridKey, QList<int>> grid;
    auto addCandidate = [&](int vi) {
        if (vi < 0 || vi >= mesh.vertices.size()) return;
        grid[cellOf(mesh.vertices[vi].xy, tol)].append(vi);
    };
    if (targets.isEmpty()) {
        for (int vi = 0; vi < mesh.vertices.size(); ++vi) addCandidate(vi);
    } else {
        for (int vi : targets) addCandidate(vi);
    }

    for (const auto &node : nodes) {
        const QString &id = node.first;
        const QPointF &xy = node.second;
        if (id.isEmpty() || !std::isfinite(xy.x()) || !std::isfinite(xy.y()))
            continue;

        // Nearest in-tolerance candidate wins.
        int bestVi = -1;
        double bestD = std::numeric_limits<double>::max();
        const GridKey c = cellOf(xy, tol);
        for (qint64 dx = -1; dx <= 1; ++dx)
            for (qint64 dy = -1; dy <= 1; ++dy) {
                const auto it = grid.constFind({ c.first + dx, c.second + dy });
                if (it == grid.cend()) continue;
                for (int vi : it.value()) {
                    const double d = std::hypot(mesh.vertices[vi].xy.x() - xy.x(),
                                                mesh.vertices[vi].xy.y() - xy.y());
                    if (d <= tol && d < bestD) { bestD = d; bestVi = vi; }
                }
            }

        if (bestVi < 0) { ++out.unmatchedNodes; continue; }
        if (!mesh.vertices[bestVi].coupledNode.isEmpty()) { ++out.alreadyCoupled; continue; }
        if (out.matches.contains(bestVi)) { ++out.unmatchedNodes; continue; }
        out.matches.insert(bestVi, id);
    }
    return out;
}

} // namespace mesh
