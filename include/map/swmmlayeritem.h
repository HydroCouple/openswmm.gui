/*!
 * \file   swmmlayeritem.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Batched QGraphicsItem that renders all SWMM network geometry for
 *         one SWMMModelLayer in a single paint() call. Replaces the former
 *         approach of one QGraphicsItem per node / link / subcatchment /
 *         gage, which doesn't scale beyond tens of thousands of objects.
 *
 * Design notes
 * ============
 * - One SWMMLayerItem per SWMMModelLayer, added to the overlay scene by
 *   SWMMModelLayer::populateScene().
 * - boundingRect() is the scene-space extent of the cached geometry — the
 *   scene's BSP indexes this once (cheap) and only calls paint() when the
 *   view's exposed rect intersects it.
 * - paint() reads the layer's SoA directly and draws each category via
 *   bucketed QPainter calls (drawLines, drawPolygon, drawEllipse loops).
 * - Selection highlight is a second pass over m_selectedNames.
 * - Per-object visibility (m_hiddenObjects) is honoured.
 * - Item type id is SWMMLayerItemType so the existing
 *   openswmmvisscene.cpp selection-iteration code can skip it (it's not a
 *   per-object item).
 */

#ifndef SWMMLAYERITEM_H
#define SWMMLAYERITEM_H

#include <QGraphicsItem>
#include <QPointer>

class SWMMModelLayer;

/*! Stable item-type id so callers can dynamic_cast<>/type()-check. Sits
 *  past the existing chrome ids in graphicsitems.h. */
static constexpr int SWMMLayerItemType = QGraphicsItem::UserType + 20;

class SWMMLayerItem : public QGraphicsItem
{
public:
    explicit SWMMLayerItem(SWMMModelLayer *layer,
                           QGraphicsItem *parent = nullptr);
    ~SWMMLayerItem() override = default;

    int type() const override { return SWMMLayerItemType; }

    /*! Recompute the cached bounding rect from the layer's SoA. Call after
     *  a geometry-mutating edit (move / add / remove). */
    void refreshBoundingRect();

    QRectF boundingRect() const override { return m_boundingRect; }

    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    /*! Layer that owns this batched renderer. Used by the layer's
     *  depopulateScene() to pick out its own batched item when tearing
     *  down. Multiple layers each have one, so identity is required. */
    SWMMModelLayer *ownerLayer() const { return m_layer.data(); }

private:
    QPointer<SWMMModelLayer> m_layer;
    QRectF                   m_boundingRect;   // scene-space, cached
};

#endif // SWMMLAYERITEM_H
