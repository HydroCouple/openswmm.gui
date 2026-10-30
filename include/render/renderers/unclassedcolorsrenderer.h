/**
 * @file      unclassedcolorsrenderer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Continuous-color renderer — no bins (Slice Z.9).
 *
 *         UnclassedColorsRenderer is the per-feature sibling of
 *         GraduatedRenderer that skips the IntervalBinner entirely:
 *         instead of bucketing the attribute value into N classes, it
 *         normalises the value linearly into [0, 1] over the ramp's
 *         (minValue, maxValue) and samples the ramp directly.
 *
 *         Use case: 2D depth fields, continuous flow rates, anywhere the
 *         user wants smooth colour transitions instead of stepped bins.
 *         Equivalent to QGIS / ArcGIS Pro's "Unclassed Colors" /
 *         "Continuous" symbology.
 *
 *         Below-min values are hidden; above-max values use the ramp's
 *         end color. NaN / invalid attributes use \ref noDataColor.
 *         Legacy sentinel colors remain serialized for project round trips.
 *
 *         Cross-slice: RENDERING_RULE_MODEL_PLAN.md §5 Renderer roster
 *         (Unclassed Colors entry) + §16 Slice Z.9.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_UNCLASSEDCOLORSRENDERER_H
#define OPENSWMM_RENDER_UNCLASSEDCOLORSRENDERER_H

#include "render/colorramp.h"
#include "render/ifeaturerenderer.h"

#include <QColor>
#include <QString>

namespace OpenSWMM::Render
{

/*!
 * \class UnclassedColorsRenderer
 * \brief Maps a continuous numeric attribute to a continuous colour ramp,
 *        no bin discretisation.
 */
class UnclassedColorsRenderer final : public IFeatureRenderer
{
public:
    /** @brief Construct an empty style with a viridis [0,1] ramp and five legend labels. */
    UnclassedColorsRenderer() = default;
    /** @brief Destroy the stored configuration values. */
    ~UnclassedColorsRenderer() override = default;

    /** @brief Read the exact numeric classification key.
     * @return Owned key text; empty disables per-feature recoloring. */
    [[nodiscard]] QString classifyAttribute() const { return m_classifyAttribute; }
    /** @brief Replace the classification key without trimming or notifications.
     * @param name Key moved verbatim; empty causes symbolFor() to return the template unchanged. */
    void setClassifyAttribute(QString name) { m_classifyAttribute = std::move(name); }

    /** @brief Inspect the continuous ramp and its data-unit bounds.
     * @return Borrowed ramp valid for this renderer's lifetime; setters/loading can change it. */
    [[nodiscard]] const RasterColorRamp &ramp() const { return m_ramp; }
    /** @brief Replace the ramp, including its data range and interpolation mode.
     * @param ramp Payload moved without stop/range validation or notifications. */
    void setRamp(RasterColorRamp ramp);

    /** @brief Read the ramp's lower data bound.
     * @return Stored value in classification-attribute units. */
    [[nodiscard]] double minValue() const { return m_ramp.minValue; }
    /** @brief Read the ramp's upper data bound.
     * @return Stored value in the same units as minValue(). */
    [[nodiscard]] double maxValue() const { return m_ramp.maxValue; }
    /** @brief Replace only the ramp's data bounds without normalization.
     * @param minValue Lower bound in attribute units, expected finite.
     * @param maxValue Upper bound in the same units, expected finite and at least
     * minValue. Ordering/finiteness are not checked and no notification is emitted. */
    void setRange(double minValue, double maxValue);

    /*! The SymbolStyle template. At paint time the renderer overrides
     *  the "color" prop on every SymbolLayer with the ramp-sampled
     *  colour, as well as existing fillColor slots.
     * @return Borrowed template; no ramp sampling occurs on access. */
    [[nodiscard]] const SymbolStyle &baseSymbol() const { return m_baseSymbol; }
    /** @brief Replace the complete template without validation or notifications.
     * @param s Style moved into storage, retaining its opacity and property grammar. */
    void setBaseSymbol(SymbolStyle s) { m_baseSymbol = std::move(s); }

    /** @brief Read legacy underflow-color metadata.
     * @return Stored color; rendering hides numeric underflow independently of this value. */
    [[nodiscard]] QColor belowRangeColor() const { return m_belowRange; }
    /** @brief Retain an underflow color for persistence compatibility.
     * @param c Color copied verbatim, including invalid input; rendering does not sample it. */
    void setBelowRangeColor(QColor c) { m_belowRange = c; }

    /** @brief Read legacy overflow-color metadata.
     * @return Stored color; rendering saturates to the ramp endpoint independently. */
    [[nodiscard]] QColor aboveRangeColor() const { return m_aboveRange; }
    /** @brief Retain an overflow color for persistence compatibility.
     * @param c Color copied verbatim, including invalid input; sampling ignores it. */
    void setAboveRangeColor(QColor c) { m_aboveRange = c; }

    /*! Colour for NaN / non-finite / missing-attribute values.
     *  Default: fully transparent.
     * @return Stored missing-data color, which can be invalid if explicitly assigned. */
    [[nodiscard]] QColor noDataColor() const { return m_noData; }
    /** @brief Replace the missing-data color without validation or notifications.
     * @param c Color to copy for missing, unconvertible or nonfinite attribute values. */
    void setNoDataColor(QColor c) { m_noData = c; }

    /** @brief Read the number of evenly spaced legend reference swatches.
     * @return Stored count, initially five and clamped to at least two by setters/loading.
     * No additional gradient header row is generated. */
    [[nodiscard]] int legendLabelCount() const { return m_legendLabelCount; }
    /** @brief Set the number of legend reference swatches.
     * @param n Count clamped to at least two with no upper bound or notification. */
    void setLegendLabelCount(int n) { m_legendLabelCount = (n < 2) ? 2 : n; }

    /*! Map one raw attribute value to its rendered colour. Public for
     *  unit tests, the legend builder, and any caller that needs to
     *  preview the renderer without invoking the full symbolFor path.
     * @param v Scalar in the ramp bounds' units; bounds must be finite and ordered.
     * @return noDataColor() for nonfinite input, transparent below minimum, high
     * endpoint above maximum, otherwise interpolated color. A range whose shifted
     * endpoints compare equal via qFuzzyCompare samples the midpoint. No legacy
     * sentinel colors or feature opacity are applied. */
    [[nodiscard]] QColor colorForValue(double v) const;

    // IFeatureRenderer.
    /** @brief Identify the persisted renderer type.
     * @return Exact token unclassed. */
    [[nodiscard]] QString rendererId() const override { return QStringLiteral("unclassed"); }
    /** @brief Copy the template and recolor its existing fillColor/color slots.
     * @param f Feature identity, unused by this renderer.
     * @param attrs Exact-key attribute values; missing/unconvertible values use noDataColor().
     * @return Owned style; empty classification key leaves it unchanged. Finite
     * underflow additionally forces total opacity to zero, while other missing
     * data changes color slots only. Outline-only layers remain unrecolored. */
    [[nodiscard]] SymbolStyle symbolFor(const FeatureRef &f,
                                        const QVariantMap &attrs) const override;
    /** @brief Sample a finite ordered ramp range for legend reference rows.
     * @return legendLabelCount() rows including both endpoints, with four-significant-
     * digit labels, equal low/high bounds, and numeric index keys. Each row copies
     * the base symbol and recolors its existing slots; class editing remains unsupported. */
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Serialize the renderer, ramp, template and optional legacy metadata.
     * @return Owned JSON snapshot; default transparent no-data and five-label settings are omitted. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Load configuration without validating the renderer discriminator.
     * @param j Object to load. Missing ramp/baseSymbol preserve current values;
     * other fields reset to their documented defaults. Label count is clamped to
     * at least two, but color strings, range bounds and stops receive no extra validation. */
    void fromJson(const QJsonObject &j) override;
    /** @brief Copy the complete continuous-renderer configuration.
     * @return Sole ownership of an independently editable renderer value. */
    [[nodiscard]] std::unique_ptr<IFeatureRenderer> clone() const override;

private:
    QString m_classifyAttribute;
    RasterColorRamp m_ramp = RasterColorRamp::viridis(0.0, 1.0);
    SymbolStyle m_baseSymbol;
    QColor m_belowRange; /*!< Legacy metadata; underflow is hidden. */
    QColor m_aboveRange; /*!< Legacy metadata; overflow saturates.   */
    QColor m_noData = QColor(0, 0, 0, 0);
    int m_legendLabelCount = 5;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_UNCLASSEDCOLORSRENDERER_H
