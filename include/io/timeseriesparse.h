/*!
 * \file   timeseriesparse.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.3 — shared CSV / TSV / .dat row parser.
 *
 * Extracted from ObservedCsvRunLayer::parseRow_ so the same parser drives:
 *   - file import (ObservedCsvRunLayer, TimeseriesProvider ExternalFile mode)
 *   - clipboard paste into the TimeseriesEditorDialog grid
 *
 * Supported time formats (tried in order, mirrors legacy + ObservedCsvRunLayer):
 *   - ISO 8601:           yyyy-MM-ddTHH:mm:ss, yyyy-MM-dd HH:mm[:ss], yyyy-MM-dd
 *   - SWMM .dat / US:     MM/dd/yyyy [HH:mm[:ss]]
 *   - European:           dd/MM/yyyy HH:mm
 *   - Hours-since-start:  numeric fallback (only when fallbackBase is finite)
 *
 * Auto-detected delimiters: tab, comma, semicolon (highest-count wins; defaults
 * to comma when none are present).
 */
#ifndef OPENSWMMVIS_IO_TIMESERIESPARSE_H
#define OPENSWMMVIS_IO_TIMESERIESPARSE_H

#include <QChar>
#include <QString>

#include <vector>

namespace openswmmvis::io {

/*!
 * \brief Guess the per-row delimiter from a sample line.
 * \details Counts tab / comma / semicolon occurrences in the line; the
 *          highest non-zero count wins. Ties favour tab over comma over
 *          semicolon. Defaults to comma when nothing matches.
 */
QChar guessDelimiter(const QString& sampleLine);

/*!
 * \brief Try to parse \a s as a timestamp into a SWMM Julian date.
 * \param s             The raw cell text (may contain leading/trailing whitespace).
 * \param jOut          On success, the SWMM Julian date (days since 1899-12-30).
 * \param fallbackBase  When all dated formats fail, treat \a s as a number of
 *                      hours since this base Julian date. Pass NaN to disable
 *                      the numeric fallback.
 * \returns \c true on success.
 */
bool tryParseTimestamp(const QString& s, double& jOut, double fallbackBase);

/*!
 * \brief Parse one delimited row into a timestamp + N value cells.
 * \param line                          The raw input line (no trailing newline).
 * \param delim                         Delimiter from guessDelimiter().
 * \param timeJulianOut                 On success, SWMM Julian date for cell 0.
 * \param valuesOut                     Filled with one double per value column;
 *                                      empty / non-numeric cells become NaN.
 * \param hoursSinceStartFallbackBase   Forwarded to tryParseTimestamp().
 * \returns \c true iff the timestamp parsed. Value cells may be NaN.
 */
bool parseRow(const QString& line,
              QChar delim,
              double& timeJulianOut,
              std::vector<double>& valuesOut,
              double hoursSinceStartFallbackBase);

/*!
 * \brief State carried across lines when reading a SWMM .dat timeseries file.
 * \details The .dat format permits 2-token lines (`<time> <value>`) that use
 *          the *most recently parsed* date, plus 3-token lines
 *          (`<date> <time> <value>`) that re-anchor it. The cached date is
 *          tracked here so the caller can drive a per-line parse loop.
 */
struct SwmmDatState
{
    /*! \brief SWMM Julian date used as the anchor for 2-token lines (the
     *  whole-day part). Initialise to a sensible base before parsing the
     *  first line — e.g. `dateTimeToSwmmJulian(QDateTime(QDate(2000,1,1),
     *  QTime(0,0), Qt::UTC))`. Updated when a 3-token line is read. */
    double lastDateJulian = 0.0;

    /*! \brief Set to true once a 3-token line has been parsed. Lets the
     *  caller distinguish "anchor came from the file" vs "anchor was the
     *  GUI's default" for UI hints. */
    bool   anyDateSeen    = false;
};

/*!
 * \brief Parse one line of a SWMM .dat timeseries file.
 * \details Mirrors the engine's `table_parseFileLine` (legacy/engine/table.c):
 *          - Whitespace and/or comma separated tokens
 *          - Lines starting with `;` are comments (rejected)
 *          - Empty lines are rejected
 *          - 2 tokens → `<time> <value>` where \p time is either `HH:MM[:SS]`
 *            or numeric hours; value's date is \c state.lastDateJulian
 *          - 3 tokens → `<date> <time> <value>`; the parsed date replaces
 *            \c state.lastDateJulian for subsequent lines
 *          - No header / column-name parsing — .dat is data-only by spec
 * \param line             Raw input line (no trailing newline).
 * \param state            In/out cache of the most recently parsed date.
 * \param timeJulianOut    On success, SWMM Julian date for this row.
 * \param valueOut         On success, the value at this row.
 * \returns \c true iff the line yielded a usable (time, value) pair.
 */
bool parseSwmmDatLine(const QString& line,
                      SwmmDatState&  state,
                      double&        timeJulianOut,
                      double&        valueOut);

} // namespace openswmmvis::io

#endif // OPENSWMMVIS_IO_TIMESERIESPARSE_H
