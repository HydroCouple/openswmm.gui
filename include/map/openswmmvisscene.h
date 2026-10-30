/**
 * @file      openswmmvisscene.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     QGraphicsScene managing all map layer items.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef SWMMVISSCENE_H
#define SWMMVISSCENE_H

#include <QGraphicsScene>
#include <limits>

class OpenSWMMVisLayer;
class MapCanvas;

/*!
 * \class OpenSWMMVisScene
 * \brief Central QGraphicsScene subclass that holds all map-layer graphics items.
 *
 * \details Layers populate the scene with QGraphicsItems via
 *          OpenSWMMVisLayer::populateScene() / depopulateScene().  The scene uses
 *          BSP-tree indexing for efficient hit-testing and spatial queries.
 *
 *          Scene coordinates are prepared by each layer/host. Current map
 *          render paths generally store canvas-map X and negative canvas-map Y
 *          so Qt scene-downward Y matches the displayed geographic direction.
 */
class OpenSWMMVisScene : public QGraphicsScene
{
    Q_OBJECT

public:
    /**
     * @param[in] parent Optional QObject owner.
     * @note Creates ordinary QGraphicsScene using BSP item index; scene owns added graphics items
     *   under Qt contract. Layer items use host-prepared scene coordinates (typically canvas map X,
     *   negative map Y), not an automatic CRS/Y transform. GUI-thread only.
     */
    explicit OpenSWMMVisScene(QObject *parent = nullptr);
    /**
     * @brief Apply Qt scene destruction, including owned graphics items; borrowed item pointers
     *   become invalid.
     */
    ~OpenSWMMVisScene() override;

    /*!
     * \brief Removes all items owned by a particular layer.
     * @param[in] layer Optional borrowed layer identity; null no-op.
     * @note Scans/deletes recognized legacy Node/Link/Catchment,
     *   VectorPoint/Line/Polygon/PolygonPath and RasterTile item types with exact owner pointer.
     *   Does not remove newer batched SWMMLayerItem, 2D/feature/text items through this helper;
     *   their layer depopulation owns cleanup. Does not call layer callbacks or clear layer-held
     *   raw item pointers. GUI-thread only.
     */
    void removeItemsForLayer(OpenSWMMVisLayer *layer);

    // Render a layer-stack band without changing item visibility, selection,
    // or the scene's change notifications. Children follow their top-level item.
    /**
     * @param[in] painter Required active painter borrowed for synchronous render.
     * @param[in] target Target device/painter rectangle, Qt render defaults apply when null.
     * @param[in] source Source scene rectangle, Qt render defaults apply when null.
     * @param[in] minimumZ Inclusive lower TOP-LEVEL item z bound.
     * @param[in] maximumZ Exclusive upper TOP-LEVEL item z bound.
     * @note Temporarily changes internal z-band and calls scene render IgnoreAspectRatio, restoring
     *   bounds with RAII. Child items follow top-level z, not their local z. Does not change
     *   visible/selected flags or emit scene changes; non-thread-safe/reentrant nested calls
     *   restore prior band.
     */
    void renderSlice(QPainter *painter, const QRectF &target, const QRectF &source, qreal minimumZ,
                     qreal maximumZ);

protected:
    /**
     * @param[in] painter Active borrowed painter forwarded to Qt base.
     * @param[in] count Nonnegative candidate count, parallel arrays contain at least count entries.
     * @param[in] items Nonnull candidate item pointers in incoming drawing order.
     * @param[in] options Parallel style options copied only for selected z-band.
     * @param[in] widget Optional Qt paint widget context.
     * @note Keeps incoming item/option order where topLevelItem.z is in inclusive-min/exclusive-max
     *   band, then delegates Qt painting. Outside/NaN z ignored by comparisons. No item ownership
     *   or visibility/selection mutation.
     */
    void drawItems(QPainter *painter, int count, QGraphicsItem *items[],
                   const QStyleOptionGraphicsItem options[], QWidget *widget = nullptr) override;

private:
    qreal m_minimumRenderZ = -std::numeric_limits<qreal>::infinity();
    qreal m_maximumRenderZ = std::numeric_limits<qreal>::infinity();

signals:
    /**
     * @param[in] item Borrowed hovered scene item or null by producer convention; can be deleted
     *   after signal.
     * @brief Expose hover notification channel; this implementation does not emit it itself or take
     *   item ownership.
     */
    void itemHovered(QGraphicsItem *item);
};

#endif // SWMMVISSCENE_H
