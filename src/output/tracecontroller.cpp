#include <QSet>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "layers/traceanalysislayer.h"
#include "map/mapcanvas.h"
#include "map/openswmmvisscene.h"
#include "map/spatialreferencesystem.h"
#include "output/outputstatsregistry.h"
#include "output/traceanalysisstore.h"
#include "output/tracecontroller.h"
#include "swmmvisprojectwindow.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>
#include <QtConcurrent>
#include <stdexcept>

namespace openswmmvis::trace
{
namespace
{
QString fingerprint(const QString &path, std::atomic_bool &cancel)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error("Cannot read source output");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!f.atEnd())
    {
        if (cancel.load())
            throw std::runtime_error("Analysis cancelled");
        auto bytes = f.read(4 * 1024 * 1024);
        if (bytes.isEmpty() && f.error() != QFile::NoError)
            throw std::runtime_error("Cannot fingerprint source output");
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}
void require(bool ok, const QString &message)
{
    if (!ok)
        throw std::runtime_error(message.toStdString());
}
} // namespace
TraceController::TraceController(SWMMVisProjectWindow *p) : QObject(p), m_project(p)
{
    setObjectName(QStringLiteral("flowTraceController"));
    connect(p->statsRegistry(), &OutputStatsRegistry::runsChanged, this,
            &TraceController::reconcileOutputRuns);
    connect(&m_watcher, &QFutureWatcher<JobReply>::finished, this,
            [this]
            {
                auto reply = m_watcher.result();
                emit busyChanged(false);
                if (!m_project)
                    return;
                if (reply.dataset)
                {
                    m_project->statsRegistry()->attachAnalysis(
                        reply.runId, reply.dataset->fingerprint, reply.dataset->packagePath,
                        reply.dataset->snapshot.toJson());
                    m_project->setHasChanges(true);
                    emit prepared(reply.dataset);
                }
                for (auto &r : reply.results)
                    emit resultReady(r);
                if (!reply.error.isEmpty() && !reply.cancelled)
                    emit failed(reply.error);
                emit finished(reply.runId, reply.cancelled);
            });
}
TraceController::~TraceController()
{
    cancel();
    m_watcher.waitForFinished();
}
TraceController *TraceController::forProject(SWMMVisProjectWindow *p, bool create)
{
    if (!p)
        return nullptr;
    auto *c = p->findChild<TraceController *>(QStringLiteral("flowTraceController"),
                                              Qt::FindDirectChildrenOnly);
    return c || !create ? c : new TraceController(p);
}
SWMMResultsLayer *TraceController::outputForRun(const QString &id)
{
    if (!m_project)
        return nullptr;
    auto *registry = m_project->statsRegistry();
    for (const auto &identity : registry->identities())
        if (identity.runId == id && identity.layer)
            return identity.layer;
    auto run = registry->run(id);
    if (run.id.isEmpty())
        return nullptr;
    auto *output = new SWMMResultsLayer(run.path, m_project->modelLayer(),
                                        m_project->modelLayer()->workspace());
    output->setProperty("traceStoredRunId", id);
    output->setName(tr("%1 · Run %2 · Saved analysis").arg(run.label).arg(run.number));
    m_project->canvas()->addLayer(output, false);
    return output;
}
TraceSublayer *TraceController::attachResult(std::shared_ptr<Result> result, bool travel)
{
    auto *output = outputForRun(result->dataset->runId);
    if (!output)
        return nullptr;
    for (auto *sub : output->sublayers())
        if (auto *trace = dynamic_cast<TraceSublayer *>(sub))
            if (trace->layer()->result()->id == result->id && trace->travel() == travel)
            {
                trace->setVisible(true);
                output->setVisible(true);
                return trace;
            }
    auto *trace = new TraceSublayer(output, result, travel);
    if (!output->outputHandle())
    {
        output->setExtent(trace->layer()->extent());
        if (!result->dataset->snapshot.wkt.isEmpty())
            output->setSRS(SpatialReferenceSystem::fromWktOrProj(result->dataset->snapshot.wkt),
                           true);
    }
    output->addAnalysisSublayer(trace);
    connect(trace, &OpenSWMM::Render::ISublayer::invalidated, m_project,
            [p = m_project]
            {
                if (p)
                    p->setHasChanges(true);
            });
    m_project->setHasChanges(true);
    return trace;
}
void TraceController::reconcileOutputRuns()
{
    if (!m_project || m_rebinding)
        return;
    m_rebinding = true;
    const auto identities = m_project->statsRegistry()->identities();
    for (const auto &identity : identities)
    {
        auto *output = identity.layer;
        if (!output)
            continue;
        const auto subs = output->sublayers();
        for (auto *sub : subs)
        {
            auto *trace = dynamic_cast<TraceSublayer *>(sub);
            if (!trace || trace->layer()->result()->dataset->runId == identity.runId)
                continue;
            auto *saved = outputForRun(trace->layer()->result()->dataset->runId);
            if (!saved || saved == output)
                continue;
            trace->layer()->depopulateScene(m_project->canvas()->mapScene());
            output->takeAnalysisSublayer(trace);
            saved->addAnalysisSublayer(trace);
        }
    }
    m_rebinding = false;
}
void TraceController::cancel()
{
    if (m_cancel)
        m_cancel->store(true);
}
QString TraceController::defaultPackagePath(const QString &id) const
{
    if (!m_project)
        return {};
    auto r = m_project->statsRegistry()->run(id);
    if (!r.packagePath.isEmpty())
        return r.packagePath;
    QString model = m_project->modelLayer() ? m_project->modelLayer()->modelFilePath() : QString();
    QString base = QFileInfo(model.isEmpty() ? r.path : model).absolutePath();
    return QDir(base).filePath(QStringLiteral("analysis/%1/%2.analysis.gpkg")
                                   .arg(r.id, QFileInfo(r.path).completeBaseName()));
}
void TraceController::run(const QString &id, const QStringList &requested, int direction,
                          const QString &destination, const Snapshot &snapshot,
                          const QString &replaceId)
{
    if (busy() || !m_project)
    {
        emit failed(tr("An analysis is already running."));
        return;
    }
    auto source = m_project->statsRegistry()->run(id);
    if (source.id.isEmpty() || !source.canAnalyze())
    {
        emit failed(tr("Select a completed output or a valid imported result."));
        return;
    }
    const QString path = destination.isEmpty() ? defaultPackagePath(id) : destination;
    if (!source.packagePath.isEmpty() &&
        QFileInfo(path).absoluteFilePath() != QFileInfo(source.packagePath).absoluteFilePath())
    {
        emit failed(tr("This run already has an analysis package. Use its existing destination to "
                       "keep its saved estimates together."));
        return;
    }
    const bool prepared = QFile::exists(path);
    const bool historical = m_project->statsRegistry()->latestRun(source.path).id != source.id;
    const QString scanPath = !source.retainedPath.isEmpty() ? source.retainedPath : source.path;
    if (!prepared && historical && source.retainedPath.isEmpty())
    {
        emit failed(tr("This historical run has no saved averages or retained output. The current "
                       "file belongs to a newer run."));
        return;
    }
    if (!prepared && source.retainedPath.isEmpty() && source.size >= 0)
    {
        QFileInfo file(source.path);
        if (file.size() != source.size ||
            file.lastModified().toMSecsSinceEpoch() != source.modified)
        {
            emit failed(tr("The output changed since this run was recorded. Reload the replacement "
                           "as a new result."));
            return;
        }
    }
    const QStringList seeds = requested;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    auto cancelFlag = m_cancel;
    m_activePath = source.path;
    QPointer<TraceController> guard(this);
    emit busyChanged(true);
    m_watcher.setFuture(QtConcurrent::run(
        [source, path, scanPath, seeds, direction, snapshot, replaceId, cancelFlag, guard]
        {
            JobReply reply;
            reply.runId = source.id;
            struct Progress
            {
                std::shared_ptr<std::atomic_bool> cancel;
                QPointer<TraceController> controller;
                int last = -1;
            };
            Progress p{cancelFlag, guard, -1};
            auto cb = [](double fraction, const char *stage, void *ptr) -> int
            {
                auto &p = *static_cast<Progress *>(ptr);
                int percent = int(fraction * 100);
                if (percent != p.last)
                {
                    p.last = percent;
                    QString text = QString::fromUtf8(stage);
                    if (p.controller)
                        QMetaObject::invokeMethod(
                            p.controller,
                            [g = p.controller, percent, text]
                            {
                                if (g)
                                    emit g->progress(percent, text);
                            },
                            Qt::QueuedConnection);
                }
                return p.cancel->load() ? 1 : 0;
            };
            std::unique_ptr<void, decltype(&swmm_trace_close)> handle(nullptr, swmm_trace_close);
            try
            {
                QString error;
                std::shared_ptr<Dataset> d;
                if (QFile::exists(path))
                {
                    d = AnalysisStore::readDataset(path, &error);
                    require(bool(d), error);
                    require(d->runId == source.id, tr("This package belongs to a different "
                                                      "run. Choose another destination."));
                    if (!source.fingerprint.isEmpty())
                        require(d->fingerprint == source.fingerprint,
                                tr("Saved package fingerprint differs from the selected run."));
                }
                else
                {
                    d = std::make_shared<Dataset>();
                    d->snapshot =
                        source.snapshot.isEmpty() ? snapshot : Snapshot::fromJson(source.snapshot);
                    require(d->snapshot.valid(&error), error);
                    d->sourceId = source.sourceId;
                    d->runId = source.id;
                    d->label = QStringLiteral("%1 · Run %2").arg(source.label).arg(source.number);
                    d->outputPath = source.path;
                    d->packagePath = path;
                    if (source.native)
                        d->provenance =
                            QStringLiteral("Captured model snapshot for this simulation execution");
                    d->fingerprint = fingerprint(scanPath, *cancelFlag);
                    if (!source.fingerprint.isEmpty())
                        require(source.fingerprint == d->fingerprint,
                                tr("The output file has changed. Reload it as a new result "
                                   "version."));
                    require(QDir().mkpath(QFileInfo(path).absolutePath()),
                            tr("Cannot create the analysis folder."));
                    handle.reset(createHandle(d->snapshot, d->options, &error));
                    require(bool(handle), error);
                    QString cache =
                        QFileInfo(path).absolutePath() + QStringLiteral("/hydraulics.trace.nc");
                    int code = swmm_trace_prepare(handle.get(), scanPath.toUtf8().constData(),
                                                  cache.toUtf8().constData(),
                                                  d->fingerprint.toUtf8().constData(), cb, &p);
                    require(code == 0, QString::fromUtf8(swmm_trace_error(handle.get())));
                    require(fingerprint(scanPath, *cancelFlag) == d->fingerprint,
                            tr("Source output changed during analysis preparation."));
                    d->nodes.resize(d->snapshot.nodes.size());
                    d->links.resize(d->snapshot.links.size());
                    swmm_trace_get_info(handle.get(), &d->info);
                    require(swmm_trace_get_averages(handle.get(), d->nodes.data(), d->nodes.size(),
                                                    d->links.data(), d->links.size()) == 0,
                            tr("Cannot retrieve hydraulic averages."));
                    require(AnalysisStore::prepare(path, *d, &error, cancelFlag.get()), error);
                }
                reply.dataset = d;
                if (!handle)
                {
                    handle.reset(createHandle(d->snapshot, d->options, &error));
                    require(bool(handle), error);
                    require(swmm_trace_set_averages(handle.get(), d->nodes.data(), d->nodes.size(),
                                                    d->links.data(), d->links.size(),
                                                    &d->info) == 0,
                            tr("Saved averages are invalid."));
                }
                QVector<int> indices;
                QStringList missing;
                QSet<QString> seen;
                for (const auto &s : seeds)
                {
                    if (seen.contains(s))
                        continue;
                    seen.insert(s);
                    int found = -1;
                    for (int i = 0; i < d->snapshot.nodes.size(); ++i)
                        if (d->snapshot.nodes[i].id == s)
                        {
                            found = i;
                            break;
                        }
                    if (found < 0)
                        missing.append(s);
                    else
                        indices.append(found);
                }
                require(missing.isEmpty(), tr("These nodes are absent from this run: %1")
                                               .arg(missing.join(QStringLiteral(", "))));
                require(replaceId.isEmpty() || indices.size() == 1,
                        tr("Update one saved estimate at a time."));
                // A repeat tool click reopens its saved estimate. Preparing a
                // different node only solves from averages; the .out is not rescanned.
                auto saved = AnalysisStore::analyses(path, &error);
                require(error.isEmpty(), error);
                for (int seed : indices)
                {
                    if (cancelFlag->load())
                        break;
                    if (replaceId.isEmpty())
                    {
                        QString cachedId;
                        for (const auto &row : saved)
                            if (row.value("seed").toInt(-1) == seed &&
                                row.value("direction").toInt(-1) == direction)
                                cachedId = row.value("id").toString();
                        if (!cachedId.isEmpty())
                        {
                            auto cached = AnalysisStore::read(path, cachedId, &error);
                            require(bool(cached), error);
                            reply.results.append(cached);
                            continue;
                        }
                    }
                    auto r = std::make_shared<Result>();
                    r->dataset = d;
                    r->id = replaceId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                                                : replaceId;
                    if (!replaceId.isEmpty())
                    {
                        auto previous = AnalysisStore::read(path, replaceId, &error);
                        require(bool(previous), error);
                        r->style = previous->style;
                    }
                    r->seed = seed;
                    r->direction = direction;
                    r->nodes.resize(d->nodes.size());
                    r->links.resize(d->links.size());
                    int code = swmm_trace_estimate(handle.get(), direction, seed, r->nodes.data(),
                                                   r->nodes.size(), r->links.data(),
                                                   r->links.size(), &r->summary, cb, &p);
                    require(code == 0, QString::fromUtf8(swmm_trace_error(handle.get())));
                    // Retain computed work for a save retry even if the writer fails.
                    reply.results.append(r);
                    require(AnalysisStore::save(path, *r, &error, cancelFlag.get()), error);
                    r->saved = true;
                }
            }
            catch (const std::exception &e)
            {
                reply.error = QString::fromUtf8(e.what());
            }
            reply.cancelled = cancelFlag->load();
            return reply;
        }));
}
bool TraceController::beforeOverwrite(const QString &path, QString *error)
{
    if (m_activePath == path && busy())
    {
        cancel();
        m_watcher.waitForFinished();
        // The queued completion callback may not have run while waiting here.
        const auto reply = m_watcher.result();
        if (m_project && reply.dataset)
            m_project->statsRegistry()->attachAnalysis(reply.runId, reply.dataset->fingerprint,
                                                       reply.dataset->packagePath,
                                                       reply.dataset->snapshot.toJson());
    }
    if (!m_project)
        return true;
    auto *registry = m_project->statsRegistry();
    auto old = registry->latestRun(path);
    if (old.id.isEmpty() || old.packagePath.isEmpty() || !QFile::exists(path))
        return true;
    QVariant preference = m_project->property("traceKeepPreviousRaw");
    if (preference.isValid() && !preference.toBool())
        return true;
    if (!old.fingerprint.isEmpty())
    {
        try
        {
            std::atomic_bool keepGoing{false};
            if (fingerprint(path, keepGoing) != old.fingerprint)
            {
                if (error)
                    *error = tr("The previous output has been replaced externally. Its saved "
                                "analysis is preserved, but this file cannot be retained as that "
                                "run. Select a different output path.");
                return false;
            }
        }
        catch (const std::exception &e)
        {
            if (error)
                *error = QString::fromUtf8(e.what());
            return false;
        }
    }
    QString directory = QFileInfo(old.packagePath).absolutePath();
    if (!QDir().mkpath(directory))
    {
        if (error)
            *error = tr("Cannot create the previous-run folder.");
        return false;
    }
    QString retained = QDir(directory).filePath(QStringLiteral("retained-%1.out").arg(old.id));
    if (QFile::exists(retained))
    {
        try
        {
            std::atomic_bool keepGoing{false};
            if (fingerprint(retained, keepGoing) != fingerprint(path, keepGoing))
            {
                if (error)
                    *error = tr("The retained-output destination contains different data.");
                return false;
            }
        }
        catch (const std::exception &e)
        {
            if (error)
                *error = QString::fromUtf8(e.what());
            return false;
        }
    }
    if (!QFile::exists(retained) && !QFile::copy(path, retained))
    {
        if (error)
            *error = tr("Cannot retain the previous output. Choose another output "
                        "path or disable Keep previous raw results.");
        return false;
    }
    QString rpt = old.reportPath.isEmpty() ? QFileInfo(path).absolutePath() + "/" +
                                                 QFileInfo(path).completeBaseName() + ".rpt"
                                           : old.reportPath;
    QString rptCopy = retained + ".rpt";
    if (QFile::exists(rpt) && !QFile::exists(rptCopy) && !QFile::copy(rpt, rptCopy))
    {
        if (error)
            *error = tr("Cannot retain the previous report.");
        return false;
    }
    QSaveFile snapshotFile(retained + QStringLiteral(".model.json"));
    auto bytes = QJsonDocument(old.snapshot).toJson();
    if (!snapshotFile.open(QIODevice::WriteOnly) || snapshotFile.write(bytes) != bytes.size() ||
        !snapshotFile.commit())
    {
        if (error)
            *error = tr("Cannot retain the matching model snapshot.");
        return false;
    }
    registry->setRetainedPath(old.id, retained);
    return true;
}
} // namespace openswmmvis::trace
