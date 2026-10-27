/**
 * @file extensiontimeseriesaccess.cpp
 * @brief Cancellable local dataset preparation and exact-session native layer publication.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensiontimeseriesaccess.h"
#include "plugins/guiextensionhost.h"
#include "plugins/extensionstateregistry.h"
#include "timeseries/timeseriesstore.h"
#include "layers/timeserieslayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "swmmvisprojectwindow.h"
#include <QFileInfo>
#include <QFile>
#include <QPointer>
#include <QSet>
#include <QScopeGuard>
#include <QThread>
#include <atomic>
#include <memory>

namespace openswmmvis
{
namespace
{
using namespace OpenSWMM::Sdk;
/** @brief Own only this operation's unpublished package and cancellation boundary. */
struct PendingPackage {
    QString path; ///< Populated by the worker only after its own successful atomic creation.
    std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
    std::atomic_int publication{0}; ///< 0 preparing, 1 cancelled, 2 publication committed.
    /** @brief Request cancellation until publication commits, including after worker settlement. */
    void cancel()
    {
        int expected = 0;
        if (publication.compare_exchange_strong(expected, 1))
            cancelled->store(true);
    }
    /** @brief Linearize GUI publication against off-thread cancellation. */
    bool commitPublication()
    {
        int expected = 0;
        return publication.compare_exchange_strong(expected, 2);
    }
    /** @brief Remove only this cancelled operation's unexposed native output. */
    void discardCancelled()
    {
        if (publication.load() != 2 && cancelled->load() && !path.isEmpty()) {
            QFile::remove(path);
            QFile::remove(path + "-wal");
            QFile::remove(path + "-shm");
            path.clear();
        }
    }
    /** @brief Also clean when shutdown suppresses the queued completion entirely. */
    ~PendingPackage() { discardCancelled(); }
};
/** @brief Resolve the captured Ready lifetime without creating or activating a model. */
SWMMVisProjectWindow *targetWindow(GuiExtensionHost *host, const TimeSeriesImportRequest &request)
{
    const auto state = host && !host->isStopping() && host->stateRegistry()
                           ? host->stateRegistry()->session(request.sessionId)
                           : ModelSessionState{};
    if (state.status != SessionStatus::Ready || state.sessionId.isNull() ||
        state.loadGeneration != request.loadGeneration || state.saving)
        return nullptr;
    auto *instance = host->resolveSession(request.sessionId);
    auto *window = instance ? qobject_cast<SWMMVisProjectWindow *>(instance->lifetime()) : nullptr;
    return window && !window->isClosing() && window->canvas() && window->modelLayer() ? window
                                                                                      : nullptr;
}
/** @brief Find an already published import in the current attachment stack. */
TimeSeriesLayer *existingImport(SWMMVisProjectWindow *window, const QString &identity)
{
    if (window)
        for (auto *base : window->canvas()->layers())
            if (auto *layer = qobject_cast<TimeSeriesLayer *>(base);
                layer && layer->property("timeSeriesImportIdentity").toString() == identity)
                return layer;
    return nullptr;
}
}
void installExtensionTimeSeriesAccess(GuiExtensionHost *host)
{
    using namespace OpenSWMM::Sdk;
    if (!host)
        return;
    const QPointer<GuiExtensionHost> guard(host);
    auto pending = std::make_shared<QSet<QString>>();
    host->setTimeSeriesControls(
        [guard, pending](const TimeSeriesImportRequest &request, QObject *context,
                         std::function<void(const TimeSeriesImportResult &)> finished,
                         QString *error) {
            auto *window = targetWindow(guard, request);
            if (!window || !QFileInfo(request.descriptorPath).isFile()) {
                if (error)
                    *error = QStringLiteral(
                        "The original project or completed descriptor is unavailable.");
                return static_cast<IJob *>(nullptr);
            }
            const QString identity = guard->pluginId() + ':' + request.importId.toString();
            const QString key = request.sessionId.toString() + ':' + identity;
            if (pending->contains(key)) {
                if (error)
                    *error = QStringLiteral("This dataset import is already running.");
                return static_cast<IJob *>(nullptr);
            }
            auto result = std::make_shared<TimeSeriesImportResult>();
            result->importId = request.importId;
            auto package = std::make_shared<PendingPackage>();
            const auto cancel = package->cancelled;
            auto jobHandle = std::make_shared<IJob *>(nullptr);
            const QPointer<QObject> receiver(context);
            const QPointer<SWMMVisProjectWindow> owner(window);
            const auto cancelledConnection = QObject::connect(context, &QObject::destroyed, guard,
                                                              [package] { package->cancel(); });
            const auto shutdownConnection =
                QObject::connect(guard, &GuiExtensionHost::aboutToShutdown, guard, [package] {
                    package->cancel();
                    package->discardCancelled(); // Workers are joined before this signal.
                });
            const QPointer<TimeSeriesLayer> already(existingImport(window, identity));
            if (already) {
                result->layerId = already->layerId();
                result->packagePath = already->sourcePath();
                if (const auto data = already->dataset()) {
                    result->datasetId = data->id;
                    result->seriesCount = data->series.size();
                }
            }
            pending->insert(key);
            auto *job = guard->runJob(
                QStringLiteral("Add time-series layer"),
                [request, result, cancel, package](IJobContext &job) {
                    if (!result->layerId.isEmpty())
                        return;
                    job.setProgress(0.05, QStringLiteral("Preparing indexed observations…"));
                    const auto cancelled = [&] { return cancel->load() || job.isCancelled(); };
                    if (cancelled()) {
                        result->cancelled = true;
                        return;
                    }
                    if (!OpenSWMM::TimeSeries::TimeSeriesStore::importDescriptor(
                            request.descriptorPath, request.packagePath, &result->error,
                            cancel.get(), cancelled)) {
                        result->cancelled = cancelled();
                        return;
                    }
                    result->packagePath = QFileInfo(request.packagePath).absoluteFilePath();
                    package->path = result->packagePath;
                    const auto data = OpenSWMM::TimeSeries::TimeSeriesStore::open(
                        result->packagePath, &result->error);
                    if (data) {
                        result->datasetId = data->id;
                        result->seriesCount = data->series.size();
                    }
                    result->cancelled = cancelled();
                    job.setProgress(1., QStringLiteral("Observations prepared"));
                },
                [guard, owner, receiver, already, request, identity, key, pending, result, cancel,
                 package, jobHandle, finished = std::move(finished), cancelledConnection,
                 shutdownConnection](bool ok, const QString &jobError) {
                    pending->remove(key);
                    QObject::disconnect(cancelledConnection);
                    QObject::disconnect(shutdownConnection);
                    const auto releaseHook = qScopeGuard([guard, jobHandle] {
                        if (guard)
                            guard->setJobCancellationHook(*jobHandle, {});
                    });
                    result->cancelled = result->cancelled || cancel->load();
                    if (result->cancelled) {
                        package->cancel();
                        package->discardCancelled();
                        result->layerId.clear();
                        if (package->publication.load() != 2 && package->path.isEmpty())
                            result->packagePath.clear();
                    }
                    if (!guard || guard->isStopping() || !receiver ||
                        receiver->thread() != guard->thread())
                        return;
                    if (!ok && result->error.isEmpty())
                        result->error = jobError;
                    if (!result->cancelled && result->error.isEmpty()) {
                        if (!owner || targetWindow(guard, request) != owner) {
                            result->layerId.clear();
                            result->error = QStringLiteral(
                                "Saved, not added: the original project was closed, reloaded or is saving.");
                        } else if (!result->layerId.isEmpty()) {
                            if (!already || existingImport(owner, identity) != already ||
                                !package->commitPublication()) {
                                result->layerId.clear();
                                result->cancelled = cancel->load();
                                result->error = QStringLiteral(
                                    "The previously imported layer is no longer attached or import was cancelled.");
                            }
                        } else if (result->seriesCount == 0) {
                            result->error =
                                QStringLiteral("No observations were available to add.");
                        } else {
                            QPointer<TimeSeriesLayer> layer(
                                new TimeSeriesLayer(owner->modelLayer()->workspace()));
                            if (!layer->openTimeSeries(result->packagePath, result->datasetId,
                                                       &result->error)) {
                                delete layer.data();
                            } else if (!guard || guard->isStopping() || !receiver || !owner ||
                                       targetWindow(guard, request) != owner ||
                                       !package->commitPublication()) {
                                delete layer.data();
                                result->cancelled = cancel->load();
                                result->error = QStringLiteral(
                                    "Saved, not added: the import target changed or import was cancelled.");
                                package->discardCancelled();
                                if (result->cancelled)
                                    result->packagePath.clear();
                            } else {
                                layer->setProperty("timeSeriesImportIdentity", identity);
                                owner->canvas()->addLayer(layer);
                                if (owner && !owner->isClosing())
                                    owner->setHasChanges(true);
                                if (guard && !guard->isStopping() && owner && layer &&
                                    targetWindow(guard, request) == owner &&
                                    owner->canvas()->layers().contains(layer))
                                    result->layerId = layer->layerId();
                                else
                                    result->error = QStringLiteral(
                                        "Saved, not added: the project or layer changed during publication.");
                            }
                        }
                    }
                    if (guard && !guard->isStopping() && receiver &&
                        receiver->thread() == guard->thread())
                        finished(*result);
                });
            *jobHandle = job;
            if (job)
                guard->setJobCancellationHook(job, [package] { package->cancel(); });
            if (!job) {
                pending->remove(key);
                QObject::disconnect(cancelledConnection);
                QObject::disconnect(shutdownConnection);
                if (error)
                    *error = QStringLiteral("The observation import job could not be started.");
            }
            return job;
        },
        [guard](const QUuid &sessionId) {
            QList<TimeSeriesLayerInfo> records;
            auto *instance = guard ? guard->resolveSession(sessionId) : nullptr;
            auto *window =
                instance ? qobject_cast<SWMMVisProjectWindow *>(instance->lifetime()) : nullptr;
            if (window && !window->isClosing())
                for (auto *base : window->canvas()->layers())
                    if (auto *layer = qobject_cast<TimeSeriesLayer *>(base)) {
                        const auto data = layer->dataset();
                        records.append({layer->layerId(), data ? data->id : QString{},
                                        layer->name(), layer->sourcePath(),
                                        data ? int(data->series.size()) : 0});
                        if (records.size() == 1000)
                            break;
                    }
            return records;
        });
}
}
