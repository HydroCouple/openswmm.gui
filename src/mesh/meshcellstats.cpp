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

} // namespace mesh
