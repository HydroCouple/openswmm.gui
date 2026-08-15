/*!
 * \file   multikindrenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BI-MK.1 Phase 8.13.40 — per-kind renderer dispatch.
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
struct QStringHash
{
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
    MultiKindRenderer();
    ~MultiKindRenderer() override = default;

    /*!
     * \brief Returns the renderer registered for \p kind, or `nullptr` if
     *        no per-kind renderer exists for that key (caller should fall
     *        back to \ref fallback()).
     */
    [[nodiscard]] const IFeatureRenderer *rendererFor(const QString &kind) const;
    [[nodiscard]] IFeatureRenderer       *rendererFor(const QString &kind);

    /*!
     * \brief Registers \p renderer as the per-kind renderer for \p kind,
     *        replacing any prior entry. Null \p renderer removes the entry.
     */
    void setRendererFor(const QString &kind, std::unique_ptr<IFeatureRenderer> renderer);

    /*!
     * \brief Removes the renderer for \p kind (no-op if not present).
     */
    void clearRendererFor(const QString &kind);

    /*!
     * \brief Renderer used when no per-kind entry matches the feature's
     *        categoryHint. Defaults to a SingleSymbolRenderer with a
     *        plain SymbolStyle so symbolFor() never returns garbage.
     */
    [[nodiscard]] const IFeatureRenderer *fallback() const { return m_fallback.get(); }
    [[nodiscard]] IFeatureRenderer       *fallback()       { return m_fallback.get(); }
    void setFallback(std::unique_ptr<IFeatureRenderer> r);

    /*!
     * \brief All registered kind keys in deterministic (sorted) order.
     *        Used by the legend pipeline and serialisation so output is
     *        reproducible across runs / saves.
     */
    [[nodiscard]] std::vector<QString> kinds() const;

    /*! \brief Number of registered per-kind renderers (excludes fallback). */
    [[nodiscard]] int kindCount() const { return m_perKind.size(); }

    // IFeatureRenderer
    [[nodiscard]] QString rendererId() const override { return QStringLiteral("multikind"); }
    [[nodiscard]] SymbolStyle symbolFor(const FeatureRef &f,
                                        const QVariantMap &attrs) const override;
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;
    [[nodiscard]] std::unique_ptr<IFeatureRenderer> clone() const override;

private:
    // std::unordered_map (not QHash) because Qt 6 QHash requires copyable
    // values and unique_ptr's copy ctor is deleted. The custom QStringHash
    // adapter keeps QString as the key type so callers don't see std::string.
    std::unordered_map<QString,
                       std::unique_ptr<IFeatureRenderer>,
                       detail::QStringHash> m_perKind;
    std::unique_ptr<IFeatureRenderer>       m_fallback;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_MULTIKINDRENDERER_H
