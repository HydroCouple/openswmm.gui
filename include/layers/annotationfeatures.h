/*!
 * \file annotationfeatures.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \license GPL-3.0-or-later
 */
#ifndef OPENSWMM_ANNOTATIONFEATURES_H
#define OPENSWMM_ANNOTATIONFEATURES_H
#include <QJsonObject>
#include <QPointF>
#include <QVariantMap>
#include <QVector>
class OpenSWMMVisLayer;
class SpatialReferenceSystem;
namespace AnnotationFeatures {
struct Feature {
    QJsonObject reference;
    QString name;
    QPointF position; // source CRS
    QVariantMap attributes;
};
QVector<Feature> enumerate(OpenSWMMVisLayer *layer);
bool resolve(OpenSWMMVisLayer *layer, const QJsonObject &reference, Feature &feature);
OpenSWMMVisLayer *source(const QList<OpenSWMMVisLayer *> &layers, const QJsonObject &reference);
bool transform(QPointF &point, const SpatialReferenceSystem *from, const SpatialReferenceSystem *to);
QString key(const QJsonObject &reference);
QString evaluate(const QString &expression, const Feature &feature);
}
#endif
