/*!
 * \file   seriesdataexport.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Time-series data export (CSV / SWMM .dat) for the Comparison
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
 */
#ifndef OPENSWMMVIS_PLOT_SERIESDATAEXPORT_H
#define OPENSWMMVIS_PLOT_SERIESDATAEXPORT_H

#include <QString>
#include <QStringList>
#include <QVector>

#include <vector>

namespace openswmmvis::plot {

/*! \brief One series to export — name becomes the CSV column header /
 *  .dat comment line. Times are SWMM Julian doubles (see
 *  core/swmmdatetime.h). */
struct ExportSeries {
    QString             name;
    std::vector<double> timesJulian;
    std::vector<double> values;        ///< Same length as timesJulian.
};

/*! \brief Build wide-format CSV text: column 0 = ISO timestamp, one column
 *  per series. Rows are the sorted union of all series' timestamps; a cell
 *  is empty where its series has no sample (or a non-finite value) at that
 *  time. */
QString seriesToCsvText(const QVector<ExportSeries>& series);

/*! \brief Build SWMM .dat text for a single series: ";name" comment line
 *  followed by "MM/dd/yyyy HH:mm:ss value" rows. Non-finite values are
 *  skipped (SWMM cannot parse them). */
QString seriesToDatText(const ExportSeries& series);

/*! \brief Write CSV to \p path. Returns false and populates \p errorOut
 *  on failure. */
bool writeSeriesCsv(const QString& path,
                    const QVector<ExportSeries>& series,
                    QString* errorOut = nullptr);

/*! \brief Write .dat file(s). A single series writes exactly \p path;
 *  multiple series write one file each, named
 *  "<base>_<sanitized series name>.dat" beside \p path. Returns the list
 *  of files written (empty on failure, with \p errorOut populated). */
QStringList writeSeriesDat(const QString& path,
                           const QVector<ExportSeries>& series,
                           QString* errorOut = nullptr);

/*! \brief Reduce a series name to a filesystem-safe token for the
 *  multi-series .dat fan-out (non [A-Za-z0-9_-] runs collapse to '_'). */
QString sanitizedFileToken(const QString& name);

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_SERIESDATAEXPORT_H
