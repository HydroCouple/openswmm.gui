/*!
 * \file   test_vjsourcesummary.cpp
 * \brief  lateralSourceSummary(): the sentence the virtual-junction delete /
 *         re-fuse prompts show for a fed node, built from the engine alone
 *         (VJ_LATERAL_INFLOW_GUI_PLAN_2026-09-04).
 */

#include "layers/vjsourcesummary.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_inflows.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_subcatchments.h>

#include <QObject>
#include <QTest>

using OpenSWMMVis::VirtualJunction::lateralSourceSummary;

class TestVjSourceSummary : public QObject
{
    Q_OBJECT

private:
    // J1 —C1— MID —C2— O1 with MID flagged virtual. MID then receives one
    // DWF row and two [INFLOWS] rows and becomes the outlet of subcatchment
    // S1; J1 carries nothing. The flag is set before the sources are added
    // so the fixture builds against any engine version.
    SWMM_Engine buildFixture()
    {
        SWMM_Engine e = swmm_engine_new();
        if (!e) return nullptr;
        swmm_node_add(e, "J1",  0);
        swmm_node_add(e, "MID", 0);
        swmm_node_add(e, "O1",  1);
        const int j = swmm_node_index(e, "J1");
        const int m = swmm_node_index(e, "MID");
        const int o = swmm_node_index(e, "O1");
        swmm_node_set_invert_elev(e, j, 100.0);
        swmm_node_set_invert_elev(e, m,  97.5);
        swmm_node_set_invert_elev(e, o,  95.0);
        swmm_link_add(e, "C1", 0);   // 0 = conduit
        swmm_link_add(e, "C2", 0);
        swmm_link_set_nodes(e, swmm_link_index(e, "C1"), j, m);
        swmm_link_set_nodes(e, swmm_link_index(e, "C2"), m, o);
        swmm_node_set_virtual(e, m, 1);

        swmm_dwf_add(e, m, "FLOW", 0.5, "", "", "", "");
        swmm_ext_inflow_add(e, m, "FLOW", "TS1", "FLOW",   1.0, 1.0, 0.0, "");
        swmm_ext_inflow_add(e, m, "TSS",  "TS2", "CONCEN", 1.0, 1.0, 0.0, "");
        swmm_subcatch_add(e, "S1");
        swmm_subcatch_set_outlet(e, swmm_subcatch_index(e, "S1"), m);
        return e;
    }

private slots:
    void fedVirtualJunctionListsRowsAndSubcatchment()
    {
        SWMM_Engine e = buildFixture();
        QVERIFY(e);
        const int m = swmm_node_index(e, "MID");
        int isVirtual = 0;
        swmm_node_is_virtual(e, m, &isVirtual);
        QCOMPARE(isVirtual, 1);

        const QString text = lateralSourceSummary(e, m);
        QVERIFY2(text.contains(QStringLiteral("Re-fusing will remove its")),
                 qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("2 inflow entries")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("1 DWF entry")), qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("RDII")), qPrintable(text));
        QVERIFY2(text.contains(
                     QStringLiteral("Subcatchment \"S1\" will lose its outlet.")),
                 qPrintable(text));
        swmm_engine_destroy(e);
    }

    void unfedNodeIsEmpty()
    {
        SWMM_Engine e = buildFixture();
        QVERIFY(e);
        QVERIFY(lateralSourceSummary(e, swmm_node_index(e, "J1")).isEmpty());
        QVERIFY(lateralSourceSummary(e, -1).isEmpty());
        QVERIFY(lateralSourceSummary(nullptr, 0).isEmpty());
        swmm_engine_destroy(e);
    }
};

QTEST_MAIN(TestVjSourceSummary)
#include "test_vjsourcesummary.moc"
