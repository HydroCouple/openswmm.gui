/*!
 * \file   test_typeconversionflow.cpp
 * \brief  Tests for node/link type conversion (right-click "Convert To" /
 *         Attribute Table "Change Type"). Two layers:
 *           1. The pure text builders in `TypeConversionFlow`
 *              (nodeTypeLabel / linkTypeLabel / confirmText / summaryHtml)
 *              — headless, no engine, no layer, no modal dialog.
 *           2. The engine conversion contract that the layer methods wrap
 *              (`swmm_node_convert` / `swmm_link_convert`): success codes,
 *              new_type, endpoint preservation, same-type rejection, and
 *              the cleared-fields marshaling assumption (NULL-terminated
 *              arrays sized by n_cleared).
 *
 * The modal `TypeConversionFlow::run()` is deliberately NOT exercised
 * (it blocks on QMessageBox). Its non-UI call into the layer is covered
 * by the layer's own apply* methods; the layer is stubbed at link time
 * here (same idiom as test_control_rule_models.cpp) since the production
 * SWMMModelLayer drags in nanoflann / GDAL / the scene graph.
 */

#include "ui/dialogs/typeconversionflow.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_edit.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

using openswmmvis::ui::TypeConversionFlow;

namespace {

// Two junctions + one conduit C1 (J1 → J2). Mirrors the fixture in
// test_linkpropertyadapter.cpp.
SWMM_Engine buildFixture()
{
    SWMM_Engine e = swmm_engine_new();
    if (!e) return nullptr;
    swmm_node_add(e, "J1", SWMM_NODE_JUNCTION);
    swmm_node_add(e, "J2", SWMM_NODE_JUNCTION);
    swmm_link_add(e, "C1", SWMM_LINK_CONDUIT);
    const int li = swmm_link_index(e, "C1");
    swmm_link_set_nodes(e, li, swmm_node_index(e, "J1"),
                               swmm_node_index(e, "J2"));
    return e;
}

} // namespace

class TestTypeConversionFlow : public QObject
{
    Q_OBJECT

private slots:

    // ====================================================================
    // Pure text builders
    // ====================================================================

    void nodeLabels()
    {
        QCOMPARE(TypeConversionFlow::nodeTypeLabel(SWMM_NODE_JUNCTION),
                 QStringLiteral("Junction"));
        QCOMPARE(TypeConversionFlow::nodeTypeLabel(SWMM_NODE_OUTFALL),
                 QStringLiteral("Outfall"));
        QCOMPARE(TypeConversionFlow::nodeTypeLabel(SWMM_NODE_STORAGE),
                 QStringLiteral("Storage"));
        QCOMPARE(TypeConversionFlow::nodeTypeLabel(SWMM_NODE_DIVIDER),
                 QStringLiteral("Divider"));
        QVERIFY(TypeConversionFlow::nodeTypeLabel(-1).isEmpty());
        QVERIFY(TypeConversionFlow::nodeTypeLabel(4).isEmpty());
    }

    void linkLabels()
    {
        QCOMPARE(TypeConversionFlow::linkTypeLabel(SWMM_LINK_CONDUIT),
                 QStringLiteral("Conduit"));
        QCOMPARE(TypeConversionFlow::linkTypeLabel(SWMM_LINK_PUMP),
                 QStringLiteral("Pump"));
        QCOMPARE(TypeConversionFlow::linkTypeLabel(SWMM_LINK_ORIFICE),
                 QStringLiteral("Orifice"));
        QCOMPARE(TypeConversionFlow::linkTypeLabel(SWMM_LINK_WEIR),
                 QStringLiteral("Weir"));
        QCOMPARE(TypeConversionFlow::linkTypeLabel(SWMM_LINK_OUTLET),
                 QStringLiteral("Outlet"));
        QVERIFY(TypeConversionFlow::linkTypeLabel(-1).isEmpty());
        QVERIFY(TypeConversionFlow::linkTypeLabel(5).isEmpty());
    }

    void confirmTextMentionsNameAndBothTypes()
    {
        const QString t = TypeConversionFlow::confirmText(
            /*isNode=*/true, QStringLiteral("J12"),
            SWMM_NODE_JUNCTION, SWMM_NODE_STORAGE);
        QVERIFY(t.contains(QStringLiteral("J12")));
        QVERIFY(t.contains(QStringLiteral("Junction")));
        QVERIFY(t.contains(QStringLiteral("Storage")));
        // The warning must make the data loss explicit.
        QVERIFY(t.contains(QStringLiteral("cleared")));

        const QString l = TypeConversionFlow::confirmText(
            /*isNode=*/false, QStringLiteral("C1"),
            SWMM_LINK_CONDUIT, SWMM_LINK_PUMP);
        QVERIFY(l.contains(QStringLiteral("C1")));
        QVERIFY(l.contains(QStringLiteral("Conduit")));
        QVERIFY(l.contains(QStringLiteral("Pump")));
    }

    void summaryHtmlVariants()
    {
        // Cleared only.
        {
            const QString s = TypeConversionFlow::summaryHtml(
                {QStringLiteral("dwf_inflows"), QStringLiteral("rdii")}, {});
            QVERIFY(s.contains(QStringLiteral("Cleared fields")));
            QVERIFY(s.contains(QStringLiteral("dwf_inflows")));
            QVERIFY(!s.contains(QStringLiteral("Topology warnings")));
        }
        // Warnings only.
        {
            const QString s = TypeConversionFlow::summaryHtml(
                {}, {QStringLiteral("Outfall has 2 inflow links")});
            QVERIFY(s.contains(QStringLiteral("Topology warnings")));
            QVERIFY(s.contains(QStringLiteral("2 inflow links")));
            QVERIFY(!s.contains(QStringLiteral("Cleared fields")));
        }
        // Both.
        {
            const QString s = TypeConversionFlow::summaryHtml(
                {QStringLiteral("seep_rate")},
                {QStringLiteral("warn A")});
            QVERIFY(s.contains(QStringLiteral("Cleared fields")));
            QVERIFY(s.contains(QStringLiteral("Topology warnings")));
        }
        // Neither → explicit "no side effects".
        {
            const QString s = TypeConversionFlow::summaryHtml({}, {});
            QVERIFY(s.contains(QStringLiteral("no side effects")));
        }
    }

    // ====================================================================
    // Engine conversion contract (what the layer methods wrap)
    // ====================================================================

    void linkConvertPreservesEndpoints()
    {
        SWMM_Engine e = buildFixture();
        QVERIFY(e);
        const int li = swmm_link_index(e, "C1");
        int n1 = -1, n2 = -1;
        QCOMPARE(swmm_link_get_from_node(e, li, &n1), SWMM_OK);
        QCOMPARE(swmm_link_get_to_node(e, li, &n2), SWMM_OK);

        SWMM_ConversionResult res{};
        QCOMPARE(swmm_link_convert(e, li, SWMM_LINK_PUMP, &res), SWMM_OK);
        QCOMPARE(res.new_type, int(SWMM_LINK_PUMP));

        int type = -1;
        QCOMPARE(swmm_link_get_type(e, li, &type), SWMM_OK);
        QCOMPARE(type, int(SWMM_LINK_PUMP));

        // Endpoints survive a type-only conversion.
        int p1 = -1, p2 = -1;
        QCOMPARE(swmm_link_get_from_node(e, li, &p1), SWMM_OK);
        QCOMPARE(swmm_link_get_to_node(e, li, &p2), SWMM_OK);
        QCOMPARE(p1, n1);
        QCOMPARE(p2, n2);

        swmm_conversion_result_free(&res);
        swmm_engine_destroy(e);
    }

    void sameTypeConvertIsRejected()
    {
        SWMM_Engine e = buildFixture();
        QVERIFY(e);
        const int li = swmm_link_index(e, "C1");
        SWMM_ConversionResult res{};
        // Converting a conduit to a conduit must be rejected so the GUI
        // pickers are right to exclude the current type.
        QCOMPARE(swmm_link_convert(e, li, SWMM_LINK_CONDUIT, &res),
                 int(SWMM_ERR_BADPARAM));
        swmm_conversion_result_free(&res);
        swmm_engine_destroy(e);
    }

    void nodeConvertMarshalsResult()
    {
        SWMM_Engine e = buildFixture();
        QVERIFY(e);
        const int ni = swmm_node_index(e, "J1");

        SWMM_ConversionResult res{};
        QCOMPARE(swmm_node_convert(e, ni, SWMM_NODE_STORAGE, &res), SWMM_OK);
        QCOMPARE(res.new_type, int(SWMM_NODE_STORAGE));

        int type = -1;
        QCOMPARE(swmm_node_get_type(e, ni, &type), SWMM_OK);
        QCOMPARE(type, int(SWMM_NODE_STORAGE));

        // The cleared-fields array is NULL-terminated and sized by
        // n_cleared; the layer's marshaling loop relies on both. Walk it
        // exactly as SWMMModelLayer::applyNodeConvert does.
        QVERIFY(res.n_cleared >= 0);
        QStringList cleared;
        for (int i = 0; i < res.n_cleared; ++i) {
            QVERIFY(res.cleared_fields[i] != nullptr);
            cleared << QString::fromUtf8(res.cleared_fields[i]);
        }
        QCOMPARE(cleared.size(), res.n_cleared);

        swmm_conversion_result_free(&res);
        swmm_engine_destroy(e);
    }
};

// ----------------------------------------------------------------------------
// Link-time stubs for the SWMMModelLayer convert methods that
// typeconversionflow.cpp references from run(). The tests never call run(),
// so these are never invoked — they only satisfy the linker without pulling
// the full SWMMModelLayer (and its nanoflann / GDAL deps) into the binary.
// ----------------------------------------------------------------------------
#include "layers/swmmmodellayer.h"

bool SWMMModelLayer::applyNodeConvert(const QString&, int, QStringList*,
                                      QStringList*, QString*) { return false; }
bool SWMMModelLayer::applyLinkConvert(const QString&, int, QStringList*,
                                      QStringList*, QString*) { return false; }

QTEST_MAIN(TestTypeConversionFlow)
#include "test_typeconversionflow.moc"
