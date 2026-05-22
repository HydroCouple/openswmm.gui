/*!
 * \file   graduatedrenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Renderer that classifies a numeric attribute into bins, each with its own colour.
 *
 *         GraduatedRenderer is what drives the per-attribute heatmap on
 *         SWMMResultsLayer (depth / head / flow) and SWMM2DResultsLayer
 *         (depth). It carries:
 *           - classifyAttribute  — which attribute drives binning
 *           - minValue / maxValue — the value range covered by the bins
 *           - binColors          — N QColors, one per bin
 *           - baseSymbol         — the SymbolStyle template; the renderer
 *                                  overrides "color" in every SymbolLayer
 *                                  with the per-bin colour at paint time.
 *
 *         v1 uses equal-interval binning (linear) only. Slice BB Phase
 *         8.6.2 will swap binColors[] for a proper ColorRamp + IntervalBinner
 *         (Quantile / StdDev / Jenks / Log binning). The public symbolFor /
 *         legendSymbolItems contract does not change at that swap.
 *
 *         Cross-slice: Slice BI Phase 8.13.6 (see GUI_IMPLEMENTATION_PLAN.md
 *         §J.2 + §J.4). Sub-phase 8.13.6.2 — concrete renderer class.
 */

#ifndef OPENSWMM_RENDER_GRADUATEDRENDERER_H
#define OPENSWMM_RENDER_GRADUATEDRENDERER_H

#include "render/ifeaturerenderer.h"

#include <QColor>
#include <QList>
#include <QString>
#include <QVector>

namespace OpenSWMM::Render
{

/*!
 * \class GraduatedRenderer
 * \brief Classifies a numeric attribute into bins and assigns a colour per bin.
 */
class GraduatedRenderer final : public IFeatureRenderer
{
public:
    GraduatedRenderer() = default;
    ~GraduatedRenderer() override = default;

    /*!
     * \brief Name of the attribute key (in QVariantMap attrs) to classify on.
     *        Typical SWMM values: "depth", "head", "flow", "runoff".
     */
    [[nodiscard]] QString classifyAttribute() const { return m_classifyAttribute; }
    void setClassifyAttribute(QString name) { m_classifyAttribute = std::move(name); }

    /*!
     * \brief The numeric range covered by the bins.
     *        Values below minValue collapse into bin 0;
     *        values above maxValue collapse into bin (N−1).
     */
    [[nodiscard]] double minValue() const { return m_minValue; }
    [[nodiscard]] double maxValue() const { return m_maxValue; }
    void setRange(double minValue, double maxValue);

    /*!
     * \brief Per-bin colours. binCount() == binColors().size().
     */
    [[nodiscard]] const QList<QColor> &binColors() const { return m_binColors; }
    void setBinColors(QList<QColor> colors);
    [[nodiscard]] int binCount() const { return m_binColors.size(); }

    /*!
     * \brief The SymbolStyle template. At paint time the renderer overrides
     *        the "color" property of every SymbolLayer in this template with
     *        the appropriate per-bin colour.
     */
    [[nodiscard]] const SymbolStyle &baseSymbol() const { return m_baseSymbol; }
    void setBaseSymbol(SymbolStyle s);

    /*!
     * \brief Computes min/max from a sample of values and sets them.
     *        NaNs and infinities are skipped. If the sample is empty or
     *        contains no finite values, the existing range is left unchanged.
     */
    void autoClassify(const QVector<double> &samples);

    /*!
     * \brief Returns the colour for one raw attribute value.
     *        Clamps to [bin 0, bin N−1]. Returns Qt::transparent when bin
     *        count is zero.
     */
    [[nodiscard]] QColor colorForValue(double v) const;

    // IFeatureRenderer.
    [[nodiscard]] QString rendererId() const override { return QStringLiteral("graduated"); }
    [[nodiscard]] SymbolStyle symbolFor(const FeatureRef &f,
                                        const QVariantMap &attrs) const override;
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;
    [[nodiscard]] std::unique_ptr<IFeatureRenderer> clone() const override;

private:
    QString        m_classifyAttribute;
    double         m_minValue = 0.0;
    double         m_maxValue = 1.0;
    QList<QColor>  m_binColors;
    SymbolStyle    m_baseSymbol;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_GRADUATEDRENDERER_H
