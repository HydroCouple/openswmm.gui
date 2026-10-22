/**
 * @file      symbollevels.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Cross-feature paint order (Symbol Levels) — Slice Z.11.
 *
 *         QGIS-native feature. Without Symbol Levels, every feature is
 *         painted top-down before moving to the next; intersection
 *         seams in line networks look fragmented because each feature's
 *         outline paints over the previous feature's fill. With Symbol
 *         Levels enabled, the paint pass groups by (level, feature)
 *         instead of (feature, level) — all symbol layers at level 0
 *         paint across every feature first, then level 1, etc.
 *
 *         The level is a per-SymbolLayer integer (canonical prop key
 *         `"level"`, default 0). The enable flag lives on Rule
 *         (`Rule::symbolLevelsEnabled` — added in Z.11).
 *
 *         Slice Z.11 ships the data-side: the prop convention, the
 *         enable flag, and the paint-order helper. Integration with the
 *         actual layer paint loops is the named Z.11a follow-up — the
 *         existing renderers' paint paths consume the helper output as
 *         a sequence of (feature-index, symbol-layer-index) pairs.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_SYMBOLLEVELS_H
#define OPENSWMM_RENDER_SYMBOLLEVELS_H

#include "render/symbolstyle.h"

#include <QString>
#include <QVector>

namespace OpenSWMM::Render
{

/*! Canonical SymbolLayer::props key holding the integer paint level.
 *  Missing → default level 0. Lower levels paint first. */
inline constexpr const char *kSymbolLevelPropKey = "level";

/*! Read the level for one SymbolLayer. Returns 0 when the prop is
 *  absent or non-numeric. */
/** @param layer Borrowed layer; only the level property is read.
 * @return QVariant::toInt() result when conversion succeeds, otherwise zero.
 * Negative levels are allowed and sort before zero. */
[[nodiscard]] int symbolLayerLevel(const SymbolLayer &layer);

/*! Write the level into a SymbolLayer's props. */
/** @param[in,out] layer Destination; only its level property is inserted/replaced.
 * @param level Raw signed paint-order key; not clamped or validated. No signals are emitted. */
void setSymbolLayerLevel(SymbolLayer &layer, int level);

/*!
 * \struct PaintStep
 * \brief One entry in the paint sequence computed by
 *        \ref computeSymbolLevelOrder.
 *
 *        `featureIndex` indexes into the input `features` vector;
 *        `symbolLayerIndex` indexes into that feature's Symbol's
 *        `layers` vector. The paint host applies the matching
 *        SymbolLayer to the feature, then moves on to the next step.
 */
struct PaintStep {
    int featureIndex = 0;
    int symbolLayerIndex = 0;
};

/** @brief Compare both paint-step indices exactly.
 * @param a Left step; indices need not reference a live feature list.
 * @param b Right step.
 * @return True only when both feature and symbol-layer indices match. */
[[nodiscard]] inline bool operator==(const PaintStep &a, const PaintStep &b)
{
    return a.featureIndex == b.featureIndex && a.symbolLayerIndex == b.symbolLayerIndex;
}

/*!
 * \brief Compute the paint sequence for \p features under \p enabled
 *        Symbol Levels.
 *
 *        When \p enabled is `false` (default), paint order is the
 *        natural per-feature, bottom-up symbol-layer order: for each
 *        feature in input order, every symbol layer in order. This is
 *        the legacy behavior.
 *
 *        When \p enabled is `true`, paint order is level-major: for
 *        each distinct level (ascending), every feature's symbol
 *        layers at that level, in feature-input order. Within one
 *        feature the symbol-layer-input order is preserved among
 *        siblings of the same level. Symbol layers missing the level
 *        prop default to level 0.
 *
 *        Stable: ties are broken by feature-input order, then
 *        symbol-layer-input order.
 */
/** @param features Borrowed per-feature symbol styles, read synchronously. Feature,
 * layer and total-step counts must fit int; no referenced data is retained.
 * @param enabled Select level-major ordering instead of natural feature-major order.
 * @return Independent sequence containing every layer of every feature exactly once,
 * including disabled layers; filtering/enabled-state checks remain the paint host's
 * responsibility. Empty input or empty styles contribute no steps. No painting occurs. */
[[nodiscard]] QVector<PaintStep> computeSymbolLevelOrder(const QVector<SymbolStyle> &features,
                                                         bool enabled);

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SYMBOLLEVELS_H
