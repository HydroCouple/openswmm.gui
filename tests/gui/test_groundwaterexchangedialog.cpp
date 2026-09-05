/*!
 * \file   test_groundwaterexchangedialog.cpp
 * \brief  GroundwaterExchangeDialog (AQUIFER_GROUNDWATER_EXCHANGE_GUI_PLAN
 *         G4): loads [GROUNDWATER] / [GWF] values from a fixture, Apply
 *         writes node + params + expressions (empty clears), Apply is gated
 *         on expression validity and on an assigned aquifer, and the shared
 *         groundwaterSummary() strings agree with the engine state.
 */

#include "ui/dialogs/groundwaterexchangedialog.h"
#include "ui/properties/groundwatersummary.h"
#include "ui/widgets/gwfexpressionedit.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_subcatchments.h>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QObject>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

using openswmmvis::ui::GwfExpressionEdit;

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

// Opens tests/gui/data/gw_exchange_fixture.inp: S1 on AQ1 → J1 with a
// LATERAL expression; J1 —C1— O1.
SWMM_Engine openFixture()
{
    const QString inp = dataDir() + QStringLiteral("/gw_exchange_fixture.inp");
    if (!QFile::exists(inp)) return nullptr;
    SWMM_Engine e = swmm_engine_create();
    if (!e) return nullptr;
    const QString rpt = dataDir() + QStringLiteral("/gw_exchange_fixture.rpt");
    if (swmm_engine_open(e, inp.toUtf8().constData(),
                         rpt.toUtf8().constData(), nullptr, nullptr) != SWMM_OK) {
        swmm_engine_destroy(e);
        return nullptr;
    }
    return e;
}

SubcatchCompoundEditRef makeRef(SWMM_Engine e, const QString &sub)
{
    SubcatchCompoundEditRef r;
    r.engine  = e;
    r.subName = sub;
    r.kind    = SubcatchCompoundEditRef::Groundwater;
    return r;
}

QString gwfExpr(SWMM_Engine e, int s, int type)
{
    char buf[512] = {};
    swmm_subcatch_get_gwf_expression(e, s, type, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

} // namespace

class TestGroundwaterExchangeDialog : public QObject
{
    Q_OBJECT

private slots:
    void loadsExistingValues()
    {
        SWMM_Engine e = openFixture();
        QVERIFY2(e, "fixture gw_exchange_fixture.inp missing or failed to open");
        GroundwaterExchangeDialog dlg(makeRef(e, QStringLiteral("S1")));

        auto *node = dlg.findChild<QComboBox *>(QStringLiteral("gwNode"));
        QVERIFY(node);
        QCOMPARE(node->currentText(), QStringLiteral("J1"));
        // "(none)" + every node.
        QCOMPARE(node->count(), 1 + swmm_node_count(e));
        QCOMPARE(node->itemText(0), QStringLiteral("(none)"));

        QCOMPARE(dlg.findChild<QDoubleSpinBox *>(QStringLiteral("gwSurfEl"))->value(), 12.0);
        QCOMPARE(dlg.findChild<QDoubleSpinBox *>(QStringLiteral("gwA1"))->value(), 0.001);
        QCOMPARE(dlg.findChild<QDoubleSpinBox *>(QStringLiteral("gwB1"))->value(), 1.0);

        auto *lateral = dlg.findChild<GwfExpressionEdit *>(QStringLiteral("gwLateral"));
        auto *deep    = dlg.findChild<GwfExpressionEdit *>(QStringLiteral("gwDeep"));
        QVERIFY(lateral && deep);
        QCOMPARE(lateral->expression(), QStringLiteral("0.001*(HGW-10)"));
        QVERIFY(deep->expression().isEmpty());

        QVERIFY(dlg.canApply());
        QVERIFY(dlg.findChild<QPushButton *>(QStringLiteral("gwApply"))->isEnabled());

        swmm_engine_close(e);
        swmm_engine_destroy(e);
    }

    void applyWritesParamsAndExpressions()
    {
        SWMM_Engine e = openFixture();
        QVERIFY(e);
        GroundwaterExchangeDialog dlg(makeRef(e, QStringLiteral("S1")));
        QSignalSpy applied(&dlg, &GroundwaterExchangeDialog::applied);

        auto *node = dlg.findChild<QComboBox *>(QStringLiteral("gwNode"));
        node->setCurrentText(QStringLiteral("O1"));
        dlg.findChild<QDoubleSpinBox *>(QStringLiteral("gwSurfEl"))->setValue(15.5);
        dlg.findChild<QDoubleSpinBox *>(QStringLiteral("gwA2"))->setValue(0.25);
        dlg.findChild<QDoubleSpinBox *>(QStringLiteral("gwHstar"))->setValue(2.0);
        auto *lateral = dlg.findChild<GwfExpressionEdit *>(QStringLiteral("gwLateral"));
        auto *deep    = dlg.findChild<GwfExpressionEdit *>(QStringLiteral("gwDeep"));
        lateral->setExpression(QString());                    // clear
        deep->setExpression(QStringLiteral("0.0005*HGW"));    // set
        QVERIFY(dlg.canApply());

        dlg.findChild<QPushButton *>(QStringLiteral("gwApply"))->click();
        QCOMPARE(applied.count(), 1);

        const int s = swmm_subcatch_index(e, "S1");
        int nd = -1;
        swmm_subcatch_get_gw_node(e, s, &nd);
        QCOMPARE(nd, swmm_node_index(e, "O1"));
        double surf=0,a1=0,b1=0,a2=0,b2=0,a3=0,tw=0,hstar=0;
        swmm_subcatch_get_gw_params(e, s, &surf, &a1, &b1, &a2, &b2, &a3, &tw, &hstar);
        QCOMPARE(surf, 15.5);
        QCOMPARE(a1, 0.001);
        QCOMPARE(a2, 0.25);
        QCOMPARE(hstar, 2.0);
        QVERIFY(gwfExpr(e, s, SWMM_GWF_LATERAL).isEmpty());
        QCOMPARE(gwfExpr(e, s, SWMM_GWF_DEEP), QStringLiteral("0.0005*HGW"));
        QCOMPARE(dlg.updatedSummary(), QStringLiteral("AQ1 → O1 (custom)"));

        swmm_engine_close(e);
        swmm_engine_destroy(e);
    }

    void applyDisabledWhileExpressionInvalid()
    {
        SWMM_Engine e = openFixture();
        QVERIFY(e);
        GroundwaterExchangeDialog dlg(makeRef(e, QStringLiteral("S1")));
        auto *apply   = dlg.findChild<QPushButton *>(QStringLiteral("gwApply"));
        auto *lateral = dlg.findChild<GwfExpressionEdit *>(QStringLiteral("gwLateral"));
        QVERIFY(apply && lateral);

        lateral->setExpression(QStringLiteral("0.001*(HGWW-10)"));
        QVERIFY(!dlg.canApply());
        QVERIFY(!apply->isEnabled());
        QVERIFY(!apply->toolTip().isEmpty());

        // Clicking a disabled button must not write.
        QSignalSpy applied(&dlg, &GroundwaterExchangeDialog::applied);
        apply->click();
        QCOMPARE(applied.count(), 0);
        QCOMPARE(gwfExpr(e, swmm_subcatch_index(e, "S1"), SWMM_GWF_LATERAL),
                 QStringLiteral("0.001*(HGW-10)"));

        lateral->setExpression(QStringLiteral("0.001*(HGW-10)"));
        QVERIFY(dlg.canApply());
        QVERIFY(apply->isEnabled());

        swmm_engine_close(e);
        swmm_engine_destroy(e);
    }

    void applyDisabledWithoutAquifer()
    {
        SWMM_Engine e = openFixture();
        QVERIFY(e);
        const int s = swmm_subcatch_index(e, "S1");
        QCOMPARE(swmm_subcatch_set_aquifer(e, s, -1), SWMM_OK);

        GroundwaterExchangeDialog dlg(makeRef(e, QStringLiteral("S1")));
        QVERIFY(!dlg.canApply());
        QVERIFY(!dlg.findChild<QPushButton *>(QStringLiteral("gwApply"))->isEnabled());
        QVERIFY(!dlg.findChild<QComboBox *>(QStringLiteral("gwNode"))->isEnabled());

        swmm_engine_close(e);
        swmm_engine_destroy(e);
    }

    void summaryStrings()
    {
        SWMM_Engine e = openFixture();
        QVERIFY(e);
        const int s = swmm_subcatch_index(e, "S1");

        QCOMPARE(groundwaterSummary(e, s), QStringLiteral("AQ1 → J1 (custom)"));

        QCOMPARE(swmm_subcatch_set_gwf_expression(e, s, SWMM_GWF_LATERAL, ""), SWMM_OK);
        QCOMPARE(groundwaterSummary(e, s), QStringLiteral("AQ1 → J1"));

        QCOMPARE(swmm_subcatch_set_gw_node(e, s, -1), SWMM_OK);
        QCOMPARE(groundwaterSummary(e, s), QStringLiteral("AQ1 → (no node)"));

        QCOMPARE(swmm_subcatch_set_aquifer(e, s, -1), SWMM_OK);
        QCOMPARE(groundwaterSummary(e, s), QStringLiteral("(none)"));

        QCOMPARE(groundwaterSummary(e, -1), QStringLiteral("(none)"));
        QCOMPARE(groundwaterSummary(nullptr, 0), QStringLiteral("(none)"));

        swmm_engine_close(e);
        swmm_engine_destroy(e);
    }
};

QTEST_MAIN(TestGroundwaterExchangeDialog)
#include "test_groundwaterexchangedialog.moc"
