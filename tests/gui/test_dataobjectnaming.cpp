/*!
 * \file   test_dataobjectnaming.cpp
 * \brief  Slice DA.1 contract test — pins the engine ABI surface that
 *         `SWMMModelLayer::dataObjectCount` / `dataObjectNameAt` rely on
 *         for Control Rules and Unit Hydrographs in the Object Browser.
 *
 * Two user-visible bugs motivated DA.1:
 *   (a) Control Rules surfaced as "Rule 1, Rule 2…" regardless of the
 *       user-supplied RULE name in the .inp.
 *   (b) Unit Hydrograph rows appeared blank in the browser because
 *       dataObjectCount returned the raw per-(group, month, response)
 *       entry count while dataObjectNameAt de-duplicated.
 *
 * Fix relies on two new engine APIs:
 *   - DA-ENG-01: swmm_hydrograph_group_count / swmm_hydrograph_group_id
 *   - DA-ENG-02: swmm_control_get_id (parses RULE <name> server-side)
 *
 * Self-contained: pulls in only the engine target. The GUI sentinel
 * mapping (SWMM_ERR_BADPARAM → "Rule N [unnamed]") is replicated here
 * via a small helper that mirrors the switch arms in
 * `src/layers/swmmmodellayer.cpp` so a regression in the GUI mapping
 * surfaces alongside the engine-ABI changes.
 */

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_controls.h>
#include <openswmm/engine/openswmm_inflows.h>

#include <QObject>
#include <QString>
#include <QTest>

#include <cstring>

namespace {

// Replicates the DataControls arm of SWMMModelLayer::dataObjectNameAt
// (src/layers/swmmmodellayer.cpp). Kept in sync as the contract surface
// between the engine ABI and the GUI Object Browser.
QString guiNameForControlRule(SWMM_Engine e, int row)
{
    char buf[128] = {};
    const int rc = swmm_control_get_id(e, row, buf, sizeof(buf));
    if (rc == SWMM_OK) return QString::fromUtf8(buf);
    if (rc == SWMM_ERR_BADPARAM)
        return QObject::tr("Rule %1 [unnamed]").arg(row + 1);
    return {};
}

// Replicates the DataHydrographs arm of dataObjectNameAt.
QString guiNameForHydrographGroup(SWMM_Engine e, int row)
{
    char buf[128] = {};
    if (swmm_hydrograph_group_id(e, row, buf, sizeof(buf)) != SWMM_OK)
        return {};
    return QString::fromUtf8(buf);
}

} // namespace

class TestDataObjectNaming : public QObject
{
    Q_OBJECT

private slots:

    // ====================================================================
    // Hydrograph group enumeration (DA-ENG-01)
    // ====================================================================

    /*! \brief One group × 12 monthly rows surfaces as a single group row
     *         (not 12). Closes the count/name mismatch that left the
     *         Object Browser showing 11 blank rows. */
    void hydrographSingleGroupTwelveMonths()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        for (int m = 0; m < 12; ++m) {
            QCOMPARE(swmm_hydrograph_add(e, "SanSewer", m, 0,
                                          0.05, 1.0, 2.0, 0, 0, 0),
                     SWMM_OK);
        }
        QCOMPARE(swmm_hydrograph_count(e),       12);
        QCOMPARE(swmm_hydrograph_group_count(e),  1);
        QCOMPARE(guiNameForHydrographGroup(e, 0), QStringLiteral("SanSewer"));
        swmm_engine_destroy(e);
    }

    /*! \brief Three interleaved groups surface in first-occurrence order. */
    void hydrographMultipleGroupsFirstOccurrenceOrder()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        QCOMPARE(swmm_hydrograph_add(e, "Combined", 0, 0, 0.1, 1, 2, 0,0,0), SWMM_OK);
        QCOMPARE(swmm_hydrograph_add(e, "Sanitary", 0, 0, 0.1, 1, 2, 0,0,0), SWMM_OK);
        QCOMPARE(swmm_hydrograph_add(e, "Combined", 1, 0, 0.1, 1, 2, 0,0,0), SWMM_OK);
        QCOMPARE(swmm_hydrograph_add(e, "Storm",    0, 0, 0.1, 1, 2, 0,0,0), SWMM_OK);
        QCOMPARE(swmm_hydrograph_add(e, "Sanitary", 1, 0, 0.1, 1, 2, 0,0,0), SWMM_OK);

        QCOMPARE(swmm_hydrograph_group_count(e), 3);
        QCOMPARE(guiNameForHydrographGroup(e, 0), QStringLiteral("Combined"));
        QCOMPARE(guiNameForHydrographGroup(e, 1), QStringLiteral("Sanitary"));
        QCOMPARE(guiNameForHydrographGroup(e, 2), QStringLiteral("Storm"));
        // Past-the-end returns an empty string via the GUI helper.
        QCOMPARE(guiNameForHydrographGroup(e, 3), QString());
        swmm_engine_destroy(e);
    }

    /*! \brief A gage-only group (no parameter rows yet) still appears
     *         in the browser enumeration. */
    void hydrographGageOnlyGroupAppears()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        QCOMPARE(swmm_hydrograph_add_gage(e, "GageOnly", "G1"), SWMM_OK);
        QCOMPARE(swmm_hydrograph_add(e, "Params", -1, 0,
                                      0.1, 1, 2, 0, 0, 0), SWMM_OK);

        QCOMPARE(swmm_hydrograph_group_count(e), 2);
        QCOMPARE(guiNameForHydrographGroup(e, 0), QStringLiteral("Params"));
        QCOMPARE(guiNameForHydrographGroup(e, 1), QStringLiteral("GageOnly"));
        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Control rule naming (DA-ENG-02)
    // ====================================================================

    /*! \brief A well-formed RULE block surfaces its user-supplied name,
     *         not the legacy "Rule N" fallback. */
    void controlRuleNamedRuleSurfacesUserName()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        QCOMPARE(swmm_control_add_rule(e,
            "RULE PumpOnHigh\n"
            "IF NODE J1 DEPTH > 5\n"
            "THEN PUMP P1 STATUS = ON"), SWMM_OK);
        QCOMPARE(guiNameForControlRule(e, 0), QStringLiteral("PumpOnHigh"));
        swmm_engine_destroy(e);
    }

    /*! \brief Lowercase / mixed-case `rule` keywords still parse. */
    void controlRuleLowercaseKeyword()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        QCOMPARE(swmm_control_add_rule(e,
            "rule WeirBypass\nIF NODE J1 DEPTH < 1\nTHEN PUMP P1 STATUS = OFF"),
            SWMM_OK);
        QCOMPARE(swmm_control_add_rule(e,
            "Rule TankFill\nIF NODE J1 DEPTH < 2\nTHEN PUMP P1 STATUS = ON"),
            SWMM_OK);
        QCOMPARE(guiNameForControlRule(e, 0), QStringLiteral("WeirBypass"));
        QCOMPARE(guiNameForControlRule(e, 1), QStringLiteral("TankFill"));
        swmm_engine_destroy(e);
    }

    /*! \brief A rule with no parseable RULE keyword surfaces the
     *         "Rule N [unnamed]" sentinel so users can still locate and
     *         fix it. */
    void controlRuleMalformedSurfacesSentinel()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        // No RULE keyword at all.
        QCOMPARE(swmm_control_add_rule(e,
            "IF NODE J1 DEPTH > 5\nTHEN PUMP P1 STATUS = ON"), SWMM_OK);
        // RULES (plural) — must not match the RULE keyword.
        QCOMPARE(swmm_control_add_rule(e, "RULES are not RULE\n"), SWMM_OK);

        QCOMPARE(guiNameForControlRule(e, 0), QStringLiteral("Rule 1 [unnamed]"));
        QCOMPARE(guiNameForControlRule(e, 1), QStringLiteral("Rule 2 [unnamed]"));
        swmm_engine_destroy(e);
    }

    /*! \brief swmm_control_count + the new name accessor agree on the
     *         set size after mutations — the Object Browser refreshes
     *         consistently. */
    void controlRuleBrowserRefreshAfterAdd()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        QCOMPARE(swmm_control_count(e), 0);

        QCOMPARE(swmm_control_add_rule(e,
            "RULE R1\nIF NODE J1 DEPTH > 5\nTHEN PUMP P1 STATUS = ON"),
            SWMM_OK);
        QCOMPARE(swmm_control_count(e), 1);
        QCOMPARE(guiNameForControlRule(e, 0), QStringLiteral("R1"));

        QCOMPARE(swmm_control_add_rule(e,
            "RULE R2\nIF NODE J1 DEPTH < 1\nTHEN PUMP P1 STATUS = OFF"),
            SWMM_OK);
        QCOMPARE(swmm_control_count(e), 2);
        QCOMPARE(guiNameForControlRule(e, 1), QStringLiteral("R2"));
        swmm_engine_destroy(e);
    }

    /*! \brief hydrograph_group_count + group_id remain in agreement
     *         after add_hydrograph mutations — the Object Browser
     *         refreshes consistently. */
    void hydrographBrowserRefreshAfterAdd()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        QCOMPARE(swmm_hydrograph_group_count(e), 0);

        QCOMPARE(swmm_hydrograph_add(e, "GroupA", -1, 0, 0.1, 1, 2, 0,0,0), SWMM_OK);
        QCOMPARE(swmm_hydrograph_group_count(e), 1);
        QCOMPARE(guiNameForHydrographGroup(e, 0), QStringLiteral("GroupA"));

        // Adding another row in the same group doesn't change the count.
        QCOMPARE(swmm_hydrograph_add(e, "GroupA",  0, 0, 0.1, 1, 2, 0,0,0), SWMM_OK);
        QCOMPARE(swmm_hydrograph_group_count(e), 1);

        // A new group bumps the count by exactly one.
        QCOMPARE(swmm_hydrograph_add(e, "GroupB", -1, 0, 0.1, 1, 2, 0,0,0), SWMM_OK);
        QCOMPARE(swmm_hydrograph_group_count(e), 2);
        QCOMPARE(guiNameForHydrographGroup(e, 1), QStringLiteral("GroupB"));
        swmm_engine_destroy(e);
    }
};

QTEST_GUILESS_MAIN(TestDataObjectNaming)
#include "test_dataobjectnaming.moc"
