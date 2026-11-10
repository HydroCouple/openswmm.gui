/**
 * @file      test_typeconversionflow.cpp
 * @brief     Tests for node/link type conversion (right-click "Convert To" /
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
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/dialogs/typeconversionflow.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_edit.h>

#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <QScopeGuard>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

using openswmmvis::ui::TypeConversionFlow;

namespace
{

// Two junctions + one conduit C1 (J1 → J2). Mirrors the fixture in
// test_linkpropertyadapter.cpp.
/**
 *
 * @brief Allocate a BUILDING engine with two junctions and an attempted conduit binding between
 * them.
 * @return Caller-owned engine, destroyed with swmm_engine_destroy(), or null if allocation
 * fails. Individual add/index/set-nodes statuses are unchecked; caller assertions cover
 * needed behavior. No geometry initialization or solver run.
 *
 */
SWMM_Engine buildFixture()
{
    SWMM_Engine e = swmm_engine_new();
    if (!e)
        return nullptr;
    swmm_node_add(e, "J1", SWMM_NODE_JUNCTION);
    swmm_node_add(e, "J2", SWMM_NODE_JUNCTION);
    swmm_link_add(e, "C1", SWMM_LINK_CONDUIT);
    const int li = swmm_link_index(e, "C1");
    swmm_link_set_nodes(e, li, swmm_node_index(e, "J1"), swmm_node_index(e, "J2"));
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

    /**
     *
     * @brief Pin four native node labels, virtual/inlet GUI pseudo-types and empty labels for
     * invalid values.
     * No conversion, dialog or native engine is involved.
     *
     */
    void nodeLabels()
    {
        QCOMPARE(TypeConversionFlow::nodeTypeLabel(SWMM_NODE_JUNCTION), QStringLiteral("Junction"));
        QCOMPARE(TypeConversionFlow::nodeTypeLabel(SWMM_NODE_OUTFALL), QStringLiteral("Outfall"));
        QCOMPARE(TypeConversionFlow::nodeTypeLabel(SWMM_NODE_STORAGE), QStringLiteral("Storage"));
        QCOMPARE(TypeConversionFlow::nodeTypeLabel(SWMM_NODE_DIVIDER), QStringLiteral("Divider"));
        QVERIFY(TypeConversionFlow::nodeTypeLabel(-1).isEmpty());
        // GUI pseudo-type: virtual junction surfaces as its own node kind.
        QCOMPARE(TypeConversionFlow::nodeTypeLabel(TypeConversionFlow::kVirtualNodeType),
                 QStringLiteral("Virtual Junction"));
        QCOMPARE(TypeConversionFlow::nodeTypeLabel(TypeConversionFlow::kInletNodeType),
                 QStringLiteral("Inlet Junction"));
        QVERIFY(TypeConversionFlow::nodeTypeLabel(6).isEmpty());
    }

    /**
     *
     * @brief Pin all five native link labels and empty labels for minus one/five.
     * No conversion or model layer is instantiated.
     *
     */
    void linkLabels()
    {
        QCOMPARE(TypeConversionFlow::linkTypeLabel(SWMM_LINK_CONDUIT), QStringLiteral("Conduit"));
        QCOMPARE(TypeConversionFlow::linkTypeLabel(SWMM_LINK_PUMP), QStringLiteral("Pump"));
        QCOMPARE(TypeConversionFlow::linkTypeLabel(SWMM_LINK_ORIFICE), QStringLiteral("Orifice"));
        QCOMPARE(TypeConversionFlow::linkTypeLabel(SWMM_LINK_WEIR), QStringLiteral("Weir"));
        QCOMPARE(TypeConversionFlow::linkTypeLabel(SWMM_LINK_OUTLET), QStringLiteral("Outlet"));
        QVERIFY(TypeConversionFlow::linkTypeLabel(-1).isEmpty());
        QVERIFY(TypeConversionFlow::linkTypeLabel(5).isEmpty());
    }

    /**
     *
     * @brief Require pure confirmation text to mention node/link identity and source/destination
     * types.
     * Checks cleared warning for node conversion only; no modal confirmation or HTML-escaping
     * edge case is exercised.
     *
     */
    void confirmTextMentionsNameAndBothTypes()
    {
        const QString t = TypeConversionFlow::confirmText(
            /*isNode=*/true, QStringLiteral("J12"), SWMM_NODE_JUNCTION, SWMM_NODE_STORAGE);
        QVERIFY(t.contains(QStringLiteral("J12")));
        QVERIFY(t.contains(QStringLiteral("Junction")));
        QVERIFY(t.contains(QStringLiteral("Storage")));
        // The warning must make the data loss explicit.
        QVERIFY(t.contains(QStringLiteral("cleared")));

        const QString l = TypeConversionFlow::confirmText(
            /*isNode=*/false, QStringLiteral("C1"), SWMM_LINK_CONDUIT, SWMM_LINK_PUMP);
        QVERIFY(l.contains(QStringLiteral("C1")));
        QVERIFY(l.contains(QStringLiteral("Conduit")));
        QVERIFY(l.contains(QStringLiteral("Pump")));
    }

    /**
     *
     * @brief Require cleared-only, warning-only, both and neither summary variants to
     * include/exclude their section text.
     * Checks representative supplied strings and no-side-effects wording; no rendered HTML,
     * escaping or native conversion occurs.
     *
     */
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
            const QString s =
                TypeConversionFlow::summaryHtml({}, {QStringLiteral("Outfall has 2 inflow links")});
            QVERIFY(s.contains(QStringLiteral("Topology warnings")));
            QVERIFY(s.contains(QStringLiteral("2 inflow links")));
            QVERIFY(!s.contains(QStringLiteral("Cleared fields")));
        }
        // Both.
        {
            const QString s = TypeConversionFlow::summaryHtml({QStringLiteral("seep_rate")},
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

    /**
     *
     * @brief Require conduit-to-pump conversion to return/store Pump and retain both native
     * endpoint indices.
     * Frees conversion-result allocations then destroys the engine on success; early Qt
     * assertion return lacks scope cleanup. No hydraulic initialization or GUI action.
     *
     */
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

    /**
     *
     * @brief Require conduit-to-conduit conversion to return BADPARAM.
     * Frees the result and engine on success; does not independently compare all post-refusal
     * model fields or error text.
     *
     */
    void sameTypeConvertIsRejected()
    {
        SWMM_Engine e = buildFixture();
        QVERIFY(e);
        const int li = swmm_link_index(e, "C1");
        SWMM_ConversionResult res{};
        // Converting a conduit to a conduit must be rejected so the GUI
        // pickers are right to exclude the current type.
        QCOMPARE(swmm_link_convert(e, li, SWMM_LINK_CONDUIT, &res), int(SWMM_ERR_BADPARAM));
        swmm_conversion_result_free(&res);
        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require junction-to-storage conversion and counted cleared-field strings to marshal
     * safely.
     * Checks native/result new type, nonnegative count, each counted pointer nonnull and
     * copied-list length. Does not inspect an extra null terminator after n_cleared despite
     * the older comment's broader claim. Frees result/engine on the successful path.
     *
     */
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
    void dependentLinkDoesNotBlockIndependentLink()
    {
        auto e = buildFixture();
        QVERIFY(e);
        const auto cleanup = qScopeGuard([&] { swmm_engine_destroy(e); });
        QCOMPARE(swmm_node_add(e, "J3", SWMM_NODE_JUNCTION), SWMM_OK);
        QCOMPARE(swmm_link_add(e, "C2", SWMM_LINK_CONDUIT), SWMM_OK);
        QCOMPARE(swmm_link_set_nodes(e, 1, 1, 2), SWMM_OK);
        QCOMPARE(swmm_node_set_virtual(e, 1, 1), SWMM_OK);
        QCOMPARE(swmm_link_add(e, "free", SWMM_LINK_CONDUIT), SWMM_OK);
        QCOMPARE(swmm_link_set_nodes(e, 2, 0, 2), SWMM_OK);
        QCOMPARE(swmm_link_add(e, "pump", SWMM_LINK_PUMP), SWMM_OK);
        int ids[] = {0, 2, 3, -1};
        for (int apply : {0, 1}) {
            SWMM_TypeChange rows[4]{};
            const auto release = qScopeGuard([&] {
                for (auto &row : rows)
                    swmm_conversion_result_free(&row.details);
            });
            QCOMPARE(swmm_convert_types(e, 0, SWMM_LINK_PUMP, ids, 4, -1, -1, 0, apply, rows),
                     SWMM_OK);
            QCOMPARE(rows[0].status, 609);
            QCOMPARE(rows[0].dependency_node, 1);
            QCOMPARE(rows[1].status, SWMM_OK);
            QCOMPARE(rows[2].source_type, int(SWMM_LINK_PUMP));
            QCOMPARE(rows[3].status, SWMM_ERR_BADINDEX);
            int type = -1, rule = -1;
            QCOMPARE(swmm_link_get_type(e, 2, &type), SWMM_OK);
            QCOMPARE(type, int(apply ? SWMM_LINK_PUMP : SWMM_LINK_CONDUIT));
            QCOMPARE(swmm_node_virtual_eligible(e, 1, &rule), SWMM_OK);
            QCOMPARE(rule, 0);
            if (apply)
                QVERIFY(rows[1].details.n_warnings > 0);
        }
        QCOMPARE(swmm_link_convert(e, 0, SWMM_LINK_PUMP, nullptr), 609);
    }

    void inletPromotionIsAtomic()
    {
        auto e = buildFixture();
        QVERIFY(e);
        const auto cleanup = qScopeGuard([&] { swmm_engine_destroy(e); });
        QCOMPARE(swmm_options_set(e, "FLOW_ROUTING", "DYNWAVE"), SWMM_OK);
        QCOMPARE(swmm_node_add(e, "J3", SWMM_NODE_JUNCTION), SWMM_OK);
        QCOMPARE(swmm_link_add(e, "C2", SWMM_LINK_CONDUIT), SWMM_OK);
        QCOMPARE(swmm_link_set_nodes(e, 1, 1, 2), SWMM_OK);
        for (int i : {0, 1})
            QCOMPARE(swmm_link_set_xsect(e, i, SWMM_XSECT_RECT_OPEN, 3, 4, 0, 0), SWMM_OK);
        QCOMPARE(swmm_node_convert(e, 1, SWMM_NODE_STORAGE, nullptr), SWMM_OK);
        QCOMPARE(swmm_node_set_storage_functional(e, 1, 12, 1.5, 7), SWMM_OK);
        QCOMPARE(swmm_node_set_max_depth(e, 1, 9), SWMM_OK);
        QCOMPARE(swmm_inlet_add(e, "Drop", "DROP_GRATE"), SWMM_OK);
        QCOMPARE(swmm_inlet_add(e, "Street", "CURB"), SWMM_OK);
        const int host = 1;
        auto change = [&](int target, int design, int capture, int apply) {
            SWMM_TypeChange row{};
            const int rc = swmm_convert_types(e, 1, target, &host, 1, design, capture,
                                              SWMM_INLET_ON_SAG, apply, &row);
            swmm_conversion_result_free(&row.details);
            return rc ? rc : row.status;
        };
        QCOMPARE(change(SWMM_CONVERT_INLET, 0, host, 1), 627);
        QCOMPARE(change(SWMM_CONVERT_INLET, 1, 0, 1), 623);
        QCOMPARE(change(SWMM_CONVERT_INLET, -1, 0, 1), 625);
        int type = -1, flag = -1;
        QCOMPARE(swmm_node_get_type(e, host, &type), SWMM_OK);
        QCOMPARE(type, int(SWMM_NODE_STORAGE));
        double a = 0, b = 0, c = 0, depth = 0;
        QCOMPARE(swmm_node_get_storage_functional(e, host, &a, &b, &c), SWMM_OK);
        QCOMPARE(a, 12.);
        QCOMPARE(b, 1.5);
        QCOMPARE(c, 7.);
        QCOMPARE(swmm_node_get_max_depth(e, host, &depth), SWMM_OK);
        QCOMPARE(depth, 9.);
        QCOMPARE(swmm_node_is_virtual(e, host, &flag), SWMM_OK);
        QCOMPARE(flag, 0);
        QCOMPARE(swmm_inlet_usage_count(e), 0);
        QCOMPARE(change(SWMM_CONVERT_INLET, -2, -1, 0), SWMM_OK);
        QCOMPARE(change(SWMM_CONVERT_INLET, 0, 0, 1), SWMM_OK);
        SWMM_InletUsage usage{};
        QCOMPARE(swmm_inlet_usage_get(e, 0, &usage), SWMM_OK);
        QCOMPARE(usage.host_idx, host);
        QCOMPARE(usage.capture_node_idx, 0);
        QCOMPARE(usage.placement, int(SWMM_INLET_ON_SAG));
        QCOMPARE(change(SWMM_CONVERT_VIRTUAL, -1, -1, 1), SWMM_OK);
        QCOMPARE(swmm_inlet_usage_count(e), 0);
        QCOMPARE(swmm_node_is_virtual(e, host, &flag), SWMM_OK);
        QCOMPARE(flag, 1);
        QCOMPARE(change(SWMM_NODE_JUNCTION, -1, -1, 1), SWMM_OK);
        QCOMPARE(swmm_node_is_virtual(e, host, &flag), SWMM_OK);
        QCOMPARE(flag, 0);

        usage.host_kind = SWMM_INLET_HOST_LINK;
        usage.host_idx = 0;
        QCOMPARE(swmm_inlet_usage_set(e, &usage, nullptr), SWMM_OK);
        QCOMPARE(change(SWMM_CONVERT_INLET, 0, 0, 1), 629);
        QCOMPARE(swmm_link_convert(e, 0, SWMM_LINK_WEIR, nullptr), 635);
    }
};

QTEST_MAIN(TestTypeConversionFlow)
#include "test_typeconversionflow.moc"
