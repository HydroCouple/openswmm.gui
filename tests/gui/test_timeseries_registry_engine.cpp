/*!
 * \file   test_timeseries_registry_engine.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.3.8 — TimeseriesRegistry::loadFromEngine
 *         round-trips a BUILDING-state engine with two Tseries.
 */
#include "plot/swmmjuliandatetime.h"
#include "timeseries/timeseriesprovider.h"
#include "timeseries/timeseriesregistry.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_tables.h>

#include <QDateTime>
#include <QObject>
#include <QTest>

using openswmmvis::plot::dateTimeToSwmmJulian;
using openswmmvis::timeseries::TimeseriesProvider;
using openswmmvis::timeseries::TimeseriesRegistry;

namespace {
QDateTime t(int year, int month, int day, int hour, int minute = 0)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute), Qt::UTC);
}

void addSeries(SWMM_Engine eng, const char *id,
               const QVector<QPair<QDateTime, double>> &pts)
{
    QVERIFY(swmm_timeseries_add(eng, id) == SWMM_OK);
    const int idx = swmm_table_index(eng, id);
    QVERIFY(idx >= 0);
    for (const auto &p : pts) {
        const double j = dateTimeToSwmmJulian(p.first);
        QVERIFY(swmm_table_add_point(eng, idx, j, p.second) == SWMM_OK);
    }
}
} // namespace

class TestTimeseriesRegistryEngine : public QObject
{
    Q_OBJECT

private slots:

    void loadFromEngine_BuildingState_RoundTrip()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);

        addSeries(eng, "RAIN_A", {
            {t(2026, 1, 1, 0),  0.5},
            {t(2026, 1, 1, 6),  1.5},
            {t(2026, 1, 1, 12), 0.7},
        });
        addSeries(eng, "INFLOW_B", {
            {t(2026, 1, 1, 0),  10.0},
            {t(2026, 1, 1, 6),  20.0},
        });

        TimeseriesRegistry reg;
        const int added = reg.loadFromEngine(eng);
        QCOMPARE(added, 2);

        auto *a = reg.findByName(QStringLiteral("RAIN_A"));
        auto *b = reg.findByName(QStringLiteral("INFLOW_B"));
        QVERIFY(a != nullptr);
        QVERIFY(b != nullptr);
        QCOMPARE(a->pointCount(), 3);
        QCOMPARE(b->pointCount(), 2);

        // Point values round-trip exactly.
        QCOMPARE(a->pointAt(0).value, 0.5);
        QCOMPARE(a->pointAt(1).value, 1.5);
        QCOMPARE(a->pointAt(2).value, 0.7);
        QCOMPARE(b->pointAt(0).value, 10.0);
        QCOMPARE(b->pointAt(1).value, 20.0);

        // Times round-trip via Julian — equality holds since both directions
        // go through the same conversion.
        QCOMPARE(a->pointAt(0).time, t(2026, 1, 1, 0));
        QCOMPARE(a->pointAt(1).time, t(2026, 1, 1, 6));

        swmm_engine_destroy(eng);
    }

    void loadFromEngine_SkipsDuplicates()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);
        addSeries(eng, "DUP", {{t(2026, 1, 1, 0), 1.0}});

        TimeseriesRegistry reg;
        QVERIFY(reg.create(QStringLiteral("DUP")) != nullptr);   // pre-existing

        const int added = reg.loadFromEngine(eng);
        QCOMPARE(added, 0);                                       // skipped
        QCOMPARE(reg.providerCount(), 1);
        // Original (empty) provider is preserved; engine points NOT merged in.
        QCOMPARE(reg.findByName(QStringLiteral("DUP"))->pointCount(), 0);

        swmm_engine_destroy(eng);
    }

    void loadFromEngine_NullHandleReturnsZero()
    {
        TimeseriesRegistry reg;
        QCOMPARE(reg.loadFromEngine(nullptr), 0);
        QCOMPARE(reg.providerCount(), 0);
    }

    // ── Phase 6.7.3.7 — saveToEngine round-trip ─────────────────────────────

    void saveToEngine_CreatesNewSeries()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);

        // Build a brand-new provider that doesn't exist in the engine yet.
        TimeseriesRegistry reg;
        auto *p = reg.create(QStringLiteral("NEW_RAIN"));
        QVERIFY(p != nullptr);
        QVERIFY(p->setAllPoints({
            {t(2026, 1, 1, 0), 0.5},
            {t(2026, 1, 1, 6), 1.5},
            {t(2026, 1, 1, 12), 0.7},
        }));

        const int written = reg.saveToEngine(eng);
        QCOMPARE(written, 1);

        // Confirm it now exists in the engine with correct points.
        const int idx = swmm_table_index(eng, "NEW_RAIN");
        QVERIFY(idx >= 0);
        int n = 0;
        QCOMPARE(swmm_table_get_point_count(eng, idx, &n), SWMM_OK);
        QCOMPARE(n, 3);

        double x = 0, y = 0;
        QCOMPARE(swmm_table_get_point(eng, idx, 1, &x, &y), SWMM_OK);
        QCOMPARE(y, 1.5);

        swmm_engine_destroy(eng);
    }

    void saveToEngine_ReplacesExistingPoints()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);

        // Seed engine with 3 points, then mutate provider to a different set
        // and confirm saveToEngine replaces them all (no append-only drift).
        addSeries(eng, "EXIST", {
            {t(2026, 1, 1, 0),  1.0},
            {t(2026, 1, 1, 6),  2.0},
            {t(2026, 1, 1, 12), 3.0},
        });

        TimeseriesRegistry reg;
        QCOMPARE(reg.loadFromEngine(eng), 1);
        auto *p = reg.findByName(QStringLiteral("EXIST"));
        QVERIFY(p != nullptr);
        QCOMPARE(p->pointCount(), 3);

        // Replace with 2 different points.
        QVERIFY(p->setAllPoints({
            {t(2026, 2, 1, 0), 100.0},
            {t(2026, 2, 1, 1), 200.0},
        }));
        QCOMPARE(reg.saveToEngine(eng), 1);

        // Engine now has 2 points matching the new values.
        const int idx = swmm_table_index(eng, "EXIST");
        int n = 0;
        QCOMPARE(swmm_table_get_point_count(eng, idx, &n), SWMM_OK);
        QCOMPARE(n, 2);

        double x = 0, y = 0;
        QCOMPARE(swmm_table_get_point(eng, idx, 0, &x, &y), SWMM_OK);
        QCOMPARE(y, 100.0);
        QCOMPARE(swmm_table_get_point(eng, idx, 1, &x, &y), SWMM_OK);
        QCOMPARE(y, 200.0);

        swmm_engine_destroy(eng);
    }

    void saveToEngine_SkipsExternalFileProviders()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);

        TimeseriesRegistry reg;
        auto *p = reg.create(QStringLiteral("EXT"));
        QVERIFY(p != nullptr);
        QVERIFY(p->setAllPoints({{t(2026, 1, 1, 0), 1.0}}));
        p->setSourceMode(TimeseriesProvider::SourceMode::ExternalFile);

        // External-mode provider is not written by saveToEngine.
        QCOMPARE(reg.saveToEngine(eng), 0);
        QCOMPARE(swmm_table_index(eng, "EXT"), -1);

        swmm_engine_destroy(eng);
    }

    void saveToEngine_NullHandleReturnsZero()
    {
        TimeseriesRegistry reg;
        QCOMPARE(reg.saveToEngine(nullptr), 0);
    }

    void loadFromEngine_CachesHandle_NoArgSavePicksItUp()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);
        addSeries(eng, "RAIN", {
            {t(2026, 1, 1, 0),  1.0},
            {t(2026, 1, 1, 6),  2.0},
        });

        TimeseriesRegistry reg;
        QCOMPARE(reg.engineHandle(), nullptr);
        QCOMPARE(reg.loadFromEngine(eng), 1);
        QCOMPARE(reg.engineHandle(), eng);   // cached

        // Mutate provider, flush via no-arg overload.
        auto *p = reg.findByName(QStringLiteral("RAIN"));
        QVERIFY(p != nullptr);
        QVERIFY(p->setAllPoints({
            {t(2026, 5, 1, 0),  99.0},
            {t(2026, 5, 1, 12), 88.0},
        }));
        QCOMPARE(reg.saveToEngine(), 1);   // no-arg uses cached handle

        const int idx = swmm_table_index(eng, "RAIN");
        int n = 0;
        QCOMPARE(swmm_table_get_point_count(eng, idx, &n), SWMM_OK);
        QCOMPARE(n, 2);
        double x = 0, y = 0;
        QCOMPARE(swmm_table_get_point(eng, idx, 0, &x, &y), SWMM_OK);
        QCOMPARE(y, 99.0);

        swmm_engine_destroy(eng);
    }

    void saveToEngineNoArg_WithoutBoundHandleIsNoop()
    {
        TimeseriesRegistry reg;
        QVERIFY(reg.create(QStringLiteral("A")) != nullptr);
        QCOMPARE(reg.saveToEngine(), 0);   // no handle ever bound
    }
};

QTEST_APPLESS_MAIN(TestTimeseriesRegistryEngine)
#include "test_timeseries_registry_engine.moc"
