/**
 * @file      resulttable.h
 * @brief     V6 — tables of result series (Tabular Results) built from
 *            `IRunLayer` series, with units, per-row status and CSV export.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * workplans/GW2D_AND_PIPE_REACTION_RESULTS_VISUALIZATION_PLAN_2026-10-14.md
 * D-V6 / V6: the table is assembled from `SeriesData` the dialog fetched
 * through `IRunLayer` (fixed attributes, species, loads, 2D variables, 2D
 * region aggregates alike), so the table and the comparison plot read the
 * same numbers. Two shapes:
 *   - by object:   one row per object, one column per report time;
 *   - by variable: one row per report time, one column per descriptor.
 * Times are the union of the series' own sample times, so a series that
 * lacks a sample (a rate's first interval, a Missing 2D cell) shows a BLANK
 * cell — never 0 — and the row's Status says what is missing. A series that
 * failed outright keeps its row/column, all blank, with its error as status.
 *
 * CSV / TSV: header row, then a "Units" row, then the data; blank = missing;
 * a final Status column. Values are written at full precision.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_RESULTS_RESULTTABLE_H
#define OPENSWMMVIS_RESULTS_RESULTTABLE_H

#include "plot/irunlayer.h"

#include <QString>
#include <QStringList>
#include <QVector>

class QStandardItemModel;

namespace openswmmvis::results
{

struct ResultTable {
    QString rowHeader;           ///< first-column header ("Object" / "Time")
    QStringList columnHeaders;   ///< value-column headers (no units)
    QStringList columnUnits;     ///< parallel to columnHeaders; empty = none
    QStringList columnNotes;     ///< parallel; header tooltip (how a value is computed)
    QStringList rowLabels;       ///< first-column text per row
    QVector<double> rowTimes;    ///< by-variable: SWMM DateTime per row; else empty
    QVector<QVector<double>> values; ///< [row][column]; NaN = missing
    QStringList rowStatus;       ///< empty = complete

    /** @return Number of rows. */
    int rowCount() const noexcept { return int(rowLabels.size()); }
    /** @return Number of value columns. */
    int columnCount() const noexcept { return int(columnHeaders.size()); }
};

/*! \brief "yyyy-MM-dd HH:mm:ss" for a SWMM DateTime (days since 1899-12-30). */
QString timeLabel(double swmmDateTime);

/*!
 * \brief One row per object, one column per report time.
 * \param objectLabels Row labels, parallel to \p series.
 * \param series Resolved series (any `ok` state).
 * \param units Units of every value column (one descriptor).
 * \param note Optional description of the descriptor (header tooltips).
 */
ResultTable tableByObject(const QStringList &objectLabels,
                          const QVector<plot::SeriesData> &series, const QString &units,
                          const QString &note = {});

/*!
 * \brief One row per report time, one column per series.
 * \param headers Column headers, parallel to \p series.
 * \param units Column units, parallel to \p series.
 * \param notes Optional per-column descriptions (header tooltips).
 */
ResultTable tableByVariable(const QStringList &headers, const QStringList &units,
                            const QVector<plot::SeriesData> &series,
                            const QStringList &notes = {});

/*!
 * \brief Write \p table as delimited text (header, Units row, rows, Status).
 * \return False with \p error set when the file cannot be written.
 */
bool writeDelimited(const ResultTable &table, const QString &path, QChar delimiter,
                    QString *error);

/*!
 * \brief Show \p table in \p model: headers carry units (and the column
 *        notes as tooltips), missing cells are empty items (no EditRole, so
 *        they sort apart from 0), a final Status column; numeric cells carry
 *        the full value as EditRole.
 */
void fillItemModel(const ResultTable &table, QStandardItemModel &model);

} // namespace openswmmvis::results

#endif // OPENSWMMVIS_RESULTS_RESULTTABLE_H
