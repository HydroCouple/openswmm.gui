/**
 * @file      swmmlayeritem.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Batched QGraphicsItem that renders all SWMM network geometry for
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
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
    /**
     * @brief Create nonselectable/nonmovable no-cache batched network item and initialize bounds.
     * @param[in] layer Optional borrowed SWMMModelLayer guarded by QPointer; deletion makes paint
     *   no-op/bounds empty after refresh.
     * @param[in] parent Optional graphics-item owner; otherwise scene/caller owns item.
     * @note GUI-thread painting/geometry access only, exclusive against mutable model/cache
     *   rebuild. Extended style option enabled for exposed-rectangle culling. Does not own model,
     *   populate caches, wire geometry-change signals or install item in scene; host calls
     *   refreshBoundingRect after relevant changes.
     */
    explicit SWMMLayerItem(SWMMModelLayer *layer, QGraphicsItem *parent = nullptr);
    /**
     * @brief Destroy item-owned label cache/graphics children; model remains owned externally and
     *   is not modified.
     */
    ~SWMMLayerItem() override = default;

    /**
     * @return SWMMLayerItemType (UserType+20) stable discriminator for whole-layer item, not an
     *   individual network object type.
     */
    int type() const override { return SWMMLayerItemType; }

    /*! Recompute the cached bounding rect from the layer's SoA. Call after
     *  a geometry-mutating edit (move / add / remove).
     * @note Calls prepareGeometryChange then unions cached scene node/gage points and nonnull
     *   link/catch boxes when array sizes match source stores. Size match is not epoch/content
     *   validation; caller must maintain cache freshness. Otherwise transforms every native source
     *   node/gage/interior vertex with unchecked OGR transform and scene Y inversion.
     * @note Empty/deleted model yields empty bounds; nonempty union padded by fixed16SCENEunits,
     *   not16pixels, independent of actual glyph width/label/connector extents. No
     *   visibility/selection filter, model mutation, cache rebuild or explicit update signal.
     *   GUI-thread/scene geometry ownership required.
     */
    void refreshBoundingRect();

    /**
     * @return Cached scene-coordinate union plus fixed16scene-unit pad, or empty; no
     *   recomputation/CRS check. Host refreshes after geometry changes. It does not include every
     *   possible screen-sized marker/label at arbitrary zoom.
     */
    QRectF boundingRect() const override { return m_boundingRect; }

    /**
     * @param[in] painter Required active scene-coordinate painter when live visible model; expected
     *   positive approximately uniform X view scale for cosmetic glyph sizing.
     * @param[in] option Required extended graphics option; exposedRect used for cached
     *   point/bbox/grid culling, null rect means full extent.
     * @param[in] widget Optional borrowed paint viewport; parent chain or scene parent locates
     *   canvas scale denominator for label gating.
     * @note Null/deleted/hidden model no-op. Requires coherent model SoA/flags/flat-scene caches
     *   and GUI-thread serialized engine/label/style access. Sets painter opacity to layer opacity
     *   BEFORE save, replacing incoming opacity; later restore retains that changed opacity, so
     *   caller guards outer state. Applies resolvable layer polygon mask clip, otherwise draws
     *   unclipped.
     * @note Draw order catch polygons/outlet connectors, links/selection/arrows, inlet connectors,
     *   nodes/selection, LID leaf badges, gages/selection, labels. QSG-owned kind geometry skipped
     *   on CPU, but inlet connectors, LID badges and labels still CPU-painted. Show-kind/hidden
     *   flags gate supported passes; node/link/catch per-kind opacity applies, gage base pass uses
     *   direct symbol values.
     * @note Links bucket paths by subtype and renderer colour/width, width quantized1/16pixel
     *   capped256for positive overrides, offsets from single/per-feature renderer caches.
     *   Strokes/glyphs/arrows converted to device coordinates and painted with reset transform to
     *   avoid large-coordinate raster errors. Point scale estimates use positive m11 else1;
     *   arbitrary rotated/nonuniform transforms are not fully normalized. Selected glyphs use kind
     *   base size/shape while selection paint overrides class colour.
     * @note Flow arrows use ORIGINAL cached polyline arclength midpoint/tangent, not offset path;
     *   optional positive-flow gate queries model flow. Connector/leaf passes may read engine; no
     *   model/engine write or refresh signals. Painting mutates only item label cache and
     *   painter/logging state.
     * @note Labels always CPU, gated by kind/layer enable, positive m11 LOD preference and scale
     *   window. Text/priority lazily cached against editRevision, text fields/decimals and store
     *   sizes; same-size unannounced object changes can stale cache. Attribute queries use
     *   name-only identifyByName, so cross-kind same-name ambiguity follows that API. Labels anchor
     *   at node/gage points, middle INDEX segment of link or catch bbox center, not full line
     *   arclength center. Stable priority sort then greedy device-rectangle collision pruning
     *   within this item. No viewport-label clipping/scene layout ownership beyond painter clip.
     */
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    /*! Layer that owns this batched renderer. Used by the layer's
     *  depopulateScene() to pick out its own batched item when tearing
     *  down. Multiple layers each have one, so identity is required.
     * @return Borrowed guarded model or null after deletion; no ownership transfer or
     *   editable/cache-readiness validation.
     */
    SWMMModelLayer *ownerLayer() const { return m_layer.data(); }

private:
    QPointer<SWMMModelLayer> m_layer;
    QRectF m_boundingRect; // scene-space, cached

    /*! Per-feature label text + priority cache
     *  (LAYER_STYLING_LABELING_PLAN follow-up). Resolving an attribute-
     *  referencing label config costs one identifyByName per feature; this
     *  caches the resolved (text, priority) per SoA group so pan/zoom
     *  repaints don't re-resolve. Entries are filled lazily (null QString =
     *  unresolved). Invalidated when the layer's editRevision moves or the
     *  label config's text-affecting fields change. */
    struct LabelTextCache {
        quint64 editRev = ~0ull; // layer epoch at build time
        QString expression, fieldName, priorityField;
        int decimals = -1;        // feedback C4 — text-affecting
        QVector<QString> text[4]; // 0=node 1=link 2=catch 3=gage
        QVector<double> priority[4];
        /**
         * @brief Clear all four text and priority vectors, preserving epoch/config fields for
         *   caller reset.
         * @note LabelTextCache-local storage only, no repaint/engine lookup or invalidation signal.
         *   Caller resizes/replaces metadata before next lazy fill.
         */
        void clear()
        {
            for (auto &t : text)
                t.clear();
            for (auto &p : priority)
                p.clear();
        }
    };
    LabelTextCache m_labelCache;
};

#endif // SWMMLAYERITEM_H
