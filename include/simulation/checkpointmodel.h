/* SPDX-License-Identifier: GPL-3.0-or-later */

/**
 * @file      checkpointmodel.h
 * @brief     Project-owned checkpoint model and controller for GUI views.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef CHECKPOINTMODEL_H
#define CHECKPOINTMODEL_H
#include "simulation/checkpointworker.h"
#include <QAbstractTableModel>

struct CheckpointEntry {
    QString id, name, path, lineage, revision;
    quint64 publication = 0;
    double acceptedDate = qQNaN(), acceptedSeconds = qQNaN();
    bool pinned = false, compatible = false;
    QString inspection = QStringLiteral("Not checked"), recipe;
    QJsonObject metadata;
};
struct CheckpointFile {
    QString role, path, requiredBytes, currentBytes;
    bool readable = false;
};
struct CheckpointStorage {
    quint64 bytes = 0;
    bool complete = true;
    QStringList paths;
};

// One GUI-thread model/controller per project; views own no native objects.
class CheckpointModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    /**
     * @brief Load a project checkpoint catalog and connect owned IO/run controllers.
     * @param[in] catalog JSON catalog path whose bytes are guarded against external replacement before persistence.
     * @param[in] parent Optional QObject owner.
     * @details Use on the GUI thread. Entries contain value metadata; engine/checkpoint handles remain on workers. Malformed/unreadable existing catalogs disable subsequent catalog writes.
     */
    explicit CheckpointModel(const QString &catalog, QObject *parent = nullptr);
    /**
     * @brief Count top-level checkpoint entries.
     * @param[in] parent Invalid for root.
     * @return Entry count at root, otherwise zero.
     */
    int rowCount(const QModelIndex &parent = {}) const override;
    /**
     * @brief Expose name, accepted time, kind, inspection, and pin columns.
     * @param[in] parent Invalid for root.
     * @return Five at root, otherwise zero.
     */
    int columnCount(const QModelIndex &parent = {}) const override;
    /**
     * @brief Read checkpoint display/tooltip/pin state.
     * @param[in] i Current table index.
     * @param[in] role DisplayRole, ToolTipRole, or pin-column CheckStateRole.
     * @return Requested metadata or invalid QVariant for unsupported/invalid input.
     */
    QVariant data(const QModelIndex &i, int role = Qt::DisplayRole) const override;
    /**
     * @brief Label the checkpoint table columns.
     * @param[in] c Zero-based column.
     * @param[in] o Horizontal selects labels; other orientations unsupported.
     * @param[in] role Only DisplayRole is supported.
     * @return Translated label or invalid QVariant for unsupported orientation/role.
     */
    QVariant headerData(int c, Qt::Orientation o, int role = Qt::DisplayRole) const override;
    /**
     * @brief Store a pin-column check state and attempt catalog persistence.
     * @param[in] i Existing row in pin column four.
     * @param[in] v Checked means pinned; any other integer state means unpinned.
     * @param[in] role Must be CheckStateRole.
     * @return False for unsupported row/column/role; true after local assignment even if catalog persistence fails.
     * @details Emits row dataChanged() and stateChanged(); no checkpoint bytes are modified.
     */
    bool setData(const QModelIndex &i, const QVariant &v, int role) override;
    /**
     * @brief Add user-checkable behavior for the pin column.
     * @param[in] i Table index.
     * @return Base table flags plus ItemIsUserCheckable for valid column four.
     */
    Qt::ItemFlags flags(const QModelIndex &i) const override;
    /**
     * @brief Inspect a catalog entry without copying it.
     * @param[in] row Zero-based entry index.
     * @return Borrowed pointer or null out of range; list changes/destruction invalidate it.
     */
    const CheckpointEntry *entry(int row) const;
    /**
     * @brief Replace the frozen input/engine/profile recipe used for inspection and runs.
     * @param[in] r Value-only context copied into model storage.
     * @details A changed recipe invalidates the IO generation and marks all compatibility results stale without cancelling a running simulation. Identical recipe still refreshes other context fields.
     */
    void setContext(const CheckpointRequest &r);
    /**
     * @brief Inspect the stored value-only request context.
     * @return Borrowed context reference, updated by setContext()/setInventory().
     */
    const CheckpointRequest &context() const { return m_context; }
    /**
     * @brief Update path guards for future checkpoint requests.
     * @param[in] inputs Protected input paths.
     * @param[in] active Active reader/writer paths.
     * @details Does not invalidate compatibility or change already dispatched request snapshots.
     */
    void setInventory(const QStringList &inputs, const QStringList &active);
    /**
     * @brief Store native checkpoint capability flags and notify stateChanged() unconditionally.
     * @param[in] caps Engine capability bitmask used by views.
     */
    void setCapabilities(unsigned long caps)
    {
        m_capabilities = caps;
        emit stateChanged();
    }
    /**
     * @brief Read the last supplied capability mask.
     * @return Stored flags, initially zero.
     */
    unsigned long capabilities() const { return m_capabilities; }
    /**
     * @brief Register an existing absolute checkpoint path without validating its contents.
     * @param[in] path Absolute existing file path; filesystem aliases of an existing entry count as already imported.
     * @return False for closed model/nonabsolute/nonfile input; true for an existing entry or local insertion, even if catalog write fails.
     */
    bool importFile(const QString &path);
    /**
     * @brief Remove a catalog row while retaining all checkpoint/companion bytes.
     * @param[in] row Existing zero-based row.
     * @return False for invalid row/closed project; true after local removal and attempted persistence.
     * @details Invalidates inspection if this row is currently being checked; pinned entries are still forgettable.
     */
    bool forget(int row); // Only catalog entry. Bytes remain.
    /**
     * @brief Dispatch compatibility inspection against the current frozen recipe.
     * @param[in] row Existing zero-based checkpoint row.
     * @return True when worker dispatch succeeds; false for invalid row, closed project, active IO, or launch failure.
     * @details Marks the entry Checking/incompatible immediately; async completion installs metadata only for a successful current generation.
     */
    bool inspectEntry(int row);
    /**
     * @brief Dispatch exclusive relocatable-bundle export for a checkpoint.
     * @param[in] row Existing catalog row.
     * @param[in] newFolder New absolute destination directory, validated by the worker.
     * @return True when dispatched, false for invalid row/closed model/busy IO or dispatch failure; completion arrives asynchronously.
     */
    bool exportEntry(int row, const QString &newFolder);
    /**
     * @brief Dispatch a new native run or a compatible checkpoint resume into an exclusive folder.
     * @param[in] row Negative selects a fresh Run; nonnegative selects an inspected compatible row whose recipe still matches.
     * @param[in] newFolder New absolute run destination validated by the worker.
     * @return False while IO/run busy, closed, invalid/incompatible recipe, or failed dispatch; true after dispatch and runStarted()/stateChanged().
     */
    bool startRun(int row, const QString &newFolder); // row -1: initial run
    /**
     * @brief Queue checkpoint publication at a worker accepted boundary.
     * @param[in] newPath Absolute new checkpoint path forwarded to queueSave().
     * @return True when queued, false when closed or the run worker cannot accept it; actual publication arrives later.
     */
    bool save(const QString &newPath);
    /**
     * @brief Request cooperative IO cancellation and publish waiting status.
     * @details Native synchronous IO drains before completion; this does not block the GUI.
     */
    void cancelIO();
    /**
     * @brief Request run termination at an accepted worker boundary and publish waiting status.
     */
    void cancelRun();
    /**
     * @brief Mark closed, invalidate IO results, and request run cancellation.
     * @details Suppresses later notice/snow rows but allows completion/save reconciliation; retain the model until workers finish or destruction waits for them.
     */
    void closeProject();
    /**
     * @brief Inspect pending inspection/export work.
     * @return IO worker busy state.
     */
    bool ioBusy() const { return m_io.isBusy(); }
    /**
     * @brief Inspect pending run/resume work.
     * @return Run worker busy state.
     */
    bool runBusy() const { return m_run.isBusy(); }
    /**
     * @brief Test whether either owned controller has active work.
     * @return IO-busy or run-busy.
     */
    bool busy() const { return ioBusy() || runBusy(); }
    /**
     * @brief Read the current human-facing checkpoint operation status.
     * @return Copied status text, initially empty or a catalog-load diagnostic.
     */
    QString status() const { return m_status; }
    /**
     * @brief Snapshot this model current operation paths for overwrite guards.
     * @return Nonempty run/IO destinations, model snapshots, and checkpoint paths; may contain duplicates and is not an external lock.
     */
    QStringList reservedPaths() const;
    /**
     * @brief Enumerate a checkpoint and inspected companion paths with current readability/size.
     * @param[in] row Zero-based catalog entry.
     * @return Owning list with metadata required-byte counts and filesystem current sizes, empty for invalid row.
     */
    QList<CheckpointFile> files(int row) const;
    /**
     * @brief Suggest older unpinned checkpoint rows while retaining newest entries per lineage.
     * @param[in] keepNewest Positive count of eligible publications retained within each lineage; nonpositive returns empty.
     * @return Candidate row indices only, performing no deletion. Requires readable metadata/files and no overlap with reserved/active paths.
     */
    QList<int> suggestions(int keepNewest) const;
    /**
     * @brief Sum unique readable checkpoint/companion file sizes for selected rows.
     * @param[in] rows Zero-based entry indices; invalid rows contribute no files.
     * @return Paths/byte total with complete=false for unreadable files, failed size conversion, or overflow; filesystem aliases are counted once.
     */
    CheckpointStorage storage(const QList<int> &rows) const;
    /**
     * @brief Format an inspected SWMM absolute date with milliseconds.
     * @param[in] raw Numeric SWMM days since 1899-12-30.
     * @return Calendar text, em dash for nonnumeric input, or Invalid date for failed conversion.
     */
    static QString calendar(const QJsonValue &raw);
    /**
     * @brief Format a report-stream coverage summary.
     * @param[in] report Inspected enabled/records/cadence/first/last metadata.
     * @return Localized disabled, zero-record, or range/cadence text; no independent schema validation.
     */
    static QString coverage(const QJsonObject &report);
    /**
     * @brief Format engine/source/driver accepted-clock diagnostics.
     * @param[in] metadata Inspected clock document with seconds and SWMM date fields.
     * @return Multiline display text, describing missing sources/driver explicitly.
     */
    static QString clocks(const QJsonObject &metadata);
signals:
    /**
     * @brief Notify views after operation status, capabilities, or catalog state changes.
     */
    void stateChanged();
    /**
     * @brief Publish a user-facing operation/catalog diagnostic.
     * @param[in] message Text describing success/failure; normal operation notices are suppressed after closeProject().
     */
    void notice(const QString &message);
    /**
     * @brief Announce dispatch of a new run/resume.
     * @param[in] label Destination folder basename.
     * @param[in] input Frozen model snapshot path.
     */
    void runStarted(const QString &label, const QString &input);
    /**
     * @brief Publish the worker simulation window.
     * @param[in] startDate SWMM absolute start days.
     * @param[in] endDate SWMM absolute end days.
     */
    void runTimeline(double startDate, double endDate);
    /**
     * @brief Publish the most recently accepted engine boundary.
     * @param[in] acceptedSeconds Seconds elapsed from simulation start.
     */
    void runProgress(double acceptedSeconds);
    /**
     * @brief Publish worker-owned snow balance strings while project is open.
     * @param[in] rows Water volumes in m3 and energy in kJ.
     */
    void runSnowLedgerChanged(const QStringList &rows);
    /**
     * @brief Publish terminal run/resume results after reconciling saved checkpoints.
     * @param[in] result Value-only result including accepted boundary and retained output paths.
     */
    void runFinished(const CheckpointResult &result);

private:
    QString m_catalog, m_status, m_ioEntry, m_recipe, m_runRecipe, m_lineage;
    CheckpointRequest m_context, m_runContext, m_ioContext;
    CheckpointWorker m_io, m_run;
    QList<CheckpointEntry> m_entries;
    quint64 m_publication = 0;
    unsigned long m_capabilities = 0;
    QByteArray m_catalogHash;
    bool m_writable = true, m_closed = false;
    /**
     * @brief Read schema-one catalog and its SHA-256 baseline without touching checkpoint contents.
     * @details Missing catalog yields empty state. Unreadable/invalid data disables writes; invalid entries clear the loaded list. Compatibility always requires new inspection.
     */
    void load();
    /**
     * @brief Atomically replace the catalog only if its bytes still match the loaded/write baseline.
     * @return True after QSaveFile commit and hash update; false with notice() for invalid/unreadable/externally changed catalog or IO failure. Local entry mutations are not rolled back.
     */
    bool persist();
    /**
     * @brief Emit a whole-entry table data change.
     * @param[in] row Current row spanning columns zero through four.
     */
    void changed(int row);
    /**
     * @brief Reconcile a worker save result into the catalog once by exact path.
     * @param[in] s Completed save with accepted date/seconds, or a publication error.
     * @details Successful new paths get a lineage/publication record requiring later inspection; persistence failures keep the local record and saved file.
     */
    void recordSave(const CheckpointSaveResult &s);
    /**
     * @brief Encode compatibility-defining authoring inputs as compact JSON.
     * @param[in] r Request carrying model revision/hash/path, engine, recording profile, source flags, and ownership token.
     * @return Stable compact recipe text; excludes destination and active-path inventories.
     */
    static QString recipe(const CheckpointRequest &r);
};
#endif
