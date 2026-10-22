/**
 * @file      categorizedrenderer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Renderer that assigns a distinct symbol per unique value of an attribute.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    struct Category {
        QString value; /*!< The attribute value this category matches. */
        QString label; /*!< Legend label (defaults to `value`). */
        SymbolStyle symbol;
    };

    /** @brief Construct an empty category list, classification key and fallback symbol. */
    CategorizedRenderer() = default;
    /** @brief Destroy the renderer's stored value-type configuration. */
    ~CategorizedRenderer() override = default;

    /*!
     * \brief Name of the attribute key (in QVariantMap attrs) to look up.
     * @return Owned exact key text; an empty key still participates in lookup.
     */
    [[nodiscard]] QString classifyAttribute() const { return m_classifyAttribute; }
    /** @brief Replace the lookup key without normalization or notifications.
     * @param name Exact key moved into storage; empty and whitespace-only keys are accepted. */
    void setClassifyAttribute(QString name) { m_classifyAttribute = std::move(name); }

    /** @brief Inspect categories in matching and legend order.
     * @return Borrowed list valid for this renderer's lifetime; setters/loading can replace it. */
    [[nodiscard]] const QList<Category> &categories() const { return m_categories; }
    /** @brief Replace all categories without validation or notifications.
     * @param cats Ordered list moved into storage; duplicate/empty values are
     * retained and the first exact value match wins at paint time. */
    void setCategories(QList<Category> cats);
    /** @brief Append a category to matching/legend order.
     * @param c Category moved into storage; duplicate values are retained and can
     * be shadowed by earlier categories. No label fallback is stored into c.label. */
    void addCategory(Category c);

    /*!
     * \brief Symbol returned when a feature's attribute value does not match
     *        any category. Defaults to an empty SymbolStyle.
     * @return Borrowed fallback template, valid until replacement/loading/destruction.
     */
    [[nodiscard]] const SymbolStyle &fallbackSymbol() const { return m_fallback; }
    /** @brief Replace the unmatched-feature symbol without validation or notifications.
     * @param s Style moved into storage; an empty stack is legal. */
    void setFallbackSymbol(SymbolStyle s) { m_fallback = std::move(s); }

    // IFeatureRenderer.
    /** @brief Identify the persisted renderer type.
     * @return Exact token categorized. */
    [[nodiscard]] QString rendererId() const override { return QStringLiteral("categorized"); }
    /** @brief Select the first category whose text equals the converted attribute.
     * @param f Feature identity, unused by this renderer.
     * @param attrs Attribute map looked up at the exact classifyAttribute() key.
     * QVariant::toString() supplies the comparison text; absent/unconvertible
     * values can therefore match an empty-valued category.
     * @return Owned first matching symbol, otherwise the fallback. Comparison is
     * case-sensitive and does not trim or normalize either side. */
    [[nodiscard]] SymbolStyle symbolFor(const FeatureRef &f,
                                        const QVariantMap &attrs) const override;
    /** @brief Build one legend row per stored category, including shadowed duplicates.
     * @return Owned rows in list order, using nonempty label or category value,
     * numeric index keys, and stored symbols. No fallback row is added. */
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Serialize the exact lookup key, ordered categories and fallback symbol.
     * @return Owned object with id categorized; empty category labels are omitted. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Replace all renderer configuration from JSON without validation.
     * @param j Object whose id is ignored. Missing categories clear the list;
     * nonobject entries become empty-valued/default-symbol categories. Missing
     * key/fallback become empty, and duplicate categories are retained. */
    void fromJson(const QJsonObject &j) override;
    /** @brief Copy all category configuration into an independent renderer.
     * @return Sole ownership of the new renderer snapshot. */
    [[nodiscard]] std::unique_ptr<IFeatureRenderer> clone() const override;

    // ── Per-class editing (Slice BB Phase 8.6.16) ──────────────────────
    // classKey is the category index as a string ("0", "1", …). The
    // per-class color edit writes the colour into every layer of
    // m_categories[idx].symbol that already has a "color" slot. Symbol
    // swap replaces the whole SymbolStyle on that category. Size / width
    // are not editable here (use setSymbolForClass for full control).
    /** @brief Advertise color and whole-symbol editing for category rows.
     * @param kind Requested edit capability.
     * @return True only for Color or Symbol; Size, Width and unknown values return false. */
    [[nodiscard]] bool supportsClassEdit(ClassEditKind kind) const override
    {
        return kind == ClassEditKind::Color || kind == ClassEditKind::Symbol;
    }
    /** @brief Read the first usable color in a category symbol.
     * @param classKey Category index parsed with QString::toInt(); it must be in range.
     * @return Color per SymbolProps::firstColor(), or invalid for a bad key/absent color. */
    [[nodiscard]] QColor colorForClass(const QString &classKey) const override;
    /** @brief Recolor existing fillColor/color slots of one category.
     * @param classKey Category index parsed with QString::toInt(); bad/out-of-range keys are no-ops.
     * @param color Valid color to copy; invalid input is ignored. No notification
     * is emitted, and outline-only slots and data-defined expressions are retained. */
    void setColorForClass(const QString &classKey, const QColor &color) override;
    /** @brief Replace a category's complete symbol without changing its value or label.
     * @param classKey Category index parsed with QString::toInt(); bad/out-of-range keys are no-ops.
     * @param style Symbol copied without validation or notifications. */
    void setSymbolForClass(const QString &classKey, const SymbolStyle &style) override;

private:
    QString m_classifyAttribute;
    QList<Category> m_categories;
    SymbolStyle m_fallback;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_CATEGORIZEDRENDERER_H
