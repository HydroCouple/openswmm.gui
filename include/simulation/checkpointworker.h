/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHECKPOINTWORKER_H
#define CHECKPOINTWORKER_H
#include <QObject>
#include <QFuture>
#include <QJsonObject>
#include <QStringList>
#include <memory>

// Value-only requests/results. Native handles never leave the owning worker.
struct CheckpointRequest {
    enum class Operation { Inspect, Export, Resume };
    Operation operation = Operation::Inspect;
    QString projectRevision;
    QString engineVersion = QStringLiteral("6.0.0");
    QString modelPath, checkpointPath;
    QByteArray modelSha256; // required for Inspect/Resume: immutable input snapshot
    int outputProfile = 0;
    bool sourceHistory = false, detailedSources = false;
    QString ownershipToken;
    QString stagingRoot; // existing reviewable directory for private inspection
    QString destination; // new exclusive run/export directory
    QStringList protectedInputs, activePaths; // includes active readers/writers
};
struct CheckpointSaveResult {
    QString path;
    bool success = false;
    int errorCode = 0;
    QString error;
};
struct CheckpointResult {
    quint64 generation = 0;
    QString projectRevision;
    CheckpointRequest::Operation operation = CheckpointRequest::Operation::Inspect;
    bool success = false, cancelled = false, stale = false;
    int errorCode = 0;
    QString error;
    QJsonObject metadata;
    double acceptedSeconds = 0;
    QStringList outputPaths;
    QString publishedPath;
    QList<CheckpointSaveResult> saves;
};
Q_DECLARE_METATYPE(CheckpointResult)

// GUI-thread controller. Invalidation cancels cooperatively; synchronous native
// IO drains. Closing a view must retain this controller until finished. Its
// destructor waits as a final fallback. No dialog or authored-run activation here.
class CheckpointWorker : public QObject {
    Q_OBJECT
public:
    explicit CheckpointWorker(QObject* parent = nullptr);
    ~CheckpointWorker() override;
    bool start(const CheckpointRequest&);
    void cancel();
    void invalidate();
    bool queueSave(const QString& newPath);
    bool isBusy() const { return m_busy; }
    quint64 generation() const { return m_generation; }
signals:
    void progress(quint64 generation, double acceptedSeconds);
    void finished(const CheckpointResult&);
private:
    struct Control;
    static CheckpointResult execute(const CheckpointRequest&, quint64,
                                   const std::shared_ptr<Control>&, CheckpointWorker*);
    std::shared_ptr<Control> m_control;
    QFuture<CheckpointResult> m_future;
    quint64 m_generation = 0;
    bool m_busy = false;
};
#endif
