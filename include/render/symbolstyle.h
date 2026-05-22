/*!
 * \file   symbolstyle.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  A stack of SymbolLayer paint passes plus a layer-level opacity.
 *
 *         SymbolStyle is the unit a renderer returns from symbolFor() —
 *         it is what the layer's paint loop knows how to apply to one
 *         feature. The compositional model (a list of SymbolLayers) is
 *         the central design of §J.3: a single struct can describe both
 *         a uniform Qt::blue circle and a double-ringed manhole with a
 *         centre cross.
 *
 *         opacity multiplies the per-SymbolLayer alpha so the whole stack
 *         can be faded in / out without touching each layer's props.
 *
 *         Cross-slice: Slice BI Phase 8.13.6 (see GUI_IMPLEMENTATION_PLAN.md
 *         §J.3). Sub-phase 8.13.6.1 — interface + types only.
 */

#ifndef OPENSWMM_RENDER_SYMBOLSTYLE_H
#define OPENSWMM_RENDER_SYMBOLSTYLE_H

#include "render/symbollayer.h"

#include <QJsonObject>
#include <QList>

namespace OpenSWMM::Render
{

/*!
 * \struct SymbolStyle
 * \brief Stack of SymbolLayer paint passes (bottom-up) plus an overall opacity.
 */
struct SymbolStyle
{
    QList<SymbolLayer> layers;        /*!< Painted bottom-up; later passes on top. */
    qreal              opacity = 1.0; /*!< Multiplies every layer's alpha. Range [0,1]. */

    [[nodiscard]] QJsonObject toJson() const;
    void fromJson(const QJsonObject &j);
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SYMBOLSTYLE_H
