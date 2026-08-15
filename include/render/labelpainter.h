/*!
 * \file   labelpainter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice US.B3 (UNIFIED_STYLING plan B3) — the one place every layer
 *         paints a text label, so halo / background / scale-gating /
 *         placement behave identically across the 1D model, 1D results, and
 *         2D results paint paths.
 *
 *         Pure QPainter helper driven by a LabelConfig value — no MapCanvas
 *         or layer coupling. Callers compute the canvas scale denominator
 *         (MapCanvas::scaleDenominator) and pass it to scaleVisible(); the
 *         draw routine renders constant-point-size text in screen space
 *         (the GIS standard — labels don't grow/shrink with zoom).
 *
 *         Draw order is fixed: background rounded-rect → stroked-path halo →
 *         glyph fill, so the halo reads over a busy basemap and the optional
 *         background frame sits behind everything.
 */
#ifndef OPENSWMM_RENDER_LABELPAINTER_H
#define OPENSWMM_RENDER_LABELPAINTER_H

#include "render/labelconfig.h"

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>

class QPainter;

namespace OpenSWMM::Render
{

/*!
 * \class LabelPainter
 * \brief Stateless label-drawing helpers shared by every paint path.
 */
class LabelPainter
{
public:
    /*!
     * \brief Whether a label is visible at the given canvas scale denominator
     *        (the "1:N" map scale). Honours LabelConfig::minScale / maxScale
     *        (0 = unbounded): hidden when zoomed further out than minScale or
     *        further in than maxScale. A non-finite / non-positive denominator
     *        is treated as "always visible" (gating disabled).
     */
    [[nodiscard]] static bool scaleVisible(const LabelConfig &cfg,
                                           double scaleDenominator);

    /*!
     * \brief Offset (screen px) from the feature anchor to the TOP-LEFT of the
     *        text box for the configured placement. \p textSize is the text
     *        bounding size. AutoPlacement = above-right (the historic default).
     */
    [[nodiscard]] static QPointF placementOffset(const LabelConfig &cfg,
                                                 const QSizeF &textSize);

    /*!
     * \brief The text box rect (screen px) placed at \p anchor per the
     *        configured placement. Top-left = anchor + placementOffset.
     */
    [[nodiscard]] static QRectF labelRect(const LabelConfig &cfg,
                                          const QPointF &anchor,
                                          const QSizeF &textSize);

    /*!
     * \brief Draw \p text with the TOP-LEFT of its box at \p topLeft (screen
     *        px), using cfg.effectiveFont(): background → halo → fill. The
     *        painter's world transform should be disabled (screen space) so
     *        the point size is constant across zoom. Saves/restores painter
     *        state internally.
     */
    static void drawLabel(QPainter &p, const QPointF &topLeft,
                          const QString &text, const LabelConfig &cfg);

    /*!
     * \brief Convenience overload — places the text at \p anchor per the
     *        configured placement and draws it. Returns the box rect drawn
     *        (useful for caller-side collision bookkeeping later).
     */
    static QRectF drawLabelAt(QPainter &p, const QPointF &anchor,
                              const QString &text, const LabelConfig &cfg);
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_LABELPAINTER_H
