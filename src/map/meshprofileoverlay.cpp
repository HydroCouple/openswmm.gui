/*!
 * \file   meshprofileoverlay.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "map/meshprofileoverlay.h"

#include <QPainter>
#include <QPen>
#include <QPolygonF>

#include <cmath>
#include <limits>

namespace
{
const QColor kLineColor(0x7B, 0x2F, 0xBE);          // profile line (violet)
const QColor kArrowFill(0xE8, 0x3A, 0x1F);          // position arrow (orange-red)
} // namespace

void MeshProfileOverlay::setPolyline(const QVector<QPointF> &scenePolyline)
{
    m_poly = scenePolyline;

    // Cumulative chainage per vertex.
    m_chain.clear();
    m_chain.reserve(m_poly.size());
    m_total = 0.0;
    for (int i = 0; i < m_poly.size(); ++i) {
        if (i > 0)
            m_total += std::hypot(m_poly[i].x() - m_poly[i - 1].x(),
                                  m_poly[i].y() - m_poly[i - 1].y());
        m_chain.push_back(m_total);
    }
}

void MeshProfileOverlay::setArrowChainage(double chainage)
{
    m_arrowChainage = (m_poly.size() < 2) ? -1.0 : chainage;
}

void MeshProfileOverlay::paint(QPainter &p, const SceneToPixel &toPixel) const
{
    if (m_poly.size() >= 2) {
        QPolygonF pix;
        pix.reserve(m_poly.size());
        for (const QPointF &sp : m_poly)
            pix << toPixel(sp);

        auto makePen = [](const QColor &c, qreal w) {
            QPen pen(c);
            pen.setWidthF(w);            // pixel widths — already in device space
            pen.setJoinStyle(Qt::RoundJoin);
            pen.setCapStyle(Qt::RoundCap);
            return pen;
        };

        p.save();
        p.setBrush(Qt::NoBrush);
        // White casing under a colored line so the path reads on any basemap.
        p.setPen(makePen(QColor(0xFF, 0xFF, 0xFF, 220), 5.0));
        p.drawPolyline(pix);
        p.setPen(makePen(kLineColor, 2.5));
        p.drawPolyline(pix);
        p.restore();
    }

    if (m_arrowChainage >= 0.0 && m_poly.size() >= 2) {
        // Downward-pointing pin: tip marks the exact position, body above, at a
        // constant pixel size regardless of zoom (matches MapToolProfileMarker).
        const QPointF a = toPixel(chainageToScene(m_arrowChainage));
        QPolygonF pin;
        pin << a << QPointF(a.x() - 7.0, a.y() - 16.0)
                 << QPointF(a.x() + 7.0, a.y() - 16.0);
        QPen outline(QColor(0xFF, 0xFF, 0xFF, 230));
        outline.setWidthF(1.5);
        p.save();
        p.setPen(outline);
        p.setBrush(kArrowFill);
        p.drawPolygon(pin);
        p.restore();
    }
}

QPointF MeshProfileOverlay::chainageToScene(double chainage) const
{
    if (m_poly.isEmpty()) return QPointF();
    if (m_poly.size() == 1) return m_poly.first();
    const double c = std::clamp(chainage, 0.0, m_total);
    for (int i = 1; i < m_poly.size(); ++i) {
        if (c > m_chain[i]) continue;
        const double seg = m_chain[i] - m_chain[i - 1];
        const double t   = (seg > 1e-12) ? (c - m_chain[i - 1]) / seg : 0.0;
        return QPointF(m_poly[i - 1].x() + (m_poly[i].x() - m_poly[i - 1].x()) * t,
                       m_poly[i - 1].y() + (m_poly[i].y() - m_poly[i - 1].y()) * t);
    }
    return m_poly.last();
}

double MeshProfileOverlay::nearestChainage(const QPointF &scenePt) const
{
    if (m_poly.size() < 2) return 0.0;
    double bestD2 = std::numeric_limits<double>::infinity();
    double bestChain = 0.0;
    for (int i = 1; i < m_poly.size(); ++i) {
        const QPointF a = m_poly[i - 1], b = m_poly[i];
        const double abx = b.x() - a.x(), aby = b.y() - a.y();
        const double len2 = abx * abx + aby * aby;
        double t = 0.0;
        if (len2 > 1e-12)
            t = std::clamp(((scenePt.x() - a.x()) * abx + (scenePt.y() - a.y()) * aby) / len2,
                           0.0, 1.0);
        const double projx = a.x() + abx * t, projy = a.y() + aby * t;
        const double dx = scenePt.x() - projx, dy = scenePt.y() - projy;
        const double d2 = dx * dx + dy * dy;
        if (d2 < bestD2) {
            bestD2 = d2;
            bestChain = m_chain[i - 1] + t * std::sqrt(len2);
        }
    }
    return bestChain;
}
