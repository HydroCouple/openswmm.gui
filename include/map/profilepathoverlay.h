/**
 * @file      profilepathoverlay.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Transient map-canvas overlay used by Slice BC to highlight
 *         candidate profile paths while the path-picker is open.
 *
 *         The overlay stores the same path state as the legacy
 *         QGraphicsScene item, but MapCanvas can also paint it in the final
 *         widget overlay pass after the QSG 1D/2D result frame.  That keeps
 *         candidate and accepted paths visible above flood maps and result
 *         layers that are composited later than the scene buffer.
 *         Coordinates follow the standard layer-CRS → scene convention used
 *         by SWMMLayerItem (`QPointF(mx, -my)`).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef PROFILE_PATH_OVERLAY_H
#define PROFILE_PATH_OVERLAY_H

#include "plot/profilerouter.h"

#include <QColor>
#include <QGraphicsItemGroup>
#include <QPointF>
#include <QVector>
#include <functional>

class SWMMModelLayer;
class QPainter;

/*!
 * \class ProfilePathOverlay
 * \brief Renders an ordered list of candidate paths over the map.
 *
 *        Each path is drawn as a translucent thick poly-line in a distinct
 *        categorical color (reuses `CategoricalPalette`).  One path may be
 *        promoted to "highlighted" (full opacity, others dimmed); the
 *        start/end endpoints get static preference-styled halos so the user knows which
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
    /**
     * @param[in] model Optional borrowed model layer, must outlive overlay; raw pointer is not
     *   guarded.
     * @param[in] parent Optional QGraphicsItem owner; otherwise scene/caller owns overlay.
     * @note Noninteractive group, child events disabled, no mouse buttons, z band1e7. Scene/widget
     *   painter methods require GUI-thread serialization; model indices/geometry must remain
     *   coherent or caller rebuild path references. No timer/animation is created for endpoint
     *   halos.
     */
    explicit ProfilePathOverlay(SWMMModelLayer *model, QGraphicsItem *parent = nullptr);

    /*!
     * \brief Replaces the displayed candidate paths.  Rebuilds child items.
     * @param[in] paths Ordered value-copied router paths with current engine node/link IDs; invalid
     *   IDs may yield missing geometry/origin fallback.
     * @note Always resets highlight minus one, deletes/rebuilds owned path children and reapplies
     *   style. Existing endpoint indices/halos retained. Does not route/search, select model
     *   objects or emit a canvas repaint signal directly.
     */
    void setPaths(const QVector<ProfileRouter::Path> &paths);

    /*!
     * \brief Promotes path \p index to full opacity; dims the rest.
     *        Pass \c -1 to show all candidates at equal weight.
     * @param[in] index Candidate ordinal; any negative or>=pathCount canonicalizes to minus
     *   one/equal weighting.
     * @note Equality no-op. Restyles scene children:promoted alpha1, width7.5px, zraised; others
     *   alpha0.35, width6px; no highlight alpha0.75. Widget overlay reads same state at next host
     *   paint. No geometry rebuild/own repaint signal.
     */
    void setHighlightedPath(int index);

    /*!
     * \brief Marks two endpoints with static halos so the user can see
     *        which start/end the candidates connect.  Pass `-1` to clear.
     * @param[in] startEngineNodeIdx Global current node index, negative deletes start scene halo.
     * @param[in] endEngineNodeIdx Global current node index, negative deletes end scene halo.
     * @note Stores indices verbatim; positive invalid index paints origin fallback. Reuses/creates
     *   fixed-pixel scene ellipses from current preferences, positions through native→canvas
     *   transform/Y inversion. Static rings, not pulsing animation; no host repaint signal.
     */
    void setEndpoints(int startEngineNodeIdx, int endEngineNodeIdx);

    /*!
     * \brief Removes rendered path/scene-halo children; the overlay stays attached.  Caller is
     *        responsible for `delete`-ing the overlay or letting the scene
     *        own it; this method is for the "search returned empty" path.
     * @note Clears paths/highlight and deletes path+halo child items, but does NOT detach overlay
     *   from scene/parent or reset stored endpoint indices. Widget paintOverlay still consults
     *   endpoint indices and can draw halos after clear; setEndpoints(-1,-1)to clear that path too.
     *   Caller owns overlay deletion/host repaint.
     */
    void clear();

    /*!
     * \brief Returns the number of candidate paths currently displayed.
     * @return Stored candidate path count, including empty/unrenderable paths; does not count
     *   surviving geometry children/links.
     */
    [[nodiscard]] int pathCount() const;

    /*!
     * \brief Returns the categorical color assigned to path \p index.
     *        Useful for the picker dialog to render a matching color chip.
     * @param[in] index Candidate path ordinal.
     * @return Shared categorical palette colour by ordinal, gray out of-range. Does not include
     *   highlight alpha/casing or stored preferences.
     */
    [[nodiscard]] QColor colorForPath(int index) const;

    /*! \brief Map scene point → device pixel, supplied by the canvas. */
    using SceneToPixel = std::function<QPointF(const QPointF &)>;

    /*! \brief Draw paths + endpoint halos in widget/pixel space. Called by
     *  MapCanvas after the QSG frame so the profile overlay sits above 1D/2D
     *  results, regardless of scene z-order.
     * @param[in] p Active borrowed painter in callback pixel space; render state saved/restored.
     * @param[in] toPixel Required callable scene→painter-pixel mapping when geometry/endpoints
     *   exist; empty callback is not guarded.
     * @note Requires live borrowed model; null model no-op. Recomputes projected link geometry each
     *   paint, orients each link toward path traversal, bridges between nonempty segments, paints
     *   dim paths then highlighted above with white casing. Endpoint preference rings drawn from
     *   stored indices independently of scene halo item existence. No clipping/finite
     *   check/animation or host repaint emission; caller handles viewport and DPR.
     */
    void paintOverlay(QPainter &p, const SceneToPixel &toPixel) const;

private:
    /**
     * @brief Delete/rebuild one owned scene path item per stored candidate from current borrowed
     *   model geometry, then restyle highlight.
     * @note Null model leaves no children. Skips empty link geometry, orients link vertices by
     *   proximity to path upstream node. Scene child construction differs from widget overlay by
     *   not explicitly lineTo the next segment first point. No cached model lifetime
     *   protection/signals.
     */
    void rebuild();
    /**
     * @brief Apply categorical cosmetic round pens and zorder to current scene path children based
     *   on stored highlighted ordinal.
     * @note Does not rebuild projected geometry, change endpoints or request canvas repaint; child
     *   item pen/zchanges trigger ordinary scene dirtiness.
     */
    void applyHighlightStyling();

    /**
     * @param[in] engineLinkIdx Current global cached link index.
     * @return Owned native polyline projected through model transform to canvas X,-Y scene
     *   positions; empty null model/invalid link. Transform failure status ignored and helper falls
     *   back native input; no source geometry mutation.
     */
    [[nodiscard]] QVector<QPointF> sceneCoordsForLink(int engineLinkIdx) const;
    /**
     * @param[in] engineNodeIdx Current global cached node index.
     * @return Projected canvas X,-Y point; origin fallback for null model/invalid cached node.
     *   Transform failure status ignored with native-coordinate fallback, no separate success flag.
     */
    [[nodiscard]] QPointF sceneCoordForNode(int engineNodeIdx) const;

    SWMMModelLayer *m_model = nullptr;
    QVector<ProfileRouter::Path> m_paths;
    QVector<QGraphicsPathItem *> m_pathItems;
    QGraphicsEllipseItem *m_startHalo = nullptr;
    QGraphicsEllipseItem *m_endHalo = nullptr;
    int m_highlighted = -1;
    int m_startEngineNodeIdx = -1;
    int m_endEngineNodeIdx = -1;
};

#endif // PROFILE_PATH_OVERLAY_H
