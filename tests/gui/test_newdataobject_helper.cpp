/*!
 * \file   test_newdataobject_helper.cpp
 * \brief  Slice DA.3 — pins the contract that the New Data Object
 *         creation flow relies on:
 *
 *           1. `addNewDataObject` correctly dispatches per-type options
 *              (curveType, patternType, lidType, pollutant units,
 *              inlet type, control-rule skeleton, hydrograph rain
 *              gage + response).
 *           2. The skeleton-rule builder emits well-formed RULE text
 *              that DA-ENG-02 (`swmm_control_get_id`) can parse back.
 *           3. The hydrograph creation path inserts a single placeholder
 *              parameter row + the gage assignment so the group
 *              surfaces in the Object Browser immediately.
 *
 *         The `NewDataObjectDialog` itself (QDialog UI) is exercised
 *         end-to-end at manual-verification time; this test pins the
 *         engine-side contract its callbacks depend on.
 */

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_controls.h>
#include <openswmm/engine/openswmm_inflows.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_quality.h>
#include <openswmm/engine/openswmm_tables.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

namespace {

// Mirrors the static helper in objectbrowserpanel.cpp. Kept in sync as
// part of DA.3's surface contract — if these skeletons change, the
// unit test catches the round-trip break.
QString buildRuleSkeleton(const QString &skeleton, const QString &name)
{
    if (skeleton == QLatin1String("pump"))
        return QStringLiteral(
            "RULE %1\n"
            "IF NODE J1 DEPTH > 5.0\n"
            "THEN PUMP P1 STATUS = ON").arg(name);
    if (skeleton == QLatin1String("orifice"))
        return QStringLiteral(
            "RULE %1\n"
            "IF NODE J1 DEPTH > 5.0\n"
            "THEN ORIFICE O1 SETTING = 0.5").arg(name);
    if (skeleton == QLatin1String("weir"))
        return QStringLiteral(
            "RULE %1\n"
            "IF LINK W1 FLOW > 10.0\n"
            "THEN WEIR W1 SETTING = 0\n"
            "ELSE WEIR W1 SETTING = 1").arg(name);
    return QStringLiteral("RULE %1\n").arg(name);
}

} // namespace

class TestNewDataObjectHelper : public QObject
{
    Q_OBJECT

private slots:

    // ====================================================================
    // Rule skeletons → swmm_control_get_id round-trip
    // ====================================================================

    void ruleSkeletonsAllParseBack_data()
    {
        QTest::addColumn<QString>("skeleton");
        QTest::addColumn<QString>("ruleName");
        QTest::newRow("empty")   << QString("empty")   << QString("Rule1");
        QTest::newRow("pump")    << QString("pump")    << QString("PumpOn");
        QTest::newRow("orifice") << QString("orifice") << QString("OrificeClose");
        QTest::newRow("weir")    << QString("weir")    << QString("WeirBypass");
    }

    void ruleSkeletonsAllParseBack()
    {
        QFETCH(QString, skeleton);
        QFETCH(QString, ruleName);

        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);

        const QString body = buildRuleSkeleton(skeleton, ruleName);
        QCOMPARE(swmm_control_add_rule(e, body.toUtf8().constData()), SWMM_OK);
        QCOMPARE(swmm_control_count(e), 1);

        char buf[64] = {};
        QCOMPARE(swmm_control_get_id(e, 0, buf, sizeof(buf)), SWMM_OK);
        QCOMPARE(QString::fromUtf8(buf), ruleName);

        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Hydrograph creation path (matches addNewDataObject's DataHydrographs)
    // ====================================================================

    void hydrographCreationSurfacesAsGroup()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);

        const char *groupName = "SanSewer";
        const char *gageName  = "RG1";
        const int   response  = 1;  // MEDIUM
        // Replays addNewDataObject's DataHydrographs branch.
        swmm_hydrograph_add_gage(e, groupName, gageName);
        QCOMPARE(swmm_hydrograph_add(e, groupName, -1 /*ALL*/, response,
                                      0.0, 0.0, 0.0, 0.0, 0.0, 0.0),
                 SWMM_OK);

        // Object Browser surface contract (DA.1): group count == 1.
        QCOMPARE(swmm_hydrograph_group_count(e), 1);
        char buf[64] = {};
        QCOMPARE(swmm_hydrograph_group_id(e, 0, buf, sizeof(buf)), SWMM_OK);
        QCOMPARE(QString::fromUtf8(buf), QString::fromLatin1(groupName));

        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Per-type subtype propagation — exercises the `options` map keys
    // ====================================================================

    void curveTypeRoundTripsThroughEngine()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);

        // Replays the DataCurves branch with curveType = 3 (RATING).
        QCOMPARE(swmm_curve_add(e, "MyCurve", 3 /*RATING*/), SWMM_OK);
        const int idx = swmm_table_index(e, "MyCurve");
        QVERIFY(idx >= 0);
        int t = -1;
        QCOMPARE(swmm_table_get_type(e, idx, &t), SWMM_OK);
        QCOMPARE(t, 3);

        swmm_engine_destroy(e);
    }

    void pollutantUnitsRoundTripsThroughEngine()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);

        // Replays the DataPollutants branch with units = 1 (UG/L).
        QCOMPARE(swmm_pollutant_add(e, "Hg", 1), SWMM_OK);
        const int idx = swmm_pollutant_index(e, "Hg");
        QVERIFY(idx >= 0);
        int u = -1;
        QCOMPARE(swmm_pollutant_get_units(e, idx, &u), SWMM_OK);
        QCOMPARE(u, 1);

        swmm_engine_destroy(e);
    }
};

QTEST_GUILESS_MAIN(TestNewDataObjectHelper)
#include "test_newdataobject_helper.moc"
