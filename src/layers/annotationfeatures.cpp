/**
 * @file      annotationfeatures.cpp
 * @brief     Conversion and storage of editable text annotation features.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "layers/annotationfeatures.h"
#include "layers/swmmmodellayer.h"
#include "layers/gisvectorlayer.h"
#include "map/spatialreferencesystem.h"
#include <QRegularExpression>
#include <QLineF>
#include <QJsonDocument>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <ogrsf_frmts.h>
#include <cmath>
#include <memory>

namespace AnnotationFeatures
{
namespace
{
/**
 * @brief Copy layer identity metadata into a feature-reference prefix.
 * @param[in] layer Required borrowed source layer.
 * @return Owned layerId/layerName/layerType JSON fields.
 */
QJsonObject reference(OpenSWMMVisLayer *layer)
{
    return {{"layerId", layer->layerId()},
            {"layerName", layer->name()},
            {"layerType", int(layer->layerType())}};
}

/**
 * @brief Resolve a named SWMM object and choose a representative source-map anchor.
 * @param[in] model Required stable borrowed model/geometry cache.
 * @param[in] name Exact object identity for category-filtered identify.
 * @param[in] category Valid category ordinal; invalid returns false immediately.
 * @param[in,out] f Destination may be partly modified before a failure; use fresh value storage.
 * @return True for a nonempty identified attribute map and finite XY anchor. Uses explicit X/Y when present, subcatch vertex mean or link arc midpoint otherwise.
 */
bool modelFeature(SWMMModelLayer *model, const QString &name, int category, Feature &f)
{
    if (category < 0 || category >= SWMMModelLayer::NumCategories)
        return false;
    const auto cat = static_cast<SWMMModelLayer::Category>(category);
    f.attributes = model->identifyByName(name, SWMMModelLayer::kindBitForCategory(cat));
    if (f.attributes.isEmpty())
        return false;
    f.name = name;
    f.reference = reference(model);
    f.reference["name"] = name;
    f.reference["category"] = category;
    if (f.attributes.contains("X") && f.attributes.contains("Y")) {
        f.position = {f.attributes.value("X").toDouble(), f.attributes.value("Y").toDouble()};
    } else {
        QVector<QPointF> points;
        if (category >= SWMMModelLayer::CatConduits && category <= SWMMModelLayer::CatOutlets)
            points = model->cachedLinkPolyline(model->linkIndex(name));
        else if (category == SWMMModelLayer::CatSubcatchments) {
            if (model->engine())
                points = model->cachedSubcatchVertices(
                    swmm_subcatch_index(model->engine(), name.toUtf8().constData()));
        }
        if (points.isEmpty())
            return false;
        if (category == SWMMModelLayer::CatSubcatchments) {
            QPointF sum;
            for (const auto &p : points)
                sum += p;
            f.position = sum / points.size();
        } else {
            double length = 0;
            for (int i = 1; i < points.size(); ++i)
                length += QLineF(points[i - 1], points[i]).length();
            double remaining = length / 2;
            f.position = points.first();
            for (int i = 1; i < points.size(); ++i) {
                const double segment = QLineF(points[i - 1], points[i]).length();
                if (segment > 0 && remaining <= segment) {
                    f.position =
                        points[i - 1] + (points[i] - points[i - 1]) * (remaining / segment);
                    break;
                }
                remaining -= segment;
            }
        }
    }
    return std::isfinite(f.position.x()) && std::isfinite(f.position.y());
}
/**
 * @brief Copy a readable OGR feature into annotation identity/anchor/text attributes.
 * @param[in] layer Required borrowed vector layer supplying layer identity.
 * @param[in] ogr Optional borrowed OGR feature; geometry and nonnull FID are required. It is not destroyed here.
 * @param[in,out] f Destination receives anchor/reference/name and appended string fields; existing unrelated attributes are not cleared.
 * @return True for finite centroid/line-half-length anchor, false otherwise; failures may leave partial output. A nonempty case-insensitive name attribute overrides displayed numeric FID.
 */
bool gisFeature(GISVectorLayer *layer, OGRFeature *ogr, Feature &f)
{
    if (!ogr || !ogr->GetGeometryRef() || ogr->GetFID() == OGRNullFID)
        return false;
    OGRPoint p;
    auto *g = ogr->GetGeometryRef();
    if (wkbFlatten(g->getGeometryType()) == wkbLineString) {
        auto *line = g->toLineString();
        line->Value(line->get_Length() / 2, &p);
    } else if (g->Centroid(&p) != OGRERR_NONE)
        return false;
    f.position = {p.getX(), p.getY()};
    f.name = QString::number(ogr->GetFID());
    f.reference = reference(layer);
    f.reference["fid"] = f.name; // preserve 64-bit identifiers exactly
    auto *def = ogr->GetDefnRef();
    for (int i = 0; i < def->GetFieldCount(); ++i)
        if (ogr->IsFieldSetAndNotNull(i))
            f.attributes[QString::fromUtf8(def->GetFieldDefn(i)->GetNameRef())] =
                QString::fromUtf8(ogr->GetFieldAsString(i));
    for (auto it = f.attributes.cbegin(); it != f.attributes.cend(); ++it)
        if (it.key().compare("name", Qt::CaseInsensitive) == 0 &&
            !it.value().toString().isEmpty()) {
            f.name = it.value().toString();
            break;
        }
    return std::isfinite(p.getX()) && std::isfinite(p.getY());
}
}
QVector<Feature> enumerate(OpenSWMMVisLayer *layer)
{
    QVector<Feature> result;
    if (auto *model = qobject_cast<SWMMModelLayer *>(layer)) {
        for (int c = 0; c < SWMMModelLayer::NumCategories; ++c) {
            const auto cat = static_cast<SWMMModelLayer::Category>(c);
            for (int i = 0; i < model->categoryCount(cat); ++i) {
                Feature f;
                if (modelFeature(model, model->objectNameAt(cat, i), c, f))
                    result.append(f);
            }
        }
    } else if (auto *gis = qobject_cast<GISVectorLayer *>(layer)) {
        if (auto *ogr = gis->ogrLayer()) {
            // A viewport spatial filter must not limit label generation.
            std::unique_ptr<OGRGeometry> filter(
                ogr->GetSpatialFilter() ? ogr->GetSpatialFilter()->clone() : nullptr);
            ogr->SetSpatialFilter(nullptr);
            ogr->ResetReading();
            while (auto *raw = ogr->GetNextFeature()) {
                Feature f;
                if (gisFeature(gis, raw, f))
                    result.append(f);
                OGRFeature::DestroyFeature(raw);
            }
            ogr->SetSpatialFilter(filter.get());
            ogr->ResetReading();
        }
    }
    return result;
}
bool resolve(OpenSWMMVisLayer *layer, const QJsonObject &ref, Feature &f)
{
    if (auto *model = qobject_cast<SWMMModelLayer *>(layer))
        return modelFeature(model, ref.value("name").toString(), ref.value("category").toInt(-1),
                            f);
    if (auto *gis = qobject_cast<GISVectorLayer *>(layer)) {
        if (!gis->ogrLayer())
            return false;
        bool ok = false;
        const auto fid = ref.value("fid").toString().toLongLong(&ok);
        if (!ok)
            return false;
        auto *raw = gis->ogrLayer()->GetFeature(fid);
        const bool found = gisFeature(gis, raw, f);
        if (raw)
            OGRFeature::DestroyFeature(raw);
        return found;
    }
    return false;
}
OpenSWMMVisLayer *source(const QList<OpenSWMMVisLayer *> &layers, const QJsonObject &ref)
{
    for (auto *layer : layers)
        if (layer->layerId() == ref.value("layerId").toString())
            return layer;
    if (!ref.value("layerId").toString().isEmpty())
        return nullptr;
    // Legacy/fallback lookup must be unique; never attach to an arbitrary layer.
    OpenSWMMVisLayer *found = nullptr;
    for (auto *layer : layers)
        if (layer->name() == ref.value("layerName").toString() &&
            int(layer->layerType()) == ref.value("layerType").toInt(-1)) {
            if (found)
                return nullptr;
            found = layer;
        }
    return found;
}
bool transform(QPointF &p, const SpatialReferenceSystem *from, const SpatialReferenceSystem *to)
{
    if (!from || !to || !from->ogrSpatialReference() || !to->ogrSpatialReference() ||
        from->ogrSpatialReference()->IsSame(to->ogrSpatialReference()))
        return true;
    auto *ct =
        OGRCreateCoordinateTransformation(from->ogrSpatialReference(), to->ogrSpatialReference());
    if (!ct)
        return false;
    double x = p.x(), y = p.y();
    const bool ok = ct->Transform(1, &x, &y);
    OGRCoordinateTransformation::DestroyCT(ct);
    if (ok)
        p = {x, y};
    return ok;
}
QString key(const QJsonObject &ref)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"layerId", ref.value("layerId")},
                                                       {"category", ref.value("category")},
                                                       {"name", ref.value("name")},
                                                       {"fid", ref.value("fid")}})
                                 .toJson(QJsonDocument::Compact));
}
QString evaluate(const QString &expression, const Feature &f)
{
    static const QRegularExpression tokens(QStringLiteral("\\{([^{}]+)\\}"));
    QString result;
    qsizetype previous = 0;
    auto it = tokens.globalMatch(expression);
    while (it.hasNext()) {
        const auto match = it.next();
        result += expression.mid(previous, match.capturedStart() - previous);
        const QString key = match.captured(1);
        if (key.compare("name", Qt::CaseInsensitive) == 0)
            result += f.name;
        else if (key == "fid")
            result += f.reference.value("fid").toString(f.name);
        else {
            for (auto attr = f.attributes.cbegin(); attr != f.attributes.cend(); ++attr)
                if (attr.key().compare(key, Qt::CaseInsensitive) == 0) {
                    result += attr.value().toString();
                    break;
                }
        }
        previous = match.capturedEnd();
    }
    return result + expression.mid(previous);
}
}
