/*!
 * \file   maptoolmeshprofile.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Canvas map tool that traces a free-form polyline across the 2D mesh
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
    explicit MapToolMeshProfile(MapCanvas *canvas, QObject *parent = nullptr);
    ~MapToolMeshProfile() override = default;

    void activate() override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

    void paint(QPainter *painter,
               const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /*! \brief Emitted when the user finishes a ≥2-vertex trace. The polyline
     *  is in scene coords (sx = mapX, sy = -mapY). */
    void profilePathTraced(const QVector<QPointF> &scenePolyline);

    /*! \brief Status hint piped to the main-window status bar. */
    void statusMessageChanged(const QString &message);

private:
    /*! \brief Map (px,py) → scene (sx,sy) via the layer Y-flip (sx = mx, sy = -my). */
    [[nodiscard]] QPointF pixelToScene_(int px, int py) const;

    void finishTrace_();
    void cancelTrace_();

    bool             m_drawing = false;
    QVector<QPointF> m_scenePts;     // committed vertices (scene coords)
    QPoint           m_cursorPixel;  // live cursor (pixel coords)
};

#endif // OPENSWMMVIS_MAP_TOOLS_MAPTOOLMESHPROFILE_H
