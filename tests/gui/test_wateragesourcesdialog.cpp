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

#include <QAccessible>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>
#include <QStringList>

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

QStringList engineAgeState(SWMM_Engine engine)
{
    QStringList state;
    for (int source : {SWMM_AGE_SRC_RAINFALL, SWMM_AGE_SRC_DWF, SWMM_AGE_SRC_GW,
                       SWMM_AGE_SRC_RDII, SWMM_AGE_SRC_EXTERNAL_INFLOW,
                       SWMM_AGE_SRC_IFACE, SWMM_AGE_SRC_INITIAL_STATE}) {
        double hours = 0.0;
        const int rc = swmm_water_age_get_global_source(engine, source, &hours);
        state.append(QStringLiteral("global:%1:%2:%3").arg(source).arg(rc)
                         .arg(hours, 0, 'g', 17));
    }
    int count = 0;
    swmm_water_age_override_count(engine, &count);
    for (int i = 0; i < count; ++i) {
        int source = -1, node = -1;
        double hours = 0.0;
        const int rc = swmm_water_age_get_override(engine, i, &source, &node, &hours);
        state.append(QStringLiteral("override:%1:%2:%3:%4").arg(source).arg(node)
                         .arg(rc).arg(hours, 0, 'g', 17));
    }
    return state;
}

} // namespace

class TestWaterAgeSourcesDialog : public QObject
{
    Q_OBJECT
private slots:
    void invalidationDiscardsDraftAndGuardsLaterSignals();
    void constructsWithNullEngine();
    void hydratesGlobalsIncludingNegatives();
    void writesEditedGlobalOnOk();
    void noOpOkWritesNothing();
    void overridesAddUpdateAndRemove();
    void overrideSourcesAreParserScoped();
    void duplicateOverrideRefusesAllWritesAndAllowsCorrection();
    void missingSelectionRefusesAllWritesAndAllowsCorrection_data();
    void missingSelectionRefusesAllWritesAndAllowsCorrection();
    void invalidDraftDismissal_data();
    void invalidDraftDismissal();
    void tableControlsExposeAccessibleIdentityAfterRemoval();
    void rowActionsCannotBecomeEnterDefault();
    void exactValuesSurviveRoundedDisplay_data();
    void exactValuesSurviveRoundedDisplay();
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

void TestWaterAgeSourcesDialog::duplicateOverrideRefusesAllWritesAndAllowsCorrection()
{
    SWMM_Engine e = makeAgeEngine();
    QCOMPARE(swmm_water_age_set_override(e, SWMM_AGE_SRC_DWF, 0, 4.0), SWMM_OK);
    QCOMPARE(swmm_water_age_set_override(e, SWMM_AGE_SRC_EXTERNAL_INFLOW, 1, 9.0), SWMM_OK);
    const auto before = engineAgeState(e);
    WaterAgeSourcesDialog dlg(e, nullptr);
    auto *table = dlg.findChild<QTableWidget *>(QStringLiteral("wa_overrideTable"));
    auto *add = dlg.findChild<QPushButton *>(QStringLiteral("wa_addBtn"));
    auto *remove = dlg.findChild<QPushButton *>(QStringLiteral("wa_removeBtn"));
    QVERIFY(table && add && remove);
    QCOMPARE(table->rowCount(), 2);
    // Stage a deletion as well as a global edit: invalid overrides must not
    // allow either earlier write phase to run.
    table->setCurrentCell(1, 0);
    remove->click();
    add->click();
    auto *source = qobject_cast<QComboBox *>(table->cellWidget(1, 0));
    auto *node = qobject_cast<QComboBox *>(table->cellWidget(1, 1));
    auto *hours = qobject_cast<QDoubleSpinBox *>(table->cellWidget(1, 2));
    QVERIFY(source && node && hours);
    source->setCurrentIndex(source->findData(int(SWMM_AGE_SRC_DWF)));
    node->setCurrentIndex(node->findData(0));
    hours->setValue(7.5);
    globalSpin(dlg, SWMM_AGE_SRC_GW)->setValue(6.0);
    dlg.show();
    dlg.activateWindow();
    QTest::qWait(1);
    clickOk(dlg);

    QVERIFY(dlg.isVisible());
    QCOMPARE(dlg.wroteAnyChanges(), false);
    QCOMPARE(dlg.lastWriteCount(), 0);
    QCOMPARE(engineAgeState(e), before);
    QCOMPARE(table->rowCount(), 2);
    QCOMPARE(hours->value(), 7.5);
    QCOMPARE(globalSpin(dlg, SWMM_AGE_SRC_GW)->value(), 6.0);
    auto *error = dlg.findChild<QLabel *>(QStringLiteral("wa_validationError"));
    QVERIFY(error && !error->text().isEmpty());
    QTRY_VERIFY(source->hasFocus());

    node->setCurrentIndex(node->findData(1));
    clickOk(dlg);
    QCOMPARE(dlg.result(), int(QDialog::Accepted));
    QVERIFY(dlg.wroteAnyChanges());
    QVERIFY(error->text().isEmpty());
    double global = 0.0;
    QCOMPARE(swmm_water_age_get_global_source(e, SWMM_AGE_SRC_GW, &global), SWMM_OK);
    QCOMPARE(global, 6.0);
    int count = 0;
    QCOMPARE(swmm_water_age_override_count(e, &count), SWMM_OK);
    QCOMPARE(count, 2);
    bool foundCorrected = false;
    for (int i = 0; i < count; ++i) {
        int src = -1, nd = -1;
        double age = 0.0;
        QCOMPARE(swmm_water_age_get_override(e, i, &src, &nd, &age), SWMM_OK);
        QCOMPARE(src, int(SWMM_AGE_SRC_DWF));
        if (nd == 1) { QCOMPARE(age, 7.5); foundCorrected = true; }
    }
    QVERIFY(foundCorrected);
    swmm_engine_destroy(e);
}

void TestWaterAgeSourcesDialog::missingSelectionRefusesAllWritesAndAllowsCorrection_data()
{
    QTest::addColumn<int>("column");
    QTest::newRow("missing-source") << 0;
    QTest::newRow("missing-node") << 1;
}

void TestWaterAgeSourcesDialog::missingSelectionRefusesAllWritesAndAllowsCorrection()
{
    QFETCH(int, column);
    SWMM_Engine e = makeAgeEngine();
    QCOMPARE(swmm_water_age_set_override(e, SWMM_AGE_SRC_DWF, 1, 4.0), SWMM_OK);
    const auto before = engineAgeState(e);
    WaterAgeSourcesDialog dlg(e, nullptr);
    auto *table = dlg.findChild<QTableWidget *>(QStringLiteral("wa_overrideTable"));
    QVERIFY(table);
    auto *selection = qobject_cast<QComboBox *>(table->cellWidget(0, column));
    auto *hours = qobject_cast<QDoubleSpinBox *>(table->cellWidget(0, 2));
    QVERIFY(selection && hours);
    selection->setCurrentIndex(-1);
    hours->setValue(-2.5); // Valid extraction age is retained through refusal.
    globalSpin(dlg, SWMM_AGE_SRC_GW)->setValue(3.0);
    dlg.show();
    dlg.activateWindow();
    QTest::qWait(1);
    clickOk(dlg);

    QVERIFY(dlg.isVisible());
    QCOMPARE(dlg.wroteAnyChanges(), false);
    QCOMPARE(dlg.lastWriteCount(), 0);
    QCOMPARE(engineAgeState(e), before);
    QCOMPARE(selection->currentIndex(), -1);
    QCOMPARE(hours->value(), -2.5);
    auto *error = dlg.findChild<QLabel *>(QStringLiteral("wa_validationError"));
    QVERIFY(error && !error->text().isEmpty());
    QTRY_VERIFY(selection->hasFocus());

    selection->setCurrentIndex(selection->findData(column == 0 ? int(SWMM_AGE_SRC_DWF) : 1));
    clickOk(dlg);
    QCOMPARE(dlg.result(), int(QDialog::Accepted));
    QVERIFY(dlg.wroteAnyChanges());
    QVERIFY(error->text().isEmpty());
    int count = 0, source = -1, nd = -1;
    double age = 0.0;
    QCOMPARE(swmm_water_age_override_count(e, &count), SWMM_OK);
    QCOMPARE(count, 1);
    QCOMPARE(swmm_water_age_get_override(e, 0, &source, &nd, &age), SWMM_OK);
    QCOMPARE(source, int(SWMM_AGE_SRC_DWF));
    QCOMPARE(nd, 1);
    QCOMPARE(age, -2.5);
    swmm_engine_destroy(e);
}

void TestWaterAgeSourcesDialog::invalidDraftDismissal_data()
{
    QTest::addColumn<bool>("escape");
    QTest::newRow("cancel-button") << false;
    QTest::newRow("escape") << true;
}

void TestWaterAgeSourcesDialog::invalidDraftDismissal()
{
    QFETCH(bool, escape);
    SWMM_Engine e = makeAgeEngine();
    QCOMPARE(swmm_water_age_set_override(e, SWMM_AGE_SRC_DWF, 0, 4.0), SWMM_OK);
    const auto before = engineAgeState(e);
    WaterAgeSourcesDialog dlg(e, nullptr);
    auto *add = dlg.findChild<QPushButton *>(QStringLiteral("wa_addBtn"));
    QVERIFY(add);
    add->click(); // Defaults duplicate the existing DWF/J0 override.
    globalSpin(dlg, SWMM_AGE_SRC_GW)->setValue(6.0);
    dlg.show();
    if (escape) {
        QTest::keyClick(&dlg, Qt::Key_Escape);
    } else {
        auto *buttons = dlg.findChild<QDialogButtonBox *>();
        QVERIFY(buttons && buttons->button(QDialogButtonBox::Cancel));
        buttons->button(QDialogButtonBox::Cancel)->click();
    }
    QCOMPARE(dlg.result(), int(QDialog::Rejected));
    QVERIFY(!dlg.isVisible());
    QCOMPARE(dlg.wroteAnyChanges(), false);
    QCOMPARE(dlg.lastWriteCount(), 0);
    QCOMPARE(engineAgeState(e), before);
    swmm_engine_destroy(e);
}

void TestWaterAgeSourcesDialog::tableControlsExposeAccessibleIdentityAfterRemoval()
{
    SWMM_Engine e = makeAgeEngine();
    WaterAgeSourcesDialog dlg(e, nullptr);
    auto *globals = dlg.findChild<QTableWidget *>(QStringLiteral("wa_globalTable"));
    auto *overrides = dlg.findChild<QTableWidget *>(QStringLiteral("wa_overrideTable"));
    auto *add = dlg.findChild<QPushButton *>(QStringLiteral("wa_addBtn"));
    auto *remove = dlg.findChild<QPushButton *>(QStringLiteral("wa_removeBtn"));
    QVERIFY(globals && overrides && add && remove);
    for (auto *table : {globals, overrides}) {
        auto *iface = QAccessible::queryAccessibleInterface(table);
        QVERIFY(iface);
        QCOMPARE(iface->role(), QAccessible::Table);
        QVERIFY(!iface->text(QAccessible::Name).isEmpty());
    }
    QVERIFY(globals->accessibleName() != overrides->accessibleName());
    for (int row = 0; row < globals->rowCount(); ++row) {
        auto *iface = QAccessible::queryAccessibleInterface(globals->cellWidget(row, 1));
        QVERIFY(iface);
        QVERIFY(iface->text(QAccessible::Name).contains(globals->item(row, 0)->text()));
    }
    add->click();
    add->click();
    const QStringList fields{QStringLiteral("source"), QStringLiteral("node"), QStringLiteral("age")};
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 3; ++column) {
            auto *editor = overrides->cellWidget(row, column);
            auto *iface = QAccessible::queryAccessibleInterface(editor);
            QVERIFY(iface);
            const QString name = iface->text(QAccessible::Name);
            const QString description = iface->text(QAccessible::Description);
            const QString diagnostic = QStringLiteral("row=%1 column=%2 interfaceName='%3' accessibleName='%4' description='%5' value='%6'")
                .arg(row).arg(column).arg(name, editor->accessibleName(), description, iface->text(QAccessible::Value));
            QVERIFY2(description.contains(QString::number(row + 1)), qPrintable(diagnostic));
            QVERIFY2(description.contains(fields[column], Qt::CaseInsensitive), qPrintable(diagnostic));
            if (auto *combo = qobject_cast<QComboBox *>(editor)) {
                // Native combo interfaces may expose the selected item as
                // Name; row/field context belongs in Description as well.
                QVERIFY2(name == combo->currentText() || name == editor->accessibleName(), qPrintable(diagnostic));
                QVERIFY2(iface->text(QAccessible::Value) == combo->currentText(), qPrintable(diagnostic));
            } else {
                QVERIFY2(name.contains(QString::number(row + 1)), qPrintable(diagnostic));
                QVERIFY2(name.contains(fields[column], Qt::CaseInsensitive), qPrintable(diagnostic));
            }
        }
    }
    overrides->setCurrentCell(0, 0);
    remove->click();
    QCOMPARE(overrides->rowCount(), 1);
    for (int column = 0; column < 3; ++column) {
        auto *editor = overrides->cellWidget(0, column);
        auto *iface = QAccessible::queryAccessibleInterface(editor);
        QVERIFY(iface);
        const QString name = iface->text(QAccessible::Name);
        const QString description = iface->text(QAccessible::Description);
        const QString diagnostic = QStringLiteral("after removal row=0 column=%1 interfaceName='%2' accessibleName='%3' description='%4' value='%5'")
            .arg(column).arg(name, editor->accessibleName(), description, iface->text(QAccessible::Value));
        QVERIFY2(description.contains(QStringLiteral("1")), qPrintable(diagnostic));
        QVERIFY2(!description.contains(QStringLiteral("2")), qPrintable(diagnostic));
        QVERIFY2(description.contains(fields[column], Qt::CaseInsensitive), qPrintable(diagnostic));
        if (auto *combo = qobject_cast<QComboBox *>(editor)) {
            QVERIFY2(name == combo->currentText() || name == editor->accessibleName(), qPrintable(diagnostic));
            QVERIFY2(iface->text(QAccessible::Value) == combo->currentText(), qPrintable(diagnostic));
        } else {
            QVERIFY2(name.contains(QStringLiteral("1")), qPrintable(diagnostic));
            QVERIFY2(!name.contains(QStringLiteral("2")), qPrintable(diagnostic));
            QVERIFY2(name.contains(fields[column], Qt::CaseInsensitive), qPrintable(diagnostic));
        }
    }
    swmm_engine_destroy(e);
}

void TestWaterAgeSourcesDialog::rowActionsCannotBecomeEnterDefault()
{
    SWMM_Engine e = makeAgeEngine();
    WaterAgeSourcesDialog dlg(e, nullptr);
    for (const auto &name : {QStringLiteral("wa_addBtn"), QStringLiteral("wa_removeBtn")}) {
        auto *button = dlg.findChild<QPushButton *>(name);
        QVERIFY(button);
        QVERIFY(!button->autoDefault());
        QVERIFY(!button->isDefault());
    }
    swmm_engine_destroy(e);
}

void TestWaterAgeSourcesDialog::exactValuesSurviveRoundedDisplay_data()
{
    QTest::addColumn<QString>("edit");
    for (const char *name : {"none", "unrelated", "explicit"})
        QTest::newRow(name) << QString::fromLatin1(name);
}

void TestWaterAgeSourcesDialog::exactValuesSurviveRoundedDisplay()
{
    QFETCH(QString, edit);
    SWMM_Engine e = makeAgeEngine();
    QCOMPARE(swmm_water_age_set_global_source(e, SWMM_AGE_SRC_GW, 1.23456789), SWMM_OK);
    QCOMPARE(swmm_water_age_set_override(e, SWMM_AGE_SRC_DWF, 0, 2.34567891), SWMM_OK);
    double originalGlobal = 0, originalOverride = 0;
    swmm_water_age_get_global_source(e, SWMM_AGE_SRC_GW, &originalGlobal);
    swmm_water_age_get_override(e, 0, nullptr, nullptr, &originalOverride);
    WaterAgeSourcesDialog dlg(e);
    if (edit == "unrelated") globalSpin(dlg, SWMM_AGE_SRC_RDII)->setValue(9.0);
    if (edit == "explicit") globalSpin(dlg, SWMM_AGE_SRC_GW)->setValue(8.125);
    clickOk(dlg);
    double actual = 0;
    QCOMPARE(swmm_water_age_get_global_source(e, SWMM_AGE_SRC_GW, &actual), SWMM_OK);
    QCOMPARE(actual, edit == "explicit" ? 8.125 : originalGlobal);
    QCOMPARE(swmm_water_age_get_override(e, 0, nullptr, nullptr, &actual), SWMM_OK);
    QCOMPARE(actual, originalOverride);
    QCOMPARE(dlg.wroteAnyChanges(), edit != "none");
    if (edit == "none") QCOMPARE(dlg.lastWriteCount(), 0);
    swmm_engine_destroy(e);
}

void TestWaterAgeSourcesDialog::invalidationDiscardsDraftAndGuardsLaterSignals()
{
    SWMM_Engine e = makeAgeEngine();
    WaterAgeSourcesDialog dlg(e);
    dlg.show();
    QVERIFY(QMetaObject::invokeMethod(&dlg, "invalidateEngine", Qt::DirectConnection));
    QVERIFY(!dlg.isEnabled());
    QVERIFY(!dlg.isVisible());
    swmm_engine_destroy(e);
    // A queued/direct accepted signal after model closure cannot reuse the
    // original handle or change the rejected result.
    QVERIFY(QMetaObject::invokeMethod(&dlg, "onAccept", Qt::DirectConnection));
    QCOMPARE(dlg.result(), int(QDialog::Rejected));
    QVERIFY(!dlg.wroteAnyChanges());
}

QTEST_MAIN(TestWaterAgeSourcesDialog)
#include "test_wateragesourcesdialog.moc"
