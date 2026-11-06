/**
 * @file      symbolstyle.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     A stack of SymbolLayer paint passes plus a layer-level opacity.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
struct SymbolStyle {
    QList<SymbolLayer> layers; /*!< Painted bottom-up; later passes on top. */
    qreal opacity = 1.0;       /*!< Multiplies every layer's alpha. Range [0,1]. */

    /** @brief Serialize the ordered symbol stack and overall opacity.
     * @return Owned object containing every layer, including an empty array when
     * the stack is empty. Opacity is written verbatim without clamping. */
    [[nodiscard]] QJsonObject toJson() const;
    /** @brief Replace the stack and opacity from JSON without merging existing layers.
     * @param j Style object; missing/nonarray layers clear the stack and missing
     * or nonnumeric opacity becomes one. Each array entry is loaded as a
     * SymbolLayer, including nonobject entries that become default layers.
     * Opacity is not clamped and individual layer semantics are not validated. */
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
 *  Returns \a fallback when the key is absent or unparseable.
 * @param props Property map to inspect without mutation.
 * @param key Exact case-sensitive lookup key, without normalization.
 * @param fallback Color returned for absent/invalid values, even if itself invalid.
 * @return Valid QColor payload or parsed string color; an invalid QColor payload
 * returns fallback directly rather than being stringified into black. */
[[nodiscard]] QColor readColor(const QVariantMap &props, const QString &key,
                               const QColor &fallback = QColor());

/** @brief Store a color in the canonical in-memory QVariant representation.
 * @param[in,out] props Property map receiving the new or replaced entry.
 * @param key Exact key to insert; empty/arbitrary keys are accepted.
 * @param c Color to copy, including an invalid color; no parsing or validation occurs. */
void writeColor(QVariantMap &props, const QString &key, const QColor &c);

/*! First resolvable colour across the stack, checking the grammar keys
 *  "fillColor" (marker/fill specs), then "color" (line spec), then
 *  "outlineColor" per layer. Matches the legend-swatch convention.
 * @param style Stack inspected in stored bottom-to-top order, including disabled-looking layers.
 * @param fallback Color returned if no valid color is found.
 * @return First valid tolerant color read in layer/key order; opacity, kind and
 * data-defined expressions are not evaluated. */
[[nodiscard]] QColor firstColor(const SymbolStyle &style, const QColor &fallback = QColor());

/*! Write \a c into whichever colour slot(s) each layer already advertises
 *  ("fillColor" and/or "color"). Layers without a colour slot are left
 *  untouched. Single shared copy of the helper previously duplicated in
 *  Graduated / Categorized / SingleSymbol / UnclassedColors renderers.
 * @param[in,out] style Stack whose existing fillColor/color entries are replaced.
 * @param c Color copied as a QColor variant, including invalid input.
 * Outline-only entries, expressions, opacity, and layers without either key are unchanged. */
void overrideColorInPlace(SymbolStyle &style, const QColor &c);

} // namespace SymbolProps

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SYMBOLSTYLE_H
