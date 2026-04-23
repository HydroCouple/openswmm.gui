/*!
 * \file   maptoolmovenode.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 * \brief  Drag-to-reposition tool for SWMM nodes.
 */

#ifndef MAPTOOLMOVENODE_H
#define MAPTOOLMOVENODE_H

#include "map/tools/maptool.h"

#include <QPoint>
#include <QString>

class SWMMModelLayer;
class NodeGraphicsItem;
class QGraphicsItem;

/*!
 * \class OpenSWMMVisMapToolMoveNode
 * \brief Drag a SWMM node to a new location.
 * \details Left-click a node to start a drag; release the mouse to commit.
 *          The commit mutates engine + cache through SWMMModelLayer and
 *          pushes a MoveNodeCommand on the canvas' MapUndoStack. When
 *          auto-length is enabled on the active project window, every
 *          conduit whose endpoint is the moved node has its length
 *          recomputed from the new polyline as part of the same command.
 *
 *          Press Escape during a drag to cancel.
 *
 *          The tool only engages when the model layer is displaying in
 *          its native CRS (no live GDAL reprojection transform). Attempts
 *          to start a drag while the canvas is reprojecting the layer
 *          are ignored with a log entry — Phase 0.7 reproject first, then
 *          edit.
 */
class OpenSWMMVisMapToolMoveNode : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    explicit OpenSWMMVisMapToolMoveNode(MapCanvas *canvas, QObject *parent = nullptr);

    [[nodiscard]] QCursor cursor() const override;

    void activate() override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

signals:
    /*!
     * \brief Emitted after a successful node move. Carries the node's
     *        name, new map-CRS coord, and the count of conduits whose
     *        length was auto-recomputed.
     */
    void nodeMoved(const QString &nodeName, double newX, double newY,
                   int autoLengthedCount);

private:
    /*!
     * \brief Looks up the top-most NodeGraphicsItem under the pixel
     *        position, filtered to items owned by a SWMMModelLayer.
     */
    NodeGraphicsItem *hitTestNode(const QPoint &pixel) const;

    /*!
     * \brief Resolves which SWMMModelLayer the drag is editing. If the
     *        layer is currently reprojecting (non-null OGR transform),
     *        returns nullptr so the caller can bail out cleanly.
     */
    SWMMModelLayer *editableLayerFor(NodeGraphicsItem *item) const;

    /*!
     * \brief Updates scene positions (node + any attached link endpoints)
     *        during the drag for immediate visual feedback, without
     *        touching engine state.
     */
    void applyDragPreview(double sceneX, double sceneY);

    /*!
     * \brief Restores scene positions to their pre-drag state.
     */
    void cancelDragPreview();

    bool               m_dragging  = false;
    NodeGraphicsItem  *m_dragItem  = nullptr;
    SWMMModelLayer    *m_layer     = nullptr;
    int                m_nodeIdx   = -1;
    QString            m_nodeName;

    // Pre-drag state (scene coords: y = -layer_y)
    QPointF            m_originalScenePos;
    QVector<QGraphicsItem *> m_attachedLinkItems;  // cached for preview updates
};

#endif // MAPTOOLMOVENODE_H
