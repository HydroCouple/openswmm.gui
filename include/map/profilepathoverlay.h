/*!
 * \file   profilepathoverlay.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Transient map-canvas overlay used by Slice BC to highlight
 *         candidate profile paths while the path-picker is open.
 *
 *         The overlay is a `QGraphicsItemGroup` attached directly to the
 *         map scene (`MapCanvas::mapScene()`).  Its lifecycle is decoupled
 *         from the active map tool — the picker dialog can stay open while
 *         the user clicks elsewhere on the canvas without losing the
 *         highlight.  Coordinates follow the standard layer-CRS → scene
 *         convention used by SWMMLayerItem (`QPointF(mx, -my)`).
 */

#ifndef PROFILE_PATH_OVERLAY_H
#define PROFILE_PATH_OVERLAY_H

#include "plot/profilerouter.h"

#include <QColor>
#include <QGraphicsItemGroup>
#include <QPointF>
#include <QVector>

class SWMMModelLayer;

/*!
 * \class ProfilePathOverlay
 * \brief Renders an ordered list of candidate paths over the map.
 *
 *        Each path is drawn as a translucent thick poly-line in a distinct
 *        categorical color (reuses `CategoricalPalette`).  One path may be
 *        promoted to "highlighted" (full opacity, others dimmed); the
 *        start/end endpoints get pulsing halos so the user knows which
 *        endpoints the candidates connect.
 *
 *        The overlay is *read-only* — it does not accept clicks; the
 *        `MapToolSelectProfile` continues to drive interaction.  Hover
 *        synchronization with the picker dialog goes through
 *        `setHighlightedPath()`.
 */
class ProfilePathOverlay : public QGraphicsItemGroup
{
public:
    explicit ProfilePathOverlay(SWMMModelLayer *model,
                                QGraphicsItem *parent = nullptr);

    /*!
     * \brief Replaces the displayed candidate paths.  Rebuilds child items.
     */
    void setPaths(const QVector<ProfileRouter::Path> &paths);

    /*!
     * \brief Promotes path \p index to full opacity; dims the rest.
     *        Pass \c -1 to show all candidates at equal weight.
     */
    void setHighlightedPath(int index);

    /*!
     * \brief Marks two endpoints with pulsing halos so the user can see
     *        which start/end the candidates connect.  Pass `-1` to clear.
     */
    void setEndpoints(int startEngineNodeIdx, int endEngineNodeIdx);

    /*!
     * \brief Removes all rendered children and detaches.  Caller is
     *        responsible for `delete`-ing the overlay or letting the scene
     *        own it; this method is for the "search returned empty" path.
     */
    void clear();

    /*!
     * \brief Returns the number of candidate paths currently displayed.
     */
    [[nodiscard]] int pathCount() const;

    /*!
     * \brief Returns the categorical color assigned to path \p index.
     *        Useful for the picker dialog to render a matching color chip.
     */
    [[nodiscard]] QColor colorForPath(int index) const;

private:
    void rebuild();
    void applyHighlightStyling();

    [[nodiscard]] QVector<QPointF> sceneCoordsForLink(int engineLinkIdx) const;
    [[nodiscard]] QPointF          sceneCoordForNode(int engineNodeIdx) const;

    SWMMModelLayer                   *m_model       = nullptr;
    QVector<ProfileRouter::Path>      m_paths;
    QVector<QGraphicsPathItem *>      m_pathItems;
    QGraphicsEllipseItem             *m_startHalo  = nullptr;
    QGraphicsEllipseItem             *m_endHalo    = nullptr;
    int                               m_highlighted = -1;
    int                               m_startEngineNodeIdx = -1;
    int                               m_endEngineNodeIdx   = -1;
};

#endif // PROFILE_PATH_OVERLAY_H
