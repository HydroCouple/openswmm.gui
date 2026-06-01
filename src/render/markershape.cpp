/*!
 * \file   markershape.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  MarkerShape JSON + draw implementation (Slice Z.4).
 */

#include "render/markershape.h"

#include <QBrush>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QPolygonF>

#include <array>
#include <cmath>

namespace OpenSWMM::Render
{

namespace {

struct Mapping {
    MarkerShape shape;
    const char *token;
};
constexpr std::array<Mapping, 19> kMap = {{
    {MarkerShape::Circle,              "circle"},
    {MarkerShape::Square,              "square"},
    {MarkerShape::Triangle,            "triangle"},
    {MarkerShape::Diamond,             "diamond"},
    {MarkerShape::Star,                "star"},
    {MarkerShape::Cross,               "cross"},
    {MarkerShape::Plus,                "plus"},
    {MarkerShape::XCross,              "xcross"},
    {MarkerShape::Pentagon,            "pentagon"},
    {MarkerShape::Hexagon,             "hexagon"},
    {MarkerShape::Arrow,               "arrow"},
    {MarkerShape::EquilateralTriangle, "eqtriangle"},
    {MarkerShape::HalfCircle,          "halfcircle"},
    {MarkerShape::TriangleDown,        "triangledown"},
    {MarkerShape::Octagon,             "octagon"},
    {MarkerShape::Hexagram,            "hexagram"},
    {MarkerShape::ArrowUp,             "arrowup"},
    {MarkerShape::ArrowDown,           "arrowdown"},
    {MarkerShape::ArrowLeft,           "arrowleft"},
}};

QPolygonF regularPolygon(int sides, const QPointF &center, qreal radius,
                          qreal phaseRad)
{
    QPolygonF poly;
    poly.reserve(sides);
    for (int i = 0; i < sides; ++i) {
        const qreal a = phaseRad + (2.0 * M_PI * i) / sides;
        poly << QPointF(center.x() + radius * std::cos(a),
                        center.y() + radius * std::sin(a));
    }
    return poly;
}

QPolygonF starPolygon(int points, const QPointF &center, qreal rOuter,
                       qreal rInner, qreal phaseRad)
{
    QPolygonF poly;
    poly.reserve(points * 2);
    for (int i = 0; i < points * 2; ++i) {
        const qreal r = (i % 2 == 0) ? rOuter : rInner;
        const qreal a = phaseRad + (M_PI * i) / points;
        poly << QPointF(center.x() + r * std::cos(a),
                        center.y() + r * std::sin(a));
    }
    return poly;
}

} // namespace

QString markerShapeToString(MarkerShape shape)
{
    for (const auto &m : kMap)
        if (m.shape == shape)
            return QString::fromLatin1(m.token);
    return QStringLiteral("circle");
}

MarkerShape markerShapeFromString(const QString &s)
{
    for (const auto &m : kMap)
        if (s == QLatin1String(m.token))
            return m.shape;
    return MarkerShape::Circle;
}

void drawMarkerShape(QPainter *painter,
                     MarkerShape shape,
                     const QPointF &center,
                     qreal sizePx,
                     const QBrush &fillBrush,
                     const QPen &outlinePen,
                     qreal rotationDeg)
{
    if (!painter || sizePx <= 0.0)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(fillBrush);
    painter->setPen(outlinePen);

    if (!qFuzzyIsNull(rotationDeg)) {
        painter->translate(center);
        painter->rotate(rotationDeg);
        painter->translate(-center);
    }

    const qreal r = sizePx * 0.5;
    const qreal phaseUp = -M_PI / 2.0;  // 12 o'clock

    switch (shape)
    {
    case MarkerShape::Circle:
        painter->drawEllipse(center, r, r);
        break;

    case MarkerShape::Square:
        painter->drawRect(QRectF(center.x() - r, center.y() - r,
                                 sizePx, sizePx));
        break;

    case MarkerShape::Triangle:
    {
        // Right-pointing isoceles, fitting the size×size box.
        QPolygonF p;
        p << QPointF(center.x() + r, center.y())
          << QPointF(center.x() - r, center.y() - r)
          << QPointF(center.x() - r, center.y() + r);
        painter->drawPolygon(p);
        break;
    }

    case MarkerShape::Diamond:
    {
        QPolygonF p;
        p << QPointF(center.x(), center.y() - r)
          << QPointF(center.x() + r, center.y())
          << QPointF(center.x(), center.y() + r)
          << QPointF(center.x() - r, center.y());
        painter->drawPolygon(p);
        break;
    }

    case MarkerShape::Star:
        painter->drawPolygon(starPolygon(5, center, r, r * 0.4, phaseUp));
        break;

    case MarkerShape::Cross:
    {
        // Stroked '+' — vertical bar + horizontal bar.
        QPen stroke = outlinePen;
        if (stroke.widthF() <= 0)
            stroke.setWidthF(qMax(qreal(1.0), sizePx * 0.10));
        painter->setPen(stroke);
        painter->drawLine(QPointF(center.x(), center.y() - r),
                          QPointF(center.x(), center.y() + r));
        painter->drawLine(QPointF(center.x() - r, center.y()),
                          QPointF(center.x() + r, center.y()));
        break;
    }

    case MarkerShape::Plus:
    {
        // Filled '+' — two interlocked rectangles.
        const qreal arm = r * 0.32;
        QPolygonF p;
        p << QPointF(center.x() - arm, center.y() - r)
          << QPointF(center.x() + arm, center.y() - r)
          << QPointF(center.x() + arm, center.y() - arm)
          << QPointF(center.x() + r,   center.y() - arm)
          << QPointF(center.x() + r,   center.y() + arm)
          << QPointF(center.x() + arm, center.y() + arm)
          << QPointF(center.x() + arm, center.y() + r)
          << QPointF(center.x() - arm, center.y() + r)
          << QPointF(center.x() - arm, center.y() + arm)
          << QPointF(center.x() - r,   center.y() + arm)
          << QPointF(center.x() - r,   center.y() - arm)
          << QPointF(center.x() - arm, center.y() - arm);
        painter->drawPolygon(p);
        break;
    }

    case MarkerShape::XCross:
    {
        QPen stroke = outlinePen;
        if (stroke.widthF() <= 0)
            stroke.setWidthF(qMax(qreal(1.0), sizePx * 0.10));
        painter->setPen(stroke);
        painter->drawLine(QPointF(center.x() - r, center.y() - r),
                          QPointF(center.x() + r, center.y() + r));
        painter->drawLine(QPointF(center.x() - r, center.y() + r),
                          QPointF(center.x() + r, center.y() - r));
        break;
    }

    case MarkerShape::Pentagon:
        painter->drawPolygon(regularPolygon(5, center, r, phaseUp));
        break;

    case MarkerShape::Hexagon:
        painter->drawPolygon(regularPolygon(6, center, r, phaseUp));
        break;

    case MarkerShape::Arrow:
    {
        // Right-pointing arrow head, fitting the box.
        QPolygonF p;
        p << QPointF(center.x() + r,        center.y())
          << QPointF(center.x() - r * 0.6,  center.y() - r)
          << QPointF(center.x() - r * 0.3,  center.y())
          << QPointF(center.x() - r * 0.6,  center.y() + r);
        painter->drawPolygon(p);
        break;
    }

    case MarkerShape::EquilateralTriangle:
        painter->drawPolygon(regularPolygon(3, center, r, phaseUp));
        break;

    case MarkerShape::HalfCircle:
    {
        // Semi-circle with flat side at the bottom.
        QPainterPath path;
        path.moveTo(center.x() - r, center.y());
        path.arcTo(QRectF(center.x() - r, center.y() - r, sizePx, sizePx),
                   180.0, -180.0);
        path.closeSubpath();
        painter->drawPath(path);
        break;
    }

    case MarkerShape::TriangleDown:
    {
        // Down-pointing isoceles, fitting the size×size box.
        QPolygonF p;
        p << QPointF(center.x(),     center.y() + r)
          << QPointF(center.x() - r, center.y() - r)
          << QPointF(center.x() + r, center.y() - r);
        painter->drawPolygon(p);
        break;
    }

    case MarkerShape::Octagon:
        // Flat-topped regular octagon (phase offset by half a step).
        painter->drawPolygon(regularPolygon(8, center, r, phaseUp + M_PI / 8.0));
        break;

    case MarkerShape::Hexagram:
        // Six-pointed star (Star of David).
        painter->drawPolygon(starPolygon(6, center, r, r * 0.5774, phaseUp));
        break;

    case MarkerShape::ArrowUp:
    {
        QPolygonF p;
        p << QPointF(center.x(),            center.y() - r)
          << QPointF(center.x() - r,        center.y() + r * 0.6)
          << QPointF(center.x(),            center.y() + r * 0.3)
          << QPointF(center.x() + r,        center.y() + r * 0.6);
        painter->drawPolygon(p);
        break;
    }

    case MarkerShape::ArrowDown:
    {
        QPolygonF p;
        p << QPointF(center.x(),            center.y() + r)
          << QPointF(center.x() - r,        center.y() - r * 0.6)
          << QPointF(center.x(),            center.y() - r * 0.3)
          << QPointF(center.x() + r,        center.y() - r * 0.6);
        painter->drawPolygon(p);
        break;
    }

    case MarkerShape::ArrowLeft:
    {
        QPolygonF p;
        p << QPointF(center.x() - r,        center.y())
          << QPointF(center.x() + r * 0.6,  center.y() - r)
          << QPointF(center.x() + r * 0.3,  center.y())
          << QPointF(center.x() + r * 0.6,  center.y() + r);
        painter->drawPolygon(p);
        break;
    }
    }

    painter->restore();
}

} // namespace OpenSWMM::Render
