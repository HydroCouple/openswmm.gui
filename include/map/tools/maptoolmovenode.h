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
#include <QPointF>
#include <QString>

class SWMMModelLayer;

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
 *          Post Slice R Phase 3: hit-testing and live preview go through
 *          the layer's `pickAt` / `previewNodeMove` — no dependency on
 *          per-object `NodeGraphicsItem` placeholders (those were
 *          retired once every interactive tool moved to the layer API).
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
    /*! Layer + SoA node index hit by the current drag, or invalid. */
    struct NodeHit {
        SWMMModelLayer *layer    = nullptr;
        int             nodeIdx  = -1;
        QString         nodeName;
        bool valid() const { return layer && nodeIdx >= 0; }
    };

    /*! Pick the top-most SWMM node under the pixel through the layer's
     *  `pickAt` API. Rain-gage hits are filtered out — the MoveNode
     *  tool only edits network nodes. */
    NodeHit pickNode(const QPoint &pixel) const;

    /*! Live preview update: rewrites the cached SoA position for the
     *  node being dragged (and the attached link endpoints). No engine
     *  write — that happens on release via MoveNodeCommand. */
    void applyDragPreview(double mapX, double mapY);

    /*! Roll the layer's cached coord back to the pre-drag position. */
    void cancelDragPreview();

    bool               m_dragging  = false;
    SWMMModelLayer    *m_layer     = nullptr;
    int                m_nodeIdx   = -1;
    QString            m_nodeName;

    // Pre-drag map-CRS coord (for Escape / cancel rollback).
    double             m_originalMapX = 0.0;
    double             m_originalMapY = 0.0;
};

#endif // MAPTOOLMOVENODE_H
