/*!
 * \file   timeseriesundocommands.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.3.1 — QUndoCommand subclasses for TimeseriesProvider.
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
 */
#ifndef OPENSWMMVIS_TIMESERIES_TIMESERIESUNDOCOMMANDS_H
#define OPENSWMMVIS_TIMESERIES_TIMESERIESUNDOCOMMANDS_H

#include "timeseries/timeseriesprovider.h"

#include <QDateTime>
#include <QString>
#include <QUndoCommand>
#include <QVector>

namespace openswmmvis::timeseries {

/*! \brief Change one point's value. Time unchanged → monotonicity preserved. */
class SetPointValueCommand : public QUndoCommand
{
public:
    SetPointValueCommand(TimeseriesProvider *provider,
                         int index,
                         double newValue,
                         QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    TimeseriesProvider *m_provider;
    int    m_index;
    double m_oldValue;
    double m_newValue;
};

/*! \brief Change both time and value at an existing index. The provider
 *  refuses if the new time would break strict-ascending order. */
class MovePointCommand : public QUndoCommand
{
public:
    MovePointCommand(TimeseriesProvider *provider,
                     int index,
                     QDateTime newTime,
                     double newValue,
                     QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    TimeseriesProvider *m_provider;
    int       m_index;
    QDateTime m_oldTime;
    double    m_oldValue;
    QDateTime m_newTime;
    double    m_newValue;
};

/*! \brief Insert one point. Stores the time so undo can locate the inserted row. */
class InsertPointCommand : public QUndoCommand
{
public:
    InsertPointCommand(TimeseriesProvider *provider,
                       QDateTime time,
                       double value,
                       QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

    /*! \brief Index of the inserted point after redo. -1 until first redo,
     *  or if redo was rejected. */
    int insertedIndex() const noexcept { return m_lastInsertedIndex; }

private:
    TimeseriesProvider *m_provider;
    QDateTime m_time;
    double    m_value;
    int       m_lastInsertedIndex = -1;
};

/*! \brief Delete a set of points. Stores (time, value) for each so undo can
 *  reinstate them. */
class DeletePointsCommand : public QUndoCommand
{
public:
    DeletePointsCommand(TimeseriesProvider *provider,
                        QVector<int> indices,
                        QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    TimeseriesProvider          *m_provider;
    QVector<TimeseriesPoint>     m_removed;   ///< Saved at construction, in time order.
};

/*! \brief Replace the entire point list (used by rotate / scale bulk
 *  transforms). The caller must pre-validate that \a newPoints is strictly
 *  ascending; the provider also validates and will refuse otherwise. */
class BulkTransformCommand : public QUndoCommand
{
public:
    BulkTransformCommand(TimeseriesProvider *provider,
                         QVector<TimeseriesPoint> newPoints,
                         QString description,
                         QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    TimeseriesProvider          *m_provider;
    QVector<TimeseriesPoint>     m_oldPoints;
    QVector<TimeseriesPoint>     m_newPoints;
};

/*! \brief Rename the Tseries. Cascading reference rewrites in dependent
 *  objects (inflows, rain gages, controls) are wired by the registry
 *  layer — see Phase 6.7.3.2. */
class RenameTimeseriesCommand : public QUndoCommand
{
public:
    RenameTimeseriesCommand(TimeseriesProvider *provider,
                            QString newName,
                            QUndoCommand *parent = nullptr);
    void undo() override;
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
    ChangeSourceModeCommand(TimeseriesProvider *provider,
                            TimeseriesProvider::SourceMode newMode,
                            QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    TimeseriesProvider                  *m_provider;
    TimeseriesProvider::SourceMode       m_oldMode;
    TimeseriesProvider::SourceMode       m_newMode;
};

/*! \brief Switch the series between Absolute (dated) and Relative
 *  (elapsed-from-simulation-start) time modes. The prior state is captured
 *  exactly — count, anchor and the sticky all-relative intent — so undoing
 *  from a loaded Mixed series restores Mixed, not merely Absolute. */
class SetTimeModeCommand : public QUndoCommand
{
public:
    SetTimeModeCommand(TimeseriesProvider *provider,
                       TimeseriesProvider::TimeMode newMode,
                       QDateTime anchorForRelative,
                       QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    TimeseriesProvider              *m_provider;
    int                              m_oldCount;
    QDateTime                        m_oldAnchor;
    bool                             m_oldAllRelative;
    TimeseriesProvider::TimeMode     m_newMode;
    QDateTime                        m_anchor;
};

} // namespace openswmmvis::timeseries

#endif // OPENSWMMVIS_TIMESERIES_TIMESERIESUNDOCOMMANDS_H
