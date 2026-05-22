/*!
 * \file   legendpanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Dockable legend that mirrors the active canvas's renderers.
 *
 *         The legend walks each visible layer on the bound MapCanvas and
 *         calls the layer's renderer->legendSymbolItems() (per the
 *         legend-from-renderer rule in §J.5). The map and the legend
 *         therefore share one source of truth — no parallel store.
 *
 *         Today this is the minimum needed to satisfy the toolbar's
 *         "Show Legend" toggle: a tree with one root per layer and one
 *         child row per LegendSymbolItem (swatch + label).
 */
#ifndef OPENSWMMVIS_UI_PANELS_LEGENDPANEL_H
#define OPENSWMMVIS_UI_PANELS_LEGENDPANEL_H

#include <QPointer>
#include <QWidget>

class QTreeWidget;
class MapCanvas;
class OpenSWMMVisLayer;

namespace openswmmvis::ui {

class LegendPanel : public QWidget
{
    Q_OBJECT
public:
    explicit LegendPanel(QWidget *parent = nullptr);

    /*! \brief Bind the panel to a canvas. Pass nullptr to clear. */
    void setCanvas(MapCanvas *canvas);

public slots:
    /*! \brief Rebuild the tree from the current canvas. */
    void refresh();

private:
    void connectLayer(OpenSWMMVisLayer *layer);
    void disconnectLayer(OpenSWMMVisLayer *layer);

    QTreeWidget       *m_tree   = nullptr;
    QPointer<MapCanvas> m_canvas;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_PANELS_LEGENDPANEL_H
