/**
 * @file      conduitsplitpick.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Shared "which conduit is under the cursor, and where along it"
 *         hit-test used by every tool that inserts a node by splitting a
 *         conduit (the generic add-node tools, Add Virtual Junction, Add
 *         Inlet Junction), plus the split-name generators.
 *
 *         One implementation replaces the byte-identical private copies the
 *         split tools used to carry (ADDNODE_SPLIT_REDESIGN_PLAN_2026-09-10.md
 *         step 1).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef CONDUITSPLITPICK_H
#define CONDUITSPLITPICK_H

#include <QPoint>
#include <QPointF>
#include <QString>

class MapCanvas;
class SWMMModelLayer;

namespace ConduitSplitPick
{

/*! One conduit hit: the layer, the conduit, the normalised split position and
 *  the closest point on its vertex-aware polyline (layer CRS). */
struct ConduitHit {
    SWMMModelLayer *layer = nullptr;
    int linkIdx = -1; ///< SoA/engine conduit index
    QString name;
    double t = 0.5;        ///< normalised polyline position, clamped to [0.02, 0.98]
    QPointF point;         ///< closest point (layer CRS)
    bool isStreet = false; ///< cross section is SWMM_XSECT_STREET
    /**
     * @return Raw layer pointer nonnull and linkIdx>=0; no engine/role/topology/lifetime
     *   validation.
     */
    [[nodiscard]] bool valid() const { return layer != nullptr && linkIdx >= 0; }
};

/*!
 * \brief Hit-test CONDUITS only (pumps, weirs, orifices and outlets have no
 *        length to divide) across the canvas's visible SWMM model layers, with
 *        a 12-pixel tolerance at the current zoom. Computes the arclength
 *        position `t` of the closest point, kept off the ends so no sliver
 *        conduit results, and whether the conduit is a STREET section.
 * \returns An invalid hit when nothing qualifies.
 * @param[in] canvas Optional borrowed canvas; null returns empty invalid hit.
 * @param[in] pixel Widget pixel position; diagonal+12pixels gives max-axis canvas-map pick
 *   tolerance.
 * @note GUI-thread read of visible model layers in canvas-list order, not global-nearest search;
 *   does not independently test opacity. Calls model pickAt, which can return another category and
 *   thereby skip that layer even when a conduit is nearby. Requires cached polyline with at
 *   least2points and positive native Euclidean length.
 * @note Projects cursor canvas→layer with ignored transform status/native fallback, computes
 *   closest original polyline point and native arclength ratio. Returned t clamps0.02..0.98 but
 *   returned point is NOT recomputed at clamped t; preview can differ from actual split near ends.
 *   isStreet false if no engine/getter failure. Returned layer pointer borrowed/raw; no selection
 *   or geometry mutation.
 */
[[nodiscard]] ConduitHit pickConduit(const MapCanvas *canvas, const QPoint &pixel);

/*! prefix1, prefix2, … — first name free in \p layer, where the prefix
 *  is PreferencesManager::elementNamePrefix(\p elementKind).
 * @param[in] layer Optional borrowed model used only for node-name cache queries.
 * @param[in] elementKind Preferences naming key, e.g. junction/virtual_junction.
 * @return First prefix+integer available among1..99999, prefix1 for null layer, unchecked prefix_X
 *   if exhausted. No reservation, cross-kind uniqueness check or engine mutation; caller handles
 *   concurrent later name collision.
 */
[[nodiscard]] QString nextNodeName(const SWMMModelLayer *layer, const QString &elementKind);

/*! base_B, then base_B1, base_B2, … — the downstream half's name.
 * @param[in] layer Optional borrowed model queried for existing link names.
 * @param[in] baseName Upstream conduit ID copied verbatim into suffix pattern.
 * @return First free base_B then base_B1..B99999; null layer returns base_B, exhausted unchecked
 *   base_BX. Does not reserve name, normalize identifiers or modify model.
 */
[[nodiscard]] QString nextSplitLinkName(const SWMMModelLayer *layer, const QString &baseName);

} // namespace ConduitSplitPick

#endif // CONDUITSPLITPICK_H
