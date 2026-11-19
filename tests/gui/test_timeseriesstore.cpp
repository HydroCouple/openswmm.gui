/**
 * @file test_timeseriesstore.cpp
 * @brief Offline native observation import, identity, temporal and paging acceptance.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "timeseries/timeseriesstore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>
#include <QUuid>
#include <cmath>
#include <algorithm>

using namespace OpenSWMM::TimeSeries;
namespace
{
/** @brief Retain every fixture/output in a uniquely named reviewable repository folder. */
QString directory(const QString &name)
{
    const auto root = QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath())
                          .absoluteFilePath("../verification/timeseries_layers_2026-10-27/storage");
    const auto path =
        QDir(root).filePath(name + "-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    QDir().mkpath(path);
    return path;
}
/** @brief Atomically irrelevant fixture write; failures abort the individual test setup. */
bool write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
/** @brief Encode CSV fields with all commas, quotes and newlines preserved. */
QByteArray csvRow(const QStringList &values)
{
    QList<QByteArray> cells;
    for (const auto &value : values) {
        auto cell = value.toUtf8();
        cell.replace("\"", "\"\"");
        cells.append('"' + cell + '"');
    }
    return cells.join(',') + "\r\n";
}
/** @brief Build a neutral descriptor mapping a long-form fixture without USGS-specific code. */
QJsonObject spec(const QString &basis = "instant", bool located = true)
{
    QJsonObject mapping{
        {"time", "time"},           {"value", "value"}, {"sourceSeriesId", "series"},
        {"statistic", "statistic"}, {"unit", "unit"},   {"properties", "properties"}};
    QJsonObject defaults{{"provider", "fixture"},
                         {"collection", "observations"},
                         {"siteId", "00123"},
                         {"parameter", "00060"},
                         {"timeBasis", basis}};
    QJsonObject result{
        {"schema", "openswmm.timeseries-import/1"},
        {"datasetId", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"title", "Reviewable observations"},
        {"sources", QJsonArray{QJsonObject{
                        {"path", "samples.csv"}, {"columns", mapping}, {"defaults", defaults}}}}};
    if (located)
        result["locations"] = QJsonArray{
            QJsonObject{{"id", "00123"},
                        {"title", "Station A"},
                        {"attributes", QJsonObject{{"district", "001"}}},
                        {"geometry", QJsonObject{{"type", "Point"},
                                                 {"coordinates", QJsonArray{-77.1, 38.9}}}}}};
    return result;
}
/** @brief Write descriptor and complete CSV for one independently reviewable import. */
bool fixture(const QString &folder, const QJsonObject &descriptor, const QList<QStringList> &rows)
{
    QByteArray csv = csvRow({"time", "value", "series", "statistic", "unit", "properties"});
    for (const auto &row : rows)
        csv += csvRow(row);
    return write(folder + "/samples.csv", csv) &&
           write(folder + "/import.json", QJsonDocument(descriptor).toJson());
}
}

/** @brief Tests exercise real GeoPackages and never require an engine or plugin. */
class TestTimeSeriesStore : public QObject
{
    Q_OBJECT
private slots:
    /** @brief Partition statistics, retain escaped metadata, sort and page source timestamps. */
    void roundTripPartitionAndPaging()
    {
        const auto folder = directory("partition");
        const QList<QStringList> rows{
            {"2024-12-01T01:00:00Z", "0", "sensor-1", "mean", "m3/s",
             "{\"note\":\"a,b\\nquoted \\\"text\\\"\"}"},
            {"2024-11-30T19:00:00-05:00", "2.5", "sensor-1", "mean", "m3/s", "{}"},
            {"2024-12-01T02:00:00Z", "", "sensor-1", "mean", "m3/s", "{}"},
            {"2024-12-01T00:00:00Z", "9", "sensor-1", "maximum", "m3/s", "{}"}};
        QVERIFY(fixture(folder, spec(), rows));
        QString error;
        QVERIFY2(TimeSeriesStore::importDescriptor(folder + "/import.json", folder + "/series.gpkg",
                                                   &error),
                 qPrintable(error));
        const auto data = TimeSeriesStore::open(folder + "/series.gpkg", &error);
        QVERIFY2(data, qPrintable(error));
        QCOMPARE(data->series.size(), 2);
        QCOMPARE(data->locations.size(), 1);
        QCOMPARE(data->locations[0].id, "00123");
        QVERIFY(data->locations[0].hasGeometry);
        const auto mean = *std::find_if(data->series.begin(), data->series.end(),
                                        [](const auto &s) { return s.statistic == "mean"; });
        QCOMPARE(mean.count, 3);
        QCOMPARE(mean.validCount, 2);
        QCOMPARE(mean.locationId, "00123");
        ObservationQuery query;
        query.limit = 1;
        const auto page1 = TimeSeriesStore::readPage(data->path, mean.id, query, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(page1.observations.size(), 1);
        QVERIFY(page1.hasMore);
        QCOMPARE(page1.observations[0].value, 2.5);
        QCOMPARE(page1.observations[0].sourceTime, "2024-11-30T19:00:00-05:00");
        query.afterKey = page1.nextKey;
        query.limit = 5;
        const auto page2 = TimeSeriesStore::readPage(data->path, mean.id, query, &error);
        QCOMPARE(page2.observations.size(), 2);
        QVERIFY(!page2.hasMore);
        QVERIFY(page2.observations[0].hasValue);
        QCOMPARE(page2.observations[0].value, 0.0);
        QCOMPARE(page2.observations[0].properties["note"].toString(), "a,b\nquoted \"text\"");
        QVERIFY(!page2.observations[1].hasValue);
        QVERIFY(std::isnan(page2.observations[1].value));
    }
    /** @brief Calendar dates and all-null unlocated data retain their distinct semantics. */
    void calendarDatesAndAllNull()
    {
        const auto folder = directory("daily");
        QVERIFY(fixture(folder, spec("calendar-date", false),
                        {{"2024-12-01", "", "daily", "mean", "", "{}"}}));
        QString error;
        QVERIFY2(TimeSeriesStore::importDescriptor(folder + "/import.json", folder + "/series.gpkg",
                                                   &error),
                 qPrintable(error));
        const auto data = TimeSeriesStore::open(folder + "/series.gpkg", &error);
        QVERIFY(data);
        QVERIFY(data->locations.isEmpty());
        QCOMPARE(data->series[0].timeBasis, TimeBasis::CalendarDate);
        QCOMPARE(data->series[0].firstKey, QDate(2024, 12, 1).toJulianDay());
        QCOMPARE(data->series[0].validCount, 0);
    }
    /** @brief Enumerate contract failures including pre-CSV duplicate conflicts. */
    void rejectsInvalid_data()
    {
        QTest::addColumn<QString>("kind");
        for (const auto &kind :
             {"units", "value-conflict", "quality-conflict", "timestamp", "nonfinite", "json",
              "empty", "schema", "location", "malformed-csv", "invalid-utf8", "truncated-utf8"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
    }
    /** @brief Invalid input never publishes a destination or leaves staged native files. */
    void rejectsInvalid()
    {
        QFETCH(QString, kind);
        const auto folder = directory(kind);
        auto descriptor = spec();
        QList<QStringList> rows{{"2024-12-01T00:00:00Z", "1", "sensor", "mean", "m3/s", "{}"}};
        if (kind == "units") {
            rows.append(rows[0]);
            rows[1][4] = "ft3/s";
        }
        if (kind == "value-conflict") {
            rows.append(rows[0]);
            rows[1][1] = "2";
        }
        if (kind == "quality-conflict") {
            rows.append(rows[0]);
            rows[1][5] = "{\"quality\":\"estimated\"}";
        }
        if (kind == "timestamp")
            rows[0][0] = "2024-12-01T00:00:00";
        if (kind == "nonfinite")
            rows[0][1] = "NaN";
        if (kind == "json")
            rows[0][5] = "{broken";
        if (kind == "empty")
            rows.clear();
        if (kind == "schema")
            descriptor["schema"] = "future/99";
        if (kind == "location")
            descriptor["locations"] = QJsonArray{
                QJsonObject{{"id", "00123"},
                            {"geometry", QJsonObject{{"type", "Point"},
                                                     {"coordinates", QJsonArray{200, 90}}}}}};
        QVERIFY(fixture(folder, descriptor, rows));
        if (kind == "malformed-csv")
            QVERIFY(write(folder + "/samples.csv", "time,value\n\"unclosed,1\n"));
        if (kind == "invalid-utf8" || kind == "truncated-utf8") {
            const auto broken =
                QByteArray(
                    "time,value,series,statistic,unit,properties\n2024-12-01T00:00:00Z,1,sensor") +
                QByteArray::fromHex(kind == "invalid-utf8" ? "ff" : "c3") + ",mean,m3/s,{}\n";
            QVERIFY(write(folder + "/samples.csv", broken));
        }
        QString error;
        QVERIFY(!TimeSeriesStore::importDescriptor(folder + "/import.json", folder + "/series.gpkg",
                                                   &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFileInfo::exists(folder + "/series.gpkg"));
        QVERIFY(QDir(folder).entryList({"*.partial.gpkg*"}, QDir::Files).isEmpty());
    }
    /** @brief Exact duplicates collapse; explicit replacement of committed files is refused. */
    void duplicatesAndOverwrite()
    {
        const auto folder = directory("duplicate");
        const QStringList row{"2024-12-01T00:00:00Z", "1", "sensor", "mean", "m3/s", "{}"};
        QVERIFY(fixture(folder, spec(), {row, row}));
        QString error;
        QVERIFY2(TimeSeriesStore::importDescriptor(folder + "/import.json", folder + "/series.gpkg",
                                                   &error),
                 qPrintable(error));
        const auto data = TimeSeriesStore::open(folder + "/series.gpkg", &error);
        QVERIFY(data);
        QCOMPARE(data->series[0].count, 1);
        QVERIFY(!TimeSeriesStore::importDescriptor(folder + "/import.json", data->path, &error));
        QCOMPARE(TimeSeriesStore::open(data->path)->id, data->id);
    }
    /** @brief A captured identity/revision refuses a package replaced between paged reads. */
    void capturedSourceRevisionRefusesReplacement()
    {
        const auto folder = directory("source-replacement");
        const auto descriptor = spec();
        QList<QStringList> rows{{"2024-12-01T00:00:00Z", "1", "sensor", "", "m", "{}"}};
        QVERIFY(fixture(folder, descriptor, rows));
        QString error;
        QVERIFY2(TimeSeriesStore::importDescriptor(folder + "/import.json", folder + "/series.gpkg",
                                                   &error),
                 qPrintable(error));
        const auto original = TimeSeriesStore::open(folder + "/series.gpkg", &error);
        QVERIFY(original);
        ObservationQuery query;
        query.expectedDatasetId = original->id;
        query.expectedFingerprint = original->fingerprint;
        QCOMPARE(TimeSeriesStore::readPage(original->path, original->series[0].id, query, &error)
                     .observations.size(),
                 1);
        rows[0][1] = "2";
        QVERIFY(fixture(folder, descriptor, rows));
        QVERIFY2(TimeSeriesStore::importDescriptor(folder + "/import.json",
                                                   folder + "/changed.gpkg", &error),
                 qPrintable(error));
        QVERIFY(QFile::rename(original->path, folder + "/original.gpkg"));
        QVERIFY(QFile::copy(folder + "/changed.gpkg", original->path));
        QVERIFY(TimeSeriesStore::readPage(original->path, original->series[0].id, query, &error)
                    .observations.isEmpty());
        QVERIFY(error.contains("changed"));
        query.expectedFingerprint.clear();
        query.expectedDatasetId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(TimeSeriesStore::readPage(original->path, original->series[0].id, query, &error)
                    .observations.isEmpty());
        QVERIFY(error.contains("changed"));
        const auto updated = TimeSeriesStore::open(original->path, &error);
        QVERIFY(updated);
        query.expectedDatasetId = updated->id;
        query.expectedFingerprint = updated->fingerprint;
        const auto page =
            TimeSeriesStore::readPage(updated->path, updated->series[0].id, query, &error);
        QCOMPARE(page.observations.size(), 1);
        QCOMPARE(page.observations[0].value, 2.);
    }
    /** @brief Sorted fast-path inserts and earlier/repeated keys share the same conflict rules. */
    void monotoneAndOutOfOrderDuplicates()
    {
        const auto folder = directory("out-of-order");
        const QStringList first{"2024-12-01T01:00:00Z", "1", "sensor", "", "m", "{}"};
        const QStringList second{"2024-12-01T02:00:00Z", "2", "sensor", "", "m", "{}"};
        const QStringList third{"2024-12-01T03:00:00Z", "3", "sensor", "", "m", "{}"};
        const auto descriptor = spec();
        QList<QStringList> rows{second, third, first, third, first};
        QVERIFY(fixture(folder, descriptor, rows));
        QString error;
        QVERIFY2(TimeSeriesStore::importDescriptor(folder + "/import.json", folder + "/series.gpkg",
                                                   &error),
                 qPrintable(error));
        const auto data = TimeSeriesStore::open(folder + "/series.gpkg", &error);
        QVERIFY(data);
        QCOMPARE(data->series[0].count, 3);
        const auto page = TimeSeriesStore::readPage(data->path, data->series[0].id, {}, &error);
        QCOMPARE(page.observations.size(), 3);
        QCOMPARE(page.observations[0].value, 1.);
        QCOMPARE(page.observations[2].value, 3.);
        auto conflict = second;
        conflict[1] = "99";
        rows.append(conflict);
        QVERIFY(fixture(folder, descriptor, rows));
        QVERIFY(!TimeSeriesStore::importDescriptor(folder + "/import.json",
                                                   folder + "/conflict.gpkg", &error));
        QVERIFY(error.contains("Conflicting"));
        QVERIFY(!QFileInfo::exists(folder + "/conflict.gpkg"));
        QCOMPARE(TimeSeriesStore::open(data->path)->series[0].count, 3);
    }
    /** @brief Cancellation during streamed records cleans only the unpublished native package. */
    void cancellationAndIndexedPaging()
    {
        const auto folder = directory("paging");
        QList<QStringList> rows;
        const auto start = QDateTime::fromString("2024-12-01T00:00:00Z", Qt::ISODate);
        for (int i = 0; i < 12000; ++i)
            rows.append({start.addSecs(i).toString(Qt::ISODate), QString::number(i), "sensor", "",
                         "m", "{}"});
        QVERIFY(fixture(folder, spec(), rows));
        QString error;
        int checks = 0;
        QVERIFY(!TimeSeriesStore::importDescriptor(folder + "/import.json",
                                                   folder + "/cancelled.gpkg", &error, nullptr,
                                                   [&] { return ++checks > 50; }));
        QVERIFY(!QFileInfo::exists(folder + "/cancelled.gpkg"));
        QVERIFY(QFileInfo::exists(folder + "/samples.csv"));
        QVERIFY2(TimeSeriesStore::importDescriptor(folder + "/import.json", folder + "/series.gpkg",
                                                   &error),
                 qPrintable(error));
        const auto data = TimeSeriesStore::open(folder + "/series.gpkg", &error);
        QVERIFY(data);
        ObservationQuery query;
        query.limit = 20000;
        const auto page1 = TimeSeriesStore::readPage(data->path, data->series[0].id, query, &error);
        QCOMPARE(page1.observations.size(), 10000);
        QVERIFY(page1.hasMore);
        query.afterKey = page1.nextKey;
        const auto page2 = TimeSeriesStore::readPage(data->path, data->series[0].id, query, &error);
        QCOMPARE(page2.observations.size(), 2000);
        QVERIFY(!page2.hasMore);
        QCOMPARE(page2.observations[0].value, 10000.0);
        std::atomic_bool cancel{true};
        QVERIFY(TimeSeriesStore::readPage(data->path, data->series[0].id, {}, &error, &cancel)
                    .observations.isEmpty());
        QVERIFY(!error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestTimeSeriesStore)
#include "test_timeseriesstore.moc"
