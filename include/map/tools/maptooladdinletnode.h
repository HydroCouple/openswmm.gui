/*!
 * \file   maptooladdinletnode.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 * \brief  Click-a-street-conduit tool that inserts a configured inlet junction.
 */

#ifndef MAPTOOLADDINLETNODE_H
#define MAPTOOLADDINLETNODE_H

#include "map/tools/conduitsplitpick.h"
#include "map/tools/maptool.h"

#include <QString>

class SWMMModelLayer;

/*!
 * \class OpenSWMMVisMapToolAddInletNode
 * \brief Left-click a STREET conduit to split it and insert an inlet junction
 *        at the picked point.
 * \details Structurally the virtual-junction tool
 *          (OpenSWMMVisMapToolAddVirtualNode) with two additions:
 *
 *          - the hit must be a STREET conduit. An inlet junction models a
 *            gutter inlet, so a pipe is not a legal host; clicking one emits a
 *            status hint instead of splitting. (Drop inlets on RECT_OPEN /
 *            TRAPEZOIDAL channels are an inlet-USAGE case, edited from the
 *            conduit's Inlets row — not a node insertion.)
 *          - the insertion is configured BEFORE it happens (decision D-G6):
 *            a modal InletJunctionSetupDialog collects the inlet design and
 *            capture node, because the engine requires both for the node to
 *            validate. The dialog opens from the mouse RELEASE (the click is
 *            armed on press) — starting a modal session while the button is
 *            still down freezes input on macOS (see maptoolpick2dcells.h).
 *
 *          Each insertion pushes an InsertInletJunctionCommand (engine-side
 *          `swmm_conduit_split_inlet`; undo re-fuses and drops the usage row).
 */
class OpenSWMMVisMapToolAddInletNode : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    explicit OpenSWMMVisMapToolAddInletNode(MapCanvas *canvas,
                                            QObject *parent = nullptr);

    [[nodiscard]] QCursor cursor() const override;

    void activate()   override;
    void deactivate() override;

    void mousePressEvent  (QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent   (QMouseEvent *event) override;
    void paint(QPainter *painter, const MapExtent &extent,
               const SpatialReferenceSystem *srs) override;

signals:
    void inletJunctionAdded(const QString &nodeName,
                            const QString &splitLinkName,
                            const QString &newLinkName);
    void statusMessageChanged(const QString &message);

private:
    ConduitSplitPick::ConduitHit m_hover;   ///< live preview of the split point
    bool                         m_armed = false;
};

#endif // MAPTOOLADDINLETNODE_H
