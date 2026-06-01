/*!
 * \file   legenddock.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BB Phase 8.6.11 / 8.6.16 — dockable legend / per-class
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

namespace openswmmvis::ui {

class LegendLayerTreeModel;

class LegendDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit LegendDock(QWidget *parent = nullptr);
    ~LegendDock() override = default;

    /*! \brief Re-bind to a (possibly null) canvas. Replaces the internal
     *         LegendLayerTreeModel so it reads from the new canvas's
     *         layer list. */
    void setCanvas(MapCanvas *canvas);

    /*! \brief Currently bound canvas, or nullptr if unbound.
     *
     *  Defined out-of-line so QPointer<MapCanvas>::data() doesn't need
     *  to be instantiated in the header (MapCanvas is forward-declared). */
    [[nodiscard]] MapCanvas *canvas() const noexcept;

private slots:
    /*! Slice S4 P5 — right-click handler on the legend tree. If the
     *  clicked row carries a non-empty SublayerIdRole, surfaces an
     *  "Edit Sublayer Style…" menu action that opens SublayerStyleDialog
     *  for the originating sublayer. */
    void onCustomContextMenuRequested(const QPoint &pos);

private:
    QPointer<MapCanvas>  m_canvas;
    QTreeView           *m_tree  = nullptr;
    LegendLayerTreeModel *m_model = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_PANELS_LEGENDDOCK_H
