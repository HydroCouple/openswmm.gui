// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "output/tracedata.h"
#include <QFutureWatcher>
#include <QLoggingCategory>
#include <QObject>
#include <QPointer>
#include <atomic>
class SWMMVisProjectWindow;
class SWMMResultsLayer;
namespace openswmmvis::trace
{
Q_DECLARE_LOGGING_CATEGORY(lcTracePerf)
class TraceSublayer;
struct JobReply
{
    QString runId, error;
    bool cancelled = false;
    std::shared_ptr<Dataset> dataset;
    QVector<std::shared_ptr<Result>> results;
};
class TraceController : public QObject
{
    Q_OBJECT
  public:
    explicit TraceController(SWMMVisProjectWindow *);
    ~TraceController() override;
    static TraceController *forProject(SWMMVisProjectWindow *, bool create = true);
    bool busy() const { return m_watcher.isRunning(); }
    void run(const QString &runId, const QStringList &nodes, int direction,
             const QString &destination, const Snapshot &snapshot, const QString &replaceId = {});
    /*! Discard the run's saved averages and estimates, rescan its current output, and
     *  recompute every saved estimate (same ids, so open layers refresh in place) plus
     *  \p extraNodes in \p extraDirection. */
    void recalculate(const QString &runId, const QStringList &extraNodes = {},
                     int extraDirection = 0, const Snapshot &snapshot = {});
    void cancel();
    /*! True when an analysis layer showing \p resultId is open in this project. */
    bool hasOpenLayer(const QString &resultId) const;
    SWMMResultsLayer *outputForRun(const QString &runId);
    TraceSublayer *attachResult(std::shared_ptr<Result>, bool travel);
    void reconcileOutputRuns();
    bool beforeOverwrite(const QString &outputPath, QString *error);
    QString defaultPackagePath(const QString &runId) const;
  signals:
    void progress(int percent, const QString &stage);
    void busyChanged(bool);
    void prepared(std::shared_ptr<openswmmvis::trace::Dataset>);
    void resultReady(std::shared_ptr<openswmmvis::trace::Result>);
    void failed(const QString &message);
    void finished(const QString &runId, bool cancelled);

  private:
    struct Request
    {
        QString seedId;
        int direction = 0;
        QString replaceId;   ///< Keep this analysis id (recalculation).
        QJsonObject style;   ///< Saved style carried over to the new estimate.
    };
    void startJob(const QString &runId, QVector<Request> requests, const QString &destination,
                  const Snapshot &snapshot, bool discard);
    void refreshOpenLayers(const std::shared_ptr<Result> &);
    QPointer<SWMMVisProjectWindow> m_project;
    QFutureWatcher<JobReply> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QString m_activePath;
    bool m_rebinding = false;
};
} // namespace openswmmvis::trace
