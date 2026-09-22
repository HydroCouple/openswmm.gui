/*!
 * \file   channelburnboundary.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Truncating a burned channel at the 2D domain boundary — phase P5 of
 * workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md (§7).
 *
 * WHY THIS EXISTS AT ALL.  The plan flagged (§12.1) that it did not know
 * whether the mesh pipeline clips a boundary-crossing line or drops it. It
 * DROPS it: `meshgenerationdialog.cpp` strips a polyline's intermediate
 * vertices that fall outside the domain and then skips the whole feature when
 * either ENDPOINT is outside, because a segment crossing the boundary makes the
 * PSLG non-planar and aborts Triangle. So there is no clip to reuse, and §7 is
 * a real routine rather than a wiring job.
 *
 * WHAT IT PRODUCES.  The crossing itself, as a normalized position along the
 * ORIGINAL polyline — which is exactly what `InsertNodeSplitCommand` takes —
 * plus the inside portion of the centreline, which is the part that gets
 * burned.
 *
 * ORDER MATTERS WHEN APPLYING THIS.  An outfall must be terminal, and
 * `InsertNodeSplitCommand` refuses OUTFALL for that reason: split at the
 * crossing as a JUNCTION, delete the inside reach, and only then convert the
 * now-terminal node (§16.6).
 */
#ifndef OPENSWMMVIS_MESH_CHANNELBURNBOUNDARY_H
#define OPENSWMMVIS_MESH_CHANNELBURNBOUNDARY_H

#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QVector>

namespace mesh {

/*! \brief One point where a centreline crosses the domain edge. */
struct BoundaryCrossing
{
    QPointF point;               ///< On the domain ring.
    double  t        = 0.0;      ///< Normalized position along the whole polyline, [0, 1] —
                                 ///<     the parameter `InsertNodeSplitCommand` takes.
    double  chainage = 0.0;      ///< Distance along the polyline to \ref point.
    int     segment  = 0;        ///< Index of the polyline segment it lies on.
    bool    entering = false;    ///< True when the path is INSIDE the domain after this point.
};

/*!
 * \brief The domain a channel is clipped against: outer rings minus holes.
 *
 * Mirrors the mesh pipeline's own view (`PipelineInputs::domains` and
 * `holeRings`), so a channel running into a hole is correctly outside.
 */
struct BurnDomain
{
    QVector<QPolygonF> rings;
    QVector<QPolygonF> holes;

    [[nodiscard]] bool isEmpty() const noexcept { return rings.isEmpty(); }
    /*! \brief Inside some ring and outside every hole. */
    [[nodiscard]] bool contains(const QPointF &p) const;
};

/*! \brief Every crossing along \p path, in path order. */
[[nodiscard]] QVector<BoundaryCrossing> boundaryCrossings(const QVector<QPointF> &path,
                                                          const BurnDomain &domain);

/*!
 * \brief The portions of \p path that lie inside \p domain, in path order.
 *
 * Each run starts and ends exactly on the ring where it was cut, so the result
 * is a planar-safe polyline: no segment straddles the boundary. A path wholly
 * inside comes back unchanged; a path wholly outside returns nothing.
 */
[[nodiscard]] QVector<QVector<QPointF>> clipPolylineToDomain(const QVector<QPointF> &path,
                                                             const BurnDomain &domain);

/*!
 * \brief The single crossing to truncate a conduit at (§7 step 2).
 *
 * Walking from the upstream end: if the path starts inside, this is the first
 * EXIT; if it starts outside, the first ENTRY. Either way it is the one point
 * where the burned reach meets the 1D reach that survives.
 *
 * \returns false when the path never crosses — wholly inside (burn it all) or
 *          wholly outside (burn none of it), which \p allInside distinguishes.
 */
[[nodiscard]] bool truncationCrossing(const QVector<QPointF> &path, const BurnDomain &domain,
                                      BoundaryCrossing *out, bool *allInside = nullptr);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_CHANNELBURNBOUNDARY_H
