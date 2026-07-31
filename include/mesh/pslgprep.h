/*!
 * \file   pslgprep.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * PSLG preparation utilities shared by the mesh-generation pipeline and its
 * tests: RDP simplification, ring densification, hole-ring preparation
 * (simplify → validate → densify → interior seed, parallel across rings),
 * and a y-banded odd-even point-in-rings index.
 *
 * Hole-ring validation runs on the SIMPLIFIED ring, before densification:
 * densifyRing() only inserts collinear points on existing edges and proper
 * self-intersection ignores endpoint/collinear touches, so a densified ring
 * properly self-intersects iff its simplified parent does — while the O(n²)
 * edge-pair test runs on tens of vertices instead of the densified count.
 * Rings arriving from an OGR UnaryUnion dissolve are valid by construction
 * (GEOS output); only RDP simplification can break them, which is exactly
 * what is validated here.
 */
#ifndef OPENSWMMVIS_MESH_PSLGPREP_H
#define OPENSWMMVIS_MESH_PSLGPREP_H

#include "mesh/meshgenerator.h"

#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QVector>

#include <functional>

namespace mesh {
namespace pslg {

/*! Simplify an open polyline with RDP.  First and last points are always
 *  kept.  Returns \p pts unchanged when epsilon <= 0 or pts.size() <= 2. */
[[nodiscard]] QVector<QPointF> simplifyPolyline(const QVector<QPointF> &pts,
                                                double epsilon);

/*! Simplify a closed polygon ring with RDP.  A closed ring (first == last)
 *  stays closed; a result degenerating below 3 vertices returns the input. */
[[nodiscard]] QVector<QPointF> simplifyRing(const QVector<QPointF> &ring,
                                            double epsilon);

/*! Split every ring edge longer than \p maxLen into equal parts — pure
 *  vertex insertion, geometry unchanged.  No-op when maxLen <= 0. */
[[nodiscard]] QVector<QPointF> densifyRing(const QVector<QPointF> &ring,
                                           double maxLen);

/*! Squared distance from \p p to the CLOSED segment ab. */
[[nodiscard]] double distSqToSegment(const QPointF &p, const QPointF &a,
                                     const QPointF &b);

/*! Snap near-coincident Steiner points to a grid of cell size \p snapEps and
 *  drop duplicates.  Only untagged points (marker == 0) merge; tagged points
 *  always survive.  Survivor order is preserved. */
void snapAndDedupe(QVector<mesh::SteinerPoint> &pts, double snapEps);

/*!
 * \brief Greedy minimum-separation thinning in input order.
 *
 * pts[i] is kept iff no already-kept point lies strictly within \p minSep of
 * it — so input order IS the priority order (earlier points win their
 * neighbourhood).  Spatial hash with cell size = minSep and a 3×3 scan, the
 * same idiom as the terrain Poisson-disk filter.  Returns one keep flag per
 * point; minSep <= 0 keeps everything.
 */
[[nodiscard]] QVector<bool> greedyMinSeparation(const QVector<QPointF> &pts,
                                                double minSep);

/*!
 * \brief One hole ring after preparation for the PSLG.
 *
 * \c ring is always the simplified + densified boundary (used by downstream
 * spatial indexes even when invalid); \c seed and \c valid are set only when
 * the simplified ring passed validation.
 */
struct PreparedRing
{
    QVector<QPointF> ring;   ///< simplified + densified boundary (mesh CRS)
    QPointF          seed;   ///< interior seed point for Triangle's hole carve
    bool             valid = false;  ///< false → degenerate or self-intersecting
};

/*! Prepare a single hole ring: simplify → validate → densify → seed. */
[[nodiscard]] PreparedRing prepareHoleRing(const QVector<QPointF> &raw,
                                           double simplifyEps,
                                           double maxEdgeLen);

/*!
 * \brief Prepare many hole rings in parallel (order-preserving).
 *
 * Rings are processed in chunks via QtConcurrent::blockingMapped; between
 * chunks \p isCancelled is polled and \p onChunk(done, total) reports
 * progress.  \p out receives one entry per input ring, in input order.
 * Invalid rings are counted into \p skippedOut.  Returns false only when
 * cancelled (out then holds the chunks finished so far).
 */
bool prepareHoleRings(const QVector<QVector<QPointF>> &raw,
                      double simplifyEps, double maxEdgeLen,
                      QVector<PreparedRing> *out,
                      const std::function<bool()> &isCancelled = {},
                      const std::function<void(int, int)> &onChunk = {},
                      int *skippedOut = nullptr);

/*!
 * \brief y-banded odd-even point-in-rings index.
 *
 * All rings share one crossing parity, so building with domains only answers
 * "inside any domain ring", and building with domains + holes answers
 * "inside a domain minus its (disjoint) hole rings" — the semantics the
 * worker's terrain filter has always used.  Queries visit only the edges
 * whose y-span overlaps the query's band: ~O(edges / nBands) per point
 * instead of O(total ring vertices).
 */
class PointInRingsIndex
{
public:
    void build(const QVector<QPolygonF>        &domains,
               const QVector<QVector<QPointF>> &holes  = {},
               int                              nBands = 1024);

    [[nodiscard]] bool   contains(const QPointF &p) const;
    [[nodiscard]] QRectF boundingBox() const;
    [[nodiscard]] bool   isEmpty() const { return m_edges.isEmpty(); }

private:
    void addRing(const QVector<QPointF> &ring);

    QVector<QPair<QPointF, QPointF>> m_edges;
    QVector<QVector<int>>            m_bandEdges;
    double m_x0 = 0.0, m_x1 = 0.0, m_y0 = 0.0, m_y1 = 0.0;
    double m_bandH  = 0.0;
    int    m_nBands = 0;
};

} // namespace pslg
} // namespace mesh

#endif // OPENSWMMVIS_MESH_PSLGPREP_H
