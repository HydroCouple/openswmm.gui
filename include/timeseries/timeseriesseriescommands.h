/*!
 * \file   timeseriesseriescommands.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QUndoCommands that create, replace, and delete WHOLE time series.
 *
 * \details Separate from timeseriesundocommands.h on purpose. Those commands
 *          mutate one already-existing TimeseriesProvider and nothing else, so
 *          they compile as a leaf against Qt alone — which is what
 *          tests/unit/test_timeseries_undocommands.cpp relies on. Whole-series
 *          lifecycle needs the registry AND the engine (a series must exist in
 *          the engine before a rain gage can point at it), so it lives here
 *          rather than dragging that dependency into the leaf.
 *
 *          Three constraints shape these commands:
 *
 *          1. They key on the series NAME, never on a TimeseriesProvider*.
 *             TimeseriesRegistry::remove() uses deleteLater(), so a stored
 *             pointer can dangle before undo runs.
 *          2. TimeseriesRegistry::remove() drops the provider but does NOT
 *             delete the engine's table, and saveToEngine() never deletes rows
 *             either. Undoing a create must therefore delete the engine row
 *             explicitly, or the series survives invisibly and reappears in the
 *             written INP.
 *          3. Every redo() must tolerate re-entry: QUndoStack::beginMacro runs
 *             a child's redo() at push time, and again on each later redo.
 */

#ifndef OPENSWMMVIS_TIMESERIES_TIMESERIESSERIESCOMMANDS_H
#define OPENSWMMVIS_TIMESERIES_TIMESERIESSERIESCOMMANDS_H

#include "timeseries/timeseriesprovider.h"

#include <QString>
#include <QUndoCommand>
#include <QVector>

namespace openswmmvis::timeseries {

/*! \brief Create an inline series and flush it to the engine. */
class AddTimeseriesCommand : public QUndoCommand
{
public:
    /*! \param layerHandle Opaque SWMMModelLayer*, supplying both the registry
     *         and the engine handle. Type-erased so this header stays free of
     *         the layer graph. */
    AddTimeseriesCommand(void *layerHandle,
                         QString name,
                         QVector<TimeseriesPoint> points,
                         QString unitsLabel,
                         QString description,
                         QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    void                    *m_layer = nullptr;
    QString                  m_name;
    QVector<TimeseriesPoint> m_points;
    QString                  m_unitsLabel;
    QString                  m_description;
    bool                     m_preexisting = false;  ///< Someone else owns it.
};

/*! \brief Replace every point (and the description) of an existing series. */
class SetTimeseriesPointsCommand : public QUndoCommand
{
public:
    SetTimeseriesPointsCommand(void *layerHandle,
                               QString name,
                               QVector<TimeseriesPoint> newPoints,
                               QString newDescription,
                               QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    void apply(const QVector<TimeseriesPoint> &pts, const QString &desc);

    void                    *m_layer = nullptr;
    QString                  m_name;
    QVector<TimeseriesPoint> m_newPoints, m_oldPoints;
    QString                  m_newDescription, m_oldDescription;
    bool                     m_captured = false;
};

/*! \brief Delete a series from both the registry and the engine. */
class DeleteTimeseriesCommand : public QUndoCommand
{
public:
    DeleteTimeseriesCommand(void *layerHandle,
                            QString name,
                            QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    void                    *m_layer = nullptr;
    QString                  m_name;
    QVector<TimeseriesPoint> m_points;        ///< Snapshot for undo.
    QString                  m_unitsLabel;
    QString                  m_description;
    bool                     m_captured = false;
};

} // namespace openswmmvis::timeseries

#endif // OPENSWMMVIS_TIMESERIES_TIMESERIESSERIESCOMMANDS_H
