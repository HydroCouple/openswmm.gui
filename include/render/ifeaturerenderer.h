/*!
 * \file   ifeaturerenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  The single seam every layer paint path goes through.
 *
 *         IFeatureRenderer is the load-bearing abstraction introduced by
 *         GUI_IMPLEMENTATION_PLAN.md §J.2. Every vector / point / line /
 *         polygon layer owns one IFeatureRenderer and asks it
 *         `symbolFor(feature, attrs)` to obtain the SymbolStyle to paint.
 *
 *         Concrete renderers (sub-phase 8.13.6.2):
 *           - SingleSymbolRenderer  — one SymbolStyle for all features.
 *           - GraduatedRenderer     — classify a numeric attribute into bins.
 *           - CategorizedRenderer   — distinct symbol per unique enum value.
 *           - RuleBasedRenderer     — first-match rule list with expressions.
 *
 *         The legend pipeline (§J.5) walks `legendSymbolItems()` to build
 *         a legend that is structurally guaranteed to match what the map
 *         draws — there is no second code path for "what does this layer
 *         look like".
 *
 *         JSON round-trip (`toJson` / `fromJson`) is mandatory so styles
 *         survive `.oswp` save/load and `.swmm-style.json` import/export
 *         (Slice BB Phase 8.6.13).
 *
 *         `clone()` returns a deep copy so layers can hand out renderer
 *         snapshots to map presets (Slice CJ.2) without aliasing.
 *
 *         Sub-phase 8.13.6.1 — interface + types only. No concrete
 *         renderers, no Layer integration, no paint-loop refactor. Those
 *         arrive in subsequent sub-phases under the same Phase 8.13.6.
 */

#ifndef OPENSWMM_RENDER_IFEATURERENDERER_H
#define OPENSWMM_RENDER_IFEATURERENDERER_H

#include "render/featureref.h"
#include "render/legendsymbolitem.h"
#include "render/symbolstyle.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantMap>

#include <memory>

namespace OpenSWMM::Render
{

/*!
 * \class IFeatureRenderer
 * \brief Abstract interface — every vector layer's paint path goes here.
 *
 *        Implementations must be deterministic: given the same FeatureRef
 *        and the same attribute map, symbolFor() must return the same
 *        SymbolStyle (modulo data-defined overrides whose inputs change).
 *        This is what lets the legend pipeline trust legendSymbolItems()
 *        as ground truth for "what the map will show".
 *
 *        Renderers are owned via std::unique_ptr by the layer. Use clone()
 *        whenever a snapshot is needed (map presets, undo stack).
 */
class IFeatureRenderer
{
public:
    virtual ~IFeatureRenderer() = default;

    /*!
     * \brief Stable string id ("single", "graduated", "categorized", "rule").
     *        Used as the discriminator in JSON round-trip.
     */
    [[nodiscard]] virtual QString rendererId() const = 0;

    /*!
     * \brief Returns the SymbolStyle to paint for one feature.
     * \param f      Stable identifier for the feature (see FeatureRef).
     * \param attrs  Attribute key/value pairs the renderer may classify on.
     *               Renderers must tolerate missing keys gracefully (return
     *               a default symbol rather than crashing).
     */
    [[nodiscard]] virtual SymbolStyle symbolFor(const FeatureRef &f,
                                                const QVariantMap &attrs) const = 0;

    /*!
     * \brief Returns one LegendSymbolItem per visible legend row.
     *        Drives the legend dock, the on-canvas legend overlay, and the
     *        map composer's legend frame (legend-from-renderer rule, §J.5).
     */
    [[nodiscard]] virtual QList<LegendSymbolItem> legendSymbolItems() const = 0;

    /*!
     * \brief Serialises this renderer to a JSON object.
     *        The "id" field equals rendererId(); remaining keys are
     *        renderer-specific.
     */
    [[nodiscard]] virtual QJsonObject toJson() const = 0;

    /*!
     * \brief Restores this renderer from a JSON object previously produced
     *        by toJson(). Implementations should tolerate missing/unknown
     *        keys by falling back to default values rather than throwing.
     */
    virtual void fromJson(const QJsonObject &j) = 0;

    /*!
     * \brief Deep copy. Owning layers / map presets / undo stack call this
     *        when they need a renderer snapshot decoupled from the live one.
     */
    [[nodiscard]] virtual std::unique_ptr<IFeatureRenderer> clone() const = 0;

protected:
    IFeatureRenderer() = default;
    IFeatureRenderer(const IFeatureRenderer &) = default;
    IFeatureRenderer &operator=(const IFeatureRenderer &) = default;
    IFeatureRenderer(IFeatureRenderer &&) = default;
    IFeatureRenderer &operator=(IFeatureRenderer &&) = default;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_IFEATURERENDERER_H
