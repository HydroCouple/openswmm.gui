/*!
 * \file   maptooladdlink.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Click-to-draw tool for SWMM links (conduit/pump/orifice/weir/outlet).
 */

#ifndef MAPTOOLADDLINK_H
#define MAPTOOLADDLINK_H

#include "map/tools/maptool.h"

#include <QPointF>
#include <QString>
#include <QVector>

class SWMMModelLayer;

/*!
 * \class OpenSWMMVisMapToolAddLink
 * \brief Two-click polyline tool that creates a SWMM link between two nodes.
 *
 * Interaction:
 *   - Click on (or near) an existing node → anchors the from-node.
 *   - Subsequent left-clicks add intermediate polyline vertices.
 *   - Click on a second node → commits the link.
 *   - Right-click → removes the last intermediate vertex (or cancels if none).
 *   - Escape → cancels without creating anything.
 *
 * Node snapping activates within \c m_snapPx pixels of a node centre.
 * The nearest node within that tolerance is highlighted with a snap
 * indicator ring painted on the overlay channel.
 */
class OpenSWMMVisMapToolAddLink : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    /*!
     * \param canvas     Target canvas.
     * \param linkType   SWMM_LinkType (0=Conduit, 1=Pump, 2=Orifice, 3=Weir, 4=Outlet).
     * \param namePrefix Auto-name prefix ("C", "P", "Or", "W", "Ou").
     */
    /*! \param elementKind  Naming-preference key: "conduit", "pump",
     *                      "orifice", "weir", or "outlet". */
    OpenSWMMVisMapToolAddLink(MapCanvas *canvas, int linkType,
                               const QString &elementKind, QObject *parent = nullptr);

    [[nodiscard]] QCursor cursor() const override;

    void activate()   override;
    void deactivate() override;

    void mousePressEvent  (QMouseEvent *event) override;
    void mouseMoveEvent   (QMouseEvent *event) override;
    void keyPressEvent    (QKeyEvent   *event) override;
    void paint(QPainter *painter, const MapExtent &extent,
               const SpatialReferenceSystem *srs) override;

signals:
    void linkAdded(const QString &name, int linkType,
                   const QString &fromNode, const QString &toNode);

private:
    enum class State { Idle, Drawing };

    [[nodiscard]] SWMMModelLayer *activeModelLayer() const;
    [[nodiscard]] QString nextAvailableName(SWMMModelLayer *layer) const;

    /*! Snap to the nearest node within m_snapPx; returns "" if none found. */
    [[nodiscard]] QString snapToNode(SWMMModelLayer *layer,
                                      double mapX, double mapY,
                                      double *snapX, double *snapY) const;

    void cancel();
    void commit(SWMMModelLayer *layer, const QString &toNodeName,
                double toX, double toY);

    int     m_linkType;
    QString m_elementKind;
    int     m_snapPx = 12; // snap radius in screen pixels

    State            m_state      = State::Idle;
    QString          m_fromNode;
    QPointF          m_fromPt;    // map coords of from-node
    QVector<QPointF> m_vertices;  // intermediate vertices (map coords)
    QPointF          m_cursor;    // current mouse map coords (for rubber-band)
    QString          m_snapTarget; // name of node currently snapped to
    QPointF          m_snapPt;    // map coords of snap target
};

#endif // MAPTOOLADDLINK_H
