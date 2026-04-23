/*!
 * \file   simulationrunner.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */
#include "simulation/simulationrunner.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_massbalance.h>
#include <openswmm/engine/openswmm_callbacks.h>

#include <QMetaObject>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

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

            // Register callbacks (fire on this worker thread)
            swmm_set_progress_callback(eng, &SimulationRunner::progressCallback, rawSelf);
            swmm_set_warning_callback (eng, &SimulationRunner::warningCallback,  rawSelf);

            // Start
            rc = swmm_engine_start(eng, 1 /* save_results */);
            if (rc != SWMM_OK) {
                const QString msg = QString::fromUtf8(swmm_error_message(rc));
                swmm_engine_close(eng);
                swmm_engine_destroy(eng);
                return {false, rc, msg, 0.0, 0.0};
            }

            // Step loop
            double elapsed = 0.0;
            while (!rawSelf->m_cancel.load()) {
                rc = swmm_engine_step(eng, &elapsed);
                if (rc != SWMM_OK || elapsed <= 0.0)
                    break;
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
}

// ---------------------------------------------------------------------------
// Static C callbacks (worker thread → GUI thread via queued invoke)
// ---------------------------------------------------------------------------

void SimulationRunner::progressCallback(void* /*engine*/, double frac,
                                         double simTime, void *ud)
{
    auto *runner = static_cast<SimulationRunner *>(ud);
    const int jobId = runner->m_jobId;
    QMetaObject::invokeMethod(runner,
        [runner, jobId, frac, simTime]() {
            emit runner->progressChanged(jobId, frac, simTime);
        },
        Qt::QueuedConnection);
}

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
