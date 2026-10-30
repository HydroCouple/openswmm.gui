/**
 * @file      meshprofileoverlay.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Map-canvas overlay for a 2D-mesh longitudinal profile: the traced
 *         path drawn persistently on the map plus a position arrow that mirrors
 *         the profile chart's cursor (and can be dragged on the map via
 *         MapToolProfileMarker).
 *
 *         A lightweight geometry model + widget-space painter (NOT a
 *         QGraphicsItem). MapCanvas owns the pointer and paints it via
 *         `paint()` AFTER the QSG mesh frame is composited, so the line and
 *         marker always sit on top of the 2D flood-map mesh (a scene item
 *         can't — the QSG frame is blitted over the whole QGraphicsScene
 *         buffer). Its lifetime is owned by the MeshProfilePlotDialog —
 *         created when the profile window opens and detached/destroyed when it
 *         closes. The traced polyline is already in scene coords (sx = mapX,
 *         sy = -mapY, the MapToolMeshProfile convention).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef MESH_PROFILE_OVERLAY_H
#define MESH_PROFILE_OVERLAY_H

#include <QPointF>
#include <QVector>
#include <functional>

class QPainter;

class MeshProfileOverlay
{
public:
    /**
     * @note Value-only empty geometry, total0, arrow hidden minus one. No QObject/signals/scene
     *   ownership; caller coordinates GUI painting and borrowed attachment lifetime.
     */
    MeshProfileOverlay() = default;

    /*! \brief Set the persistent profile polyline (scene coords). Rebuilds the
     *  chainage table used for chainage↔point mapping.
     * @param[in] scenePolyline Value-copied path in prepared canvas X,-Y scene map units; finite
     *   points expected, duplicate points retained.
     * @note Recomputes cumulative Euclidean scene lengths with no geodesic/model-unit conversion or
     *   validity check. Does NOT reset/clamp existing arrow-chainage field, so later paint may
     *   clamp it to new path. No repaint signal; host must update.
     */
    void setPolyline(const QVector<QPointF> &scenePolyline);

    /*! \brief Position the arrow at \p chainage (scene units along the path).
     *  A negative value hides the arrow.
     * @param[in] chainage Raw distance along scene polyline, negative hides; nonfinite unchecked
     *   and comparison controls paint.
     * @note Fewer than2stored points forces hidden minus one; otherwise copies raw value and paint
     *   clamps through chainageToScene. No host update/notification.
     */
    void setArrowChainage(double chainage);

    /*! \brief Map scene point → device pixel, supplied by the canvas. */
    using SceneToPixel = std::function<QPointF(const QPointF &)>;

    /*! \brief Draw the casing + colored line + position arrow in widget/pixel
     *  space. Called by MapCanvas::paintEvent after the QSG mesh composite so
     *  the profile sits on top of every map layer.
     * @param[in] p Borrowed active painter in callback output-pixel coordinate space; state
     *   saved/restored around each pass.
     * @param[in] toPixel Required callable when at least2points exist; empty callback not guarded.
     * @note Draws white5pxcasing/violet2.5pxpolyline and orange-red14×16pxpin when arrow chainage
     *   nonnegative. Does not reset transform, clip, DPR-convert or request repaint; caller aligns
     *   callback/painter spaces. Empty/single-point path draws nothing.
     */
    void paint(QPainter &p, const SceneToPixel &toPixel) const;

    // ── Geometry helpers (chainage ↔ scene point along the polyline) ──────
    /**
     * @return Cached sum of scene Euclidean segment lengths, zero for empty/single point; not
     *   necessarily project-metres length after reprojection. Nonfinite input can propagate.
     */
    [[nodiscard]] double totalLength() const { return m_total; }
    /**
     * @param[in] chainage Finite requested scene distance; clamped0..totalLength for ordinary valid
     *   geometry.
     * @return Piecewise-linear scene point, origin empty, sole point singleton, last point past
     *   end. Segments<=1e-12length choose first endpoint; NaN/malformed chains not explicitly
     *   rejected. No source mutation.
     */
    [[nodiscard]] QPointF chainageToScene(double chainage) const;
    /*! \brief Chainage of the point on the polyline closest to \p scenePt
     *  (orthogonal projection). Used by the map drag tool.
     * @param[in] scenePt Finite scene-map query point in same path frame.
     * @return Scene Euclidean chainage of closest clamped segment projection, zero for fewer
     *   than2points/no finite candidate. Equal-distance ties keep first segment;
     *   length²<=1e-12treated zero-length at first endpoint. No pixel-space tolerance/geodesic
     *   conversion.
     */
    [[nodiscard]] double nearestChainage(const QPointF &scenePt) const;
    /**
     * @return Borrowed owned value vector, valid until setPolyline/destruction; copy before
     *   retaining across changes or independent worker use.
     */
    [[nodiscard]] const QVector<QPointF> &polyline() const { return m_poly; }

private:
    QVector<QPointF> m_poly; ///< scene-coord vertices
    QVector<double> m_chain; ///< cumulative chainage per vertex
    double m_total = 0.0;
    double m_arrowChainage = -1.0; ///< <0 hides the arrow
};

#endif // MESH_PROFILE_OVERLAY_H
