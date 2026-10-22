/**
 * @file      diagramspec.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Typed config for the Layer Properties → Diagrams tab (Slice Z.12).
 *
 *         RENDERING_RULE_MODEL_PLAN.md §11.1 — embedded pie / bar /
 *         time-series / histogram charts attached to each feature.
 *         SWMM use cases:
 *           - Junction with embedded depth-vs-time micro-chart that
 *             animates with the timeline
 *           - Storage unit with pie of inflow-source fractions
 *           - Subcatchment with bar of runoff vs infiltration
 *
 *         For static charts (Pie / Bar / Histogram), `attributes` lists
 *         the field names that drive the bars / wedges. For time-series
 *         charts, `seriesExpression` evaluates per-feature to a vector
 *         that the chart consumes per animation tick.
 *
 *         Slice Z.12-data ships the value type + JSON round-trip. The
 *         tab UI, the per-feature chart painter, and the seriesExpression
 *         evaluator integration are separate slices.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_DIAGRAMSPEC_H
#define OPENSWMM_RENDER_DIAGRAMSPEC_H

#include <QColor>
#include <QJsonObject>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace OpenSWMM::Render
{

/*!
 * \enum DiagramType
 * \brief Chart kind painted per feature.
 */
enum class DiagramType : int {
    Pie = 0,
    Bar = 1,
    TimeSeries = 2,
    Histogram = 3,
};

/** @brief Encode the chart type as a stable JSON token.
 * @param t Type to encode; an unknown enum value selects pie.
 * @return pie, bar, timeSeries, or histogram. */
[[nodiscard]] QString diagramTypeToString(DiagramType t);
/** @brief Decode the chart token with exact case-sensitive matching.
 * @param s Token text, without trimming or case folding.
 * @return Recognized chart type, or Pie for empty/unknown text. */
[[nodiscard]] DiagramType diagramTypeFromString(const QString &s);

/*!
 * \struct DiagramSpec
 * \brief Per-layer diagram configuration.
 */
struct DiagramSpec {
    /*! \brief Master switch. When false, no chart is painted. */
    bool enabled = false;

    /*! \brief Chart kind. */
    DiagramType type = DiagramType::Pie;

    /*! \brief For static charts (Pie / Bar / Histogram): which attribute
     *         names drive the bars / wedges. For TimeSeries: ignored
     *         (use \ref seriesExpression). */
    QStringList attributes;

    /*! \brief For TimeSeries: an expression that evaluates per feature
     *         to a vector of doubles. Empty for static chart types. */
    QString seriesExpression;

    /*! \brief Chart bounding-box size in pixels. */
    QSizeF sizePx = QSizeF(40.0, 40.0);

    /*! \brief Offset from the feature's anchor point in pixels. */
    QPointF offsetPx = QPointF(0.0, 0.0);

    /*! \brief Color palette for wedges / bars. Empty → use the layer's
     *         categorical palette default. */
    QVector<QColor> palette;

    /*! \brief Value range for bars / histogram (min, max). Both zero =
     *         auto-fit to the data. */
    qreal rangeMin = 0.0;
    qreal rangeMax = 0.0;

    /** @brief Serialize nondefault chart settings without evaluating feature data.
     * @return Owned object preserving ordered attribute/palette lists, pixel size
     * and offsets, and data-unit bounds. Colors use ARGB text; no size/range validation occurs. */
    [[nodiscard]] QJsonObject toJson() const;
    /** @brief Load chart settings with defaults for absent or mistyped fields.
     * @param j Chart JSON; unknown type tokens select Pie.
     * @return Owned settings without expression, size, range or color validation.
     * Nonstring attributes become empty entries; invalid palette entries remain
     * invalid QColors. Size defaults to 40 by 40 pixels; offset and bounds default to zero. */
    static DiagramSpec fromJson(const QJsonObject &j);

    /** @brief Compare chart configuration for editor change detection.
     * @param other Settings to compare without modification.
     * @return True when text, lists, colors, size and offset match exactly and
     * each range bound matches via qFuzzyCompare after adding one. */
    [[nodiscard]] bool operator==(const DiagramSpec &other) const;
    /** @brief Test whether chart configurations differ.
     * @param other Settings to compare without modification.
     * @return Negation of operator==(), including its fuzzy bound comparisons. */
    [[nodiscard]] bool operator!=(const DiagramSpec &other) const { return !(*this == other); }
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_DIAGRAMSPEC_H
