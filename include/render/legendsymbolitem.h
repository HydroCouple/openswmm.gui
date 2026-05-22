/*!
 * \file   legendsymbolitem.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  One row in a layer's legend — label + swatch symbol + optional range.
 *
 *         A LegendSymbolItem is what IFeatureRenderer::legendSymbolItems()
 *         returns — one entry per visible row of the legend. The legend
 *         renderer (LegendRenderer, Slice BB Phase 8.6.12) paints each
 *         item as a (swatch, label) pair using `symbol` for the swatch.
 *
 *         range is meaningful only for graduated renderers (numeric bins);
 *         categorized / single-symbol renderers leave it as the default
 *         (NaN, NaN) sentinel.
 *
 *         userLabel / visible / sortIndex are the per-item overrides
 *         introduced in Slice BB Phase 8.6.10 — they let users rename,
 *         hide, or reorder legend rows without touching the renderer
 *         itself. Persisted in `.oswp` under `"legendOverrides"`.
 *
 *         The legend-from-renderer rule (§J.5): legend widgets must walk
 *         this list rather than maintain a parallel copy of "what symbols
 *         exist for this layer". That guarantees no drift between the map
 *         and the legend.
 *
 *         Cross-slice: Slice BB Phase 8.6 + Slice BI Phase 8.13.6 (see
 *         GUI_IMPLEMENTATION_PLAN.md §J.5). Sub-phase 8.13.6.1 —
 *         interface + types only.
 */

#ifndef OPENSWMM_RENDER_LEGENDSYMBOLITEM_H
#define OPENSWMM_RENDER_LEGENDSYMBOLITEM_H

#include "render/symbolstyle.h"

#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QtNumeric>

namespace OpenSWMM::Render
{

/*!
 * \struct LegendSymbolItem
 * \brief One row in a layer's legend.
 */
struct LegendSymbolItem
{
    QString              label;       /*!< Renderer-supplied label (e.g. "0 – 0.5 ft³/s"). */
    SymbolStyle          symbol;      /*!< Painted as the legend swatch. */
    QPair<double, double> range = { qQNaN(), qQNaN() }; /*!< (low, high) for graduated; NaNs otherwise. */
    QString              userLabel;   /*!< Optional override (BB Phase 8.6.10). Empty = use `label`. */
    bool                 visible = true;  /*!< Whether to render this row in the legend. */
    int                  sortIndex = 0;   /*!< Renderer-supplied default order; can be overridden. */

    [[nodiscard]] QJsonObject toJson() const;
    void fromJson(const QJsonObject &j);

    /*!
     * \brief Returns the label that should actually be drawn — `userLabel`
     *        when non-empty, otherwise `label`.
     */
    [[nodiscard]] QString effectiveLabel() const
    {
        return userLabel.isEmpty() ? label : userLabel;
    }
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_LEGENDSYMBOLITEM_H
