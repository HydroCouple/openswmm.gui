/*!
 * \file   seriesstyle.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "plot/seriesstyle.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QScatterSeries>
#include <QXYSeries>

namespace openswmmvis::plot {

// Matplotlib "tab10" + a few extra warmer tones for 12-cycle wrap.
static const QColor kCycle[12] = {
    QColor( 31, 119, 180),  // tab:blue
    QColor(255, 127,  14),  // tab:orange
    QColor( 44, 160,  44),  // tab:green
    QColor(214,  39,  40),  // tab:red
    QColor(148, 103, 189),  // tab:purple
    QColor(140,  86,  75),  // tab:brown
    QColor(227, 119, 194),  // tab:pink
    QColor(127, 127, 127),  // tab:gray
    QColor(188, 189,  34),  // tab:olive
    QColor( 23, 190, 207),  // tab:cyan
    QColor(255, 187, 120),  // light orange
    QColor(174, 199, 232),  // light blue
};

SeriesStyle defaultStyleForCycle(int cycleIndex)
{
    SeriesStyle s;
    const int n = static_cast<int>(sizeof(kCycle) / sizeof(kCycle[0]));
    s.color = kCycle[((cycleIndex % n) + n) % n];
    return s;
}

QColor SeriesStyle::effectiveMarkerFillColor() const
{
    return markerFillColor.isValid() ? markerFillColor : color;
}

QColor SeriesStyle::effectiveMarkerBorderColor() const
{
    return markerBorderColor.isValid() ? markerBorderColor : color.darker(140);
}

QColor SeriesStyle::effectivePointLabelColor() const
{
    return pointLabelColor.isValid() ? pointLabelColor : color.darker(160);
}

QColor SeriesStyle::effectiveAreaFillColor() const
{
    if (areaFillColor.isValid()) return areaFillColor;
    QColor c = color;
    c.setAlphaF(0.25);  // semi-transparent fill under the line
    return c;
}

QString markerShapeToString(MarkerShape s)
{
    switch (s) {
    case MarkerShape::Circle:   return QStringLiteral("circle");
    case MarkerShape::Square:   return QStringLiteral("square");
    case MarkerShape::Triangle: return QStringLiteral("triangle");
    case MarkerShape::Diamond:  return QStringLiteral("diamond");
    case MarkerShape::Cross:    return QStringLiteral("cross");
    case MarkerShape::Plus:     return QStringLiteral("plus");
    }
    return QStringLiteral("circle");
}

MarkerShape markerShapeFromString(const QString& s)
{
    if (s == QStringLiteral("square"))   return MarkerShape::Square;
    if (s == QStringLiteral("triangle")) return MarkerShape::Triangle;
    if (s == QStringLiteral("diamond"))  return MarkerShape::Diamond;
    if (s == QStringLiteral("cross"))    return MarkerShape::Cross;
    if (s == QStringLiteral("plus"))     return MarkerShape::Plus;
    return MarkerShape::Circle;
}

QString penStyleToString(Qt::PenStyle s)
{
    switch (s) {
    case Qt::SolidLine:      return QStringLiteral("solid");
    case Qt::DashLine:       return QStringLiteral("dash");
    case Qt::DotLine:        return QStringLiteral("dot");
    case Qt::DashDotLine:    return QStringLiteral("dash-dot");
    case Qt::DashDotDotLine: return QStringLiteral("dash-dot-dot");
    default:                 return QStringLiteral("solid");
    }
}

Qt::PenStyle penStyleFromString(const QString& s)
{
    if (s == QStringLiteral("dash"))         return Qt::DashLine;
    if (s == QStringLiteral("dot"))          return Qt::DotLine;
    if (s == QStringLiteral("dash-dot"))     return Qt::DashDotLine;
    if (s == QStringLiteral("dash-dot-dot")) return Qt::DashDotDotLine;
    return Qt::SolidLine;
}

QString penCapStyleToString(Qt::PenCapStyle s)
{
    switch (s) {
    case Qt::FlatCap:   return QStringLiteral("flat");
    case Qt::SquareCap: return QStringLiteral("square");
    case Qt::RoundCap:  return QStringLiteral("round");
    default:            return QStringLiteral("flat");
    }
}

Qt::PenCapStyle penCapStyleFromString(const QString& s)
{
    if (s == QStringLiteral("square")) return Qt::SquareCap;
    if (s == QStringLiteral("round"))  return Qt::RoundCap;
    return Qt::FlatCap;
}

QString penJoinStyleToString(Qt::PenJoinStyle s)
{
    switch (s) {
    case Qt::BevelJoin:    return QStringLiteral("bevel");
    case Qt::MiterJoin:    return QStringLiteral("miter");
    case Qt::RoundJoin:    return QStringLiteral("round");
    case Qt::SvgMiterJoin: return QStringLiteral("svg-miter");
    default:               return QStringLiteral("bevel");
    }
}

Qt::PenJoinStyle penJoinStyleFromString(const QString& s)
{
    if (s == QStringLiteral("miter"))     return Qt::MiterJoin;
    if (s == QStringLiteral("round"))     return Qt::RoundJoin;
    if (s == QStringLiteral("svg-miter")) return Qt::SvgMiterJoin;
    return Qt::BevelJoin;
}

namespace {

QString colorToJsonString(const QColor& c)
{
    return c.isValid() ? c.name(QColor::HexArgb) : QString();
}

QColor colorFromJson(const QJsonValue& v)
{
    if (!v.isString()) return QColor();
    const QString s = v.toString();
    return s.isEmpty() ? QColor() : QColor(s);
}

} // anonymous

QJsonObject SeriesStyle::toJson() const
{
    QJsonObject o;
    // Identity
    o[QStringLiteral("color")]       = color.name(QColor::HexArgb);
    o[QStringLiteral("opacity")]     = opacity;
    if (!legendName.isEmpty())
        o[QStringLiteral("legendName")] = legendName;

    // Line
    o[QStringLiteral("showLine")]    = showLine;
    o[QStringLiteral("lineWidth")]   = lineWidth;
    o[QStringLiteral("dash")]        = penStyleToString(dash);
    o[QStringLiteral("capStyle")]    = penCapStyleToString(capStyle);
    o[QStringLiteral("joinStyle")]   = penJoinStyleToString(joinStyle);

    // Markers
    o[QStringLiteral("showMarkers")]       = showMarkers;
    o[QStringLiteral("shape")]             = markerShapeToString(shape);
    o[QStringLiteral("markerSize")]        = markerSize;
    if (markerFillColor.isValid())
        o[QStringLiteral("markerFillColor")]   = colorToJsonString(markerFillColor);
    if (markerBorderColor.isValid())
        o[QStringLiteral("markerBorderColor")] = colorToJsonString(markerBorderColor);
    o[QStringLiteral("markerBorderWidth")] = markerBorderWidth;

    // Point labels
    o[QStringLiteral("showPointLabels")]     = showPointLabels;
    o[QStringLiteral("pointLabelFont")]      = pointLabelFont.toString();
    if (pointLabelColor.isValid())
        o[QStringLiteral("pointLabelColor")] = colorToJsonString(pointLabelColor);
    o[QStringLiteral("pointLabelPrecision")] = pointLabelPrecision;
    o[QStringLiteral("pointLabelFormatMode")] = static_cast<int>(pointLabelFormatMode);

    // Area fill
    o[QStringLiteral("showAreaFill")] = showAreaFill;
    if (areaFillColor.isValid())
        o[QStringLiteral("areaFillColor")] = colorToJsonString(areaFillColor);

    return o;
}

SeriesStyle SeriesStyle::fromJson(const QJsonObject& obj)
{
    SeriesStyle s;
    // Identity
    if (obj.contains(QStringLiteral("color")))
        s.color = QColor(obj.value(QStringLiteral("color")).toString());
    if (obj.contains(QStringLiteral("opacity")))
        s.opacity = obj.value(QStringLiteral("opacity")).toDouble(1.0);
    if (obj.contains(QStringLiteral("legendName")))
        s.legendName = obj.value(QStringLiteral("legendName")).toString();

    // Line
    if (obj.contains(QStringLiteral("showLine")))
        s.showLine = obj.value(QStringLiteral("showLine")).toBool(true);
    if (obj.contains(QStringLiteral("lineWidth")))
        s.lineWidth = obj.value(QStringLiteral("lineWidth")).toDouble(1.6);
    if (obj.contains(QStringLiteral("dash")))
        s.dash = penStyleFromString(obj.value(QStringLiteral("dash")).toString());
    if (obj.contains(QStringLiteral("capStyle")))
        s.capStyle = penCapStyleFromString(obj.value(QStringLiteral("capStyle")).toString());
    if (obj.contains(QStringLiteral("joinStyle")))
        s.joinStyle = penJoinStyleFromString(obj.value(QStringLiteral("joinStyle")).toString());

    // Markers
    if (obj.contains(QStringLiteral("showMarkers")))
        s.showMarkers = obj.value(QStringLiteral("showMarkers")).toBool(false);
    if (obj.contains(QStringLiteral("shape")))
        s.shape = markerShapeFromString(obj.value(QStringLiteral("shape")).toString());
    if (obj.contains(QStringLiteral("markerSize")))
        s.markerSize = obj.value(QStringLiteral("markerSize")).toDouble(5.0);
    if (obj.contains(QStringLiteral("markerFillColor")))
        s.markerFillColor = colorFromJson(obj.value(QStringLiteral("markerFillColor")));
    if (obj.contains(QStringLiteral("markerBorderColor")))
        s.markerBorderColor = colorFromJson(obj.value(QStringLiteral("markerBorderColor")));
    if (obj.contains(QStringLiteral("markerBorderWidth")))
        s.markerBorderWidth = obj.value(QStringLiteral("markerBorderWidth")).toDouble(0.0);

    // Point labels
    if (obj.contains(QStringLiteral("showPointLabels")))
        s.showPointLabels = obj.value(QStringLiteral("showPointLabels")).toBool(false);
    if (obj.contains(QStringLiteral("pointLabelFont")))
        s.pointLabelFont.fromString(obj.value(QStringLiteral("pointLabelFont")).toString());
    if (obj.contains(QStringLiteral("pointLabelColor")))
        s.pointLabelColor = colorFromJson(obj.value(QStringLiteral("pointLabelColor")));
    if (obj.contains(QStringLiteral("pointLabelPrecision")))
        s.pointLabelPrecision = obj.value(QStringLiteral("pointLabelPrecision")).toInt(2);
    if (obj.contains(QStringLiteral("pointLabelFormatMode")))
        s.pointLabelFormatMode = static_cast<NumberFormatMode>(
            obj.value(QStringLiteral("pointLabelFormatMode")).toInt(0));

    // Area fill
    if (obj.contains(QStringLiteral("showAreaFill")))
        s.showAreaFill = obj.value(QStringLiteral("showAreaFill")).toBool(false);
    if (obj.contains(QStringLiteral("areaFillColor")))
        s.areaFillColor = colorFromJson(obj.value(QStringLiteral("areaFillColor")));

    return s;
}

// ---------------------------------------------------------------------------
// Apply helpers
// ---------------------------------------------------------------------------

QPen penForStyle(const SeriesStyle& style)
{
    QColor c = style.color;
    if (style.opacity < 1.0 && c.isValid())
        c.setAlphaF(c.alphaF() * style.opacity);

    QPen p(c);
    p.setStyle(style.dash);
    p.setWidthF(style.lineWidth);
    p.setCapStyle(style.capStyle);
    p.setJoinStyle(style.joinStyle);
    return p;
}

namespace {

// Render the requested marker glyph as a small pixmap for QScatterSeries
// to use as its light marker. Built per-style change; cheap (one pixmap).
QPixmap buildMarkerPixmap(const SeriesStyle& style)
{
    const int side = std::max(4, qRound(style.markerSize) + 2);
    QImage img(side, side, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QPointF c(side * 0.5, side * 0.5);
    const qreal r = style.markerSize * 0.5;

    QColor fill   = style.effectiveMarkerFillColor();
    QColor border = style.effectiveMarkerBorderColor();
    if (style.opacity < 1.0) {
        fill.setAlphaF(fill.alphaF() * style.opacity);
        border.setAlphaF(border.alphaF() * style.opacity);
    }

    QPen borderPen(border);
    borderPen.setWidthF(std::max(0.0, style.markerBorderWidth));
    if (style.markerBorderWidth <= 0.0) borderPen = Qt::NoPen;
    painter.setPen(borderPen);
    painter.setBrush(fill);

    switch (style.shape) {
    case MarkerShape::Circle:
        painter.drawEllipse(c, r, r);
        break;
    case MarkerShape::Square:
        painter.drawRect(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r));
        break;
    case MarkerShape::Triangle: {
        QPolygonF tri;
        tri << QPointF(c.x(), c.y() - r)
            << QPointF(c.x() + r * 0.866, c.y() + r * 0.5)
            << QPointF(c.x() - r * 0.866, c.y() + r * 0.5);
        painter.drawPolygon(tri);
        break;
    }
    case MarkerShape::Diamond: {
        QPolygonF dia;
        dia << QPointF(c.x(),     c.y() - r)
            << QPointF(c.x() + r, c.y())
            << QPointF(c.x(),     c.y() + r)
            << QPointF(c.x() - r, c.y());
        painter.drawPolygon(dia);
        break;
    }
    case MarkerShape::Cross: {
        QPen strokePen(fill);
        strokePen.setWidthF(std::max(1.0, r * 0.4));
        strokePen.setCapStyle(Qt::FlatCap);
        painter.setPen(strokePen);
        const qreal d = r * 0.707;  // 45° offsets
        painter.drawLine(QPointF(c.x() - d, c.y() - d), QPointF(c.x() + d, c.y() + d));
        painter.drawLine(QPointF(c.x() - d, c.y() + d), QPointF(c.x() + d, c.y() - d));
        break;
    }
    case MarkerShape::Plus: {
        QPen strokePen(fill);
        strokePen.setWidthF(std::max(1.0, r * 0.4));
        strokePen.setCapStyle(Qt::FlatCap);
        painter.setPen(strokePen);
        painter.drawLine(QPointF(c.x() - r, c.y()), QPointF(c.x() + r, c.y()));
        painter.drawLine(QPointF(c.x(), c.y() - r), QPointF(c.x(), c.y() + r));
        break;
    }
    }

    painter.end();
    return QPixmap::fromImage(img);
}

} // anonymous

void applySeriesStyle(const SeriesStyle& style,
                      QXYSeries* series,
                      QScatterSeries* markerOverlay)
{
    if (!series) return;

    // ---- Line / pen ---------------------------------------------------------
    series->setPen(penForStyle(style));
    series->setVisible(style.showLine || style.showMarkers);
    series->setOpacity(style.opacity);
    if (!style.legendName.isEmpty())
        series->setName(style.legendName);

    // ---- Markers ------------------------------------------------------------
    auto *asScatter = qobject_cast<QScatterSeries*>(series);
    if (asScatter) {
        // The series itself is a scatter — apply marker fields directly.
        asScatter->setMarkerSize(style.markerSize);
        asScatter->setColor(style.effectiveMarkerFillColor());
        asScatter->setBorderColor(style.effectiveMarkerBorderColor());
        asScatter->setVisible(style.showMarkers);
    } else if (markerOverlay) {
        // Paired scatter overlay: render the chosen shape as a pixmap so the
        // user gets shape variety (Circle/Square/Triangle/...).
        markerOverlay->setMarkerSize(style.markerSize);
        markerOverlay->setBrush(buildMarkerPixmap(style));
        markerOverlay->setPen(QPen(Qt::NoPen));
        markerOverlay->setVisible(style.showMarkers);
        markerOverlay->setOpacity(style.opacity);
        if (!style.legendName.isEmpty())
            markerOverlay->setName(style.legendName);
    } else {
        // No overlay supplied — fall back to Qt's built-in tiny dot markers.
        series->setPointsVisible(style.showMarkers);
    }

    // ---- Point labels -------------------------------------------------------
    series->setPointLabelsVisible(style.showPointLabels);
    if (style.showPointLabels) {
        series->setPointLabelsFont(style.pointLabelFont);
        series->setPointLabelsColor(style.effectivePointLabelColor());
        const NumberFormat labelFmt{ style.pointLabelFormatMode, style.pointLabelPrecision };
        series->setPointLabelsFormat(labelFmt.printfSpec());
    }

    // ---- Area fill ----------------------------------------------------------
    // QXYSeries doesn't paint an area under the line itself — area fill is a
    // host-dialog concern (wrap in QAreaSeries when style.showAreaFill is on).
    // We still set the series brush so anything that consults it (e.g.
    // QAreaSeries::setBrush via the upper series) has a sensible default.
    if (style.showAreaFill)
        series->setBrush(QBrush(style.effectiveAreaFillColor()));
    else
        series->setBrush(Qt::NoBrush);
}

} // namespace openswmmvis::plot
