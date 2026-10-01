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
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <QVector>

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

QVector<double> gwParams(SWMM_Engine e, int s)
{
    QVector<double> values(8);
    swmm_subcatch_get_gw_params(e, s, &values[0], &values[1], &values[2],
        &values[3], &values[4], &values[5], &values[6], &values[7]);
    return values;
}

} // namespace

class TestGroundwaterExchangeDialog : public QObject
{
    Q_OBJECT

private slots:
    void unchangedApplyPreservesExactValues()
    {
        SWMM_Engine e = openFixture();
        QVERIFY(e);
        const int s = swmm_subcatch_index(e, "S1");
        QCOMPARE(swmm_subcatch_set_gw_params(e, s, 12.123456789, 0.0000123456789,
            1.123456789, 0.0, 1.0, 0.0, 10.0, 9.0), SWMM_OK);
        const auto before = gwParams(e, s);
        GroundwaterExchangeDialog dlg(makeRef(e, QStringLiteral("S1")));
        QSignalSpy applied(&dlg, &GroundwaterExchangeDialog::applied);
        dlg.findChild<QPushButton *>(QStringLiteral("gwApply"))->click();
        QCOMPARE(gwParams(e, s), before);
        QCOMPARE(applied.count(), 0);
        swmm_engine_destroy(e);
    }

    void oneFieldEditPreservesOtherPrecisionAndRepeatIsNoOp()
    {
        SWMM_Engine e = openFixture();
        QVERIFY(e);
        const int s = swmm_subcatch_index(e, "S1");
        QCOMPARE(swmm_subcatch_set_gw_params(e, s, 12.123456789, 0.0000123456789,
            1.123456789, 0.0, 1.0, 0.0, 10.0, 9.0), SWMM_OK);
        auto expected = gwParams(e, s);
        GroundwaterExchangeDialog dlg(makeRef(e, QStringLiteral("S1")));
        QSignalSpy applied(&dlg, &GroundwaterExchangeDialog::applied);
        dlg.findChild<QDoubleSpinBox *>(QStringLiteral("gwA2"))->setValue(0.25);
        expected[3] = 0.25;
        auto *apply = dlg.findChild<QPushButton *>(QStringLiteral("gwApply"));
        apply->click();
        QCOMPARE(gwParams(e, s), expected);
        QCOMPARE(applied.count(), 1);
        apply->click();
        QCOMPARE(gwParams(e, s), expected);
        QCOMPARE(applied.count(), 1);
        swmm_engine_destroy(e);
    }

    void longExpressionLoadsWithoutTruncation()
    {
        SWMM_Engine e = openFixture();
        QVERIFY(e);
        const int s = swmm_subcatch_index(e, "S1");
        const QString expression = QStringLiteral("HGW") + QStringLiteral("+0").repeated(400);
        QCOMPARE(swmm_subcatch_set_gwf_expression(e, s, SWMM_GWF_LATERAL,
            expression.toUtf8().constData()), SWMM_OK);
        GroundwaterExchangeDialog dlg(makeRef(e, QStringLiteral("S1")));
        QCOMPARE(dlg.findChild<GwfExpressionEdit *>(QStringLiteral("gwLateral"))->expression(), expression);
        QSignalSpy applied(&dlg, &GroundwaterExchangeDialog::applied);
        dlg.findChild<QPushButton *>(QStringLiteral("gwApply"))->click();
        QCOMPARE(applied.count(), 0);
        QByteArray actual(2048, '\0');
        QCOMPARE(swmm_subcatch_get_gwf_expression(e, s, SWMM_GWF_LATERAL,
            actual.data(), actual.size()), SWMM_OK);
        QCOMPARE(QString::fromUtf8(actual.constData()), expression);
        swmm_engine_destroy(e);
    }

    void missingNodeSelectionIsRefusedBeforeWriting()
    {
        SWMM_Engine e = openFixture();
        QVERIFY(e);
        const int s = swmm_subcatch_index(e, "S1");
        const auto before = gwParams(e, s);
        GroundwaterExchangeDialog dlg(makeRef(e, QStringLiteral("S1")));
        dlg.findChild<QComboBox *>(QStringLiteral("gwNode"))->setCurrentIndex(-1);
        dlg.findChild<QDoubleSpinBox *>(QStringLiteral("gwA2"))->setValue(0.25);
        QSignalSpy applied(&dlg, &GroundwaterExchangeDialog::applied);
        dlg.findChild<QPushButton *>(QStringLiteral("gwApply"))->click();
        QCOMPARE(applied.count(), 0);
        QCOMPARE(gwParams(e, s), before);
        int node = -1;
        QCOMPARE(swmm_subcatch_get_gw_node(e, s, &node), SWMM_OK);
        QCOMPARE(node, swmm_node_index(e, "J1"));
        auto *error = dlg.findChild<QLabel *>(QStringLiteral("gwWriteError"));
        QVERIFY(error && !error->text().isEmpty());
        swmm_engine_destroy(e);
    }

    void externallyChangedDraftIsRefused_data()
    {
        QTest::addColumn<bool>("removeAquifer");
        QTest::newRow("parameters") << false;
        QTest::newRow("aquifer") << true;
    }

    void externallyChangedDraftIsRefused()
    {
        QFETCH(bool, removeAquifer);
        SWMM_Engine e = openFixture();
        QVERIFY(e);
        const int s = swmm_subcatch_index(e, "S1");
        GroundwaterExchangeDialog dlg(makeRef(e, QStringLiteral("S1")));
        dlg.findChild<QDoubleSpinBox *>(QStringLiteral("gwA2"))->setValue(0.25);
        if (removeAquifer)
            QCOMPARE(swmm_subcatch_set_aquifer(e, s, -1), SWMM_OK);
        else
            QCOMPARE(swmm_subcatch_set_gw_params(e, s, 24.0, .004, 1, 0, 1, 0, 10, 9), SWMM_OK);
        const auto before = gwParams(e, s);
        QSignalSpy applied(&dlg, &GroundwaterExchangeDialog::applied);
        dlg.findChild<QPushButton *>(QStringLiteral("gwApply"))->click();
        QCOMPARE(applied.count(), 0);
        QCOMPARE(gwParams(e, s), before);
        QCOMPARE(dlg.findChild<QDoubleSpinBox *>(QStringLiteral("gwA2"))->value(), 0.25);
        auto *error = dlg.findChild<QLabel *>(QStringLiteral("gwWriteError"));
        QVERIFY(error && !error->text().isEmpty());
        swmm_engine_destroy(e);
    }

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

    void immediateApplyValidatesCurrentDraft_data()
    {
        QTest::addColumn<QString>("editorName");
        QTest::addColumn<int>("expressionType");
        QTest::newRow("lateral") << QStringLiteral("gwLateral") << int(SWMM_GWF_LATERAL);
        QTest::newRow("deep") << QStringLiteral("gwDeep") << int(SWMM_GWF_DEEP);
    }

    void immediateApplyValidatesCurrentDraft()
    {
        QFETCH(QString, editorName);
        QFETCH(int, expressionType);
        SWMM_Engine e = openFixture();
        QVERIFY(e);
        const int s = swmm_subcatch_index(e, "S1");
        const auto beforeParams = gwParams(e, s);
        const QString beforeLateral = gwfExpr(e, s, SWMM_GWF_LATERAL);
        const QString beforeDeep = gwfExpr(e, s, SWMM_GWF_DEEP);
        GroundwaterExchangeDialog dlg(makeRef(e, QStringLiteral("S1")));
        QSignalSpy applied(&dlg, &GroundwaterExchangeDialog::applied);
        auto *edit = dlg.findChild<GwfExpressionEdit *>(editorName);
        auto *apply = dlg.findChild<QPushButton *>(QStringLiteral("gwApply"));
        auto *node = dlg.findChild<QComboBox *>(QStringLiteral("gwNode"));
        auto *surface = dlg.findChild<QDoubleSpinBox *>(QStringLiteral("gwSurfEl"));
        QVERIFY(edit && apply && node && surface);
        const QString validDescription = edit->accessibleDescription();
        dlg.show();
        dlg.activateWindow();
        QTest::qWait(1);
        node->setCurrentText(QStringLiteral("O1"));
        surface->setValue(15.5);
        node->setFocus();

        // setPlainText follows normal typing's textChanged/debounce path;
        // setExpression would validate synchronously and hide this race.
        const QString invalid = QStringLiteral("HGW + (");
        edit->setPlainText(invalid);
        QVERIFY(apply->isEnabled());
        apply->click(); // No event processing or debounce wait before Apply.

        QCOMPARE(applied.count(), 0);
        QCOMPARE(gwParams(e, s), beforeParams);
        int receivingNode = -1;
        QCOMPARE(swmm_subcatch_get_gw_node(e, s, &receivingNode), SWMM_OK);
        QCOMPARE(receivingNode, swmm_node_index(e, "J1"));
        QCOMPARE(gwfExpr(e, s, SWMM_GWF_LATERAL), beforeLateral);
        QCOMPARE(gwfExpr(e, s, SWMM_GWF_DEEP), beforeDeep);
        QCOMPARE(edit->expression(), invalid);
        QCOMPARE(node->currentText(), QStringLiteral("O1"));
        QCOMPARE(surface->value(), 15.5);
        QVERIFY(dlg.isVisible());
        QVERIFY(!dlg.canApply());
        QVERIFY(!apply->isEnabled());
        QTRY_VERIFY(edit->hasFocus());
        QVERIFY(!edit->accessibleDescription().isEmpty());
        QVERIFY(edit->accessibleDescription() != validDescription);

        const QString corrected = QStringLiteral("0.002*(HGW-10)");
        edit->setPlainText(corrected);
        QTRY_VERIFY(apply->isEnabled());
        QVERIFY(dlg.canApply());
        QCOMPARE(edit->accessibleDescription(), validDescription);
        apply->click();
        QCOMPARE(applied.count(), 1);
        QCOMPARE(gwfExpr(e, s, expressionType), corrected);
        QCOMPARE(gwParams(e, s)[0], 15.5);
        QCOMPARE(swmm_subcatch_get_gw_node(e, s, &receivingNode), SWMM_OK);
        QCOMPARE(receivingNode, swmm_node_index(e, "O1"));
        dlg.close();
        swmm_engine_close(e);
        swmm_engine_destroy(e);
    }

    void expressionControlsHaveDistinctAccessibleIdentities()
    {
        SWMM_Engine e = openFixture();
        QVERIFY(e);
        GroundwaterExchangeDialog dlg(makeRef(e, QStringLiteral("S1")));
        auto *lateral = dlg.findChild<GwfExpressionEdit *>(QStringLiteral("gwLateral"));
        auto *deep = dlg.findChild<GwfExpressionEdit *>(QStringLiteral("gwDeep"));
        auto *lateralInsert = dlg.findChild<QToolButton *>(QStringLiteral("gwLateralInsert"));
        auto *deepInsert = dlg.findChild<QToolButton *>(QStringLiteral("gwDeepInsert"));
        QVERIFY(lateral && deep && lateralInsert && deepInsert);
        QVERIFY(lateral->accessibleName().contains(QStringLiteral("lateral"), Qt::CaseInsensitive));
        QVERIFY(deep->accessibleName().contains(QStringLiteral("deep"), Qt::CaseInsensitive));
        QVERIFY(lateral->accessibleName() != deep->accessibleName());
        QVERIFY(!lateral->accessibleDescription().isEmpty());
        QVERIFY(!deep->accessibleDescription().isEmpty());
        QVERIFY(lateralInsert->accessibleName().contains(QStringLiteral("lateral"), Qt::CaseInsensitive));
        QVERIFY(deepInsert->accessibleName().contains(QStringLiteral("deep"), Qt::CaseInsensitive));
        QVERIFY(lateralInsert->accessibleName() != deepInsert->accessibleName());
        bool lateralBuddy = false, deepBuddy = false;
        for (auto *label : dlg.findChildren<QLabel *>()) {
            lateralBuddy |= label->buddy() == lateral && label->text().contains(QLatin1Char('&'));
            deepBuddy |= label->buddy() == deep && label->text().contains(QLatin1Char('&'));
        }
        QVERIFY(lateralBuddy);
        QVERIFY(deepBuddy);
        swmm_engine_close(e);
        swmm_engine_destroy(e);
    }

    void dismissDoesNotWriteDraft_data()
    {
        QTest::addColumn<bool>("escape");
        QTest::newRow("close-button") << false;
        QTest::newRow("escape") << true;
    }

    void dismissDoesNotWriteDraft()
    {
        QFETCH(bool, escape);
        SWMM_Engine e = openFixture();
        QVERIFY(e);
        const int s = swmm_subcatch_index(e, "S1");
        const auto beforeParams = gwParams(e, s);
        const QString beforeLateral = gwfExpr(e, s, SWMM_GWF_LATERAL);
        const QString beforeDeep = gwfExpr(e, s, SWMM_GWF_DEEP);
        GroundwaterExchangeDialog dlg(makeRef(e, QStringLiteral("S1")));
        QSignalSpy applied(&dlg, &GroundwaterExchangeDialog::applied);
        dlg.show();
        dlg.findChild<QComboBox *>(QStringLiteral("gwNode"))->setCurrentText(QStringLiteral("O1"));
        dlg.findChild<QDoubleSpinBox *>(QStringLiteral("gwSurfEl"))->setValue(22.0);
        dlg.findChild<GwfExpressionEdit *>(QStringLiteral("gwLateral"))->setPlainText(QStringLiteral("0.5*HGW"));
        dlg.findChild<GwfExpressionEdit *>(QStringLiteral("gwDeep"))->setPlainText(QStringLiteral("HGW + ("));
        if (escape) {
            QTest::keyClick(&dlg, Qt::Key_Escape);
        } else {
            auto *buttons = dlg.findChild<QDialogButtonBox *>();
            QVERIFY(buttons && buttons->button(QDialogButtonBox::Close));
            buttons->button(QDialogButtonBox::Close)->click();
        }
        QVERIFY(!dlg.isVisible());
        QCOMPARE(dlg.result(), int(QDialog::Rejected));
        QCOMPARE(applied.count(), 0);
        QCOMPARE(gwParams(e, s), beforeParams);
        int receivingNode = -1;
        QCOMPARE(swmm_subcatch_get_gw_node(e, s, &receivingNode), SWMM_OK);
        QCOMPARE(receivingNode, swmm_node_index(e, "J1"));
        QCOMPARE(gwfExpr(e, s, SWMM_GWF_LATERAL), beforeLateral);
        QCOMPARE(gwfExpr(e, s, SWMM_GWF_DEEP), beforeDeep);
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
