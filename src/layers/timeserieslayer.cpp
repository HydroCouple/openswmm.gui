/**
 * @file timeserieslayer.cpp
 * @brief Native observation-layer lifecycle, station selection and persistent feature links.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "layers/timeserieslayer.h"
#include "layers/gisobjectref.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/networkplotpick.h"
#include "swmmvisprojectwindow.h"
#include "timeseries/timeseriesstore.h"
#include "ui/panels/timeserieslayerbrowser.h"
#include <QDialog>
#include <QCryptographicHash>
#include <functional>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QListWidget>
#include <QMessageBox>
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QVBoxLayout>
#include <cmath>
#include <ogrsf_frmts.h>

using namespace OpenSWMM::TimeSeries;
namespace
{
/** @brief Undo a project-sidecar relationship value; guarded callbacks tolerate layer removal. */
class LinkCommand final : public QUndoCommand
{
public:
    LinkCommand(std::function<void(const QJsonArray &)> apply, QJsonArray before, QJsonArray after)
        : QUndoCommand(QObject::tr("Change time-series feature links")), m_apply(std::move(apply)),
          m_before(std::move(before)), m_after(std::move(after))
    {
    }
    void undo() override { m_apply(m_before); }
    void redo() override { m_apply(m_after); }

private:
    std::function<void(const QJsonArray &)> m_apply;
    QJsonArray m_before, m_after;
};
/** @brief Conservative file size/mtime fence for all GIS links; never itself a feature identity. */
QString fileRevision(const GISVectorLayer *layer)
{
    const QFileInfo info(layer->filePath());
    return QString::number(info.size()) + QLatin1Char(':') +
           QString::number(info.lastModified().toMSecsSinceEpoch());
}
/** @brief Resolve an optional stable field key without retaining an OGR feature or changing filters. */
long long resolveFid(GISVectorLayer *layer, const QJsonObject &link)
{
    if (!layer || !layer->ogrLayer() || layer->isTemporary() ||
        QFileInfo(layer->filePath()).absoluteFilePath() != link.value("source").toString() ||
        layer->ogrLayerName() != link.value("table").toString())
        return -1;
    if (fileRevision(layer) != link.value("revision").toString())
        return -1;
    auto *ogr = layer->ogrLayer();
    const QString key = link.value("keyField").toString();
    const long long fid = link.value("fid").toString().toLongLong();
    if (key.isEmpty()) {
        if (fileRevision(layer) != link.value("revision").toString())
            return -1;
        auto *feature = ogr->GetFeature(fid);
        if (!feature)
            return -1;
        OGRFeature::DestroyFeature(feature);
        return fid;
    }
    const int field = ogr->GetLayerDefn()->GetFieldIndex(key.toUtf8().constData());
    if (field < 0)
        return -1;
    // Resolve the stable key against all rows, independent of presentation filters.
    QString quoted = key;
    quoted.replace('"', "\"\"");
    QString value = link.value("keyValue").toString();
    value.replace('\'', "''");
    OGRGeometry *oldSpatial = ogr->GetSpatialFilter() ? ogr->GetSpatialFilter()->clone() : nullptr;
    const auto restore = qScopeGuard([&] {
        ogr->SetAttributeFilter(layer->filterExpression().toUtf8().constData());
        ogr->SetSpatialFilter(oldSpatial);
        if (oldSpatial)
            OGRGeometryFactory::destroyGeometry(oldSpatial);
        ogr->ResetReading();
    });
    ogr->SetSpatialFilter(nullptr);
    if (ogr->SetAttributeFilter(
            QStringLiteral("\"%1\" = '%2'").arg(quoted, value).toUtf8().constData()) != OGRERR_NONE)
        return -1;
    ogr->ResetReading();
    auto *first = ogr->GetNextFeature();
    const long long answer = first ? first->GetFID() : -1;
    OGRFeature::DestroyFeature(first);
    auto *second = ogr->GetNextFeature();
    const bool ambiguous = second != nullptr;
    OGRFeature::DestroyFeature(second);
    return ambiguous ? -1 : answer;
}
/** @brief Validate an exact typed model object without using the ambiguous name-only index. */
bool modelHas(::SWMMModelLayer *model, const SWMMObjectRef &ref)
{
    return model && ref.objectType >= SWMMObjectRef::Node &&
           ref.objectType <= SWMMObjectRef::RainGage &&
           !model->identifyByName(ref.name, ::SWMMModelLayer::kindMaskForObjectType(ref.objectType))
                .isEmpty();
}
/** @brief Hash a typed model object's current immutable value snapshot for deletion/undo fencing. */
QString modelSignature(::SWMMModelLayer *model, const SWMMObjectRef &ref)
{
    const auto values =
        model->identifyByName(ref.name, ::SWMMModelLayer::kindMaskForObjectType(ref.objectType));
    if (values.isEmpty())
        return {};
    return QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument::fromVariant(values).toJson(QJsonDocument::Compact),
                                 QCryptographicHash::Sha256)
            .toHex());
}

}

TimeSeriesLayer::TimeSeriesLayer(OpenSWMMVisWorkspace *parent) : GISVectorLayer({}, {}, parent)
{
    setLayerType(SWMMTimeSeriesLayer);
    setName(tr("Time series"));
    auto sym = symbol();
    sym.markerSize = 8.0;
    sym.markerFill = QColor(0, 165, 200);
    sym.markerOutline = QColor(0, 65, 90);
    sym.showLabels = true;
    sym.labelField = QStringLiteral("title");
    setSymbol(sym);
    auto labels = labelConfig();
    labels.enabled = true;
    labels.fieldName = "title";
    labels.haloEnabled = true;
    setLabelConfig(labels);
    connect(this, &GISVectorLayer::selectionChanged, this, [this](const QSet<long long> &fids) {
        if (!m_syncing)
            setSelectedSeriesIds(seriesForLocations(fids));
    });
}
TimeSeriesLayer::~TimeSeriesLayer()
{
    bindCanvas(nullptr);
    if (m_browser)
        m_browser->close();
}

bool TimeSeriesLayer::openTimeSeries(const QString &path, const QString &expected, QString *error)
{
    QString message;
    auto data = TimeSeriesStore::open(path, &message);
    const QString wanted = expected.isEmpty() ? m_expectedId : expected;
    if (data && !wanted.isEmpty() && data->id != wanted) {
        message = tr("This file belongs to a different time-series dataset.");
        data.reset();
    }
    m_sourcePath = QFileInfo(path).absoluteFilePath();
    m_expectedId = wanted;
    m_locationFids.clear();
    m_seriesIndex.clear();
    m_data.reset();
    closeDataset();
    if (data) {
        GISVectorLayer::openDataset(m_sourcePath, QStringLiteral("ts_locations"));
        if (!ogrLayer())
            message = tr("The dataset's monitoring-location table cannot be opened.");
        else {
            m_data = data;
            for (qsizetype i = 0; i < data->series.size(); ++i)
                m_seriesIndex.insert(data->series[i].id, i);
            m_expectedId = data->id;
            if (!data->title.isEmpty())
                setName(data->title);
            ogrLayer()->ResetReading();
            while (auto *f = ogrLayer()->GetNextFeature()) {
                m_locationFids.insert(f->GetFID(),
                                      QString::fromUtf8(f->GetFieldAsString("location_id")));
                OGRFeature::DestroyFeature(f);
            }
            ogrLayer()->ResetReading();
        }
    }
    m_sourceError = m_data ? QString{} : message;
    if (error)
        *error = m_sourceError;
    markSceneDirty();
    emit datasetChanged();
    emit childrenChanged();
    emit openFinished(bool(m_data));
    emit repaintRequested();
    setSelectedSeriesIds(selectedSeriesIds());
    return bool(m_data);
}
QVector<QPair<QString, QString>> TimeSeriesLayer::extendedMetadata() const
{
    return {{tr("Dataset"), m_expectedId},
            {tr("Series"), QString::number(m_data ? m_data->series.size() : 0)},
            {tr("Locations"), QString::number(m_data ? m_data->locations.size() : 0)},
            {tr("Source status"), m_data ? tr("Available") : m_sourceError}};
}
QStringList TimeSeriesLayer::seriesIds() const
{
    QStringList ids;
    if (m_data)
        for (const auto &s : m_data->series)
            ids.append(s.id);
    return ids;
}
SeriesInfo TimeSeriesLayer::seriesInfo(const QString &id) const
{
    const auto it = m_seriesIndex.constFind(id);
    return m_data && it != m_seriesIndex.cend() ? m_data->series[it.value()] : SeriesInfo{};
}

QString TimeSeriesLayer::seriesCaption(const QString &id) const
{
    const auto s = seriesInfo(id);
    QString text = s.title.isEmpty() ? s.sourceSeriesId : s.title;
    if (!s.siteId.isEmpty())
        text = s.siteId + " — " + text;
    if (!s.statistic.isEmpty())
        text += " — " + s.statistic;
    if (!s.unit.isEmpty())
        text += " [" + s.unit + ']';
    return text;
}
QStringList TimeSeriesLayer::selectedSeriesIds() const
{
    QStringList ids;
    for (const auto &id : seriesIds())
        if (m_selected.contains(id))
            ids.append(id);
    return ids;
}
SWMMObjectRef TimeSeriesLayer::seriesRef(const QString &id) const
{
    return {SWMMObjectRef::ObservedSeries, layerId() + QLatin1Char('/') + id};
}
QString TimeSeriesLayer::seriesIdForRef(const SWMMObjectRef &ref) const
{
    const auto prefix = layerId() + QLatin1Char('/');
    if (ref.objectType != SWMMObjectRef::ObservedSeries || !ref.name.startsWith(prefix))
        return {};
    const auto id = ref.name.mid(prefix.size());
    return seriesInfo(id).id;
}
QStringList TimeSeriesLayer::seriesForLocations(const QSet<long long> &fids) const
{
    QSet<QString> locations;
    for (auto fid : fids)
        if (m_locationFids.contains(fid))
            locations.insert(m_locationFids.value(fid));
    QStringList ids;
    if (m_data)
        for (const auto &s : m_data->series)
            if (locations.contains(s.locationId))
                ids.append(s.id);
    return ids;
}
MapExtent TimeSeriesLayer::locationsExtent(const QStringList &ids) const
{
    const double missing = std::numeric_limits<double>::quiet_NaN();
    MapExtent bounds(missing, missing, missing, missing);
    if (!m_canvas || !m_canvas->layers().contains(const_cast<TimeSeriesLayer *>(this)) || !m_data)
        return bounds;
    const QSet<QString> wanted(ids.begin(), ids.end());
    QSet<QString> wantedLocations, ownedLocations;
    for (const auto &id : wanted) {
        const auto series = seriesInfo(id);
        if (!series.locationId.isEmpty())
            wantedLocations.insert(series.locationId);
    }
    bool any = false;
    const auto include = [&](const OpenSWMMVisLayer *source, const MapExtent &native) {
        const auto extent = m_canvas->extentInCanvasCRS(source, native);
        if (!std::isfinite(extent.xMin()) || !std::isfinite(extent.yMin()) ||
            !std::isfinite(extent.xMax()) || !std::isfinite(extent.yMax()) ||
            extent.xMin() > extent.xMax() || extent.yMin() > extent.yMax())
            return;
        bounds = any ? bounds.united(extent) : extent;
        any = true;
    };
    const auto includeFeature = [&](GISVectorLayer *source, long long fid) {
        auto *feature = source && source->ogrLayer() ? source->ogrLayer()->GetFeature(fid) : nullptr;
        const bool spatial = feature && feature->GetGeometryRef() &&
                             !feature->GetGeometryRef()->IsEmpty();
        if (spatial) {
            OGREnvelope envelope;
            feature->GetGeometryRef()->getEnvelope(&envelope);
            include(source, MapExtent(envelope.MinX, envelope.MinY, envelope.MaxX, envelope.MaxY));
        }
        OGRFeature::DestroyFeature(feature);
        return spatial;
    };
    for (auto it = m_locationFids.cbegin(); it != m_locationFids.cend(); ++it)
        if (wantedLocations.contains(it.value()) &&
            includeFeature(const_cast<TimeSeriesLayer *>(this), it.key()))
            ownedLocations.insert(it.value());
    for (const auto &value : m_associations) {
        const auto link = value.toObject();
        const auto id = link.value("seriesId").toString();
        // A feature assignment does not move a downloaded station. Linked geometry
        // is a useful fallback only when the source series has no owned location.
        if (!wanted.contains(id) || ownedLocations.contains(seriesInfo(id).locationId))
            continue;
        SWMMObjectRef ref;
        if (!resolveAssociation(link, &ref))
            continue;
        for (auto *source : m_canvas->layers()) {
            if (source->layerId() != link.value("layerId").toString())
                continue;
            if (auto *gis = qobject_cast<GISVectorLayer *>(source)) {
                QString owner;
                long long fid = -1;
                if (GisObjectRef::parseFeature(ref, &owner, &fid) && owner == source->layerId())
                    includeFeature(gis, fid);
            } else if (auto *model = qobject_cast<::SWMMModelLayer *>(source)) {
                include(model, model->objectExtent(ref.name));
            }
        }
    }
    return bounds;
}
bool TimeSeriesLayer::canZoomToLocations(const QStringList &ids) const
{
    const auto bounds = locationsExtent(ids);
    return std::isfinite(bounds.xMin()) && std::isfinite(bounds.yMin()) &&
           std::isfinite(bounds.xMax()) && std::isfinite(bounds.yMax());
}
bool TimeSeriesLayer::zoomToLocations(const QStringList &ids)
{
    const auto bounds = locationsExtent(ids);
    return m_canvas && m_canvas->zoomToExtent(bounds);
}
void TimeSeriesLayer::setSelectedSeriesIds(const QStringList &ids)
{
    QSet<QString> accepted;
    for (const auto &id : ids)
        if (!seriesInfo(id).id.isEmpty())
            accepted.insert(id);
    if (accepted == m_selected)
        return;
    m_selected = accepted;
    synchronizeSelection();
    emit seriesSelectionChanged(selectedSeriesIds());
}
void TimeSeriesLayer::synchronizeSelection()
{
    if (m_syncing)
        return;
    QScopedValueRollback<bool> lock(m_syncing, true);
    QSet<QString> locations;
    for (const auto &id : m_selected)
        locations.insert(seriesInfo(id).locationId);
    QSet<long long> fids;
    for (auto it = m_locationFids.cbegin(); it != m_locationFids.cend(); ++it)
        if (locations.contains(it.value()))
            fids.insert(it.key());
    if (selectedFeatureIds() != fids)
        setSelectedFeatureIds(fids);
    if (m_selection) {
        auto next = m_selection->selection();
        for (auto it = next.begin(); it != next.end();)
            if (it->objectType == SWMMObjectRef::ObservedSeries &&
                it->name.startsWith(layerId() + '/'))
                it = next.erase(it);
            else
                ++it;
        for (const auto &old : m_linkHighlights)
            next.remove(old);
        m_linkHighlights.clear();
        for (const auto &id : m_selected) {
            next.insert(seriesRef(id));
            for (const auto &value : m_associations) {
                const auto link = value.toObject();
                SWMMObjectRef target;
                if (link.value("seriesId").toString() == id && resolveAssociation(link, &target) &&
                    !next.contains(target)) {
                    next.insert(target);
                    m_linkHighlights.insert(target);
                }
            }
        }
        m_selection->select(next);
    }
}
void TimeSeriesLayer::bindCanvas(MapCanvas *canvas)
{
    for (const auto &connection : m_connections)
        disconnect(connection);
    m_connections.clear();
    if (m_selection && m_canvas != canvas) {
        auto next = m_selection->selection();
        for (auto it = next.begin(); it != next.end();)
            if (it->objectType == SWMMObjectRef::ObservedSeries &&
                it->name.startsWith(layerId() + '/'))
                it = next.erase(it);
            else
                ++it;
        for (const auto &old : m_linkHighlights)
            next.remove(old);
        m_linkHighlights.clear();
        m_selection->select(next);
    }
    m_canvas = canvas;
    m_selection = nullptr;
    auto *window = canvas ? qobject_cast<SWMMVisProjectWindow *>(canvas->parent()) : nullptr;
    if (window) {
        m_selection = window->selectionManager();
        m_connections.append(connect(this, &TimeSeriesLayer::associationsChanged, window,
                                     [window] { window->setHasChanges(true); }));
    }
    if (m_selection)
        m_connections.append(connect(
            m_selection, &SelectionManager::selectionChanged, this,
            [this](const QSet<SWMMObjectRef> &refs, const QSet<SWMMObjectRef> &added,
                   const QSet<SWMMObjectRef> &removed) {
                if (m_syncing)
                    return;
                bool observedChanged = false, spatialChanged = false;
                for (const auto &ref : added + removed) {
                    if (ref.objectType == SWMMObjectRef::ObservedSeries &&
                        ref.name.startsWith(layerId() + '/'))
                        observedChanged = true;
                    if (ref.objectType == SWMMObjectRef::Feature ||
                        (ref.objectType >= SWMMObjectRef::Node &&
                         ref.objectType <= SWMMObjectRef::RainGage))
                        spatialChanged = true;
                }
                if (!observedChanged && !spatialChanged)
                    return;
                QStringList ids;
                for (const auto &ref : refs) {
                    if (observedChanged) {
                        const auto id = seriesIdForRef(ref);
                        if (!id.isEmpty())
                            ids.append(id);
                    } else {
                        QString owner;
                        long long fid;
                        if (GisObjectRef::parseFeature(ref, &owner, &fid) && owner == layerId())
                            ids.append(seriesForLocations({fid}));
                        else
                            ids.append(seriesForFeature(ref));
                    }
                }
                setSelectedSeriesIds(ids);
            }));
    if (canvas) {
        // A project can restore/load its model after this collection. Rebuild
        // guarded model callbacks when the canvas model membership changes;
        // other target changes only need current highlight resolution.
        const auto refreshTargets = [this](OpenSWMMVisLayer *changed) {
            if (qobject_cast<::SWMMModelLayer *>(changed))
                bindCanvas(m_canvas);
            else
                synchronizeSelection();
            emit associationResolutionChanged();
        };
        m_connections.append(connect(canvas, &MapCanvas::layerAdded, this, refreshTargets));
        m_connections.append(connect(canvas, &MapCanvas::layerRemoved, this, refreshTargets));
        for (auto *layer : canvas->layers())
            if (auto *model = qobject_cast<::SWMMModelLayer *>(layer)) {
                const auto refreshModelLinks = [this, model] {
                    auto links = m_associations;
                    bool changed = false;
                    for (int i = 0; i < links.size(); ++i) {
                        auto link = links[i].toObject();
                        if (link.value("kind") != "model" ||
                            link.value("layerId").toString() != model->layerId() ||
                            link.value("deleted").toBool())
                            continue;
                        const SWMMObjectRef ref{static_cast<SWMMObjectRef::ObjectType>(
                                                    link.value("objectType").toInt()),
                                                link.value("name").toString()};
                        const auto signature = modelSignature(model, ref);
                        if (signature.isEmpty()) {
                            link["deleted"] = true;
                            changed = true;
                        } else if (signature != link.value("signature").toString()) {
                            link["signature"] = signature;
                            changed = true;
                        }
                        links[i] = link;
                    }
                    if (changed)
                        applyAssociations(links);
                    emit associationResolutionChanged();
                };
                m_connections.append(
                    connect(model, &::SWMMModelLayer::geometryChanged, this, refreshModelLinks));
                m_connections.append(
                    connect(model, &::SWMMModelLayer::attributeChanged, this, refreshModelLinks));
                const auto lastUndo = std::make_shared<int>(canvas->undoStack()->index());
                const QPointer<::SWMMModelLayer> guardedModel(model);
                m_connections.append(
                    connect(canvas->undoStack(), &QUndoStack::indexChanged, this,
                            [this, guardedModel, lastUndo](int index) {
                                auto *model = guardedModel.data();
                                if (!model)
                                    return;
                                const bool undo = index < *lastUndo;
                                *lastUndo = index;
                                if (!undo)
                                    return;
                                auto links = m_associations;
                                bool changed = false;
                                for (int i = 0; i < links.size(); ++i) {
                                    auto link = links[i].toObject();
                                    if (link.value("kind") != "model" ||
                                        link.value("layerId").toString() != model->layerId() ||
                                        !link.value("deleted").toBool())
                                        continue;
                                    const SWMMObjectRef ref{static_cast<SWMMObjectRef::ObjectType>(
                                                                link.value("objectType").toInt()),
                                                            link.value("name").toString()};
                                    const auto signature = modelSignature(model, ref);
                                    if (!signature.isEmpty() &&
                                        signature == link.value("signature").toString()) {
                                        link.remove("deleted");
                                        links[i] = link;
                                        changed = true;
                                    }
                                }
                                if (changed)
                                    applyAssociations(links);
                            }));
                m_connections.append(connect(
                    model, &::SWMMModelLayer::objectRenamed, this,
                    [this, model](const QString &oldName, const QString &newName, int kind) {
                        auto links = m_associations;
                        bool changed = false;
                        for (int i = 0; i < links.size(); ++i) {
                            auto link = links[i].toObject();
                            if (link.value("layerId").toString() == model->layerId() &&
                                link.value("kind").toString() == "model" &&
                                link.value("name").toString() == oldName &&
                                ::SWMMModelLayer::kindMaskForObjectType(
                                    link.value("objectType").toInt()) == kind) {
                                link["name"] = newName;
                                link.remove("deleted");
                                link["signature"] =
                                    modelSignature(model, {static_cast<SWMMObjectRef::ObjectType>(
                                                               link.value("objectType").toInt()),
                                                           newName});
                                links[i] = link;
                                changed = true;
                            }
                        }
                        if (changed)
                            applyAssociations(links);
                    }));
            }
    }
    synchronizeSelection();
}
void TimeSeriesLayer::restoreAssociations(const QJsonArray &links)
{
    QJsonArray accepted;
    QSet<QString> seen;
    for (const auto &v : links) {
        const auto link = v.toObject();
        const auto id = link.value("seriesId").toString();
        if (id.isEmpty() || seen.contains(id) || link.value("layerId").toString().isEmpty() ||
            (link.value("kind") != "gis" && link.value("kind") != "model"))
            continue;
        accepted.append(link);
        seen.insert(id);
    }
    applyAssociations(accepted);
}
void TimeSeriesLayer::applyAssociations(const QJsonArray &links)
{
    if (m_associations == links)
        return;
    m_associations = links;
    synchronizeSelection();
    emit associationsChanged();
}
bool TimeSeriesLayer::resolveAssociation(const QJsonObject &link, SWMMObjectRef *target) const
{
    if (!m_canvas || link.value("deleted").toBool())
        return false;
    for (auto *layer : m_canvas->layers()) {
        if (layer->layerId() != link.value("layerId").toString() || layer->isTemporary())
            continue;
        if (link.value("kind") == "gis") {
            auto *gis = qobject_cast<GISVectorLayer *>(layer);
            const auto fid = resolveFid(gis, link);
            if (fid >= 0) {
                if (target)
                    *target = GisObjectRef::feature(layer->layerId(), fid);
                return true;
            }
        } else if (link.value("kind") == "model") {
            SWMMObjectRef ref{
                static_cast<SWMMObjectRef::ObjectType>(link.value("objectType").toInt()),
                link.value("name").toString()};
            if (modelHas(qobject_cast<::SWMMModelLayer *>(layer), ref)) {
                if (target)
                    *target = ref;
                return true;
            }
        }
    }
    return false;
}
QString TimeSeriesLayer::associationStatus(const QString &id) const
{
    for (const auto &v : m_associations)
        if (v.toObject().value("seriesId").toString() == id)
            return resolveAssociation(v.toObject()) ? tr("Linked") : tr("Unresolved link");
    return tr("Not linked");
}
QStringList TimeSeriesLayer::seriesForFeature(const SWMMObjectRef &feature,
                                              const OpenSWMMVisLayer *targetLayer) const
{
    QStringList ids;
    for (const auto &v : m_associations) {
        const auto link = v.toObject();
        if (targetLayer && link.value("layerId").toString() != targetLayer->layerId())
            continue;
        SWMMObjectRef resolved;
        if (resolveAssociation(link, &resolved) && resolved == feature &&
            !seriesInfo(link.value("seriesId").toString()).id.isEmpty())
            ids.append(link.value("seriesId").toString());
    }
    return ids;
}
bool TimeSeriesLayer::attachToSelectedFeature(const QStringList &ids, QString *error)
{
    auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (!m_selection || !m_canvas)
        return fail(tr("Choose a feature in the current project first."));
    QSet<SWMMObjectRef> features;
    for (const auto &ref : m_selection->selection()) {
        if (ref.objectType >= SWMMObjectRef::Node && ref.objectType <= SWMMObjectRef::RainGage)
            features.insert(ref);
        if (ref.objectType == SWMMObjectRef::Feature) {
            QString source;
            long long fid;
            if (GisObjectRef::parseFeature(ref, &source, &fid) && source != layerId())
                features.insert(ref);
        }
    }
    if (features.size() != 1)
        return fail(tr("Select exactly one persistent feature or model object to attach."));
    const auto ref = *features.cbegin();
    QJsonObject link;
    if (ref.objectType == SWMMObjectRef::Feature) {
        QString source;
        long long fid = -1;
        GisObjectRef::parseFeature(ref, &source, &fid);
        GISVectorLayer *gis = nullptr;
        for (auto *layer : m_canvas->layers())
            if (layer->layerId() == source)
                gis = qobject_cast<GISVectorLayer *>(layer);
        if (!gis || gis->isTemporary() || !gis->ogrLayer() || qobject_cast<TimeSeriesLayer *>(gis))
            return fail(
                tr("Choose a permanent GIS feature, not a catalogue preview or station marker."));
        auto *f = gis->ogrLayer()->GetFeature(fid);
        if (!f)
            return fail(tr("The selected feature is no longer available."));
        const auto cleanup = qScopeGuard([f] { OGRFeature::DestroyFeature(f); });
        link = {{"kind", "gis"},
                {"layerId", source},
                {"source", QFileInfo(gis->filePath()).absoluteFilePath()},
                {"table", gis->ogrLayerName()},
                {"fid", QString::number(fid)},
                {"revision", fileRevision(gis)}};
        for (const auto &key :
             {"location_id", "monitoring_location_id", "catalogue_id", "uuid", "id"}) {
            const int idx = f->GetFieldIndex(key);
            if (idx >= 0 && f->IsFieldSetAndNotNull(idx)) {
                link["keyField"] = QString::fromUtf8(key);
                link["keyValue"] = QString::fromUtf8(f->GetFieldAsString(idx));
                if (resolveFid(gis, link) == fid)
                    break;
                link.remove("keyField");
                link.remove("keyValue");
            }
        }
    } else {
        ::SWMMModelLayer *found = nullptr;
        for (auto *layer : m_canvas->layers())
            if (auto *model = qobject_cast<::SWMMModelLayer *>(layer); modelHas(model, ref)) {
                if (found)
                    return fail(tr("The model object is ambiguous across loaded model layers."));
                found = model;
            }
        if (!found)
            return fail(tr("The selected model object is unavailable."));
        link = {{"kind", "model"},
                {"layerId", found->layerId()},
                {"objectType", int(ref.objectType)},
                {"name", ref.name},
                {"signature", modelSignature(found, ref)}};
    }
    QJsonArray after;
    const QSet<QString> selected(ids.begin(), ids.end());
    for (const auto &v : m_associations)
        if (!selected.contains(v.toObject().value("seriesId").toString()))
            after.append(v);
    for (const auto &id : ids)
        if (!seriesInfo(id).id.isEmpty()) {
            auto value = link;
            value["seriesId"] = id;
            after.append(value);
        }
    const QPointer<TimeSeriesLayer> self(this);
    auto apply = [self](const QJsonArray &value) {
        if (self)
            self->applyAssociations(value);
    };
    m_canvas->undoStack()->push(new LinkCommand(apply, m_associations, after));
    if (error)
        error->clear();
    return true;
}
void TimeSeriesLayer::detachFeatures(const QStringList &ids)
{
    const QSet<QString> selected(ids.begin(), ids.end());
    QJsonArray after;
    for (const auto &v : m_associations)
        if (!selected.contains(v.toObject().value("seriesId").toString()))
            after.append(v);
    if (after == m_associations)
        return;
    const QPointer<TimeSeriesLayer> self(this);
    auto apply = [self](const QJsonArray &value) {
        if (self)
            self->applyAssociations(value);
    };
    if (m_canvas)
        m_canvas->undoStack()->push(new LinkCommand(apply, m_associations, after));
    else
        apply(after);
}
void TimeSeriesLayer::showData(QWidget *parent, const QString &initialSeries)
{
    if (!m_browser)
        m_browser = new TimeSeriesLayerBrowser(this, parent);
    if (!initialSeries.isEmpty())
        setSelectedSeriesIds({initialSeries});
    m_browser->show();
    m_browser->raise();
    m_browser->activateWindow();
}
void TimeSeriesLayer::requestPlot(const QStringList &requested, QWidget *parent)
{
    QStringList ids = requested.isEmpty() ? seriesIds() : requested;
    if (ids.isEmpty())
        return;
    if (ids.size() == 1) {
        emit plotRequested(ids);
        return;
    }
    QPointer<TimeSeriesLayer> self(this);
    // The nested event loop may close its former parent or unload the owning
    // document. Keep this stack dialog parentless and reject on either lifetime.
    QDialog chooser;
    chooser.setWindowModality(Qt::ApplicationModal);
    connect(this, &QObject::destroyed, &chooser, &QDialog::reject);
    if (parent)
        connect(parent, &QObject::destroyed, &chooser, &QDialog::reject);
    chooser.setWindowTitle(tr("Plot time series"));
    auto *layout = new QVBoxLayout(&chooser);
    auto *list = new QListWidget(&chooser);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (const auto &id : ids) {
        auto *item = new QListWidgetItem(seriesCaption(id), list);
        item->setData(Qt::UserRole, id);
        item->setSelected(m_selected.contains(id));
    }
    layout->addWidget(list);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &chooser);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &chooser, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &chooser, &QDialog::reject);
    chooser.resize(620, 300);
    if (chooser.exec() != QDialog::Accepted || !self)
        return;
    ids.clear();
    for (auto *item : list->selectedItems())
        ids.append(item->data(Qt::UserRole).toString());
    if (!ids.isEmpty())
        emit self->plotRequested(ids);
}
void TimeSeriesLayer::locateSource(QWidget *parent)
{
    const QPointer<TimeSeriesLayer> self(this);
    const QPointer<QWidget> caller(parent);
    QFileDialog dialog(nullptr, tr("Locate time-series dataset"),
                       QFileInfo(m_sourcePath).absolutePath(),
                       tr("Time-series GeoPackage (*.gpkg)"));
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setWindowModality(Qt::ApplicationModal);
    connect(this, &QObject::destroyed, &dialog, &QDialog::reject);
    if (parent)
        connect(parent, &QObject::destroyed, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || !self || dialog.selectedFiles().isEmpty())
        return;
    const auto path = dialog.selectedFiles().first();
    const auto oldPath = self->m_sourcePath;
    const auto nameBefore = self->name();
    QString error;
    if (!self->openTimeSeries(path, self->m_expectedId, &error)) {
        if (!self)
            return;
        self->openTimeSeries(oldPath, self->m_expectedId);
        if (!self)
            return;
        self->setName(nameBefore);
        // A modeless message cannot retain a dead borrowed window through another nested loop.
        auto *message = new QMessageBox(QMessageBox::Warning, tr("Time-series source"), error,
                                        QMessageBox::Ok, caller.data());
        message->setAttribute(Qt::WA_DeleteOnClose);
        message->open();
    } else if (self) {
        self->setName(nameBefore);
        emit self->associationsChanged();
    }
}

QList<TimeSeriesMapHit> timeSeriesHitsAt(MapCanvas *canvas, double x, double y, double tolerance)
{
    QList<TimeSeriesMapHit> hits;
    if (!canvas)
        return hits;
    const auto network = openswmmvis::pickNetworkForPlot(canvas, x, y, tolerance);
    const auto layers = canvas->layers();
    for (auto it = layers.crbegin(); it != layers.crend(); ++it) {
        auto *series = qobject_cast<TimeSeriesLayer *>(*it);
        if (!series || !series->isVisible() || series->opacity() <= 0 || !series->dataset())
            continue;
        QStringList ids;
        QSet<long long> fids;
        for (const auto &feature : series->identifyAt(x, y, canvas->canvasSRS(), tolerance))
            fids.insert(feature.value("fid").toLongLong());
        ids.append(series->seriesForLocations(fids));
        if (network.object.valid)
            ids.append(series->seriesForFeature(network.ref(), network.model));
        for (auto target = layers.crbegin(); target != layers.crend(); ++target) {
            auto *gis = qobject_cast<GISVectorLayer *>(*target);
            if (!gis || qobject_cast<TimeSeriesLayer *>(gis) || !gis->isVisible() ||
                gis->opacity() <= 0)
                continue;
            for (const auto &feature : gis->identifyAt(x, y, canvas->canvasSRS(), tolerance))
                ids.append(series->seriesForFeature(
                    GisObjectRef::feature(gis->layerId(), feature.value("fid").toLongLong()), gis));
        }
        ids.removeDuplicates();
        if (!ids.isEmpty())
            hits.append({series, ids});
    }
    return hits;
}
