/*!
 * \file   seriesstyle.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Per-series rendering style for chart plots (comprehensive schema).
 *
 * One `SeriesStyle` describes everything the chart needs to render a
 * single line on a `QChart`: pen (colour / width / dash / cap / join),
 * marker (visible / shape / size / fill / border), point labels (visible
 * / font / colour / precision), area fill (visible / colour), opacity,
 * and a user-editable legend name.
 *
 * Styles round-trip through `QJsonObject` so per-series tweaks persist
 * across sessions. Invalid `QColor`s for the marker-border, marker-fill,
 * point-label, and area-fill colours mean "derive from the main series
 * colour" — callers should fall back via `effectiveMarkerFillColor()`
 * and friends rather than treating those as transparent.
 */
#ifndef OPENSWMMVIS_PLOT_SERIESSTYLE_H
#define OPENSWMMVIS_PLOT_SERIESSTYLE_H

#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QPen>
#include <QString>
#include <Qt>

class QXYSeries;
class QScatterSeries;

namespace openswmmvis::plot {

/*! \brief Marker shape used when `SeriesStyle::showMarkers == true`. */
enum class MarkerShape {
    Circle,
    Square,
    Triangle,
    Diamond,
    Cross,
    Plus
};

/*! \brief Aggregated rendering parameters for one chart series. */
struct SeriesStyle {
    // ---- Identity -------------------------------------------------------
    QColor       color        = QColor(31, 119, 180);   // matplotlib "tab:blue"
    qreal        opacity      = 1.0;                    // 0..1
    QString      legendName;                            // empty = auto

    // ---- Line -----------------------------------------------------------
    bool             showLine   = true;
    qreal            lineWidth  = 1.6;
    Qt::PenStyle     dash       = Qt::SolidLine;
    Qt::PenCapStyle  capStyle   = Qt::FlatCap;
    Qt::PenJoinStyle joinStyle  = Qt::BevelJoin;

    // ---- Markers --------------------------------------------------------
    bool         showMarkers       = false;
    MarkerShape  shape             = MarkerShape::Circle;
    qreal        markerSize        = 5.0;
    QColor       markerFillColor;            // invalid = derive from color
    QColor       markerBorderColor;          // invalid = derive from color
    qreal        markerBorderWidth = 0.0;

    // ---- Point labels ---------------------------------------------------
    bool   showPointLabels      = false;
    QFont  pointLabelFont;                   // default = QFont()
    QColor pointLabelColor;                  // invalid = derive from color
    int    pointLabelPrecision  = 2;         // decimal places

    // ---- Area fill ------------------------------------------------------
    bool   showAreaFill = false;
    QColor areaFillColor;                    // invalid = derive (semi-transparent color)

    QJsonObject toJson() const;
    static SeriesStyle fromJson(const QJsonObject& obj);

    QColor effectiveMarkerFillColor()   const;
    QColor effectiveMarkerBorderColor() const;
    QColor effectivePointLabelColor()   const;
    QColor effectiveAreaFillColor()     const;  // alpha-baked fallback if invalid
};

/*! \brief Returns a default styled cycle position — the 12-color cycle
 *  used when new series are added without an explicit style. Wraps modulo 12. */
SeriesStyle defaultStyleForCycle(int cycleIndex);

/*! \brief String key for `MarkerShape` ↔ JSON. */
QString markerShapeToString(MarkerShape s);
MarkerShape markerShapeFromString(const QString& s);

/*! \brief String key for `Qt::PenStyle` (Solid / Dash / Dot / DashDot / DashDotDot). */
QString penStyleToString(Qt::PenStyle s);
Qt::PenStyle penStyleFromString(const QString& s);

/*! \brief String keys for `Qt::PenCapStyle` and `Qt::PenJoinStyle` (JSON round-trip). */
QString penCapStyleToString(Qt::PenCapStyle s);
Qt::PenCapStyle penCapStyleFromString(const QString& s);
QString penJoinStyleToString(Qt::PenJoinStyle s);
Qt::PenJoinStyle penJoinStyleFromString(const QString& s);

/*! \brief Build a QPen from the line-related fields of \a style.
 *  Width / cap / join / dash all wired up; colour with alpha from `opacity`. */
QPen penForStyle(const SeriesStyle& style);

/*! \brief Apply every field of \a style to \a series.
 *
 *  Sets the line pen, marker visibility / shape / size / fill / border,
 *  series opacity, point-label visibility / font / colour / precision,
 *  optional area-fill brush (only honoured if the series supports it via
 *  `setBrush()` — the area-fill toggle wires that on automatically),
 *  and the legend name when `style.legendName` is non-empty.
 *
 *  Marker shape is encoded via a paired `QScatterSeries` overlay (since
 *  `QLineSeries` itself lacks shape choice). If \a markerOverlay is given,
 *  it is reconfigured in-place; if it is `nullptr`, marker shape is
 *  applied to a wrapped `QScatterSeries` only when \a series is itself
 *  one — otherwise the line's built-in dot markers are toggled. */
void applySeriesStyle(const SeriesStyle& style,
                      QXYSeries* series,
                      QScatterSeries* markerOverlay = nullptr);

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_SERIESSTYLE_H
