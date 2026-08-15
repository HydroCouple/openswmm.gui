/*!
 * \file   openswmmvisscene.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  QGraphicsScene managing all map layer items.
 */

#ifndef SWMMVISSCENE_H
#define SWMMVISSCENE_H

#include <QGraphicsScene>

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
 *          The scene coordinates are in **map CRS units**:
 *          - X increases to the right.
 *          - Y increases upward (geographic convention).
 *
 *          The associated QGraphicsView (MapCanvas) applies a vertical flip in
 *          its transform so that screen-Y-down maps to geo-Y-up.
 */
class OpenSWMMVisScene : public QGraphicsScene
{
    Q_OBJECT

public:
    explicit OpenSWMMVisScene(QObject *parent = nullptr);
    ~OpenSWMMVisScene() override;

    /*!
     * \brief Removes all items owned by a particular layer.
     */
    void removeItemsForLayer(OpenSWMMVisLayer *layer);

signals:
    void itemHovered(QGraphicsItem *item);
};

#endif // SWMMVISSCENE_H
