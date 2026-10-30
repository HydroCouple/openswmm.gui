/**
 * @file      seriesstyle.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Per-series rendering style for chart plots (comprehensive schema).
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PLOT_SERIESSTYLE_H
#define OPENSWMMVIS_PLOT_SERIESSTYLE_H

#include "plot/numberformat.h"

#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QPen>
#include <QString>
#include <Qt>

class QXYSeries;
class QScatterSeries;

namespace openswmmvis::plot
{

/*! \brief Marker shape used when `SeriesStyle::showMarkers == true`. */
enum class MarkerShape { Circle, Square, Triangle, Diamond, Cross, Plus };

/*! \brief Aggregated rendering parameters for one chart series. */
struct SeriesStyle {
    // ---- Identity -------------------------------------------------------
    QColor color = QColor(31, 119, 180); // matplotlib "tab:blue"
    qreal opacity = 1.0;                 // 0..1
    QString legendName;                  // empty = auto

    // ---- Line -----------------------------------------------------------
    bool showLine = true;
    qreal lineWidth = 1.6;
    Qt::PenStyle dash = Qt::SolidLine;
    Qt::PenCapStyle capStyle = Qt::FlatCap;
    Qt::PenJoinStyle joinStyle = Qt::BevelJoin;

    // ---- Markers --------------------------------------------------------
    bool showMarkers = false;
    MarkerShape shape = MarkerShape::Circle;
    qreal markerSize = 5.0;
    QColor markerFillColor;   // invalid = derive from color
    QColor markerBorderColor; // invalid = derive from color
    qreal markerBorderWidth = 0.0;

    // ---- Point labels ---------------------------------------------------
    bool showPointLabels = false;
    QFont pointLabelFont;        // default = QFont()
    QColor pointLabelColor;      // invalid = derive from color
    int pointLabelPrecision = 2; // decimals or sig figs (see mode)
    NumberFormatMode pointLabelFormatMode = NumberFormatMode::Decimals;
    QString pointLabelFormat; // optional printf override; empty = use mode+precision

    // ---- Area fill ------------------------------------------------------
    bool showAreaFill = false;
    QColor areaFillColor; // invalid = derive (semi-transparent color)

    /**
     * @brief Serialize raw rendering fields for project/settings persistence.
     * @return Independent object using ARGB colour strings, stable enum tokens and QFont text. Empty legend/custom-label text and invalid optional colours are omitted; no range normalization occurs.
     */
    QJsonObject toJson() const;
    /**
     * @brief Read a style over the struct defaults, tolerating absent/ill-typed fields.
     * @param[in] obj Persisted object; unknown keys are ignored and recognized keys use Qt conversion defaults.
     * @return Independent style. Enum strings use documented fallbacks; numeric dimensions/opacity/precision and the integer format mode are not range-validated. Invalid optional colours retain their derived-colour behavior.
     */
    static SeriesStyle fromJson(const QJsonObject &obj);

    /**
     * @brief Resolve an optional rendering colour without changing stored fields.
     * @return Explicit valid marker fill or the main series colour.
     */
    QColor effectiveMarkerFillColor() const;
    /**
     * @brief Resolve an optional rendering colour without changing stored fields.
     * @return Explicit valid marker border or main colour darkened by QColor factor 140.
     */
    QColor effectiveMarkerBorderColor() const;
    /**
     * @brief Resolve an optional rendering colour without changing stored fields.
     * @return Explicit valid label colour or main colour darkened by QColor factor 160.
     */
    QColor effectivePointLabelColor() const;
    /**
     * @brief Resolve an optional rendering colour without changing stored fields.
     * @return Explicit valid area colour or main colour with alpha replaced by 0.25. No style.opacity multiplier is applied here.
     */
    QColor effectiveAreaFillColor() const; // alpha-baked fallback if invalid
};

/*! \brief Returns a default styled cycle position — the 12-color cycle
 *  used when new series are added without an explicit style. Wraps modulo 12.
 * @param[in] cycleIndex Any signed cycle position; negative positions wrap too.
 * @return Default-constructed style with only its colour replaced by the twelve-colour cycle selection.
 */
SeriesStyle defaultStyleForCycle(int cycleIndex);

/*! \brief String key for `MarkerShape` ↔ JSON.
 * @brief Encode a rendering enum as a stable lowercase JSON token.
 * @param[in] s MarkerShape value.
 * @return One of circle, square, triangle, diamond, cross, plus; unsupported values fall back to circle.
 */
QString markerShapeToString(MarkerShape s);
/**
 * @brief Decode an exact lowercase rendering token.
 * @param[in] s Persisted token; comparison is case-sensitive and does not trim whitespace.
 * @return Matching enum for circle, square, triangle, diamond, cross, plus, or Circle for unrecognized text.
 */
MarkerShape markerShapeFromString(const QString &s);

/*! \brief String key for `Qt::PenStyle` (Solid / Dash / Dot / DashDot / DashDotDot).
 * @brief Encode a rendering enum as a stable lowercase JSON token.
 * @param[in] s Qt::PenStyle value.
 * @return One of solid, dash, dot, dash-dot, dash-dot-dot; unsupported values fall back to solid.
 */
QString penStyleToString(Qt::PenStyle s);
/**
 * @brief Decode an exact lowercase rendering token.
 * @param[in] s Persisted token; comparison is case-sensitive and does not trim whitespace.
 * @return Matching enum for solid, dash, dot, dash-dot, dash-dot-dot, or SolidLine for unrecognized text.
 */
Qt::PenStyle penStyleFromString(const QString &s);

/*! \brief String keys for `Qt::PenCapStyle` and `Qt::PenJoinStyle` (JSON round-trip).
 * @brief Encode a rendering enum as a stable lowercase JSON token.
 * @param[in] s Qt::PenCapStyle value.
 * @return One of flat, square, round; unsupported values fall back to flat.
 */
QString penCapStyleToString(Qt::PenCapStyle s);
/**
 * @brief Decode an exact lowercase rendering token.
 * @param[in] s Persisted token; comparison is case-sensitive and does not trim whitespace.
 * @return Matching enum for flat, square, round, or FlatCap for unrecognized text.
 */
Qt::PenCapStyle penCapStyleFromString(const QString &s);
/**
 * @brief Encode a rendering enum as a stable lowercase JSON token.
 * @param[in] s Qt::PenJoinStyle value.
 * @return One of bevel, miter, round, svg-miter; unsupported values fall back to bevel.
 */
QString penJoinStyleToString(Qt::PenJoinStyle s);
/**
 * @brief Decode an exact lowercase rendering token.
 * @param[in] s Persisted token; comparison is case-sensitive and does not trim whitespace.
 * @return Matching enum for bevel, miter, round, svg-miter, or BevelJoin for unrecognized text.
 */
Qt::PenJoinStyle penJoinStyleFromString(const QString &s);

/*! \brief Build a QPen from the line-related fields of \a style.
 *  Width / cap / join / dash all wired up; colour with alpha from `opacity`.
 * @param[in] style Raw line style; expected valid nonnegative width and opacity in [0,1]. showLine does not affect this pen.
 * @return Pen copying dash/width/cap/join; valid main-colour alpha is multiplied by opacity only when opacity is below one. No series or global state is changed.
 */
QPen penForStyle(const SeriesStyle &style);

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
 *  represented by the scatter's existing shape when \a series is itself
 *  one — otherwise the line's built-in dot markers are toggled.
 * @param[in] style Style values expected within valid Qt ranges.
 * @param[in,out] series Optional existing target; null is a no-op. Object ownership and samples remain unchanged.
 * @param[in,out] markerOverlay Optional existing scatter overlay for a non-scatter target; its style/visibility/name change without sample synchronization or reparenting.
 * @note A scatter target receives size/fill/border and showMarkers visibility but does not receive the custom shape pixmap. Non-scatter overlays receive the rendered glyph. For a line without an overlay only built-in dot visibility is available. showLine/showMarkers jointly control a line target's visibility; no separate NoPen is installed when markers alone are enabled.
 * @note Point-label fields apply only while labels are enabled, and Qt receives NumberFormat::printfSpec(). The area brush does not create a QAreaSeries; the host must supply area rendering. Nonempty legend names replace names; empty names preserve the current names. Use on the GUI thread.
 */
void applySeriesStyle(const SeriesStyle &style, QXYSeries *series,
                      QScatterSeries *markerOverlay = nullptr);

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_SERIESSTYLE_H
