/*!
 * \file   stylepreviewswatch.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/stylepreviewswatch.h"

#include "render/markershape.h"   // G-1 — canonical marker preview

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <cmath>

namespace openswmmvis::ui {

StylePreviewSwatch::StylePreviewSwatch(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    // Theme-live well background (was a hardcoded light gray).
    setBackgroundRole(QPalette::AlternateBase);
}

void StylePreviewSwatch::setKind(Kind k)       { m_kind = k;       update(); }
void StylePreviewSwatch::setColor(const QColor &c)   { m_color = c; update(); }
void StylePreviewSwatch::setStrokePen(const QPen &p) { m_strokePen = p; update(); }
void StylePreviewSwatch::setMarkerSizePx(double px) { m_markerSizePx = px; update(); }
void StylePreviewSwatch::setMarkerShape(int s)      { m_markerShape = s; update(); }
void StylePreviewSwatch::setLineWidthPx(double px)  { m_lineWidthPx = px; update(); }
void StylePreviewSwatch::setShowArrows(bool v)      { m_showArrows = v; update(); }
void StylePreviewSwatch::setFillOpacity(double v)   { m_fillOpacity = v; update(); }

void StylePreviewSwatch::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Outer frame.
    QPen frame(QColor(180, 180, 180));
    frame.setWidthF(0.7);
    p.setPen(frame);
    p.setBrush(palette().window());
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 4, 4);

    const QRectF area = rect().adjusted(8, 8, -8, -8);
    if (!area.isValid()) return;

    switch (m_kind) {
        case PointKind: {
            // G-1 — render every canonical shape via the shared painter so
            // the preview matches the on-canvas VectorPointItem exactly.
            const QPointF c(area.center());
            const double sizePx = std::max(4.0, m_markerSizePx);
            OpenSWMM::Render::drawMarkerShape(
                &p, static_cast<OpenSWMM::Render::MarkerShape>(m_markerShape),
                c, sizePx, QBrush(m_color), m_strokePen);
            break;
        }
        case LineKind: {
            QPen linePen(m_color);
            linePen.setWidthF(std::max(0.5, m_lineWidthPx));
            linePen.setStyle(m_strokePen.style());
            linePen.setCapStyle(Qt::FlatCap);
            linePen.setJoinStyle(Qt::RoundJoin);
            p.setPen(linePen);
            const double y = area.center().y();
            p.drawLine(QPointF(area.left() + 4, y),
                       QPointF(area.right() - 4, y));
            if (m_showArrows) {
                const double tipX = area.center().x();
                const double half = std::max(3.0, m_lineWidthPx * 2.0);
                QPolygonF head;
                head << QPointF(tipX,        y)
                     << QPointF(tipX - half, y - half * 0.6)
                     << QPointF(tipX - half, y + half * 0.6);
                p.setBrush(m_color.darker(140));
                p.setPen(QPen(m_color.darker(160), 0.7));
                p.drawPolygon(head);
            }
            break;
        }
        case PolygonKind: {
            QColor fill = m_color;
            fill.setAlphaF(std::clamp(m_fillOpacity, 0.0, 1.0));
            QPen outline = m_strokePen;
            if (outline.style() == Qt::NoPen) outline = QPen(Qt::NoPen);
            p.setPen(outline);
            p.setBrush(fill);
            const double w = area.width() * 0.6;
            const double h = area.height() * 0.55;
            const QRectF rect(area.center().x() - w * 0.5,
                              area.center().y() - h * 0.5, w, h);
            p.drawRoundedRect(rect, 4, 4);
            break;
        }
    }
}

} // namespace openswmmvis::ui
