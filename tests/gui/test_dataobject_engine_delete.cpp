/*!
 * \file   test_dataobject_engine_delete.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Perf-plan Phase A3 (BULK_DELETE_AND_WINDOWS_OPEN_PERF_PLAN_2026-09-01) bug
 * fix: the transect and curve registries' remove() must delete the ENGINE
 * copy too.  Their saveToEngine only ever adds/updates, so before the fix a
 * "deleted" object survived in the engine and reappeared in the written INP.
 * The timeseries twin lives in test_timeseries_registry_engine.cpp.
 */
#include "curve/curveprovider.h"
#include "curve/curveregistry.h"
#include "transect/transectprovider.h"
#include "transect/transectregistry.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_tables.h>

#include <QObject>
#include <QTest>

using openswmmvis::curve::CurveRegistry;
using openswmmvis::transect::TransectRegistry;

namespace {

void addTransect(SWMM_Engine eng, const char *id)
{
    QVERIFY(swmm_transect_add(eng, id) == SWMM_OK);
    const int idx = swmm_transect_index(eng, id);
    QVERIFY(idx >= 0);
    QVERIFY(swmm_transect_add_station(eng, idx, 0.0, 10.0) == SWMM_OK);
    QVERIFY(swmm_transect_add_station(eng, idx, 5.0, 2.0) == SWMM_OK);
    QVERIFY(swmm_transect_add_station(eng, idx, 10.0, 10.0) == SWMM_OK);
}

void addCurve(SWMM_Engine eng, const char *id)
{
    QVERIFY(swmm_curve_add(eng, id, /*Storage*/ 1) == SWMM_OK);
    const int idx = swmm_table_index(eng, id);
    QVERIFY(idx >= 0);
    QVERIFY(swmm_table_add_point(eng, idx, 0.0, 1.0) == SWMM_OK);
    QVERIFY(swmm_table_add_point(eng, idx, 2.0, 4.0) == SWMM_OK);
}

} // namespace

class TestDataObjectEngineDelete : public QObject
{
    Q_OBJECT

private slots:

    void transectRemove_DeletesTheEngineCopy()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);
        addTransect(eng, "GONE");
        addTransect(eng, "KEPT");

        TransectRegistry reg;
        QCOMPARE(reg.loadFromEngine(eng), 2);
        auto *p = reg.findByName(QStringLiteral("GONE"));
        QVERIFY(p);
        reg.remove(p);

        // The engine copy is gone, the sibling untouched…
        QCOMPARE(swmm_transect_index(eng, "GONE"), -1);
        QVERIFY(swmm_transect_index(eng, "KEPT") >= 0);

        // …and a later full flush cannot resurrect it.
        reg.saveToEngine(eng);
        QCOMPARE(swmm_transect_index(eng, "GONE"), -1);
        QVERIFY(swmm_transect_index(eng, "KEPT") >= 0);

        swmm_engine_destroy(eng);
    }

    void curveRemove_DeletesTheEngineTable()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);
        addCurve(eng, "GONE");
        addCurve(eng, "KEPT");

        CurveRegistry reg;
        QCOMPARE(reg.loadFromEngine(eng), 2);
        auto *p = reg.findByName(QStringLiteral("GONE"));
        QVERIFY(p);
        reg.remove(p);

        QCOMPARE(swmm_table_index(eng, "GONE"), -1);
        QVERIFY(swmm_table_index(eng, "KEPT") >= 0);

        reg.saveToEngine(eng);
        QCOMPARE(swmm_table_index(eng, "GONE"), -1);

        swmm_engine_destroy(eng);
    }
};

QTEST_MAIN(TestDataObjectEngineDelete)
#include "test_dataobject_engine_delete.moc"
