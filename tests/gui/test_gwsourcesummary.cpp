/**
 * @file      test_gwsourcesummary.cpp
 * @brief     groundwaterSourceSubcatchments(): the node-side list of
 *         subcatchments discharging groundwater to a node, built from the
 *         engine alone (AQUIFER_GROUNDWATER_EXCHANGE_GUI_PLAN G5).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/gwsourcesummary.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_subcatchments.h>

#include <QFile>
#include <QObject>
#include <QTest>

using OpenSWMMVis::Groundwater::groundwaterSourceSubcatchments;

namespace
{

/**
 *
 * @brief Select SWMMVIS_GUI_TEST_DATA or the current directory for groundwater fixtures.
 * @return Owned unvalidated directory path; no creation.
 *
 */
QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

// tests/gui/data/gw_exchange_fixture.inp: S1 on AQ1 → J1; J1 —C1— O1.
/**
 *
 * @brief Open gw_exchange_fixture.inp and its adjacent report for source-summary cases.
 * @return Owned engine to close/destroy, or nullptr if input is absent, engine
 * allocation fails or open fails (failed handle destroyed). Writes/overwrites
 * gw_exchange_fixture.rpt under dataDir(); no simulation or source INP write.
 *
 */
SWMM_Engine openFixture()
{
    const QString inp = dataDir() + QStringLiteral("/gw_exchange_fixture.inp");
    if (!QFile::exists(inp))
        return nullptr;
    SWMM_Engine e = swmm_engine_create();
    if (!e)
        return nullptr;
    const QString rpt = dataDir() + QStringLiteral("/gw_exchange_fixture.rpt");
    if (swmm_engine_open(e, inp.toUtf8().constData(), rpt.toUtf8().constData(), nullptr, nullptr) !=
        SWMM_OK) {
        swmm_engine_destroy(e);
        return nullptr;
    }
    return e;
}

} // namespace

class TestGwSourceSummary : public QObject
{
    Q_OBJECT

private slots:
    /**
     *
     * @brief Require J1 to list groundwater source S1 and outfall O1 to list none in the loaded
     * fixture.
     *
     */
    void fixtureListsReceivingNodeSources()
    {
        SWMM_Engine e = openFixture();
        QVERIFY2(e, "fixture gw_exchange_fixture.inp missing or failed to open");

        QCOMPARE(groundwaterSourceSubcatchments(e, swmm_node_index(e, "J1")),
                 QStringList{QStringLiteral("S1")});
        QVERIFY(groundwaterSourceSubcatchments(e, swmm_node_index(e, "O1")).isEmpty());

        swmm_engine_close(e);
        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require aquifer removal/restoration and receiving-node reassignment to update source
     * summaries from engine state.
     *
     */
    void receivingNodeWithoutAquiferIsNotASource()
    {
        SWMM_Engine e = openFixture();
        QVERIFY(e);
        const int s = swmm_subcatch_index(e, "S1");
        const int j = swmm_node_index(e, "J1");

        QCOMPARE(swmm_subcatch_set_aquifer(e, s, -1), SWMM_OK);
        QVERIFY(groundwaterSourceSubcatchments(e, j).isEmpty());

        QCOMPARE(swmm_subcatch_set_aquifer(e, s, swmm_aquifer_index(e, "AQ1")), SWMM_OK);
        QCOMPARE(groundwaterSourceSubcatchments(e, j).size(), 1);

        // Re-pointing the receiving node moves the entry.
        QCOMPARE(swmm_subcatch_set_gw_node(e, s, swmm_node_index(e, "O1")), SWMM_OK);
        QVERIFY(groundwaterSourceSubcatchments(e, j).isEmpty());
        QCOMPARE(groundwaterSourceSubcatchments(e, swmm_node_index(e, "O1")),
                 QStringList{QStringLiteral("S1")});

        swmm_engine_close(e);
        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require null engine and invalid/absent node indices to yield empty groundwater
     * summaries.
     *
     */
    void badInputsAreEmpty()
    {
        QVERIFY(groundwaterSourceSubcatchments(nullptr, 0).isEmpty());
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        QVERIFY(groundwaterSourceSubcatchments(e, -1).isEmpty());
        QVERIFY(groundwaterSourceSubcatchments(e, 0).isEmpty());
        swmm_engine_destroy(e);
    }
};

QTEST_MAIN(TestGwSourceSummary)
#include "test_gwsourcesummary.moc"
