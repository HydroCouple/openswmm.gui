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
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QUuid>
#include <QtConcurrent>
#include <openssl/evp.h>
#include <stdexcept>
#include <thread>

namespace openswmmvis::trace
{
Q_LOGGING_CATEGORY(lcTracePerf, "openswmm.trace.perf")
namespace
{
QString fingerprint(const QString &path, std::atomic_bool &cancel)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error("Cannot read source output");
    // Same SHA-256 digest as QCryptographicHash, but OpenSSL uses the CPU's
    // SHA extensions. This Qt build hashes in plain C at ~100 MB/s, which made
    // a 0.5 GB output cost 4.5 s per pass.
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("Cannot initialize the output fingerprint");
    QByteArray buffer(4 * 1024 * 1024, Qt::Uninitialized);
    while (!f.atEnd())
    {
        if (cancel.load())
            throw std::runtime_error("Analysis cancelled");
        const qint64 n = f.read(buffer.data(), buffer.size());
        if (n < 0 || (n == 0 && f.error() != QFile::NoError) ||
            (n > 0 && EVP_DigestUpdate(ctx.get(), buffer.constData(), size_t(n)) != 1))
            throw std::runtime_error("Cannot fingerprint source output");
    }
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(ctx.get(), digest, &length) != 1)
        throw std::runtime_error("Cannot fingerprint source output");
    return QString::fromLatin1(
        QByteArray(reinterpret_cast<const char *>(digest), int(length)).toHex());
}
void require(bool ok, const QString &message)
{
    if (!ok)
        throw std::runtime_error(message.toStdString());
}
void perf(const QString &run, const QString &stage, qint64 ms, const QString &detail = {})
{
    qCInfo(lcTracePerf).noquote()
        << QStringLiteral("trace.perf run=%1 stage=%2 ms=%3 %4").arg(run, stage).arg(ms).arg(detail);
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
                QElapsedTimer gui;
                gui.start();
                if (reply.dataset)
                {
                    m_project->statsRegistry()->attachAnalysis(
                        reply.runId, reply.dataset->fingerprint, reply.dataset->packagePath,
                        reply.dataset->snapshot.toJson());
                    m_project->setHasChanges(true);
                    emit prepared(reply.dataset);
                    perf(reply.runId, QStringLiteral("attachAnalysis"), gui.restart());
                }
                for (auto &r : reply.results)
                {
                    emit resultReady(r);
                    perf(reply.runId, QStringLiteral("attachResult"), gui.restart(),
                         QStringLiteral("seed=%1").arg(r->seed));
                }
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
            QElapsedTimer total, step;
            total.start();
            step.start();
            auto mark = [&](const QString &stage, const QString &detail = {})
            { perf(source.id, stage, step.restart(), detail); };
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
                    mark(QStringLiteral("readDataset"));
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
                    require(QDir().mkpath(QFileInfo(path).absolutePath()),
                            tr("Cannot create the analysis folder."));
                    handle.reset(createHandle(d->snapshot, d->options, &error));
                    require(bool(handle), error);
                    mark(QStringLiteral("createHandle"));
                    // The digest and the engine scan each read the whole
                    // output. Run them together so the wall time is the longer
                    // of the two, not their sum. No engine cache is written:
                    // the GeoPackage keeps the averages, and a cache key would
                    // need the digest before the scan could start. The engine
                    // still rejects an output whose size or mtime changed
                    // during its scan.
                    QString digest, digestError;
                    std::thread hasher(
                        [&]
                        {
                            try
                            {
                                digest = fingerprint(scanPath, *cancelFlag);
                            }
                            catch (const std::exception &e)
                            {
                                digestError = QString::fromUtf8(e.what());
                            }
                        });
                    int code = swmm_trace_prepare(handle.get(), scanPath.toUtf8().constData(),
                                                  nullptr, nullptr, cb, &p);
                    hasher.join();
                    mark(QStringLiteral("prepare+fingerprint"),
                         QStringLiteral("bytes=%1").arg(QFileInfo(scanPath).size()));
                    require(code == 0, QString::fromUtf8(swmm_trace_error(handle.get())));
                    require(digestError.isEmpty(), digestError);
                    d->fingerprint = digest;
                    if (!source.fingerprint.isEmpty())
                        require(source.fingerprint == d->fingerprint,
                                tr("The output file has changed. Reload it as a new result "
                                   "version."));
                    d->nodes.resize(d->snapshot.nodes.size());
                    d->links.resize(d->snapshot.links.size());
                    swmm_trace_get_info(handle.get(), &d->info);
                    require(swmm_trace_get_averages(handle.get(), d->nodes.data(), d->nodes.size(),
                                                    d->links.data(), d->links.size()) == 0,
                            tr("Cannot retrieve hydraulic averages."));
                    mark(QStringLiteral("getAverages"));
                    require(AnalysisStore::prepare(path, *d, &error, cancelFlag.get()), error);
                    mark(QStringLiteral("gpkgPrepare"));
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
                    mark(QStringLiteral("setAverages"));
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
                mark(QStringLiteral("analyses"));
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
                            auto cached = AnalysisStore::read(path, cachedId, d, &error);
                            require(bool(cached), error);
                            mark(QStringLiteral("cachedRead"), QStringLiteral("seed=%1").arg(seed));
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
                    mark(QStringLiteral("estimate"), QStringLiteral("seed=%1").arg(seed));
                    // Retain computed work for a save retry even if the writer fails.
                    reply.results.append(r);
                    require(AnalysisStore::save(path, *r, &error, cancelFlag.get()), error);
                    r->saved = true;
                    mark(QStringLiteral("save"), QStringLiteral("seed=%1").arg(seed));
                }
            }
            catch (const std::exception &e)
            {
                reply.error = QString::fromUtf8(e.what());
            }
            reply.cancelled = cancelFlag->load();
            perf(source.id, QStringLiteral("total"), total.elapsed(),
                 reply.dataset ? QStringLiteral("nodes=%1 links=%2 periods=%3 results=%4")
                                     .arg(reply.dataset->nodes.size())
                                     .arg(reply.dataset->links.size())
                                     .arg(reply.dataset->info.periods)
                                     .arg(reply.results.size())
                               : QStringLiteral("error=%1").arg(reply.error));
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
