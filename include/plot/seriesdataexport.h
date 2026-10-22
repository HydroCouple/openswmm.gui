/**
 * @file      seriesdataexport.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Time-series data export (CSV / SWMM .dat) for the Comparison
 *         Plot Dialog.
 *
 * Pure model-layer code (Qt Core only — no widgets) so the formats are
 * unit-testable. Both formats round-trip through the app's own readers:
 *   - CSV uses ISO "yyyy-MM-dd HH:mm:ss" timestamps, which
 *     `ObservedCsvRunLayer` (via io::parseRow) re-loads directly.
 *   - .dat uses the SWMM external time-series format
 *     ("MM/dd/yyyy HH:mm:ss value"), readable by the engine's TIMESERIES
 *     FILE mechanism and by `ObservedCsvRunLayer`.
 *
 * A .dat file carries exactly one series, so exporting N > 1 series to
 * .dat fans out to one file per series with the series name appended to
 * the chosen base name.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PLOT_SERIESDATAEXPORT_H
#define OPENSWMMVIS_PLOT_SERIESDATAEXPORT_H

#include <QString>
#include <QStringList>
#include <QVector>

#include <vector>

namespace openswmmvis::plot
{

/*! \brief One series to export — name becomes the CSV column header /
 *  .dat comment line. Times are SWMM Julian doubles (see
 *  core/swmmdatetime.h). */
struct ExportSeries {
    QString name;
    std::vector<double> timesJulian;
    std::vector<double> values; ///< Same length as timesJulian.
};

/*! \brief Build wide-format CSV text: column 0 = ISO timestamp, one column
 *  per series. Rows are the sorted union of all series' timestamps; a cell
 *  is empty where its series has no sample (or a non-finite value) at that
 *  time.
 * @param[in] series Column order and names to export; each series uses only the shorter of its time/value vectors.
 * @return CSV text with a Date/Time header even for no series. Invalid dates are skipped; converted UTC timestamps form sorted rows, with later duplicate samples overwriting earlier values in their column. Numeric values use 12 significant digits; absent/nonfinite cells are blank.
 * @note Timestamp conversion is SWMM-date conversion, not astronomical Julian conversion. Names are quoted for comma, quote or newline; units are not converted or included automatically.
 */
QString seriesToCsvText(const QVector<ExportSeries> &series);

/*! \brief Build SWMM .dat text for a single series: ";name" comment line
 *  followed by "MM/dd/yyyy HH:mm:ss value" rows. Non-finite values are
 *  skipped (SWMM cannot parse them).
 * @param[in] series Input order is preserved over the common time/value prefix. The name is emitted verbatim after a semicolon.
 * @return Comment plus UTC date/time rows at second resolution and values with 12 significant digits; invalid dates and nonfinite values are omitted. No sorting or name-line escaping occurs.
 */
QString seriesToDatText(const ExportSeries &series);

/*! \brief Write CSV to \p path. Returns false and populates \p errorOut
 *  on failure.
 * @param[in] path Destination opened for text writing and truncated if present; parent directories must already exist.
 * @param[in] series Nonempty series list; a list of empty series still writes headers.
 * @param[out] errorOut Optional diagnostic destination, assigned for an empty list or open failure; unchanged on success.
 * @return False for an empty list or file-open failure, otherwise true. Stream/close errors after opening are not checked. This direct write is not an atomic replacement.
 */
bool writeSeriesCsv(const QString &path, const QVector<ExportSeries> &series,
                    QString *errorOut = nullptr);

/*! \brief Write .dat file(s). A single series writes exactly \p path;
 *  multiple series write one file each, named
 *  "<base>_<sanitized series name>.dat" beside \p path. Returns the list
 *  of files written (empty on failure, with \p errorOut populated).
 * @param[in] path Exact destination for one series, or directory/base-name template for several; directories must exist.
 * @param[in] series Nonempty series list in output order. Sanitized-name collisions are not disambiguated and can overwrite a prior file.
 * @param[out] errorOut Optional empty-list/open-failure diagnostic; unchanged on success.
 * @return Destination paths on success, or empty on failure. Earlier files remain after a later failure; writes truncate existing files and do not check post-open stream errors.
 */
QStringList writeSeriesDat(const QString &path, const QVector<ExportSeries> &series,
                           QString *errorOut = nullptr);

/*! \brief Reduce a series name to a filesystem-safe token for the
 *  multi-series .dat fan-out (non [A-Za-z0-9_-] runs collapse to '_').
 * @param[in] name Arbitrary display label; not interpreted as a path.
 * @return ASCII letters/digits/hyphens/underscores with other runs replaced by one underscore, outer underscores removed, and series used when nothing remains. No uniqueness or platform-reserved-name check is performed.
 */
QString sanitizedFileToken(const QString &name);

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_SERIESDATAEXPORT_H
