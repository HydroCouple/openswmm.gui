/*!
 * \file   legendpanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/panels/legendpanel.h"

#include "layers/gisrasterlayer.h"
#include "layers/gisvectorlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/swmmresultslayer.h"
#include "map/mapcanvas.h"
#include "render/ifeaturerenderer.h"
#include "render/irasterrenderer.h"
#include "render/legendsymbolitem.h"

#include <QHeaderView>
#include <QPainter>
#include <QPixmap>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace openswmmvis::ui {

namespace {

QColor firstSymbolColor(const OpenSWMM::Render::SymbolStyle &style)
{
    for (const auto &layer : style.layers)
    {
        const auto it = layer.props.constFind(QStringLiteral("color"));
        if (it != layer.props.constEnd())
        {
            QColor c(it.value().toString());
            if (c.isValid()) return c;
        }
    }
    return QColor(Qt::gray);
}

QIcon swatchIcon(const OpenSWMM::Render::LegendSymbolItem &item)
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor c = firstSymbolColor(item.symbol);
    p.setBrush(c);
    p.setPen(QPen(c.darker(140), 1.0));
    p.drawRect(2, 4, 12, 8);
    return QIcon(pm);
}

QList<OpenSWMM::Render::LegendSymbolItem> legendItemsFor(OpenSWMMVisLayer *layer)
{
    using namespace OpenSWMM::Render;

    if (auto *l = qobject_cast<SWMMResultsLayer *>(layer); l && l->renderer())
        return l->renderer()->legendSymbolItems();
    if (auto *l = qobject_cast<SWMM2DResultsLayer *>(layer); l && l->renderer())
        return l->renderer()->legendSymbolItems();
    if (auto *l = qobject_cast<SWMM2DMeshLayer *>(layer); l && l->renderer())
        return l->renderer()->legendSymbolItems();
    if (auto *l = qobject_cast<GISVectorLayer *>(layer); l && l->renderer())
        return l->renderer()->legendSymbolItems();
    if (auto *l = qobject_cast<GISRasterLayer *>(layer); l && l->rasterRenderer())
        return l->rasterRenderer()->legendSymbolItems();
    return {};
}

} // namespace

LegendPanel::LegendPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setSectionResizeMode(QHeaderView::Stretch);
    lay->addWidget(m_tree);
}

void LegendPanel::setCanvas(MapCanvas *canvas)
{
    if (m_canvas == canvas) return;

    if (m_canvas) {
        disconnect(m_canvas, nullptr, this, nullptr);
        for (OpenSWMMVisLayer *l : m_canvas->layers())
            disconnectLayer(l);
    }

    m_canvas = canvas;

    if (m_canvas) {
        connect(m_canvas, &MapCanvas::layerAdded,        this, [this](OpenSWMMVisLayer *l) {
            connectLayer(l);
            refresh();
        });
        connect(m_canvas, &MapCanvas::layerRemoved,      this, [this](OpenSWMMVisLayer *l) {
            disconnectLayer(l);
            refresh();
        });
        connect(m_canvas, &MapCanvas::layerOrderChanged, this, &LegendPanel::refresh);
        for (OpenSWMMVisLayer *l : m_canvas->layers())
            connectLayer(l);
    }

    refresh();
}

void LegendPanel::connectLayer(OpenSWMMVisLayer *layer)
{
    if (!layer) return;
    connect(layer, &OpenSWMMVisLayer::visibilityChanged, this, &LegendPanel::refresh,
            Qt::UniqueConnection);
    connect(layer, &OpenSWMMVisLayer::nameChanged,       this, &LegendPanel::refresh,
            Qt::UniqueConnection);
    connect(layer, &OpenSWMMVisLayer::repaintRequested,  this, &LegendPanel::refresh,
            Qt::UniqueConnection);
}

void LegendPanel::disconnectLayer(OpenSWMMVisLayer *layer)
{
    if (!layer) return;
    disconnect(layer, nullptr, this, nullptr);
}

void LegendPanel::refresh()
{
    m_tree->clear();
    if (!m_canvas) return;

    const auto layers = m_canvas->layers();
    // Top to bottom in the legend mirrors top of stack first.
    for (int i = layers.size() - 1; i >= 0; --i)
    {
        OpenSWMMVisLayer *layer = layers.at(i);
        if (!layer || !layer->isVisible()) continue;

        auto *layerItem = new QTreeWidgetItem(m_tree);
        layerItem->setText(0, layer->name());
        layerItem->setFirstColumnSpanned(true);

        for (const auto &row : legendItemsFor(layer))
        {
            if (!row.visible) continue;
            auto *child = new QTreeWidgetItem(layerItem);
            child->setText(0, row.effectiveLabel());
            child->setIcon(0, swatchIcon(row));
        }
        layerItem->setExpanded(true);
    }
}

} // namespace openswmmvis::ui
