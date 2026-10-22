/**
 * @file      test_comparisonplot_export.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Pins the Comparison Plot data-export formats (plot/seriesdataexport):
 *         wide-format CSV with union-of-timestamps rows, SWMM .dat text,
 *         multi-series .dat filename fan-out, and name sanitization.
 *
 * Output files are written to ./test_comparisonplot_export_output/ (under the
 * test's working directory) so they can be reviewed after a run — not to a
 * temp dir.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "plot/seriesdataexport.h"
#include "core/swmmdatetime.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QtTest/QtTest>

#include <cmath>
#include <limits>

using namespace openswmmvis::plot;
using openswmmvis::core::qDateTimeToSwmmDateTime;

namespace
{

/**
 *
 * @brief Convert a UTC civil fixture timestamp into engine SWMM datetime days.
 * @param y Calendar year.
 * @param mo Month 1..12 for valid input.
 * @param d Valid day of that year/month.
 * @param h Hour 0..23.
 * @param mi Minute 0..59.
 * @param s Second 0..59, default zero.
 * @return qDateTimeToSwmmDateTime result in days, not astronomical Julian-date units
 * despite the helper name. Invalid calendar/clock input follows the conversion
 * helper policy; this wrapper performs no validation or state mutation.
 *
 */
double julianAt(int y, int mo, int d, int h, int mi, int s = 0)
{
    return qDateTimeToSwmmDateTime(QDateTime(QDate(y, mo, d), QTime(h, mi, s), Qt::UTC));
}

} // namespace

class TestComparisonPlotExport : public QObject
{
    Q_OBJECT

private slots:
    /**
     *
     * @brief Select and attempt to create test_comparisonplot_export_output under the current
     * directory.
     * Directory-creation success is not asserted until later checked file operations.
     *
     */
    void initTestCase();

    /**
     *
     * @brief Require wide CSV to contain the sorted four-timestamp union, exact header and empty
     * cells where one series has no sample.
     *
     */
    void csv_unionOfTimestamps();
    /**
     *
     * @brief Require a series header containing a comma to be CSV-quoted.
     *
     */
    void csv_quotesNamesWithCommas();
    /**
     *
     * @brief Pin the DAT comment header and two UTC MM/dd/yyyy time/value rows.
     *
     */
    void dat_singleSeriesFormat();
    /**
     *
     * @brief Require a NaN sample to be omitted from DAT without any nan token, leaving two
     * finite rows plus header.
     *
     */
    void dat_skipsNonFiniteValues();
    /**
     *
     * @brief Require written single_series.csv bytes to equal the in-memory CSV formatter output.
     * Writes/overwrites the file under the setup artifact directory and reads it back.
     *
     */
    void writeCsv_createsReviewableFile();
    /**
     *
     * @brief Require two DAT series to use sanitized per-series filenames and one series to keep
     * the chosen path.
     * Writes files under the setup artifact directory; checks returned paths/existence,
     * not individual DAT file contents.
     *
     */
    void writeDat_multiSeriesFanout();
    /**
     *
     * @brief Pin punctuation/path-separator sanitization and the series fallback for an empty
     * filename token.
     *
     */
    void sanitizedToken();

private:
    QString m_outDir;
};

void TestComparisonPlotExport::initTestCase()
{
    m_outDir = QDir::current().filePath(QStringLiteral("test_comparisonplot_export_output"));
    QDir().mkpath(m_outDir);
}

void TestComparisonPlotExport::csv_unionOfTimestamps()
{
    // Series A: 00:00, 00:15, 00:30. Series B: 00:15, 00:30, 00:45.
    ExportSeries a{
        QStringLiteral("Run1 — J1 (Depth)"),
        {julianAt(2026, 1, 1, 0, 0), julianAt(2026, 1, 1, 0, 15), julianAt(2026, 1, 1, 0, 30)},
        {1.0, 2.0, 3.0}};
    ExportSeries b{
        QStringLiteral("Run2 — J1 (Depth)"),
        {julianAt(2026, 1, 1, 0, 15), julianAt(2026, 1, 1, 0, 30), julianAt(2026, 1, 1, 0, 45)},
        {20.0, 30.0, 40.0}};

    const QString csv = seriesToCsvText({a, b});
    const QStringList lines = csv.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    QCOMPARE(lines.size(), 5); // header + 4 distinct timestamps
    QCOMPARE(lines[0], QStringLiteral("Date/Time,Run1 — J1 (Depth),Run2 — J1 (Depth)"));
    // First timestamp only in A; last only in B — the missing cell is empty.
    QCOMPARE(lines[1], QStringLiteral("2026-01-01 00:00:00,1,"));
    QCOMPARE(lines[2], QStringLiteral("2026-01-01 00:15:00,2,20"));
    QCOMPARE(lines[3], QStringLiteral("2026-01-01 00:30:00,3,30"));
    QCOMPARE(lines[4], QStringLiteral("2026-01-01 00:45:00,,40"));
}

void TestComparisonPlotExport::csv_quotesNamesWithCommas()
{
    ExportSeries s{QStringLiteral("Run, with comma"), {julianAt(2026, 1, 1, 0, 0)}, {5.0}};
    const QString csv = seriesToCsvText({s});
    QVERIFY(csv.startsWith(QStringLiteral("Date/Time,\"Run, with comma\"\n")));
}

void TestComparisonPlotExport::dat_singleSeriesFormat()
{
    ExportSeries s{QStringLiteral("Run1 — J1 (Depth)"),
                   {julianAt(2026, 1, 1, 0, 15), julianAt(2026, 1, 1, 0, 30)},
                   {1.5, 2.25}};
    const QString dat = seriesToDatText(s);
    const QStringList lines = dat.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines[0], QStringLiteral(";Run1 — J1 (Depth)"));
    QCOMPARE(lines[1], QStringLiteral("01/01/2026 00:15:00 1.5"));
    QCOMPARE(lines[2], QStringLiteral("01/01/2026 00:30:00 2.25"));
}

void TestComparisonPlotExport::dat_skipsNonFiniteValues()
{
    ExportSeries s{
        QStringLiteral("gaps"),
        {julianAt(2026, 1, 1, 0, 0), julianAt(2026, 1, 1, 0, 15), julianAt(2026, 1, 1, 0, 30)},
        {1.0, std::numeric_limits<double>::quiet_NaN(), 3.0}};
    const QString dat = seriesToDatText(s);
    const QStringList lines = dat.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 3); // comment + 2 finite rows
    QVERIFY(!dat.contains(QStringLiteral("nan"), Qt::CaseInsensitive));
}

void TestComparisonPlotExport::writeCsv_createsReviewableFile()
{
    ExportSeries s{QStringLiteral("Run1 — J1 (Flow)"), {julianAt(2026, 1, 1, 0, 0)}, {42.0}};
    const QString path = m_outDir + QStringLiteral("/single_series.csv");
    QString err;
    QVERIFY2(writeSeriesCsv(path, {s}, &err), qPrintable(err));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(f.readAll());
    QCOMPARE(content, seriesToCsvText({s}));
}

void TestComparisonPlotExport::writeDat_multiSeriesFanout()
{
    ExportSeries a{QStringLiteral("Run1 — J1 (Depth)"), {julianAt(2026, 1, 1, 0, 0)}, {1.0}};
    ExportSeries b{QStringLiteral("Run2 — J1 (Depth)"), {julianAt(2026, 1, 1, 0, 0)}, {2.0}};

    const QString path = m_outDir + QStringLiteral("/multi.dat");
    QString err;
    const QStringList written = writeSeriesDat(path, {a, b}, &err);
    QCOMPARE(written.size(), 2);
    QVERIFY(written[0].endsWith(QStringLiteral("multi_Run1_J1_Depth.dat")));
    QVERIFY(written[1].endsWith(QStringLiteral("multi_Run2_J1_Depth.dat")));
    for (const QString &p : written)
        QVERIFY2(QFile::exists(p), qPrintable(p));

    // Single series keeps the exact chosen path.
    const QString singlePath = m_outDir + QStringLiteral("/single.dat");
    const QStringList one = writeSeriesDat(singlePath, {a}, &err);
    QCOMPARE(one, QStringList{singlePath});
}

void TestComparisonPlotExport::sanitizedToken()
{
    QCOMPARE(sanitizedFileToken(QStringLiteral("Run1 — J1 (Depth)")),
             QStringLiteral("Run1_J1_Depth"));
    QCOMPARE(sanitizedFileToken(QStringLiteral("a/b\\c:d")), QStringLiteral("a_b_c_d"));
    QCOMPARE(sanitizedFileToken(QString()), QStringLiteral("series"));
}

QTEST_MAIN(TestComparisonPlotExport)
#include "test_comparisonplot_export.moc"
