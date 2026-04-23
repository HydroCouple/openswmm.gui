/*!
 * \file   maptooleditvertex.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 * \brief  Interior-vertex editor for SWMM link polylines.
 */

#ifndef MAPTOOLEDITVERTEX_H
#define MAPTOOLEDITVERTEX_H

#include "map/tools/maptool.h"

#include <QPoint>
#include <QPointF>
#include <QString>
#include <QVector>

class SWMMModelLayer;
class LinkGraphicsItem;

/*!
 * \class OpenSWMMVisMapToolEditVertex
 * \brief Edit the interior polyline vertices of a SWMM link.
 * \details
 *   - **Click a link** to activate vertex editing on it. Handles are drawn
 *     at each interior vertex (in overlay paint, not as scene items).
 *   - **Drag a handle** to move the vertex. Commit on release pushes an
 *     EditVertexCommand.
 *   - **Right-click an empty segment** → Insert vertex at the closest point.
 *   - **Right-click a handle** → Delete that vertex.
 *   - **Escape** clears the active link.
 *
 *   When auto-length is enabled on the active project window, the conduit's
 *   length is recomputed from the new polyline and round-tripped through
 *   the engine as part of the same undoable command.
 */
class OpenSWMMVisMapToolEditVertex : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    explicit OpenSWMMVisMapToolEditVertex(MapCanvas *canvas, QObject *parent = nullptr);

    [[nodiscard]] QCursor cursor() const override;

    void activate() override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

    void paint(QPainter *painter,
               const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    void verticesEdited(const QString &linkName, int interiorVertexCount);

private:
    void clearActiveLink();
    LinkGraphicsItem *hitTestLink(const QPoint &pixel) const;
    int               hitTestInteriorHandle(const QPoint &pixel) const;
    void              commitInterior(QVector<QPointF> newInterior);

    SWMMModelLayer   *m_layer   = nullptr;
    int               m_linkIdx = -1;
    QString           m_linkName;

    // The cached interior vertices for the current link in LAYER coords.
    QVector<QPointF>  m_interior;

    // Drag state
    bool              m_dragging   = false;
    int               m_dragVertex = -1;       // index into m_interior
    int               m_pickTol    = 8;        // pixels for handle hit
};

#endif // MAPTOOLEDITVERTEX_H
