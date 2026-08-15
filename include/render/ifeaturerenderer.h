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

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantMap>

#include <memory>

namespace OpenSWMM::Render
{

/*!
 * \enum ClassEditKind
 * \brief Categories of per-class mutation that a renderer may support.
 *
 *        Slice BB Phase 8.6.16 — the legend right-click context menu and
 *        the LegendPropertiesDialog Layers tab both query
 *        IFeatureRenderer::supportsClassEdit(kind) to decide whether to
 *        offer the corresponding action / cell editor. A renderer that
 *        returns false for a kind also no-ops the matching setter.
 */
enum class ClassEditKind {
    Color,   /*!< Per-class swatch / fill colour. */
    Size,    /*!< Per-class marker size (points / squares / triangles…). */
    Width,   /*!< Per-class line width (polylines / strokes). */
    Symbol   /*!< Wholesale per-class SymbolStyle replacement. */
};

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

    // ── Per-class editing (Slice BB Phase 8.6.16) ──────────────────────
    //
    // Lets the legend's right-click "Change color…/size…/symbol…" entries
    // and the LegendPropertiesDialog Layers tab mutate a specific legend
    // row in place. The classKey identifies which row to edit; its format
    // is renderer-specific (per-renderer doc on the overrides):
    //   - GraduatedRenderer:  stringified bin index ("0", "1", …)
    //   - CategorizedRenderer: stringified category index ("0", "1", …)
    //   - SingleSymbolRenderer: "single" (only one class)
    //
    // Default implementations are no-ops returning false / doing nothing
    // so a renderer that supports nothing (e.g. RuleBasedRenderer today)
    // doesn't need to override anything. Callers MUST query
    // supportsClassEdit(kind) before invoking the matching setter.

    /*! \brief True if this renderer can mutate \p kind on a per-class
     *         basis. Defaults to false so opt-in is explicit. */
    [[nodiscard]] virtual bool supportsClassEdit(ClassEditKind /*kind*/) const { return false; }

    /*! \brief Read the current colour for \p classKey. Returns an invalid
     *         QColor when the renderer has no explicit value for that class
     *         (e.g. GraduatedRenderer with no override at that bin index,
     *         where the legend swatch comes from sampling the ramp).
     *
     *         Used by undo-command constructors to snapshot the "before"
     *         state so undo can restore it via setColorForClass. */
    [[nodiscard]] virtual QColor colorForClass(const QString & /*classKey*/) const
    { return {}; }

    /*! \brief Read the current marker / line size for \p classKey. Returns
     *         a negative value when the renderer has no explicit per-class
     *         size (e.g. GraduatedRenderer's globally-mapped sizeForBin,
     *         or a class that doesn't carry a size prop). Used by
     *         SetRendererClassSizeCommand to snapshot the "before" state. */
    [[nodiscard]] virtual qreal sizeForClass(const QString & /*classKey*/) const
    { return -1.0; }

    /*! \brief Override the colour for the class identified by \p classKey.
     *         Has no effect when supportsClassEdit(Color) is false. */
    virtual void setColorForClass(const QString & /*classKey*/, const QColor & /*color*/) {}

    /*! \brief Override the marker size for \p classKey (points / squares /
     *         triangles…). No-op when supportsClassEdit(Size) is false. */
    virtual void setSizeForClass(const QString & /*classKey*/, qreal /*size*/) {}

    /*! \brief Override the line width for \p classKey. No-op when
     *         supportsClassEdit(Width) is false. */
    virtual void setWidthForClass(const QString & /*classKey*/, qreal /*width*/) {}

    /*! \brief Wholesale-replace the SymbolStyle for \p classKey. No-op
     *         when supportsClassEdit(Symbol) is false. */
    virtual void setSymbolForClass(const QString & /*classKey*/,
                                   const SymbolStyle & /*style*/) {}

    /*! \brief Drop every per-class override applied via the setters above.
     *         The renderer falls back to its ramp / categories / base
     *         symbol for all classes. Default implementation: no-op. */
    virtual void clearClassEditOverrides() {}

protected:
    IFeatureRenderer() = default;
    IFeatureRenderer(const IFeatureRenderer &) = default;
    IFeatureRenderer &operator=(const IFeatureRenderer &) = default;
    IFeatureRenderer(IFeatureRenderer &&) = default;
    IFeatureRenderer &operator=(IFeatureRenderer &&) = default;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_IFEATURERENDERER_H
