/*!
 * \file   simulationrunner.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "simulation/simulationrunner.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_massbalance.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_callbacks.h>
#include <openswmm/engine/openswmm_2d.h>

#include "core/preferencesmanager.h"
#include "core/swmmdatetime.h"
#include "mesh/meshcellgeom.h"   // mesh::kEdgeStride / edgeSlot — 2D edge-slot layout
#include "legacy_version.h"      // LEGACY_SWMM_VERSION — the unversioned worker's engine

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>
#include <QThread>
#include <QVector>
#include <exception>
#include <filesystem>
#include <memory>
#include <new>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#ifdef Q_OS_UNIX
#include <sys/file.h>
#include <cerrno>
#include <cstring>
#elif defined(Q_OS_WIN)
#include <qt_windows.h>
#endif

namespace {

// H5Fcreate(TRUNC) can truncate an existing output before discovering that
// another app still has its HDF5 lock. Probe without creating/truncating the
// file, AFTER the GUI has released its own result readers and BEFORE any
// report/output writer starts. This also catches a locked zero-byte output
// left by an earlier failed attempt. The engine still owns the run-time lock.
bool twoDOutputAvailable(const QString& path, QString& error)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) return true;
    bool busy=false;
    QString detail;
#ifdef Q_OS_UNIX
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite | QIODevice::ExistingOnly)) {
        detail=file.errorString();
    } else {
        int result;
        do { result=::flock(file.handle(),LOCK_EX|LOCK_NB); } while(result<0 && errno==EINTR);
        if (result==0) return true; // QFile destruction releases the probe lock
        busy=errno==EWOULDBLOCK || errno==EAGAIN;
        detail=QString::fromLocal8Bit(std::strerror(errno));
    }
#elif defined(Q_OS_WIN)
    const QString native=QDir::toNativeSeparators(path);
    HANDLE file=CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),GENERIC_WRITE,
                            0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file!=INVALID_HANDLE_VALUE) { CloseHandle(file); return true; }
    const DWORD code=GetLastError();
    busy=code==ERROR_SHARING_VIOLATION || code==ERROR_LOCK_VIOLATION;
    detail=QString::number(code);
#else
    return true;
#endif
    error=busy
        ? SimulationRunner::tr("2D results file is in use by another simulation or results window:\n%1\n"
                               "Close that results view (including other app instances), or wait for its simulation to finish, then retry.").arg(path)
        : SimulationRunner::tr("Cannot access the 2D results file for writing:\n%1\n%2").arg(path,detail);
    return false;
}

// Append-only per-run log beside the report (<rpt stem>.runlog.txt): the
// phases the worker passed through, the outcome and the timing. Flushed per
// line so a hard crash still leaves what happened up to that point. Written
// by the worker thread only.
struct RunLog {
    QFile file;
    explicit RunLog(const QString &rptPath)
    {
        const QFileInfo fi(rptPath);
        file.setFileName(fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName()
                         + QStringLiteral(".runlog.txt"));
        file.open(QIODevice::Append | QIODevice::Text);
    }
    void line(const QString &text)
    {
        if (!file.isOpen()) return;
        file.write((QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
                    + QLatin1Char(' ') + text + QLatin1Char('\n')).toUtf8());
        file.flush();
    }
};

// Test-only fault injection: SWMMVIS_TEST_FAULT="<phase>:<kind>" makes the
// worker fail deliberately at that phase (kind "bad_alloc" throws,
// "numerical" returns SWMM_ERR_NUMERICAL from a step). Empty = inert.
struct TestFault {
    QString phase, kind;
    bool at(const char *p) const { return phase == QLatin1String(p); }
};

// swmm_2d_get_run_stats → SimulationRunner::twoDSolverStats, queued onto the
// GUI thread. Called from the worker with the engine still open; a refused
// read (2D not active, solver finalised) simply emits nothing.
void emitTwoDSolverStats(SimulationRunner *self, int jobId, SWMM_Engine eng)
{
    SWMM_2DRunStats st{};
    if (swmm_2d_get_run_stats(eng, &st) != SWMM_OK) return;
    QVector<qint64> tiers;
    for (int k = 0; k < st.n_tiers && k < 8; ++k)
        tiers.push_back(st.tier_cells[k]);
    const QString backend  = QString::fromUtf8(st.backend);
    const int     momentum = st.momentum;
    const int     ltsTiers = st.lts_tiers;
    const qint64  steps    = st.steps;
    QMetaObject::invokeMethod(self,
        [self, jobId, backend, momentum, ltsTiers, steps, tiers]() {
            emit self->twoDSolverStats(jobId, backend, momentum, ltsTiers,
                                       steps, tiers);
        },
        Qt::QueuedConnection);
}

} // namespace
#include <QCoreApplication>
#include <QtNumeric>

#include <cmath>

namespace {

/**
 * @brief Convert a SWMM engine time (OADate: decimal days since
 *        1899-12-30 00:00:00 local) to a QDateTime. Returns an invalid
 *        QDateTime for non-finite or clearly-bogus values (≤ epoch).
 */
QDateTime oaDateToQDateTime(double oaDate)
{
    if (!(oaDate > 0.0) || !std::isfinite(oaDate)) return QDateTime();
    // UTC, NOT LocalTime: this anchors the 2D results time axis (HDF5 sim_start_
    // and the live per-tick curQDT). The 1D results clock is built in UTC
    // (openswmmvis::core::swmmDateTimeToQDateTime). The animation controller's
    // causal sync compares 2D frame times against the 1D cursor as absolute
    // instants (setCurrentSimTimeAsOf: ti <= cursor), so a LocalTime epoch here
    // offset every 2D frame by the local UTC offset and froze 2D playback (the
    // cursor never reached the shifted frames). SWMM dates are nominal/zone-
    // less, so UTC reproduces the model date verbatim and keeps both axes on
    // one basis.
    return openswmmvis::core::swmmDateTimeToQDateTime(oaDate);
}

/**
 * @brief Find the legacy worker executable for a 5.x engine version.
 *
 * Workers are versioned: `openswmm-legacy-worker-<version>` (the 5.2.4 one is
 * built by cmake/EngineVersions.cmake). The unversioned `openswmm-legacy-worker`
 * the 6.x engine install ships IS the 5.3.0 worker, so it is accepted as a
 * fallback for LEGACY_SWMM_VERSION only — never for any other version, or a
 * missing 5.2.4 worker would silently run the model on 5.3.0.
 *
 * Searches, for each candidate name, in:
 *  1. Same directory as the current executable
 *  2. {app_dir}/bin/{CONFIG}/ (debug/release subdirs)
 *  3. {app_dir}/../bin/{CONFIG}/
 *
 * @return Absolute path to the worker, or an empty string if none was found.
 */
QString findLegacyWorker(const QString &version)
{
#ifdef Q_OS_WIN
    const QString exeSuffix = QStringLiteral(".exe");
#else
    const QString exeSuffix;
#endif
    QStringList workerNames;
    workerNames << QStringLiteral("openswmm-legacy-worker-") + version + exeSuffix;
    if (version == QLatin1String(LEGACY_SWMM_VERSION))
        workerNames << QStringLiteral("openswmm-legacy-worker") + exeSuffix;

    const QString appDir = QCoreApplication::applicationDirPath();

    // On macOS the executable lives inside the .app bundle at
    // {build}/SWMMVis.app/Contents/MacOS — walk up to the build root.
    // On other platforms applicationDirPath() IS the build/install root.
    QStringList roots;
    roots << appDir;                        // flat install / non-bundle
    roots << appDir + "/../../..";          // macOS .app bundle → build root
    roots << appDir + "/../..";             // one-level wrapper

    const QString buildConfig =
#ifdef QT_DEBUG
        QStringLiteral("Debug");
#else
        QStringLiteral("Release");
#endif

    QStringList searchPaths;
    for (const QString &root : roots) {
        searchPaths << root;
        searchPaths << root + "/bin";
        searchPaths << root + "/bin/" + buildConfig;
        searchPaths << root + "/openswmm_engine/src/legacy/worker";
    }

    for (const QString &workerName : workerNames) {
        for (const QString &dir : searchPaths) {
            const QString candidate = QFileInfo(dir + "/" + workerName).absoluteFilePath();
            if (QFile::exists(candidate))
                return candidate;
        }
    }

    // Empty on a miss so the caller's not-found check actually fires (the old
    // bare-name fallback to PATH made that check dead code).
    qWarning() << "Legacy worker executable not found for engine" << version
               << "— looked for" << workerNames << "in" << searchPaths;
    return QString();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public static helpers
// ---------------------------------------------------------------------------

namespace {
QString withoutInpComment(QString line)
{
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        if (line[i] == '"') quoted = !quoted;
        else if (line[i] == ';' && !quoted) { line.truncate(i); break; }
    }
    return line.trimmed();
}

// Match the engine's quoted-token and comma-delimiter rules for option rows.
// In a whitespace row an embedded comma belongs to a name; pure CSV separates it.
QStringList optionTokens(const QString &line)
{
    bool quoted = false, pureCsv = true;
    for (const auto c : line) {
        if (c == '"') quoted = !quoted;
        else if (!quoted && c.isSpace()) pureCsv = false;
    }
    const auto comma = [&](int i) {
        return line[i] == ',' && (pureCsv || i == 0 || i + 1 == line.size()
            || line[i - 1].isSpace() || line[i + 1].isSpace());
    };
    QStringList tokens;
    int i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i].isSpace()) ++i;
        if (i == line.size()) break;
        if (comma(i)) {
            if (i == 0 || line[i - 1] == ',') tokens.append(QString());
            ++i; continue;
        }
        if (line[i] == '"') {
            const int start = ++i;
            while (i < line.size() && line[i] != '"') ++i;
            tokens.append(line.mid(start, i - start));
            if (i < line.size()) ++i;
            while (i < line.size() && line[i].isSpace()) ++i;
            if (i < line.size() && line[i] == ',') ++i;
        } else {
            const int start = i;
            while (i < line.size() && !line[i].isSpace() && !comma(i)) ++i;
            tokens.append(line.mid(start, i - start));
            if (i < line.size() && comma(i)) ++i;
        }
    }
    return tokens;
}

struct RunFileReferences { QStringList inputs, outputs; };

RunFileReferences knownRunReferences(const QString &inp)
{
    RunFileReferences references;
    QFile file(inp);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return references;
    const QDir directory = QFileInfo(inp).absoluteDir();
    const auto path = [&](const QString &token) {
        return QDir::cleanPath(QFileInfo(token).isAbsolute() ? token : directory.absoluteFilePath(token));
    };
    const auto addInput = [&](const QString &token) {
        if (!token.isEmpty()) references.inputs.append(path(token));
    };
    QString section;
    while (!file.atEnd()) {
        const QString line = withoutInpComment(QString::fromUtf8(file.readLine()));
        if (line.isEmpty()) continue;
        if (line.startsWith('[') && line.endsWith(']')) {
            section = line.mid(1, line.size() - 2).trimmed().toUpper();
            continue;
        }
        const auto tokens = optionTokens(line);
        if (tokens.isEmpty()) continue;
        if (section == "FILES" && tokens.size() >= 3) {
            const auto mode = tokens[0].toUpper();
            if (mode == "USE") addInput(tokens[2]);
            else if (mode == "SAVE" && !tokens[2].isEmpty()) references.outputs.append(path(tokens[2]));
        } else if (section == "TIMESERIES" || section == "RAINGAGES") {
            for (int i = 1; i + 1 < tokens.size(); ++i) {
                if (tokens[i].compare("FILE", Qt::CaseInsensitive) != 0) continue;
                addInput(tokens[i + 1]);
                // The engine's timeseries path:column decorator is not part
                // of the physical datasource. Protect both forms conservatively.
                if (section == "TIMESERIES") {
                    const int colon = tokens[i + 1].lastIndexOf(':');
                    if (colon > 1) addInput(tokens[i + 1].left(colon));
                }
                break;
            }
        } else if ((section == "TEMPERATURE" || section == "2D_MESH_FILE"
                    || section.contains("INITIAL_QUALITY"))
                   && tokens.size() >= 2 && tokens[0].compare("FILE", Qt::CaseInsensitive) == 0) {
            // The engine's external-mesh handler also accepts unquoted paths
            // with spaces by joining every token after FILE.
            addInput(section == "2D_MESH_FILE" ? tokens.mid(1).join(' ') : tokens[1]);
        } else if (section == "PLUGINS") {
            addInput(tokens[0]);
        } else if (section == "PROCESS_COMPONENTS") {
            for (const auto &token : tokens)
                if (token.startsWith("config=", Qt::CaseInsensitive)) addInput(token.mid(7));
        }
    }
    // This intentionally covers declared built-in references, not arbitrary
    // plugin arguments or nested component/datasource dependency graphs.
    return references;
}

std::filesystem::path nativeRunPath(const QString &path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toUtf8().constData());
#endif
}

bool sameRunFile(const QString &left, const QString &right)
{
    if (left.isEmpty() || right.isEmpty()) return false;
    std::error_code error;
    if (std::filesystem::equivalent(nativeRunPath(left), nativeRunPath(right), error)) return true;
    const auto a = std::filesystem::weakly_canonical(nativeRunPath(left), error);
    if (!error) {
        const auto b = std::filesystem::weakly_canonical(nativeRunPath(right), error);
        if (!error && a == b) return true;
    }
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    return QDir::cleanPath(QFileInfo(left).absoluteFilePath()).compare(
        QDir::cleanPath(QFileInfo(right).absoluteFilePath()), sensitivity) == 0;
}
} // namespace

QString SimulationRunner::parseTwoDOption(const QString &inpPath, const QString &key)
{
    QFile file(inpPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    bool inOptions = false;
    QString value;
    while (!file.atEnd()) {
        const QString line = withoutInpComment(QString::fromUtf8(file.readLine()));
        if (line.isEmpty()) continue;
        if (line.startsWith('[') && line.endsWith(']')) {
            inOptions = line.mid(1, line.size() - 2).trimmed().compare(
                QStringLiteral("2D_OPTIONS"), Qt::CaseInsensitive) == 0;
            continue;
        }
        if (!inOptions) continue;
        const auto tokens = optionTokens(line);
        if (tokens.size() >= 2 && tokens.first().compare(key, Qt::CaseInsensitive) == 0)
            value = tokens[1]; // The engine applies the last option assignment.
    }
    return value;
}

QString SimulationRunner::parseTwoDOutputFile(const QString &inpPath)
{
    const QString value = parseTwoDOption(inpPath, QStringLiteral("OUTPUT_FILE"));
    if (value.isEmpty()) return {};
    QFileInfo fi(value);
    if (fi.isAbsolute()) return value;
    return QFileInfo(inpPath).absoluteDir().absoluteFilePath(value);
}

QStringList SimulationRunner::runOutputPaths(const QString &inp, const QString &rpt, const QString &out)
{
    const QFileInfo report(rpt);
    QStringList paths{QFileInfo(rpt).absoluteFilePath(), QFileInfo(out).absoluteFilePath(),
        report.absoluteDir().filePath(report.completeBaseName() + QStringLiteral(".runlog.txt"))};
    const QString h5 = parseTwoDOutputFile(inp);
    if (!h5.isEmpty()) paths.append(QFileInfo(h5).absoluteFilePath());
    paths.append(knownRunReferences(inp).outputs);
    return paths;
}

bool SimulationRunner::sameFilePath(const QString &left, const QString &right)
{
    return sameRunFile(left, right);
}

bool SimulationRunner::validateRunPaths(const QString &inp, const QString &rpt, const QString &out,
                                        const QStringList &protectedInputs, const QStringList &activeOutputs,
                                        QString *error, const QStringList &additionalOutputs)
{
    if (error) error->clear();
    const auto fail = [&](const QString &why) { if (error) *error = why; return false; };
    if (inp.isEmpty() || rpt.isEmpty() || out.isEmpty())
        return fail(tr("Run input, report and output file paths must be specified."));
    QStringList inputs = protectedInputs;
    inputs.append(inp);
    inputs.append(knownRunReferences(inp).inputs);
    auto outputs = runOutputPaths(inp, rpt, out);
    outputs.append(additionalOutputs);
    for (int i = 0; i < outputs.size(); ++i) {
        const auto &path = outputs[i];
        const QFileInfo info(path);
        if ((info.isSymLink() && !info.exists()) || (info.exists() && !info.isFile()))
            return fail(tr("Run output path is not a regular file: %1").arg(path));
        for (const auto &input : inputs)
            if (sameRunFile(path, input))
                return fail(tr("Run output file %1 would overwrite protected input %2. Choose a different output path.").arg(path, input));
        for (int j = 0; j < i; ++j)
            if (sameRunFile(path, outputs[j]))
                return fail(tr("Run output file paths %1 and %2 refer to the same file. Choose separate destinations.").arg(path, outputs[j]));
        for (const auto &active : activeOutputs)
            if (sameRunFile(path, active))
                return fail(tr("Run output file %1 is already in use by another simulation (%2). Stop that run or choose another path.").arg(path, active));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Internal result type returned from the worker lambda
// ---------------------------------------------------------------------------

struct SimulationResult {
    bool    success        = false;
    int     errorCode      = 0;
    QString errorMessage;
    double  runoffErrFrac  = 0.0;
    double  routingErrFrac = 0.0;
    // Defaulted so brace-init error returns report "no 2D value".
    double  twoDErrFrac = qQNaN();
    /// Where the run was when it failed ("open", "initialize", "start",
    /// "step at <sim time>", "end"); empty on success.
    QString phase;
};

Q_DECLARE_METATYPE(SimulationResult)

namespace {

// A failed SimulationResult for an exception that escaped the worker body.
SimulationResult exceptionResult(const QString &phase, const QString &what, RunLog *log)
{
    SimulationResult r;
    r.success      = false;
    r.errorCode    = SWMM_ERR_INTERNAL;
    r.phase        = phase;
    r.errorMessage = QCoreApplication::translate(
        "SimulationRunner", "Simulation worker threw during %1: %2")
        .arg(phase.isEmpty() ? QStringLiteral("run") : phase, what);
    if (log) log->line(QStringLiteral("EXCEPTION phase=%1 %2").arg(phase, what));
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SimulationRunner::SimulationRunner(int jobId,
                                   const QString &instanceName,
                                   const QString &inpPath,
                                   const QString &rptPath,
                                   const QString &outPath,
                                   const QString &engineVersion,
                                   QObject *parent)
    : QObject(parent)
    , m_jobId(jobId)
    , m_instanceName(instanceName)
    , m_inpPath(inpPath)
    , m_rptPath(rptPath)
    , m_outPath(outPath)
    , m_engineVersion(engineVersion)
{
    // Slice CF.MVP — explicit metatype registration so the new twoD*
    // signals carrying these vector types reliably cross the worker→GUI
    // thread boundary via queued connection. Qt 6 typically auto-registers
    // QList<T> for primitive T, but registering here is cheap insurance
    // against a silently dropped signal.
    static const bool s_metatypesRegistered = []() {
        qRegisterMetaType<QVector<float>>("QVector<float>");
        qRegisterMetaType<QVector<int>>("QVector<int>");
        qRegisterMetaType<QVector<double>>("QVector<double>");
        return true;
    }();
    (void)s_metatypesRegistered;
}

SimulationRunner::~SimulationRunner()
{
    // Keep callback receiver and atomics alive until the worker no longer
    // uses them. Cancellation cannot interrupt an engine/library call already
    // in progress, so shutdown may wait for that call to return.
    cancel();
    m_workerFuture.waitForFinished();
}

namespace {
// The engine's step loop must NOT share the GLOBAL QThreadPool with the
// per-tick map-render, contour and .out-rescan jobs the GUI queues while a
// run streams: on a saturated pool the engine queued behind them and they
// behind the engine (a run on a 10-core Mac spent its time waiting on
// render jobs). A private pool; a few runs may still overlap.
QThreadPool *enginePool()
{
    static QThreadPool pool;
    static const bool initialised = []() {
        pool.setMaxThreadCount(4);
        return true;
    }();
    Q_UNUSED(initialised)
    return &pool;
}
} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void SimulationRunner::start()
{
    if (m_started) return;
    m_started = true;
    if (!m_inpPath.isEmpty()) m_inpPath = QFileInfo(m_inpPath).absoluteFilePath();
    if (!m_rptPath.isEmpty()) m_rptPath = QFileInfo(m_rptPath).absoluteFilePath();
    if (!m_outPath.isEmpty()) m_outPath = QFileInfo(m_outPath).absoluteFilePath();
    m_outputPaths = runOutputPaths(m_inpPath, m_rptPath, m_outPath);
    emit started(m_jobId);
    QString pathError;
    if (!validateRunPaths(m_inpPath, m_rptPath, m_outPath, {}, {}, &pathError)) {
        QMetaObject::invokeMethod(this, [this, pathError] {
            emit finished(m_jobId, false, SWMM_ERR_BADPARAM, pathError, 0, 0, qQNaN());
        }, Qt::QueuedConnection);
        return;
    }
    if (!m_engineVersion.startsWith(QLatin1String("5."))
        && !twoDOutputAvailable(parseTwoDOutputFile(m_inpPath),pathError)) {
        QMetaObject::invokeMethod(this, [this,pathError] {
            emit finished(m_jobId,false,SWMM_ERR_IO,pathError,0,0,qQNaN());
        },Qt::QueuedConnection);
        return;
    }

    // Capture everything the lambda needs by value; the runner pointer is
    // passed as user_data to the C callbacks (safe because the runner lives
    // until after finished() fires and the caller calls deleteLater()).
    // Capture absolute paths before dispatch. Current-engine external file
    // slots resolve against the INP directory; legacy workers get their own cwd.
    const QByteArray inp = QFileInfo(m_inpPath).absoluteFilePath().toUtf8();
    const QByteArray rpt = QFileInfo(m_rptPath).absoluteFilePath().toUtf8();
    const QByteArray out = QFileInfo(m_outPath).absoluteFilePath().toUtf8();
    SimulationRunner *rawSelf = this;

    // Shared with the worker: the phase it is in (read by the exception
    // guard), the per-run log, and the test-only fault spec.
    auto phase  = std::make_shared<QString>();
    auto runLog = std::make_shared<RunLog>(m_rptPath);
    TestFault fault;
    {
        const QString spec = qEnvironmentVariable("SWMMVIS_TEST_FAULT");
        const int c = spec.indexOf(QLatin1Char(':'));
        if (c > 0) { fault.phase = spec.left(c); fault.kind = spec.mid(c + 1); }
    }
    runLog->line(QStringLiteral("run %1 (engine %2)").arg(m_inpPath, m_engineVersion));

    auto *watcher = new QFutureWatcher<SimulationResult>(this);

    connect(watcher, &QFutureWatcher<SimulationResult>::finished, this,
            [this, watcher]() {
                // result() rethrows anything the worker let escape — that
                // would land on the GUI thread inside a signal emission and
                // terminate the application. The worker body is guarded
                // below, so this is belt-and-braces.
                SimulationResult res;
                try {
                    res = watcher->result();
                } catch (const std::exception &e) {
                    res.errorCode    = SWMM_ERR_INTERNAL;
                    res.errorMessage = tr("Simulation worker failed: %1")
                                           .arg(QString::fromUtf8(e.what()));
                } catch (...) {
                    res.errorCode    = SWMM_ERR_INTERNAL;
                    res.errorMessage = tr("Simulation worker failed with a "
                                          "non-standard exception");
                }
                watcher->deleteLater();
                emit finished(m_jobId, res.success, res.errorCode,
                              res.errorMessage, res.runoffErrFrac, res.routingErrFrac,
                              res.twoDErrFrac);
            });

    // Snapshot the progress-tick interval on the GUI thread so the
    // worker never touches the singleton / QSettings from off-thread.
    const int tickIntervalMs = PreferencesManager::instance()->progressTickMs();
    const QString engineVersion = m_engineVersion;

    auto body = [inp, rpt, out, rawSelf, tickIntervalMs, engineVersion,
                 phase, runLog, fault]() -> SimulationResult {
            // Do not change process cwd: independent simulations and GUI
            // file operations share it. Current-engine file slots are anchored
            // by PostParseResolver; legacy cwd is set on its child below.

            // Use legacy worker for 5.x versions, refactored engine for 6.0.0+
            const bool useLegacy = engineVersion.startsWith("5.");

            if (!useLegacy) {
                // ===== REFACTORED ENGINE PATH =====
            // Owns the handle for the whole body: every early return and any
            // exception (the injected bad_alloc below included) closes and
            // destroys it. A throw between create and the explicit destroy
            // used to leak the parsed model and, for a 2D run, the whole
            // mesh + solver state. close() is safe in every engine state.
            struct EngineGuard {
                SWMM_Engine eng = swmm_engine_create();
                ~EngineGuard()
                {
                    if (!eng) return;
                    swmm_engine_close(eng);
                    swmm_engine_destroy(eng);
                }
            } engineGuard;
            SWMM_Engine eng = engineGuard.eng;

            // Specific failure text. The engine records the actual cause
            // ("ERROR 209: ...", "USE HOTSTART: ...") retrievable via
            // swmm_get_last_error_msg; swmm_error_message(code) is only the
            // generic category ("Input file parse error") — same reason the
            // end-of-run capture below reads the engine message first. Must
            // be read BEFORE close/destroy.
            auto engineFailureText = [](SWMM_Engine e, int code) -> QString {
                QString msg =
                    QString::fromUtf8(swmm_get_last_error_msg(e)).trimmed();
                if (msg.isEmpty())
                    msg = QString::fromUtf8(swmm_error_message(code));
                return msg;
            };

            // Open
            *phase = QStringLiteral("open");
            runLog->line(QStringLiteral("open"));
            int rc = swmm_engine_open(eng,
                                      inp.constData(),
                                      rpt.constData(),
                                      out.constData(),
                                      nullptr);
            if (rc != SWMM_OK) {
                QString msg = engineFailureText(eng, rc);
                // A failed parse usually records several errors — surface
                // them all in the log, not just the first.
                const int nErr = swmm_get_error_count(eng);
                for (int i = 0; i < nErr; ++i) {
                    const QString e =
                        QString::fromUtf8(swmm_get_error_at(eng, i)).trimmed();
                    if (!e.isEmpty() && e != msg)
                        msg += QLatin1Char('\n') + e;
                }
                return {false, rc, msg, 0.0, 0.0};   // engineGuard closes + destroys
            }

            // Initialize
            *phase = QStringLiteral("initialize");
            runLog->line(QStringLiteral("initialize"));
            rc = swmm_engine_initialize(eng);
            if (rc != SWMM_OK) {
                const QString msg = engineFailureText(eng, rc);
                return {false, rc, msg, 0.0, 0.0};   // engineGuard closes + destroys
            }

            // Register the warning callback only. The engine's
            // emit_progress() is invoked once (during initialize) and not
            // from the step loop, so a registered progress callback never
            // fires during the run — progress is polled inline below.
            swmm_set_warning_callback(eng, &SimulationRunner::warningCallback, rawSelf);

            // ── Slice CF.MVP — 2D mesh hand-off to the GUI ─────────────────
            // After successful initialize, the surface_router_ mesh is fully
            // built. Query it once and ship the geometry to the GUI thread
            // via twoDInitialized; the GUI builds an EngineMesh2DSource and
            // attaches a SWMM2DResultsLayer. Also re-scan the .inp for
            // [2D_OPTIONS] OUTPUT_FILE so the GUI knows where the engine
            // will write its CF/UGRID HDF5 (used at finished for the
            // post-run scrub source).
            int twoD_active = 0;
            swmm_2d_is_active(eng, &twoD_active);
            int twoD_n_tri  = 0;   // CELL count (triangles + quads; historical name)
            int twoD_n_vert = 0;
            // Engine bulk edge arrays are [cell*stride + e], stride 3 for an
            // all-triangle mesh and 4 once any quad exists
            // (swmm_2d_edge_stride). Everything shipped to the GUI is
            // repacked to mesh::kEdgeStride (4) so downstream uses ONE layout
            // (mesh::edgeSlot). Bulk buffers pulled from the engine are sized
            // twoD_n_tri * twoD_edge_stride.
            int twoD_edge_stride = 3;
            if (twoD_active) {
                if (swmm_2d_cell_count(eng, &twoD_n_tri) != SWMM_OK)
                    swmm_2d_triangle_count(eng, &twoD_n_tri);   // older engine
                swmm_2d_vertex_count(eng, &twoD_n_vert);
                if (swmm_2d_edge_stride(eng, &twoD_edge_stride) != SWMM_OK
                    || (twoD_edge_stride != 3 && twoD_edge_stride != mesh::kEdgeStride))
                    twoD_edge_stride = 3;
                if (twoD_n_tri > 0 && twoD_n_vert > 0) {
                    QVector<double> vx(twoD_n_vert), vy(twoD_n_vert),
                                    vz(twoD_n_vert);
                    swmm_2d_vertex_get_xyz_bulk(eng, vx.data(), vy.data(), vz.data());
                    // Cell connectivity, flat [v0,v1,v2,v3] per cell with
                    // v3 = -1 for a triangle (mixed tri/quad meshes).
                    QVector<int> cellFlat(twoD_n_tri * 4, -1);
                    for (int t = 0; t < twoD_n_tri; ++t) {
                        int v[4] = {-1, -1, -1, -1};
                        int nv = 0;
                        if (swmm_2d_cell_get_vertices(eng, t, v, &nv) != SWMM_OK) {
                            // Older engine without the cell API: triangles only.
                            swmm_2d_triangle_get_vertices(eng, t, &v[0], &v[1], &v[2]);
                            v[3] = -1;
                            nv = 3;
                        }
                        cellFlat[t * 4 + 0] = v[0];
                        cellFlat[t * 4 + 1] = v[1];
                        cellFlat[t * 4 + 2] = v[2];
                        cellFlat[t * 4 + 3] = (nv >= 4) ? v[3] : -1;
                    }
                    const QString h5Path = parseTwoDOutputFile(QString::fromUtf8(inp));
                    const int jobId = rawSelf->m_jobId;
                    QMetaObject::invokeMethod(rawSelf,
                        [rawSelf, jobId, h5Path,
                         vx = std::move(vx), vy = std::move(vy),
                         vz = std::move(vz),
                         cellFlat = std::move(cellFlat)]() mutable {
                            emit rawSelf->twoDInitialized(
                                jobId, h5Path, vx, vy, vz, cellFlat);
                        },
                        Qt::QueuedConnection);

                    // CF.2.4 — ship time-invariant edge geometry so the GUI
                    // can reconstruct cell-centred velocity from per-tick
                    // flux without re-deriving lengths/normals from vertex
                    // coords. Engine returns doubles; convert to float for
                    // the wire (RT0 doesn't need double precision) and
                    // repack to stride mesh::kEdgeStride.
                    const int nEng = twoD_n_tri * twoD_edge_stride;
                    const int n3   = mesh::edgeSlotCount(twoD_n_tri);
                    std::vector<double> rawLen(nEng), rawNx(nEng), rawNy(nEng);
                    if (swmm_2d_edge_get_geometry_bulk(
                            eng, rawLen.data(), rawNx.data(), rawNy.data()) == SWMM_OK)
                    {
                        QVector<float> qLen(n3, 0.0f), qNx(n3, 0.0f), qNy(n3, 0.0f);
                        for (int c = 0; c < twoD_n_tri; ++c) {
                            for (int e = 0; e < twoD_edge_stride; ++e) {
                                const int src = c * twoD_edge_stride + e;
                                const int dst = mesh::edgeSlot(c, e);
                                qLen[dst] = static_cast<float>(rawLen[src]);
                                qNx[dst]  = static_cast<float>(rawNx[src]);
                                qNy[dst]  = static_cast<float>(rawNy[src]);
                            }
                        }
                        QMetaObject::invokeMethod(rawSelf,
                            [rawSelf, jobId,
                             qLen = std::move(qLen),
                             qNx  = std::move(qNx),
                             qNy  = std::move(qNy)]() mutable {
                                emit rawSelf->twoDEdgeGeometryAvailable(
                                    jobId, qLen, qNx, qNy);
                            },
                            Qt::QueuedConnection);
                    }
                }
            }

            // Start
            *phase = QStringLiteral("start");
            runLog->line(QStringLiteral("start"));
            rc = swmm_engine_start(eng, 1 /* save_results */);
            if (rc != SWMM_OK) {
                const QString msg = engineFailureText(eng, rc);
                return {false, rc, msg, 0.0, 0.0};   // engineGuard closes + destroys
            }

            // Step loop — polls m_cancel and m_paused on every iteration.
            // Pause parks in a short sleep rather than busy-waiting; the
            // step loop resumes as soon as setPaused(false) clears the
            // flag. Cancel breaks out so the engine still runs its
            // end/report/close flush below.
            //
            // Progress reporting is done HERE by polling the engine after
            // each step rather than via the C progress callback. The engine's
            // emit_progress() is only invoked once at initialize time, so the
            // registered callback never fires during the run. Polling a
            // handful of cheap getters per step and posting the result back
            // via queued invokeMethod is both simpler and more reliable, and
            // the rate limit keeps the GUI event queue from flooding.
            // Engine time unit quirk:
            //   swmm_get_start_time / swmm_get_end_time  → OADate (days since 1899-12-30)
            //   swmm_get_current_time                     → SECONDS since sim start
            // So the simulation window needs the OADate helper but the
            // per-tick "current" is seconds → added to the start QDateTime.
            double startOA = 0.0, endOA = 0.0;
            swmm_get_start_time(eng, &startOA);
            swmm_get_end_time  (eng, &endOA);
            const QDateTime simStart     = oaDateToQDateTime(startOA);
            const QDateTime simEnd       = oaDateToQDateTime(endOA);
            const double    simSpanDays  = endOA - startOA;
            qDebug() << "[sim-dates] startOA=" << startOA
                     << " endOA=" << endOA
                     << " span(days)=" << simSpanDays
                     << " →" << simStart << simEnd;

            if (simStart.isValid() && simEnd.isValid()) {
                const int jobId = rawSelf->m_jobId;
                QMetaObject::invokeMethod(rawSelf,
                    [rawSelf, jobId, simStart, simEnd]() {
                        emit rawSelf->simulationDatesKnown(
                            jobId, simStart, simEnd);
                    },
                    Qt::QueuedConnection);
            }

            // One-shot initial tick at 0 % so the row populates before the
            // first engine step (which on large models can take seconds).
            {
                const int jobId = rawSelf->m_jobId;
                const QDateTime initial = simStart;
                const double twoDErr0 = twoD_active ? 0.0 : qQNaN();
                QMetaObject::invokeMethod(rawSelf,
                    [rawSelf, jobId, initial, twoDErr0]() {
                        emit rawSelf->progressChanged(jobId, 0.0, initial, 0.0, 0.0, 0.0,
                                                      twoDErr0);
                    },
                    Qt::QueuedConnection);
                // The 2D backend / closure / LTS_TIERS are known as soon as
                // the solver is chosen at start — show them before the first
                // step, which on a large mesh can take a while.
                if (twoD_active) emitTwoDSolverStats(rawSelf, jobId, eng);
            }

            // NOTE on units: swmm_engine_step()'s out-parameter is the
            // CUMULATIVE elapsed time in DAYS (SWMMEngine.cpp:
            // *elapsed_time = current_time / SEC_PER_DAY), not a per-step
            // delta and not seconds. It is used here only to detect the end
            // of the run (elapsed <= 0.0); every quantity reported to the GUI
            // comes from swmm_get_current_time(), which is seconds since the
            // simulation start.
            double elapsed = 0.0;
            qint64 stepCount = 0;
            QString lastGroundwaterCaptureError;
            qint64 skipped2DTicks = 0;   // bundles dropped because the GUI thread was behind
            QElapsedTimer tickTimer;
            tickTimer.start();
            // Rate-limit GUI emissions to `tickIntervalMs` (user pref,
            // default 1 Hz — Slice V). Small models can step thousands
            // of times per second; emitting every step would starve
            // the GUI event loop.
            const qint64 kTickIntervalMs = tickIntervalMs;
            qint64 lastTickMs = -kTickIntervalMs; // fire immediately on first step
            *phase = QStringLiteral("step");
            runLog->line(QStringLiteral("step loop"));
            // A step failure is captured HERE, with the engine's specific
            // message and error list, before end()/report() can disturb them.
            int     stepFailCode = SWMM_OK;
            QString stepFailMsg, stepFailPhase;
            while (!rawSelf->m_cancel.load()) {
                if (rawSelf->m_paused.load()) {
                    QThread::msleep(50);
                    continue;
                }
                rc = swmm_engine_step(eng, &elapsed);
                if (fault.at("step") && stepCount == 2) {
                    if (fault.kind == QLatin1String("bad_alloc")) throw std::bad_alloc();
                    if (fault.kind == QLatin1String("numerical")) rc = SWMM_ERR_NUMERICAL;
                }
                if (rc != SWMM_OK || elapsed <= 0.0)
                    break;

                ++stepCount;

                const qint64 nowMs = tickTimer.elapsed();
                if (nowMs - lastTickMs < kTickIntervalMs)
                    continue;
                lastTickMs = nowMs;

                double curTSec = 0.0;
                swmm_get_current_time(eng, &curTSec);  // seconds since sim start
                double frac = 0.0;
                if (simSpanDays > 0.0) {
                    frac = (curTSec / 86400.0) / simSpanDays;
                    if (frac < 0.0) frac = 0.0;
                    if (frac > 1.0) frac = 1.0;
                }
                double runoffErr = 0.0, routingErr = 0.0;
                swmm_get_runoff_continuity_error (eng, &runoffErr);
                swmm_get_routing_continuity_error(eng, &routingErr);
                double twoDErr = qQNaN();
                if (twoD_active)
                    swmm_2d_get_continuity_error(eng, &twoDErr);

                // Running average routing step (seconds) — same formula the
                // out-of-process worker path uses below. It was previously
                // summing swmm_engine_step()'s CUMULATIVE elapsed DAYS, which
                // grows as dt*(N+1)/(2*86400): on a 10 s routing step it reads
                // ~0.01 around step 200 and creeps up from there, so a healthy
                // run looked permanently stalled.
                const double avgTs = stepCount > 0 ? curTSec / double(stepCount) : 0.0;
                const int jobId = rawSelf->m_jobId;
                const QDateTime curQDT = simStart.isValid()
                    ? simStart.addMSecs(static_cast<qint64>(curTSec * 1000.0))
                    : QDateTime();
                QMetaObject::invokeMethod(rawSelf,
                    [rawSelf, jobId, frac, curQDT, runoffErr, routingErr, avgTs, twoDErr]() {
                        emit rawSelf->progressChanged(jobId, frac, curQDT,
                                                      runoffErr, routingErr, avgTs,
                                                      twoDErr);
                    },
                    Qt::QueuedConnection);
                // Marcher substeps + LTS tier occupancy, same cadence.
                if (twoD_active) emitTwoDSolverStats(rawSelf, jobId, eng);

                // ── Slice CF.MVP — per-tick 2D depth slice ─────────────────
                // Rate-limited by the surrounding kTickIntervalMs gate. Pulls
                // the latest per-triangle depth from the in-process engine
                // and ships it to the GUI thread, where SWMM2DResultsLayer
                // recolours the mesh. Engine API returns doubles; we
                // downcast to float for the wire because mm-level depth
                // precision is plenty for colour mapping and the HDF5
                // reader produces float to match.
                // Back-pressure: the four queued payloads below are only
                // rate-limited on THIS side. If the GUI thread takes longer
                // than a tick to digest a bundle, the event queue grew
                // without bound (each entry pinning a full-mesh copy). Skip
                // the bundle while two are still queued; progress went out
                // above regardless, and the next tick catches up.
                if (twoD_active && twoD_n_tri > 0
                    && rawSelf->m_pending2DTicks.load() >= 2) {
                    ++skipped2DTicks;
                    continue;
                }
                if (twoD_active && twoD_n_tri > 0) {
                    rawSelf->m_pending2DTicks.fetch_add(1);
                    std::vector<double> raw(twoD_n_tri);
                    swmm_2d_get_depths_bulk(eng, raw.data());
                    QVector<float> depths(twoD_n_tri);
                    for (int t = 0; t < twoD_n_tri; ++t)
                        depths[t] = static_cast<float>(raw[t]);
                    QMetaObject::invokeMethod(rawSelf,
                        [rawSelf, jobId, depths = std::move(depths),
                         curQDT, curTSec]() mutable {
                            emit rawSelf->twoDDepthsAvailable(
                                jobId, depths, curQDT, curTSec);
                        },
                        Qt::QueuedConnection);

                    // CF.2.4 — per-tick signed edge flux. Paired with the
                    // depth slice via the matching elapsedSec on the GUI side
                    // so a single tick maps to a single history frame in
                    // EngineMesh2DSource regardless of queue ordering.
                    // Engine stride (3|4) in, mesh::kEdgeStride out.
                    const int nEng = twoD_n_tri * twoD_edge_stride;
                    std::vector<double> rawFlux(nEng);
                    if (swmm_2d_get_edge_flux_bulk(eng, rawFlux.data()) == SWMM_OK)
                    {
                        QVector<float> flux(mesh::edgeSlotCount(twoD_n_tri), 0.0f);
                        for (int c = 0; c < twoD_n_tri; ++c)
                            for (int e = 0; e < twoD_edge_stride; ++e)
                                flux[mesh::edgeSlot(c, e)] =
                                    static_cast<float>(rawFlux[c * twoD_edge_stride + e]);
                        QMetaObject::invokeMethod(rawSelf,
                            [rawSelf, jobId, flux = std::move(flux),
                             curQDT, curTSec]() mutable {
                                emit rawSelf->twoDFluxAvailable(
                                    jobId, flux, curQDT, curTSec);
                            },
                            Qt::QueuedConnection);
                    }

                    // Per-tick rainfall intensity + cumulative volume per
                    // cell. Both calls must succeed (older engines lack
                    // them) or nothing is emitted and the GUI keeps the
                    // Rainfall entries greyed out until the HDF5 swap-in.
                    {
                        std::vector<double> rawRain(twoD_n_tri), rawCum(twoD_n_tri);
                        if (swmm_2d_get_rainfall_bulk(eng, rawRain.data()) == SWMM_OK
                            && swmm_2d_get_rain_volume_bulk(eng, rawCum.data()) == SWMM_OK)
                        {
                            QVector<float> rain(twoD_n_tri), cum(twoD_n_tri);
                            for (int t = 0; t < twoD_n_tri; ++t) {
                                rain[t] = static_cast<float>(rawRain[t]);
                                cum[t]  = static_cast<float>(rawCum[t]);
                            }
                            QMetaObject::invokeMethod(rawSelf,
                                [rawSelf, jobId, rain = std::move(rain),
                                 cum = std::move(cum), curQDT, curTSec]() mutable {
                                    emit rawSelf->twoDRainfallAvailable(
                                        jobId, rain, cum, curQDT, curTSec);
                                },
                                Qt::QueuedConnection);
                        }
                    }

                    // Per-tick water-surface elevation per cell, so the
                    // mid-run export carries the solver's head (same SWMM_OK
                    // gating as rainfall).
                    {
                        std::vector<double> rawHead(twoD_n_tri);
                        if (swmm_2d_get_heads_bulk(eng, rawHead.data()) == SWMM_OK)
                        {
                            QVector<float> heads(twoD_n_tri);
                            for (int t = 0; t < twoD_n_tri; ++t)
                                heads[t] = static_cast<float>(rawHead[t]);
                            QMetaObject::invokeMethod(rawSelf,
                                [rawSelf, jobId, heads = std::move(heads),
                                 curQDT, curTSec]() mutable {
                                    emit rawSelf->twoDHeadsAvailable(
                                        jobId, heads, curQDT, curTSec);
                                },
                                Qt::QueuedConnection);
                        }
                    }

                    // Per-tick cumulative maxima (the live ENVELOPES) — the
                    // engine updates them on its refresh cadence, so the
                    // mid-run export's "max so far" includes sub-tick peaks
                    // no retained frame saw. Both calls must succeed.
                    {
                        std::vector<double> rawMaxD(twoD_n_tri), rawMaxV(twoD_n_tri);
                        if (swmm_2d_get_stat_max_depths(eng, rawMaxD.data()) == SWMM_OK
                            && swmm_2d_get_stat_max_velocities(eng, rawMaxV.data()) == SWMM_OK)
                        {
                            QVector<float> maxD(twoD_n_tri), maxV(twoD_n_tri);
                            for (int t = 0; t < twoD_n_tri; ++t) {
                                maxD[t] = static_cast<float>(rawMaxD[t]);
                                maxV[t] = static_cast<float>(rawMaxV[t]);
                            }
                            QMetaObject::invokeMethod(rawSelf,
                                [rawSelf, jobId, maxD = std::move(maxD),
                                 maxV = std::move(maxV)]() mutable {
                                    emit rawSelf->twoDEnvelopesAvailable(
                                        jobId, maxD, maxV);
                                },
                                Qt::QueuedConnection);
                        }
                    }

                    // Per-tick SIGNED vertex render depths (wet-masked
                    // η_v − z_v) — drives the smooth (Gouraud) depth fill +
                    // contour interpolation. Replaces the solver vertex-head
                    // field, whose stencil blends DRY-cell bed elevations into
                    // shoreline vertices (water rendered climbing adverse
                    // slopes/steps). Same SWMM_OK gating as the flux call: an
                    // engine without the API simply never emits, and the GUI
                    // falls back to its wet-only incident-cell reconstruction.
                    if (twoD_n_vert > 0) {
                        QVector<double> vdepths(twoD_n_vert);
                        if (swmm_2d_vertex_get_render_depths_bulk(
                                eng, vdepths.data()) == SWMM_OK)
                        {
                            QMetaObject::invokeMethod(rawSelf,
                                [rawSelf, jobId, vdepths = std::move(vdepths),
                                 curQDT, curTSec]() mutable {
                                    emit rawSelf->twoDVertexDepthsAvailable(
                                        jobId, vdepths, curQDT, curTSec);
                                },
                                Qt::QueuedConnection);
                        }
                    }
                    // Read while the worker owns the initialized engine; the
                    // GUI receives only immutable values, never a live handle.
                    QString groundwaterError;
                    auto groundwater = openswmmvis::io::captureGroundwaterVariables(
                        eng, twoD_n_tri, &groundwaterError);
                    if (groundwater) {
                        QMetaObject::invokeMethod(rawSelf,
                            [rawSelf, jobId, groundwater = std::move(groundwater), curQDT, curTSec]() {
                                emit rawSelf->twoDVariablesAvailable(jobId, groundwater, curQDT, curTSec);
                            }, Qt::QueuedConnection);
                    }
                    if (!groundwaterError.isEmpty() && groundwaterError != lastGroundwaterCaptureError) {
                        QMetaObject::invokeMethod(rawSelf,
                            [rawSelf, jobId, groundwaterError]() {
                                emit rawSelf->warningReceived(jobId, 0, groundwaterError);
                            }, Qt::QueuedConnection);
                    }
                    lastGroundwaterCaptureError = groundwaterError;

                    // Trailing marker: same receiver, so it is delivered
                    // after the bundle's payloads (FIFO) — i.e. once the GUI
                    // thread has run every slot for this tick.
                    QMetaObject::invokeMethod(rawSelf,
                        [rawSelf]() { rawSelf->m_pending2DTicks.fetch_sub(1); },
                        Qt::QueuedConnection);
                }
            }
            if (skipped2DTicks > 0)
                runLog->line(QStringLiteral("2D ticks skipped (GUI busy): %1")
                                 .arg(skipped2DTicks));

            if (rc != SWMM_OK) {
                stepFailCode = rc;
                const double simSec = elapsed * 86400.0;
                stepFailPhase = QStringLiteral("step at %1").arg(
                    simStart.isValid()
                        ? simStart.addMSecs(qint64(simSec * 1000.0)).toString(Qt::ISODate)
                        : QStringLiteral("%1 s").arg(simSec, 0, 'f', 0));
                QString msg = engineFailureText(eng, rc);
                // The engine's error list carries the specific cause (a
                // diverging node, a plugin failure …) — surface all of it.
                const int nErr = swmm_get_error_count(eng);
                for (int i = 0; i < nErr; ++i) {
                    const QString e =
                        QString::fromUtf8(swmm_get_error_at(eng, i)).trimmed();
                    if (!e.isEmpty() && !msg.contains(e))
                        msg += QLatin1Char('\n') + e;
                }
                stepFailMsg = (rc == SWMM_ERR_NUMERICAL)
                    ? QStringLiteral("Routing diverged (%1): %2").arg(stepFailPhase, msg)
                    : QStringLiteral("%1 (%2)").arg(msg, stepFailPhase);
                *phase = stepFailPhase;
                runLog->line(QStringLiteral("step FAILED code=%1 %2").arg(rc).arg(stepFailMsg));
            }

            // Final 2D solver telemetry — a short run can finish before any
            // progress tick, and end() finalises the marcher's counters.
            if (twoD_active) emitTwoDSolverStats(rawSelf, rawSelf->m_jobId, eng);

            // End
            if (stepFailCode == SWMM_OK) *phase = QStringLiteral("end");
            runLog->line(QStringLiteral("end"));
            swmm_engine_end(eng);

            // Continuity errors (available after end)
            double runoffErr  = 0.0;
            double routingErr = 0.0;
            swmm_get_runoff_continuity_error (eng, &runoffErr);
            swmm_get_routing_continuity_error(eng, &routingErr);
            double twoDErr = qQNaN();
            if (twoD_active)
                swmm_2d_get_continuity_error(eng, &twoDErr);

            // Determine overall success. Capture the engine's own error
            // message (e.g. "ERROR 145: Drainage system has no acceptable
            // outlet nodes.") before destroy — swmm_error_message(code)
            // only yields the generic category text ("Input file parse
            // error"), which hides the actual cause from the user.
            const int lastErr = swmm_get_last_error(eng);
            const QString lastErrMsg =
                QString::fromUtf8(swmm_get_last_error_msg(eng)).trimmed();
            const bool cancelled = rawSelf->m_cancel.load();

            // Report + close. A cancelled run still writes the report —
            // swmm_engine_end has finalized the stats up to the stop point,
            // so the summary tables cover the simulated span, and closing
            // the report properly suppresses the plugin's "[Report
            // interrupted]" footer. (Skipping it made Cancel feel snappier
            // on big 2D models but threw away everything the run had
            // computed.)
            swmm_engine_report(eng);
            // close + destroy: engineGuard, at scope exit (after the return
            // value below is built — same order as the explicit calls were).

            if (cancelled)
                return {false, 0,
                        QStringLiteral("Cancelled — report written up to the "
                                       "stop point"),
                        runoffErr, routingErr, twoDErr};

            if (stepFailCode != SWMM_OK)
                return {false, stepFailCode, stepFailMsg, runoffErr, routingErr, twoDErr,
                        stepFailPhase};
            if (lastErr != SWMM_OK) {
                const QString msg = !lastErrMsg.isEmpty()
                    ? lastErrMsg
                    : QString::fromUtf8(swmm_error_message(lastErr));
                return {false, lastErr, msg, runoffErr, routingErr, twoDErr,
                        QStringLiteral("end")};
            }
            return {true, SWMM_OK, {}, runoffErr, routingErr, twoDErr};

            } else {
                // ===== LEGACY ENGINE WORKER PROCESS PATH =====
                // Spawn worker process for isolated legacy engine execution.
                // Each worker has its own global state, allowing true parallelism.

                const QString workerPath = findLegacyWorker(engineVersion);
                if (workerPath.isEmpty()) {
                    return {false, 1,
                            QStringLiteral("Legacy worker executable not found (openswmm-legacy-worker-%1)")
                                .arg(engineVersion),
                            0.0, 0.0};
                }

                QProcess worker;
                worker.setProgram(workerPath);
                worker.setWorkingDirectory(QFileInfo(QString::fromUtf8(inp)).absolutePath());
                // 4th arg: progress emit interval (ms) from the user's
                // progressTickMs preference, so the worker rate-limits by
                // wall-clock the same way the in-process path does (Gap 2).
                worker.setArguments({QString::fromUtf8(inp),
                                     QString::fromUtf8(rpt),
                                     QString::fromUtf8(out),
                                     QString::number(tickIntervalMs)});
                worker.start();

                if (!worker.waitForStarted(10000)) {
                    return {false, 1,
                            QStringLiteral("Failed to start legacy worker: %1")
                                .arg(workerPath), 0.0, 0.0};
                }

                // ── Incremental stdout drain ──────────────────────────────
                // The worker emits a JSON progress line every 10 steps and
                // fflushes it. If we only read stdout AFTER the process exits
                // (waitForFinished + readAll), a non-trivial run fills the OS
                // pipe buffer (~64 KB on macOS), the worker blocks on fflush
                // waiting for us to drain, and we block on waitForFinished
                // waiting for it to exit → deadlock, the run hangs forever at
                // 0 %. So drain continuously while it runs. waitForReadyRead()
                // works without an event loop (it select()s on the channel
                // fd), so it is safe on this thread-pool thread.
                int     lastErrorCode = 0;
                QString lastErrorMsg;
                double  runoffErrFrac  = 0.0;  // final continuity errors, filled
                double  routingErrFrac = 0.0;  // by the worker's "continuity" line
                double  startOA   = 0.0;
                double  endOA     = 0.0;
                double  spanDays  = 0.0;
                QDateTime simStart;
                QByteArray pending;     // accumulates partial trailing stdout line
                QByteArray stderrBuf;   // bounded capture of worker stderr (Gap 6)

                auto handleLine = [&](const QByteArray &rawLine) {
                    const QByteArray line = rawLine.trimmed();
                    if (line.isEmpty()) return;
                    const QJsonDocument doc = QJsonDocument::fromJson(line);
                    if (!doc.isObject()) return;  // ignore engine console noise
                    const QJsonObject obj  = doc.object();
                    const QString     type = obj.value("type").toString();
                    const int jobId = rawSelf->m_jobId;

                    if (type == "dates") {
                        startOA  = obj.value("start").toDouble();
                        endOA    = obj.value("end").toDouble();
                        spanDays = endOA - startOA;
                        simStart = oaDateToQDateTime(startOA);
                        const QDateTime simEnd = oaDateToQDateTime(endOA);
                        if (simStart.isValid() && simEnd.isValid()) {
                            QMetaObject::invokeMethod(rawSelf,
                                [rawSelf, jobId, simStart, simEnd]() {
                                    emit rawSelf->simulationDatesKnown(
                                        jobId, simStart, simEnd);
                                }, Qt::QueuedConnection);
                        }
                    } else if (type == "progress") {
                        const double elapsedDays = obj.value("elapsed").toDouble();
                        const int    stepCount   = obj.value("stepCount").toInt();
                        double frac = 0.0;
                        if (spanDays > 0.0) {
                            frac = elapsedDays / spanDays;
                            if (frac < 0.0) frac = 0.0;
                            if (frac > 1.0) frac = 1.0;
                        }
                        // Running average routing step (seconds). The worker
                        // reports cumulative elapsed days plus the step count.
                        const double avgTs = stepCount > 0
                            ? (elapsedDays * 86400.0) / double(stepCount)
                            : 0.0;
                        // Running (timestep-by-timestep) continuity errors,
                        // emitted by the worker via swmm_getRunningMassBalErr as
                        // fractions. Absent on older workers → default 0.0.
                        const double runoffErr  = obj.value("runoff").toDouble();
                        const double routingErr = obj.value("routing").toDouble();
                        const QDateTime curQDT = simStart.isValid()
                            ? simStart.addMSecs(
                                  static_cast<qint64>(elapsedDays * 86400.0 * 1000.0))
                            : QDateTime();
                        QMetaObject::invokeMethod(rawSelf,
                            [rawSelf, jobId, frac, curQDT, runoffErr, routingErr, avgTs]() {
                                emit rawSelf->progressChanged(jobId, frac, curQDT,
                                                              runoffErr, routingErr, avgTs,
                                                              qQNaN() /* legacy: no 2D */);
                            }, Qt::QueuedConnection);
                    } else if (type == "warning") {
                        const int     code = obj.value("code").toInt();
                        const QString msg  = obj.value("message").toString();
                        QMetaObject::invokeMethod(rawSelf,
                            [rawSelf, jobId, code, msg]() {
                                emit rawSelf->warningReceived(jobId, code, msg);
                            }, Qt::QueuedConnection);
                    } else if (type == "error") {
                        lastErrorCode = obj.value("code").toInt();
                        lastErrorMsg  = obj.value("message").toString();
                    } else if (type == "continuity") {
                        // Final mass-balance errors emitted by the worker after
                        // swmm_end(). Stored as fractions (0.001 = 0.1 %) and
                        // forwarded to finished() so the status model shows the
                        // same continuity columns the refactored engine fills.
                        runoffErrFrac  = obj.value("runoff").toDouble();
                        routingErrFrac = obj.value("routing").toDouble();
                    }
                };

                auto drain = [&]() {
                    pending += worker.readAllStandardOutput();
                    int nl;
                    while ((nl = pending.indexOf('\n')) >= 0) {
                        handleLine(pending.left(nl));
                        pending.remove(0, nl + 1);
                    }
                    // Capture stderr (engine console + worker "ERROR n: …" lines)
                    // continuously so failure diagnostics survive even when an
                    // "error" JSON line was also emitted. Bound to the last
                    // kStderrCapBytes to avoid unbounded growth on a chatty run.
                    stderrBuf += worker.readAllStandardError();
                    constexpr int kStderrCapBytes = 16 * 1024;
                    if (stderrBuf.size() > kStderrCapBytes)
                        stderrBuf = stderrBuf.right(kStderrCapBytes);
                };

                bool cancelled = false;
                while (worker.state() != QProcess::NotRunning) {
                    if (rawSelf->m_cancel.load()) {
                        worker.kill();
                        worker.waitForFinished(5000);
                        cancelled = true;
                        break;
                    }
                    // Wakes on new output or after the timeout (so the cancel
                    // flag is polled even during long quiet stretches).
                    worker.waitForReadyRead(200);
                    drain();
                }
                // Final drain — flush whatever arrived after the last wait and
                // the trailing line that had no newline.
                drain();
                if (!pending.trimmed().isEmpty())
                    handleLine(pending);

                if (cancelled)
                    return {false, 0, QStringLiteral("Cancelled"), 0.0, 0.0};

                const int exitCode = worker.exitCode();
                // A worker that emitted an {"type":"error"} line but still
                // exited 0 must NOT be reported as success — otherwise the
                // failure reason is silently discarded and the run "fails
                // without a message". Treat any seen error (message or
                // non-zero code) as a failure alongside the crash /
                // non-zero-exit cases.
                const bool crashed      = worker.exitStatus() == QProcess::CrashExit;
                const bool emittedError = !lastErrorMsg.isEmpty() || lastErrorCode != 0;
                if (crashed || exitCode != 0 || emittedError) {
                    QString msg = !lastErrorMsg.isEmpty()
                        ? lastErrorMsg
                        : QStringLiteral("Legacy worker exited with code %1").arg(exitCode);
                    // Append captured stderr context (if any) so the detail is
                    // not lost when an "error" JSON line was already seen. The
                    // finished() handler logs errorMessage at Error severity.
                    const QString errText = QString::fromUtf8(stderrBuf).trimmed();
                    if (!errText.isEmpty())
                        msg += QStringLiteral("\n\nWorker stderr:\n%1").arg(errText);
                    const int code = exitCode != 0 ? exitCode
                                   : (lastErrorCode != 0 ? lastErrorCode : -1);
                    return {false, code, msg, 0.0, 0.0};
                }

                return {true, 0, QString(), runoffErrFrac, routingErrFrac};
            }
        };

    const auto future = QtConcurrent::run(enginePool(),
        [body, phase, runLog]() -> SimulationResult {
            // Last line of defence for the run: nothing thrown by the worker
            // may escape the future. Report it as a failed run, with the
            // phase it was in, and write it to the run log.
            try {
                SimulationResult r = body();
                if (!r.success && r.phase.isEmpty()) r.phase = *phase;
                runLog->line(r.success
                    ? QStringLiteral("finished: success")
                    : QStringLiteral("finished: %1 code=%2 phase=%3 %4")
                          .arg(r.errorCode == 0 ? QStringLiteral("cancelled")
                                                : QStringLiteral("FAILED"))
                          .arg(r.errorCode).arg(r.phase, r.errorMessage));
                return r;
            } catch (const std::bad_alloc &) {
                return exceptionResult(*phase,
                    QStringLiteral("out of memory (std::bad_alloc)"), runLog.get());
            } catch (const std::exception &e) {
                return exceptionResult(*phase, QString::fromUtf8(e.what()), runLog.get());
            } catch (...) {
                return exceptionResult(*phase,
                    QStringLiteral("non-standard exception"), runLog.get());
            }
        });
    m_workerFuture = QFuture<void>(future);
    watcher->setFuture(future);
}

void SimulationRunner::cancel()
{
    m_cancel.store(true);
    // Ensure a paused loop wakes up to see the cancel flag.
    m_paused.store(false);
}

void SimulationRunner::setPaused(bool paused)
{
    m_paused.store(paused);
}

// ---------------------------------------------------------------------------
// Static C callbacks (worker thread → GUI thread via queued invoke)
// ---------------------------------------------------------------------------

void SimulationRunner::warningCallback(void* /*engine*/, int code,
                                        const char *msg, void *ud)
{
    auto *runner = static_cast<SimulationRunner *>(ud);
    const int jobId = runner->m_jobId;
    const QString message = QString::fromUtf8(msg ? msg : "");
    QMetaObject::invokeMethod(runner,
        [runner, jobId, code, message]() {
            emit runner->warningReceived(jobId, code, message);
        },
        Qt::QueuedConnection);
}
