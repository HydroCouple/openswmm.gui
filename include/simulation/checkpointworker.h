/* SPDX-License-Identifier: GPL-3.0-or-later */

/**
 * @file      checkpointworker.h
 * @brief     Worker-side checkpoint operations using value-only requests and results.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef CHECKPOINTWORKER_H
#define CHECKPOINTWORKER_H
#include <QObject>
#include <QtNumeric>
#include <QFuture>
#include <QJsonObject>
#include <QStringList>
#include <memory>

// Value-only requests/results. Native handles never leave the owning worker.
struct CheckpointRequest {
    enum class Operation { Inspect, Export, Resume, Run };
    Operation operation = Operation::Inspect;
    QString projectRevision;
    QString engineVersion = QStringLiteral("6.0.0");
    QString modelPath, checkpointPath;
    QByteArray modelSha256; // required for Inspect/Resume/Run: immutable input snapshot
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
    double acceptedSeconds = qQNaN(), acceptedDate = qQNaN();
};
Q_DECLARE_METATYPE(CheckpointSaveResult)
struct CheckpointResult {
    quint64 generation = 0;
    QString projectRevision;
    CheckpointRequest::Operation operation = CheckpointRequest::Operation::Inspect;
    bool success = false, cancelled = false, stale = false;
    int errorCode = 0;
    QString error;
    QJsonObject metadata;
    double acceptedSeconds = 0;
    double startDate = 0, endDate = 0;
    double runoffError = qQNaN(), routingError = qQNaN(), twoDError = qQNaN();
    QStringList outputPaths;
    QString publishedPath;
    QList<CheckpointSaveResult> saves;
};
Q_DECLARE_METATYPE(CheckpointResult)

// GUI-thread controller. Invalidation cancels cooperatively; synchronous native
// IO drains. Closing a view must retain this controller until finished. Its
// destructor waits as a final fallback. Authored runs use the native builder.
class CheckpointWorker : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Create an idle GUI-thread checkpoint controller and register value-result Qt metatypes.
     * @param[in] parent Optional QObject owner.
     */
    explicit CheckpointWorker(QObject *parent = nullptr);
    /**
     * @brief Request cancellation and wait for synchronous native work to drain before destruction.
     * @details Destruction can block; normal view closure retains the controller until finished().
     */
    ~CheckpointWorker() override;
    /**
     * @brief Dispatch a value-only checkpoint operation to a dedicated native worker pool.
     * @param[in] request Frozen request copied before dispatch; engine/checkpoint handles are constructed only inside the worker.
     * @return False while busy or when dispatch setup fails; true once launched (even if launch-recovery drains synchronously and reports a result).
     * @details Starts a new generation and eventually emits finished() with native error/cancellation/staleness. Caller must use the controller owning thread.
     */
    bool start(const CheckpointRequest &request);
    /**
     * @brief Set the current operation cooperative cancellation flag, without invalidating its generation.
     * @details Synchronous native IO completes before cancellation can be observed. No-op before an operation exists.
     */
    void cancel();
    /**
     * @brief Increment result generation and request cancellation so stale inspection metadata is suppressed.
     */
    void invalidate();
    /**
     * @brief Queue checkpoint publication for an active run/resume accepted boundary.
     * @param[in] newPath Absolute round-trip UTF-8 path without NULs; actual write/alias/exclusive checks run on the worker.
     * @return False when not busy, cancelled, path-invalid, or no longer accepting saves; true means queued, not saved.
     * @details Call on the controller thread. Inspect/export do not accept save requests; saves arriving after run processing closes are reported as unprocessed-save errors.
     */
    bool queueSave(const QString &newPath);
    /**
     * @brief Test whether dispatch has not yet completed GUI-side reconciliation.
     * @return Busy flag, requiring owning-thread access.
     */
    bool isBusy() const { return m_busy; }
    /**
     * @brief Read the current result-generation token.
     * @return Token incremented by start/invalidate and used to reject stale notifications.
     */
    quint64 generation() const { return m_generation; }
signals:
    /**
     * @brief Publish a current-generation accepted run boundary.
     * @param[in] generation Dispatch generation.
     * @param[in] acceptedSeconds Elapsed simulation seconds.
     */
    void progress(quint64 generation, double acceptedSeconds);
    /**
     * @brief Publish current-generation snow diagnostics as values only.
     * @param[in] generation Dispatch generation.
     * @param[in] rows SI water/energy balance strings.
     */
    void snowLedgerChanged(quint64 generation, const QStringList &rows);
    /**
     * @brief Publish a current-generation run simulation window.
     * @param[in] generation Dispatch generation.
     * @param[in] startDate SWMM absolute start days.
     * @param[in] endDate SWMM absolute end days.
     */
    void timeline(quint64 generation, double startDate, double endDate);
    /**
     * @brief Publish one accepted-boundary checkpoint save attempt.
     * @param[in] generation Operation generation; save results are delivered even if inspection generation changes.
     * @param[in] result Value-only publication result and accepted time on success.
     */
    void saved(quint64 generation, const CheckpointSaveResult &result);
    /**
     * @brief Publish terminal operation state after clearing controller busy state.
     * @param[in] result Value-only result; stale/cancelled inspections expose no reusable metadata.
     */
    void finished(const CheckpointResult &result);

private:
    struct Control;
    /**
     * @brief Execute native inspection/export/run/resume with worker-local handles and path leases.
     * @param[in] request Frozen request with absolute paths; inspect/run/resume require immutable SHA-256 model input and linked engine version.
     * @param[in] generation Dispatch token copied into returned/queued values.
     * @param[in] control Shared cancellation/save-queue state, synchronized by atomics/mutex.
     * @param[in] receiver Borrowed GUI controller kept alive until work drains; used only for queued value notifications.
     * @return Terminal accepted-state result; run failures/cancellation retain partial files. Export publication is exclusive and inspection scratch is confined to the supplied reviewable staging root.
     * @details Handles are created/closed/destroyed on the worker, plugin/process-component input rows are rejected, and caught native/standard failures become diagnostics.
     */
    static CheckpointResult execute(const CheckpointRequest &request, quint64 generation,
                                    const std::shared_ptr<Control> &control,
                                    CheckpointWorker *receiver);
    std::shared_ptr<Control> m_control;
    QFuture<CheckpointResult> m_future;
    quint64 m_generation = 0;
    bool m_busy = false;
};
#endif
