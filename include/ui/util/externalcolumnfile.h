/*!
 * \file   externalcolumnfile.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Shared column enumeration + single-column reader for external
 *         multi-column series files (CSV / TSV / PCSWMM .tsf).
 *
 * Extracted from TimeseriesEditorDialog::loadExternalFileIntoProvider_'s
 * inline header-vs-data probe so the timeseries dialog and the rain-gage
 * editors share one implementation (spec §4 task 1,
 * workplans/MULTICOLUMN_SERIES_SINGLE_READ_2026-08-17.md). Format detection
 * mirrors the engine's MultiColumnSeriesFile.cpp:
 *   - TSF: first content line begins "IDs:" (case-insensitive) → tab
 *     delimited; line-1 tokens 1..N name the columns; lines 2 (parameter
 *     row) and 3 (units row) carry no data and are skipped.
 *   - CSV/TSV: delimiter sniffed from the header row (quoted regions
 *     skipped); when the first content line parses as a data row, "col_N"
 *     display names are fabricated — but that line is still spent as the
 *     header row, as the engine spends it, so the two agree on which rows
 *     are data.
 * QtCore-only — safe for unit tests without widgets. The pure string /
 * datetime rules live in externalcolumnfilecore.h.
 */
#ifndef OPENSWMMVIS_UI_UTIL_EXTERNALCOLUMNFILE_H
#define OPENSWMMVIS_UI_UTIL_EXTERNALCOLUMNFILE_H

#include <QString>
#include <QStringList>
#include <QVector>

namespace openswmmvis::ui {

/*! \brief One parsed sample: SWMM DateTime (days since 1899-12-30) + value. */
struct ExternalSeriesPoint {
    double timeJulian = 0.0;
    double value      = 0.0;
};

/*! \brief Enumerate the value-column names of \a path (time column excluded).
 *
 *  TSF → the IDs row's tokens; CSV/TSV → header cells, or fabricated
 *  "col_N" names when the first content line is itself a data row. Returns
 *  an empty list for unreadable files and for headerless whitespace formats
 *  (SWMM .dat / standard rain files) — those have no columns to pick.
 *  \param errorOut        Optional; receives a human-readable reason on failure.
 *  \param fabricatedOut   Optional; true when the names are fabricated
 *                         (headerless file). Such a name cannot be resolved
 *                         by the engine, so callers must not persist it in a
 *                         "path:col" token — only the first data column of a
 *                         headerless file is addressable, via an EMPTY
 *                         selector (review B-4 / risk R1). */
QStringList readHeaders(const QString& path, QString* errorOut = nullptr,
                       bool* fabricatedOut = nullptr);

/*! \brief Read one column of \a path into \a pointsOut.
 *
 *  \param columnSelector  Column name; matched case-insensitively against
 *                         readHeaders() names (engine parity). Empty selects
 *                         the first data column; a non-empty name that
 *                         matches nothing FAILS (the engine errors on it —
 *                         review B-3 — so no silent first-column fallback).
 *                         A headerless file resolves no name at all: only an
 *                         empty selector reads it.
 *  \param pointsOut       Cleared, then filled sorted ascending in time.
 *                         Cells that are missing or non-numeric are skipped
 *                         (mirrors the engine's copy-out of NaN cells). For a
 *                         headerless file the first content line is consumed
 *                         as the header row, exactly as the engine does, so
 *                         both agree on which rows are data.
 *  \param headersOut      Optional; receives the same list readHeaders()
 *                         returns, from the same single pass — populated even
 *                         when the column selector fails to resolve.
 *  \param errorOut        Optional; human-readable reason on failure.
 *  \param fabricatedOut   Optional; see readHeaders().
 *  \returns the number of points loaded, or -1 when the file cannot be
 *           opened / has no usable header / the selector matched no column. */
int readColumn(const QString& path, const QString& columnSelector,
               QVector<ExternalSeriesPoint>& pointsOut,
               QStringList* headersOut = nullptr,
               QString* errorOut = nullptr,
               bool* fabricatedOut = nullptr);

/*! \brief The column selector \a path should carry, given \a current.
 *
 *  Answers the "the file just changed — now what?" question for every editor
 *  that binds a column to a path, so the timeseries dialog, the rain-gage
 *  property row and the rain-gage attribute cell all reconcile identically:
 *    - \a current still names a column of the new file → \a current, unchanged
 *      (case preserved as the file spells it, so the stored token matches the
 *      file);
 *    - the file has real headers but \a current names none of them (including
 *      the empty selector) → its FIRST value column, which is what an editor
 *      would otherwise leave the user to pick by hand;
 *    - the file has no usable headers — headerless, whitespace-delimited or
 *      unreadable → EMPTY, because a fabricated "col_N" is not resolvable by
 *      the engine and a stale name from the previous file would fail the run
 *      (review B-4 / risk R1).
 *
 *  Callers compare the result with \a current and write only on a change, so a
 *  path edit that leaves the binding valid costs nothing. */
QString reconcileColumnSelector(const QString& path, const QString& current);

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_UTIL_EXTERNALCOLUMNFILE_H
