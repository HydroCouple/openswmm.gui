/*!
 * \file   seriesstyle.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "plot/seriesstyle.h"

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

QJsonObject SeriesStyle::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("color")]       = color.name(QColor::HexArgb);
    o[QStringLiteral("showLine")]    = showLine;
    o[QStringLiteral("lineWidth")]   = lineWidth;
    o[QStringLiteral("dash")]        = penStyleToString(dash);
    o[QStringLiteral("showMarkers")] = showMarkers;
    o[QStringLiteral("shape")]       = markerShapeToString(shape);
    o[QStringLiteral("markerSize")]  = markerSize;
    o[QStringLiteral("opacity")]     = opacity;
    if (!legendName.isEmpty())
        o[QStringLiteral("legendName")] = legendName;
    return o;
}

SeriesStyle SeriesStyle::fromJson(const QJsonObject& obj)
{
    SeriesStyle s;
    if (obj.contains(QStringLiteral("color")))
        s.color = QColor(obj.value(QStringLiteral("color")).toString());
    if (obj.contains(QStringLiteral("showLine")))
        s.showLine = obj.value(QStringLiteral("showLine")).toBool(true);
    if (obj.contains(QStringLiteral("lineWidth")))
        s.lineWidth = obj.value(QStringLiteral("lineWidth")).toDouble(1.6);
    if (obj.contains(QStringLiteral("dash")))
        s.dash = penStyleFromString(obj.value(QStringLiteral("dash")).toString());
    if (obj.contains(QStringLiteral("showMarkers")))
        s.showMarkers = obj.value(QStringLiteral("showMarkers")).toBool(false);
    if (obj.contains(QStringLiteral("shape")))
        s.shape = markerShapeFromString(obj.value(QStringLiteral("shape")).toString());
    if (obj.contains(QStringLiteral("markerSize")))
        s.markerSize = obj.value(QStringLiteral("markerSize")).toDouble(5.0);
    if (obj.contains(QStringLiteral("opacity")))
        s.opacity = obj.value(QStringLiteral("opacity")).toDouble(1.0);
    if (obj.contains(QStringLiteral("legendName")))
        s.legendName = obj.value(QStringLiteral("legendName")).toString();
    return s;
}

} // namespace openswmmvis::plot
