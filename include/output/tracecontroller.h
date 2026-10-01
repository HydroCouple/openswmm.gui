// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "output/tracedata.h"
#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <atomic>
class SWMMVisProjectWindow;
namespace openswmmvis::trace
{
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
    void cancel();
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
    QPointer<SWMMVisProjectWindow> m_project;
    QFutureWatcher<JobReply> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QString m_activePath;
};
} // namespace openswmmvis::trace
