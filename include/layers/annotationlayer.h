/*!
 * \file annotationlayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \license GPL-3.0-or-later
 */
#ifndef OPENSWMMVIS_LAYERS_ANNOTATIONLAYER_H
#define OPENSWMMVIS_LAYERS_ANNOTATIONLAYER_H
#include "layers/openswmmvislayer.h"
#include <QHash>
#include <QJsonArray>
#include <QTransform>
class AnnotationTextItem;

// Persistent text model. The canvas paints all text layers together, above
// the CPU/GPU map, to share collision placement and screen-space picking.
class OpenSWMMVisAnnotationLayer : public OpenSWMMVisLayer
{
    Q_OBJECT
public:
    explicit OpenSWMMVisAnnotationLayer(const QString &name = QStringLiteral("Text"),
                                       OpenSWMMVisWorkspace *parent = nullptr);
    bool addAnnotation(AnnotationTextItem *item);
    bool removeAnnotation(const QString &id);
    AnnotationTextItem *takeAnnotation(const QString &id);
    AnnotationTextItem *annotation(const QString &id) const;
    QVector<AnnotationTextItem *> annotations() const { return m_items; }
    AnnotationTextItem *annotationAtScenePos(const QPointF &scenePos) const;
    AnnotationTextItem *annotationAtPixel(const QPointF &pixel) const;
    QRectF renderedBounds(const QString &id) const { return m_bounds.value(id); }
    QPointF renderedOffset(const QString &id) const { return m_offsets.value(id); }
    bool autoLayout() const { return m_autoLayout; }
    void setAutoLayout(bool enabled);
    QJsonArray toJson() const;
    void fromJson(const QJsonArray &items);
    QJsonObject layerJson() const;
    void restoreLayer(const QJsonObject &object);
    QString sourceDescription() const override { return tr("(in-memory)"); }
    QVector<QPair<QString, QString>> extendedMetadata() const override
    { return {{tr("Text items"), QString::number(m_items.size())}}; }
    void populateScene(QGraphicsScene *, const MapExtent &, const SpatialReferenceSystem *) override {}
    void depopulateScene(QGraphicsScene *) override { m_bounds.clear(); m_offsets.clear(); }
    void refreshScene(QGraphicsScene *, const MapExtent &, const SpatialReferenceSystem *) override {}
    static void paintLayers(QPainter &p, const QList<OpenSWMMVisLayer *> &layers,
                            const MapExtent &extent, const SpatialReferenceSystem *srs,
                            const QSize &viewport);
signals:
    void annotationsChanged();
private:
    QVector<AnnotationTextItem *> m_items;
    QHash<QString, QRectF> m_bounds;
    QHash<QString, QPointF> m_offsets;
    QTransform m_sceneToPixel;
    bool m_autoLayout = false;
};
#endif
