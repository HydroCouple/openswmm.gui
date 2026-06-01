/*!
 * \file   unclassedcolorsrenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Continuous-color renderer — no bins (Slice Z.9).
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
 *         Below-min values use \ref belowRangeColor (default = the ramp's
 *         start color); above-max values use \ref aboveRangeColor
 *         (default = the ramp's end color). NaN / invalid attribute
 *         values fall back to \ref noDataColor.
 *
 *         Cross-slice: RENDERING_RULE_MODEL_PLAN.md §5 Renderer roster
 *         (Unclassed Colors entry) + §16 Slice Z.9.
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
    UnclassedColorsRenderer() = default;
    ~UnclassedColorsRenderer() override = default;

    /*! Attribute key (in attrs map) the renderer reads at paint time. */
    [[nodiscard]] QString classifyAttribute() const { return m_classifyAttribute; }
    void setClassifyAttribute(QString name) { m_classifyAttribute = std::move(name); }

    /*! The colour ramp. Sampled at (v − minValue) / (maxValue − minValue). */
    [[nodiscard]] const RasterColorRamp &ramp() const { return m_ramp; }
    void setRamp(RasterColorRamp ramp);

    /*! Convenience accessors mirroring GraduatedRenderer's surface. */
    [[nodiscard]] double minValue() const { return m_ramp.minValue; }
    [[nodiscard]] double maxValue() const { return m_ramp.maxValue; }
    void setRange(double minValue, double maxValue);

    /*! The SymbolStyle template. At paint time the renderer overrides
     *  the "color" prop on every SymbolLayer with the ramp-sampled
     *  colour. */
    [[nodiscard]] const SymbolStyle &baseSymbol() const { return m_baseSymbol; }
    void setBaseSymbol(SymbolStyle s) { m_baseSymbol = std::move(s); }

    /*! Colour for values below minValue. Default: invalid (renderer
     *  samples the ramp at position 0 — the ramp's start colour). */
    [[nodiscard]] QColor belowRangeColor() const { return m_belowRange; }
    void setBelowRangeColor(QColor c) { m_belowRange = c; }

    /*! Colour for values above maxValue. */
    [[nodiscard]] QColor aboveRangeColor() const { return m_aboveRange; }
    void setAboveRangeColor(QColor c) { m_aboveRange = c; }

    /*! Colour for NaN / non-finite / missing-attribute values.
     *  Default: fully transparent. */
    [[nodiscard]] QColor noDataColor() const { return m_noData; }
    void setNoDataColor(QColor c) { m_noData = c; }

    /*! Number of swatches the legend emits (default 5). The renderer
     *  always shows a gradient header item plus N evenly-spaced value
     *  labels — the user reads the gradient continuously, the labels
     *  anchor reference points. */
    [[nodiscard]] int legendLabelCount() const { return m_legendLabelCount; }
    void setLegendLabelCount(int n) { m_legendLabelCount = (n < 2) ? 2 : n; }

    /*! Map one raw attribute value to its rendered colour. Public for
     *  unit tests, the legend builder, and any caller that needs to
     *  preview the renderer without invoking the full symbolFor path. */
    [[nodiscard]] QColor colorForValue(double v) const;

    // IFeatureRenderer.
    [[nodiscard]] QString rendererId() const override { return QStringLiteral("unclassed"); }
    [[nodiscard]] SymbolStyle symbolFor(const FeatureRef &f,
                                        const QVariantMap &attrs) const override;
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;
    [[nodiscard]] std::unique_ptr<IFeatureRenderer> clone() const override;

private:
    QString          m_classifyAttribute;
    RasterColorRamp  m_ramp = RasterColorRamp::viridis(0.0, 1.0);
    SymbolStyle      m_baseSymbol;
    QColor           m_belowRange;       /*!< Invalid by default → use ramp start. */
    QColor           m_aboveRange;       /*!< Invalid by default → use ramp end.   */
    QColor           m_noData = QColor(0, 0, 0, 0);
    int              m_legendLabelCount = 5;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_UNCLASSEDCOLORSRENDERER_H
