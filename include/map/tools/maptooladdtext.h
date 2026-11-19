/**
 * @file      maptooladdtext.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Click-to-place text annotation map tool.
 *
 * Click on empty map → opens AnnotationStyleDialog with defaults; on accept,
 * pushes a full-layer EditTextLayerCommand snapshot to the canvas undo stack. Click on an
 * existing annotation → opens the dialog seeded with that annotation's
 * current style (in-place edit). Cancel leaves the layer untouched.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MAP_TOOLS_MAPTOOLADDTEXT_H
#define OPENSWMMVIS_MAP_TOOLS_MAPTOOLADDTEXT_H

#include "map/tools/maptool.h"
#include <QPointer>
#include <QJsonObject>
#include <QPointF>

class OpenSWMMVisAnnotationLayer;

class OpenSWMMVisMapToolAddText : public OpenSWMMVisMapTool
{
    Q_OBJECT
public:
    /*! \param layer  Annotation target, borrowed and guarded by QPointer; deletion disables edits.
     * @brief Create inactive Edit Text tool without installing it or editing model.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/events; QObject ownership
     *   separate.
     * @param[in] parent Optional QObject owner, otherwise caller owns tool.
     * @note GUI-thread event/painter use only; constructor does not connect external
     *   selection/status consumers.
     */
    explicit OpenSWMMVisMapToolAddText(MapCanvas *canvas, OpenSWMMVisAnnotationLayer *layer,
                                       QObject *parent = nullptr);

    /**
     * @return Crosshair independent of hover/gesture state; no direct widget cursor change.
     */
    [[nodiscard]] QCursor cursor() const override;

    /**
     * @param[in] layer Optional new annotation target guarded by QPointer.
     * @brief Replace target pointer without clearing current gesture/snapshot or notifying host.
     * @note Caller switches only outside gesture or deactivates first; otherwise retained pressed
     *   ID/before snapshot refer to previous target. No ownership transfer/visibility check.
     */
    void setLayer(OpenSWMMVisAnnotationLayer *layer);
    /**
     * @param[in] event Required borrowed press when target/canvas is eligible.
     * @brief Snapshot visible attached target layer on left press and remember hit
     *   annotation/offset for possible drag.
     * @note Requires layer in canvas and visible, but no opacity check. Stores whole-layer JSON
     *   even background click. Annotation hit uses widget pixels; clears pressed ID for background.
     *   No selection/undo/event acceptance at press.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed move with target/pressed ID; left button mask required.
     * @brief Once Manhattan pixel drag threshold reached, pin annotation and update screen offset
     *   from original rendered offset.
     * @note Mutates item live before undo entry, then canvas update. Layer deletion/missing item
     *   no-op; does not independently recheck canvas/layer attachment/visibility, so caller
     *   preserves canvas. Deactivate restores pre-drag layer JSON.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed release with attached visible target/live canvas; left and
     *   nonempty before snapshot required.
     * @brief Commit drag, edit existing text in staged dialog, or transform/place new text from
     *   background release.
     * @note Drag pushes full-layer before/after Move text snapshot; existing item opens staged edit
     *   and pushes only accepted changed JSON. New item transforms canvas→annotation CRS (failure
     *   returns early leaving gesture state), opens dialog, adds on accept and pushes full-layer
     *   Add text snapshot, then emits annotationAdded. Cancel deletes new provisional item; staged
     *   dialog protects existing item.
     * @note Requires nonnull canvas undoStack; push re-restores after snapshot, replacing layer
     *   item identities while stable IDs persist. No AddAnnotationCommand used in current
     *   implementation. Normal completion clears pressed/snapshot/drag state and updates canvas;
     *   rejected early branches keep state. Modal callbacks must preserve tool/canvas/target
     *   lifetime.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed event, no null guard; Escape requires live canvas.
     * @brief Escape clears canvas active tool, causing deactivate and rollback of uncommitted drag.
     * @note Does not restore previous tool, accept event or forward non-Escape keys to base.
     *   Committed edits stay in undo history.
     */
    void keyPressEvent(QKeyEvent *event) override;
    /**
     * @brief Restore whole before-layer JSON only for active drag, clear gesture state and
     *   deactivate base.
     * @note Guarded deleted layer skips restore. No explicit update/repaint after clearing; layer
     *   restoration supplies its normal signals. Other independent layer changes since press can be
     *   overwritten by full snapshot restore.
     */
    void deactivate() override;

signals:
    /*! Emitted after a new annotation is placed. The new item's id is in `id`.
     * @param[in] id Stable ID of newly accepted annotation; item pointer can change during snapshot
     *   command redo.
     * @note Emitted after layer add and undo-stack push; existing edits/drags/cancel do not emit.
     *   Consumer resolves by ID, not retained provisional item pointer.
     */
    void annotationAdded(const QString &id);

private:
    QPointer<OpenSWMMVisAnnotationLayer> m_layer;
    QString m_pressedId;
    QPointF m_pressPixel, m_startOffset;
    QJsonObject m_before;
    bool m_dragged = false;
};

#endif // OPENSWMMVIS_MAP_TOOLS_MAPTOOLADDTEXT_H
