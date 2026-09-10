/*!
 * \file   maptooladdnode.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 * \brief  Click-to-create tool for SWMM nodes — free placement, or insertion
 *         on a conduit (which splits it) when the click lands on one.
 */

#ifndef MAPTOOLADDNODE_H
#define MAPTOOLADDNODE_H

#include "map/tools/conduitsplitpick.h"
#include "map/tools/maptool.h"
#include "map/snapengine.h"

#include <QColor>
#include <QString>

class GISRasterLayer;
class SWMMModelLayer;

/*!
 * \class OpenSWMMVisMapToolAddNode
 * \brief Left-click on the canvas to place a new SWMM node.
 * \details The tool is configured with a fixed node type at construction
 *          (Junction / Outfall / Storage / Divider). A numeric suffix on
 *          a default name prefix is auto-assigned to keep the
 *          engine's uniqueness invariant ("J1", "J2", …) unless the
 *          project window provides a custom prefix.
 *
 *          Where the click lands decides what happens
 *          (ADDNODE_SPLIT_REDESIGN_PLAN_2026-09-10.md):
 *            - empty canvas (or snapped to an existing node) → the node is
 *              placed there (AddNodeCommand, terrain-derived invert).
 *            - a conduit → the conduit is split at that point and the node
 *              inserted there (InsertNodeSplitCommand; storage and dividers
 *              are converted from the split junction and get their creation
 *              defaults). Outfalls are refused on a conduit — an outfall must
 *              be terminal — with a status-bar hint and no edit.
 *          Pumps, weirs, orifices and outlets have no length to divide and
 *          never count as a conduit hit. The click is armed on press and
 *          committed on RELEASE (the convention for canvas tools that may
 *          open a dialog — see maptoolpick2dcells.h).
 *
 *          Every placement pushes one command onto the canvas' MapUndoStack.
 *          Engine state must be OPENED or BUILDING — the tool logs and
 *          ignores clicks otherwise.
 */
class OpenSWMMVisMapToolAddNode : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    /*!
     * \param canvas    Target map canvas.
     * \param nodeType  SWMM_NodeType value (0=Junction / 1=Outfall /
     *                  2=Storage / 3=Divider).
     * \param elementKind  Naming-preference key: "junction", "outfall",
     *                     "storage", or "divider". Used to look up the
     *                     configurable prefix from PreferencesManager.
     */
    OpenSWMMVisMapToolAddNode(MapCanvas *canvas, int nodeType,
                               const QString &elementKind,
                               QObject *parent = nullptr);

    [[nodiscard]] QCursor cursor() const override;

    void activate()   override;
    void deactivate() override;

    void mousePressEvent  (QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent   (QMouseEvent *event) override;
    void paint(QPainter *painter, const MapExtent &extent,
               const SpatialReferenceSystem *srs) override;

    /*!
     * \brief Sets the active terrain layer and node offset used to auto-fill
     *        \c InvertElev when a node is placed freely (a node inserted on a
     *        conduit takes the engine's interpolated invert instead).
     * \param layer   Raster layer to sample; nullptr disables terrain assistance.
     * \param offset  Signed value added to terrain Z (negative = below ground).
     * \param factor  Conversion factor from raster vertical units to model
     *                vertical units (e.g., 3.28084 when raster is metres and
     *                model is feet).  Defaults to 1.0 (no conversion).
     */
    void setTerrain(GISRasterLayer *layer, double offset, double factor = 1.0);

    /*! \brief Whether this tool's node kind may be inserted mid-conduit —
     *         every kind but OUTFALL. */
    [[nodiscard]] bool kindCanSplitConduit() const;

    [[nodiscard]] int nodeType() const { return m_nodeType; }

signals:
    void nodeAdded(const QString &name, int nodeType, double x, double y);
    /*! Emitted when a placement split a conduit (in addition to nodeAdded). */
    void nodeInsertedOnConduit(const QString &nodeName,
                               const QString &splitLinkName,
                               const QString &newLinkName);
    void statusMessageChanged(const QString &message);

private:
    [[nodiscard]] SWMMModelLayer *activeModelLayer() const;

    /*!
     * \brief Propose a unique name by walking `<prefix>1`, `<prefix>2`, …
     *        until the engine's nodeIndex lookup returns -1.
     */
    [[nodiscard]] QString nextAvailableName(SWMMModelLayer *layer) const;

    /*! Human label for status messages ("junction", "storage node", …). */
    [[nodiscard]] QString kindLabel() const;
    /*! The placed symbol's fill colour, for the hover marker. */
    [[nodiscard]] QColor markerColor(const SWMMModelLayer *layer) const;

    void commitFreePlacement(SWMMModelLayer *layer, double mapX, double mapY);
    void commitSplit(const ConduitSplitPick::ConduitHit &hit);

    int                m_nodeType;
    QString            m_elementKind;
    SnapEngine::Result m_snap;
    ConduitSplitPick::ConduitHit m_hover;   ///< conduit under the cursor, if any
    bool               m_armed = false;

    GISRasterLayer    *m_terrainLayer  = nullptr;
    double             m_terrainOffset = 0.0;
    double             m_terrainFactor = 1.0; // raster → model vertical unit
};

#endif // MAPTOOLADDNODE_H
