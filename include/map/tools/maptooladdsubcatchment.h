/**
 * @file      maptooladdsubcatchment.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Polygon draw tool for SWMM subcatchments.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef MAPTOOLADDSUBCATCHMENT_H
#define MAPTOOLADDSUBCATCHMENT_H

#include "map/tools/maptool.h"
#include "map/snapengine.h"

#include <QPointF>
#include <QString>
#include <QVector>

class SWMMModelLayer;

/*!
 * \class OpenSWMMVisMapToolAddSubcatchment
 * \brief Click-to-place polygon vertices; double-click to close and commit.
 *
 * Interaction:
 *   - Left-click → add vertex; rubber-band polygon updates.
 *   - Double-click → add the clicked point as the final vertex, then close
 *     the polygon and commit (minimum 3 vertices).
 *   - Right-click → remove last vertex when more than one exists; otherwise cancel.
 *   - Enter / Return → commit (same as double-click).
 *   - Escape → cancel.
 */
class OpenSWMMVisMapToolAddSubcatchment : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    /**
     * @brief Create inactive network drawing tool without installing it or changing engine.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/events; no ownership
     *   transfer.
     * @param[in] parent Optional QObject owner; caller owns tool if null.
     * @note GUI-thread only. Current raw model is chosen from canvas list during use;
     *   model/terrain/hover lifetimes and topology coherence remain caller responsibility. Undo
     *   stack required for actual creation; absent stack deletes commands without executing.
     */
    explicit OpenSWMMVisMapToolAddSubcatchment(MapCanvas *canvas, QObject *parent = nullptr);

    /**
     * @return Crosshair independent of current snap or drawing phase; no widget cursor change.
     */
    [[nodiscard]] QCursor cursor() const override;

    /**
     * @brief Cancel polygon/snap state and invalidate Overlay before base activate; committed
     *   subcatchments remain.
     */
    void activate() override;
    /**
     * @brief Cancel polygon/snap state and invalidate Overlay before base deactivate; committed
     *   subcatchments remain.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed press when canvas lives; left/right handled.
     * @brief Left appends canvas-frame vertex using latest snap; right removes one vertex when>1,
     *   otherwise cancels.
     * @note First left when not drawing refreshes snap; later clicks reuse last mouse-move
     *   candidate, which can differ from click. Native snap projected layer→canvas with ignored
     *   status. No current model still permits raw canvas polygon. Does not accept event or
     *   suppress duplicate vertices.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed move with canvas.
     * @brief Refresh general snap and canvas-frame rubber cursor, invalidating Overlay even before
     *   first placed vertex.
     * @note Native snap round-trip transform status ignored; no geometry or selection mutation.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed event; left/live canvas only.
     * @brief Refresh snapped final point, remove prior trailing point within2widgetpixels per axis
     *   of raw click, append once and commit.
     * @note Pixel dedup handles platform press/double-click ordering; no topology/self-intersection
     *   validity check. Does not explicitly close by repeating first vertex; polygon model handles
     *   representation.
     */
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed event; Escape cancel, Return/Enter commit, other
     *   unchanged.
     * @brief Cancel or commit existing vertex list without appending current rubber cursor. No
     *   event acceptance/tool switch.
     */
    void keyPressEvent(QKeyEvent *event) override;
    /**
     * @param[in] painter Required active widget-pixel painter only while drawing/nonempty; null not
     *   guarded then.
     * @param[in] extent Unused current canvas extent.
     * @param[in] srs Unused borrowed current canvas CRS.
     * @brief Paint filled green dashed polygon from placed vertices plus cursor, vertex
     *   dots/closing line and generic snap ring.
     * @note Placed geometry uses canvas frame; generic snap ring directly converts native result so
     *   requires compatible frames. Saves/restores painter state, no DPR/transform reset or repaint
     *   request.
     */
    void paint(QPainter *painter, const MapExtent &extent,
               const SpatialReferenceSystem *srs) override;

signals:
    /**
     * @param[in] name Generated desired subcatchment ID; engine existence not verified.
     * @note Emitted after attempted undo push and typed selection, even absent stack/rejected add.
     *   Consumer queries model for actual success.
     */
    void subcatchmentAdded(const QString &name);

private:
    /**
     * @return Borrowed first SWMMModelLayer in canvas list, null without canvas/model. No
     *   visibility, opacity, selected-layer, open-engine or editability filter.
     */
    [[nodiscard]] SWMMModelLayer *activeModelLayer() const;
    /**
     * @param[in] layer Required borrowed model with coherent current names; helper does not reserve
     *   result.
     * @return Preference-prefix plus first unused integer1..99999 for corresponding object kind;
     *   unchecked prefix_X on exhaustion. Node delegates shared helper (which accepts null), other
     *   helpers require live layer. No mutation/cross-kind guarantee.
     */
    [[nodiscard]] QString nextAvailableName(SWMMModelLayer *layer) const;
    /**
     * @brief Clear polygon/drawing/snap state and invalidate Overlay on live canvas.
     * @note Cursor coordinate retained but drawing false; no engine rollback, selection clearing or
     *   active-tool switch.
     */
    void cancel();
    /**
     * @brief With>=3vertices and current model, transform polygon to native frame and enqueue
     *   AddSubcatchmentCommand.
     * @note Too few points/no model cancels. Transform statuses ignored with canvas fallback; no
     *   local area/self-intersection validation. Requires live canvas on viable model path; no
     *   stack deletes command without redo. Selection/added signal still occur, then cancel and
     *   Scene/Overlay invalidation. Current command handles defaults and optional global-unit
     *   auto-area.
     */
    void commit();

    QVector<QPointF> m_vertices; // map coords of placed vertices
    QPointF m_cursor;            // current cursor in map coords (snapped)
    bool m_drawing = false;
    SnapEngine::Result m_snap; // last snap result for paint indicator
};

#endif // MAPTOOLADDSUBCATCHMENT_H
