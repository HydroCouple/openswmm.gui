/*!
 * \file   timeseriesundocommands.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "timeseries/timeseriesundocommands.h"

#include <QObject>

#include <algorithm>
#include <utility>

namespace openswmmvis::timeseries {

// ─────────────────────────────────────────────────────────────────────────────
// SetPointValueCommand
// ─────────────────────────────────────────────────────────────────────────────

SetPointValueCommand::SetPointValueCommand(TimeseriesProvider *provider,
                                           int index,
                                           double newValue,
                                           QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_provider(provider)
    , m_index(index)
    , m_oldValue(provider && index >= 0 && index < provider->pointCount()
                     ? provider->pointAt(index).value
                     : 0.0)
    , m_newValue(newValue)
{
    setText(QObject::tr("Set timeseries value"));
}

void SetPointValueCommand::undo() { m_provider->setValueAt(m_index, m_oldValue); }
void SetPointValueCommand::redo() { m_provider->setValueAt(m_index, m_newValue); }

// ─────────────────────────────────────────────────────────────────────────────
// MovePointCommand
// ─────────────────────────────────────────────────────────────────────────────

MovePointCommand::MovePointCommand(TimeseriesProvider *provider,
                                   int index,
                                   QDateTime newTime,
                                   double newValue,
                                   QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_provider(provider)
    , m_index(index)
    , m_oldTime(provider && index >= 0 && index < provider->pointCount()
                    ? provider->pointAt(index).time
                    : QDateTime())
    , m_oldValue(provider && index >= 0 && index < provider->pointCount()
                     ? provider->pointAt(index).value
                     : 0.0)
    , m_newTime(std::move(newTime))
    , m_newValue(newValue)
{
    setText(QObject::tr("Move timeseries point"));
}

void MovePointCommand::undo() { m_provider->setPointAt(m_index, m_oldTime, m_oldValue); }
void MovePointCommand::redo() { m_provider->setPointAt(m_index, m_newTime, m_newValue); }

// ─────────────────────────────────────────────────────────────────────────────
// InsertPointCommand
// ─────────────────────────────────────────────────────────────────────────────

InsertPointCommand::InsertPointCommand(TimeseriesProvider *provider,
                                       QDateTime time,
                                       double value,
                                       QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_provider(provider)
    , m_time(std::move(time))
    , m_value(value)
{
    setText(QObject::tr("Insert timeseries point"));
}

void InsertPointCommand::redo()
{
    m_lastInsertedIndex = m_provider->insertPoint(m_time, m_value);
}

void InsertPointCommand::undo()
{
    if (m_lastInsertedIndex < 0) return;
    m_provider->removePointsAt({m_lastInsertedIndex});
    m_lastInsertedIndex = -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// DeletePointsCommand
// ─────────────────────────────────────────────────────────────────────────────

DeletePointsCommand::DeletePointsCommand(TimeseriesProvider *provider,
                                         QVector<int> indices,
                                         QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_provider(provider)
{
    setText(QObject::tr("Delete timeseries points"));

    // Snapshot (time, value) for each surviving index in ascending time order so
    // undo's reinsert calls land them back in monotone-safe positions.
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    m_removed.reserve(indices.size());
    for (int idx : std::as_const(indices)) {
        if (idx < 0 || idx >= provider->pointCount()) continue;
        m_removed.push_back(provider->pointAt(idx));
    }
}

void DeletePointsCommand::redo()
{
    // Map removed time → current index in provider, then bulk-delete.
    QVector<int> idx;
    idx.reserve(m_removed.size());
    for (const TimeseriesPoint& p : m_removed) {
        for (int i = 0; i < m_provider->pointCount(); ++i) {
            if (m_provider->pointAt(i).time == p.time) { idx.push_back(i); break; }
        }
    }
    m_provider->removePointsAt(std::move(idx));
}

void DeletePointsCommand::undo()
{
    for (const TimeseriesPoint& p : std::as_const(m_removed))
        m_provider->insertPoint(p.time, p.value);
}

// ─────────────────────────────────────────────────────────────────────────────
// BulkTransformCommand
// ─────────────────────────────────────────────────────────────────────────────

BulkTransformCommand::BulkTransformCommand(TimeseriesProvider *provider,
                                           QVector<TimeseriesPoint> newPoints,
                                           QString description,
                                           QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_provider(provider)
    , m_oldPoints(provider->points())
    , m_newPoints(std::move(newPoints))
{
    setText(description.isEmpty() ? QObject::tr("Bulk transform") : std::move(description));
}

void BulkTransformCommand::redo() { m_provider->setAllPoints(m_newPoints); }
void BulkTransformCommand::undo() { m_provider->setAllPoints(m_oldPoints); }

// ─────────────────────────────────────────────────────────────────────────────
// RenameTimeseriesCommand
// ─────────────────────────────────────────────────────────────────────────────

RenameTimeseriesCommand::RenameTimeseriesCommand(TimeseriesProvider *provider,
                                                 QString newName,
                                                 QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_provider(provider)
    , m_oldName(provider->name())
    , m_newName(std::move(newName))
{
    setText(QObject::tr("Rename timeseries"));
}

void RenameTimeseriesCommand::redo() { m_provider->setName(m_newName); }
void RenameTimeseriesCommand::undo() { m_provider->setName(m_oldName); }

// ─────────────────────────────────────────────────────────────────────────────
// ChangeSourceModeCommand
// ─────────────────────────────────────────────────────────────────────────────

ChangeSourceModeCommand::ChangeSourceModeCommand(TimeseriesProvider *provider,
                                                 TimeseriesProvider::SourceMode newMode,
                                                 QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_provider(provider)
    , m_oldMode(provider->sourceMode())
    , m_newMode(newMode)
{
    setText(QObject::tr("Change timeseries source"));
}

void ChangeSourceModeCommand::redo() { m_provider->setSourceMode(m_newMode); }
void ChangeSourceModeCommand::undo() { m_provider->setSourceMode(m_oldMode); }

} // namespace openswmmvis::timeseries
