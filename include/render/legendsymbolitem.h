/**
 * @file      legendsymbolitem.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     One row in a layer's legend — label + swatch symbol + optional range.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_LEGENDSYMBOLITEM_H
#define OPENSWMM_RENDER_LEGENDSYMBOLITEM_H

#include "render/legendrampspec.h"
#include "render/symbolstyle.h"

#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QtNumeric>

#include <memory>

namespace OpenSWMM::Render
{

/*!
 * \struct LegendSymbolItem
 * \brief One row in a layer's legend.
 */
struct LegendSymbolItem {
    QString label;      /*!< Renderer-supplied label (for example "0 – 0.5 ft³/s"). */
    SymbolStyle symbol; /*!< Painted as the legend swatch. */
    QPair<double, double> range = {qQNaN(),
                                   qQNaN()}; /*!< (low, high) for graduated; NaNs otherwise. */
    QString userLabel;   /*!< Optional override (BB Phase 8.6.10). Empty = use `label`. */
    bool visible = true; /*!< Whether to render this row in the legend. */
    int sortIndex = 0;   /*!< Renderer-supplied default order; can be overridden. */
    QString classKey;    /*!< Slice BB Phase 8.6.16 — identifies this row to
                                               IFeatureRenderer::setColorForClass etc. Renderer-specific:
                                               "0"…"N" for graduated/categorized, "single" for single-symbol.
                                               Empty when the renderer doesn't expose per-class editing. */
    QString sublayerId;  /*!< Slice S1 (RENDERING_OUTPUT_SUBLAYERS_PLAN.md §4.3) —
                                               identifies the ISublayer that produced this row, so the
                                               legend dock right-click → "Edit Sublayer Style…" can jump
                                               back to its style dialog. Empty when the row comes from a
                                               whole-layer renderer rather than a specific sublayer. */

    int rampId = 0; /*!< Rows sampled from one continuous color ramp (and its "< low" / "> high"
                         rows) share a nonzero id; 0 = not part of a ramp. */
    std::shared_ptr<const LegendRampSpec> ramp; /*!< The ramp, on the first row with the id. */

    /** @brief Serialize row identity, symbol, labels, visibility and ordering.
     * @return Owned object; range is included only when both bounds are finite.
     * Optional/default row fields are omitted and the symbol uses SymbolStyle::toJson(). */
    [[nodiscard]] QJsonObject toJson() const;
    /** @brief Replace all legend-row fields from JSON with missing-field defaults.
     * @param j Row object. Missing range becomes two NaNs; individual missing or
     * nonnumeric bounds also become NaN. Text defaults empty, visibility true,
     * and sortIndex zero. No range ordering or class/sublayer identity validation
     * occurs; existing symbol layers are replaced through SymbolStyle::fromJson(). */
    void fromJson(const QJsonObject &j);

    /*!
     * \brief Returns the label that should actually be drawn — `userLabel`
     *        when non-empty, otherwise `label`.
     * @return Owned text, without trimming; whitespace-only user labels still win.
     */
    [[nodiscard]] QString effectiveLabel() const { return userLabel.isEmpty() ? label : userLabel; }
};

/*!
 * \brief Mark @p count rows from @p first as samples of one continuous color ramp: they get a
 *        new ramp id and the first of them gets @p spec. The rows themselves do not change.
 * @param rows Legend rows.
 * @param first First ramp row (include the "< low" row when there is one).
 * @param count Rows in the ramp (include the "> high" row when there is one); out-of-range
 *   indexes are ignored.
 * @param spec The ramp; nothing is tagged when it is not valid.
 */
void tagLegendRamp(QList<LegendSymbolItem> &rows, int first, int count, LegendRampSpec spec);

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_LEGENDSYMBOLITEM_H
