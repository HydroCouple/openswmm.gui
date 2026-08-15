/*!
 * \file   maptooladdvirtualnode.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 * \brief  Click-a-conduit tool that inserts a virtual junction by splitting.
 */

#ifndef MAPTOOLADDVIRTUALNODE_H
#define MAPTOOLADDVIRTUALNODE_H

#include "map/tools/maptool.h"

#include <QPointF>
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
 *          conduit hit is resolved with the same pickAt() hit-test the
 *          vertex editor uses; the normalized split position t comes from
 *          the closest point on the conduit's vertex-aware polyline. Every
 *          insertion pushes an InsertVirtualJunctionCommand (engine-side
 *          `swmm_conduit_split`; undo re-fuses).
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

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent (QMouseEvent *event) override;
    void paint(QPainter *painter, const MapExtent &extent,
               const SpatialReferenceSystem *srs) override;

signals:
    void virtualJunctionAdded(const QString &nodeName,
                              const QString &splitLinkName,
                              const QString &newLinkName);
    void statusMessageChanged(const QString &message);

private:
    struct ConduitHit {
        SWMMModelLayer *layer   = nullptr;
        int             linkIdx = -1;      ///< SoA/engine conduit index
        QString         name;
        double          t = 0.5;           ///< normalized polyline position
        QPointF         point;             ///< closest point (layer CRS)
        bool valid() const { return layer != nullptr && linkIdx >= 0; }
    };

    /*! \brief Hit-test conduits only; computes t and the marker point. */
    [[nodiscard]] ConduitHit pickConduit(const QPoint &pixel) const;

    [[nodiscard]] QString nextNodeName(SWMMModelLayer *layer) const;
    [[nodiscard]] QString nextLinkName(SWMMModelLayer *layer,
                                       const QString &baseName) const;

    ConduitHit m_hover;     ///< live preview of the split point
};

#endif // MAPTOOLADDVIRTUALNODE_H
