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

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>
#include <QThread>
#include <QVector>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
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
 * @brief Find the openswmm-legacy-worker executable.
 *
 * Searches in:
 *  1. Same directory as the current executable
 *  2. {app_dir}/bin/{CONFIG}/ (debug/release subdirs)
 *  3. {app_dir}/../bin/{CONFIG}/
 *  4. System PATH
 *
 * @return Path to worker executable, or empty string if not found.
 */
QString findLegacyWorker()
{
    const QString workerName =
#ifdef Q_OS_WIN
        QStringLiteral("openswmm-legacy-worker.exe");
#else
        QStringLiteral("openswmm-legacy-worker");
#endif

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

    for (const QString &dir : searchPaths) {
        const QString candidate = QFileInfo(dir + "/" + workerName).absoluteFilePath();
        if (QFile::exists(candidate))
            return candidate;
    }

    // Fall back to PATH
    qWarning() << "Legacy worker executable not found — searched:" << searchPaths;
    return workerName;   // let QProcess try PATH; it will fail with a clear error
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public static helpers
// ---------------------------------------------------------------------------

QString SimulationRunner::parseTwoDOption(const QString &inpPath,
                                            const QString &key)
{
    QFile f(inpPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    bool inOptions = false;
    QString value;
    while (!f.atEnd()) {
        const QByteArray rawLine = f.readLine();
        QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty() || line.startsWith(';')) continue;
        if (line.startsWith('[') && line.endsWith(']')) {
            inOptions = (line.compare(QStringLiteral("[2D_OPTIONS]"),
                                       Qt::CaseInsensitive) == 0);
            continue;
        }
        if (!inOptions) continue;
        const QStringList tokens = line.split(QRegularExpression(R"(\s+)"),
                                               Qt::SkipEmptyParts);
        if (tokens.size() < 2) continue;
        if (tokens.first().compare(key, Qt::CaseInsensitive) == 0) {
            value = tokens.at(1);
            break;
        }
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

// ---------------------------------------------------------------------------
// Internal result type returned from the worker lambda
// ---------------------------------------------------------------------------

struct SimulationResult {
    bool    success;
    int     errorCode;
    QString errorMessage;
    double  runoffErrFrac;
    double  routingErrFrac;
    // Defaulted so brace-init error returns report "no 2D value".
    double  twoDErrFrac = qQNaN();
};

Q_DECLARE_METATYPE(SimulationResult)

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

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void SimulationRunner::start()
{
    emit started(m_jobId);

    // Capture everything the lambda needs by value; the runner pointer is
    // passed as user_data to the C callbacks (safe because the runner lives
    // until after finished() fires and the caller calls deleteLater()).
    const QByteArray inp = m_inpPath.toUtf8();
    const QByteArray rpt = m_rptPath.toUtf8();
    const QByteArray out = m_outPath.toUtf8();
    SimulationRunner *rawSelf = this;

    auto *watcher = new QFutureWatcher<SimulationResult>(this);

    connect(watcher, &QFutureWatcher<SimulationResult>::finished, this,
            [this, watcher]() {
                SimulationResult res = watcher->result();
                watcher->deleteLater();
                emit finished(m_jobId, res.success, res.errorCode,
                              res.errorMessage, res.runoffErrFrac, res.routingErrFrac,
                              res.twoDErrFrac);
            });

    // Snapshot the progress-tick interval on the GUI thread so the
    // worker never touches the singleton / QSettings from off-thread.
    const int tickIntervalMs = PreferencesManager::instance()->progressTickMs();
    const QString engineVersion = m_engineVersion;

    watcher->setFuture(
        QtConcurrent::run([inp, rpt, out, rawSelf, tickIntervalMs, engineVersion]() -> SimulationResult {
            // The engine resolves RELATIVE sidecar paths named in the .inp —
            // [RAINGAGES] FILE, interface files, hotstarts — against the
            // PROCESS working directory. The GUI runs the engine in-process,
            // so that directory is wherever the .app happened to be launched
            // from, NOT the model folder. A relative rain-file reference then
            // silently resolves to nothing and the run proceeds with ZERO
            // rainfall: the 1D network and the 2D mesh both stay dry except
            // where coupling/outfall water arrives. The same model run from
            // the CLI in its own directory rains normally, which is what made
            // this look like a rendering fault.
            //
            // Pin the cwd to the model directory for the duration of the run.
            // Process-global, so concurrent runs from different folders would
            // race — acceptable today (runs are launched one at a time) and
            // far better than silently dropping the forcing.
            struct CwdGuard {
                QString prev;
                explicit CwdGuard(const QString &dir) : prev(QDir::currentPath())
                {
                    if (!dir.isEmpty()) QDir::setCurrent(dir);
                }
                ~CwdGuard() { if (!prev.isEmpty()) QDir::setCurrent(prev); }
            };
            const CwdGuard cwdGuard(
                QFileInfo(QString::fromUtf8(inp)).absolutePath());

            // Use legacy worker for 5.x versions, refactored engine for 6.0.0+
            const bool useLegacy = engineVersion.startsWith("5.");

            if (!useLegacy) {
                // ===== REFACTORED ENGINE PATH =====
            SWMM_Engine eng = swmm_engine_create();

            // Open
            int rc = swmm_engine_open(eng,
                                      inp.constData(),
                                      rpt.constData(),
                                      out.constData(),
                                      nullptr);
            if (rc != SWMM_OK) {
                const QString msg = QString::fromUtf8(swmm_error_message(rc));
                swmm_engine_destroy(eng);
                return {false, rc, msg, 0.0, 0.0};
            }

            // Initialize
            rc = swmm_engine_initialize(eng);
            if (rc != SWMM_OK) {
                const QString msg = QString::fromUtf8(swmm_error_message(rc));
                swmm_engine_close(eng);
                swmm_engine_destroy(eng);
                return {false, rc, msg, 0.0, 0.0};
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
            int twoD_n_tri  = 0;
            int twoD_n_vert = 0;
            if (twoD_active) {
                swmm_2d_triangle_count(eng, &twoD_n_tri);
                swmm_2d_vertex_count(eng, &twoD_n_vert);
                if (twoD_n_tri > 0 && twoD_n_vert > 0) {
                    QVector<double> vx(twoD_n_vert), vy(twoD_n_vert),
                                    vz(twoD_n_vert);
                    swmm_2d_vertex_get_xyz_bulk(eng, vx.data(), vy.data(), vz.data());
                    QVector<int> triFlat(twoD_n_tri * 3);
                    for (int t = 0; t < twoD_n_tri; ++t) {
                        int v0 = 0, v1 = 0, v2 = 0;
                        swmm_2d_triangle_get_vertices(eng, t, &v0, &v1, &v2);
                        triFlat[t * 3 + 0] = v0;
                        triFlat[t * 3 + 1] = v1;
                        triFlat[t * 3 + 2] = v2;
                    }
                    const QString h5Path = parseTwoDOutputFile(QString::fromUtf8(inp));
                    const int jobId = rawSelf->m_jobId;
                    QMetaObject::invokeMethod(rawSelf,
                        [rawSelf, jobId, h5Path,
                         vx = std::move(vx), vy = std::move(vy),
                         vz = std::move(vz),
                         triFlat = std::move(triFlat)]() mutable {
                            emit rawSelf->twoDInitialized(
                                jobId, h5Path, vx, vy, vz, triFlat);
                        },
                        Qt::QueuedConnection);

                    // CF.2.4 — ship time-invariant edge geometry so the GUI
                    // can reconstruct cell-centred velocity from per-tick
                    // flux without re-deriving lengths/normals from vertex
                    // coords. Engine returns doubles; convert to float for
                    // the wire (RT0 doesn't need double precision).
                    const int n3 = twoD_n_tri * 3;
                    std::vector<double> rawLen(n3), rawNx(n3), rawNy(n3);
                    if (swmm_2d_edge_get_geometry_bulk(
                            eng, rawLen.data(), rawNx.data(), rawNy.data()) == SWMM_OK)
                    {
                        QVector<float> qLen(n3), qNx(n3), qNy(n3);
                        for (int i = 0; i < n3; ++i) {
                            qLen[i] = static_cast<float>(rawLen[i]);
                            qNx[i]  = static_cast<float>(rawNx[i]);
                            qNy[i]  = static_cast<float>(rawNy[i]);
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
            rc = swmm_engine_start(eng, 1 /* save_results */);
            if (rc != SWMM_OK) {
                const QString msg = QString::fromUtf8(swmm_error_message(rc));
                swmm_engine_close(eng);
                swmm_engine_destroy(eng);
                return {false, rc, msg, 0.0, 0.0};
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
            QElapsedTimer tickTimer;
            tickTimer.start();
            // Rate-limit GUI emissions to `tickIntervalMs` (user pref,
            // default 1 Hz — Slice V). Small models can step thousands
            // of times per second; emitting every step would starve
            // the GUI event loop.
            const qint64 kTickIntervalMs = tickIntervalMs;
            qint64 lastTickMs = -kTickIntervalMs; // fire immediately on first step
            while (!rawSelf->m_cancel.load()) {
                if (rawSelf->m_paused.load()) {
                    QThread::msleep(50);
                    continue;
                }
                rc = swmm_engine_step(eng, &elapsed);
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

                // ── Slice CF.MVP — per-tick 2D depth slice ─────────────────
                // Rate-limited by the surrounding kTickIntervalMs gate. Pulls
                // the latest per-triangle depth from the in-process engine
                // and ships it to the GUI thread, where SWMM2DResultsLayer
                // recolours the mesh. Engine API returns doubles; we
                // downcast to float for the wire because mm-level depth
                // precision is plenty for colour mapping and the HDF5
                // reader produces float to match.
                if (twoD_active && twoD_n_tri > 0) {
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
                    const int n3 = twoD_n_tri * 3;
                    std::vector<double> rawFlux(n3);
                    if (swmm_2d_get_edge_flux_bulk(eng, rawFlux.data()) == SWMM_OK)
                    {
                        QVector<float> flux(n3);
                        for (int i = 0; i < n3; ++i)
                            flux[i] = static_cast<float>(rawFlux[i]);
                        QMetaObject::invokeMethod(rawSelf,
                            [rawSelf, jobId, flux = std::move(flux),
                             curQDT, curTSec]() mutable {
                                emit rawSelf->twoDFluxAvailable(
                                    jobId, flux, curQDT, curTSec);
                            },
                            Qt::QueuedConnection);
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
                }
            }

            // End
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
            swmm_engine_close (eng);
            swmm_engine_destroy(eng);

            if (cancelled)
                return {false, 0,
                        QStringLiteral("Cancelled — report written up to the "
                                       "stop point"),
                        runoffErr, routingErr, twoDErr};

            if (lastErr != SWMM_OK) {
                const QString msg = !lastErrMsg.isEmpty()
                    ? lastErrMsg
                    : QString::fromUtf8(swmm_error_message(lastErr));
                return {false, lastErr, msg, runoffErr, routingErr, twoDErr};
            }
            return {true, SWMM_OK, {}, runoffErr, routingErr, twoDErr};

            } else {
                // ===== LEGACY ENGINE WORKER PROCESS PATH =====
                // Spawn worker process for isolated legacy engine execution.
                // Each worker has its own global state, allowing true parallelism.

                const QString workerPath = findLegacyWorker();
                if (workerPath.isEmpty()) {
                    return {false, 1, QStringLiteral("Legacy worker executable not found"), 0.0, 0.0};
                }

                QProcess worker;
                worker.setProgram(workerPath);
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
        })
    );
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
