/*!
 * \file   maptooladdvirtualnode.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 * \brief  Click-a-conduit tool that inserts a virtual junction by splitting.
 */

#ifndef MAPTOOLADDVIRTUALNODE_H
#define MAPTOOLADDVIRTUALNODE_H

#include "map/tools/conduitsplitpick.h"
#include "map/tools/maptool.h"

#include <QString>

class SWMMModelLayer;

/*!
 * \class OpenSWMMVisMapToolAddVirtualNode
 * \brief Left-click a conduit to split it and insert a virtual junction at
 *        the picked point.
 * \details A virtual junction only exists between exactly two conduits, so
 *          free placement is disabled (decision D-G3 in
 *          workplans/VIRTUAL_JUNCTION_GUI_PLAN_2026-08-01.md): clicking empty
 *          canvas emits a status-bar hint instead of placing a node. The
 *          conduit hit comes from ConduitSplitPick::pickConduit (shared with
 *          the add-node and inlet tools). Every insertion pushes an
 *          InsertVirtualJunctionCommand (engine-side `swmm_conduit_split`;
 *          undo re-fuses). The click is armed on press and committed on
 *          RELEASE, the convention every canvas tool that may open a dialog
 *          or menu follows (see maptoolpick2dcells.h).
 */
class OpenSWMMVisMapToolAddVirtualNode : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    explicit OpenSWMMVisMapToolAddVirtualNode(MapCanvas *canvas,
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
    void virtualJunctionAdded(const QString &nodeName,
                              const QString &splitLinkName,
                              const QString &newLinkName);
    void statusMessageChanged(const QString &message);

private:
    ConduitSplitPick::ConduitHit m_hover;   ///< live preview of the split point
    bool                         m_armed = false;
};

#endif // MAPTOOLADDVIRTUALNODE_H
