/* SPDX-License-Identifier: GPL-3.0-or-later */

/**
 * @file      checkpointworker.cpp
 * @brief     Worker-side checkpoint operations using value-only requests and results.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "simulation/checkpointworker.h"
#include "simulation/simulationrunner.h"
#include "simulation/runpathlease.h"
#include "simulation/twodbackendpolicy.h"
#include "simulation/snowledger.h"
#include <openswmm/engine/openswmm_massbalance.h>
#ifdef OPENSWMM_HAS_2D
#include <openswmm/engine/openswmm_2d.h>
#endif
#include <openswmm/engine/openswmm_checkpoint.h>
#include <openswmm/engine/openswmm_model.h>
#include <QtConcurrent>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>

struct CheckpointWorker::Control {
    std::atomic_bool cancelled{false};
    QMutex mutex;
    bool acceptingSaves = true;
    QStringList saves;
};
namespace
{
/**
 * @brief Choose the bounded private pool for checkpoint IO or runs.
 * @param[in] run True selects the two-thread run/resume pool; false selects one-thread inspection/export IO.
 * @return Borrowed process-lifetime pool, separate from GUI rendering tasks.
 */
QThreadPool *checkpointPool(bool run)
{
    static QThreadPool io, runs;
    static const bool configured = [] {
        io.setMaxThreadCount(1);
        runs.setMaxThreadCount(2);
        return true;
    }();
    Q_UNUSED(configured);
    return run ? &runs : &io;
}
struct Failure {
    int code;
    QString message;
};
/**
 * @brief Throw a checkpoint-worker Failure when an invariant is not satisfied.
 * @param[in] ok Condition to enforce.
 * @param[in] message Diagnostic copied into the failure.
 * @param[in] code SWMM error code, default BADPARAM.
 * @throws Failure When ok is false.
 */
void require(bool ok, const QString &message, int code = SWMM_ERR_BADPARAM)
{
    if (!ok)
        throw Failure{code, message};
}
/**
 * @brief Convert a checkpoint API error into a structured worker failure.
 * @param[in] code SWMM return code, zero for success.
 * @throws Failure For nonzero code with the checkpoint API current error message.
 */
void checkpoint(int code)
{
    if (code)
        throw Failure{code, QString::fromUtf8(swmm_checkpoint_last_error_msg())};
}
struct Engine {
    SWMM_Engine handle = swmm_engine_create();
    /**
     * @brief Create a worker-owned native engine or throw NOMEM Failure.
     */
    Engine()
    {
        require(handle, QStringLiteral("Cannot allocate checkpoint engine."), SWMM_ERR_NOMEM);
    }
    /**
     * @brief Close and destroy the worker-owned engine on every exit path.
     */
    ~Engine()
    {
        swmm_engine_close(handle);
        swmm_engine_destroy(handle);
    }
    /**
     * @brief Raise a worker failure for a native engine error.
     * @param[in] code SWMM return code; zero is a no-op.
     * @throws Failure For nonzero code using the engine-specific last message or code fallback.
     */
    void checked(int code) const
    {
        if (code) {
            auto message = QString::fromUtf8(swmm_get_last_error_msg(handle));
            if (message.isEmpty())
                message = QStringLiteral("Checkpoint engine operation failed [%1].").arg(code);
            throw Failure{code, message};
        }
    }
};
struct Snapshot {
    SWMM_CheckpointInspection handle = nullptr;
    /**
     * @brief Release the owned native checkpoint inspection handle, including null-safe cleanup.
     */
    ~Snapshot() { swmm_checkpoint_inspection_destroy(handle); }
};
/**
 * @brief Validate absolute path text before handing it to native checkpoint APIs.
 * @param[in] path Candidate text.
 * @return True for nonempty absolute round-trip UTF-8 without embedded NULs; no existence/readability check.
 */
bool absolutePath(const QString &path)
{
    return !path.isEmpty() && !path.contains(QChar(0)) &&
           QString::fromUtf8(path.toUtf8()) == path && QFileInfo(path).isAbsolute();
}
/**
 * @brief Convert a Qt path to the platform filesystem path type.
 * @param[in] path Path text.
 * @return Wide Windows or UTF-8 non-Windows path; no IO.
 */
std::filesystem::path nativePath(const QString &path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toUtf8().constData());
#endif
}
/**
 * @brief Delegate alias/ancestor detection to the shared reservation policy.
 * @param[in] a First file/directory path.
 * @param[in] b Second file/directory path.
 * @return RunPathLease::overlaps() result.
 */
bool overlaps(const QString &a, const QString &b)
{
    return RunPathLease::overlaps(a, b);
}
struct Lease : RunPathLease {
    /**
     * @brief Create a scoped native-operation reservation group.
     * @param[in] owner Opaque operation token shared with related leases; never dereferenced.
     */
    explicit Lease(const void *owner) : RunPathLease(owner) {}
    /**
     * @brief Reserve a native operation path or throw an IO failure.
     * @param[in] path Candidate path, empty is a no-op.
     * @param[in] write Writer/exclusive intent.
     * @param[in] active Additional reader/writer paths that must not overlap.
     * @throws Failure When RunPathLease rejects the reservation.
     */
    void add(const QString &path, bool write, const QStringList &active = {})
    {
        QString error;
        require(RunPathLease::add(path, write, &error, active), error, SWMM_ERR_IO);
    }
};
/**
 * @brief Hash the immutable input snapshot on the worker.
 * @param[in] path File to read completely.
 * @return SHA-256 bytes.
 * @throws Failure For open/hash failure using SWMM_ERR_IO.
 */
QByteArray inputHash(const QString &path)
{
    QFile input(path);
    require(input.open(QIODevice::ReadOnly), QStringLiteral("Cannot read the model snapshot."),
            SWMM_ERR_IO);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    require(hash.addData(&input), QStringLiteral("Cannot hash the model snapshot."), SWMM_ERR_IO);
    return hash.result();
}
/**
 * @brief Reject authored external plugins/process components unsupported by the native checkpoint builder.
 * @param[in] path UTF-8 model snapshot scanned without mutation.
 * @throws Failure For unreadable input or nonempty PLUGINS/PROCESS_COMPONENTS rows.
 */
void requireNativeInput(const QString &path)
{
    QFile input(path);
    require(input.open(QIODevice::ReadOnly), QStringLiteral("Cannot read the model snapshot."),
            SWMM_ERR_IO);
    bool external = false;
    while (!input.atEnd()) {
        auto line = QString::fromUtf8(input.readLine()).section(';', 0, 0).trimmed();
        if (line.startsWith(QChar(0xfeff)))
            line.remove(0, 1);
        if (line.startsWith('[') && line.contains(']')) {
            const auto section = line.mid(1, line.indexOf(']') - 1).trimmed().toUpper();
            external = section == "PLUGINS" || section == "PROCESS_COMPONENTS";
        } else if (external && !line.isEmpty()) {
            throw Failure{
                SWMM_ERR_LIFECYCLE,
                QStringLiteral(
                    "Native checkpoint worker cannot open plugin or process-component rows.")};
        }
    }
    require(input.error() == QFileDevice::NoError,
            QStringLiteral("Cannot scan the model snapshot."), SWMM_ERR_IO);
}
/**
 * @brief Inspect and structurally validate native checkpoint clocks, coverage, and companions.
 * @param[in] engine Borrowed worker engine prepared for the frozen model.
 * @param[in] path Absolute checkpoint path to inspect.
 * @return Owning schema-one metadata with validated finite clocks, decimal count strings, and absolute companion paths.
 * @throws Failure For native inspection/JSON/schema errors or unavailable external-driver metadata.
 */
QJsonObject inspect(SWMM_Engine engine, const QString &path)
{
    Snapshot snapshot;
    checkpoint(swmm_checkpoint_inspect(engine, path.toUtf8().constData(), &snapshot.handle));
    size_t size = 0;
    checkpoint(swmm_checkpoint_inspection_json(snapshot.handle, nullptr, 0, &size));
    require(size > 1 && size <= size_t(std::numeric_limits<int>::max()),
            QStringLiteral("Invalid checkpoint metadata size."), SWMM_ERR_INTERNAL);
    QByteArray bytes(int(size), Qt::Uninitialized);
    checkpoint(swmm_checkpoint_inspection_json(snapshot.handle, bytes.data(), size, &size));
    bytes.chop(1);
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && doc.isObject(),
            QStringLiteral("Invalid checkpoint metadata document."), SWMM_ERR_INTERNAL);
    auto m = doc.object();
    require(m.value("schema_version").toInt(-1) == 1,
            QStringLiteral("Unsupported checkpoint metadata schema."), SWMM_ERR_LIFECYCLE);
    auto number = [](const QJsonValue &v) { return v.isDouble() && std::isfinite(v.toDouble()); };
    auto count = [](const QJsonValue &v) {
        if (!v.isString())
            return false;
        const auto s = v.toString();
        bool ok = false;
        s.toULongLong(&ok);
        return ok && !s.isEmpty() &&
               std::all_of(s.begin(), s.end(), [](QChar c) { return c >= '0' && c <= '9'; });
    };
    for (const auto *k : {"engine_seconds", "engine_date", "start_date", "report_start_date",
                          "next_report_seconds"})
        require(number(m.value(k)), QStringLiteral("Invalid checkpoint clock metadata."),
                SWMM_ERR_INTERNAL);
    const auto source = m.value("completed_source_seconds");
    require(source.isNull() || number(source), QStringLiteral("Invalid source clock."),
            SWMM_ERR_INTERNAL);
    for (const auto *k : {"checkpoint_bytes", "source_frames", "source_prefix_bytes"})
        require(count(m.value(k)), QStringLiteral("Invalid checkpoint count metadata."),
                SWMM_ERR_INTERNAL);
    for (const auto *k : {"file_outputs", "relocatable_bundle", "detailed_sources"})
        require(m.value(k).isBool(), QStringLiteral("Invalid checkpoint recording metadata."),
                SWMM_ERR_INTERNAL);
    for (const auto *k : {"binary", "mesh"}) {
        require(m.value(k).isObject(), QStringLiteral("Invalid report metadata."),
                SWMM_ERR_INTERNAL);
        const auto report = m.value(k).toObject();
        require(report.value("enabled").isBool() && count(report.value("records")) &&
                    number(report.value("cadence_seconds")),
                QStringLiteral("Invalid report metadata."), SWMM_ERR_INTERNAL);
        for (const auto *date : {"first_date", "last_date"})
            require(report.value(date).isNull() || number(report.value(date)),
                    QStringLiteral("Invalid report date."), SWMM_ERR_INTERNAL);
    }
    // The native builder deliberately has no external-driver adapter.
    require(m.value("driver").isNull(),
            QStringLiteral("This checkpoint requires an unavailable external-driver adapter."),
            SWMM_ERR_LIFECYCLE);
    require(m.value("companions").isArray(), QStringLiteral("Invalid companion metadata."),
            SWMM_ERR_INTERNAL);
    for (const auto &v : m.value("companions").toArray()) {
        const auto f = v.toObject();
        const auto role = f.value("role").toString();
        require(v.isObject() && (role == "source" || role == "binary" || role == "mesh") &&
                    absolutePath(f.value("path").toString()) && count(f.value("required_bytes")),
                QStringLiteral("Invalid checkpoint companion."), SWMM_ERR_INTERNAL);
    }
    return m;
}
}
CheckpointWorker::CheckpointWorker(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<CheckpointResult>();
    qRegisterMetaType<CheckpointSaveResult>();
}
CheckpointWorker::~CheckpointWorker()
{
    cancel();
    m_future.waitForFinished();
}
void CheckpointWorker::cancel()
{
    if (m_control)
        m_control->cancelled = true;
}
void CheckpointWorker::invalidate()
{
    ++m_generation;
    cancel();
}
bool CheckpointWorker::queueSave(const QString &path)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!m_busy || !m_control || m_control->cancelled || !absolutePath(path))
        return false;
    QMutexLocker lock(&m_control->mutex);
    if (!m_control->acceptingSaves)
        return false;
    m_control->saves.append(path);
    return true;
}
bool CheckpointWorker::start(const CheckpointRequest &request)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_busy)
        return false;
    m_busy = true;
    const auto generation = ++m_generation;
    QFutureWatcher<CheckpointResult> *watcher = nullptr;
    bool launched = false;
    try {
        m_control = std::make_shared<Control>();
        m_control->acceptingSaves = (request.operation == CheckpointRequest::Operation::Resume ||
                                     request.operation == CheckpointRequest::Operation::Run);
        watcher = new QFutureWatcher<CheckpointResult>(this);
        connect(watcher, &QFutureWatcher<CheckpointResult>::finished, this,
                [this, watcher, generation] {
                    CheckpointResult result;
                    try {
                        result = watcher->result();
                    } catch (...) {
                        result.generation = generation;
                        result.errorCode = SWMM_ERR_INTERNAL;
                        result.error = QStringLiteral("Checkpoint worker result failed.");
                    }
                    if (m_control && m_control->cancelled)
                        result.cancelled = true;
                    if (result.cancelled &&
                        result.operation == CheckpointRequest::Operation::Inspect) {
                        result.success = false;
                        result.metadata = {};
                    }
                    result.stale = result.generation != m_generation;
                    if (result.stale)
                        result.metadata = {};
                    m_busy = false;
                    m_control.reset();
                    watcher->deleteLater();
                    emit finished(result);
                });
        m_future = QtConcurrent::run(
            checkpointPool(request.operation == CheckpointRequest::Operation::Resume ||
                           request.operation == CheckpointRequest::Operation::Run),
            [request, generation, control = m_control, this] {
                return execute(request, generation, control, this);
            });
        launched = true;
        watcher->setFuture(m_future);
    } catch (...) {
        // A watcher-allocation failure after dispatch must still drain the task;
        // keep this receiver alive and preserve any completed publication.
        if (launched) {
            cancel();
            m_future.waitForFinished();
            try {
                auto result = m_future.result();
                result.cancelled = true;
                result.stale = generation != m_generation;
                if (result.operation == CheckpointRequest::Operation::Inspect)
                    result.success = false;
                if (result.stale || result.operation == CheckpointRequest::Operation::Inspect)
                    result.metadata = {};
                delete watcher;
                watcher = nullptr;
                m_busy = false;
                m_control.reset();
                emit finished(result);
                return true;
            } catch (...) {
            }
        }
        delete watcher;
        m_busy = false;
        m_control.reset();
        return false;
    }
    return true;
}
CheckpointResult CheckpointWorker::execute(const CheckpointRequest &request, quint64 generation,
                                           const std::shared_ptr<Control> &control,
                                           CheckpointWorker *receiver)
{
    CheckpointResult result;
    result.generation = generation;
    result.projectRevision = request.projectRevision;
    result.operation = request.operation;
    std::unique_ptr<QFile> runLog;
    Lease outputs(control.get());
    auto cancelled = [&] {
        if (control->cancelled)
            throw Failure{SWMM_OK, QStringLiteral("Checkpoint operation cancelled.")};
    };
    try {
        cancelled();
        require(request.engineVersion == "6.0.0",
                QStringLiteral("Checkpoint recovery requires the linked current engine."),
                SWMM_ERR_LIFECYCLE);
        if (request.operation != CheckpointRequest::Operation::Run)
            require(absolutePath(request.checkpointPath),
                    QStringLiteral("Choose an absolute checkpoint path."));
        for (const auto &p : request.protectedInputs + request.activePaths)
            require(absolutePath(p), QStringLiteral("Protected paths must be absolute."));
        Lease inputs(control.get());
        if (!request.checkpointPath.isEmpty())
            inputs.add(request.checkpointPath, false, request.activePaths);
        // Protected paths are overwrite guards, not resources this operation reads.
        // Other projects may be actively writing some of these guarded files.
        if (request.operation == CheckpointRequest::Operation::Export) {
            require(absolutePath(request.destination),
                    QStringLiteral("Choose a new absolute bundle folder."));
            outputs.add(request.destination, true, request.activePaths);
            for (const auto &p : request.protectedInputs)
                require(!overlaps(request.destination, p),
                        QStringLiteral("Bundle folder overlaps a protected input."), SWMM_ERR_IO);
            cancelled();
            checkpoint(swmm_checkpoint_export_bundle(request.checkpointPath.toUtf8().constData(),
                                                     request.destination.toUtf8().constData()));
            result.publishedPath = request.destination;
            result.success = true;
        } else {
            require(absolutePath(request.modelPath) && request.modelSha256.size() == 32,
                    QStringLiteral("A frozen model snapshot and SHA-256 are required."));
            inputs.add(request.modelPath, false, request.activePaths);
            require(inputHash(request.modelPath) == request.modelSha256,
                    QStringLiteral("Model snapshot has changed."), SWMM_ERR_IO);
            requireNativeInput(request.modelPath);
            require(request.outputProfile >= 2 ||
                        SimulationRunner::parseTwoDOutputFile(request.modelPath).isEmpty(),
                    QStringLiteral(
                        "This input declares a mesh writer; select a mesh recording profile."),
                    SWMM_ERR_LIFECYCLE);
            require(request.outputProfile >= 0 && request.outputProfile <= 4 &&
                        (!request.detailedSources || request.sourceHistory) &&
                        (request.outputProfile != 4 || request.sourceHistory),
                    QStringLiteral("Invalid checkpoint recording profile."));
            require(!request.ownershipToken.contains(QChar(0)) &&
                        QString::fromUtf8(request.ownershipToken.toUtf8()) ==
                            request.ownershipToken,
                    QStringLiteral("Invalid ownership review token."));
            QString directory;
            std::unique_ptr<QTemporaryDir> scratch;
            if (request.operation == CheckpointRequest::Operation::Inspect) {
                require(absolutePath(request.stagingRoot) && QFileInfo(request.stagingRoot).isDir(),
                        QStringLiteral("Choose an existing reviewable staging directory."));
                scratch = std::make_unique<QTemporaryDir>(
                    QDir(request.stagingRoot).filePath("checkpoint-check-XXXXXX"));
                require(scratch->isValid(),
                        QStringLiteral("Cannot allocate private inspection paths."), SWMM_ERR_IO);
                directory = scratch->path();
            } else {
                require(absolutePath(request.destination),
                        QStringLiteral("Choose a new absolute run folder."));
                directory = request.destination;
            }
            // One exclusive folder covers all native output members, logs and
            // later publication staging; readers and other runs cannot overlap.
            outputs.add(directory, true, request.activePaths);
            for (const auto &p :
                 request.protectedInputs + QStringList{request.modelPath, request.checkpointPath})
                require(!overlaps(directory, p),
                        QStringLiteral("Run folder overlaps a protected input."), SWMM_ERR_IO);
            if (!scratch) {
                std::error_code ec;
                require(std::filesystem::create_directory(nativePath(directory), ec) && !ec,
                        QStringLiteral("Run folder must be new and writable."), SWMM_ERR_IO);
            }
            const QDir dir(directory);
            const auto report = dir.filePath("run.rpt"), binary = dir.filePath("run.out"),
                       mesh = dir.filePath("mesh.h5"), journal = dir.filePath("source.history");
            result.outputPaths = QStringList{report};
            if (request.outputProfile == 1 || request.outputProfile >= 3)
                result.outputPaths.append(binary);
            if (request.outputProfile >= 2)
                result.outputPaths.append(mesh);
            if (request.sourceHistory)
                result.outputPaths.append(journal);
            if (!scratch) {
                const auto logPath = dir.filePath("run.runlog.txt");
                runLog = std::make_unique<QFile>(logPath);
                require(runLog->open(QIODevice::WriteOnly | QIODevice::NewOnly),
                        QStringLiteral("Cannot create the private run log."), SWMM_ERR_IO);
                result.outputPaths.append(logPath);
            }
            cancelled();
            Engine engine;
            unsigned long caps = 0;
            checkpoint(swmm_checkpoint_capabilities(engine.handle, &caps));
            require(caps & SWMM_CHECKPOINT_SESSION_BUILDER,
                    QStringLiteral("Native checkpoint session construction unavailable."),
                    SWMM_ERR_LIFECYCLE);
            const auto inp = request.modelPath.toUtf8(), rpt = report.toUtf8(),
                       out = binary.toUtf8();
            engine.checked(swmm_engine_open(
                engine.handle, inp.constData(), rpt.constData(),
                (request.outputProfile == 1 || request.outputProfile >= 3) ? out.constData()
                                                                           : nullptr,
                nullptr));
            QString backendNotice;
            const int backendRc =
                openswmmvis::simulation::applyTwoDBackendPolicy(engine.handle, backendNotice);
            require(backendRc == SWMM_OK, backendNotice, backendRc);
            if (!backendNotice.isEmpty())
                qInfo().noquote() << backendNotice;
            require(inputHash(request.modelPath) == request.modelSha256,
                    QStringLiteral("Model snapshot changed while opening."), SWMM_ERR_IO);
            const auto meshPath = (request.outputProfile >= 2 ? mesh : QString()).toUtf8();
            const int meshSet = swmm_file_path_set(engine.handle, SWMM_FILE_OUTPUT_2D, nullptr,
                                                   meshPath.constData());
            require(meshSet == SWMM_OK ||
                        (meshSet == SWMM_ERR_BADPARAM &&
                         !(caps & SWMM_CHECKPOINT_SURFACE_PROFILE) && request.outputProfile < 2),
                    QStringLiteral("Cannot prepare the mesh output path."), SWMM_ERR_LIFECYCLE);
            const auto source = (request.sourceHistory ? journal : QString()).toUtf8(),
                       token = request.ownershipToken.toUtf8();
            const SWMM_CheckpointSessionOptions options{sizeof options,
                                                        1,
                                                        request.outputProfile,
                                                        source.constData(),
                                                        request.detailedSources ? 1 : 0,
                                                        token.constData()};
            cancelled();
            checkpoint(swmm_checkpoint_start_session(engine.handle, &options));
            cancelled();
            if (request.operation != CheckpointRequest::Operation::Run)
                result.metadata = inspect(engine.handle, request.checkpointPath);
            for (const auto &value : result.metadata.value("companions").toArray())
                inputs.add(value.toObject().value("path").toString(), false, request.activePaths);
            if (request.operation == CheckpointRequest::Operation::Inspect)
                result.acceptedSeconds = result.metadata.value("engine_seconds").toDouble();
            cancelled();
            if (request.operation == CheckpointRequest::Operation::Resume ||
                request.operation == CheckpointRequest::Operation::Run) {
                result.acceptedSeconds = 0;
                if (request.operation == CheckpointRequest::Operation::Resume)
                    checkpoint(swmm_checkpoint_restore(
                        engine.handle, request.checkpointPath.toUtf8().constData()));
                engine.checked(swmm_get_start_time(engine.handle, &result.startDate));
                engine.checked(swmm_get_end_time(engine.handle, &result.endDate));
                const auto start = result.startDate, end = result.endDate;
                QMetaObject::invokeMethod(
                    receiver,
                    [receiver, generation, start, end] {
                        if (receiver->m_generation == generation)
                            emit receiver->timeline(generation, start, end);
                    },
                    Qt::QueuedConnection);
                engine.checked(
                    swmm_engine_get_elapsed_seconds(engine.handle, &result.acceptedSeconds));
#ifdef OPENSWMM_HAS_2D
                int surfaceActive = 0;
                engine.checked(swmm_2d_is_active(engine.handle, &surfaceActive));
#endif
                auto savePending = [&] {
                    QStringList pending;
                    {
                        QMutexLocker lock(&control->mutex);
                        pending.swap(control->saves);
                    }
                    result.saves.reserve(result.saves.size() + pending.size());
                    for (const auto &path : pending) {
                        CheckpointSaveResult saved;
                        saved.path = path;
                        try {
                            for (const auto &p :
                                 request.protectedInputs +
                                     QStringList{request.modelPath, request.checkpointPath})
                                require(!overlaps(path, p),
                                        QStringLiteral("Save path overlaps a protected input."),
                                        SWMM_ERR_IO);
                            Lease save(control.get());
                            save.add(path, true, request.activePaths);
                            outputs.add(
                                path,
                                false); // Reserve catalog input before publication can succeed.
                            checkpoint(
                                swmm_checkpoint_save(engine.handle, path.toUtf8().constData()));
                            saved.success = true;
                            saved.acceptedSeconds = result.acceptedSeconds;
                            saved.acceptedDate =
                                result.startDate + result.acceptedSeconds / 86400.0;
                        } catch (const Failure &f) {
                            saved.errorCode = f.code;
                            saved.error = f.message;
                        }
                        result.saves.append(saved);
                        QMetaObject::invokeMethod(
                            receiver,
                            [receiver, generation, saved] {
                                emit receiver->saved(generation, saved);
                            },
                            Qt::QueuedConnection);
                    }
                };
                auto notify = [&] {
                    const auto seconds = result.acceptedSeconds;
                    const auto snow = openswmmvis::simulation::snowLedgerRows(engine.handle);
                    QMetaObject::invokeMethod(
                        receiver,
                        [receiver, generation, seconds, snow] {
                            if (receiver->m_generation != generation)
                                return;
                            emit receiver->progress(generation, seconds);
                            emit receiver->snowLedgerChanged(generation, snow);
                        },
                        Qt::QueuedConnection);
                };
                notify();
                QElapsedTimer lastProgress;
                lastProgress.start();
                while (!control->cancelled) {
                    savePending();
                    double elapsed = 0;
                    const int stepCode = swmm_engine_step(engine.handle, &elapsed);
                    // Recording IO can fail after physics was accepted. Read the
                    // actual boundary before propagating that failure.
                    engine.checked(
                        swmm_engine_get_elapsed_seconds(engine.handle, &result.acceptedSeconds));
                    engine.checked(stepCode);
                    if (lastProgress.elapsed() >= 100) {
                        notify();
                        lastProgress.restart();
                    }
                    if (elapsed == 0)
                        break;
                }
                savePending();
                notify();
#ifdef OPENSWMM_HAS_2D
                // end() releases the surface router; read the accepted balance
                // while its context is still available.
                if (surfaceActive)
                    engine.checked(swmm_2d_get_continuity_error(engine.handle, &result.twoDError));
#endif
                engine.checked(swmm_engine_end(engine.handle));
                engine.checked(swmm_engine_report(engine.handle));
                engine.checked(
                    swmm_get_runoff_continuity_error(engine.handle, &result.runoffError));
                engine.checked(
                    swmm_get_routing_continuity_error(engine.handle, &result.routingError));
                result.success = !control->cancelled;
            } else
                result.success = true;
        }
    } catch (const Failure &f) {
        result.errorCode = f.code;
        result.error = f.message;
    } catch (const std::bad_alloc &) {
        result.errorCode = SWMM_ERR_NOMEM;
        result.error = QStringLiteral("Cannot allocate checkpoint worker memory.");
    } catch (const std::exception &e) {
        result.errorCode = SWMM_ERR_INTERNAL;
        result.error = QString::fromUtf8(e.what());
    } catch (...) {
        result.errorCode = SWMM_ERR_INTERNAL;
        result.error = QStringLiteral("Unknown checkpoint worker failure.");
    }
    result.cancelled = control->cancelled;
    if (request.operation == CheckpointRequest::Operation::Inspect)
        result.outputPaths.clear();
    if (result.cancelled && request.operation == CheckpointRequest::Operation::Inspect) {
        result.success = false;
        result.metadata = {};
        result.outputPaths.clear();
    }
    {
        QMutexLocker lock(&control->mutex);
        control->acceptingSaves = false;
        for (const auto &path : control->saves)
            result.saves.append(
                {path, false, SWMM_ERR_LIFECYCLE,
                 QStringLiteral("Run ended before this Save request could be processed.")});
        control->saves.clear();
    }
    if (runLog && runLog->isOpen()) {
        try {
            const auto text =
                QStringLiteral(
                    "Project revision: %1\nAccepted seconds: %2\nSuccess: %3\nCancel requested: %4\nError [%5]: %6\n")
                    .arg(result.projectRevision)
                    .arg(result.acceptedSeconds, 0, 'g', 17)
                    .arg(result.success)
                    .arg(result.cancelled)
                    .arg(result.errorCode)
                    .arg(result.error)
                    .toUtf8();
            if (runLog->write(text) != text.size() || !runLog->flush()) {
                result.success = false;
                if (!result.errorCode) {
                    result.errorCode = SWMM_ERR_IO;
                    result.error =
                        QStringLiteral("Accepted run state retained; run log write failed.");
                }
            }
        } catch (...) {
            result.success = false;
            result.errorCode = SWMM_ERR_INTERNAL;
            result.error =
                QStringLiteral("Accepted run state retained; run log finalization failed.");
        }
        runLog->close();
    }
    return result;
}
