/*!
 * \file   meshpatch.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Structured quad patches (workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md
 * §3.2, phase G3). Two authoring forms:
 *
 *  - \ref StructuredPatch — a four-sided polygon meshed by transfinite
 *    (Coons) interpolation between opposite sides, n × m quads;
 *  - \ref SweptPatch — a centreline polyline swept with a width, quads
 *    laid across the channel at each station (channel / street case).
 *
 * Both produce a \ref PatchMesh in LOCAL vertex indices, which
 * MeshGenerator::addPatch stitches into the triangle domain: the boundary
 * segments become PSLG constraints, the interior a hole, and the quads are
 * appended after the triangles (engine order).
 */
#ifndef OPENSWMMVIS_MESH_MESHPATCH_H
#define OPENSWMMVIS_MESH_MESHPATCH_H

#include "mesh/meshresult.h"

#include <QPair>
#include <QPointF>
#include <QString>
#include <QVector>

namespace mesh {

/*! \brief Four-sided patch: corners in cyclic order (either orientation);
 *  n subdivisions along corner0→corner1 (and corner3→corner2), m along
 *  corner0→corner3 (and corner1→corner2). */
struct StructuredPatch
{
    QVector<QPointF> corners;   ///< Exactly 4, cyclic.
    int     n = 0, m = 0;
    QString tag;
};

/*! \brief Swept patch: the centreline is offset ±width/2 (mitred at interior
 *  vertices) and cut into \p across quads per station. \p along > 0
 *  resamples every centreline segment to that target spacing (original
 *  vertices are kept); 0 = one station per centreline vertex. */
struct SweptPatch
{
    QVector<QPointF> centreline;  ///< >= 2 points.
    double  width  = 0.0;
    int     across = 0;
    double  along  = 0.0;
    QString tag;
};

/*! \brief A generated patch in local indices. Quads are emitted CCW. */
struct PatchMesh
{
    QVector<QPointF>        xy;
    QVector<MeshTriangle>   quads;             ///< v3 >= 0, local indices.
    QVector<QPair<int,int>> boundarySegments;  ///< Local index pairs around the patch.
    QString                 tag;
};

/*! \brief Input checks. Empty string = valid. */
QString validate(const StructuredPatch &p);
QString validate(const SweptPatch &p);
/*! \brief Rejects a patch with a folded / concave / degenerate quad
 *  (mesh::cellIsConvex). Empty string = valid. */
QString validate(const PatchMesh &pm);

/*! \brief Transfinite interpolation on the four straight sides. The result
 *  has (n+1)×(m+1) vertices and n×m quads. On invalid input returns an
 *  empty PatchMesh and sets *err. */
PatchMesh makeTransfinitePatch(const StructuredPatch &p, QString *err = nullptr);

/*! \brief Sweep the centreline: (stations)×(across+1) vertices,
 *  (stations-1)×across quads. On invalid input (or a folded offset) returns
 *  an empty PatchMesh and sets *err. */
PatchMesh makeSweptPatch(const SweptPatch &p, QString *err = nullptr);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHPATCH_H
