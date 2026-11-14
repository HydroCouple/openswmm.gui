/**
 * @file      timeseriesseriescommands.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     QUndoCommands that create, replace, and delete WHOLE time series.
 *
 * @details Separate from timeseriesundocommands.h on purpose. Those commands
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
 *          2. Removal deletes the engine table as well as the provider. The
 *             commands also request engine deletion explicitly before removal;
 *             undo recreation restores sampled values/labels, not engine
 *             reference sites that deletion cleared.
 *          3. Every redo() must tolerate re-entry: QUndoStack::beginMacro runs
 *             a child's redo() at push time, and again on each later redo.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_TIMESERIES_TIMESERIESSERIESCOMMANDS_H
#define OPENSWMMVIS_TIMESERIES_TIMESERIESSERIESCOMMANDS_H

#include "timeseries/timeseriesprovider.h"

#include <QString>
#include <QUndoCommand>
#include <QVector>

namespace openswmmvis::timeseries
{

/*! \brief Create an inline series and flush it to the engine. */
class AddTimeseriesCommand : public QUndoCommand
{
public:
    /*!
     * @brief Capture an inline series for name-keyed creation and deletion.
     * @param[in] points Valid strictly increasing absolute timestamps and authored-unit values.
     * @param[in] unitsLabel Display-only units metadata; no conversion occurs.
     * @param[in] description Series description copied verbatim.
     * @details Redo leaves a preexisting provider alone and marks undo as a no-op. Point-validation or engine-write failures are not reported by this command.
     * @param[in] layerHandle Borrowed SWMMModelLayer pointer erased as void*; the layer must outlive every command use and be accessed on its owning thread. Null makes application a no-op.
     * @param[in] name Series identifier resolved case-insensitively through the layer registry.
     * @param[in] parent Optional QUndoCommand owner.
     */
    AddTimeseriesCommand(void *layerHandle, QString name, QVector<TimeseriesPoint> points,
                         QString unitsLabel, QString description, QUndoCommand *parent = nullptr);
    /**
     * @brief Delete the engine table and current named provider unless redo found it preexisting.
     */
    void undo() override;
    /**
     * @brief Create a new Inline provider with captured metadata/points when the name is unused, then attempt its engine flush.
     */
    void redo() override;

private:
    void *m_layer = nullptr;
    QString m_name;
    QVector<TimeseriesPoint> m_points;
    QString m_unitsLabel;
    QString m_description;
    bool m_preexisting = false; ///< Someone else owns it.
};

/*! \brief Replace every point (and the description) of an existing series. */
class SetTimeseriesPointsCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture a replacement point list/description, deferring old-data capture to first successful lookup on redo.
     * @param[in] newPoints Full replacement valid timestamp array; provider validation may reject it.
     * @param[in] newDescription Replacement description, applied even if point replacement is rejected.
     * @details Captures/restores points and description only, not source mode, file metadata, units, or relative intent.
     * @param[in] layerHandle Borrowed SWMMModelLayer pointer erased as void*; the layer must outlive every command use and be accessed on its owning thread. Null makes application a no-op.
     * @param[in] name Series identifier resolved case-insensitively through the layer registry.
     * @param[in] parent Optional QUndoCommand owner.
     */
    SetTimeseriesPointsCommand(void *layerHandle, QString name, QVector<TimeseriesPoint> newPoints,
                               QString newDescription, QUndoCommand *parent = nullptr);
    /**
     * @brief Restore captured points/description only after an earlier redo captured an existing provider.
     */
    void undo() override;
    /**
     * @brief Capture old data once when an existing provider is first found, then apply replacement data.
     */
    void redo() override;

private:
    /**
     * @brief Replace the currently named provider description/points and attempt one-provider engine flush.
     * @param[in] pts Point list forwarded to setAllPoints().
     * @param[in] desc Description assigned before point validation.
     * @details Missing layer/registry/provider is a no-op. Rejected points do not roll back the description, and flush results are ignored.
     */
    void apply(const QVector<TimeseriesPoint> &pts, const QString &desc);

    void *m_layer = nullptr;
    QString m_name;
    QVector<TimeseriesPoint> m_newPoints, m_oldPoints;
    QString m_newDescription, m_oldDescription;
    bool m_captured = false;
};

/*! \brief Delete a series from both the registry and the engine. */
class DeleteTimeseriesCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture a series by name for engine/local deletion and partial metadata recreation.
     * @details First redo snapshots points, units label, and description. Undo recreates it as Inline only when the name is free; source/file metadata, relative intent, and deleted engine reference sites are not restored.
     * @param[in] layerHandle Borrowed SWMMModelLayer pointer erased as void*; the layer must outlive every command use and be accessed on its owning thread. Null makes application a no-op.
     * @param[in] name Series identifier resolved case-insensitively through the layer registry.
     * @param[in] parent Optional QUndoCommand owner.
     */
    DeleteTimeseriesCommand(void *layerHandle, QString name, QUndoCommand *parent = nullptr);
    /**
     * @brief Recreate captured data as an Inline series if the name remains free, then attempt a one-provider flush.
     */
    void undo() override;
    /**
     * @brief Capture existing named-provider data once, delete its engine table, and unregister the provider.
     */
    void redo() override;

private:
    void *m_layer = nullptr;
    QString m_name;
    QVector<TimeseriesPoint> m_points; ///< Snapshot for undo.
    QString m_unitsLabel;
    QString m_description;
    bool m_captured = false;
};

} // namespace openswmmvis::timeseries

#endif // OPENSWMMVIS_TIMESERIES_TIMESERIESSERIESCOMMANDS_H
