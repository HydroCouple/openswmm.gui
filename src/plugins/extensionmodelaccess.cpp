/**
 * @file extensionmodelaccess.cpp
 * @brief Project/model facades, native transactions and spatial preview adapters.
 * @details ModelAccess retains guarded model/project facades for the extension lifetime.
 * Closed windows leave inert facades, so previously returned interface addresses remain safe
 * to query. Each facade scopes project state to its owning extension.
 *
 * Native and mediated edit scopes share per-window leases with normal simulations. Before/after
 * snapshots use ProjectSaveOutputs staging, constrain companion writes to a dedicated recovery
 * directory, and temporarily request inline mesh serialization. Commit rebuilds the GUI mirror
 * and pushes one snapshot command; abort reopens the before snapshot. Snapshot replay opens a
 * replacement engine successfully before releasing the current one. Files remain reviewable
 * under the project or the user-data directory.
 *
 * Spatial services use explicit traditional GIS axis order. Viewport edges are sampled before
 * transformation. Loaded-layer boundaries preserve polygon holes and multipart structure;
 * transient preview items are transformed to the canvas CRS and never become persistent layers.
 * Layer/filter/selection notifications let consumers discard stale catalogue previews.
 * All facade and engine operations are confined to the GUI thread.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionmodelaccess.h"
#include "plugins/extensionattributes.h"
#include "plugins/extensionstateregistry.h"
#include <QScopeGuard>
#include "render/renderers/graduatedrenderer.h"
#include "swmmvisprojectwindow.h"
#include "map/mapcanvas.h"
#include "map/spatialreferencesystem.h"
#include "map/openswmmvisscene.h"
#include "layers/gisvectorlayer.h"
#include "layers/featurelayer.h"
#include "layers/gisrasterlayer.h"
#include "layers/swmmresultslayer.h"
#include "layers/swmm2dresultslayer.h"
#include "project/projectserializer.h"
#include "project/projectsaveoutputs.h"
#include "map/mapundostack.h"
#include "map/importcommands.h"
#include "ui/properties/swmmsubcatchpropertyadapter.h"
#include "core/swmmdatetime.h"
#include "selection/selectionmanager.h"
#include <openswmm/engine/openswmm_model.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>
#include <QDir>
#include <QRegularExpression>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QGraphicsItemGroup>
#include <QGraphicsPathItem>
#include <QGraphicsEllipseItem>
#include <QPainterPath>
#include <QPen>
#include <QMetaProperty>
#include <QStandardPaths>
#include <QTimer>
#include <cmath>
#include <memory>
#include <map>
#include <utility>

using namespace OpenSWMM::Sdk;
namespace openswmmvis
{
namespace
{
QUuid instanceId(SWMMVisProjectWindow *window)
{
    return window ? window->property("extensionInstanceId").toUuid() : QUuid{};
}
/** Resolve a public object category to the existing layer namespace mask. */
quint8 kindMask(const QString &category)
{
    if (category == "node")
        return SWMMModelLayer::kKindNode;
    if (category == "link")
        return SWMMModelLayer::kKindLink;
    if (category == "subcatchment")
        return SWMMModelLayer::kKindCatch;
    if (category == "gage")
        return SWMMModelLayer::kKindGage;
    return 0;
}

/** Write an independent, reviewable snapshot and its companions under the model directory. */
QString writeSnapshot(SWMMVisProjectWindow *window, QString *error)
{
    if (!window || window->isClosing() || !window->modelLayer() ||
        !window->modelLayer()->engine()) {
        if (error)
            *error = QStringLiteral("Open a model before taking an extension snapshot.");
        return {};
    }
    const QDir base(
        window->isUntitled() || window->modelFilePath().isEmpty()
            ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                  .filePath("unsaved/" + instanceId(window).toString(QUuid::WithoutBraces))
            : QFileInfo(window->modelFilePath()).absolutePath());
    const auto directory =
        QStringLiteral(".swmmvis/edits/%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!base.mkpath(directory)) {
        if (error)
            *error = QStringLiteral("Cannot create the project snapshot directory.");
        return {};
    }
    const auto path = base.filePath(directory + "/model.inp");
    ProjectSaveOutputs outputs;
    outputs.protect(window->modelFilePath(), -1);
    struct SnapshotMapper {
        ProjectSaveOutputs *outputs;
        QString root;
        QByteArray storage;
        QString error;
    } mapper{&outputs, QFileInfo(path).absolutePath() + '/', {}, {}};
    const auto mapOutput = [](void *context, const char *finalPath, int kind) -> const char * {
        auto &mapped = *static_cast<SnapshotMapper *>(context);
        const auto destination = QFileInfo(QString::fromUtf8(finalPath)).absoluteFilePath();
        if (kind < ProjectSaveOutputs::Model || kind > ProjectSaveOutputs::Component ||
            !destination.startsWith(mapped.root)) {
            mapped.error = QStringLiteral("Snapshot output would escape its recovery directory: %1")
                               .arg(destination);
            return nullptr;
        }
        mapped.storage = mapped.outputs
                             ->stage(destination, static_cast<ProjectSaveOutputs::Role>(kind),
                                     kind == ProjectSaveOutputs::Model)
                             .toUtf8();
        return mapped.storage.isEmpty() ? nullptr : mapped.storage.constData();
    };
    auto engine = window->modelLayer()->engine();
    // Preserve the source mesh reference while requesting a self-contained inline snapshot.
    QByteArray meshReference(32768, '\0');
    const bool haveMeshReference = swmm_options_get_ext(engine, "MESH_FILE", meshReference.data(),
                                                        meshReference.size()) == SWMM_OK;
    if (haveMeshReference && int(qstrlen(meshReference.constData())) == meshReference.size() - 1) {
        if (error)
            *error = "The engine mesh reference is too long to snapshot safely.";
        return {};
    }
    if (haveMeshReference)
        meshReference.resize(int(qstrlen(meshReference.constData())));
    if (haveMeshReference && swmm_options_set_ext(engine, "MESH_FILE", "") != SWMM_OK) {
        if (error)
            *error = "The engine refused inline mesh serialization for a snapshot.";
        return {};
    }
    window->modelLayer()->flushDataObjectRegistries();
    const int warningsBefore = swmm_get_warning_count(engine);
    const int result =
        swmm_model_write_staged(engine, path.toUtf8().constData(), mapOutput, &mapper);
    const int restored = haveMeshReference
                             ? swmm_options_set_ext(engine, "MESH_FILE", meshReference.constData())
                             : SWMM_OK;
    QStringList diagnostics;
    bool losesData = false;
    for (int i = warningsBefore; i < swmm_get_warning_count(engine); ++i) {
        const auto warning = QString::fromUtf8(swmm_get_warning_at(engine, i));
        diagnostics.append(warning);
        losesData = losesData || warning.contains("lost from this save");
    }
    if (result != SWMM_OK || restored != SWMM_OK || losesData || !outputs.publish()) {
        if (error)
            *error = QStringLiteral(
                         "Could not write the model snapshot (engine %1, mesh restore %2): %3 %4")
                         .arg(result)
                         .arg(restored)
                         .arg(mapper.error, outputs.error()) +
                     " " + diagnostics.join("; ");
        return {};
    }
    return path;
}

/** Reopen before replacing the live engine, so parse failure keeps the current model intact. */
bool restoreSnapshot(SWMMVisProjectWindow *window, const QString &path, QString *error)
{
    if (!window || !window->modelLayer())
        return false;
    auto *engine = SWMMModelLayer::openEngineForPath(path, error);
    if (!engine)
        return false;
    window->modelLayer()->replaceEngineForEdit(engine);
    window->setHasChanges(true);
    return true;
}

/** Lease bookkeeping is per model, shared by every extension, and GUI-thread confined. */
class EngineLease final : public IEngineLease
{
public:
    EngineLease(SWMMVisProjectWindow *window, LeaseKind kind, QString holder)
        : mWindow(window), mKind(kind), mHolder(std::move(holder))
    {
        if (kind == LeaseKind::Write)
            window->setProperty("extensionWriteLease", mHolder);
        else
            window->setProperty("extensionReadLeases",
                                window->property("extensionReadLeases").toInt() + 1);
    }
    ~EngineLease() override
    {
        if (!mWindow)
            return;
        if (mKind == LeaseKind::Write)
            mWindow->setProperty("extensionWriteLease", QString());
        else
            mWindow->setProperty("extensionReadLeases",
                                 qMax(0, mWindow->property("extensionReadLeases").toInt() - 1));
    }
    void *engine() const override
    {
        return mWindow && !mWindow->isClosing() && mWindow->modelLayer()
                   ? mWindow->modelLayer()->engine()
                   : nullptr;
    }

private:
    QPointer<SWMMVisProjectWindow> mWindow;
    LeaseKind mKind;
    QString mHolder;
};

/** One undo command owns before/after paths; the initial redo is already reflected in the GUI. */
class NativeEditCommand final : public QUndoCommand
{
public:
    NativeEditCommand(SWMMVisProjectWindow *window, QString before, QString after,
                      const QString &label)
        : QUndoCommand(label), mWindow(window), mBefore(std::move(before)), mAfter(std::move(after))
    {
    }
    void undo() override { replay(mBefore); }
    void redo() override
    {
        if (mInitial)
            mInitial = false;
        else
            replay(mAfter);
    }

private:
    void replay(const QString &path)
    {
        QString error;
        if (!restoreSnapshot(mWindow, path, &error))
            qWarning().noquote() << "Extension snapshot replay failed:" << path << error;
    }
    QPointer<SWMMVisProjectWindow> mWindow;
    QString mBefore, mAfter;
    bool mInitial = true;
};

/** Snapshot transaction shared by native editing and mediated editing. Abort precedes window teardown. */
class NativeEditScope : public QObject, public INativeEditScope
{
public:
    NativeEditScope(SWMMVisProjectWindow *window, QString label,
                    std::unique_ptr<IEngineLease> lease, QString before, const QString &plugin)
        : mWindow(window), mLabel(std::move(label)), mLease(std::move(lease)),
          mBefore(std::move(before))
    {
        mMarker = QStringLiteral("extensions/operations/%1/%2")
                      .arg(plugin, QUuid::createUuid().toString(QUuid::WithoutBraces));
        QSettings settings;
        settings.setValue(mMarker, mLabel);
        settings.sync();
        mBlocked = window->modelLayer()->blockSignals(true);
        mWasDirty = window->hasChanges();
        connect(window, &SWMMVisProjectWindow::aboutToClose, this, [this]() { abort(); });
    }
    ~NativeEditScope() override
    {
        if (!mFinished)
            abort();
    }
    void *engine() const override { return !mFinished && mLease ? mLease->engine() : nullptr; }
    bool commit(QString *error = nullptr) override
    {
        if (!engine())
            return false;
        const auto after = writeSnapshot(mWindow, error);
        if (after.isEmpty()) {
            abort();
            return false;
        }
        mWindow->modelLayer()->blockSignals(mBlocked);
        mWindow->modelLayer()->rebuildFromEngine();
        mWindow->setHasChanges(true);
        mWindow->canvas()->undoStack()->push(
            new NativeEditCommand(mWindow, mBefore, after, mLabel));
        mFinished = true;
        mLease.reset();
        clearMarker();
        return true;
    }
    bool abort(QString *error = nullptr) override
    {
        if (mFinished)
            return true;
        if (!mWindow || !mWindow->modelLayer()) {
            mFinished = true;
            mLease.reset();
            clearMarker();
            return false;
        }
        mWindow->modelLayer()->blockSignals(mBlocked);
        const bool ok = restoreSnapshot(mWindow, mBefore, error);
        if (ok)
            mWindow->setHasChanges(mWasDirty);
        mFinished = true;
        mLease.reset();
        clearMarker();
        return ok;
    }

protected:
    QPointer<SWMMVisProjectWindow> mWindow;

private:
    /** @brief Clear this transaction's durable crash marker after completion or rollback. */
    void clearMarker()
    {
        QSettings settings;
        settings.remove(mMarker);
        settings.sync();
    }
    QString mLabel;
    std::unique_ptr<IEngineLease> mLease;
    QString mBefore;
    QString mMarker;
    bool mBlocked = false, mFinished = false, mWasDirty = false;
};

/** Mediated operations reuse the GUI's property adapters and apply methods inside one transaction. */
class EditScope final : public NativeEditScope, public IEditScope
{
public:
    using NativeEditScope::NativeEditScope;
    bool setAttribute(const QString &category, const QString &name, const QString &field,
                      const QVariant &value, QString *error) override
    {
        if (!engine())
            return false;
        auto *layer = mWindow->modelLayer();
        if (!kindMask(category) || layer->identifyByName(name, kindMask(category)).isEmpty()) {
            if (error)
                *error = "Unknown object category or model object.";
            return failed();
        }
        std::unique_ptr<QObject> adapter(
            category == "subcatchment"
                ? static_cast<QObject *>(new SWMMSubcatchPropertyAdapter(engine(), name))
                : SetAdapterPropertiesCommand::createAdapter(layer, kindMask(category), name));
        if (!adapter) {
            if (error)
                *error = "Unknown object category or model object.";
            return failed();
        }
        const auto key = field.toUtf8();
        const auto index = adapter->metaObject()->indexOfProperty(key.constData());
        if (index < 0 || !adapter->metaObject()->property(index).isWritable()) {
            if (error)
                *error = "The attribute does not exist or is read-only.";
            return failed();
        }
        if (!adapter->setProperty(key.constData(), value))
            return failed();
        const auto actual = adapter->property(key.constData());
        auto expected = value;
        if (!expected.convert(actual.metaType()) ||
            (actual.metaType().id() == QMetaType::Double
                 ? !qFuzzyCompare(actual.toDouble() + 1., expected.toDouble() + 1.)
                 : actual != expected)) {
            if (error)
                *error = "The engine did not accept the requested attribute value.";
            return failed();
        }
        return true;
    }
    bool addNode(const QString &name, int type, QPointF point) override
    {
        return record(engine() &&
                      mWindow->modelLayer()->applyNodeAdd(name, type, point.x(), point.y()));
    }
    bool addLink(const QString &name, int type, const QString &from, const QString &to,
                 const QVector<QPointF> &vertices) override
    {
        return record(engine() &&
                      mWindow->modelLayer()->applyLinkAdd(name, type, from, to, vertices));
    }
    bool addGage(const QString &name, QPointF point) override
    {
        return record(engine() && mWindow->modelLayer()->applyGageAdd(name, point.x(), point.y()));
    }
    bool addSubcatchment(const QString &name, const QVector<QPointF> &polygon) override
    {
        return record(engine() && mWindow->modelLayer()->applySubcatchAdd(name, polygon));
    }
    bool rename(const QString &category, const QString &name, const QString &newName) override
    {
        return record(engine() && kindMask(category) &&
                      mWindow->modelLayer()->applyRename(name, newName, kindMask(category)));
    }
    bool addTimeSeries(const QString &name, const QVector<QDateTime> &times,
                       const QVector<double> &values, bool replace) override
    {
        if (!engine() || times.size() != values.size() || times.isEmpty())
            return failed();
        for (qsizetype i = 0; i < times.size(); ++i)
            if (!times[i].isValid() || !std::isfinite(values[i]) || (i && times[i] <= times[i - 1]))
                return failed();
        int index = swmm_table_index(engine(), name.toUtf8().constData());
        int tableType = -1;
        if (index >= 0 &&
            (swmm_table_get_type(engine(), index, &tableType) != SWMM_OK || tableType != 0))
            index = -1;
        if (index >= 0 && !replace)
            return failed();
        if (index < 0 && swmm_timeseries_add(engine(), name.toUtf8().constData()) != SWMM_OK)
            return failed();
        index = swmm_table_index(engine(), name.toUtf8().constData());
        if (index < 0 || swmm_table_clear(engine(), index) != SWMM_OK)
            return failed();
        for (qsizetype i = 0; i < times.size(); ++i)
            if (swmm_table_add_point(engine(), index, core::qDateTimeToSwmmDateTime(times[i]),
                                     values[i]) != SWMM_OK)
                return failed();
        return true;
    }
    bool setGageSeries(const QString &gage, const QString &series, double seconds,
                       int format) override
    {
        if (!engine() || !std::isfinite(seconds) || seconds <= 0)
            return failed();
        const int index = swmm_gage_index(engine(), gage.toUtf8().constData());
        return record(index >= 0 &&
                      swmm_gage_set_timeseries(engine(), index, series.toUtf8().constData()) ==
                          SWMM_OK &&
                      swmm_gage_set_rain_interval(engine(), index, seconds) == SWMM_OK &&
                      swmm_gage_set_rain_type(engine(), index, format) == SWMM_OK);
    }
    bool addInflow(const QString &node, const QString &constituent, const QString &series,
                   const QString &type, double scale, double baseline) override
    {
        if (!engine())
            return failed();
        const int index = swmm_node_index(engine(), node.toUtf8().constData());
        return record(index >= 0 &&
                      swmm_ext_inflow_add(engine(), index, constituent.toUtf8().constData(),
                                          series.toUtf8().constData(), type.toUtf8().constData(),
                                          1., scale, baseline, nullptr) == SWMM_OK);
    }
    bool commit(QString *error = nullptr) override
    {
        if (mFailed) {
            if (error)
                *error = "An edit failed; the transaction has been rolled back.";
            NativeEditScope::abort();
            return false;
        }
        return NativeEditScope::commit(error);
    }
    bool abort(QString *error = nullptr) override { return NativeEditScope::abort(error); }

private:
    bool failed()
    {
        mFailed = true;
        return false;
    }
    bool record(bool ok)
    {
        if (!ok)
            mFailed = true;
        return ok;
    }
    bool mFailed = false;
};
class ModelFacade;
class ProjectFacade final : public QObject, public IProject
{
public:
    ProjectFacade(SWMMVisProjectWindow *window, QString plugin)
        : mWindow(window), mPlugin(std::move(plugin))
    {
    }
    ~ProjectFacade() override { clearPreview(); }
    ModelFacade *model = nullptr;
    QUuid id() const override { return mWindow ? instanceId(mWindow) : QUuid{}; }
    QString path() const override
    {
        if (!mWindow)
            return {};
        const auto stored = mWindow->property("extensionProjectPath").toString();
        return stored.isEmpty() && !mWindow->modelFilePath().isEmpty()
                   ? QFileInfo(mWindow->modelFilePath()).absolutePath() + "/" +
                         QFileInfo(mWindow->modelFilePath()).completeBaseName() + ".oswp"
                   : stored;
    }
    QString directory() const override
    {
        const auto projectPath = path();
        return projectPath.isEmpty() ? QString{} : QFileInfo(projectPath).absolutePath();
    }
    QList<IModelInstance *> instances() const override;
    QJsonObject extensionData() const override
    {
        return mWindow ? mWindow->property("extensionData").toJsonObject().value(mPlugin).toObject()
                       : QJsonObject{};
    }
    void setExtensionData(const QJsonObject &data) override
    {
        const QPointer<SWMMVisProjectWindow> owner(mWindow);
        if (!owner || owner->isClosing())
            return;
        auto all = owner->property("extensionData").toJsonObject();
        all[mPlugin] = data;
        owner->setProperty("extensionData", all);
        if (!owner || owner->isClosing())
            return;
        owner->setHasChanges(true);
        if (owner && !owner->isClosing())
            owner->updateExtensionSession(StateChange::ProjectState);
    }
    QString autosave(QString *error) const override
    {
        QString localError;
        if (!error)
            error = &localError;
        if (!mWindow || mWindow->isClosing()) {
            *error = "The model is closed.";
            return {};
        }
        auto *registry = ExtensionStateRegistry::instance();
        const QPointer<SWMMVisProjectWindow> owner(mWindow);
        const bool wasDirty = mWindow->hasChanges();
        const QString originalPath = mWindow->property("extensionProjectPath").toString();
        ExtensionSaveOperation operation(registry, {mWindow->extensionSessionId()},
                                         SaveKind::Recovery, {}, error);
        // Collection may refresh plugin presentation state; recovery must keep the current
        // project identity and dirty indication, including when preparation fails.
        const auto preserveProject = qScopeGuard([owner, wasDirty, originalPath] {
            if (owner && !owner->isClosing()) {
                owner->setExtensionProjectIdentity({}, originalPath);
                owner->setHasChanges(wasDirty);
            }
        });
        if (!operation.valid() || !owner || owner->isClosing())
            return {};
        if (!mWindow->property("extensionWriteLease").toString().isEmpty() ||
            mWindow->property("extensionRunCount").toInt()) {
            *error = "The model is locked by an edit/run.";
            return {};
        }
        const auto snapshot = writeSnapshot(mWindow, error);
        if (snapshot.isEmpty())
            return {};
        const auto output = QFileInfo(snapshot).absoluteDir().filePath("recovery.oswp");
        operation.setTargetPath(output);
        const bool written = ProjectSerializer::saveToFile(output, mWindow, error);
        if (!written)
            return {};
        QFile file(output);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error)
                *error = file.errorString();
            return {};
        }
        auto root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        auto sessions = root["sessions"].toArray();
        if (sessions.isEmpty()) {
            if (error)
                *error = "The recovery project contains no model session.";
            return {};
        }
        auto session = sessions[0].toObject();
        session["inpPath"] = QFileInfo(snapshot).fileName();
        sessions[0] = session;
        root["sessions"] = sessions;
        QSaveFile saved(output);
        const auto bytes = QJsonDocument(root).toJson();
        if (!saved.open(QIODevice::WriteOnly) || saved.write(bytes) != bytes.size() ||
            !saved.commit()) {
            if (error)
                *error = saved.errorString();
            return {};
        }
        operation.succeed();
        return output;
    }
    QString crsWkt() const override
    {
        auto *srs = mWindow && mWindow->modelLayer() ? mWindow->modelLayer()->srs() : nullptr;
        return srs ? srs->toWkt() : QString{};
    }
    QString crsAuthority() const override
    {
        auto *srs = mWindow && mWindow->modelLayer() ? mWindow->modelLayer()->srs() : nullptr;
        return srs ? srs->toAuthority() : QString{};
    }
    bool toLonLat(QPointF point, QPointF *result) const override
    {
        return transform(point, result, true);
    }
    bool fromLonLat(QPointF point, QPointF *result) const override
    {
        return transform(point, result, false);
    }
    QList<QVariantMap> layers() const override
    {
        QList<QVariantMap> result;
        if (mWindow && mWindow->canvas())
            for (auto *layer : mWindow->canvas()->layers())
                result.append({{"id", layer->layerId()},
                               {"title", layer->name()},
                               {"visible", layer->isVisible()}});
        return result;
    }
    QString addRasterLayer(const QString &path, const QString &title, QString *error) override
    {
        if (!mWindow || !mWindow->modelLayer() || !QFileInfo::exists(path)) {
            spatialError(error, "Choose an existing local raster file in an open project.");
            return {};
        }
        auto *layer = new GISRasterLayer(path, mWindow->modelLayer()->workspace());
        if (layer->bandCount() <= 0) {
            delete layer;
            spatialError(error, "GDAL could not open the raster file.");
            return {};
        }
        if (!title.isEmpty())
            layer->setName(title);
        mWindow->canvas()->addLayer(layer);
        mWindow->setHasChanges(true);
        return layer->layerId();
    }
    QString addFeatureLayer(const QString &path, const QString &title, QString *error) override
    {
        if (!mWindow || !mWindow->modelLayer() || !QFileInfo::exists(path)) {
            spatialError(error, "Choose an existing local vector file in an open project.");
            return {};
        }
        auto *layer = new GISVectorLayer(path, {}, mWindow->modelLayer()->workspace());
        if (!layer->ogrLayer()) {
            delete layer;
            spatialError(error, "GDAL could not open the vector file.");
            return {};
        }
        if (!title.isEmpty())
            layer->setName(title);
        mWindow->canvas()->addLayer(layer);
        mWindow->setHasChanges(true);
        return layer->layerId();
    }
    QJsonObject viewportBoundary(QString *error) const override
    {
        if (!mWindow || !mWindow->canvas() || !mWindow->canvas()->canvasSRS())
            return spatialError(error, "The map has no coordinate system.");
        const auto extent = mWindow->canvas()->extent();
        if (!extent.isValid())
            return spatialError(error, "The map viewport has no usable area.");
        OGRLinearRing ring;
        const QPointF corners[]{
            QPointF(extent.xMin(), extent.yMin()), QPointF(extent.xMax(), extent.yMin()),
            QPointF(extent.xMax(), extent.yMax()), QPointF(extent.xMin(), extent.yMax())};
        for (int edge = 0; edge < 4; ++edge)
            for (int step = 0; step < 16; ++step) {
                const auto point =
                    corners[edge] + (corners[(edge + 1) % 4] - corners[edge]) * (step / 16.);
                ring.addPoint(point.x(), point.y());
            }
        ring.closeRings();
        OGRPolygon polygon;
        polygon.addRing(&ring);
        return geographicGeometry(&polygon, mWindow->canvas()->canvasSRS(), error);
    }
    QList<QVariantMap> polygonLayers() const override
    {
        QList<QVariantMap> result;
        if (!mWindow || !mWindow->canvas())
            return result;
        for (auto *base : mWindow->canvas()->layers()) {
            auto *layer = qobject_cast<GISVectorLayer *>(base);
            if (!layer || !layer->ogrLayer())
                continue;
            const auto type = wkbFlatten(layer->ogrLayer()->GetGeomType());
            if (type == wkbPolygon || type == wkbMultiPolygon || type == wkbUnknown)
                result.append({{"id", layer->layerId()},
                               {"title", layer->name()},
                               {"count", layer->featureCount()}});
        }
        return result;
    }
    QList<QVariantMap> boundaryFeatures(const QString &id, bool selectedOnly, int offset,
                                        int limit) const override
    {
        QList<QVariantMap> result;
        auto *layer = polygonLayer(id);
        if (!layer || offset < 0 || limit <= 0)
            return result;
        auto *source = layer->ogrLayer();
        const auto selected = layer->selectedFeatureIds();
        source->ResetReading();
        int index = 0;
        while (auto *feature = source->GetNextFeature()) {
            const auto *geometry = feature->GetGeometryRef();
            const auto type = geometry ? wkbFlatten(geometry->getGeometryType()) : wkbUnknown;
            if ((type == wkbPolygon || type == wkbMultiPolygon) &&
                (!selectedOnly || selected.contains(feature->GetFID())) && index++ >= offset) {
                QString title = QStringLiteral("Feature %1").arg(feature->GetFID());
                for (const auto *key : {"name", "Name", "title", "id"}) {
                    const int field = feature->GetFieldIndex(key);
                    if (field >= 0 && feature->IsFieldSetAndNotNull(field)) {
                        title = QString::fromUtf8(feature->GetFieldAsString(field));
                        break;
                    }
                }
                result.append({{"id", qint64(feature->GetFID())},
                               {"title", title},
                               {"selected", selected.contains(feature->GetFID())}});
            }
            OGRFeature::DestroyFeature(feature);
            if (result.size() >= qMin(limit, 5000))
                break;
        }
        source->ResetReading();
        return result;
    }
    QJsonObject boundaryPolygon(const QString &id, qint64 fid, QString *error) const override
    {
        auto *layer = polygonLayer(id);
        if (!layer)
            return spatialError(error, "The boundary layer is no longer loaded.");
        auto *feature = layer->ogrLayer()->GetFeature(fid);
        if (!feature)
            return spatialError(error, "The boundary feature no longer exists.");
        auto *geometry = feature->GetGeometryRef();
        QJsonObject result;
        const auto type = geometry ? wkbFlatten(geometry->getGeometryType()) : wkbUnknown;
        if (type == wkbPolygon || type == wkbMultiPolygon)
            result = geographicGeometry(geometry, layer->srs(), error);
        else
            spatialError(error, "Choose a polygon or multipolygon boundary.");
        OGRFeature::DestroyFeature(feature);
        return result;
    }
    void clearPreview() override
    {
        if (mPreviewScene)
            delete mPreview;
        mPreview = nullptr;
        mPreviewScene = nullptr;
    }
    bool showPreview(const QJsonObject &collection, QString *error) override
    {
        if (collection["type"].toString() != "FeatureCollection" ||
            !collection["features"].isArray()) {
            spatialError(error, "Preview data must be a GeoJSON FeatureCollection.");
            return false;
        }
        if (!mWindow || !mWindow->canvas() || !mWindow->canvas()->canvasSRS() ||
            !mWindow->canvas()->canvasSRS()->ogrSpatialReference()) {
            spatialError(error, "The map has no coordinate system.");
            return false;
        }
        auto *scene = mWindow->canvas()->mapScene();
        if (!scene)
            return false;
        auto *group = new QGraphicsItemGroup;
        const QPen outline(QColor(0, 145, 170), 0);
        for (const auto &value : collection["features"].toArray()) {
            const auto json = QJsonDocument(value.toObject()["geometry"].toObject())
                                  .toJson(QJsonDocument::Compact);
            auto *geometry = OGRGeometryFactory::createFromGeoJson(json.constData());
            if (!geometry || !supportedPreviewGeometry(geometry) || !projectGeometry(geometry)) {
                OGRGeometryFactory::destroyGeometry(geometry);
                delete group;
                spatialError(error, "A preview geometry could not be transformed to the map CRS.");
                return false;
            }
            addPreviewGeometry(group, geometry, outline);
            OGRGeometryFactory::destroyGeometry(geometry);
        }
        clearPreview();
        mPreview = group;
        mPreviewScene = scene;
        group->setZValue(1e9);
        scene->addItem(group);
        return true;
    }

private:
    static QJsonObject spatialError(QString *error, const char *text)
    {
        if (error)
            *error = QString::fromUtf8(text);
        return {};
    }
    GISVectorLayer *polygonLayer(const QString &id) const
    {
        if (mWindow && mWindow->canvas())
            for (auto *base : mWindow->canvas()->layers())
                if (base->layerId() == id)
                    return qobject_cast<GISVectorLayer *>(base);
        return nullptr;
    }
    static QJsonObject geographicGeometry(const OGRGeometry *source, SpatialReferenceSystem *srs,
                                          QString *error)
    {
        if (!source || !srs || !srs->ogrSpatialReference())
            return spatialError(error, "The boundary has no coordinate system.");
        auto *copy = source->clone();
        OGRSpatialReference geographic, native(*srs->ogrSpatialReference());
        geographic.importFromEPSG(4326);
        geographic.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        native.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        auto *ct = OGRCreateCoordinateTransformation(&native, &geographic);
        if (!ct || copy->transform(ct) != OGRERR_NONE) {
            if (ct)
                OCTDestroyCoordinateTransformation(ct);
            OGRGeometryFactory::destroyGeometry(copy);
            return spatialError(error,
                                "The boundary could not be transformed to longitude/latitude.");
        }
        OCTDestroyCoordinateTransformation(ct);
        char *json = copy->exportToJson();
        const auto result = QJsonDocument::fromJson(QByteArray(json)).object();
        CPLFree(json);
        OGRGeometryFactory::destroyGeometry(copy);
        return result;
    }
    bool projectGeometry(OGRGeometry *geometry) const
    {
        OGRSpatialReference geographic,
            target(*mWindow->canvas()->canvasSRS()->ogrSpatialReference());
        geographic.importFromEPSG(4326);
        geographic.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        target.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        auto *ct = OGRCreateCoordinateTransformation(&geographic, &target);
        if (!ct)
            return false;
        const bool ok = geometry->transform(ct) == OGRERR_NONE;
        OCTDestroyCoordinateTransformation(ct);
        return ok;
    }
    /** @brief Reject unsupported preview content instead of silently accepting an empty overlay. */
    static bool supportedPreviewGeometry(const OGRGeometry *geometry)
    {
        const auto type = wkbFlatten(geometry->getGeometryType());
        if (type == wkbPoint || type == wkbPolygon)
            return true;
        if (!OGR_GT_IsSubClassOf(type, wkbGeometryCollection))
            return false;
        const auto *collection = geometry->toGeometryCollection();
        for (int i = 0; i < collection->getNumGeometries(); ++i)
            if (!supportedPreviewGeometry(collection->getGeometryRef(i)))
                return false;
        return true;
    }
    static void addPreviewGeometry(QGraphicsItemGroup *group, const OGRGeometry *geometry,
                                   const QPen &pen)
    {
        const auto type = wkbFlatten(geometry->getGeometryType());
        if (type == wkbPoint) {
            const auto *point = geometry->toPoint();
            auto *item = new QGraphicsEllipseItem(-4, -4, 8, 8, group);
            item->setPos(point->getX(), -point->getY());
            item->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            item->setPen(pen);
            item->setBrush(QColor(0, 145, 170, 150));
        } else if (type == wkbPolygon) {
            const auto *polygon = geometry->toPolygon();
            QPainterPath path;
            path.setFillRule(Qt::OddEvenFill);
            const auto ring = [&](const OGRLinearRing *r) {
                if (!r || r->getNumPoints() < 3)
                    return;
                path.moveTo(r->getX(0), -r->getY(0));
                for (int i = 1; i < r->getNumPoints(); ++i)
                    path.lineTo(r->getX(i), -r->getY(i));
                path.closeSubpath();
            };
            ring(polygon->getExteriorRing());
            for (int i = 0; i < polygon->getNumInteriorRings(); ++i)
                ring(polygon->getInteriorRing(i));
            auto *item = new QGraphicsPathItem(path, group);
            item->setPen(pen);
            item->setBrush(QColor(0, 145, 170, 35));
        } else if (OGR_GT_IsSubClassOf(type, wkbGeometryCollection)) {
            const auto *collection = geometry->toGeometryCollection();
            for (int i = 0; i < collection->getNumGeometries(); ++i)
                addPreviewGeometry(group, collection->getGeometryRef(i), pen);
        }
    }
    bool transform(QPointF point, QPointF *result, bool toGeographic) const
    {
        if (!result || crsWkt().isEmpty())
            return false;
        OGRSpatialReference project, geographic;
        if (project.SetFromUserInput(crsWkt().toUtf8().constData()) != OGRERR_NONE ||
            geographic.importFromEPSG(4326) != OGRERR_NONE)
            return false;
        project.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        geographic.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        auto *transform = OGRCreateCoordinateTransformation(toGeographic ? &project : &geographic,
                                                            toGeographic ? &geographic : &project);
        if (!transform)
            return false;
        double x = point.x(), y = point.y();
        const bool ok = transform->Transform(1, &x, &y);
        OCTDestroyCoordinateTransformation(transform);
        if (ok)
            *result = QPointF(x, y);
        return ok;
    }
    QPointer<SWMMVisProjectWindow> mWindow;
    QString mPlugin;
    QPointer<QGraphicsScene> mPreviewScene;
    QGraphicsItemGroup *mPreview = nullptr;
};
class ModelFacade final : public IModelInstance
{
public:
    ModelFacade(SWMMVisProjectWindow *window, ProjectFacade *project, QString plugin)
        : mWindow(window), mId(instanceId(window)), mProject(project), mPlugin(std::move(plugin)),
          mAttributesLayer(window->modelLayer())
    {
    }
    ~ModelFacade() override { clearAttributeProviders(); }
    /** @brief Remove contributions before plugin shutdown releases their provider resources. */
    void clearAttributeProviders()
    {
        for (const auto &id : std::as_const(mAttributeProviders))
            unregisterExtensionAttributes(mAttributesLayer, id);
        mAttributeProviders.clear();
    }
    QUuid id() const override
    {
        return mWindow ? instanceId(mWindow) : mId;
    }
    /** @brief Retain the final persistent identity at the native close boundary. */
    void captureIdentity() { mId = instanceId(mWindow); }
    QString title() const override { return mWindow ? mWindow->windowTitle() : QString{}; }
    QString inpPath() const override { return mWindow ? mWindow->modelFilePath() : QString{}; }
    QString engineVersion() const override
    {
        return mWindow ? mWindow->engineVersion() : QString{};
    }
    QObject *lifetime() const override { return mWindow; }
    IProject *project() const override { return mProject; }
    bool isOpen() const override
    {
        return mWindow && !mWindow->isClosing() && mWindow->modelLayer() &&
               mWindow->modelLayer()->engine();
    }
    QList<QVariantMap> selection() const override
    {
        QList<QVariantMap> values;
        if (isOpen())
            for (const auto &ref : mWindow->selectionManager()->selection())
                values.append({{"type", int(ref.objectType)}, {"name", ref.name}});
        return values;
    }
    void select(const QList<QVariantMap> &objects) override
    {
        if (!isOpen())
            return;
        QSet<SWMMObjectRef> refs;
        for (const auto &value : objects)
            refs.insert({static_cast<SWMMObjectRef::ObjectType>(value.value("type").toInt()),
                         value.value("name").toString()});
        mWindow->selectionManager()->select(refs);
    }
    QString snapshot(QString *error) const override
    {
        auto lease = const_cast<ModelFacade *>(this)->acquireLease(LeaseKind::Read, error);
        return lease ? writeSnapshot(mWindow, error) : QString{};
    }
    QVariantMap attributes(const QString &category, const QString &name) const override
    {
        return isOpen() && kindMask(category)
                   ? mWindow->modelLayer()->identifyByName(name, kindMask(category))
                   : QVariantMap{};
    }
    IAttributeProvider *attributeProvider() const override
    {
        return isOpen() ? mWindow->modelLayer() : nullptr;
    }
    QList<QVariantMap> results() const override
    {
        QList<QVariantMap> rows;
        if (!isOpen() || !mWindow->canvas())
            return rows;
        for (auto *base : mWindow->canvas()->layers()) {
            if (auto *layer = qobject_cast<SWMMResultsLayer *>(base))
                rows.append({{"id", layer->layerId()},
                             {"title", layer->name()},
                             {"kind", "1d"},
                             {"resultsPath", layer->resultsFilePath()},
                             {"reportPath", layer->reportFilePath()},
                             {"active", mWindow->activeResultsLayer() == layer},
                             {"periodCount", layer->totalTimeSteps()},
                             {"start", layer->startDateTime()},
                             {"end", layer->endDateTime()}});
            else if (auto *layer = qobject_cast<SWMM2DResultsLayer *>(base))
                rows.append({{"id", layer->layerId()},
                             {"title", layer->name()},
                             {"kind", "2d"},
                             {"resultsPath", layer->property("snoopy_h5_path")},
                             {"active", mWindow->active2DResultsLayer() == layer},
                             {"periodCount", layer->source() ? layer->source()->timeCount() : 0}});
        }
        return rows;
    }
    std::unique_ptr<IEngineLease> acquireLease(LeaseKind kind, QString *error) override
    {
        if (!isOpen()) {
            if (error)
                *error = "The model is closed.";
            return {};
        }
        const auto owner = mWindow->property("extensionWriteLease").toString();
        const int readers = mWindow->property("extensionReadLeases").toInt();
        if (mWindow->property("extensionRunCount").toInt() > 0 || !owner.isEmpty() ||
            (kind == LeaseKind::Write && readers > 0)) {
            if (error)
                *error = owner.isEmpty() ? "The model is locked by an active run or read lease."
                                         : "The model is locked by " + owner;
            return {};
        }
        return std::make_unique<EngineLease>(mWindow, kind, mPlugin);
    }
    std::unique_ptr<INativeEditScope> nativeEdit(const QString &label, QString *error) override
    {
        auto lease = acquireLease(LeaseKind::Write, error);
        if (!lease)
            return {};
        const auto before = writeSnapshot(mWindow, error);
        if (before.isEmpty())
            return {};
        return std::make_unique<NativeEditScope>(mWindow, label, std::move(lease), before, mPlugin);
    }
    std::unique_ptr<IEditScope> edit(const QString &label, QString *error) override
    {
        auto lease = acquireLease(LeaseKind::Write, error);
        if (!lease)
            return {};
        const auto before = writeSnapshot(mWindow, error);
        if (before.isEmpty())
            return {};
        return std::make_unique<EditScope>(mWindow, label, std::move(lease), before, mPlugin);
    }
    bool registerAttributeProvider(const QString &id, IComputedAttributeProvider *provider,
                                   QObject *context, QString *error) override
    {
        if (!isOpen() || !QRegularExpression("^[A-Za-z][A-Za-z0-9_-]*$").match(id).hasMatch()) {
            if (error)
                *error =
                    "A live model and a valid local attribute-provider identifier are required.";
            return false;
        }
        const auto key = "ext." + mPlugin + '.' + id;
        const QPointer<SWMMModelLayer> layer = mAttributesLayer;
        const auto changed = [layer](bool schema) {
            if (!layer)
                return;
            for (int c = 0; c < SWMMModelLayer::NumCategories; ++c) {
                if (auto *renderer = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(
                        layer->kindRenderer(static_cast<SWMMModelLayer::Category>(c)))) {
                    if (renderer->classifyAttribute().startsWith("ext."))
                        renderer->clearBreaks();
                    if (renderer->sizeAttribute().startsWith("ext."))
                        renderer->setSizeValueRange(0, 0);
                }
            }
            emit layer->extensionAttributesChanged(schema);
            emit layer->repaintRequested();
        };
        if (!registerExtensionAttributes(layer, key, provider, context, changed, error))
            return false;
        mAttributeProviders.insert(key);
        return true;
    }
    void unregisterAttributeProvider(const QString &id) override
    {
        const auto key = "ext." + mPlugin + '.' + id;
        unregisterExtensionAttributes(mAttributesLayer, key);
        mAttributeProviders.remove(key);
    }
    void refreshAttributeProvider(const QString &id) override
    {
        refreshExtensionAttributes(mAttributesLayer, "ext." + mPlugin + '.' + id);
    }

private:
    QPointer<SWMMVisProjectWindow> mWindow;
    QUuid mId;
    ProjectFacade *mProject;
    QString mPlugin;
    QPointer<SWMMModelLayer> mAttributesLayer;
    QSet<QString> mAttributeProviders;
};
QList<IModelInstance *> ProjectFacade::instances() const
{
    return mWindow && model && model->isOpen() ? QList<IModelInstance *>{model}
                                               : QList<IModelInstance *>{};
}

class ModelAccess final : public QObject
{
public:
    explicit ModelAccess(GuiExtensionHost *host) : QObject(host), mHost(host)
    {
        connect(host, &GuiExtensionHost::aboutToShutdown, this, [this]() {
            mStopping = true;
            for (const auto &[window, instance] : mModels)
                instance->clearAttributeProviders();
            for (const auto &instance : mRetired)
                instance->clearAttributeProviders();
        });
    }
    std::function<QList<SWMMVisProjectWindow *>()> windows;
    std::function<SWMMVisProjectWindow *()> active;
    ModelFacade *model(SWMMVisProjectWindow *window)
    {
        if (!window || window->isClosing())
            return nullptr;
        const auto it = mModels.find(window);
        return it == mModels.end() ? nullptr : it->second.get();
    }
    /** @brief Create a facade at an explicit native registration boundary, never from a getter. */
    ModelFacade *track(SWMMVisProjectWindow *window)
    {
        if (mStopping || !window || window->isClosing())
            return nullptr;
        auto it = mModels.find(window);
        if (it != mModels.end())
            return it->second.get();
        auto project = std::make_unique<ProjectFacade>(window, mHost->pluginId());
        auto model = std::make_unique<ModelFacade>(window, project.get(), mHost->pluginId());
        auto *result = model.get();
        project->model = result;
        mProjects.push_back(std::move(project));
        mModels.emplace(window, std::move(model));
        auto restoredViews = std::make_shared<QJsonArray>();
        const auto restore = [this, window, result, restoredViews]() {
            const auto views = window->property("extensionViews").toJsonArray();
            if (views != *restoredViews) {
                mHost->restoreViews(result, views);
                *restoredViews = views;
            }
        };
        connect(window, &SWMMVisProjectWindow::extensionStateRestored, this, restore);
        connect(window, &SWMMVisProjectWindow::extensionStateSaving, this,
                [this, owner = QPointer<SWMMVisProjectWindow>(window), result]() {
                    if (!owner || owner->isClosing())
                        return;
                    const auto all = owner->property("extensionViews").toJsonArray();
                    QJsonArray kept;
                    const auto prefix = "ext." + mHost->pluginId() + ".";
                    for (const auto &entry : all)
                        if (!entry.toObject()["kind"].toString().startsWith(prefix))
                            kept.append(entry);
                    const auto saved = mHost->saveViews(result);
                    if (!owner || owner->isClosing())
                        return;
                    for (const auto &entry : saved)
                        kept.append(entry);
                    owner->setProperty("extensionViews", kept);
                });
        connect(window, &SWMMVisProjectWindow::aboutToClose, this, [this, window, result]() {
            result->captureIdentity();
            mHost->notify("instanceClosed", {{"id", result->id()}});
            // Do not reuse a facade if Qt later allocates another window at the same address.
            auto it = mModels.find(window);
            if (it != mModels.end()) {
                mRetired.push_back(std::move(it->second));
                mModels.erase(it);
            }
        });
        connect(window->selectionManager(), &SelectionManager::selectionChanged, this,
                [this, result]() { mHost->notify("selectionChanged", {{"id", result->id()}}); });
        connect(window->modelLayer(), &SWMMModelLayer::modelEdited, this,
                [this, result]() { mHost->notify("modelEdited", {{"id", result->id()}}); });
        connect(window->modelLayer(), &SWMMModelLayer::attributeChanged, this,
                [this, result](const QString &name) {
                    mHost->notify("attributesChanged", {{"id", result->id()}, {"name", name}});
                });
        // A resize refits the visible bounds but emits only scaleChanged. Compare the
        // actual area/CRS so paired extent/scale signals do not invalidate a review twice.
        auto *canvas = window->canvas();
        const auto viewportState = [canvas]() {
            return std::make_pair(canvas->extent(), canvas->canvasSRS()
                                                        ? canvas->canvasSRS()->toWkt()
                                                        : QString{});
        };
        auto lastViewport = std::make_shared<decltype(viewportState())>(viewportState());
        const auto viewportChanged = [this, result, viewportState, lastViewport]() {
            if (!result->isOpen())
                return;
            const auto current = viewportState();
            if (current == *lastViewport)
                return;
            *lastViewport = current;
            mHost->notify("viewportChanged", {{"id", result->id()}});
        };
        connect(canvas, &MapCanvas::extentChanged, this, viewportChanged);
        connect(canvas, &MapCanvas::scaleChanged, this, viewportChanged);
        connect(canvas, &MapCanvas::canvasSRSChanged, this, viewportChanged);
        connect(window->canvas(), &MapCanvas::layerAdded, this,
                [this, result](OpenSWMMVisLayer *layer) {
                    watchBoundaryLayer(layer, result);
                    mHost->notify("layersChanged", {{"id", result->id()}});
                });
        connect(window->canvas(), &MapCanvas::layerRemoved, this,
                [this, result]() { mHost->notify("layersChanged", {{"id", result->id()}}); });
        connect(window, &SWMMVisProjectWindow::activeResultsLayerChanged, this,
                [this, result]() { mHost->notify("resultsChanged", {{"id", result->id()}}); });
        connect(window, &SWMMVisProjectWindow::active2DResultsLayerChanged, this,
                [this, result]() { mHost->notify("resultsChanged", {{"id", result->id()}}); });
        for (auto *layer : window->canvas()->layers())
            watchBoundaryLayer(layer, result);
        const QPointer<SWMMVisProjectWindow> owner(window);
        restore(); // Explicit backfill of state restored before this plugin host started.
        if (owner && !owner->isClosing())
            mHost->notify("instanceOpened", {{"id", result->id()}});
        return result;
    }
    QList<IProject *> projects()
    {
        QList<IProject *> out;
        for (auto *window : windows()) {
            if (auto *instance = model(window); instance && instance->isOpen())
                out.append(instance->project());
        }
        return out;
    }

private:
    /** @brief Invalidate spatial previews when an existing boundary source changes in place. */
    void watchBoundaryLayer(OpenSWMMVisLayer *base, ModelFacade *instance)
    {
        auto *layer = qobject_cast<GISVectorLayer *>(base);
        if (!layer || mObservedLayers.contains(layer))
            return;
        mObservedLayers.insert(layer);
        connect(layer, &QObject::destroyed, this,
                [this, layer]() { mObservedLayers.remove(layer); });
        const auto changed = [this, instance, id = layer->layerId()]() {
            if (instance->isOpen())
                mHost->notify("boundaryChanged", {{"id", instance->id()}, {"layerId", id}});
        };
        connect(layer, &GISVectorLayer::selectionChanged, this, changed);
        connect(layer, &GISVectorLayer::filterExpressionChanged, this, changed);
        connect(layer, &GISVectorLayer::featureCountChanged, this, changed);
        connect(layer, &OpenSWMMVisLayer::srsChanged, this, changed);
        if (auto *editable = qobject_cast<FeatureLayer *>(layer))
            connect(editable, &FeatureLayer::featuresChanged, this, changed);
    }
    GuiExtensionHost *mHost;
    bool mStopping = false; ///< Prevent new facade creation after plugin resources begin teardown.
    QSet<QObject *> mObservedLayers;
    std::map<SWMMVisProjectWindow *, std::unique_ptr<ModelFacade>> mModels;
    std::vector<std::unique_ptr<ModelFacade>> mRetired;
    std::vector<std::unique_ptr<ProjectFacade>> mProjects;
};
}
void installExtensionModelAccess(GuiExtensionHost *host,
                                 std::function<QList<SWMMVisProjectWindow *>()> windows,
                                 std::function<SWMMVisProjectWindow *()> active)
{
    auto *access = new ModelAccess(host);
    access->windows = std::move(windows);
    access->active = std::move(active);
    if (auto *registry = host->stateRegistry()) {
        QObject::connect(registry, &ExtensionStateRegistry::sessionRegistered, access,
                         [access, host](QObject *owner, const QUuid &) {
            auto *window = qobject_cast<SWMMVisProjectWindow *>(owner);
            if (window && host->mainWindow() && host->mainWindow()->isAncestorOf(window))
                access->track(window);
        });
        QObject::connect(registry, &ExtensionStateRegistry::stateEventReady, access,
                         [access, host, registry](const StateEvent &event) {
            if (event.kind != StateEventKind::ModelReady || !registry->isDeliverable(event))
                return;
            auto *window = qobject_cast<SWMMVisProjectWindow *>(
                registry->ownerForSession(event.session.sessionId));
            // Older consumers bind through activation and have no Ready event. Re-resolve
            // the active model after the outer open barrier without recreating its facade.
            if (window && access->active() == window)
                host->notify("instanceActivated", {{"id", event.session.instanceId}});
        });
        for (const auto &state : registry->snapshot().sessions) {
            auto *window = qobject_cast<SWMMVisProjectWindow *>(registry->ownerForSession(state.sessionId));
            if (window && host->mainWindow() && host->mainWindow()->isAncestorOf(window))
                access->track(window);
        }
        host->setSessionResolver([access, registry](const QUuid &sessionId) -> IModelInstance * {
            auto *window = qobject_cast<SWMMVisProjectWindow *>(registry->ownerForSession(sessionId));
            auto *instance = access->model(window);
            return instance && instance->isOpen() ? instance : nullptr;
        });
    }
    host->setModelAccess([access]() { return access->projects(); },
                         [access]() { return access->model(access->active()); });
    if (auto *mdi = host->mainWindow()->findChild<QMdiArea *>())
        QObject::connect(mdi, &QMdiArea::subWindowActivated, access, [access, host]() {
            // Moving an already-created window into MDI is also an explicit registration boundary.
            access->track(access->active());
            QTimer::singleShot(0, access, [access, host]() {
                auto *instance = access->model(access->active());
                host->notify("instanceActivated", {{"id", instance ? instance->id() : QUuid{}}});
            });
        });
}
}
