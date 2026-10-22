/**
 * @file      maptooladdvirtualnode.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Click-a-conduit tool that inserts a virtual junction by splitting.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
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
    /**
     * @brief Create inactive Add Virtual Junction tool without installing it or editing model.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/events; QObject ownership
     *   separate.
     * @param[in] parent Optional QObject owner, otherwise caller owns tool.
     * @note GUI-thread event/painter use only; constructor does not connect external
     *   selection/status consumers.
     */
    explicit OpenSWMMVisMapToolAddVirtualNode(MapCanvas *canvas, QObject *parent = nullptr);

    /**
     * @return Crosshair independent of hover/gesture state; no direct widget cursor change.
     */
    [[nodiscard]] QCursor cursor() const override;

    /**
     * @brief Clear hover/armed state, activate base and emit insertion instruction status. No
     *   canvas edit or direct repaint.
     */
    void activate() override;
    /**
     * @brief Clear raw hover/armed state, emit empty status and deactivate base. Does not undo
     *   committed splits.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed press; left+canvas arms, every other press disarms.
     * @brief Arm release-based insertion without opening dialogs or editing model; no event
     *   acceptance.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed release; left+armed+canvas required, no drag-distance
     *   threshold.
     * @brief Fresh-pick conduit, generate names and push virtual split command,
     *   selecting/announcing node if it exists afterward.
     * @note Missing hit emits guidance and returns; no free placement. No undo stack deletes
     *   command without execution. Post-check only node-name existence, not virtual role/command
     *   completion, so partial split can still announce success. Clears hover and invalidates
     *   Scene/Overlay after valid hit attempt, emits translated success/failure status. Does not
     *   accept event or deactivate.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed move when canvas exists; widget coordinates.
     * @brief Refresh shared conduit hit and invalidate Overlay every move.
     * @note Retains raw model/index/native point until cleared; caller keeps hit layer/topology
     *   stable through paint. No canvas no-op, no model selection.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] painter Optional active widget-pixel overlay painter; null or invalid hover no-op.
     * @param[in] extent Unused current canvas-map extent.
     * @param[in] srs Unused borrowed current canvas CRS.
     * @brief Draw hover split preview using current model virtual-junction colour, dotted7pixelring
     *   and10pixelcross.
     * @note Projects stored native closest point layer→canvas with ignored status then to pixels;
     *   point may differ from clamped split fraction near conduit ends. Saves/restores painter
     *   state, no DPR/transform reset or repaint; retained raw model must survive.
     */
    void paint(QPainter *painter, const MapExtent &extent,
               const SpatialReferenceSystem *srs) override;

signals:
    /**
     * @param[in] nodeName Generated inserted node ID found in model after attempted split.
     * @param[in] splitLinkName Original conduit ID retained upstream.
     * @param[in] newLinkName Generated downstream half ID; signal does not separately verify it.
     * @note Emitted after typed selection only when nodeIndex resolves; does not independently
     *   guarantee virtual role or all split post-processing succeeded.
     */
    void virtualJunctionAdded(const QString &nodeName, const QString &splitLinkName,
                              const QString &newLinkName);
    /**
     * @param[in] message Translated instruction/result, or empty on deactivate; consumer copies for
     *   later display.
     * @note Synchronous signal from activation/deactivation/release; not an engine-error detail
     *   channel or completed-operation guarantee.
     */
    void statusMessageChanged(const QString &message);

private:
    ConduitSplitPick::ConduitHit m_hover; ///< live preview of the split point
    bool m_armed = false;
};

#endif // MAPTOOLADDVIRTUALNODE_H
