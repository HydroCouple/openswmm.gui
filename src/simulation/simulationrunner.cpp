/*!
 * \file   simulationrunner.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */
#include "simulation/simulationrunner.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_massbalance.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_callbacks.h>

#include <QDate>
#include <QDateTime>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QThread>
#include <QTime>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

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
    static const QDateTime kSwmmEpoch(QDate(1899, 12, 30),
                                      QTime(0, 0), Qt::LocalTime);
    return kSwmmEpoch.addMSecs(static_cast<qint64>(oaDate * 86400.0 * 1000.0));
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Internal result type returned from the worker lambda
// ---------------------------------------------------------------------------

struct SimulationResult {
    bool    success;
    int     errorCode;
    QString errorMessage;
    double  runoffErrFrac;
    double  routingErrFrac;
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
                                   QObject *parent)
    : QObject(parent)
    , m_jobId(jobId)
    , m_instanceName(instanceName)
    , m_inpPath(inpPath)
    , m_rptPath(rptPath)
    , m_outPath(outPath)
{
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
                              res.errorMessage, res.runoffErrFrac, res.routingErrFrac);
            });

    watcher->setFuture(
        QtConcurrent::run([inp, rpt, out, rawSelf]() -> SimulationResult {
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
                QMetaObject::invokeMethod(rawSelf,
                    [rawSelf, jobId, initial]() {
                        emit rawSelf->progressChanged(jobId, 0.0, initial, 0.0, 0.0);
                    },
                    Qt::QueuedConnection);
            }

            double elapsed = 0.0;
            QElapsedTimer tickTimer;
            tickTimer.start();
            // Rate-limit GUI emissions to 1 Hz. Small models can step
            // thousands of times per second; emitting every step would
            // starve the GUI event loop.
            constexpr qint64 kTickIntervalMs = 1000;
            qint64 lastTickMs = -kTickIntervalMs; // fire immediately on first step
            while (!rawSelf->m_cancel.load()) {
                if (rawSelf->m_paused.load()) {
                    QThread::msleep(50);
                    continue;
                }
                rc = swmm_engine_step(eng, &elapsed);
                if (rc != SWMM_OK || elapsed <= 0.0)
                    break;

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

                const int jobId = rawSelf->m_jobId;
                const QDateTime curQDT = simStart.isValid()
                    ? simStart.addMSecs(static_cast<qint64>(curTSec * 1000.0))
                    : QDateTime();
                QMetaObject::invokeMethod(rawSelf,
                    [rawSelf, jobId, frac, curQDT, runoffErr, routingErr]() {
                        emit rawSelf->progressChanged(jobId, frac, curQDT,
                                                      runoffErr, routingErr);
                    },
                    Qt::QueuedConnection);
            }

            // End
            swmm_engine_end(eng);

            // Continuity errors (available after end)
            double runoffErr  = 0.0;
            double routingErr = 0.0;
            swmm_get_runoff_continuity_error (eng, &runoffErr);
            swmm_get_routing_continuity_error(eng, &routingErr);

            // Determine overall success
            const int lastErr = swmm_get_last_error(eng);
            const bool cancelled = rawSelf->m_cancel.load();

            // Report + close
            swmm_engine_report(eng);
            swmm_engine_close (eng);
            swmm_engine_destroy(eng);

            if (cancelled)
                return {false, 0, QStringLiteral("Cancelled"), runoffErr, routingErr};

            if (lastErr != SWMM_OK) {
                const QString msg = QString::fromUtf8(swmm_error_message(lastErr));
                return {false, lastErr, msg, runoffErr, routingErr};
            }
            return {true, SWMM_OK, {}, runoffErr, routingErr};
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
