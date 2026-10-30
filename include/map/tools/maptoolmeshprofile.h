/**
 * @file      maptoolmeshprofile.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Canvas map tool that traces a free-form polyline across the 2D mesh
 *         to define a longitudinal profile cross-section.
 *
 * Purely geometric — it captures a scene polyline and emits it; it never
 * touches a layer. So it is also the capture tool for the terrain-toolbar DEM
 * profile (SWMMVisProjectWindow::activateTerrainProfileTool), which routes the
 * same polyline to RasterProfileSampler instead of MeshProfileSampler.
 *
 * Behaviour (mirrors MapToolPick2DCells's lasso idiom):
 *   - Left-click adds a polyline vertex.
 *   - Mouse-move shows a live rubber segment from the last vertex to the cursor.
 *   - Double-click or Enter finishes (needs ≥ 2 vertices) and emits
 *     `profilePathTraced` with the vertices in scene coords (sx = mapX,
 *     sy = -mapY — the layer convention consumed directly by
 *     SWMM2DMeshLayer::sampleZAt / SWMM2DResultsLayer::pickCellAt).
 *   - Right-click undoes the last vertex.
 *   - Escape cancels the in-progress trace.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MAP_TOOLS_MAPTOOLMESHPROFILE_H
#define OPENSWMMVIS_MAP_TOOLS_MAPTOOLMESHPROFILE_H

#include "map/tools/maptool.h"

#include <QPoint>
#include <QPointF>
#include <QString>
#include <QVector>

class MapToolMeshProfile : public OpenSWMMVisMapTool
{
    Q_OBJECT
public:
    /**
     * @brief Create inactive tool without installing on canvas or editing layers.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/event/paint use.
     * @param[in] parent Optional QObject lifetime owner; otherwise caller owns tool.
     * @note All methods/events serialized on GUI thread. Construction does not wire result/status
     *   consumers.
     */
    explicit MapToolMeshProfile(MapCanvas *canvas, QObject *parent = nullptr);
    /**
     * @brief Destroy value trace/base QObject state; does not emit traced/cancelled result or
     *   detach itself from canvas.
     */
    ~MapToolMeshProfile() override = default;

    /**
     * @brief Activate base, clear trace and emit drawing instructions; no explicit overlay
     *   invalidation on this path.
     */
    void activate() override;
    /**
     * @brief Cancel pending trace then deactivate base; cancellation redraws only if trace state
     *   existed, status not cleared here.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed event with live canvas; left/right handled.
     * @brief Left appends raw scene point and arms trace, right removes last vertex and disarms
     *   when empty.
     * @note No snapping, layer lookup, minimum distance or duplicate removal on ordinary press.
     *   Updates Overlay when changed; other buttons untouched, no event acceptance.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed widget move when canvas and active trace exist.
     * @brief Store live cursor pixel and invalidate Overlay, without adding geometry or snapping.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed left event with live canvas and active trace.
     * @brief Remove every exact trailing scene-point duplicate of double-click endpoint, append
     *   endpoint once and finish trace.
     * @note Requires already-drawing state; uses exact coordinates rather than pixel tolerance, and
     *   count requirement is vertices, not distinct/nondegenerate points. No event acceptance.
     */
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed key; Enter/Return finish, Escape cancel, others base
     *   no-op.
     * @brief Finish/cancel current trace without appending live cursor on Enter. Does not accept
     *   handled keys or switch tool.
     */
    void keyPressEvent(QKeyEvent *event) override;

    /**
     * @param[in] painter Optional active widget-pixel painter; null/no canvas/idle/empty no-op.
     * @param[in] canvasExtent Unused current canvas extent.
     * @param[in] canvasSRS Unused borrowed current CRS.
     * @brief Draw dashed blue trace plus live cursor segment and4pixelvertexhandles.
     * @note Stored scene X,-Y inverted back to canvas map for projection; no layer CRS/geodesic
     *   conversion or geometry mutation. Saves/restores painter state, no DPR/transform reset.
     */
    void paint(QPainter *painter, const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /*! \brief Emitted when the user finishes a ≥2-vertex trace. The polyline
     *  is in scene coords (sx = mapX, sy = -mapY).
     * @param[in] scenePolyline Completed copied scene-map path (canvas mapX,-mapY),>=2entries but
     *   possibly duplicate/degenerate; consumer copies before async retention.
     * @note Emitted after internal trace cleared and Overlay invalidated. No layer CRS conversion,
     *   sampling, mesh intersection or engine change done by tool. Consumer owns resulting profile
     *   workflow; tool stays active.
     */
    void profilePathTraced(const QVector<QPointF> &scenePolyline);

    /*! \brief Status hint piped to the main-window status bar.
     * @param[in] message Translated activation instruction or insufficient-vertices hint.
     * @note Cancel/deactivate/success do not explicitly clear status; owner determines display
     *   lifetime.
     */
    void statusMessageChanged(const QString &message);

private:
    /*! \brief Map (px,py) → scene (sx,sy) via the layer Y-flip (sx = mx, sy = -my).
     * @param[in] px Widget pixel X.
     * @param[in] py Widget pixel Y.
     * @return Canvas-map(mx,-my), origin when canvas missing/invalid conversion extent. No
     *   source-layer/mesh CRS transformation or length scaling.
     */
    [[nodiscard]] QPointF pixelToScene_(int px, int py) const;

    /**
     * @brief Emit completed scene polyline when at least2stored vertices; otherwise retain trace
     *   and emit minimum-count hint.
     * @note Copies path, clears state BEFORE signal, invalidates Overlay when canvas lives. Does
     *   not append cursor, reject duplicates/zero length, sample Z or switch tool.
     */
    void finishTrace_();
    /**
     * @brief Clear trace/drawing and invalidate Overlay only when nonempty/active state existed and
     *   canvas lives.
     * @note No result/cancel/status signal, engine undo or active-tool restoration.
     */
    void cancelTrace_();

    bool m_drawing = false;
    QVector<QPointF> m_scenePts; // committed vertices (scene coords)
    QPoint m_cursorPixel;        // live cursor (pixel coords)
};

#endif // OPENSWMMVIS_MAP_TOOLS_MAPTOOLMESHPROFILE_H
