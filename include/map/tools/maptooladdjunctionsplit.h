/*!
 * \file   maptooladdjunctionsplit.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 * \brief  Click-a-conduit tool that inserts a plain junction by splitting.
 */

#ifndef MAPTOOLADDJUNCTIONSPLIT_H
#define MAPTOOLADDJUNCTIONSPLIT_H

#include "map/tools/maptool.h"

#include <QPointF>
#include <QString>

class SWMMModelLayer;

/*!
 * \class OpenSWMMVisMapToolAddJunctionSplit
 * \brief Left-click a conduit to split it and insert a regular junction at the
 *        picked point.
 * \details The same interaction as OpenSWMMVisMapToolAddVirtualNode and
 *          OpenSWMMVisMapToolAddInletNode, but the inserted node is an
 *          ordinary junction: the engine call is `swmm_conduit_split` with
 *          `make_virtual = 0`, so no virtual-junction rule validation runs.
 *          That matters because virtual junctions require DYNWAVE routing
 *          (rule 619), making this the only split available to models routed
 *          with steady or kinematic wave.
 *
 *          Conduits only. `swmm_conduit_split` rejects every other link type,
 *          and rightly so: pumps, weirs, orifices and outlets are structures
 *          defined between two nodes, with no length to divide. Clicking one
 *          (or empty canvas) emits a status-bar hint instead of placing a
 *          node — free placement belongs to the ordinary Junction tool.
 *
 *          Every insertion pushes an InsertJunctionSplitCommand; undo re-fuses
 *          the conduit pair.
 */
class OpenSWMMVisMapToolAddJunctionSplit : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    explicit OpenSWMMVisMapToolAddJunctionSplit(MapCanvas *canvas,
                                                QObject *parent = nullptr);

    [[nodiscard]] QCursor cursor() const override;

    void activate()   override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent (QMouseEvent *event) override;
    void paint(QPainter *painter, const MapExtent &extent,
               const SpatialReferenceSystem *srs) override;

signals:
    void junctionSplitAdded(const QString &nodeName,
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

#endif // MAPTOOLADDJUNCTIONSPLIT_H
