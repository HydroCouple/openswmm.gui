/*!
 * \file   maptooladdinletnode.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 * \brief  Click-a-street-conduit tool that inserts a configured inlet junction.
 */

#ifndef MAPTOOLADDINLETNODE_H
#define MAPTOOLADDINLETNODE_H

#include "map/tools/maptool.h"

#include <QPointF>
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
 *            validate.
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

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent (QMouseEvent *event) override;
    void paint(QPainter *painter, const MapExtent &extent,
               const SpatialReferenceSystem *srs) override;

signals:
    void inletJunctionAdded(const QString &nodeName,
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
        bool            isStreet = false;  ///< cross section is SWMM_XSECT_STREET
        bool valid() const { return layer != nullptr && linkIdx >= 0; }
    };

    /*! \brief Hit-test conduits only; computes t, the marker point and the
     *         STREET flag. */
    [[nodiscard]] ConduitHit pickConduit(const QPoint &pixel) const;

    [[nodiscard]] QString nextNodeName(SWMMModelLayer *layer) const;
    [[nodiscard]] QString nextLinkName(SWMMModelLayer *layer,
                                       const QString &baseName) const;

    ConduitHit m_hover;     ///< live preview of the split point
};

#endif // MAPTOOLADDINLETNODE_H
