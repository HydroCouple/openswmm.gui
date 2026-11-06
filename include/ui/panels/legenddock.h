/**
 * @file      legenddock.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BB Phase 8.6.11 / 8.6.16 — dockable legend / per-class
 *         style editor panel.
 *
 *         LegendDock hosts a QTreeView over LegendLayerTreeModel showing
 *         every visible layer's legend rows in a Layer → Item hierarchy.
 *         The Color column is click-to-edit via LegendColorDelegate;
 *         each edit pushes a SetRendererClassColorCommand onto the
 *         canvas's MapUndoStack so Ctrl+Z reverses uniformly across the
 *         dock, the on-canvas legend overlay, and the canvas itself.
 *
 *         The dock binds to a MapCanvas via setCanvas(); pass nullptr
 *         to unbind. It survives MDI tab switches the same way
 *         LegendOverlay::setCanvas does.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_PANELS_LEGENDDOCK_H
#define OPENSWMMVIS_UI_PANELS_LEGENDDOCK_H

#include <QDockWidget>
#include <QPointer>

// Qt 6's QPointer<T> requires T to be a complete QObject-derived type so
// the static_cast<QObject *> inside QPointer::operator= compiles. A bare
// forward declaration (`class MapCanvas;`) is no longer enough.
#include "map/mapcanvas.h"

class QTreeView;

namespace openswmmvis::ui
{

class LegendLayerTreeModel;

class LegendDock : public QDockWidget
{
    Q_OBJECT
public:
    /**
     * @param[in] parent Optional QWidget owner.
     * @brief Build owned single-selection legend tree/colour delegate/context menu routing,
     *   initially without legend model or canvas. GUI-thread only; no rendering edits at
     *   construction.
     */
    explicit LegendDock(QWidget *parent = nullptr);
    /**
     * @brief Release owned tree/model/delegate hierarchy/connections; borrowed canvas and
     *   canvas-owned legend overlay remain alive independently. No save/apply command.
     */
    ~LegendDock() override = default;

    /*! \brief Re-bind to a (possibly null) canvas. Replaces the internal
     *         LegendLayerTreeModel so it reads from the new canvas's
     *         layer list.
     * @param[in] canvas Optional borrowed guarded MapCanvas; null unbinds.
     * @note Pointer equality no-op, including initial null. Otherwise build new dock-owned
     *   LegendLayerTreeModel, bind shared direct-child LegendOverlay style (create canvas-owned
     *   overlay if absent), replace tree model, expand and hook future reset→expand. Old model
     *   deleteLater; columns reset to interactive defaults 200/80/60 with last-section stretch
     *   disabled. No canvas ownership transfer or immediate style edit. Destruction nullifies guard
     *   without automatically rebuilding tree.
     */
    void setCanvas(MapCanvas *canvas);

    /*! \brief Currently bound canvas, or nullptr if unbound.
     *
     *  Defined out-of-line so QPointer<MapCanvas>::data() doesn't need
     *  to be instantiated in the header (MapCanvas is forward-declared).
     * @return Borrowed guarded binding or null; does not expose model/overlay ownership.
     */
    [[nodiscard]] MapCanvas *canvas() const noexcept;

private slots:
    /*! Slice S4 P5 — right-click handler on the legend tree. If the
     *  clicked row carries a non-empty SublayerIdRole, surfaces an
     *  "Edit Style…" menu action that opens the unified LayerStyleDialog
     *  routed to the originating sublayer.
     * @param[in] pos Tree viewport pixel for indexAt and popup global mapping.
     * @note Valid row with nonempty SublayerIdRole and parent layer yields modal menu. Picked Edit
     *   Style creates dock-owned/delete-on-close MODELESS LayerStyleDialog routed by ID. Does not
     *   validate parent subtype/sublayer existence itself; raw row/model/layer must remain valid
     *   through nested menu event loop. Background/missing/cancel no-op; dialog owns actual
     *   edits/undo.
     */
    void onCustomContextMenuRequested(const QPoint &pos);

private:
    QPointer<MapCanvas> m_canvas;
    QTreeView *m_tree = nullptr;
    LegendLayerTreeModel *m_model = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_PANELS_LEGENDDOCK_H
