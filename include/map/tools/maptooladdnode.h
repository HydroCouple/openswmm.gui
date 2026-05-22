/*!
 * \file   maptooladdnode.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 * \brief  Click-to-create tool for SWMM nodes.
 */

#ifndef MAPTOOLADDNODE_H
#define MAPTOOLADDNODE_H

#include "map/tools/maptool.h"
#include "map/snapengine.h"

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
 *          Every placement pushes an AddNodeCommand onto the canvas'
 *          MapUndoStack. Engine state must be OPENED or BUILDING —
 *          the tool logs and ignores clicks otherwise.
 */
class OpenSWMMVisMapToolAddNode : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    /*!
     * \param canvas    Target map canvas.
     * \param nodeType  SWMM_NodeType value (0=Junction / 1=Outfall /
     *                  2=Storage / 3=Divider).
     * \param namePrefix Prefix used to auto-generate a unique name.
     */
    /*! \param elementKind  Naming-preference key: "junction", "outfall",
     *                      "storage", or "divider". Used to look up the
     *                      configurable prefix from PreferencesManager. */
    OpenSWMMVisMapToolAddNode(MapCanvas *canvas, int nodeType,
                               const QString &elementKind,
                               QObject *parent = nullptr);

    [[nodiscard]] QCursor cursor() const override;

    void activate()   override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent (QMouseEvent *event) override;
    void paint(QPainter *painter, const MapExtent &extent,
               const SpatialReferenceSystem *srs) override;

    /*!
     * \brief Sets the active terrain layer and node offset used to auto-fill
     *        \c InvertElev when a node is placed.
     * \param layer   Raster layer to sample; nullptr disables terrain assistance.
     * \param offset  Signed value added to terrain Z (negative = below ground).
     */
    /*!
     * \param factor  Conversion factor from raster vertical units to model
     *                vertical units (e.g., 3.28084 when raster is metres and
     *                model is feet).  Defaults to 1.0 (no conversion).
     */
    void setTerrain(GISRasterLayer *layer, double offset, double factor = 1.0);

signals:
    void nodeAdded(const QString &name, int nodeType, double x, double y);

private:
    [[nodiscard]] SWMMModelLayer *activeModelLayer() const;

    /*!
     * \brief Propose a unique name by walking `<prefix>1`, `<prefix>2`, …
     *        until the engine's nodeIndex lookup returns -1.
     */
    [[nodiscard]] QString nextAvailableName(SWMMModelLayer *layer) const;

    int                m_nodeType;
    QString            m_elementKind;
    SnapEngine::Result m_snap;

    GISRasterLayer    *m_terrainLayer  = nullptr;
    double             m_terrainOffset = 0.0;
    double             m_terrainFactor = 1.0; // raster → model vertical unit
};

#endif // MAPTOOLADDNODE_H
