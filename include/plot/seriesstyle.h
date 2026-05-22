/*!
 * \file   seriesstyle.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BL — per-series rendering style for the Comparison Plot.
 *
 * One `SeriesStyle` describes everything the chart needs to render a
 * single line on a `QChart`: color, line on/off + width + dash, marker
 * on/off + shape + size, opacity, and a user-editable legend name.
 *
 * Styles round-trip through `QJsonObject` so a user's per-series tweaks
 * persist across sessions (saved alongside the `.oswp` project sidecar
 * under a per-dialog geometry key).
 */
#ifndef OPENSWMMVIS_PLOT_SERIESSTYLE_H
#define OPENSWMMVIS_PLOT_SERIESSTYLE_H

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <Qt>

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
    QColor       color        = QColor(31, 119, 180);  // matplotlib "tab:blue"
    bool         showLine     = true;
    qreal        lineWidth    = 1.6;
    Qt::PenStyle dash         = Qt::SolidLine;
    bool         showMarkers  = false;
    MarkerShape  shape        = MarkerShape::Circle;
    qreal        markerSize   = 5.0;
    qreal        opacity      = 1.0;                   // 0..1
    QString      legendName;                            // empty = auto

    QJsonObject toJson() const;
    static SeriesStyle fromJson(const QJsonObject& obj);
};

/*! \brief Returns a default styled cycle position — the AT-spec 12-color cycle
 *  used when new series are added without an explicit style. Wraps modulo 12. */
SeriesStyle defaultStyleForCycle(int cycleIndex);

/*! \brief String key for `MarkerShape` → JSON. */
QString markerShapeToString(MarkerShape s);
MarkerShape markerShapeFromString(const QString& s);

/*! \brief String key for `Qt::PenStyle` (Solid / Dash / Dot / DashDot / DashDotDot). */
QString penStyleToString(Qt::PenStyle s);
Qt::PenStyle penStyleFromString(const QString& s);

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_SERIESSTYLE_H
