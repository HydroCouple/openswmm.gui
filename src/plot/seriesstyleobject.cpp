/*!
 * \file   seriesstyleobject.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "plot/seriesstyleobject.h"

namespace openswmmvis::plot {

SeriesStyleObject::SeriesStyleObject(QObject *parent)
    : QObject(parent)
{}

SeriesStyleObject::SeriesStyleObject(const SeriesStyle& initial, QObject *parent)
    : QObject(parent), m_style(initial)
{}

void SeriesStyleObject::setStyle(const SeriesStyle& s)
{
    // Delegate to the typed setters so per-field NOTIFYs fire only for the
    // properties that actually changed. The aggregate signal is emitted once
    // at the end (after temporarily blocking it during the burst).
    blockSignals(true);
    setColor(s.color);
    setOpacity(s.opacity);
    setLegendName(s.legendName);

    setShowLine(s.showLine);
    setLineWidth(s.lineWidth);
    setDash(s.dash);
    setCapStyle(s.capStyle);
    setJoinStyle(s.joinStyle);

    setShowMarkers(s.showMarkers);
    setShape(static_cast<MarkerShapeQ>(s.shape));
    setMarkerSize(s.markerSize);
    setMarkerFillColor(s.markerFillColor);
    setMarkerBorderColor(s.markerBorderColor);
    setMarkerBorderWidth(s.markerBorderWidth);

    setShowPointLabels(s.showPointLabels);
    setPointLabelFont(s.pointLabelFont);
    setPointLabelColor(s.pointLabelColor);
    setPointLabelPrecision(s.pointLabelPrecision);

    setShowAreaFill(s.showAreaFill);
    setAreaFillColor(s.areaFillColor);
    blockSignals(false);

    emitAggregate_();
}

void SeriesStyleObject::emitAggregate_()
{
    emit styleChanged(m_style);
}

QString SeriesStyleObject::displayLabelFor(const QString &name) const
{
    // Identity
    if (name == QStringLiteral("color"))               return QStringLiteral("Identity — Colour");
    if (name == QStringLiteral("opacity"))             return QStringLiteral("Identity — Opacity");
    if (name == QStringLiteral("legendName"))          return QStringLiteral("Identity — Legend name");

    // Line
    if (name == QStringLiteral("showLine"))            return QStringLiteral("Line — Visible");
    if (name == QStringLiteral("lineWidth"))           return QStringLiteral("Line — Width");
    if (name == QStringLiteral("dash"))                return QStringLiteral("Line — Dash");
    if (name == QStringLiteral("capStyle"))            return QStringLiteral("Line — Cap");
    if (name == QStringLiteral("joinStyle"))           return QStringLiteral("Line — Join");

    // Markers
    if (name == QStringLiteral("showMarkers"))         return QStringLiteral("Marker — Visible");
    if (name == QStringLiteral("shape"))               return QStringLiteral("Marker — Shape");
    if (name == QStringLiteral("markerSize"))          return QStringLiteral("Marker — Size");
    if (name == QStringLiteral("markerFillColor"))     return QStringLiteral("Marker — Fill colour");
    if (name == QStringLiteral("markerBorderColor"))   return QStringLiteral("Marker — Border colour");
    if (name == QStringLiteral("markerBorderWidth"))   return QStringLiteral("Marker — Border width");

    // Point labels
    if (name == QStringLiteral("showPointLabels"))     return QStringLiteral("Labels — Visible");
    if (name == QStringLiteral("pointLabelFont"))      return QStringLiteral("Labels — Font");
    if (name == QStringLiteral("pointLabelColor"))     return QStringLiteral("Labels — Colour");
    if (name == QStringLiteral("pointLabelPrecision")) return QStringLiteral("Labels — Decimals");

    // Area
    if (name == QStringLiteral("showAreaFill"))        return QStringLiteral("Area — Visible");
    if (name == QStringLiteral("areaFillColor"))       return QStringLiteral("Area — Colour");

    return {};   // empty → fall back to default property name
}

// ---------------------------------------------------------------------------
// Setters — each fires its typed NOTIFY then the aggregate signal.
// All early-return on no-op so we don't churn the chart on every paint.
// ---------------------------------------------------------------------------

#define IMPL_SETTER(field, setter, signal, type)                              \
    void SeriesStyleObject::setter(type v)                                    \
    {                                                                         \
        if (m_style.field == v) return;                                       \
        m_style.field = v;                                                    \
        emit signal(v);                                                       \
        emitAggregate_();                                                     \
    }

IMPL_SETTER(color,              setColor,             colorChanged,             const QColor&)
IMPL_SETTER(opacity,            setOpacity,           opacityChanged,           qreal)
IMPL_SETTER(legendName,         setLegendName,        legendNameChanged,        const QString&)

IMPL_SETTER(showLine,           setShowLine,          showLineChanged,          bool)
IMPL_SETTER(lineWidth,          setLineWidth,         lineWidthChanged,         qreal)
IMPL_SETTER(dash,               setDash,              dashChanged,              Qt::PenStyle)
IMPL_SETTER(capStyle,           setCapStyle,          capStyleChanged,          Qt::PenCapStyle)
IMPL_SETTER(joinStyle,          setJoinStyle,         joinStyleChanged,         Qt::PenJoinStyle)

IMPL_SETTER(showMarkers,        setShowMarkers,       showMarkersChanged,       bool)
IMPL_SETTER(markerSize,         setMarkerSize,        markerSizeChanged,        qreal)
IMPL_SETTER(markerFillColor,    setMarkerFillColor,   markerFillColorChanged,   const QColor&)
IMPL_SETTER(markerBorderColor,  setMarkerBorderColor, markerBorderColorChanged, const QColor&)
IMPL_SETTER(markerBorderWidth,  setMarkerBorderWidth, markerBorderWidthChanged, qreal)

IMPL_SETTER(showPointLabels,    setShowPointLabels,   showPointLabelsChanged,   bool)
IMPL_SETTER(pointLabelFont,     setPointLabelFont,    pointLabelFontChanged,    const QFont&)
IMPL_SETTER(pointLabelColor,    setPointLabelColor,   pointLabelColorChanged,   const QColor&)
IMPL_SETTER(pointLabelPrecision,setPointLabelPrecision,pointLabelPrecisionChanged, int)

IMPL_SETTER(showAreaFill,       setShowAreaFill,      showAreaFillChanged,      bool)
IMPL_SETTER(areaFillColor,      setAreaFillColor,     areaFillColorChanged,     const QColor&)

#undef IMPL_SETTER

// MarkerShape needs a custom setter because the Q_ENUM-registered alias
// (MarkerShapeQ) differs from the storage type (MarkerShape).
void SeriesStyleObject::setShape(MarkerShapeQ s)
{
    const auto target = static_cast<MarkerShape>(s);
    if (m_style.shape == target) return;
    m_style.shape = target;
    emit shapeChanged(s);
    emitAggregate_();
}

} // namespace openswmmvis::plot
