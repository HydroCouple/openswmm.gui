/*!
 * \file test_wateragesourcesdialog.cpp
 * \brief Y3 — the Water Age Sources editor, driven through its widgets.
 *
 * \details This is the first GUI round of the subplan whose *wiring* has an
 *          automated observer: the dialog is dependency-light (Qt Widgets +
 *          `openswmm_water_age.h`), so a test can construct it, hydrate it
 *          from an engine, click OK, and read the engine back — the
 *          `ClimatologyDialog` precedent. Y1's page could not be tested
 *          this way (`tests/gui/CMakeLists.txt:1996`), which is exactly why
 *          this dialog was kept free of project/layer dependencies.
 *
 *          The claims:
 *          1. Hydration — global ages and per-node overrides land in the
 *             widgets, negatives included (engine D-NS1 makes them legal).
 *          2. Write-back — an edited value reaches the engine on OK.
 *          3. A no-op OK writes NOTHING (the churn discipline; a dialog
 *             that rewrites every key on every OK dirties projects and
 *             defeats change tracking).
 *          4. Overrides add / update / remove, and a row deleted in the
 *             table is removed from the engine rather than surviving as a
 *             stale key.
 *          5. The editor cannot author what the parser refuses — only
 *             DWF and EXTERNAL_INFLOW appear as override sources.
 */

#include "ui/dialogs/wateragesourcesdialog.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_water_age.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QObject>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>

using OpenSWMMVis::WaterAgeSourcesDialog;

namespace {

/*! A BUILDING-state engine with two nodes, so override rows have targets. */
SWMM_Engine makeAgeEngine()
{
    SWMM_Engine e = swmm_engine_new();
    Q_ASSERT(e != nullptr);
    swmm_node_add(e, "J0", 0 /*JUNCTION*/);
    swmm_node_add(e, "J1", 0 /*JUNCTION*/);
    return e;
}

QDoubleSpinBox *globalSpin(WaterAgeSourcesDialog &dlg, int source)
{
    return dlg.findChild<QDoubleSpinBox *>(
        QStringLiteral("wa_globalSpin_%1").arg(source));
}

void clickOk(WaterAgeSourcesDialog &dlg)
{
    auto *bb = dlg.findChild<QDialogButtonBox *>();
    QVERIFY(bb);
    bb->button(QDialogButtonBox::Ok)->click();
}

} // namespace

class TestWaterAgeSourcesDialog : public QObject
{
    Q_OBJECT
private slots:
    void constructsWithNullEngine();
    void hydratesGlobalsIncludingNegatives();
    void writesEditedGlobalOnOk();
    void noOpOkWritesNothing();
    void overridesAddUpdateAndRemove();
    void overrideSourcesAreParserScoped();
};

void TestWaterAgeSourcesDialog::constructsWithNullEngine()
{
    // Must not crash with no engine — every read path early-returns.
    WaterAgeSourcesDialog dlg(nullptr, nullptr);
    QCOMPARE(dlg.wroteAnyChanges(), false);
}

void TestWaterAgeSourcesDialog::hydratesGlobalsIncludingNegatives()
{
    SWMM_Engine e = makeAgeEngine();
    QCOMPARE(swmm_water_age_set_global_source(e, SWMM_AGE_SRC_INITIAL_STATE,
                                              1.5), SWMM_OK);
    // D-NS1: a negative source age is EXTRACTION, and must survive into the
    // editor rather than being validated away or clamped to zero.
    QCOMPARE(swmm_water_age_set_global_source(e, SWMM_AGE_SRC_RDII, -2.0),
             SWMM_OK);

    WaterAgeSourcesDialog dlg(e, nullptr);
    auto *init = globalSpin(dlg, SWMM_AGE_SRC_INITIAL_STATE);
    auto *rdii = globalSpin(dlg, SWMM_AGE_SRC_RDII);
    QVERIFY(init && rdii);
    QCOMPARE(init->value(), 1.5);
    QCOMPARE(rdii->value(), -2.0);
    QCOMPARE(dlg.wroteAnyChanges(), false);

    swmm_engine_destroy(e);
}

void TestWaterAgeSourcesDialog::writesEditedGlobalOnOk()
{
    SWMM_Engine e = makeAgeEngine();
    WaterAgeSourcesDialog dlg(e, nullptr);

    auto *gw = globalSpin(dlg, SWMM_AGE_SRC_GW);
    QVERIFY(gw);
    gw->setValue(6.0);
    clickOk(dlg);

    QVERIFY(dlg.wroteAnyChanges());
    double h = 0.0;
    QCOMPARE(swmm_water_age_get_global_source(e, SWMM_AGE_SRC_GW, &h),
             SWMM_OK);
    QCOMPARE(h, 6.0);

    swmm_engine_destroy(e);
}

void TestWaterAgeSourcesDialog::noOpOkWritesNothing()
{
    SWMM_Engine e = makeAgeEngine();
    QCOMPARE(swmm_water_age_set_global_source(e, SWMM_AGE_SRC_DWF, 3.0),
             SWMM_OK);

    WaterAgeSourcesDialog dlg(e, nullptr);
    clickOk(dlg);          // opened and accepted without touching anything

    QCOMPARE(dlg.lastWriteCount(), 0);
    QCOMPARE(dlg.wroteAnyChanges(), false);
    // ...and the pre-existing value is untouched.
    double h = 0.0;
    QCOMPARE(swmm_water_age_get_global_source(e, SWMM_AGE_SRC_DWF, &h),
             SWMM_OK);
    QCOMPARE(h, 3.0);

    swmm_engine_destroy(e);
}

void TestWaterAgeSourcesDialog::overridesAddUpdateAndRemove()
{
    SWMM_Engine e = makeAgeEngine();
    // One override already in the engine: DWF at node 0.
    QCOMPARE(swmm_water_age_set_override(e, SWMM_AGE_SRC_DWF, 0, 4.0),
             SWMM_OK);

    {
        WaterAgeSourcesDialog dlg(e, nullptr);
        auto *tbl = dlg.findChild<QTableWidget *>(
            QStringLiteral("wa_overrideTable"));
        QVERIFY(tbl);
        QCOMPARE(tbl->rowCount(), 1);                 // hydrated
        auto *hours = qobject_cast<QDoubleSpinBox *>(tbl->cellWidget(0, 2));
        QVERIFY(hours);
        QCOMPARE(hours->value(), 4.0);

        hours->setValue(7.5);                         // update in place
        clickOk(dlg);
        QVERIFY(dlg.wroteAnyChanges());
    }
    int count = 0;
    QCOMPARE(swmm_water_age_override_count(e, &count), SWMM_OK);
    QCOMPARE(count, 1);                               // updated, not appended
    int src = 0, node = 0;
    double h = 0.0;
    QCOMPARE(swmm_water_age_get_override(e, 0, &src, &node, &h), SWMM_OK);
    QCOMPARE(h, 7.5);

    // Now delete the row and confirm the engine drops the key — a stale
    // key here would keep applying an age the user removed.
    {
        WaterAgeSourcesDialog dlg(e, nullptr);
        auto *tbl = dlg.findChild<QTableWidget *>(
            QStringLiteral("wa_overrideTable"));
        QVERIFY(tbl);
        QCOMPARE(tbl->rowCount(), 1);
        tbl->setCurrentCell(0, 0);
        auto *rem = dlg.findChild<QPushButton *>(QStringLiteral("wa_removeBtn"));
        QVERIFY(rem);
        rem->click();
        QCOMPARE(tbl->rowCount(), 0);
        clickOk(dlg);
        QVERIFY(dlg.wroteAnyChanges());
    }
    QCOMPARE(swmm_water_age_override_count(e, &count), SWMM_OK);
    QCOMPARE(count, 0);

    swmm_engine_destroy(e);
}

void TestWaterAgeSourcesDialog::overrideSourcesAreParserScoped()
{
    SWMM_Engine e = makeAgeEngine();
    WaterAgeSourcesDialog dlg(e, nullptr);

    auto *add = dlg.findChild<QPushButton *>(QStringLiteral("wa_addBtn"));
    QVERIFY(add);
    add->click();

    auto *tbl = dlg.findChild<QTableWidget *>(
        QStringLiteral("wa_overrideTable"));
    QVERIFY(tbl);
    QCOMPARE(tbl->rowCount(), 1);

    auto *srcCombo = qobject_cast<QComboBox *>(tbl->cellWidget(0, 0));
    QVERIFY(srcCombo);
    // The engine refuses NODE scope for anything but these two (A1a rule);
    // an editor that offered more would let a user author a table the file
    // parser rejects on reload.
    QCOMPARE(srcCombo->count(), 2);
    QVector<int> offered;
    for (int i = 0; i < srcCombo->count(); ++i)
        offered.append(srcCombo->itemData(i).toInt());
    QVERIFY(offered.contains(int(SWMM_AGE_SRC_DWF)));
    QVERIFY(offered.contains(int(SWMM_AGE_SRC_EXTERNAL_INFLOW)));
    QVERIFY(!offered.contains(int(SWMM_AGE_SRC_GW)));
    QVERIFY(!offered.contains(int(SWMM_AGE_SRC_INITIAL_STATE)));

    // The node combo must be populated from the engine, not left empty.
    auto *nodeCombo = qobject_cast<QComboBox *>(tbl->cellWidget(0, 1));
    QVERIFY(nodeCombo);
    QCOMPARE(nodeCombo->count(), 2);
    QCOMPARE(nodeCombo->itemText(0), QStringLiteral("J0"));

    swmm_engine_destroy(e);
}

QTEST_MAIN(TestWaterAgeSourcesDialog)
#include "test_wateragesourcesdialog.moc"
