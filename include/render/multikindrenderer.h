/**
 * @file      multikindrenderer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BI-MK.1 Phase 8.13.40 — per-kind renderer dispatch.
 *
 *         MultiKindRenderer is the `IFeatureRenderer` adapter that lets a
 *         multi-kind layer (`SWMMModelLayer` with 11 object categories,
 *         `SWMMResultsLayer` with 3 attribute scopes) carry one inner
 *         renderer per kind. It dispatches `symbolFor()` based on the
 *         feature's `FeatureRef::categoryHint` string, falling back to a
 *         shared default when no per-kind renderer is registered.
 *
 *         Design choice: kind keys are STRINGS (matching FeatureRef's
 *         categoryHint contract) rather than ordinals. This keeps the
 *         renderer ignorant of SWMM-specific enums — the layer is free to
 *         use any naming scheme; the renderer just dispatches on the
 *         string. `.oswp` round-trip uses the same strings as JSON keys.
 *
 *         Legend aggregation: walks every kind's renderer's
 *         `legendSymbolItems()`, prepending the kind label as a category
 *         header so the legend dock can group visually.
 *
 *         Cross-slice: GUI_IMPLEMENTATION_PLAN.md §J.10 (`MultiKindRenderer`
 *         adapter) + §N.14 (per-kind UI). Phase 8.13.40 closes the prereq
 *         that BI-MK.1.41 (SymbologyDialog left pane) builds on.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_MULTIKINDRENDERER_H
#define OPENSWMM_RENDER_MULTIKINDRENDERER_H

#include "render/ifeaturerenderer.h"

#include <QHash>
#include <QString>

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace OpenSWMM::Render::detail
{
/*!
 * \brief std-style hash for QString so unordered_map can key on it without
 *        round-tripping through std::string. Delegates to Qt's qHash.
 */
struct QStringHash {
    /** @brief Hash a QString directly for unordered renderer lookup.
     * @param s Exact case-sensitive key, inspected without conversion.
     * @return Qt hash converted to std::size_t, using qHash's default seed. */
    std::size_t operator()(const QString &s) const noexcept
    {
        return static_cast<std::size_t>(qHash(s));
    }
};
} // namespace OpenSWMM::Render::detail

namespace OpenSWMM::Render
{

/*!
 * \class MultiKindRenderer
 * \brief Per-kind IFeatureRenderer dispatch for multi-kind layers.
 */
class MultiKindRenderer final : public IFeatureRenderer
{
public:
    /** @brief Construct no per-kind entries and an owned empty SingleSymbolRenderer fallback. */
    MultiKindRenderer();
    /** @brief Destroy every owned inner renderer and the fallback. */
    ~MultiKindRenderer() override = default;

    /*!
     * \brief Returns the renderer registered for \p kind, or `nullptr` if
     *        no per-kind renderer exists for that key (caller should fall
     *        back to \ref fallback()).
     * @param kind Exact case-sensitive category key; no trimming occurs.
     * @return Borrowed renderer pointer or nullptr, valid until that entry is
     * cleared/replaced, all configuration is loaded, or this adapter is destroyed.
     */
    [[nodiscard]] const IFeatureRenderer *rendererFor(const QString &kind) const;
    /** @brief Access a mutable registered renderer without selecting the fallback.
     * @param kind Exact category key to look up.
     * @return Borrowed pointer or nullptr, with the const overload's lifetime.
     * Mutations through this pointer require caller-managed repaint notification. */
    [[nodiscard]] IFeatureRenderer *rendererFor(const QString &kind);

    /*!
     * \brief Registers \p renderer as the per-kind renderer for \p kind,
     *        replacing any prior entry. Null \p renderer removes the entry.
     * @param kind Nonempty exact key; empty keys are ignored without changing the map.
     * @param renderer Ownership transferred by value; replacement/removal destroys
     * the old entry. For an empty key, a supplied renderer is still consumed and
     * destroyed on return. No repaint notification is emitted.
     */
    void setRendererFor(const QString &kind, std::unique_ptr<IFeatureRenderer> renderer);

    /*!
     * \brief Removes the renderer for \p kind (no-op if not present).
     * @param kind Exact key to erase, destroying its renderer and invalidating borrowed pointers.
     */
    void clearRendererFor(const QString &kind);

    /*!
     * \brief Renderer used when no per-kind entry matches the feature's
     *        categoryHint. Defaults to a SingleSymbolRenderer with a
     *        plain SymbolStyle so symbolFor() never returns garbage.
     * @return Borrowed non-null fallback; replacement/loading/destruction can invalidate it.
     */
    [[nodiscard]] const IFeatureRenderer *fallback() const { return m_fallback.get(); }
    /** @brief Access the mutable fallback renderer without transferring ownership.
     * @return Borrowed non-null pointer, invalidated by replacement/loading/destruction. */
    [[nodiscard]] IFeatureRenderer *fallback() { return m_fallback.get(); }
    /** @brief Replace and own the fallback renderer when the new pointer is non-null.
     * @param r Ownership transferred by value; null retains the current fallback.
     * A successful replacement destroys the previous fallback without notification. */
    void setFallback(std::unique_ptr<IFeatureRenderer> r);

    /*!
     * \brief All registered kind keys in deterministic (sorted) order.
     *        Used by the legend pipeline and serialisation so output is
     *        reproducible across runs / saves.
     * @return Owned vector sorted with QString's ordinary comparison; excludes fallback.
     */
    [[nodiscard]] std::vector<QString> kinds() const;

    /** @brief Count registered per-kind renderers, excluding fallback.
     * @return Current entry count, assuming it fits int. */
    [[nodiscard]] int kindCount() const { return m_perKind.size(); }

    // IFeatureRenderer
    /** @brief Identify the persisted renderer type.
     * @return Exact token multikind. */
    [[nodiscard]] QString rendererId() const override { return QStringLiteral("multikind"); }
    /** @brief Dispatch symbol selection by the feature's exact category hint.
     * @param f Identity forwarded unchanged to a matching non-null renderer, or fallback.
     * @param attrs Attributes forwarded unchanged; matching/fallback renderer defines their semantics.
     * @return Owned delegated style; empty/missing hints use fallback. Exceptions
     * from an inner renderer propagate; this adapter performs no extra validation. */
    [[nodiscard]] SymbolStyle symbolFor(const FeatureRef &f,
                                        const QVariantMap &attrs) const override;
    /** @brief Aggregate registered kinds' legend rows in sorted kind order.
     * @return Owned rows with userLabel prefixed by kind / effective inner label,
     * or just kind for empty labels. No header/fallback row is added. Inner class
     * keys and other fields are preserved; per-class edits are not dispatched by this adapter. */
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Serialize every registered renderer and the fallback.
     * @return Owned multikind object with exact kind keys; each inner renderer
     * supplies its own JSON. Loading supports only the subset documented by fromJson(). */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Replace kind mappings using the local four-type renderer factory.
     * @param j Object whose own id is ignored. Recognized inner IDs are single,
     * graduated, categorized and rule; unknown/malformed entries are skipped.
     * Missing kinds clear the map. A missing/unrecognized fallback preserves the
     * current fallback. Unlike setRendererFor(), the loader accepts empty kind
     * keys, though symbolFor() does not dispatch empty hints. No notifications occur. */
    void fromJson(const QJsonObject &j) override;
    /** @brief Clone each inner renderer and the fallback into a new adapter.
     * @return Sole ownership of the independently editable deep configuration copy;
     * cloning uses inner clone() methods rather than the restricted JSON factory. */
    [[nodiscard]] std::unique_ptr<IFeatureRenderer> clone() const override;

private:
    // std::unordered_map (not QHash) because Qt 6 QHash requires copyable
    // values and unique_ptr's copy ctor is deleted. The custom QStringHash
    // adapter keeps QString as the key type so callers don't see std::string.
    std::unordered_map<QString, std::unique_ptr<IFeatureRenderer>, detail::QStringHash> m_perKind;
    std::unique_ptr<IFeatureRenderer> m_fallback;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_MULTIKINDRENDERER_H
