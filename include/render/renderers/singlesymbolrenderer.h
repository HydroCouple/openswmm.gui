/*!
 * \file   singlesymbolrenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Renderer that paints every feature with the same SymbolStyle.
 *
 *         SingleSymbolRenderer is the simplest IFeatureRenderer: it carries
 *         one SymbolStyle and returns it for every feature. It is the
 *         drop-in replacement for today's hardcoded per-kind defaults on
 *         SWMMModelLayer (and the single GISVectorSymbol on GISVectorLayer).
 *
 *         Cross-slice: Slice BI Phase 8.13.6 (see GUI_IMPLEMENTATION_PLAN.md
 *         §J.2). Sub-phase 8.13.6.2 — concrete renderer class.
 */

#ifndef OPENSWMM_RENDER_SINGLESYMBOLRENDERER_H
#define OPENSWMM_RENDER_SINGLESYMBOLRENDERER_H

#include "render/datadefined.h"
#include "render/ifeaturerenderer.h"

namespace OpenSWMM::Render
{

/*!
 * \class SingleSymbolRenderer
 * \brief One SymbolStyle for the whole layer.
 */
class SingleSymbolRenderer final : public IFeatureRenderer
{
public:
    SingleSymbolRenderer() = default;
    explicit SingleSymbolRenderer(SymbolStyle symbol, QString label = {});
    ~SingleSymbolRenderer() override = default;

    /*!
     * \brief The single style applied to every feature.
     */
    [[nodiscard]] const SymbolStyle &symbol() const { return m_symbol; }
    void setSymbol(SymbolStyle s);

    /*!
     * \brief Label drawn next to the swatch in the legend (default "").
     */
    [[nodiscard]] const QString &legendLabel() const { return m_legendLabel; }
    void setLegendLabel(QString label) { m_legendLabel = std::move(label); }

    /*!
     * \brief Slice BI Phase 8.13.43-α — data-defined size/width override.
     *        When set, `symbolFor` evaluates the override against each
     *        feature's attribute value and writes the resolved scalar
     *        into the symbol's `props["size"]` (markers) or `props["width"]`
     *        (lines) before returning the style. When empty (default),
     *        the static size in `m_symbol` is used.
     */
    [[nodiscard]] const DataDefinedScalar &sizeData() const { return m_sizeData; }
    void setSizeData(DataDefinedScalar d) { m_sizeData = std::move(d); }

    // IFeatureRenderer.
    [[nodiscard]] QString rendererId() const override { return QStringLiteral("single"); }
    [[nodiscard]] SymbolStyle symbolFor(const FeatureRef &f,
                                        const QVariantMap &attrs) const override;
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;
    [[nodiscard]] std::unique_ptr<IFeatureRenderer> clone() const override;

    // ── Per-class editing (Slice BB Phase 8.6.16) ──────────────────────
    // Only one class ("single"). All four kinds mutate the stored m_symbol
    // directly — there is no override hash because there is only one row.
    [[nodiscard]] bool supportsClassEdit(ClassEditKind /*kind*/) const override { return true; }
    [[nodiscard]] QColor colorForClass(const QString &classKey) const override;
    void setColorForClass(const QString &classKey, const QColor &color) override;
    void setSizeForClass(const QString &classKey, qreal size) override;
    void setWidthForClass(const QString &classKey, qreal width) override;
    void setSymbolForClass(const QString &classKey, const SymbolStyle &style) override;

private:
    SymbolStyle       m_symbol;
    QString           m_legendLabel;
    DataDefinedScalar m_sizeData;   /*!< Slice BI Phase 8.13.43-α — opt-in. */
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SINGLESYMBOLRENDERER_H
