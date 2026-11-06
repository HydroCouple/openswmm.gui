// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      tracecontroller.h
 * @brief     Trace Controller interface and supporting types.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

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
struct JobReply {
    QString runId, error;
    bool cancelled = false;
    std::shared_ptr<Dataset> dataset;
    QVector<std::shared_ptr<Result>> results;
};
class TraceController : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Create a GUI-thread controller owned by a project and wire run-history/result delivery.
     * @param[in] p Non-null project owner with initialized stats registry.
     * @details Native trace/GDAL handles stay on workers; only owning value snapshots/results reach the GUI.
     */
    explicit TraceController(SWMMVisProjectWindow *p);
    /**
     * @brief Request cancellation and wait for analysis work before releasing controller state.
     * @details Can block while hashing/native calls drain; no engine/worker handles survive destruction.
     */
    ~TraceController() override;
    /**
     * @brief Find the direct project-owned trace controller, optionally creating it.
     * @param[in] p Borrowed project owner; null returns null.
     * @param[in] create True constructs when absent, false performs lookup only.
     * @return Borrowed project-owned controller, or null for absent owner/controller.
     */
    static TraceController *forProject(SWMMVisProjectWindow *p, bool create = true);
    /**
     * @brief Inspect whether the current analysis future is running.
     * @return Watcher running state, requiring GUI-thread access.
     */
    bool busy() const { return m_watcher.isRunning(); }
    /**
     * @brief Request saved/cached or newly computed estimates for unique seed names.
     * @param[in] runId Exact registered analyzable run ID.
     * @param[in] nodes Seed node IDs, deduplicated by exact text in input order.
     * @param[in] direction Native trace direction, zero downstream or one upstream.
     * @param[in] destination Package path, empty to use defaultPackagePath(); an existing registered package fixes the destination.
     * @param[in] snapshot Owning fallback model snapshot used only when the run lacks its captured snapshot and no package exists.
     * @param[in] replaceId Optional saved analysis ID to preserve; requires exactly one unique requested seed.
     * @details Dispatch errors emit failed(). Computation uses worker-local handles; completed estimates survive a later save failure in resultReady() with saved=false.
     */
    void run(const QString &runId, const QStringList &nodes, int direction,
             const QString &destination, const Snapshot &snapshot, const QString &replaceId = {});
    /*! Discard the run's saved averages and estimates, rescan its current output, and
     *  recompute every saved estimate (same ids, so open layers refresh in place) plus
     *  \p extraNodes in \p extraDirection.
     * @param[in] runId Exact analyzable run ID.
     * @param[in] extraNodes Additional seed names deduplicated by exact text.
     * @param[in] extraDirection Direction for additional requests, zero downstream or one upstream.
     * @param[in] snapshot Owning fallback model snapshot when no captured snapshot exists.
     * @details Removes the current package/companions before dispatch, preserving saved IDs/styles as requests. A subsequent failure may leave the old package unavailable; no package rollback occurs. Historical runs require retained raw output for recalculation.
     */
    void recalculate(const QString &runId, const QStringList &extraNodes = {},
                     int extraDirection = 0, const Snapshot &snapshot = {});
    /**
     * @brief Set the current shared cancellation flag without blocking the GUI.
     * @details Native callbacks, hashing, and package row loops observe it cooperatively; already completed results/files can remain.
     */
    void cancel();
    /*! True when an analysis layer showing \p resultId is open in this project.
     * @param[in] resultId Exact saved analysis ID.
     * @return True for any open trace sublayer with that ID, false without a project/canvas or match.
     */
    bool hasOpenLayer(const QString &resultId) const;
    /**
     * @brief Resolve an open output host or create a saved-analysis output layer for the run.
     * @param[in] runId Exact registered run ID.
     * @return Borrowed project/canvas-owned output layer, or null for no project/unknown run.
     * @details New historical hosts carry traceStoredRunId and are added to the canvas without requiring a readable raw output handle.
     */
    SWMMResultsLayer *outputForRun(const QString &runId);
    /**
     * @brief Show an existing matching estimate sublayer or attach a new one to its run host.
     * @param[in] result Non-null shared result with non-null dataset and registered run ID.
     * @param[in] travel True selects travel-time visualization, false trace fractions.
     * @return Borrowed output-owned trace sublayer, or null if no host resolves.
     * @details Existing matching ID/mode is made visible; new sublayers retain shared result ownership and mark the project changed.
     */
    TraceSublayer *attachResult(std::shared_ptr<Result> result, bool travel);
    /**
     * @brief Move analysis sublayers to historical run hosts when a path starts representing a newer run.
     * @details GUI-thread operation guarded against recursion; removes old scene items before transferring sublayer ownership.
     */
    void reconcileOutputRuns();
    /**
     * @brief Drain matching active analysis and retain prior raw output/report/model metadata when configured.
     * @param[in] outputPath Original output path about to be replaced; active job match uses exact string equality.
     * @param[out] error Optional fingerprint/copy/publication diagnostic, unchanged on success.
     * @return True when overwrite can proceed under the retention preference, false on mismatched fingerprints or retention IO failure.
     * @details Can block waiting for worker cancellation, hashing, and file copies. Retained copies made before a later report/snapshot failure remain; it does not perform the output overwrite itself.
     */
    bool beforeOverwrite(const QString &outputPath, QString *error);
    /**
     * @brief Resolve the package location for a registered run.
     * @param[in] runId Run identifier.
     * @return Existing registered package or analysis/runId/outputStem.analysis.gpkg beside the model/output directory; empty without project. No directories are created.
     */
    QString defaultPackagePath(const QString &runId) const;
signals:
    /**
     * @brief Publish queued native analysis progress.
     * @param[in] percent Integer native fractional progress times 100, emitted when it changes.
     * @param[in] stage Native stage label copied to owning text.
     */
    void progress(int percent, const QString &stage);
    /**
     * @brief Announce worker dispatch/completion.
     * @param[in] busy True before dispatch, false before completion attachment.
     */
    void busyChanged(bool busy);
    /**
     * @brief Publish available hydraulic averages after associating their package with run history.
     * @param[in] dataset Shared owning dataset with SI values and captured topology; no native handles.
     */
    void prepared(std::shared_ptr<openswmmvis::trace::Dataset> dataset);
    /**
     * @brief Publish a computed/restored result after refreshing open layers with matching ID.
     * @param[in] result Shared owning estimate; saved indicates package persistence success, which may be false after later IO failure.
     */
    void resultReady(std::shared_ptr<openswmmvis::trace::Result> result);
    /**
     * @brief Explain rejected dispatch or uncancelled worker failure.
     * @param[in] message Owning diagnostic text; cancellation completion suppresses failed().
     */
    void failed(const QString &message);
    /**
     * @brief Announce completed worker result delivery.
     * @param[in] runId Exact source run ID.
     * @param[in] cancelled Whether cooperative cancellation was requested, even when partial work was retained.
     */
    void finished(const QString &runId, bool cancelled);

private:
    struct Request {
        QString seedId;
        int direction = 0;
        QString replaceId; ///< Keep this analysis id (recalculation).
        QJsonObject style; ///< Saved style carried over to the new estimate.
    };
    /**
     * @brief Validate history/package state and dispatch one batch against reusable or freshly scanned averages.
     * @param[in] runId Exact analyzable source run ID.
     * @param[in] requests Owning seed/direction/replacement-ID/style values.
     * @param[in] destination Optional package override, empty for default.
     * @param[in] snapshot Owning fallback topology when source lacks its own snapshot.
     * @param[in] discard Rebuild saved averages and estimates, deleting existing package/companions before dispatch.
     * @details Rejects simultaneous jobs, stale historical paths without retained bytes, changed completed outputs, and incompatible existing package destinations through failed(). Partial packages are always rebuilt.
     */
    void startJob(const QString &runId, QVector<Request> requests, const QString &destination,
                  const Snapshot &snapshot, bool discard);
    /**
     * @brief Replace matching-ID open trace layer result objects after recalculation.
     * @param[in] result Non-null shared result retained by refreshed layers.
     * @details No-op without project/canvas; equal shared pointers are not rebound.
     */
    void refreshOpenLayers(const std::shared_ptr<Result> &result);
    QPointer<SWMMVisProjectWindow> m_project;
    QFutureWatcher<JobReply> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QString m_activePath;
    bool m_rebinding = false;
};
} // namespace openswmmvis::trace
