/**
 * @file      tracecontroller.cpp
 * @brief     Trace Controller implementation.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

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
/**
 * @brief Stream a result file into a SHA-256 hexadecimal fingerprint on a worker.
 * @param[in] path Read-only result path.
 * @param[in] cancel Shared cancellation flag checked between four-MiB reads.
 * @return Lowercase digest text.
 * @throws std::runtime_error For cancellation, open/read, or OpenSSL digest failures.
 */
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
    while (!f.atEnd()) {
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
/**
 * @brief Translate a failed controller/worker invariant to a caught standard exception.
 * @param[in] ok Required condition.
 * @param[in] message Failure text.
 * @throws std::runtime_error When ok is false.
 */
void require(bool ok, const QString &message)
{
    if (!ok)
        throw std::runtime_error(message.toStdString());
}
/**
 * @brief Record one analysis-stage timing in the trace performance category.
 * @param[in] run Correlation run ID.
 * @param[in] stage Stage label.
 * @param[in] ms Elapsed wall-clock milliseconds.
 * @param[in] detail Optional extra diagnostic fields.
 */
void perf(const QString &run, const QString &stage, qint64 ms, const QString &detail = {})
{
    qCInfo(lcTracePerf).noquote() << QStringLiteral("trace.perf run=%1 stage=%2 ms=%3 %4")
                                         .arg(run, stage)
                                         .arg(ms)
                                         .arg(detail);
}
} // namespace
TraceController::TraceController(SWMMVisProjectWindow *p) : QObject(p), m_project(p)
{
    setObjectName(QStringLiteral("flowTraceController"));
    connect(p->statsRegistry(), &OutputStatsRegistry::runsChanged, this,
            &TraceController::reconcileOutputRuns);
    connect(&m_watcher, &QFutureWatcher<JobReply>::finished, this, [this] {
        auto reply = m_watcher.result();
        emit busyChanged(false);
        if (!m_project)
            return;
        QElapsedTimer gui;
        gui.start();
        if (reply.dataset) {
            // A partial package records no fingerprint: the completed
            // output must not be rejected as "changed" later.
            m_project->statsRegistry()->attachAnalysis(
                reply.runId, reply.dataset->partial ? QString() : reply.dataset->fingerprint,
                reply.dataset->packagePath, reply.dataset->snapshot.toJson());
            m_project->setHasChanges(true);
            emit prepared(reply.dataset);
            perf(reply.runId, QStringLiteral("attachAnalysis"), gui.restart());
        }
        for (auto &r : reply.results) {
            refreshOpenLayers(r);
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
            if (trace->layer()->result()->id == result->id && trace->travel() == travel) {
                trace->setVisible(true);
                output->setVisible(true);
                return trace;
            }
    auto *trace = new TraceSublayer(output, result, travel);
    if (!output->outputHandle()) {
        output->setExtent(trace->layer()->extent());
        if (!result->dataset->snapshot.wkt.isEmpty())
            output->setSRS(SpatialReferenceSystem::fromWktOrProj(result->dataset->snapshot.wkt),
                           true);
    }
    output->addAnalysisSublayer(trace);
    connect(trace, &OpenSWMM::Render::ISublayer::invalidated, m_project, [p = m_project] {
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
    for (const auto &identity : identities) {
        auto *output = identity.layer;
        if (!output)
            continue;
        const auto subs = output->sublayers();
        for (auto *sub : subs) {
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
    QVector<Request> requests;
    QSet<QString> seen;
    for (const auto &s : requested)
        if (!seen.contains(s)) {
            seen.insert(s);
            requests.append({s, direction, replaceId, {}});
        }
    if (!replaceId.isEmpty() && requests.size() != 1) {
        emit failed(tr("Update one saved estimate at a time."));
        return;
    }
    startJob(id, requests, destination, snapshot, false);
}
void TraceController::recalculate(const QString &id, const QStringList &extraNodes,
                                  int extraDirection, const Snapshot &snapshot)
{
    QVector<Request> requests;
    QSet<QString> seen;
    for (const auto &s : extraNodes)
        if (!seen.contains(s)) {
            seen.insert(s);
            requests.append({s, extraDirection, {}, {}});
        }
    startJob(id, requests, {}, snapshot, true);
}
namespace
{
/**
 * @brief Remove an existing package and its SQLite journal companions before rebuilding.
 * @param[in] path Package base path.
 * @return False on the first failed existing-file removal; true otherwise. Earlier deletions are not restored on failure.
 */
bool removePackage(const QString &path)
{
    for (const QString &suffix :
         {QString(), QStringLiteral("-wal"), QStringLiteral("-shm"), QStringLiteral("-journal")})
        if (QFile::exists(path + suffix) && !QFile::remove(path + suffix))
            return false;
    return true;
}
} // namespace
void TraceController::startJob(const QString &id, QVector<Request> requests,
                               const QString &destination, const Snapshot &snapshot, bool discard)
{
    if (busy() || !m_project) {
        emit failed(tr("An analysis is already running."));
        return;
    }
    auto *registry = m_project->statsRegistry();
    auto source = registry->run(id);
    if (source.id.isEmpty() || !source.canAnalyze()) {
        emit failed(tr("Select a completed, running or cancelled output, or a valid imported "
                       "result."));
        return;
    }
    const QString path = destination.isEmpty() ? defaultPackagePath(id) : destination;
    if (!source.packagePath.isEmpty() &&
        QFileInfo(path).absoluteFilePath() != QFileInfo(source.packagePath).absoluteFilePath()) {
        emit failed(tr("This run already has an analysis package. Use its existing destination to "
                       "keep its saved estimates together."));
        return;
    }
    const bool historical = registry->latestRun(source.path).id != source.id;
    const QString scanPath = !source.retainedPath.isEmpty() ? source.retainedPath : source.path;
    // A package built from partial results is never reused: rebuild it, and
    // recompute its saved estimates so open layers follow the newer results.
    discard =
        discard || (QFile::exists(path) && (source.partial() || AnalysisStore::isPartial(path)));
    if (discard) {
        if (historical && source.retainedPath.isEmpty()) {
            emit failed(tr("This historical run has no retained output to recalculate from. The "
                           "current file belongs to a newer run."));
            return;
        }
        QString error;
        if (QFile::exists(path)) {
            auto data = AnalysisStore::readDataset(path, &error);
            const auto rows = AnalysisStore::analyses(path, &error);
            QVector<Request> saved;
            for (const auto &row : rows) {
                const int seed = row.value("seed").toInt(-1);
                const QString seedId = row.value("seedId").toString(
                    data && seed >= 0 && seed < data->snapshot.nodes.size()
                        ? data->snapshot.nodes[seed].id
                        : QString());
                if (seedId.isEmpty())
                    continue;
                const int direction = row.value("direction").toInt();
                saved.append(
                    {seedId, direction, row.value("id").toString(), row.value("style").toObject()});
            }
            QVector<Request> fresh;
            for (const auto &r : requests) {
                bool duplicate = false;
                for (const auto &s : saved)
                    duplicate |= s.seedId == r.seedId && s.direction == r.direction;
                if (!duplicate)
                    fresh.append(r);
            }
            requests = saved + fresh;
            if (!removePackage(path)) {
                emit failed(tr("Cannot remove the saved analysis package %1.").arg(path));
                return;
            }
        }
        // The new results may legitimately differ from what was recorded: accept
        // the current output as this run's content.
        registry->attachAnalysis(id, {}, {}, source.snapshot);
        registry->recordOutputFile(id);
        source = registry->run(id);
    }
    if (requests.isEmpty()) {
        emit failed(tr("Choose at least one node to analyze."));
        return;
    }
    const bool prepared = QFile::exists(path);
    if (!prepared && historical && source.retainedPath.isEmpty()) {
        emit failed(tr("This historical run has no saved averages or retained output. The current "
                       "file belongs to a newer run."));
        return;
    }
    if (!prepared && !discard && !source.partial() && source.retainedPath.isEmpty() &&
        source.size >= 0) {
        QFileInfo file(source.path);
        if (file.size() != source.size ||
            file.lastModified().toMSecsSinceEpoch() != source.modified) {
            emit failed(tr("The output changed since this run was recorded. Use Recalculate to "
                           "replace the saved analysis with the current results."));
            return;
        }
    }
    const bool partial = source.partial();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    auto cancelFlag = m_cancel;
    m_activePath = source.path;
    QPointer<TraceController> guard(this);
    emit busyChanged(true);
    m_watcher.setFuture(QtConcurrent::run([source, path, scanPath, requests, snapshot, partial,
                                           discard, cancelFlag, guard] {
        JobReply reply;
        reply.runId = source.id;
        QElapsedTimer total, step;
        total.start();
        step.start();
        auto mark = [&](const QString &stage, const QString &detail = {}) {
            perf(source.id, stage, step.restart(), detail);
        };
        struct Progress {
            std::shared_ptr<std::atomic_bool> cancel;
            QPointer<TraceController> controller;
            int last = -1;
        };
        Progress p{cancelFlag, guard, -1};
        auto cb = [](double fraction, const char *stage, void *ptr) -> int {
            auto &p = *static_cast<Progress *>(ptr);
            int percent = int(fraction * 100);
            if (percent != p.last) {
                p.last = percent;
                QString text = QString::fromUtf8(stage);
                if (p.controller)
                    QMetaObject::invokeMethod(
                        p.controller,
                        [g = p.controller, percent, text] {
                            if (g)
                                emit g->progress(percent, text);
                        },
                        Qt::QueuedConnection);
            }
            return p.cancel->load() ? 1 : 0;
        };
        std::unique_ptr<void, decltype(&swmm_trace_close)> handle(nullptr, swmm_trace_close);
        try {
            QString error;
            std::shared_ptr<Dataset> d;
            if (QFile::exists(path)) {
                d = AnalysisStore::readDataset(path, &error);
                require(bool(d), error);
                mark(QStringLiteral("readDataset"));
                require(d->runId == source.id, tr("This package belongs to a different "
                                                  "run. Choose another destination."));
                if (!source.fingerprint.isEmpty())
                    require(d->fingerprint == source.fingerprint,
                            tr("Saved package fingerprint differs from the selected run. Use "
                               "Recalculate to rebuild it from the current results."));
            } else {
                d = std::make_shared<Dataset>();
                d->snapshot =
                    source.snapshot.isEmpty() ? snapshot : Snapshot::fromJson(source.snapshot);
                require(d->snapshot.valid(&error), error);
                d->sourceId = source.sourceId;
                d->runId = source.id;
                d->label = QStringLiteral("%1 · Run %2").arg(source.label).arg(source.number);
                d->outputPath = source.path;
                d->packagePath = path;
                d->partial = partial;
                if (source.native)
                    d->provenance =
                        QStringLiteral("Captured model snapshot for this simulation execution");
                require(QDir().mkpath(QFileInfo(path).absolutePath()),
                        tr("Cannot create the analysis folder."));
                handle.reset(createHandle(d->snapshot, d->options, &error));
                require(bool(handle), error);
                mark(QStringLiteral("createHandle"));
                if (partial) {
                    // A running or cancelled output: scan the whole periods
                    // written so far. Its content may still grow, so it has no
                    // stable digest; the package is rebuilt on the next request.
                    const qint64 bytes = QFileInfo(scanPath).size();
                    int code = swmm_trace_prepare_live(handle.get(), scanPath.toUtf8().constData(),
                                                       cb, &p);
                    mark(QStringLiteral("prepareLive"), QStringLiteral("bytes=%1").arg(bytes));
                    require(code == 0, QString::fromUtf8(swmm_trace_error(handle.get())));
                    swmm_trace_get_info(handle.get(), &d->info);
                    d->fingerprint =
                        QStringLiteral("partial:%1:%2").arg(d->info.periods).arg(bytes);
                    d->label += tr(" (partial, %n reports)", nullptr, d->info.periods);
                } else {
                    // The digest and the engine scan each read the whole
                    // output. Run them together so the wall time is the
                    // longer of the two, not their sum. No engine cache is
                    // written: the GeoPackage keeps the averages, and a cache
                    // key would need the digest before the scan could start.
                    // The engine still rejects an output whose size or mtime
                    // changed during its scan.
                    QString digest, digestError;
                    std::thread hasher([&] {
                        try {
                            digest = fingerprint(scanPath, *cancelFlag);
                        } catch (const std::exception &e) {
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
                                tr("The output file has changed. Use Recalculate to replace "
                                   "the saved analysis with the current results."));
                    swmm_trace_get_info(handle.get(), &d->info);
                }
                d->nodes.resize(d->snapshot.nodes.size());
                d->links.resize(d->snapshot.links.size());
                require(swmm_trace_get_averages(handle.get(), d->nodes.data(), d->nodes.size(),
                                                d->links.data(), d->links.size()) == 0,
                        tr("Cannot retrieve hydraulic averages."));
                mark(QStringLiteral("getAverages"));
                require(AnalysisStore::prepare(path, *d, &error, cancelFlag.get()), error);
                mark(QStringLiteral("gpkgPrepare"));
            }
            reply.dataset = d;
            if (!handle) {
                handle.reset(createHandle(d->snapshot, d->options, &error));
                require(bool(handle), error);
                require(swmm_trace_set_averages(handle.get(), d->nodes.data(), d->nodes.size(),
                                                d->links.data(), d->links.size(), &d->info) == 0,
                        tr("Saved averages are invalid."));
                mark(QStringLiteral("setAverages"));
            }
            QHash<QString, int> nodeIndex;
            for (int i = 0; i < d->snapshot.nodes.size(); ++i)
                nodeIndex.insert(d->snapshot.nodes[i].id, i);
            QStringList missing;
            for (const auto &request : requests)
                if (!nodeIndex.contains(request.seedId))
                    missing.append(request.seedId);
            require(missing.isEmpty(), tr("These nodes are absent from this run: %1")
                                           .arg(missing.join(QStringLiteral(", "))));
            // A repeat tool click reopens its saved estimate. Preparing a
            // different node only solves from averages; the .out is not rescanned.
            auto saved = AnalysisStore::analyses(path, &error);
            require(error.isEmpty(), error);
            mark(QStringLiteral("analyses"));
            for (const auto &request : requests) {
                if (cancelFlag->load())
                    break;
                const int seed = nodeIndex.value(request.seedId);
                const int direction = request.direction;
                if (request.replaceId.isEmpty() && !discard) {
                    QString cachedId;
                    for (const auto &row : saved)
                        if (row.value("seed").toInt(-1) == seed &&
                            row.value("direction").toInt(-1) == direction)
                            cachedId = row.value("id").toString();
                    if (!cachedId.isEmpty()) {
                        auto cached = AnalysisStore::read(path, cachedId, d, &error);
                        require(bool(cached), error);
                        mark(QStringLiteral("cachedRead"), QStringLiteral("seed=%1").arg(seed));
                        reply.results.append(cached);
                        continue;
                    }
                }
                auto r = std::make_shared<Result>();
                r->dataset = d;
                r->id = request.replaceId.isEmpty()
                            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                            : request.replaceId;
                r->style = request.style;
                if (!request.replaceId.isEmpty() && r->style.isEmpty())
                    for (const auto &row : saved)
                        if (row.value("id").toString() == request.replaceId)
                            r->style = row.value("style").toObject();
                r->seed = seed;
                r->direction = direction;
                r->nodes.resize(d->nodes.size());
                r->links.resize(d->links.size());
                int code = swmm_trace_estimate(handle.get(), direction, seed, r->nodes.data(),
                                               r->nodes.size(), r->links.data(), r->links.size(),
                                               &r->summary, cb, &p);
                require(code == 0, QString::fromUtf8(swmm_trace_error(handle.get())));
                mark(QStringLiteral("estimate"), QStringLiteral("seed=%1").arg(seed));
                // Retain computed work for a save retry even if the writer fails.
                reply.results.append(r);
                require(AnalysisStore::save(path, *r, &error, cancelFlag.get()), error);
                r->saved = true;
                mark(QStringLiteral("save"), QStringLiteral("seed=%1").arg(seed));
            }
        } catch (const std::exception &e) {
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
bool TraceController::hasOpenLayer(const QString &resultId) const
{
    if (!m_project || !m_project->canvas())
        return false;
    for (auto *layer : m_project->canvas()->layers())
        if (auto *output = qobject_cast<SWMMResultsLayer *>(layer))
            for (auto *sub : output->sublayers())
                if (auto *trace = dynamic_cast<TraceSublayer *>(sub))
                    if (trace->layer()->result()->id == resultId)
                        return true;
    return false;
}
void TraceController::refreshOpenLayers(const std::shared_ptr<Result> &result)
{
    if (!m_project || !m_project->canvas())
        return;
    for (auto *layer : m_project->canvas()->layers())
        if (auto *output = qobject_cast<SWMMResultsLayer *>(layer))
            for (auto *sub : output->sublayers())
                if (auto *trace = dynamic_cast<TraceSublayer *>(sub))
                    if (trace->layer()->result()->id == result->id &&
                        trace->layer()->result() != result)
                        trace->layer()->setResult(result);
}
bool TraceController::beforeOverwrite(const QString &path, QString *error)
{
    if (m_activePath == path && busy()) {
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
    if (!old.fingerprint.isEmpty()) {
        try {
            std::atomic_bool keepGoing{false};
            if (fingerprint(path, keepGoing) != old.fingerprint) {
                if (error)
                    *error = tr("The previous output has been replaced externally. Its saved "
                                "analysis is preserved, but this file cannot be retained as that "
                                "run. Select a different output path.");
                return false;
            }
        } catch (const std::exception &e) {
            if (error)
                *error = QString::fromUtf8(e.what());
            return false;
        }
    }
    QString directory = QFileInfo(old.packagePath).absolutePath();
    if (!QDir().mkpath(directory)) {
        if (error)
            *error = tr("Cannot create the previous-run folder.");
        return false;
    }
    QString retained = QDir(directory).filePath(QStringLiteral("retained-%1.out").arg(old.id));
    if (QFile::exists(retained)) {
        try {
            std::atomic_bool keepGoing{false};
            if (fingerprint(retained, keepGoing) != fingerprint(path, keepGoing)) {
                if (error)
                    *error = tr("The retained-output destination contains different data.");
                return false;
            }
        } catch (const std::exception &e) {
            if (error)
                *error = QString::fromUtf8(e.what());
            return false;
        }
    }
    if (!QFile::exists(retained) && !QFile::copy(path, retained)) {
        if (error)
            *error = tr("Cannot retain the previous output. Choose another output "
                        "path or disable Keep previous raw results.");
        return false;
    }
    QString rpt = old.reportPath.isEmpty() ? QFileInfo(path).absolutePath() + "/" +
                                                 QFileInfo(path).completeBaseName() + ".rpt"
                                           : old.reportPath;
    QString rptCopy = retained + ".rpt";
    if (QFile::exists(rpt) && !QFile::exists(rptCopy) && !QFile::copy(rpt, rptCopy)) {
        if (error)
            *error = tr("Cannot retain the previous report.");
        return false;
    }
    QSaveFile snapshotFile(retained + QStringLiteral(".model.json"));
    auto bytes = QJsonDocument(old.snapshot).toJson();
    if (!snapshotFile.open(QIODevice::WriteOnly) || snapshotFile.write(bytes) != bytes.size() ||
        !snapshotFile.commit()) {
        if (error)
            *error = tr("Cannot retain the matching model snapshot.");
        return false;
    }
    registry->setRetainedPath(old.id, retained);
    return true;
}
} // namespace openswmmvis::trace
