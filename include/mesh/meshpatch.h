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
#include <QPolygonF>
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

/*! \brief Two open bank polylines with independently supplied directions.
 *  Endpoints are paired by minimum total connection length. Stations use the
 *  union of normalized arc-length vertices, retaining every bank bend exactly.
 *  Positive along subdivides that union to the target spacing; zero retains
 *  only the union. This is normalized arc correspondence, not surveyed stations. */
struct BankPairPatch
{
    QVector<QPointF> bankA, bankB;
    int across = 0;
    double along = 0.0;
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
/*! \brief Requires finite convex CCW quads, consistently shared cell edges,
 *  and one simple boundary matching exactly the outer cell edges. Boundary
 *  crossings, remote touches, holes and disconnected pieces are rejected;
 *  an indexed boundary check avoids comparing every cell pair.
 *  Empty string = valid. */
QString validate(const PatchMesh &pm);

/*! \brief Validate the complete patch and return its CCW boundary, without
 *  a repeated closing vertex. Segment input orientation/order is immaterial.
 *  On failure returns an empty ring and sets \p err. */
QPolygonF orderedPatchBoundary(const PatchMesh &pm, QString *err = nullptr);

/*! \brief Transfinite interpolation on the four straight sides. The result
 *  has (n+1)×(m+1) vertices and n×m quads. On invalid input returns an
 *  empty PatchMesh and sets *err. */
PatchMesh makeTransfinitePatch(const StructuredPatch &p, QString *err = nullptr);

/*! \brief Sweep the centreline: (stations)×(across+1) vertices,
 *  (stations-1)×across quads. Station and index counts are checked before
 *  allocation. Invalid or globally overlapping/touching offsets return an
 *  empty PatchMesh and a diagnostic identifying the affected stations. */
PatchMesh makeSweptPatch(const SweptPatch &p, QString *err = nullptr);

/*! \brief Build a conforming strip between two simple, disjoint banks.
 *  Rejects ambiguous endpoint pairing, touching/crossing banks, folded cells,
 *  unrepresentable station geometry and unsafe subdivision counts. */
PatchMesh makeBankPairPatch(const BankPairPatch &p, QString *err = nullptr);

// ── Quad-region redesign (QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md §4.2) ───

/*! \brief Transfinite (Coons) interpolation on four POLYLINE sides forming a
 *  loop: sides[0] runs c0→c1, sides[1] c1→c2, sides[2] c2→c3, sides[3] c3→c0
 *  (each side's last point equals the next side's first). Sides are
 *  arc-length parametrised; n subdivisions along sides 0/2, m along 1/3.
 *  Boundary vertices are placed ON the polylines (so the ring the caller
 *  emits as PSLG segments is exactly this patch's boundary loop). Returns an
 *  empty PatchMesh + *err on invalid input (side with < 2 points, endpoints
 *  not matching, n or m < 1, folded quad). */
PatchMesh makeTransfinitePatch(const QVector<QVector<QPointF>> &sides, int n, int m,
                               const QString &tag, QString *err = nullptr);

/*! \brief Split a CCW ring at the four \p corners (ring vertex indices, ring
 *  order) into polyline sides, choose n = max(1, round(mean(len0, len2)/h)),
 *  m likewise from sides 1/3, and call the polyline overload. */
PatchMesh makeMappedPatch(const QPolygonF &ring, const QVector<int> &corners, double h,
                          const QString &tag, QString *err = nullptr);

/*! \brief Mapped patch with independent physical-axis spacing. The axis is
 *  undirected, measured counterclockwise from +x in degrees. The opposite
 *  logical side pair most aligned with it receives hAlong; the other receives
 *  hAcross. Equal association scores are rejected. Ring rotation/reversal must
 *  not exchange the physical spacings. Widths use the ring's coordinate units. */
PatchMesh makeMappedPatch(const QPolygonF &ring, const QVector<int> &corners,
                          double hAlong, double hAcross, double alongAngleDeg,
                          const QString &tag, QString *err = nullptr);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHPATCH_H
