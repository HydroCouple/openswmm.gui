/*!
 * \file   test_timeseries_datetime_roundtrip.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  SWMM DateTime Consolidation Phase 1 (workplans/
 *         SWMM_DATETIME_CONSOLIDATION_PLAN_2026-07-10.md) — golden
 *         round-trip for the time-series minute-truncation bug (GH #1).
 *
 *         Opens tests/gui/data/single_rain_series.inp (TSERIES1: ten 15-min
 *         stamps from 1990-01-18 00:00) via a real engine, loads it through
 *         TimeseriesRegistry::loadFromEngine (which converts every SWMM
 *         DateTime double via openswmmvis::core::swmmDateTimeToQDateTime),
 *         and asserts every displayed H:M:S is exact — in particular the
 *         three stamps (00:15, 01:00, 01:45) that the old truncating
 *         converter used to render a minute low (00:14:59, 00:59:59,
 *         01:44:59). Then round-trips through saveToEngine and a second
 *         loadFromEngine to confirm the stored doubles and the displayed
 *         grid are unchanged.
 */
#include "timeseries/timeseriesprovider.h"
#include "timeseries/timeseriesregistry.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_tables.h>

#include <QDir>
#include <QFile>
#include <QObject>
#include <QTest>

using openswmmvis::timeseries::TimeseriesProvider;
using openswmmvis::timeseries::TimeseriesRegistry;

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

// Same open pairing as SWMMModelLayer::openEngineForPath() — swmm_engine_new()
// (BUILDING-state, no file) is a different lifecycle path that does not
// reliably pair with swmm_engine_open() on this engine build.
SWMM_Engine openModel(const QString &path)
{
    SWMM_Engine eng = swmm_engine_create();
    if (!eng) return nullptr;
    if (swmm_engine_open(eng, path.toUtf8().constData(), "", "", nullptr) != SWMM_OK) {
        swmm_engine_destroy(eng);
        return nullptr;
    }
    return eng;
}

// The golden grid from single_rain_series.inp's [TIMESERIES] TSERIES1.
struct HM { int h, m; };
constexpr HM kExpectedGrid[10] = {
    {0, 0}, {0, 15}, {0, 30}, {0, 45}, {1, 0},
    {1, 15}, {1, 30}, {1, 45}, {2, 0}, {2, 15},
};

void verifyGoldenGrid(const TimeseriesProvider &ts)
{
    QCOMPARE(ts.pointCount(), 10);
    for (int i = 0; i < 10; ++i) {
        const QTime t = ts.pointAt(i).time.time();
        QVERIFY2(t.second() == 0,
                 qPrintable(QStringLiteral("point %1 has a non-zero second: %2")
                                .arg(i).arg(t.toString(QStringLiteral("HH:mm:ss")))));
        QCOMPARE(t.hour(), kExpectedGrid[i].h);
        QCOMPARE(t.minute(), kExpectedGrid[i].m);
    }
}

} // namespace

class TestTimeseriesDatetimeRoundtrip : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        const QString fixture =
            QDir(dataDir()).filePath(QStringLiteral("single_rain_series.inp"));
        QVERIFY2(QFile::exists(fixture),
                 "single_rain_series.inp missing from the gui-test data dir");
    }

    // GH #1: no 15-min stamp renders a minute low. Pins the three previously
    // wrong entries (00:15 -> was 00:14:59, 01:00 -> was 00:59:59, 01:45 ->
    // was 01:44:59) explicitly, plus the full ten-point grid.
    void fixtureNoTruncation()
    {
        const QString fixture =
            QDir(dataDir()).filePath(QStringLiteral("single_rain_series.inp"));
        SWMM_Engine eng = openModel(fixture);
        QVERIFY(eng != nullptr);

        TimeseriesRegistry reg;
        QCOMPARE(reg.loadFromEngine(eng), 1);

        auto *ts = reg.findByName(QStringLiteral("TSERIES1"));
        QVERIFY(ts != nullptr);
        verifyGoldenGrid(*ts);

        QCOMPARE(ts->pointAt(1).time.time(), QTime(0, 15, 0));
        QCOMPARE(ts->pointAt(4).time.time(), QTime(1, 0, 0));
        QCOMPARE(ts->pointAt(7).time.time(), QTime(1, 45, 0));

        swmm_engine_destroy(eng);
    }

    // INP -> GUI -> INP: saveToEngine() must write back the exact same SWMM
    // DateTime doubles the engine originally parsed (no drift through the
    // QDateTime round-trip), and a second loadFromEngine() must still show
    // the same golden H:M:S grid.
    void saveReloadRoundTripIsBitIdentical()
    {
        const QString fixture =
            QDir(dataDir()).filePath(QStringLiteral("single_rain_series.inp"));
        SWMM_Engine eng = openModel(fixture);
        QVERIFY(eng != nullptr);

        TimeseriesRegistry reg;
        QCOMPARE(reg.loadFromEngine(eng), 1);
        auto *ts = reg.findByName(QStringLiteral("TSERIES1"));
        QVERIFY(ts != nullptr);

        const int idx = swmm_table_index(eng, "TSERIES1");
        QVERIFY(idx >= 0);
        int nBefore = 0;
        QCOMPARE(swmm_table_get_point_count(eng, idx, &nBefore), SWMM_OK);
        QVector<double> xBefore(nBefore), yBefore(nBefore);
        for (int i = 0; i < nBefore; ++i)
            QCOMPARE(swmm_table_get_point(eng, idx, i, &xBefore[i], &yBefore[i]), SWMM_OK);

        QCOMPARE(reg.saveToEngine(eng), 1);

        int nAfter = 0;
        QCOMPARE(swmm_table_get_point_count(eng, idx, &nAfter), SWMM_OK);
        QCOMPARE(nAfter, nBefore);
        for (int i = 0; i < nAfter; ++i) {
            double x = 0.0, y = 0.0;
            QCOMPARE(swmm_table_get_point(eng, idx, i, &x, &y), SWMM_OK);
            QCOMPARE(x, xBefore[i]);   // bit-identical SWMM DateTime double
            QCOMPARE(y, yBefore[i]);
        }

        // Re-read through the GUI layer again — same golden grid post-save.
        TimeseriesRegistry reg2;
        QCOMPARE(reg2.loadFromEngine(eng), 1);
        auto *ts2 = reg2.findByName(QStringLiteral("TSERIES1"));
        QVERIFY(ts2 != nullptr);
        verifyGoldenGrid(*ts2);

        swmm_engine_destroy(eng);
    }
};

QTEST_APPLESS_MAIN(TestTimeseriesDatetimeRoundtrip)
#include "test_timeseries_datetime_roundtrip.moc"
