/*!
 * \file   annotationlayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Layer holding user-placed text annotations.
 *
 * Mirrors GISVectorLayer's populate / depopulate / refresh pattern: the data
 * model (AnnotationTextItem*'s) is persistent across scene rebuilds; the
 * QGraphicsItem instances are throwaway, recreated on canvas CRS change.
 *
 * Positions are stored in the layer's CRS (typically inherited from the SWMM
 * model layer). On populate, each position is reprojected to canvas CRS so
 * the annotation stays pinned to its map location.
 */
#ifndef OPENSWMMVIS_LAYERS_ANNOTATIONLAYER_H
#define OPENSWMMVIS_LAYERS_ANNOTATIONLAYER_H

#include "layers/openswmmvislayer.h"

#include <QHash>
#include <QJsonArray>
#include <QString>
#include <QVector>

class AnnotationTextItem;
class AnnotationGraphicsItem;

class OpenSWMMVisAnnotationLayer : public OpenSWMMVisLayer
{
    Q_OBJECT
public:
    explicit OpenSWMMVisAnnotationLayer(const QString &name = QStringLiteral("Annotations"),
                                        OpenSWMMVisWorkspace *parent = nullptr);
    ~OpenSWMMVisAnnotationLayer() override;

    // ----- Annotation management -----------------------------------------

    /*! Append an existing item; takes QObject parent ownership. Returns true on
     *  success (item not null and not already in the layer). Emits
     *  repaintRequested() so the canvas updates. */
    bool addAnnotation(AnnotationTextItem *item);

    /*! Remove and delete the annotation with this id. Returns true if found. */
    bool removeAnnotation(const QString &id);

    /*! Detach the annotation with this id from the layer without deleting.
     *  Caller takes ownership. Used by undo commands. */
    AnnotationTextItem *takeAnnotation(const QString &id);

    /*! Look up an annotation by id; null if absent. */
    [[nodiscard]] AnnotationTextItem *annotation(const QString &id) const;

    /*! Snapshot of all annotations. Order is insertion order. */
    [[nodiscard]] QVector<AnnotationTextItem *> annotations() const { return m_items; }

    /*! Hit-test: returns the topmost annotation whose rendered bounds contain
     *  the given scene-space point, or null if none. Used by the Add Text
     *  tool to distinguish "create new" from "edit existing". */
    [[nodiscard]] AnnotationTextItem *annotationAtScenePos(const QPointF &scenePos) const;

    // ----- Persistence ----------------------------------------------------

    [[nodiscard]] QJsonArray toJson() const;
    void fromJson(const QJsonArray &arr);

    // ----- OpenSWMMVisLayer overrides -------------------------------------

    void populateScene(QGraphicsScene *scene,
                       const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;
    void depopulateScene(QGraphicsScene *scene) override;
    void refreshScene(QGraphicsScene *scene,
                      const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;
    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

private slots:
    /*! Re-position the graphics item for a single annotation (cheap, no
     *  scene rebuild). Connected to AnnotationTextItem::positionChanged
     *  when the item is added. */
    void onItemPositionChanged();

    /*! Trigger a repaint of one item's graphics representation. */
    void onItemChanged();

private:
    /*! Reproject a (x, y) in this layer's CRS to canvas-CRS scene coords. */
    QPointF toCanvas(double x, double y) const;

    /*! Build / rebuild a graphics item for this annotation and insert at
     *  the correct scene position. Removes any existing graphics item for
     *  this id first. No-op when no scene is currently bound. */
    void buildGraphicsItem(AnnotationTextItem *item);

    QVector<AnnotationTextItem *>                m_items;       ///< owned (QObject parent = this)
    QHash<QString, AnnotationGraphicsItem *>     m_graphics;    ///< id → scene item (owned by scene when populated)
    QGraphicsScene                              *m_scene = nullptr;
    const SpatialReferenceSystem                *m_canvasSRS = nullptr;
};

#endif // OPENSWMMVIS_LAYERS_ANNOTATIONLAYER_H
