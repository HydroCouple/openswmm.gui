/*!
 * \file   conduitsplitpick.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Shared "which conduit is under the cursor, and where along it"
 *         hit-test used by every tool that inserts a node by splitting a
 *         conduit (the generic add-node tools, Add Virtual Junction, Add
 *         Inlet Junction), plus the split-name generators.
 *
 *         One implementation replaces the byte-identical private copies the
 *         split tools used to carry (ADDNODE_SPLIT_REDESIGN_PLAN_2026-09-10.md
 *         step 1).
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
struct ConduitHit
{
    SWMMModelLayer *layer   = nullptr;
    int             linkIdx = -1;      ///< SoA/engine conduit index
    QString         name;
    double          t = 0.5;           ///< normalised polyline position, clamped to [0.02, 0.98]
    QPointF         point;             ///< closest point (layer CRS)
    bool            isStreet = false;  ///< cross section is SWMM_XSECT_STREET
    [[nodiscard]] bool valid() const { return layer != nullptr && linkIdx >= 0; }
};

/*!
 * \brief Hit-test CONDUITS only (pumps, weirs, orifices and outlets have no
 *        length to divide) across the canvas's visible SWMM model layers, with
 *        a 12-pixel tolerance at the current zoom. Computes the arclength
 *        position `t` of the closest point, kept off the ends so no sliver
 *        conduit results, and whether the conduit is a STREET section.
 * \returns An invalid hit when nothing qualifies.
 */
[[nodiscard]] ConduitHit pickConduit(const MapCanvas *canvas, const QPoint &pixel);

/*! `<prefix>1`, `<prefix>2`, … — first name free in \p layer, where the prefix
 *  is PreferencesManager::elementNamePrefix(\p elementKind). */
[[nodiscard]] QString nextNodeName(const SWMMModelLayer *layer, const QString &elementKind);

/*! `<base>_B`, then `<base>_B1`, `<base>_B2`, … — the downstream half's name. */
[[nodiscard]] QString nextSplitLinkName(const SWMMModelLayer *layer, const QString &baseName);

} // namespace ConduitSplitPick

#endif // CONDUITSPLITPICK_H
