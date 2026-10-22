/**
 * @file      maptooleditvertex.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Interior-vertex editor for SWMM link polylines.
 *
 * Link polylines are stored inclusive of the from/to node coordinates at
 * indices 0 and n−1.  Only the interior vertices (indices 1..n−2) are shown
 * as editable handles; the endpoints follow their connected nodes and are
 * moved via the Move Node tool.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef MAPTOOLEDITVERTEX_H
#define MAPTOOLEDITVERTEX_H

#include "map/tools/maptool.h"

#include <QPoint>
#include <QPointF>
#include <QString>
#include <QVector>

class SWMMModelLayer;

/*!
 * \class OpenSWMMVisMapToolEditVertex
 * \brief Edit the interior polyline vertices of a SWMM link.
 * \details
 *   - **Click a link** to activate vertex editing on it. Handles are drawn
 *     at each interior vertex (in overlay paint, not as scene items).
 *   - **Drag a handle** to move the vertex. Commit on release pushes an
 *     EditVertexCommand.
 *   - **Right-click an empty segment** → Insert vertex at the closest point.
 *   - **Right-click a handle** → Delete that vertex.
 *   - **Escape** clears the active link.
 *
 *   When auto-length is enabled on the active project window, the conduit's
 *   length is recomputed from the new polyline and round-tripped through
 *   the engine as part of the same undoable command.
 */
class OpenSWMMVisMapToolEditVertex : public OpenSWMMVisMapTool
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
    explicit OpenSWMMVisMapToolEditVertex(MapCanvas *canvas, QObject *parent = nullptr);

    /**
     * @return Crosshair independent of active link/handle/drag state; no widget mutation.
     */
    [[nodiscard]] QCursor cursor() const override;

    /**
     * @brief Discard local pending drag/active link, clear handles/snaps, invalidate Scene/Overlay
     *   as appropriate, then activate base.
     * @note No committed undo replay or active-tool restoration; raw retained layer must still be
     *   alive when cancelling node preview.
     */
    void activate() override;
    /**
     * @brief Discard local pending drag/active link, clear handles/snaps, invalidate Scene/Overlay
     *   as appropriate, then deactivate base.
     * @note No committed undo replay or active-tool restoration; raw retained layer must still be
     *   alive when cancelling node preview.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed mouse event with canvas; left/right handled.
     * @brief Left starts existing interior-handle drag or activates picked link; right
     *   synchronously offers handle delete or nearby segment insertion.
     * @note Handle uses first within8pixelradius; segment insertion uses10horizontal-pixel-derived
     *   map distance and closest full-polyline projection, with no canvas→layer transform. Requires
     *   compatible frames and target lifetime through nested menu loop. No active link/background
     *   left clears editing. Does not accept event or write engine until selected context action
     *   commits.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed event during valid local vertex drag, no button mask
     *   requirement.
     * @brief Move tool-local bend to cursor or nearest model point within
     *   fixed15horizontal-pixel-derived map radius.
     * @note Uses layer snapNearestPoint in native frame and excludes original drag point; no
     *   Preferences snapEnabled/tolerance consultation or canvas→layer conversion. Only local
     *   vector changes, so active link cached outline stays original during drag; invalidates
     *   Overlay.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Borrowed release ignored entirely, may be null.
     * @brief Commit latest tool-local dragged geometry on any release while dragging, then clear
     *   drag/snap flags.
     * @note Does not sample release position/button, so last mouse-move/press point wins. Invalid
     *   retained target returns after clearing gesture; no acceptance.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Ignored borrowed event, may be null; no button check.
     * @brief Clear active link/local drag/handles and invalidate Overlay; pending local edits
     *   discarded, committed engine state retained.
     */
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed key; Escape clears editing and accepts, others unchanged.
     * @brief Discard local uncommitted drag and clear active link on Escape; no committed undo or
     *   active-tool switch.
     */
    void keyPressEvent(QKeyEvent *event) override;

    /**
     * @param[in] painter Required active widget-pixel painter when target/canvas valid; null not
     *   guarded then.
     * @param[in] canvasExtent Unused current canvas extent.
     * @param[in] canvasSRS Unused borrowed current CRS.
     * @brief Draw cached link orange outline, tool-local8pixelsquarebendhandles and green snap
     *   ring/cross while dragging.
     * @note Native cached/local coordinates passed directly to canvas pixel mapping, requiring
     *   compatible frames. Full outline not rebuilt from dragged local bends until commit.
     *   Saves/restores state and antialias hint, no DPR/transform reset or repaint.
     */
    void paint(QPainter *painter, const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /**
     * @param[in] linkName Stored active link ID, not re-resolved for command index.
     * @param[in] interiorVertexCount Requested after vector length, not confirmed accepted engine
     *   bends.
     * @note Emitted after attempted command push even absent stack or rejected mutation, then
     *   Scene/Overlay invalidated. Consumer verifies model when success matters.
     */
    void verticesEdited(const QString &linkName, int interiorVertexCount);

private:
    /*! Pick result — the SWMM layer + link SoA index under the cursor,
     *  or an invalid pair (layer null / linkIdx < 0). Slice R Phase 3
     *  retargeted this away from `LinkGraphicsItem*` to the layer's
     *  `pickAt` API so the tool has no dependency on the retired
     *  per-object placeholder items. */
    struct LinkHit {
        SWMMModelLayer *layer = nullptr;
        int linkIdx = -1;
        QString name;
        /**
         * @return Raw layer nonnull and stored index>=0; no engine/topology/name/lifetime
         *   validation.
         */
        bool valid() const { return layer && linkIdx >= 0; }
    };

    /**
     * @brief Clear raw layer/index/name, local bends, drag vertex and snap flags; invalidate
     *   Overlay on canvas.
     * @note No preview engine/cache mutation was made by local drag, so clearing simply discards
     *   it. Does not clear external selection or undo committed edit.
     */
    void clearActiveLink();
    /**
     * @param[in] pixel Widget query position; tolerance from diagonal+12pixelmapdelta.
     * @return First visible model-layer pickAt hit in a link category
     *   (conduit/pump/orifice/weir/outlet), or empty miss/no canvas. A higher-priority node hit can
     *   prevent nearby link selection within that layer. No opacity filter/global-nearest or
     *   reverse-stack search. Borrowed raw model/index.
     */
    LinkHit pickLink(const QPoint &pixel) const;
    /**
     * @param[in] pixel Widget position compared to local interior vertices mapped directly from
     *   native frame.
     * @return First local bend index within squared8pixelradius, not necessarily nearest; minus one
     *   none. Requires compatible canvas/layer frames; does not query engine or mutate geometry.
     */
    int hitTestInteriorHandle(const QPoint &pixel) const;
    /**
     * @param[in] newInterior Value-copied desired bends in native model frame, excluding endpoints;
     *   no local finite/duplicate validation.
     * @brief Snapshot current cached bends, optionally recompute conduit length and push one
     *   EditVertexCommand.
     * @note Missing target/canvas no-op. Auto-length requires canvas autoLength AND conduit; full
     *   polyline uses current cached endpoints with supplied bends, model-unit length helper. No
     *   stack deletes command without executing, yet local vector becomes requested after-state and
     *   verticesEdited/Scene+Overlay invalidation still occur. Engine failure similarly unverified;
     *   later local edit can diverge until reselect.
     */
    void commitInterior(QVector<QPointF> newInterior);

    SWMMModelLayer *m_layer = nullptr;
    int m_linkIdx = -1;
    QString m_linkName;

    // The cached interior vertices for the current link in LAYER coords.
    QVector<QPointF> m_interior;

    // Drag state
    bool m_dragging = false;
    int m_dragVertex = -1; // index into m_interior
    QPointF m_dragOrigPt;  // pre-drag map position of dragged vertex
    int m_pickTol = 8;     // pixels for handle hit

    // Snap state (set each mouseMoveEvent during drag, cleared on release)
    bool m_snapping = false;
    QPointF m_snapPt;

    static constexpr int kSnapRadiusPx = 15; // pixel radius for snap detection
};

#endif // MAPTOOLEDITVERTEX_H
