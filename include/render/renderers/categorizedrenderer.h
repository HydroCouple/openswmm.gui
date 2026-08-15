/*!
 * \file   categorizedrenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Renderer that assigns a distinct symbol per unique value of an attribute.
 *
 *         Sub-phase 8.13.6.2 STUB: this v1 stores the categories + per-category
 *         symbols + a fallback symbol, JSON-round-trips them faithfully, and
 *         returns the matching symbol for a given value (or fallback when
 *         missing). The full Map Symbology Dialog editor for adding /
 *         renaming / colouring categories is deferred to Slice **BI.3**.
 *
 *         The stub is intentionally functional rather than a no-op: existing
 *         tests in the renderer test file can confirm full round-trip + paint
 *         behaviour, so when BI.3 lands its editor it inherits a working
 *         engine instead of having to build one from scratch.
 *
 *         Cross-slice: Slice BI Phase 8.13.6 (see GUI_IMPLEMENTATION_PLAN.md
 *         §J.2). Sub-phase 8.13.6.2 — stub.
 */

#ifndef OPENSWMM_RENDER_CATEGORIZEDRENDERER_H
#define OPENSWMM_RENDER_CATEGORIZEDRENDERER_H

#include "render/ifeaturerenderer.h"

#include <QHash>
#include <QString>

namespace OpenSWMM::Render
{

/*!
 * \class CategorizedRenderer
 * \brief Distinct symbol per unique value of a string / enum attribute.
 *
 *        v1 storage / JSON / paint — UI editor in Slice BI.3.
 */
class CategorizedRenderer final : public IFeatureRenderer
{
public:
    /*!
     * \struct Category
     * \brief One named category and its symbol.
     */
    struct Category
    {
        QString     value;   /*!< The attribute value this category matches. */
        QString     label;   /*!< Legend label (defaults to `value`). */
        SymbolStyle symbol;
    };

    CategorizedRenderer() = default;
    ~CategorizedRenderer() override = default;

    /*!
     * \brief Name of the attribute key (in QVariantMap attrs) to look up.
     */
    [[nodiscard]] QString classifyAttribute() const { return m_classifyAttribute; }
    void setClassifyAttribute(QString name) { m_classifyAttribute = std::move(name); }

    [[nodiscard]] const QList<Category> &categories() const { return m_categories; }
    void setCategories(QList<Category> cats);
    void addCategory(Category c);

    /*!
     * \brief Symbol returned when a feature's attribute value does not match
     *        any category. Defaults to an empty SymbolStyle.
     */
    [[nodiscard]] const SymbolStyle &fallbackSymbol() const { return m_fallback; }
    void setFallbackSymbol(SymbolStyle s) { m_fallback = std::move(s); }

    // IFeatureRenderer.
    [[nodiscard]] QString rendererId() const override { return QStringLiteral("categorized"); }
    [[nodiscard]] SymbolStyle symbolFor(const FeatureRef &f,
                                        const QVariantMap &attrs) const override;
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;
    [[nodiscard]] std::unique_ptr<IFeatureRenderer> clone() const override;

    // ── Per-class editing (Slice BB Phase 8.6.16) ──────────────────────
    // classKey is the category index as a string ("0", "1", …). The
    // per-class color edit writes the colour into every layer of
    // m_categories[idx].symbol that already has a "color" slot. Symbol
    // swap replaces the whole SymbolStyle on that category. Size / width
    // are not editable here (use setSymbolForClass for full control).
    [[nodiscard]] bool supportsClassEdit(ClassEditKind kind) const override
    {
        return kind == ClassEditKind::Color || kind == ClassEditKind::Symbol;
    }
    [[nodiscard]] QColor colorForClass(const QString &classKey) const override;
    void setColorForClass(const QString &classKey, const QColor &color) override;
    void setSymbolForClass(const QString &classKey, const SymbolStyle &style) override;

private:
    QString          m_classifyAttribute;
    QList<Category>  m_categories;
    SymbolStyle      m_fallback;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_CATEGORIZEDRENDERER_H
