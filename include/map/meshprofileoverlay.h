/*!
 * \file   meshprofileoverlay.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Map-canvas overlay for a 2D-mesh longitudinal profile: the traced
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
    MeshProfileOverlay() = default;

    /*! \brief Set the persistent profile polyline (scene coords). Rebuilds the
     *  chainage table used for chainage↔point mapping. */
    void setPolyline(const QVector<QPointF> &scenePolyline);

    /*! \brief Position the arrow at \p chainage (scene units along the path).
     *  A negative value hides the arrow. */
    void setArrowChainage(double chainage);

    /*! \brief Map scene point → device pixel, supplied by the canvas. */
    using SceneToPixel = std::function<QPointF(const QPointF &)>;

    /*! \brief Draw the casing + colored line + position arrow in widget/pixel
     *  space. Called by MapCanvas::paintEvent after the QSG mesh composite so
     *  the profile sits on top of every map layer. */
    void paint(QPainter &p, const SceneToPixel &toPixel) const;

    // ── Geometry helpers (chainage ↔ scene point along the polyline) ──────
    [[nodiscard]] double  totalLength() const { return m_total; }
    [[nodiscard]] QPointF chainageToScene(double chainage) const;
    /*! \brief Chainage of the point on the polyline closest to \p scenePt
     *  (orthogonal projection). Used by the map drag tool. */
    [[nodiscard]] double  nearestChainage(const QPointF &scenePt) const;
    [[nodiscard]] const QVector<QPointF> &polyline() const { return m_poly; }

private:
    QVector<QPointF>      m_poly;             ///< scene-coord vertices
    QVector<double>       m_chain;            ///< cumulative chainage per vertex
    double                m_total = 0.0;
    double                m_arrowChainage = -1.0;  ///< <0 hides the arrow
};

#endif // MESH_PROFILE_OVERLAY_H
