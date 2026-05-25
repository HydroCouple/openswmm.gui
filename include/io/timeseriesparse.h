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

} // namespace openswmmvis::io

#endif // OPENSWMMVIS_IO_TIMESERIESPARSE_H
