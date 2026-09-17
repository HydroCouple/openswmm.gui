/*!
 * \file   maptoolpicknode.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  One-shot map tool that reports the SWMM node under a click.
 *
 *         Pushed onto the canvas by NodePickSession while a dialog or a
 *         property editor waits for the user to point at a node (the inlet
 *         junction's capture node today). Hovering rings the node under the
 *         cursor; a left click (armed on press, committed on release) emits
 *         nodePicked(); Escape emits cancelled(). The tool never mutates the
 *         model and holds no state beyond the hover — the session that owns
 *         it decides what a pick means and when the previous tool comes back.
 */

#ifndef MAPTOOLPICKNODE_H
#define MAPTOOLPICKNODE_H

#include "map/tools/maptool.h"

#include <QString>

class SWMMModelLayer;

class OpenSWMMVisMapToolPickNode : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    explicit OpenSWMMVisMapToolPickNode(MapCanvas *canvas, QObject *parent = nullptr);

    [[nodiscard]] QCursor cursor() const override;

    void activate()   override;
    void deactivate() override;

    void mousePressEvent  (QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent   (QMouseEvent *event) override;
    void keyPressEvent    (QKeyEvent *event) override;
    void paint(QPainter *painter, const MapExtent &extent,
               const SpatialReferenceSystem *srs) override;

signals:
    /*! A node was clicked. \p nodeIdx is the layer/engine node index. */
    void nodePicked(SWMMModelLayer *layer, const QString &name, int nodeIdx);
    /*! Escape was pressed. */
    void cancelled();

private:
    struct NodeHit {
        SWMMModelLayer *layer = nullptr;
        QString         name;
        int             idx = -1;
        [[nodiscard]] bool valid() const { return layer != nullptr && idx >= 0; }
    };
    /*! Nearest node under \p pixel across the visible model layers. */
    [[nodiscard]] NodeHit hitNode(const QPoint &pixel) const;

    NodeHit m_hover;
    bool    m_armed = false;
};

#endif // MAPTOOLPICKNODE_H
