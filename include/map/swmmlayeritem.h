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
#include <QString>
#include <QVector>

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

    /*! Per-feature label text + priority cache
     *  (LAYER_STYLING_LABELING_PLAN follow-up). Resolving an attribute-
     *  referencing label config costs one identifyByName per feature; this
     *  caches the resolved (text, priority) per SoA group so pan/zoom
     *  repaints don't re-resolve. Entries are filled lazily (null QString =
     *  unresolved). Invalidated when the layer's editRevision moves or the
     *  label config's text-affecting fields change. */
    struct LabelTextCache {
        quint64 editRev = ~0ull;                 // layer epoch at build time
        QString expression, fieldName, priorityField;
        QVector<QString> text[4];                // 0=node 1=link 2=catch 3=gage
        QVector<double>  priority[4];
        void clear()
        {
            for (auto &t : text)     t.clear();
            for (auto &p : priority) p.clear();
        }
    };
    LabelTextCache m_labelCache;
};

#endif // SWMMLAYERITEM_H
