/*!
 * \file   simulationrunner.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */
#ifndef SIMULATIONRUNNER_H
#define SIMULATIONRUNNER_H

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
                              QObject *parent = nullptr);

    /** Launch the simulation on a worker thread via QtConcurrent::run. */
    void start();

    /** Request early termination; step-loop exits at the next iteration. */
    void cancel();

    int jobId() const { return m_jobId; }

signals:
    void started(int jobId);
    void progressChanged(int jobId, double fraction, double simTimeDays);
    void warningReceived(int jobId, int code, QString message);
    /**
     * @param runoffErrFrac   runoff continuity error fraction (0.001 = 0.1 %)
     * @param routingErrFrac  routing continuity error fraction
     */
    void finished(int jobId, bool success, int errorCode, QString errorMessage,
                  double runoffErrFrac, double routingErrFrac);

private:
    // Static C callbacks — fire on worker thread; post to GUI via invokeMethod.
    static void progressCallback(void* engine, double frac, double simTime, void *ud);
    static void warningCallback (void* engine, int code, const char *msg,  void *ud);

    int     m_jobId;
    QString m_instanceName;
    QString m_inpPath;
    QString m_rptPath;
    QString m_outPath;

    std::atomic<bool> m_cancel{false};
};

#endif // SIMULATIONRUNNER_H
