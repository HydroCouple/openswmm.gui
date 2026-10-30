/**
 * @file      annotationfeatures.h
 * @brief     Conversion and storage of editable text annotation features.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_ANNOTATIONFEATURES_H
#define OPENSWMM_ANNOTATIONFEATURES_H
#include <QJsonObject>
#include <QPointF>
#include <QVariantMap>
#include <QVector>
class OpenSWMMVisLayer;
class SpatialReferenceSystem;
namespace AnnotationFeatures
{
struct Feature {
    QJsonObject reference;
    QString name;
    QPointF position; // source CRS
    QVariantMap attributes;
};
/**
 * @brief Snapshot source objects usable as feature-linked annotation anchors.
 * @param[in] layer Optional borrowed stable model/vector layer; other kinds return empty.
 * @return Owned feature/reference/attribute values in model category/object or OGR iteration order, skipping unresolved/nonfinite anchors. Model links use arc midpoint, subcatchments arithmetic vertex mean, GIS lines half-length and other GIS geometry centroid.
 * @note GUI-thread operation. GIS enumeration temporarily removes/restores the spatial filter and resets the sequential read cursor; attributes become strings and FIDs are serialized as strings to preserve 64 bits. No source ownership or feature pointers escape.
 */
QVector<Feature> enumerate(OpenSWMMVisLayer *layer);
/**
 * @brief Resolve one stored object reference against an already selected source layer.
 * @param[in] layer Optional borrowed stable model/vector source; no check here compares its ID to reference.layerId.
 * @param[in] reference Model name/category or GIS string-valued fid; malformed/absent fields fail.
 * @param[in,out] feature Populated with source-CRS anchor/name/identity/attributes on success, but can be partly changed on failure. Prefer a fresh destination; GIS conversion adds supplied fields without clearing an existing attribute map.
 * @return Whether the object/geometry resolves to a finite anchor. Source lookup/CRS transformation is a separate operation.
 */
bool resolve(OpenSWMMVisLayer *layer, const QJsonObject &reference, Feature &feature);
/**
 * @brief Select the referenced source from a caller-supplied top-level layer list.
 * @param[in] layers Borrowed nonnull layer pointers, stable through lookup.
 * @param[in] reference Exact layerId takes priority; only an empty/missing ID enables fallback to a unique exact layerName/layerType pair.
 * @return Borrowed first matching ID, unique legacy match, or null for no match/ambiguous legacy identity. A nonempty stale ID does not fall back by name.
 */
OpenSWMMVisLayer *source(const QList<OpenSWMMVisLayer *> &layers, const QJsonObject &reference);
/**
 * @brief Reproject a source-coordinate point when two usable different CRSs exist.
 * @param[in,out] point Replaced only after a successful transformation; otherwise unchanged.
 * @param[in] from Optional borrowed source CRS.
 * @param[in] to Optional borrowed destination CRS.
 * @return True with unchanged point when either CRS/OGR reference is missing or they are equivalent, otherwise OGR transform success. False for transform creation/execution failure. No finite-result check or scene-Y flip occurs.
 */
bool transform(QPointF &point, const SpatialReferenceSystem *from,
               const SpatialReferenceSystem *to);
/**
 * @brief Build a compact persistent identity token for an annotation reference.
 * @param[in] reference Arbitrary JSON; only layerId/category/name/fid are retained.
 * @return Compact JSON string of those four fields, excluding display layer name/type; missing values retain Qt JSON omission/null semantics. No validation or source access occurs.
 */
QString key(const QJsonObject &reference);
/**
 * @brief Substitute simple brace tokens into linked annotation text.
 * @param[in] expression Literal template with nonempty {token} spans; no arithmetic, escaping or recursive evaluation is implemented.
 * @param[in] feature Snapshot supplying name/reference/attributes.
 * @return Text with case-insensitive name and attribute tokens, exact lowercase fid from the reference (fallback name), and unknown tokens replaced by empty. Other text is retained verbatim.
 */
QString evaluate(const QString &expression, const Feature &feature);
}
#endif
