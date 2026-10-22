/**
 * @file      outputstatsregistry.h
 * @brief     per-project registry that tags every loaded SWMMResultsLayer with a stable identity.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice QA.1 — per-project registry that tags every loaded SWMMResultsLayer
 * with a stable identity. Downstream consumers (the node attribute panel,
 * Slice CB Statistics dashboard, Slice BF tabular results, Slice BH custom
 * reports) read from a single source of truth so the "which output produced
 * these numbers?" answer is uniform across the GUI.
 *
 * The registry is owned by SWMMVisProjectWindow (one per project window;
 * multi-project = multi-registry per Slice 13's session model). Layers
 * call registerLayer() from their openResults() success path and
 * unregisterLayer() from closeResults(); the registry emits
 * identitiesChanged() on every mutation so panels can refresh their
 * "Stats source:" combos without manual repaints.
 *
 * The null UUID is reserved as a sentinel meaning "use the editing
 * engine's ambient stats" — consumers that want to keep today's
 * behaviour when no source is explicitly chosen branch on
 * `identity.stableId.isNull()`.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OUTPUTSTATSREGISTRY_H
#define OUTPUTSTATSREGISTRY_H

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUuid>
#include <QJsonArray>
#include <QJsonObject>

class SWMMResultsLayer;

namespace openswmmvis
{

struct OutputRun {
    QString id, sourceId, path, label, state, previousId, completedAt;
    QString fingerprint, packagePath, retainedPath, reportPath;
    QJsonObject snapshot;
    int number = 1;
    qint64 size = -1, modified = -1;
    bool native = false;
    // Running and Cancelled outputs are analyzed as partial results (whole periods written so far).
    /**
     * @brief Classify outputs whose available report history may be incomplete.
     * @return True only for exact Running or Cancelled state strings.
     */
    bool partial() const
    {
        return state == QStringLiteral("Running") || state == QStringLiteral("Cancelled");
    }
    /**
     * @brief Test run-state eligibility for trace analysis.
     * @return True for Complete, Imported, Running, or Cancelled; no file-readability check occurs here.
     */
    bool canAnalyze() const
    {
        return state == QStringLiteral("Complete") || state == QStringLiteral("Imported") ||
               partial();
    }
};

/*! One row in the registry. Captures everything a consumer needs to
 *  render a row in the Stats-source combo and dispatch a stat-getter
 *  call back to the underlying layer. */
struct OutputIdentity {
    /*! Stable identity for this loaded output, generated at registration
     *  time. Survives layer reorder so per-project QSettings can persist
     *  the user's last-selected stats source by UUID. The null UUID is
     *  the "editing-engine" sentinel and is never produced by
     *  registerLayer. */
    QUuid stableId;

    /*! Human-readable short label, derived from the .out filename without
     *  extension (e.g. "run-A"). Collision-resolved by appending "(N)"
     *  when two .out files share a basename across loaded layers. */
    QString shortLabel;

    /*! Full absolute path to the .out file. Suitable for use as the
     *  combo entry's tool-tip. */
    QString tooltipPath;

    /*! Pointer to the underlying layer. Null-checked by consumers
     *  before dispatch (the layer may be destroyed before the registry
     *  receives the unregister callback in pathological teardown
     *  orders; QPointer in the registry's internal storage handles
     *  that gracefully). */
    SWMMResultsLayer *layer = nullptr;
    QString runId;
};

/*! Per-project registry of loaded output layers. */
class OutputStatsRegistry : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Create an empty project-local owner of run and loaded-layer identities.
     * @param[in] parent Optional QObject owner; use on the GUI thread.
     */
    explicit OutputStatsRegistry(QObject *parent = nullptr);
    /**
     * @brief Destroy identity/value storage without owning or destroying output layers.
     */
    ~OutputStatsRegistry() override;

    /*! Add \p layer to the registry. No-op if \p layer is already
     *  registered (looked up by pointer identity, not stableId).
     *  Emits identitiesChanged() on a real change.
     *
     *  \p resultsFilePath is captured at registration time and used to
     *  derive the short label and tooltip. The registry reads the layer historical-run property while retaining
     *  only a guarded borrowed pointer.
     * @param[in] layer Borrowed QPointer-guarded layer; null/duplicate pointer is ignored.
     * @param[in] resultsFilePath Captured absolute tooltip path used to resolve/import run identity.
     * @details Reads traceStoredRunId QObject property for historical association, otherwise chooses latest path run or imports a new one. Can emit runsChanged() as well as identitiesChanged().
     */
    void registerLayer(SWMMResultsLayer *layer, const QString &resultsFilePath);

    /*! Remove \p layer from the registry. No-op if \p layer is not
     *  registered. Emits identitiesChanged() on a real change.
     * @param[in] layer Borrowed pointer identity to remove; null/absent is ignored.
     * @details Run history remains after loaded-layer removal; recomputes labels and emits identitiesChanged().
     */
    void unregisterLayer(SWMMResultsLayer *layer);

    /*! Snapshot of every currently-registered identity, in registration
     *  order.
     * @return Owning value rows with borrowed layer pointers, possibly null if layers were destroyed; copying rows does not extend layer lifetime.
     */
    [[nodiscard]] QList<OutputIdentity> identities() const;

    /*! Look up an identity by its stableId. Returns a default-constructed
     *  OutputIdentity (null UUID, empty label) when \p id is the null
     *  UUID or no match is found.
     * @param[in] id Stable non-null registration UUID.
     * @return Matching copied row or default sentinel; any returned layer pointer remains borrowed.
     */
    [[nodiscard]] OutputIdentity identityFor(const QUuid &id) const;

    /**
     * @brief Snapshot historical runs in insertion order.
     * @return Owning value list including partial/failed/interrupted records.
     */
    QList<OutputRun> runs() const { return m_runs; }
    /**
     * @brief Resolve a historical run ID.
     * @param[in] id Exact run identifier.
     * @return Copied matching run, or default with empty id when absent.
     */
    OutputRun run(const QString &id) const;
    /**
     * @brief Resolve the newest registered run for an absolute path string.
     * @param[in] path Candidate output path, made absolute without filesystem alias resolution.
     * @return Last matching run in insertion order, or empty-id default.
     */
    OutputRun latestRun(const QString &path) const;
    /**
     * @brief Create a new native Running history version and rebind current output slots.
     * @param[in] path Output destination; history identity is path-based.
     * @param[in] snapshot Captured model metadata copied for later analysis provenance.
     * @param[in] reportPath Optional report destination retained verbatim.
     * @return New UUID-text run ID, sharing source lineage with previous runs at the same path.
     * @details Current slots without traceStoredRunId historical pinning rebind to the new ID; publishes identity/run changes.
     */
    QString beginRun(const QString &path, const QJsonObject &snapshot,
                     const QString &reportPath = {});
    /**
     * @brief Set a terminal run state and capture output size/mtime.
     * @param[in] id Exact run identifier.
     * @param[in] success True selects Complete even if cancelled is true.
     * @param[in] cancelled Selects Cancelled when unsuccessful, otherwise Failed.
     * @details Publishes identity/run changes even for unknown ID; filesystem metadata is read without content hashing.
     */
    void finishRun(const QString &id, bool success, bool cancelled);
    /**
     * @brief Reconcile a registered layer path/content version with run history.
     * @param[in] layer Registered borrowed output layer; unregistered still triggers list notifications.
     * @param[in] path Opened output path.
     * @param[in] live True treats nonnative output as Running; false as Imported and may detect changed size/mtime.
     * @details Imports a new version on path/content mismatch and captures current file size/time; does not read result periods.
     */
    void contentOpened(SWMMResultsLayer *layer, const QString &path, bool live);
    /**
     * @brief Associate package/fingerprint/model snapshot with an existing run.
     * @param[in] id Exact run ID.
     * @param[in] fingerprint Result-content fingerprint, empty for growing partial output.
     * @param[in] package Analysis package path copied verbatim.
     * @param[in] snapshot Model snapshot copied verbatim.
     * @details Unknown ID changes no records but still emits runsChanged().
     */
    void attachAnalysis(const QString &id, const QString &fingerprint, const QString &package,
                        const QJsonObject &snapshot);
    /**
     * @brief Restore package provenance into history, creating an Imported run if its ID is unknown.
     * @param[in] id Exact run ID to adopt.
     * @param[in] sourceId Source-lineage ID for a new record.
     * @param[in] path Original output path for a new record, copied verbatim.
     * @param[in] label Display label for a new record.
     * @param[in] fingerprint Package content fingerprint.
     * @param[in] package Package path.
     * @param[in] snapshot Saved model snapshot.
     * @details Existing identity metadata remains; attachAnalysis() refreshes package provenance and emits runsChanged().
     */
    void adoptAnalysis(const QString &id, const QString &sourceId, const QString &path,
                       const QString &label, const QString &fingerprint, const QString &package,
                       const QJsonObject &snapshot);
    /**
     * @brief Record a previous-output copy path after external retention work.
     * @param[in] id Exact run ID.
     * @param[in] path Retained path copied verbatim; no file copy occurs.
     * @details Emits runsChanged() even for unknown ID.
     */
    void setRetainedPath(const QString &id, const QString &path);
    /// Record the output file's current size and time for a run (its results were accepted anew).
    /**
     * @param[in] id Exact non-Running run ID.
     * @details Reads size/mtime from retainedPath when present, otherwise original path; publishes runsChanged() even when no record is updated.
     */
    void recordOutputFile(const QString &id);
    /**
     * @brief Serialize history with project-relative paths.
     * @param[in] baseDirectory Directory used for relative output/package/retained/report paths.
     * @return Owning JSON array; performs no writes. Byte-size/mtime integers are represented as JSON doubles.
     */
    QJsonArray saveRuns(const QString &baseDirectory) const;
    /**
     * @brief Replace history from project JSON and reconcile registered layer associations.
     * @param[in] a Run records; empty/duplicate IDs are skipped.
     * @param[in] baseDirectory Directory used to resolve nonempty saved paths.
     * @details Running restores as Interrupted. Does not verify files or fingerprints; rekeys slots by historical property/latest path and emits identity/run changes.
     */
    void restoreRuns(const QJsonArray &a, const QString &baseDirectory);

signals:
    /*! Emitted after registerLayer / unregisterLayer mutates the set. */
    void identitiesChanged();
    /**
     * @brief Notify observers after run history/provenance updates, including some no-match requests.
     */
    void runsChanged();

private:
    /*! Recompute every entry's shortLabel after a set mutation. Resolves
     *  filename collisions by appending "(N)" to duplicates in
     *  registration order.
     * @details Uses cached path basenames with exact case-sensitive collision counts, then appends associated run number/state. Does not emit independently.
     */
    void recomputeLabels();

    struct Slot {
        QUuid stableId;
        QString shortLabel;
        QString tooltipPath;
        QPointer<SWMMResultsLayer> layer;
        QString runId;
    };

    QList<Slot> m_slots;
    QList<OutputRun> m_runs;
    /**
     * @brief Append an Imported history version for a path.
     * @param[in] path Output path made absolute.
     * @return New run UUID; previous same-path lineage is reused with incremented run number. Emits runsChanged().
     */
    QString importRun(const QString &path);
};

} // namespace openswmmvis

#endif // OUTPUTSTATSREGISTRY_H
