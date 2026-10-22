/**
 * @file      openswmmvisscene.cpp
 * @brief     Central QGraphicsScene subclass that holds all map-layer graphics items.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "map/openswmmvisscene.h"
#include "map/graphicsitems.h"
#include "layers/openswmmvislayer.h"

#include <QGraphicsItem>
#include <QScopedValueRollback>
#include <QStyleOptionGraphicsItem>

OpenSWMMVisScene::OpenSWMMVisScene(QObject *parent) : QGraphicsScene(parent)
{
    setItemIndexMethod(QGraphicsScene::BspTreeIndex);
}

OpenSWMMVisScene::~OpenSWMMVisScene() = default;

void OpenSWMMVisScene::renderSlice(QPainter *painter, const QRectF &target, const QRectF &source,
                                   qreal minimumZ, qreal maximumZ)
{
    QScopedValueRollback<qreal> minimum(m_minimumRenderZ, minimumZ);
    QScopedValueRollback<qreal> maximum(m_maximumRenderZ, maximumZ);
    render(painter, target, source, Qt::IgnoreAspectRatio);
}

void OpenSWMMVisScene::drawItems(QPainter *painter, int count, QGraphicsItem *items[],
                                 const QStyleOptionGraphicsItem options[], QWidget *widget)
{
    QList<QGraphicsItem *> selected;
    QList<QStyleOptionGraphicsItem> selectedOptions;
    selected.reserve(count);
    selectedOptions.reserve(count);
    for (int i = 0; i < count; ++i) {
        const qreal z = items[i]->topLevelItem()->zValue();
        if (z >= m_minimumRenderZ && z < m_maximumRenderZ) {
            selected.append(items[i]);
            selectedOptions.append(options[i]);
        }
    }
    QGraphicsScene::drawItems(painter, selected.size(), selected.data(), selectedOptions.data(),
                              widget);
}

void OpenSWMMVisScene::removeItemsForLayer(OpenSWMMVisLayer *layer)
{
    if (!layer)
        return;

    QList<QGraphicsItem *> toRemove;
    for (QGraphicsItem *item : items()) {
        // Check each known item type for owner layer
        if (auto *n = dynamic_cast<NodeGraphicsItem *>(item)) {
            if (n->ownerLayer() == layer)
                toRemove.append(item);
        } else if (auto *l = dynamic_cast<LinkGraphicsItem *>(item)) {
            if (l->ownerLayer() == layer)
                toRemove.append(item);
        } else if (auto *c = dynamic_cast<CatchmentGraphicsItem *>(item)) {
            if (c->ownerLayer() == layer)
                toRemove.append(item);
        } else if (auto *vp = dynamic_cast<VectorPointItem *>(item)) {
            if (vp->ownerLayer() == layer)
                toRemove.append(item);
        } else if (auto *vl = dynamic_cast<VectorLineItem *>(item)) {
            if (vl->ownerLayer() == layer)
                toRemove.append(item);
        } else if (auto *vg = dynamic_cast<VectorPolygonItem *>(item)) {
            if (vg->ownerLayer() == layer)
                toRemove.append(item);
        } else if (auto *vpp = dynamic_cast<VectorPolygonPathItem *>(item)) {
            // The class every GIS polygon has actually used since the
            // holes work — without this branch polygon items leaked here.
            if (vpp->ownerLayer() == layer)
                toRemove.append(item);
        } else if (auto *rt = dynamic_cast<RasterTileItem *>(item)) {
            if (rt->ownerLayer() == layer)
                toRemove.append(item);
        }
    }

    for (QGraphicsItem *item : toRemove) {
        removeItem(item);
        delete item;
    }
}
