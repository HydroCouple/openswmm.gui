/*!
 * \file   legenddock.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/panels/legenddock.h"

#include "map/mapcanvas.h"
#include "ui/delegates/legendcolordelegate.h"
#include "ui/models/legendlayertreemodel.h"
#include "ui/widgets/legendoverlay.h"
#include "render/legendoverlaystyle.h"

// Slice S4 P5 — sublayer right-click → LayerStyleDialog routing.
#include "layers/openswmmvislayer.h"
#include "render/isublayer.h"
#include "render/isublayerhost.h"
#include "ui/dialogs/layerstyledialog.h"

#include <QAbstractItemView>
#include <QAction>
#include <QHeaderView>
#include <QMenu>
#include <QTreeView>

namespace openswmmvis::ui {

LegendDock::LegendDock(QWidget *parent)
    : QDockWidget(tr("Legend"), parent)
{
    setObjectName(QStringLiteral("LegendDock"));
    setFeatures(QDockWidget::DockWidgetClosable
              | QDockWidget::DockWidgetMovable
              | QDockWidget::DockWidgetFloatable);

    m_tree = new QTreeView(this);
    m_tree->setAlternatingRowColors(true);
    m_tree->setEditTriggers(QAbstractItemView::AllEditTriggers);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setAllColumnsShowFocus(true);
    // Match QGIS / ArcGIS: click directly on the swatch chip opens the
    // colour picker without a separate selection step.
    m_tree->setItemDelegateForColumn(LegendLayerTreeModel::ColColor,
                                     new LegendColorDelegate(this));

    // Slice S4 P5 — enable right-click on legend rows. The handler builds
    // a context menu that includes "Edit Sublayer Style…" when the row
    // carries a sublayerId.
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QWidget::customContextMenuRequested,
            this, &LegendDock::onCustomContextMenuRequested);

    setWidget(m_tree);
}

void LegendDock::onCustomContextMenuRequested(const QPoint &pos)
{
    if (!m_tree || !m_model) return;
    const QModelIndex idx = m_tree->indexAt(pos);
    if (!idx.isValid()) return;

    // Try to surface a sublayer-style action. The row's SublayerIdRole is
    // non-empty for rows produced by an ISublayer; the parent layer comes
    // from LayerPtrRole. Both are populated by the legend tree model.
    const QString sublayerId =
        idx.data(LegendLayerTreeModel::SublayerIdRole).toString();
    OpenSWMMVisLayer *parentLayer = qvariant_cast<OpenSWMMVisLayer *>(
        idx.data(LegendLayerTreeModel::LayerPtrRole));

    QMenu menu(this);
    QAction *editStyleAct = nullptr;
    if (!sublayerId.isEmpty() && parentLayer) {
        // Slice Z.8 — user-facing vocabulary moves away from "sublayer".
        // The internal sublayerId routing keeps the same data; only the
        // menu wording changes.
        editStyleAct = menu.addAction(tr("Edit Style…"));
    }
    if (menu.isEmpty()) return;

    QAction *picked = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (!picked) return;

    if (picked == editStyleAct) {
        // Slice U-10 — open the unified LayerStyleDialog focused on the
        // sublayer's tab. The dialog walks parentLayer->styleSubjects()
        // and matches routingId == sublayerId to pick the initial tab.
        auto *dlg = new openswmmvis::ui::LayerStyleDialog(
            parentLayer, sublayerId, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    }
}

MapCanvas *LegendDock::canvas() const noexcept
{
    return m_canvas.data();
}

void LegendDock::setCanvas(MapCanvas *canvas)
{
    if (m_canvas == canvas) return;
    m_canvas = canvas;

    // Replace the model so it reads from the new canvas's layer list.
    auto *old = m_model;
    m_model = new LegendLayerTreeModel(canvas, this);

    // Slice BB Phase 8.6.10 / 8.6.16 — bind the shared LegendOverlayStyle
    // so per-item visibility / rename edits route back through the same
    // MVC model the on-canvas overlay paints from.
    if (canvas) {
        auto *overlay = canvas->findChild<LegendOverlay *>(
            QString(), Qt::FindDirectChildrenOnly);
        if (!overlay)
            overlay = new LegendOverlay(canvas);
        m_model->setOverlayStyle(overlay->style());
    }

    m_tree->setModel(m_model);
    m_tree->expandAll();
    if (old) old->deleteLater();

    // Re-expand after every internal reset (rebuildLayerCache fires it).
    connect(m_model, &QAbstractItemModel::modelReset,
            m_tree, &QTreeView::expandAll, Qt::UniqueConnection);

    if (auto *hdr = m_tree->header()) {
        // All columns user-resizable (drag the header dividers). Interactive
        // is mutually exclusive with Stretch, so the name column no longer
        // auto-fills; the widths below are the initial defaults the user can
        // then drag. Disable last-section stretch so the dragged Size width
        // sticks instead of snapping to fill the viewport.
        hdr->setSectionResizeMode(LegendLayerTreeModel::ColItem,  QHeaderView::Interactive);
        hdr->setSectionResizeMode(LegendLayerTreeModel::ColColor, QHeaderView::Interactive);
        hdr->setSectionResizeMode(LegendLayerTreeModel::ColSize,  QHeaderView::Interactive);
        hdr->setStretchLastSection(false);
        hdr->resizeSection(LegendLayerTreeModel::ColItem,  200);
        hdr->resizeSection(LegendLayerTreeModel::ColColor, 80);
        hdr->resizeSection(LegendLayerTreeModel::ColSize,  60);
    }
}

} // namespace openswmmvis::ui
