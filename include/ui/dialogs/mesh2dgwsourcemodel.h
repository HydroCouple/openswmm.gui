/**
 * @file      mesh2dgwsourcemodel.h
 * @brief     Staged models for the `[GW_SOURCES]` list and the species terms of one source.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * `Mesh2DGwSourceModel` lists every named source (`*`, TAG, CELL and XY rows) with its location,
 * flow, flow series and scale; `Mesh2DGwSourceTermModel` views the CONC/MASS species terms of the
 * selected source and edits them through the source model, so both tables stay in step (MVC).
 *
 * Reading and the per-source write primitive are shared with the per-cell assignment path
 * (`assignment::readGroundwaterSources`, `assignment::writeGroundwaterSource`). A commit only
 * touches sources that changed. An XY source keeps its `XY x y` form: its X and Y are edited
 * directly, the engine places it in the cell with the nearest centroid (shown read-only), and
 * every rewrite re-applies the XY location (swmm_gw_source_xy_set), including a restore after
 * a failed commit. The Scope column switches a source between XY, CELL, TAG and `*`.
 *
 * Flow is total m³/s (positive injects); CONC terms are in the species' units and MASS terms in
 * its mass per second.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_MESH2DGWSOURCEMODEL_H
#define OPENSWMMVIS_UI_DIALOGS_MESH2DGWSOURCEMODEL_H

#include "assignment/groundwatertransportassignment.h"

#include <QAbstractTableModel>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <openswmm/engine/openswmm_engine.h>

namespace openswmmvis::ui
{

class Mesh2DGwSourceModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        ColName = 0,
        ColScope,
        ColTarget,
        ColX,
        ColY,
        ColFlow,
        ColFlowSeries,
        ColScale,
        ColTerms,
        ColCount
    };
    /** Same role id as Mesh2DSectionRowModel::ChoicesRole. */
    static constexpr int ChoicesRole = Qt::UserRole + 1;

    explicit Mesh2DGwSourceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    /**
     * @brief Replace staged sources and baseline with the engine's (XY rows included).
     * @param[in] engine Borrowed engine; null clears the model.
     * @return Empty on success, otherwise the read error.
     */
    QString load(SWMM_Engine engine);
    [[nodiscard]] bool isDirty() const;
    /** Editable engine, unchanged live sources, and complete, unique, in-range drafts. */
    QString validateCommit(SWMM_Engine engine) const;
    /**
     * @brief Apply only the differences: remove dropped names, append new ones and update the
     * changed fields of the rest, then verify the read-back.
     * @return Empty for a clean model or success; otherwise the error after restoring.
     */
    QString commit(SWMM_Engine engine, bool advanceBaseline = true);
    QString restoreLoaded(SWMM_Engine engine) const;
    /** Adopt the committed engine order as the clean baseline. */
    void acceptChanges();

    /** Append a CELL 1 source with a unique name and zero flow; returns its row. */
    int appendSource();
    /** True when the staged row is located by XY coordinates. */
    [[nodiscard]] bool isXy(int row) const;
    [[nodiscard]] QVector<assignment::GroundwaterSpeciesTerm> terms(int row) const;
    /** Replace a row's species terms (emits dataChanged for the Terms column). */
    bool setTerms(int row, const QVector<assignment::GroundwaterSpeciesTerm> &terms);
    /** Enabled groundwater species plus any species already used by a term. */
    [[nodiscard]] QStringList speciesChoices() const;
    /** "(none)" followed by the model's time-series names. */
    [[nodiscard]] QStringList seriesChoices() const;
    void setTagChoices(const QStringList &tags) { m_tags = tags; }

private:
    using Row = assignment::GroundwaterSourceRow;
    QString rowError(const Row &row) const;
    QString transition(SWMM_Engine engine, const QList<Row> &target) const;
    QString noneLabel() const;

    QList<Row> m_rows, m_loaded;
    QVector<assignment::GroundwaterSourceRow> m_written;
    bool m_hasWritten = false;
    QStringList m_species, m_series, m_tags;
    QString m_loadError;
    int m_cellCount = 0;
};

class Mesh2DGwSourceTermModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColSpecies = 0, ColKind, ColValue, ColSeries, ColCount };

    /**
     * @param[in] sources Borrowed source model that owns the terms; must outlive this model or
     * be destroyed first (tracked through QPointer).
     */
    explicit Mesh2DGwSourceTermModel(Mesh2DGwSourceModel *sources, QObject *parent = nullptr);

    /** Show the terms of source @p row (-1 shows none). */
    void setSourceRow(int row);
    [[nodiscard]] int sourceRow() const { return m_row; }
    /** Append a CONC term for the first species not yet on the source; returns its row or -1. */
    int appendTerm();
    bool removeTerm(int term);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    QPointer<Mesh2DGwSourceModel> m_sources;
    int m_row = -1;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_MESH2DGWSOURCEMODEL_H
