/*!
 * \file   meshquadtree.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Quadtree core mesher (workplans/MESH_OVERHAUL_PLAN_2026-09-29.md Stage 3).
 *
 * A linear (Morton-keyed) quadtree over the domain in a rotatable frame:
 *
 *  - split while a cell's side exceeds √2 · h(x) sampled over the cell, or
 *    until the floor size (hMin) is reached — leaf sides land in
 *    (h/√2, √2·h], dyadic multiples of hMin;
 *  - subtrees entirely outside the domain (or inside a hole) are pruned at
 *    the first level where no constraint crosses them; subtrees entirely
 *    clear of constraints subdivide by size only, so constraint work is
 *    confined to the cells that touch them;
 *  - 2:1 balance across edges (neighbouring leaves differ by at most one
 *    level), which bounds the edge-length ratio of neighbouring core cells
 *    at 2 by construction;
 *  - a leaf is KEPT when it is inside the domain, no constraint segment
 *    crosses it and it lies at least clearance·h from every constraint;
 *    everything else is left to the fringe conformer (Stage 4);
 *  - hanging nodes on kept leaves are resolved with templates. A quad mesh
 *    needs an even number of boundary edges, so one or three hanging nodes
 *    force a triangle: 1 → 2 quads + 1 triangle, 2 adjacent → 3 quads,
 *    2 opposite → 2 quads, 3 → 3 quads + 1 triangle, 4 → 4 quads. Template
 *    quads have scaled Jacobian ≥ 0.707, template triangles a 45° minimum
 *    angle. In Triangles mode every quad is split along a diagonal
 *    (alternating on the lattice parity for plain squares, through the
 *    widest corner for template quads).
 *
 * Output vertices are returned in mesh coordinates (the frame rotation is
 * undone). The front — every edge of a kept cell that no other kept cell
 * shares — is what the conformer stitches to the constraints.
 */
#ifndef OPENSWMMVIS_MESH_MESHQUADTREE_H
#define OPENSWMMVIS_MESH_MESHQUADTREE_H

#include <QHash>
#include <QPair>
#include <QPointF>
#include <QPolygonF>
#include <QVector>

#include <functional>

namespace mesh {

/*! \brief Rotated frame: cells are axis-aligned in coordinates
 *  x' = R(−angle)·(x − origin). */
struct QuadtreeFrame
{
    QPointF origin;
    double  angleDeg = 0.0;   ///< From +x, counter-clockwise.
};

struct QuadtreeOptions
{
    /*! Target size in mesh units at a mesh coordinate. Required. A
     *  non-positive or non-finite sample means "as large as allowed". */
    std::function<double(double, double)> hAt;
    double hMin = 0.0;        ///< Floor size = finest cell side. Required > 0.
    double hMax = 0.0;        ///< Largest cell side wanted; <= 0 = the root.
    double clearance = 0.5;   ///< Drop a leaf closer than clearance·h to a constraint.
    bool   triangles = false; ///< Triangles mode.
    qint64 maxLeaves = 50'000'000;   ///< Safety cap; build fails beyond it.
    /*! Progress in [0,1]; return false to cancel. */
    std::function<bool(double)> progress;
};

/*! \brief One output cell: v3 < 0 = triangle. Indices into QuadtreeMesh::vertices. */
struct QuadtreeCell
{
    int v0 = 0, v1 = 0, v2 = 0, v3 = -1;
};

struct QuadtreeMesh
{
    QVector<QPointF>      vertices;   ///< Mesh coordinates (frame undone).
    QVector<QuadtreeCell> cells;      ///< Counter-clockwise in mesh coordinates.
    QVector<QPair<int, int>> frontEdges; ///< Edges of kept cells with no kept cell on the other side.
    // Diagnostics
    qint64 leaves = 0, keptLeaves = 0;
    int    balanceSplits = 0;
    int    templateCells = 0;
    int    depth = 0;
    QString errorMsg;

    /*! \brief True when the mesh coordinate lies inside a kept leaf. */
    [[nodiscard]] bool containsPoint(const QPointF &p) const;
    /*! \brief Index of the cell containing the mesh coordinate, -1 when it
     *  is not inside a kept leaf (template pieces are tested individually). */
    [[nodiscard]] int cellAt(const QPointF &p) const;

    // Frame + tree bookkeeping needed by containsPoint (filled by build).
    QuadtreeFrame frame;
    double rootX0 = 0.0, rootY0 = 0.0, rootSide = 0.0;
    QHash<quint64, quint8> leafStatus;   ///< key → 1 kept, 0 dropped.
    QHash<quint64, QPair<int, int>> leafCells;   ///< kept key → (first cell, count).

private:
    [[nodiscard]] quint64 leafKeyAt(const QPointF &p) const;   ///< 0 when outside every leaf.
};

/*! \brief Build the core.
 *  \param domains     outer rings (mesh coordinates; closing duplicate optional)
 *  \param holes       hole rings
 *  \param constraints every constraint polyline (conduits, breaklines,
 *                     region rings, corridor rings). Domain and hole rings
 *                     are constraints too and need not be repeated here.
 */
bool buildQuadtreeCore(const QVector<QPolygonF> &domains,
                       const QVector<QPolygonF> &holes,
                       const QVector<QVector<QPointF>> &constraints,
                       const QuadtreeFrame &frame,
                       const QuadtreeOptions &opt,
                       QuadtreeMesh *out);

/*! \brief Morton-style leaf key: level in the top bits, then x, then y (each < 2^29). */
[[nodiscard]] inline quint64 quadtreeKey(int level, quint32 ix, quint32 iy)
{
    return (quint64(level) << 58) | (quint64(ix) << 29) | quint64(iy);
}

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHQUADTREE_H
