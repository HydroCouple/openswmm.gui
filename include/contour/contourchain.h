/*!
 * \file   contourchain.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * VS.8 — chain unordered marching-triangles segments of ONE iso-level into
 * polylines by matching quantised endpoints. The result drives along-line
 * label placement (one label every N screen pixels). Shared by the
 * QPainter isoline pass (SWMM2DResultsGraphicsItem) and the QSG renderer
 * (SWMM2DResultsQSGRenderer).
 */
#ifndef OPENSWMMVIS_CONTOUR_CONTOURCHAIN_H
#define OPENSWMMVIS_CONTOUR_CONTOURCHAIN_H

#include <QLineF>
#include <QMultiHash>
#include <QPolygonF>
#include <QVector>

#include <cmath>

namespace OpenSWMM::Contour {

/*!
 * \brief Chain unordered iso-line segments into polylines.
 *
 * \param segs    segments of a single iso-level, any order/orientation
 * \param quantum scene-space snap tolerance — shared triangle edges produce
 *                endpoints identical up to floating-point op order, so a
 *                tolerance a few orders below the cell size links them
 *                reliably.
 */
inline QVector<QPolygonF>
chainIsoSegments(const QVector<QLineF> &segs, double quantum)
{
    QVector<QPolygonF> chains;
    if (segs.isEmpty() || quantum <= 0.0) return chains;

    auto key = [quantum](const QPointF &pt) -> quint64 {
        const auto qx = qint32(std::lround(pt.x() / quantum));
        const auto qy = qint32(std::lround(pt.y() / quantum));
        return (quint64(quint32(qx)) << 32) | quint64(quint32(qy));
    };

    QMultiHash<quint64, int> byEndpoint;
    byEndpoint.reserve(segs.size() * 2);
    for (int i = 0; i < segs.size(); ++i) {
        byEndpoint.insert(key(segs[i].p1()), i);
        byEndpoint.insert(key(segs[i].p2()), i);
    }

    QVector<bool> used(segs.size(), false);

    auto takeNext = [&](const QPointF &tip, QPointF &nextPt) -> bool {
        const auto range = byEndpoint.equal_range(key(tip));
        for (auto it = range.first; it != range.second; ++it) {
            const int j = it.value();
            if (used[j]) continue;
            used[j] = true;
            // Continue from whichever endpoint matched the tip.
            const QLineF &s = segs[j];
            nextPt = (key(s.p1()) == key(tip)) ? s.p2() : s.p1();
            return true;
        }
        return false;
    };

    for (int i = 0; i < segs.size(); ++i) {
        if (used[i]) continue;
        used[i] = true;
        QPolygonF poly;
        poly << segs[i].p1() << segs[i].p2();

        QPointF next;
        while (takeNext(poly.last(), next))   poly.append(next);
        while (takeNext(poly.first(), next))  poly.prepend(next);
        chains.append(std::move(poly));
    }
    return chains;
}

} // namespace OpenSWMM::Contour

#endif // OPENSWMMVIS_CONTOUR_CONTOURCHAIN_H
