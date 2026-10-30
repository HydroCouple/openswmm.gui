/**
 * @file      timeseriesundocommands.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.3.1 — QUndoCommand subclasses for TimeseriesProvider.
 *
 * Each command operates on a single non-owning TimeseriesProvider*. Commands
 * are pushed onto the project's main `QUndoStack` so a single Cmd-Z unwinds
 * across the editor grid, the editor plot, and any open property panel
 * uniformly (the views all subscribe to the provider's signals and re-render
 * on apply).
 *
 * Drag-edit batching uses the provider's `setValueLive` for in-flight updates
 * (no undo push) and issues a single `SetPointValueCommand` on release. See
 * the dialog's chart subclass in Phase 6.7.3.5.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_TIMESERIES_TIMESERIESUNDOCOMMANDS_H
#define OPENSWMMVIS_TIMESERIES_TIMESERIESUNDOCOMMANDS_H

#include "timeseries/timeseriesprovider.h"

#include <QDateTime>
#include <QString>
#include <QUndoCommand>
#include <QVector>

namespace openswmmvis::timeseries
{

/*! \brief Change one point's value. Time unchanged → monotonicity preserved. */
class SetPointValueCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture the previous sample value and requested replacement.
     * @param[in] index Zero-based existing sample index.
     * @param[in] newValue Authored-unit value applied by redo(), without physical/finite validation.
     * @param[in] provider Non-null borrowed raw provider pointer that must outlive command construction and all undo/redo uses.
     * @param[in] parent Optional QUndoCommand owner.
     */
    SetPointValueCommand(TimeseriesProvider *provider, int index, double newValue,
                         QUndoCommand *parent = nullptr);
    /**
     * @brief Restore the captured numeric value using provider validation/notifications.
     */
    void undo() override;
    /**
     * @brief Apply the replacement numeric value using provider validation/notifications.
     */
    void redo() override;

private:
    TimeseriesProvider *m_provider;
    int m_index;
    double m_oldValue;
    double m_newValue;
};

/*! \brief Change both time and value at an existing index. The provider
 *  refuses if the new time would break strict-ascending order. */
class MovePointCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture old/new timestamp and value for one existing row.
     * @param[in] index Zero-based row index.
     * @param[in] newTime Requested valid absolute timestamp strictly between neighbors.
     * @param[in] newValue Authored-unit replacement value.
     * @details Provider order/index rejections are ignored by undo/redo and can emit mutationRejected().
     * @param[in] provider Non-null borrowed raw provider pointer that must outlive command construction and all undo/redo uses.
     * @param[in] parent Optional QUndoCommand owner.
     */
    MovePointCommand(TimeseriesProvider *provider, int index, QDateTime newTime, double newValue,
                     QUndoCommand *parent = nullptr);
    /**
     * @brief Request the captured old timestamp/value using current neighbor-order validation.
     */
    void undo() override;
    /**
     * @brief Request the new timestamp/value using current neighbor-order validation.
     */
    void redo() override;

private:
    TimeseriesProvider *m_provider;
    int m_index;
    QDateTime m_oldTime;
    double m_oldValue;
    QDateTime m_newTime;
    double m_newValue;
};

/*! \brief Insert one point. Stores its returned index for undo removal. */
class InsertPointCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture an insertion and remember its returned row index.
     * @param[in] time Valid absolute timestamp not already in the series.
     * @param[in] value Authored-unit sample value.
     * @details Undo removes the remembered index, rather than searching by timestamp; callers must maintain compatible command ordering.
     * @param[in] provider Non-null borrowed raw provider pointer that must outlive command construction and all undo/redo uses.
     * @param[in] parent Optional QUndoCommand owner.
     */
    InsertPointCommand(TimeseriesProvider *provider, QDateTime time, double value,
                       QUndoCommand *parent = nullptr);
    /**
     * @brief Remove the last recorded inserted row and reset the recorded index to -1.
     */
    void undo() override;
    /**
     * @brief Insert the captured timestamp/value and record its resulting index, or -1 on failure.
     */
    void redo() override;

    /*! \brief Index of the inserted point after redo. -1 until first redo,
     *  or if redo was rejected.
     * @return Most recent successful insertion row, or -1 before first redo, after undo, or after rejected insertion.
     */
    int insertedIndex() const noexcept { return m_lastInsertedIndex; }

private:
    TimeseriesProvider *m_provider;
    QDateTime m_time;
    double m_value;
    int m_lastInsertedIndex = -1;
};

/*! \brief Delete a set of points. Stores (time, value) for each so undo can
 *  reinstate them. */
class DeletePointsCommand : public QUndoCommand
{
public:
    /**
     * @brief Snapshot selected samples at construction for reversible removal.
     * @param[in] indices Zero-based row indices, deduplicated; invalid rows ignored.
     * @details Redo searches current rows by exact time only; undo reinserts saved samples and ignores collisions.
     * @param[in] provider Non-null borrowed raw provider pointer that must outlive command construction and all undo/redo uses.
     * @param[in] parent Optional QUndoCommand owner.
     */
    DeletePointsCommand(TimeseriesProvider *provider, QVector<int> indices,
                        QUndoCommand *parent = nullptr);
    /**
     * @brief Reinsert captured samples in order; duplicate-time failures are ignored.
     */
    void undo() override;
    /**
     * @brief Remove current rows matching captured timestamps, regardless of their current numeric values.
     */
    void redo() override;

private:
    TimeseriesProvider *m_provider;
    QVector<TimeseriesPoint> m_removed; ///< Saved at construction, in time order.
};

/*! \brief Replace the entire point list (used by rotate / scale bulk
 *  transforms). The caller must pre-validate that \a newPoints is strictly
 *  ascending; the provider also validates and will refuse otherwise. */
class BulkTransformCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture full before/after point arrays for a bulk transformation.
     * @param[in] newPoints Complete valid strictly increasing timestamp array, with authored-unit values.
     * @param[in] description User-visible undo text, or empty for a default bulk-transform label.
     * @details Provider validation applies at each undo/redo; rejection does not update points. Relative metadata is reconciled rather than independently snapshotted.
     * @param[in] provider Non-null borrowed raw provider pointer that must outlive command construction and all undo/redo uses.
     * @param[in] parent Optional QUndoCommand owner.
     */
    BulkTransformCommand(TimeseriesProvider *provider, QVector<TimeseriesPoint> newPoints,
                         QString description, QUndoCommand *parent = nullptr);
    /**
     * @brief Replace the full list with captured old samples, reconciling relative-prefix state.
     */
    void undo() override;
    /**
     * @brief Replace the full list with the requested transformed samples.
     */
    void redo() override;

private:
    TimeseriesProvider *m_provider;
    QVector<TimeseriesPoint> m_oldPoints;
    QVector<TimeseriesPoint> m_newPoints;
};

/*! \brief Rename the Tseries. Cascading reference rewrites in dependent
 *  objects (inflows, rain gages, controls) are wired by the registry
 *  layer — see Phase 6.7.3.2. */
class RenameTimeseriesCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture a local provider-only identifier change.
     * @param[in] newName Name copied verbatim without registry collision validation.
     * @details Calls provider setName(), so name lookup signals run but registry engine-first rename/reference cascading is not invoked.
     * @param[in] provider Non-null borrowed raw provider pointer that must outlive command construction and all undo/redo uses.
     * @param[in] parent Optional QUndoCommand owner.
     */
    RenameTimeseriesCommand(TimeseriesProvider *provider, QString newName,
                            QUndoCommand *parent = nullptr);
    /**
     * @brief Restore the captured local name through provider setName().
     */
    void undo() override;
    /**
     * @brief Apply the replacement local name through provider setName().
     */
    void redo() override;

private:
    TimeseriesProvider *m_provider;
    QString m_oldName;
    QString m_newName;
};

/*! \brief Flip the source mode (Inline ↔ ExternalFile ↔ GeopackageObserved).
 *  Storage migration (reading file rows into inline points, writing inline
 *  points to gpkg) is the source-mode-card's responsibility — this command
 *  records only the mode flip. */
class ChangeSourceModeCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture a source-mode flag change without data migration.
     * @param[in] newMode New local persistence mode.
     * @param[in] provider Non-null borrowed raw provider pointer that must outlive command construction and all undo/redo uses.
     * @param[in] parent Optional QUndoCommand owner.
     */
    ChangeSourceModeCommand(TimeseriesProvider *provider, TimeseriesProvider::SourceMode newMode,
                            QUndoCommand *parent = nullptr);
    /**
     * @brief Restore the previous source-mode flag without migrating data.
     */
    void undo() override;
    /**
     * @brief Apply the replacement source-mode flag without migrating data.
     */
    void redo() override;

private:
    TimeseriesProvider *m_provider;
    TimeseriesProvider::SourceMode m_oldMode;
    TimeseriesProvider::SourceMode m_newMode;
};

/*! \brief Switch the series between Absolute (dated) and Relative
 *  (elapsed-from-simulation-start) time modes. The prior state is captured
 *  exactly — count, anchor and the sticky all-relative intent — so undoing
 *  from a loaded Mixed series restores Mixed, not merely Absolute. */
class SetTimeModeCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture relative-prefix count, anchor, and derived all-relative state before a mode switch.
     * @param[in] newMode Absolute or Relative; requesting Mixed is ignored by the provider.
     * @param[in] anchorForRelative Valid new absolute anchor for Relative, or invalid to retain the existing anchor.
     * @details Points remain absolute timestamps; undo restores captured prefix/anchor intent via setRelativeInfo().
     * @param[in] provider Non-null borrowed raw provider pointer that must outlive command construction and all undo/redo uses.
     * @param[in] parent Optional QUndoCommand owner.
     */
    SetTimeModeCommand(TimeseriesProvider *provider, TimeseriesProvider::TimeMode newMode,
                       QDateTime anchorForRelative, QUndoCommand *parent = nullptr);
    /**
     * @brief Restore captured relative-prefix count/anchor and the recorded all-relative state.
     */
    void undo() override;
    /**
     * @brief Apply the requested time mode without shifting absolute sample timestamps.
     */
    void redo() override;

private:
    TimeseriesProvider *m_provider;
    int m_oldCount;
    QDateTime m_oldAnchor;
    bool m_oldAllRelative;
    TimeseriesProvider::TimeMode m_newMode;
    QDateTime m_anchor;
};

} // namespace openswmmvis::timeseries

#endif // OPENSWMMVIS_TIMESERIES_TIMESERIESUNDOCOMMANDS_H
