/*!
 * \file   simulationrunner.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#ifndef SIMULATIONRUNNER_H
#define SIMULATIONRUNNER_H

#include <QDateTime>
#include <QObject>
#include <QString>
#include <atomic>

/**
 * @brief Runs a single SWMM simulation on a worker thread and emits
 *        live progress, warnings, and completion signals back to the
 *        GUI thread.
 *
 * The runner uses the full lifecycle (create → open → initialize →
 * start → step-loop → end → report → close → destroy) so that both
 * `SWMM_ProgressCallback` and `SWMM_WarningCallback` can be registered.
 * Static C callbacks post back via `QMetaObject::invokeMethod` with
 * `Qt::QueuedConnection`.
 *
 * Ownership: created and destroyed on the GUI thread by `SWMMVis`.
 * The runner self-deletes after emitting `finished`.
 */
class SimulationRunner : public QObject
{
    Q_OBJECT

public:
    explicit SimulationRunner(int jobId,
                              const QString &instanceName,
                              const QString &inpPath,
                              const QString &rptPath,
                              const QString &outPath,
                              const QString &engineVersion = "6.0.0",
                              QObject *parent = nullptr);

    /** Launch the simulation on a worker thread via QtConcurrent::run. */
    void start();

    /**
     * @brief Request early termination. Step-loop exits at the next
     *        iteration; `swmm_engine_end` + `swmm_engine_report` +
     *        `swmm_engine_close` STILL run so the partial .out and .rpt
     *        files are flushed to disk — Cancel saves whatever the
     *        engine has produced up to the moment of cancellation.
     */
    void cancel();

    /** Request pause — the step-loop parks in a short sleep until
     *  setPaused(false) is called. Safe to call from the GUI thread. */
    void setPaused(bool paused);
    bool isPaused() const { return m_paused.load(); }

    int jobId() const { return m_jobId; }

signals:
    void started(int jobId);

    /**
     * @brief Emitted once the engine is initialised and its OPTIONS
     *        section has been parsed. Carries the engine-side simulation
     *        window as calendar QDateTime values. The GUI status model
     *        uses these to render readable start / end / current columns.
     */
    void simulationDatesKnown(int jobId, QDateTime start, QDateTime end);
    /**
     * @brief Per-tick progress signal emitted from the step loop.
     * @param fraction          0.0–1.0 sim-time progress
     * @param currentSimDate    engine "current time" converted from the
     *                          SWMM OADate to a calendar QDateTime — lets
     *                          the status model assign it verbatim without
     *                          re-deriving from start + offset (which can
     *                          drift across DST boundaries).
     * @param runoffErrFrac     cumulative runoff continuity error so far
     * @param routingErrFrac    cumulative routing continuity error so far
     */
    void progressChanged(int jobId, double fraction, QDateTime currentSimDate,
                         double runoffErrFrac, double routingErrFrac,
                         double avgTimestepSec);
    void warningReceived(int jobId, int code, QString message);
    /**
     * @param runoffErrFrac   runoff continuity error fraction (0.001 = 0.1 %)
     * @param routingErrFrac  routing continuity error fraction
     */
    void finished(int jobId, bool success, int errorCode, QString errorMessage,
                  double runoffErrFrac, double routingErrFrac);

private:
    // Warning callback — fires on the worker thread during engine
    // open/initialize, posts back via QMetaObject::invokeMethod. The
    // engine's progress callback is only invoked once (in initialize), so
    // progress is polled inline in the step loop instead, not through a
    // registered callback.
    static void warningCallback(void* engine, int code, const char *msg, void *ud);

    int     m_jobId;
    QString m_instanceName;
    QString m_inpPath;
    QString m_rptPath;
    QString m_outPath;
    QString m_engineVersion;

    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_paused{false};
};

#endif // SIMULATIONRUNNER_H
