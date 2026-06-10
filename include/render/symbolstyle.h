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

#include <QColor>
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

/*!
 * \namespace SymbolProps
 * \brief Canonical colour prop accessors for SymbolLayer prop bags (gap A1.2).
 *
 *        Convention (X1): colours live in memory as QColor *variants*;
 *        SymbolLayer::toJson/fromJson convert to/from hex strings at the
 *        JSON boundary. Historically some writers stored hex strings in
 *        memory while readers used QVariant::value<QColor>() (which does
 *        not parse hex), silently dropping colours. These helpers end the
 *        split: reads tolerate BOTH encodings forever (back-compat with
 *        styles authored before the canonicalisation); writes emit the one
 *        canonical form. All new code must go through them.
 */
namespace SymbolProps
{

/*! Tolerant colour read: accepts a QColor variant or a hex/named string.
 *  Returns \a fallback when the key is absent or unparseable. */
[[nodiscard]] QColor readColor(const QVariantMap &props, const QString &key,
                               const QColor &fallback = QColor());

/*! Canonical colour write: stores a QColor variant (never a string). */
void writeColor(QVariantMap &props, const QString &key, const QColor &c);

/*! First resolvable colour across the stack, checking the grammar keys
 *  "fillColor" (marker/fill specs), then "color" (line spec), then
 *  "outlineColor" per layer. Matches the legend-swatch convention. */
[[nodiscard]] QColor firstColor(const SymbolStyle &style,
                                const QColor &fallback = QColor());

/*! Write \a c into whichever colour slot(s) each layer already advertises
 *  ("fillColor" and/or "color"). Layers without a colour slot are left
 *  untouched. Single shared copy of the helper previously duplicated in
 *  Graduated / Categorized / SingleSymbol / UnclassedColors renderers. */
void overrideColorInPlace(SymbolStyle &style, const QColor &c);

} // namespace SymbolProps

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SYMBOLSTYLE_H
