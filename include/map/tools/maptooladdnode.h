/*!
 * \file   maptooladdnode.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 * \brief  Click-to-create tool for SWMM nodes.
 */

#ifndef MAPTOOLADDNODE_H
#define MAPTOOLADDNODE_H

#include "map/tools/maptool.h"

#include <QString>

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
    OpenSWMMVisMapToolAddNode(MapCanvas *canvas, int nodeType,
                               QString namePrefix, QObject *parent = nullptr);

    [[nodiscard]] QCursor cursor() const override;

    void mousePressEvent(QMouseEvent *event) override;

signals:
    void nodeAdded(const QString &name, int nodeType, double x, double y);

private:
    [[nodiscard]] SWMMModelLayer *activeModelLayer() const;

    /*!
     * \brief Propose a unique name by walking `<prefix>1`, `<prefix>2`, …
     *        until the engine's nodeIndex lookup returns -1.
     */
    [[nodiscard]] QString nextAvailableName(SWMMModelLayer *layer) const;

    int     m_nodeType;
    QString m_namePrefix;
};

#endif // MAPTOOLADDNODE_H
