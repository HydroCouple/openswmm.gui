// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      summarytables.h
 * @brief     Report summary tables saved beside a run's output (review G-08).
 * @details   After a run ends the runner reads the engine's summary tables (Node Depth, Outfall
 *            Loading, Flow Classification, Pumping, LID Performance, ...; swmm_summary_*) before
 *            the engine closes, and saves them as `<output>.summary.json`. The Statistics
 *            Dashboard loads that file. Values are in the model's display units, exactly as the
 *            report computes them; a value the report leaves blank is NaN (null in the file).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include <QList>
#include <QString>

#include <openswmm/engine/openswmm_engine.h>

#include <optional>

namespace openswmmvis::results
{

/// Object kinds of a summary row (the engine's SWMM_SUMMARY_OBJECT_* values).
enum SummaryObject : int { SummarySystem = -1, SummaryNode = 0, SummaryLink = 1, SummarySubcatch = 2 };

struct SummaryColumn {
    QString name;  ///< report heading, e.g. "Maximum Total Inflow"
    QString units; ///< report unit word, e.g. "CMS", "%", "days"; empty when dimensionless
};

struct SummaryRow {
    int objectType = -1;  ///< SWMM_SUMMARY_OBJECT_*: -1 system, 0 node, 1 link, 2 subcatchment
    int objectIndex = -1; ///< engine index at the time of the run
    QString objectName;   ///< object name, or "System" for a total row
    QString label;        ///< node/link type, or the LID control name
    QList<double> values; ///< one per column; NaN where the report prints nothing
};

struct SummaryTable {
    int id = -1;   ///< SWMM_SummaryTable value
    QString title; ///< e.g. "Outfall Loading Summary"
    QList<SummaryColumn> columns;
    QList<SummaryRow> rows;
};

struct SummaryTables {
    QList<SummaryTable> tables; ///< in report order; tables without rows are kept
};

/** @return `<outputPath>.summary.json`. */
QString summaryTablesPath(const QString &outputPath);

/**
 * @brief Read every summary table from an engine whose run has ended.
 * @details Built against an engine whose header lacks the summary-table API
 *   (OPENSWMM_HAS_SUMMARY_TABLES), this returns nullopt and says so.
 * @param[in] engine Borrowed engine in ENDED (before close).
 * @param[out] error Why nothing could be read (for example the engine refused the request).
 * @return The tables, or nullopt with @p error set.
 */
std::optional<SummaryTables> captureSummaryTables(SWMM_Engine engine, QString *error);

/** @brief Write @p tables as JSON in one step (QSaveFile). @return False with @p error set. */
bool saveSummaryTables(const SummaryTables &tables, const QString &path, QString *error);

/** @brief Read a file written by saveSummaryTables(). @return nullopt with @p error set. */
std::optional<SummaryTables> loadSummaryTables(const QString &path, QString *error);

} // namespace openswmmvis::results
