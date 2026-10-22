/**
 * @file      singlesymbolrenderer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Renderer that paints every feature with the same SymbolStyle.
 *
 *         SingleSymbolRenderer is the simplest IFeatureRenderer: it carries
 *         one SymbolStyle and returns it for every feature. It is the
 *         drop-in replacement for today's hardcoded per-kind defaults on
 *         SWMMModelLayer (and the single GISVectorSymbol on GISVectorLayer).
 *
 *         Cross-slice: Slice BI Phase 8.13.6 (see GUI_IMPLEMENTATION_PLAN.md
 *         §J.2). Sub-phase 8.13.6.2 — concrete renderer class.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    /** @brief Construct an empty symbol stack, empty label and inactive size mapping. */
    SingleSymbolRenderer() = default;
    /** @brief Take a symbol snapshot and legend label by value.
     * @param symbol Ordered style stack moved into this renderer without validation.
     * @param label Legend text moved verbatim; empty is legal. */
    explicit SingleSymbolRenderer(SymbolStyle symbol, QString label = {});
    /** @brief Destroy the stored value-type style and mapping. */
    ~SingleSymbolRenderer() override = default;

    /*!
     * \brief The single style applied to every feature.
     * @return Borrowed template, valid for this renderer's lifetime; setters can replace it.
     */
    [[nodiscard]] const SymbolStyle &symbol() const { return m_symbol; }
    /** @brief Replace the style template without changing its label or size mapping.
     * @param s Style moved into storage without validation or repaint notification. */
    void setSymbol(SymbolStyle s);

    /*!
     * \brief Label drawn next to the swatch in the legend (default "").
     * @return Borrowed text; later label loading/replacement can change it.
     */
    [[nodiscard]] const QString &legendLabel() const { return m_legendLabel; }
    /** @brief Replace the legend text without trimming or emitting notifications.
     * @param label Text moved verbatim, including empty or whitespace-only strings. */
    void setLegendLabel(QString label) { m_legendLabel = std::move(label); }

    /*!
     * \brief Slice BI Phase 8.13.43-α — data-defined size/width override.
     *        When set, `symbolFor` evaluates the override against each
     *        feature's attribute value and writes the resolved scalar
     *        into the symbol's `props["size"]` (markers) or `props["width"]`
     *        (lines) before returning the style. When empty (default),
     *        the static size in `m_symbol` is used.
     * @return Borrowed mapping, valid for this renderer's lifetime; no evaluation occurs here.
     */
    [[nodiscard]] const DataDefinedScalar &sizeData() const { return m_sizeData; }
    /** @brief Replace the optional per-feature size/width mapping.
     * @param d Mapping moved without validation; symbolFor() applies it only when
     * DataDefinedScalar::isValid() succeeds. No repaint notification is emitted. */
    void setSizeData(DataDefinedScalar d) { m_sizeData = std::move(d); }

    // IFeatureRenderer.
    /** @brief Identify the persisted renderer type.
     * @return Exact token single. */
    [[nodiscard]] QString rendererId() const override { return QStringLiteral("single"); }
    /** @brief Copy the template and optionally apply a numeric size/width mapping.
     * @param f Feature identity, unused by this renderer.
     * @param attrs Attribute map read at the mapping's exact key. Failed numeric
     * conversion uses outMin; nonfinite numeric input follows evaluate().
     * @return Owned style with existing size/width properties replaced when the
     * mapping is valid. Other properties and the stored template remain unchanged. */
    [[nodiscard]] SymbolStyle symbolFor(const FeatureRef &f,
                                        const QVariantMap &attrs) const override;
    /** @brief Build the single static legend row.
     * @return One row with the stored label/template, key single, and NaN range.
     * Per-feature size mapping is not evaluated for the legend. */
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Serialize renderer identity, style, optional label and valid size mapping.
     * @return Owned configuration object; an invalid mapping is omitted. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Replace all configuration from JSON without emitting notifications.
     * @param j Object for this renderer. The id field is ignored; missing symbol
     * becomes an empty stack, missing label empty, and missing sizeData an inactive mapping. */
    void fromJson(const QJsonObject &j) override;
    /** @brief Copy this renderer's entire configuration into an independent value.
     * @return Sole ownership of a new SingleSymbolRenderer with no shared mutable configuration. */
    [[nodiscard]] std::unique_ptr<IFeatureRenderer> clone() const override;

    // ── Per-class editing (Slice BB Phase 8.6.16) ──────────────────────
    // Only one class ("single"). All four kinds mutate the stored m_symbol
    // directly — there is no override hash because there is only one row.
    /** @brief Advertise direct editing of the one stored symbol.
     * The ClassEditKind argument is ignored; every kind returns true, including
     * unknown enum values. @return True unconditionally. */
    [[nodiscard]] bool supportsClassEdit(ClassEditKind /*kind*/) const override { return true; }
    /** @brief Inspect the stored symbol's first usable color.
     * @param classKey Exact single key; any other text is rejected.
     * @return First color per SymbolProps::firstColor(), or invalid when absent/key mismatch. */
    [[nodiscard]] QColor colorForClass(const QString &classKey) const override;
    /** @brief Read the stored marker size, falling back to a line width.
     * @param classKey Exact single key; any other text returns -1.
     * @return First size property across the stack, otherwise first width, converted
     * through QVariant::toReal(); -1 if neither exists. Units are logical pixels. */
    [[nodiscard]] qreal sizeForClass(const QString &classKey) const override;
    /** @brief Replace existing fillColor/color slots in the stored symbol.
     * @param classKey Exact single key; mismatches are no-ops.
     * @param color Valid color to copy; invalid input is ignored. Outline-only
     * layers and data-defined expressions remain unchanged. */
    void setColorForClass(const QString &classKey, const QColor &color) override;
    /** @brief Replace existing marker-size slots without inserting missing slots.
     * @param classKey Exact single key; mismatches are no-ops.
     * @param size Logical-pixel value stored without finiteness or nonnegative checks.
     * Width slots and the per-feature mapping are retained; no notification is emitted. */
    void setSizeForClass(const QString &classKey, qreal size) override;
    /** @brief Replace existing line-width slots without inserting missing slots.
     * @param classKey Exact single key; mismatches are no-ops.
     * @param width Logical-pixel width stored without range or finiteness validation. */
    void setWidthForClass(const QString &classKey, qreal width) override;
    /** @brief Replace the complete stored template for the one editable class.
     * @param classKey Exact single key; mismatches are no-ops.
     * @param style Value copied without validation; label and size mapping are retained. */
    void setSymbolForClass(const QString &classKey, const SymbolStyle &style) override;

private:
    SymbolStyle m_symbol;
    QString m_legendLabel;
    DataDefinedScalar m_sizeData; /*!< Slice BI Phase 8.13.43-α — opt-in. */
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SINGLESYMBOLRENDERER_H
