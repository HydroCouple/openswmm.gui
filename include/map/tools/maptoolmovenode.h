/**
 * @file      maptoolmovenode.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Drag-to-reposition tool for SWMM nodes.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef MAPTOOLMOVENODE_H
#define MAPTOOLMOVENODE_H

#include "map/tools/maptool.h"

#include <QPoint>
#include <QPointF>
#include <QString>

class SWMMModelLayer;

/*!
 * \class OpenSWMMVisMapToolMoveNode
 * \brief Drag a SWMM node or rain gage to a new location.
 * \details Left-click a node (or rain gage) to start a drag; release the
 *          mouse to commit. The commit mutates engine + cache through
 *          SWMMModelLayer and pushes a MoveNodeCommand (MoveGageCommand
 *          for a gage) on the canvas' MapUndoStack. When
 *          auto-length is enabled on the active project window, every
 *          conduit whose endpoint is the moved node has its length
 *          recomputed from the new polyline as part of the same command;
 *          gages have no links, so no auto-length leg.
 *
 *          Press Escape during a drag to cancel.
 *
 *          Post Slice R Phase 3: hit-testing and live preview go through
 *          the layer's `pickAt` / `previewNodeMove` — no dependency on
 *          per-object `NodeGraphicsItem` placeholders (those were
 *          retired once every interactive tool moved to the layer API).
 */
class OpenSWMMVisMapToolMoveNode : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    /**
     * @brief Create inactive geometry-edit tool without selection, engine write or canvas
     *   installation.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/events/undo commands.
     * @param[in] parent Optional QObject owner, otherwise caller owns tool.
     * @note GUI-thread only. Active model pointers/indices are raw and remain
     *   caller-lifetime/topology responsibility through drag/menu/replay; no engine-handle identity
     *   guard. Current editing calculations pass canvas-map coordinates directly to native model
     *   geometry helpers, so canvas and layer frames must be compatible for correct placement.
     */
    explicit OpenSWMMVisMapToolMoveNode(MapCanvas *canvas, QObject *parent = nullptr);

    /**
     * @return SizeAll cursor regardless of dragging/eligible hit, no widget mutation.
     */
    [[nodiscard]] QCursor cursor() const override;

    /**
     * @brief Restore cached pre-drag coordinates and clear drag target, invalidate Scene/Overlay as
     *   appropriate, then activate base.
     * @note No committed undo replay or active-tool restoration; raw retained layer must still be
     *   alive when cancelling node preview.
     */
    void activate() override;
    /**
     * @brief Restore cached pre-drag coordinates and clear drag target, invalidate Scene/Overlay as
     *   appropriate, then deactivate base.
     * @note No committed undo replay or active-tool restoration; raw retained layer must still be
     *   alive when cancelling node preview.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed press; left with canvas queries target.
     * @brief Pick first visible model-layer node/gage, retain raw target and capture native cached
     *   coordinate before preview.
     * @note Uses20pixel-derived map tolerance through pickAt, accepts network-node/gage categories
     *   only, no selection change or event acceptance. Getter status ignored, so failed cache read
     *   can retain previous stored original coordinate. Does not enforce engine editability or
     *   capture version.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed move while dragging; no button mask checked.
     * @brief Convert pixel to canvas-map coordinate and update model cache-only preview, including
     *   attached node-link endpoints.
     * @note No canvas→layer conversion, snapping or engine write. Missing canvas conversion yields
     *   initialized origin; proper host dispatch requires live canvas. Scene/Overlay invalidated by
     *   preview.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed release while dragging; any button ends gesture, release
     *   position used.
     * @brief Restore original cache preview then enqueue engine-backed node/gage move and clear
     *   drag state.
     * @note Gage has no link-length leg. Node with canvas autoLength gathers attached CONDUITS
     *   only, snapshots current engine length and computes new full native-polyline lengths after
     *   substituting release coordinates. Current canvas/native frame compatibility required.
     * @note No stack deletes command without redo, leaving original cache restored; nodeMoved still
     *   emits after attempted push with requested coordinates and record count. Command mutation
     *   statuses ignored. Scene/Overlay invalidated on completion; no release acceptance or
     *   zero-distance suppression.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed key when dragging; Escape handled, other/idle untouched.
     * @brief Restore cached original coordinate and clear drag on Escape, then accept key; no
     *   engine undo or tool switch.
     */
    void keyPressEvent(QKeyEvent *event) override;

signals:
    /*!
     * \brief Emitted after an attempted node/gage move. Carries the object's
     *        name, new map-CRS coord, and the count of conduits whose
     *        length was auto-recomputed.
     * @param[in] nodeName Target node OR rain-gage ID retained from press.
     * @param[in] newX Requested release canvas-map X, passed directly as native model X.
     * @param[in] newY Requested release canvas-map Y, passed directly as native model Y.
     * @param[in] autoLengthedCount Number of conduit length records attempted, zero for gage; not
     *   confirmed accepted setters.
     * @note Emitted after attempted command even absent stack/rejected move; does not certify
     *   success. Raw target state is cleared after synchronous signal.
     */
    void nodeMoved(const QString &nodeName, double newX, double newY, int autoLengthedCount);

private:
    /*! Layer + SoA index hit by the current drag, or invalid. `isGage`
     *  distinguishes the rain-gage cache from the node cache — the two
     *  are separate SoAs with separate move APIs. */
    struct NodeHit {
        SWMMModelLayer *layer = nullptr;
        int nodeIdx = -1;
        QString nodeName;
        bool isGage = false;
        /**
         * @return Raw layer nonnull and stored index>=0; no engine/topology/name/lifetime
         *   validation.
         */
        bool valid() const { return layer && nodeIdx >= 0; }
    };

    /*! Pick the first qualifying visible-model SWMM node or rain gage through
     *  the layer's `pickAt` API (same tier/priority as identifyAt —
     *  delegated point priority selects a node/gage). Links and
     *  subcatchments are ignored.
     * @param[in] pixel Widget position; tolerance max axis map delta of diagonal+20pixels.
     * @return First qualifying Node/Gage hit in visible model canvas-list order, empty on miss/no
     *   canvas; not global nearest/topmost reverse walk. Delegated pickAt priority determines point
     *   category. Opacity not independently filtered, raw pointer borrowed. No selection or
     *   geometry mutation.
     */
    NodeHit pickNode(const QPoint &pixel) const;

    /*! Live preview update: rewrites the cached SoA position for the
     *  node being dragged (and the attached link endpoints). No engine
     *  write — that happens on release via MoveNodeCommand.
     * @param[in] mapX Desired native cache X; current caller supplies canvas X, requiring
     *   compatible frames.
     * @param[in] mapY Desired native cache Y; no scene Y flip.
     * @note Requires stored dragging/live raw layer/nonnegative index, else no-op. Dispatches
     *   previewGageMove/previewNodeMove, ignores status and invalidates Scene/Overlay on canvas. No
     *   engine write, length recomputation or undo.
     */
    void applyDragPreview(double mapX, double mapY);

    /*! Roll the layer's cached coord back to the pre-drag position.
     * @note If dragging/live target, restore captured native cached coordinate through preview
     *   helper, then clear raw layer/index/name/gage/drag state. Invalidates Scene/Overlay even
     *   idle when canvas exists. No engine mutation/rollback of committed command.
     */
    void cancelDragPreview();

    bool m_dragging = false;
    SWMMModelLayer *m_layer = nullptr;
    int m_nodeIdx = -1;
    QString m_nodeName;
    bool m_isGage = false;

    // Pre-drag map-CRS coord (for Escape / cancel rollback).
    double m_originalMapX = 0.0;
    double m_originalMapY = 0.0;
};

#endif // MAPTOOLMOVENODE_H
