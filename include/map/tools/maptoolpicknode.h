/**
 * @file      maptoolpicknode.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     One-shot map tool that reports the SWMM node under a click.
 *
 *         Pushed onto the canvas by NodePickSession while a dialog or a
 *         property editor waits for the user to point at a node (the inlet
 *         junction's capture node today). Hovering rings the node under the
 *         cursor; a left click (armed on press, committed on release) emits
 *         nodePicked(); Escape emits cancelled(). The tool never mutates the
 *         model and holds no state beyond the hover — the session that owns
 *         it decides what a pick means and when the previous tool comes back.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef MAPTOOLPICKNODE_H
#define MAPTOOLPICKNODE_H

#include "map/tools/maptool.h"

#include <QString>

class SWMMModelLayer;

class OpenSWMMVisMapToolPickNode : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    /**
     * @param[in] canvas Optional borrowed raw canvas; must outlive tool and event/paint use.
     *   QObject parent ownership is separate.
     * @param[in] parent Optional QObject owner; caller owns tool if null.
     * @note GUI-thread only. Sets identity Pick Node; no selection/model edit. Owner/session
     *   decides acceptance and tool restoration; tool remains active after picks/cancel signal.
     */
    explicit OpenSWMMVisMapToolPickNode(MapCanvas *canvas, QObject *parent = nullptr);

    /**
     * @return Crosshair independent of hit/armed state; no direct canvas cursor mutation.
     */
    [[nodiscard]] QCursor cursor() const override;

    /**
     * @brief Clear hover/raw model pointer and armed press, then activate base; no overlay repaint
     *   request.
     */
    void activate() override;
    /**
     * @brief Clear hover/armed state and deactivate base without emitting cancelled or restoring
     *   another tool.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed event; no null guard.
     * @brief Arm only left press with canvas; any other press clears armed state. No selection,
     *   acceptance or hover update.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed release; left+armed+canvas required.
     * @brief Disarm eligible release and hit-test its current position, emitting nodePicked only
     *   for valid node.
     * @note No drag-distance threshold or requirement same press/hover target; background keeps
     *   session waiting. Does not select/edit node, accept event, deactivate or restore previous
     *   tool.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed event when canvas exists.
     * @brief Replace hover by current node hit and invalidate Overlay every move.
     * @note No canvas no-op; hover retains raw model/index and must remain valid until cleared or
     *   paint (topology/layer lifetime caller responsibility).
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Optional borrowed event; Escape handled, null/other passed to base no-op.
     * @brief Emit cancelled and accept Escape; does not itself deactivate, clear hover or restore
     *   prior tool.
     */
    void keyPressEvent(QKeyEvent *event) override;
    /**
     * @param[in] painter Optional active widget-pixel painter; null no-op.
     * @param[in] extent Unused current canvas extent.
     * @param[in] srs Unused borrowed canvas CRS.
     * @brief Paint orange two-pixel nine-pixel-radius ring on hovered node.
     * @note Reads current cached native node by retained raw pointer/index, projects layer→canvas
     *   then to widget pixels; failed transform status ignored with native fallback. Missing cached
     *   coordinate no-op. Saves/restores painter state, no transform/DPR reset or repaint emission;
     *   host must preserve hovered model lifetime.
     */
    void paint(QPainter *painter, const MapExtent &extent,
               const SpatialReferenceSystem *srs) override;

signals:
    /*! A node was clicked. \p nodeIdx is the layer/engine node index.
     * @param[in] layer Borrowed hit model, not owned or lifetime-pinned by signal.
     * @param[in] name Exact selected node ID.
     * @param[in] nodeIdx Current zero-based cached/engine node index, can change after topology
     *   edits.
     * @note Emitted on armed left release after fresh hit; tool/session stays active unless owner
     *   acts. No selection change.
     */
    void nodePicked(SWMMModelLayer *layer, const QString &name, int nodeIdx);
    /*! Escape was pressed.
     * @note Emitted only on Escape here; owner must finish session/restore tool.
     *   Deactivate/destruction/background click do not emit.
     */
    void cancelled();

private:
    struct NodeHit {
        SWMMModelLayer *layer = nullptr;
        QString name;
        int idx = -1;
        /**
         * @return Raw layer nonnull and idx>=0; does not validate name, current topology or object
         *   lifetime.
         */
        [[nodiscard]] bool valid() const { return layer != nullptr && idx >= 0; }
    };
    /*! First qualifying node hit under \p pixel in visible model-layer list order.
     * @param[in] pixel Widget pixel to query; tolerance uses diagonal offset+8pixels converted
     *   through canvas extent.
     * @return First visible SWMMModelLayer in canvas list whose identifyAt reports Node and current
     *   name index resolves, or empty NodeHit. Not nearest across all layers; opacity is not
     *   independently filtered.
     * @note Uses canvas-map coordinates and max axis tolerance; model identify supplies its
     *   native/cache projection rules. Borrows model result; no selection/model mutation or pointer
     *   guarding.
     */
    [[nodiscard]] NodeHit hitNode(const QPoint &pixel) const;

    NodeHit m_hover;
    bool m_armed = false;
};

#endif // MAPTOOLPICKNODE_H
