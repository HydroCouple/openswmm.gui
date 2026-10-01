/*!
 * \file   breaklinestrips.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Facing break-line pairs (workplans/MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md
 * D12.3): two terrain break lines that run side by side at a steady
 * separation, with nothing between them and lower ground between them than
 * outside both — a street between its curbs, a ditch between its banks.
 * MeshGenerator turns each pair into a bank-pair quad strip whose long sides
 * are the two lines, so the cells follow the feature.
 */
#ifndef OPENSWMMVIS_MESH_BREAKLINESTRIPS_H
#define OPENSWMMVIS_MESH_BREAKLINESTRIPS_H

#include <QPair>
#include <QPointF>
#include <QVector>

#include <functional>

namespace mesh {

struct FacingPairOptions
{
    double minWidth = 0.0;       ///< Separation range, map units (maxWidth > minWidth > 0).
    double maxWidth = 0.0;
    double station  = 0.0;       ///< Sampling step along each line, map units (> 0).
    double maxAngleDeg = 20.0;   ///< Largest angle between the two lines at a station.
    double widthChange = 0.25;   ///< Largest relative change of the separation from one station to the next.
    int    minStations = 4;      ///< A pair also runs at least three separations along the line.
    /*! Each run gives up this fraction of its separation at both ends, so the
     *  strips of two crossing streets keep apart at the intersection. */
    double endTrimFraction = 0.25;
    /*! Ground elevation at a map coordinate (NaN = unknown). Null = no
     *  trough test (every facing pair qualifies). */
    std::function<double(double, double)> elevationAt;
};

struct FacingPair
{
    int lineA = -1, lineB = -1;      ///< Indices into the input lines.
    QVector<QPointF> bankA, bankB;   ///< The facing portions: A in A's direction, B in B's.
    double width  = 0.0;             ///< Median separation.
    double length = 0.0;             ///< Along A.
};

/*! \brief Find facing pairs among \p lines (open polylines; a closed ring
 *  repeats its first point). From stations along each line a ray is cast
 *  both ways along the normal; the first thing it meets within maxWidth
 *  must be another line, near-parallel, at least minWidth away — an
 *  \p obstacles segment, or the same line, blocks it. Consecutive stations
 *  that meet the same line at a steadily changing separation form a run.
 *  Pairs come back longest first and never share a portion of a line. */
[[nodiscard]] QVector<FacingPair> findFacingPairs(const QVector<QVector<QPointF>> &lines,
                                                  const QVector<QPair<QPointF, QPointF>> &obstacles,
                                                  const FacingPairOptions &opt);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_BREAKLINESTRIPS_H
