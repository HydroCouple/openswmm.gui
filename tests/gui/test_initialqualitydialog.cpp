/*!
 * \file test_initialqualitydialog.cpp
 * \brief G-A1 — the per-element Initial Quality editor, driven through its
 *        widgets.
 *
 * \details Dependency-light dialog (Qt Widgets + `openswmm_initial_quality.h`
 *          and friends), so a test constructs it against a synthetic
 *          BUILDING engine, hydrates, clicks, and reads the engine back —
 *          the `WaterAgeSourcesDialog` precedent.
 *
 *          The claims:
 *          1. Hydration — engine rows land in the widgets (scope, element,
 *             constituent, value).
 *          2. Write-back — an added row reaches the engine on OK; the write
 *             count is exact.
 *          3. A no-op OK writes NOTHING (churn discipline).
 *          4. A row deleted in the table is removed from the engine rather
 *             than surviving as a stale key.
 *          5. Reserved species are gated: with WATER_AGE off the combo
 *             omits the age entry; on, it offers it (and heat likewise).
 *          6. Element-scoped mode (the Property Browser's per-element
 *             "Initial Quality" cell): only the scoped element's rows are
 *             shown, added rows are pinned to it, and OK never touches
 *             another element's rows.
 */

#include "ui/dialogs/initialqualitydialog.h"
#include "ui/properties/initialqualityeditbutton.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_initial_quality.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_pollutants.h>

#include <QComboBox>
#include <QAccessible>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>
#include <QMessageBox>
#include <QTimer>
#include <QScopeGuard>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QObject>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>

using OpenSWMMVis::InitialQualityDialog;

namespace {

/*! A BUILDING-state engine with nodes, a link, and one pollutant. */
SWMM_Engine makeEngine(bool ageOn = false, bool heatOn = false)
{
    SWMM_Engine e = swmm_engine_new();
    Q_ASSERT(e != nullptr);
    swmm_node_add(e, "J0", 0 /*JUNCTION*/);
    swmm_node_add(e, "J1", 0 /*JUNCTION*/);
    swmm_link_add(e, "C1", 0 /*CONDUIT*/);
    swmm_pollutant_add(e, "TSS", 0 /*MG/L*/);
    if (ageOn)  swmm_options_set(e, "WATER_AGE", "YES");
    if (heatOn) swmm_options_set(e, "HEAT_TRANSPORT", "YES");
    return e;
}

QTableWidget *table(InitialQualityDialog &dlg)
{
    return dlg.findChild<QTableWidget *>(QStringLiteral("iq_table"));
}

void clickOk(InitialQualityDialog &dlg)
{
    auto *bb = dlg.findChild<QDialogButtonBox *>();
    QVERIFY(bb);
    bb->button(QDialogButtonBox::Ok)->click();
}

} // namespace

class TestInitialQualityDialog : public QObject
{
    Q_OBJECT
private slots:
    void invalidationDiscardsDraftAndGuardsLaterSignals();
    void exactValuesSurviveRoundedDisplay_data();
    void exactValuesSurviveRoundedDisplay();
    void duplicateRowsRefuseBeforeWriting_data();
    void duplicateRowsRefuseBeforeWriting();
    void propertyEditorReportsOnlyRealChanges_data();
    void propertyEditorReportsOnlyRealChanges();
    void partialFailureThenNoOpRetryKeepsChangeFlag();
    void partialFailureRetainsDraftAndAllowsRetry();
    void rejectedFirstWriteDoesNotClaimChanges();
    void lifecycleChangeRefusesWithoutLosingDraft();
    void readOnlyUsesClose();
    void scopeChangeFollowsRowAfterDeletion();
    void fileRowsRemainProtectedAfterDeletion();
    void fieldsHaveAccessibleNamesAndCsvBuddy();
    void auxiliaryButtonsAreNotEnterDefaults();
    void escapeDiscardsDraft();
    void constructsWithNullEngine();
    void hydratesEngineRows();
    void addedRowReachesEngineOnOk();
    void noOpOkWritesNothing();
    void deletedRowIsRemovedFromEngine();
    void reservedSpeciesAreOptionGated();
    void elementScopeShowsOnlyThatElementsRows();
    void elementScopeEditPreservesOtherElements();
    void elementScopeAddAndRemoveStayScoped();
};

void TestInitialQualityDialog::duplicateRowsRefuseBeforeWriting_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("exact-name") << QStringLiteral("TSS");
    QTest::newRow("case-variant") << QStringLiteral("tss");
}

void TestInitialQualityDialog::duplicateRowsRefuseBeforeWriting()
{
    QFETCH(QString, name);
    SWMM_Engine e = makeEngine();
    auto cleanup = qScopeGuard([e] { swmm_engine_destroy(e); });
    QCOMPARE(swmm_init_quality_set(e, 0, 0, "TSS", 1.0), SWMM_OK);
    InitialQualityDialog dlg(e);
    auto *tbl = table(dlg);
    qobject_cast<QDoubleSpinBox *>(tbl->cellWidget(0, 3))->setValue(2.0);
    dlg.findChild<QPushButton *>("iq_addBtn")->click();
    auto *constituent = qobject_cast<QComboBox *>(tbl->cellWidget(1, 2));
    if (constituent->findData(name) < 0) constituent->addItem(name, name);
    constituent->setCurrentIndex(constituent->findData(name));
    qobject_cast<QDoubleSpinBox *>(tbl->cellWidget(1, 3))->setValue(1.0);
    QSignalSpy accepted(&dlg, &QDialog::accepted);
    QSignalSpy applied(&dlg, &InitialQualityDialog::changesApplied);
    QString error;
    QTimer::singleShot(0, &dlg, [&] {
        if (auto *message = dlg.findChild<QMessageBox *>()) {
            error = message->text();
            message->accept();
        }
    });
    clickOk(dlg);
    QCOMPARE(accepted.count(), 0);
    QCOMPARE(applied.count(), 0);
    QVERIFY(error.contains("duplicates row 1"));
    QCOMPARE(dlg.lastWriteCount(), 0);
    QVERIFY(!dlg.wroteAnyChanges());
    int link = -1, element = -1;
    char constituentName[64] = {};
    double value = -1;
    QCOMPARE(swmm_init_quality_get(e, 0, &link, &element, constituentName,
                                   sizeof constituentName, &value), SWMM_OK);
    QCOMPARE(value, 1.0);
    QCOMPARE(tbl->rowCount(), 2);
}

void TestInitialQualityDialog::propertyEditorReportsOnlyRealChanges_data()
{
    QTest::addColumn<QString>("action");
    QTest::addColumn<bool>("changed");
    QTest::newRow("unchanged-ok") << QStringLiteral("noop") << false;
    QTest::newRow("untouched-cancel") << QStringLiteral("cancel") << false;
    QTest::newRow("same-count-value-change") << QStringLiteral("edit") << true;
    QTest::newRow("partial-write-then-cancel") << QStringLiteral("partial") << true;
}

void TestInitialQualityDialog::propertyEditorReportsOnlyRealChanges()
{
    QFETCH(QString, action);
    QFETCH(bool, changed);
    SWMM_Engine e = makeEngine();
    auto cleanup = qScopeGuard([e] { swmm_engine_destroy(e); });
    QCOMPARE(swmm_init_quality_set(e, 0, 0, "TSS", 5.0), SWMM_OK);
    InitialQualityEditButton button;
    InitialQualityEditRef before;
    before.engine = e;
    before.elementName = QStringLiteral("J0");
    before.summary = initialQualitySummaryFor(e, 0, before.elementName);
    button.setValue(before);
    QSignalSpy notified(&button, &InitialQualityEditButton::valueChanged);
    QTimer::singleShot(0, &button, [&] {
        auto *dlg = button.findChild<InitialQualityDialog *>();
        QVERIFY(dlg);
        if (action == QLatin1String("cancel")) { dlg->reject(); return; }
        if (changed) {
            qobject_cast<QDoubleSpinBox *>(table(*dlg)->cellWidget(0, 3))->setValue(8.0);
        }
        if (action == QLatin1String("partial")) {
            dlg->findChild<QPushButton *>("iq_addBtn")->click();
            auto *constituent = qobject_cast<QComboBox *>(table(*dlg)->cellWidget(1, 2));
            constituent->addItem("Unknown", "Unknown");
            constituent->setCurrentIndex(constituent->count() - 1);
            QTimer::singleShot(0, dlg, [dlg] {
                if (auto *message = dlg->findChild<QMessageBox *>()) message->accept();
            });
            clickOk(*dlg);
            QVERIFY(dlg->wroteAnyChanges());
            dlg->reject();
        } else {
            clickOk(*dlg);
        }
    });
    QVERIFY(QMetaObject::invokeMethod(&button, "onClicked", Qt::DirectConnection));
    QCOMPARE(notified.count(), changed ? 1 : 0);
    QCOMPARE(button.value().wroteChanges, changed);
    QCOMPARE(button.value().summary, before.summary); // values changed, counts did not
    QCOMPARE(button.value() == before, !changed);
}

void TestInitialQualityDialog::partialFailureThenNoOpRetryKeepsChangeFlag()
{
    SWMM_Engine e = makeEngine();
    auto cleanup = qScopeGuard([e] { swmm_engine_destroy(e); });
    QCOMPARE(swmm_init_quality_set(e, 0, 0, "TSS", 5.0), SWMM_OK);
    InitialQualityDialog dlg(e);
    auto *tbl = table(dlg);
    qobject_cast<QDoubleSpinBox *>(tbl->cellWidget(0, 3))->setValue(8.0);
    dlg.findChild<QPushButton *>("iq_addBtn")->click();
    auto *constituent = qobject_cast<QComboBox *>(tbl->cellWidget(1, 2));
    constituent->addItem("Unknown", "Unknown");
    constituent->setCurrentIndex(constituent->count() - 1);
    QSignalSpy applied(&dlg, &InitialQualityDialog::changesApplied);
    QTimer::singleShot(0, &dlg, [&] {
        if (auto *message = dlg.findChild<QMessageBox *>()) message->accept();
    });
    clickOk(dlg);
    QCOMPARE(applied.count(), 1);
    tbl->setCurrentCell(1, 0);
    dlg.findChild<QPushButton *>("iq_removeBtn")->click();
    clickOk(dlg);
    QCOMPARE(dlg.lastWriteCount(), 0);
    QCOMPARE(applied.count(), 1);
    QVERIFY(dlg.wroteAnyChanges());
    QCOMPARE(dlg.result(), int(QDialog::Accepted));
}

void TestInitialQualityDialog::partialFailureRetainsDraftAndAllowsRetry()
{
    SWMM_Engine e = makeEngine();
    auto cleanup = qScopeGuard([e] { swmm_engine_destroy(e); });
    QCOMPARE(swmm_init_quality_set(e, 0, 0, "TSS", 5.0), SWMM_OK);
    InitialQualityDialog dlg(e);
    auto *tbl = table(dlg);
    qobject_cast<QDoubleSpinBox *>(tbl->cellWidget(0, 3))->setValue(8.0);
    dlg.findChild<QPushButton *>("iq_addBtn")->click();
    auto *element = qobject_cast<QComboBox *>(tbl->cellWidget(1, 1));
    element->setCurrentIndex(element->findData(1));
    auto *constituent = qobject_cast<QComboBox *>(tbl->cellWidget(1, 2));
    constituent->addItem("MissingSpecies", "MissingSpecies");
    constituent->setCurrentIndex(constituent->count() - 1);
    QSignalSpy applied(&dlg, &InitialQualityDialog::changesApplied);
    QSignalSpy accepted(&dlg, &QDialog::accepted);
    QString error;
    dlg.show();
    QTimer::singleShot(0, &dlg, [&] {
        if (auto *message = dlg.findChild<QMessageBox *>()) {
            error = message->text();
            message->accept();
        }
    });
    clickOk(dlg);
    QCOMPARE(accepted.count(), 0);
    QVERIFY(dlg.isVisible());
    QVERIFY(error.contains("row 2", Qt::CaseInsensitive));
    QVERIFY(error.contains("MissingSpecies"));
    QVERIFY(dlg.wroteAnyChanges());
    QCOMPARE(dlg.lastWriteCount(), 1);
    QCOMPARE(applied.count(), accepted.count() + 1);
    QCOMPARE(tbl->rowCount(), 2);
    QCOMPARE(constituent->currentText(), QStringLiteral("MissingSpecies"));
    constituent->setCurrentIndex(constituent->findData("TSS"));
    clickOk(dlg);
    QCOMPARE(accepted.count(), 1);
    QCOMPARE(dlg.lastWriteCount(), 1);
    QCOMPARE(applied.count(), accepted.count() + 1);
    QVERIFY(dlg.wroteAnyChanges());
    QCOMPARE(swmm_init_quality_count(e), 2);
}

void TestInitialQualityDialog::rejectedFirstWriteDoesNotClaimChanges()
{
    SWMM_Engine e = makeEngine();
    auto cleanup = qScopeGuard([e] { swmm_engine_destroy(e); });
    InitialQualityDialog dlg(e);
    dlg.findChild<QPushButton *>("iq_addBtn")->click();
    auto *constituent = qobject_cast<QComboBox *>(table(dlg)->cellWidget(0, 2));
    constituent->addItem("Unknown", "Unknown");
    constituent->setCurrentIndex(constituent->count() - 1);
    QSignalSpy applied(&dlg, &InitialQualityDialog::changesApplied);
    QSignalSpy accepted(&dlg, &QDialog::accepted);
    QTimer::singleShot(0, &dlg, [&] {
        if (auto *message = dlg.findChild<QMessageBox *>()) message->accept();
    });
    clickOk(dlg);
    QCOMPARE(accepted.count(), 0);
    QVERIFY(!dlg.wroteAnyChanges());
    QCOMPARE(dlg.lastWriteCount(), 0);
    QCOMPARE(applied.count(), 0);
    QCOMPARE(swmm_init_quality_count(e), 0);
}

void TestInitialQualityDialog::lifecycleChangeRefusesWithoutLosingDraft()
{
    SWMM_Engine e = makeEngine();
    auto cleanup = qScopeGuard([e] { swmm_engine_destroy(e); });
    InitialQualityDialog dlg(e);
    dlg.findChild<QPushButton *>("iq_addBtn")->click();
    QCOMPARE(swmm_engine_close(e), SWMM_OK);
    QSignalSpy applied(&dlg, &InitialQualityDialog::changesApplied);
    QSignalSpy accepted(&dlg, &QDialog::accepted);
    QTimer::singleShot(0, &dlg, [&] {
        if (auto *message = dlg.findChild<QMessageBox *>()) message->accept();
    });
    clickOk(dlg);
    QCOMPARE(accepted.count(), 0);
    QCOMPARE(table(dlg)->rowCount(), 1);
    QCOMPARE(dlg.lastWriteCount(), 0);
    QCOMPARE(applied.count(), 0);
    QVERIFY(!dlg.wroteAnyChanges());
}

void TestInitialQualityDialog::readOnlyUsesClose()
{
    InitialQualityDialog dlg(nullptr);
    auto *buttons = dlg.findChild<QDialogButtonBox *>();
    QCOMPARE(buttons->standardButtons(), QDialogButtonBox::Close);
    QVERIFY(!dlg.findChild<QPushButton *>("iq_addBtn")->isEnabled());
    QVERIFY(!dlg.findChild<QPushButton *>("iq_importBtn")->isEnabled());
    QVERIFY(!dlg.findChild<QLineEdit *>("iq_fileEdit")->isEnabled());
}

void TestInitialQualityDialog::scopeChangeFollowsRowAfterDeletion()
{
    SWMM_Engine e = makeEngine();
    auto cleanup = qScopeGuard([e] { swmm_engine_destroy(e); });
    QCOMPARE(swmm_init_quality_set(e, 0, 0, "TSS", 5.0), SWMM_OK);
    QCOMPARE(swmm_init_quality_set(e, 0, 1, "TSS", 9.0), SWMM_OK);
    InitialQualityDialog dlg(e);
    auto *tbl = table(dlg);
    tbl->setCurrentCell(0, 0);
    dlg.findChild<QPushButton *>("iq_removeBtn")->click();
    QCOMPARE(tbl->rowCount(), 1);
    auto *scope = qobject_cast<QComboBox *>(tbl->cellWidget(0, 0));
    auto *element = qobject_cast<QComboBox *>(tbl->cellWidget(0, 1));
    QVERIFY(scope && element);
    scope->setCurrentIndex(scope->findData(1));
    QCOMPARE(element->count(), 1);
    QCOMPARE(element->currentText(), QStringLiteral("C1"));
    clickOk(dlg);
    QCOMPARE(swmm_init_quality_count(e), 1);
    int link = 0, index = -1;
    char constituent[64] = {};
    double value = 0;
    QCOMPARE(swmm_init_quality_get(e, 0, &link, &index, constituent,
                                   sizeof constituent, &value), SWMM_OK);
    QCOMPARE(link, 1);
    QCOMPARE(index, 0);
    QCOMPARE(value, 9.0);
}

void TestInitialQualityDialog::fileRowsRemainProtectedAfterDeletion()
{
    const auto testDir = QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir();
    const QString output = qEnvironmentVariable("SWMMVIS_IQ_DIALOG_TEST_OUTPUT",
        testDir.filePath("../../workplans/artifacts/phase_21_parallel/dialogs/output"));
    QVERIFY(QDir().mkpath(output));
    QFile fixture(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", testDir.filePath("data"))
                  + QStringLiteral("/gw_exchange_fixture.inp"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    const QByteArray deck = fixture.readAll() +
        "\n[POLLUTANTS]\nTSS MG/L 0 0 0 0\n"
        "[INITIAL_QUALITY]\nFILE initial.csv\nNODE J1 TSS 5\n";
    const QString inputPath = QDir(output).filePath("initial_file.inp");
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    QCOMPARE(input.write(deck), deck.size());
    input.close();
    QFile csv(QDir(output).filePath("initial.csv"));
    QVERIFY(csv.open(QIODevice::WriteOnly));
    const QByteArray csvBytes("scope,element,constituent,value\nLINK,C1,TSS,7\n");
    QCOMPARE(csv.write(csvBytes), csvBytes.size());
    csv.close();
    SWMM_Engine e = swmm_engine_create();
    QVERIFY(e);
    auto cleanup = qScopeGuard([e] { swmm_engine_destroy(e); });
    const auto report = QDir(output).filePath("initial_file.rpt").toUtf8();
    QCOMPARE(swmm_engine_open(e, inputPath.toUtf8().constData(),
                              report.constData(), nullptr, nullptr), SWMM_OK);
    QCOMPARE(swmm_init_quality_count(e), 2);
    QCOMPARE(swmm_init_quality_is_file(e, 0), 0);
    QCOMPARE(swmm_init_quality_is_file(e, 1), 1);
    InitialQualityDialog dlg(e);
    auto *tbl = table(dlg);
    auto *remove = dlg.findChild<QPushButton *>("iq_removeBtn");
    tbl->setCurrentCell(0, 0);
    remove->click();
    QCOMPARE(tbl->rowCount(), 1);
    dlg.findChild<QPushButton *>("iq_addBtn")->click();
    QCOMPARE(tbl->rowCount(), 2);
    auto *value = qobject_cast<QDoubleSpinBox *>(tbl->cellWidget(1, 3));
    QVERIFY(value);
    value->setValue(11.0);
    clickOk(dlg);
    QCOMPARE(swmm_init_quality_count(e), 2);
    QCOMPARE(dlg.lastWriteCount(), 1);
    int fileCount = 0, inlineCount = 0;
    for (int i = 0; i < 2; ++i) {
        int link = 0, element = -1;
        char constituent[64] = {};
        double result = 0;
        QCOMPARE(swmm_init_quality_get(e, i, &link, &element, constituent,
                                       sizeof constituent, &result), SWMM_OK);
        if (swmm_init_quality_is_file(e, i)) {
            QCOMPARE(result, 7.0);
            ++fileCount;
        } else {
            QCOMPARE(result, 11.0);
            ++inlineCount;
        }
    }
    QCOMPARE(fileCount, 1);
    QCOMPARE(inlineCount, 1);
    InitialQualityDialog reopened(e);
    auto *reopenedTable = table(reopened);
    const int fileRow = reopenedTable->cellWidget(0, 0)->isEnabled() ? 1 : 0;
    reopenedTable->setCurrentCell(fileRow, 0);
    reopened.findChild<QPushButton *>("iq_removeBtn")->click();
    QCOMPARE(reopenedTable->rowCount(), 2);
    QVERIFY(csv.open(QIODevice::ReadOnly));
    QCOMPARE(csv.readAll(), csvBytes);
}

void TestInitialQualityDialog::fieldsHaveAccessibleNamesAndCsvBuddy()
{
    SWMM_Engine e = makeEngine();
    auto cleanup = qScopeGuard([e] { swmm_engine_destroy(e); });
    InitialQualityDialog dlg(e);
    dlg.findChild<QPushButton *>("iq_addBtn")->click();
    auto *file = dlg.findChild<QLineEdit *>("iq_fileEdit");
    QVERIFY(file);
    bool hasBuddy = false;
    for (auto *label : dlg.findChildren<QLabel *>())
        if (label->buddy() == file) {
            QVERIFY(label->text().contains('&'));
            hasBuddy = true;
        }
    QVERIFY(hasBuddy);
    for (QWidget *widget : {static_cast<QWidget *>(table(dlg)),
                           table(dlg)->cellWidget(0, 0), table(dlg)->cellWidget(0, 1),
                           table(dlg)->cellWidget(0, 2), table(dlg)->cellWidget(0, 3)}) {
        auto *accessible = QAccessible::queryAccessibleInterface(widget);
        QVERIFY(accessible);
        QVERIFY2(!accessible->text(QAccessible::Name).isEmpty(), widget->metaObject()->className());
    }
}

void TestInitialQualityDialog::auxiliaryButtonsAreNotEnterDefaults()
{
    InitialQualityDialog dlg(nullptr);
    auto *buttons = dlg.findChild<QDialogButtonBox *>();
    for (auto *button : dlg.findChildren<QPushButton *>()) {
        if (buttons->standardButton(button) != QDialogButtonBox::NoButton) continue;
        QVERIFY2(!button->autoDefault(), qPrintable(button->text()));
    }
}

void TestInitialQualityDialog::escapeDiscardsDraft()
{
    SWMM_Engine e = makeEngine();
    auto cleanup = qScopeGuard([e] { swmm_engine_destroy(e); });
    InitialQualityDialog dlg(e);
    dlg.findChild<QPushButton *>("iq_addBtn")->click();
    QSignalSpy rejected(&dlg, &QDialog::rejected);
    dlg.show();
    QTest::keyClick(&dlg, Qt::Key_Escape);
    QCOMPARE(rejected.count(), 1);
    QCOMPARE(swmm_init_quality_count(e), 0);
    QVERIFY(!dlg.wroteAnyChanges());
}

void TestInitialQualityDialog::constructsWithNullEngine()
{
    InitialQualityDialog dlg(nullptr, nullptr);
    QCOMPARE(dlg.wroteAnyChanges(), false);
}

void TestInitialQualityDialog::hydratesEngineRows()
{
    SWMM_Engine e = makeEngine();
    QCOMPARE(swmm_init_quality_set(e, 0, 1, "TSS", 12.5), SWMM_OK);
    QCOMPARE(swmm_init_quality_set(e, 1, 0, "TSS", 9.0), SWMM_OK);

    InitialQualityDialog dlg(e, nullptr);
    auto *tbl = table(dlg);
    QVERIFY(tbl);
    QCOMPARE(tbl->rowCount(), 2);

    auto *scope = qobject_cast<QComboBox *>(tbl->cellWidget(0, 0));
    auto *elem  = qobject_cast<QComboBox *>(tbl->cellWidget(0, 1));
    auto *cons  = qobject_cast<QComboBox *>(tbl->cellWidget(0, 2));
    auto *val   = qobject_cast<QDoubleSpinBox *>(tbl->cellWidget(0, 3));
    QVERIFY(scope && elem && cons && val);
    QCOMPARE(scope->currentData().toInt(), 0);
    QCOMPARE(elem->currentText(), QStringLiteral("J1"));
    QCOMPARE(cons->currentData().toString(), QStringLiteral("TSS"));
    QCOMPARE(val->value(), 12.5);

    auto *scope1 = qobject_cast<QComboBox *>(tbl->cellWidget(1, 0));
    auto *elem1  = qobject_cast<QComboBox *>(tbl->cellWidget(1, 1));
    QVERIFY(scope1 && elem1);
    QCOMPARE(scope1->currentData().toInt(), 1);
    QCOMPARE(elem1->currentText(), QStringLiteral("C1"));

    QCOMPARE(dlg.wroteAnyChanges(), false);
    swmm_engine_destroy(e);
}

void TestInitialQualityDialog::addedRowReachesEngineOnOk()
{
    SWMM_Engine e = makeEngine();
    QCOMPARE(swmm_init_quality_set(e, 0, 0, "TSS", 5.0), SWMM_OK);

    InitialQualityDialog dlg(e, nullptr);
    auto *add = dlg.findChild<QPushButton *>(QStringLiteral("iq_addBtn"));
    QVERIFY(add);
    add->click();

    auto *tbl = table(dlg);
    QVERIFY(tbl);
    QCOMPARE(tbl->rowCount(), 2);
    const int r = 1;
    auto *elem = qobject_cast<QComboBox *>(tbl->cellWidget(r, 1));
    auto *val  = qobject_cast<QDoubleSpinBox *>(tbl->cellWidget(r, 3));
    QVERIFY(elem && val);
    elem->setCurrentIndex(elem->findText(QStringLiteral("J1")));
    val->setValue(12.5);
    clickOk(dlg);

    QVERIFY(dlg.wroteAnyChanges());
    QCOMPARE(dlg.lastWriteCount(), 1);          // exactly the new row
    QCOMPARE(swmm_init_quality_count(e), 2);
    swmm_engine_destroy(e);
}

void TestInitialQualityDialog::noOpOkWritesNothing()
{
    SWMM_Engine e = makeEngine();
    QCOMPARE(swmm_init_quality_set(e, 0, 0, "TSS", 5.0), SWMM_OK);

    InitialQualityDialog dlg(e, nullptr);
    QSignalSpy applied(&dlg, &InitialQualityDialog::changesApplied);
    clickOk(dlg);

    QCOMPARE(applied.count(), 0);
    QCOMPARE(dlg.lastWriteCount(), 0);
    QCOMPARE(dlg.wroteAnyChanges(), false);
    QCOMPARE(swmm_init_quality_count(e), 1);
    double v = 0.0;
    int is_link = 0, elem = -1;
    char buf[64];
    QCOMPARE(swmm_init_quality_get(e, 0, &is_link, &elem, buf, 64, &v),
             SWMM_OK);
    QCOMPARE(v, 5.0);
    swmm_engine_destroy(e);
}

void TestInitialQualityDialog::deletedRowIsRemovedFromEngine()
{
    SWMM_Engine e = makeEngine();
    QCOMPARE(swmm_init_quality_set(e, 0, 0, "TSS", 5.0), SWMM_OK);
    QCOMPARE(swmm_init_quality_set(e, 1, 0, "TSS", 9.0), SWMM_OK);

    InitialQualityDialog dlg(e, nullptr);
    auto *tbl = table(dlg);
    QVERIFY(tbl);
    QCOMPARE(tbl->rowCount(), 2);
    tbl->setCurrentCell(0, 0);
    auto *rem = dlg.findChild<QPushButton *>(QStringLiteral("iq_removeBtn"));
    QVERIFY(rem);
    rem->click();
    QCOMPARE(tbl->rowCount(), 1);
    clickOk(dlg);

    QVERIFY(dlg.wroteAnyChanges());
    QCOMPARE(swmm_init_quality_count(e), 1);    // stale key would read 2
    int is_link = 0, elem = -1;
    char buf[64];
    double v = 0.0;
    QCOMPARE(swmm_init_quality_get(e, 0, &is_link, &elem, buf, 64, &v),
             SWMM_OK);
    QCOMPARE(is_link, 1);                       // the LINK row survived
    swmm_engine_destroy(e);
}

void TestInitialQualityDialog::reservedSpeciesAreOptionGated()
{
    // Age and heat OFF: only the pollutant is offered.
    {
        SWMM_Engine e = makeEngine(false, false);
        InitialQualityDialog dlg(e, nullptr);
        auto *add = dlg.findChild<QPushButton *>(
            QStringLiteral("iq_addBtn"));
        QVERIFY(add);
        add->click();
        auto *cons = qobject_cast<QComboBox *>(
            table(dlg)->cellWidget(0, 2));
        QVERIFY(cons);
        QCOMPARE(cons->count(), 1);
        QCOMPARE(cons->itemData(0).toString(), QStringLiteral("TSS"));
        swmm_engine_destroy(e);
    }
    // Both ON: pollutant + age + temperature.
    {
        SWMM_Engine e = makeEngine(true, true);
        InitialQualityDialog dlg(e, nullptr);
        auto *add = dlg.findChild<QPushButton *>(
            QStringLiteral("iq_addBtn"));
        QVERIFY(add);
        add->click();
        auto *cons = qobject_cast<QComboBox *>(
            table(dlg)->cellWidget(0, 2));
        QVERIFY(cons);
        QCOMPARE(cons->count(), 3);
        QVector<QString> offered;
        for (int i = 0; i < cons->count(); ++i)
            offered.append(cons->itemData(i).toString());
        QVERIFY(offered.contains(QStringLiteral("__WATER_AGE__")));
        QVERIFY(offered.contains(QStringLiteral("__TEMPERATURE__")));

        // Reserved species accept negatives (signed age, D-NS1); pollutant
        // selection floors the spin at zero.
        auto *val = qobject_cast<QDoubleSpinBox *>(
            table(dlg)->cellWidget(0, 3));
        QVERIFY(val);
        QCOMPARE(val->minimum(), 0.0);          // pollutant selected first
        cons->setCurrentIndex(
            cons->findData(QStringLiteral("__WATER_AGE__")));
        QVERIFY(val->minimum() < 0.0);
        swmm_engine_destroy(e);
    }
}

void TestInitialQualityDialog::elementScopeShowsOnlyThatElementsRows()
{
    SWMM_Engine e = makeEngine();
    QCOMPARE(swmm_init_quality_set(e, 0, 0, "TSS", 5.0),  SWMM_OK);  // J0
    QCOMPARE(swmm_init_quality_set(e, 0, 1, "TSS", 12.5), SWMM_OK);  // J1
    QCOMPARE(swmm_init_quality_set(e, 1, 0, "TSS", 9.0),  SWMM_OK);  // C1

    InitialQualityDialog dlg(e, nullptr);
    dlg.setElementScope(0, QStringLiteral("J1"));

    auto *tbl = table(dlg);
    QVERIFY(tbl);
    QCOMPARE(tbl->rowCount(), 1);
    QVERIFY(tbl->isColumnHidden(0));            // Scope collapsed
    QVERIFY(tbl->isColumnHidden(1));            // Element collapsed
    auto *val = qobject_cast<QDoubleSpinBox *>(tbl->cellWidget(0, 3));
    QVERIFY(val);
    QCOMPARE(val->value(), 12.5);
    swmm_engine_destroy(e);
}

void TestInitialQualityDialog::elementScopeEditPreservesOtherElements()
{
    SWMM_Engine e = makeEngine();
    QCOMPARE(swmm_init_quality_set(e, 0, 0, "TSS", 5.0),  SWMM_OK);  // J0
    QCOMPARE(swmm_init_quality_set(e, 0, 1, "TSS", 12.5), SWMM_OK);  // J1
    QCOMPARE(swmm_init_quality_set(e, 1, 0, "TSS", 9.0),  SWMM_OK);  // C1

    InitialQualityDialog dlg(e, nullptr);
    dlg.setElementScope(0, QStringLiteral("J1"));
    auto *tbl = table(dlg);
    QVERIFY(tbl);
    auto *val = qobject_cast<QDoubleSpinBox *>(tbl->cellWidget(0, 3));
    QVERIFY(val);
    val->setValue(20.0);
    clickOk(dlg);

    QCOMPARE(dlg.lastWriteCount(), 1);
    QCOMPARE(swmm_init_quality_count(e), 3);    // nobody else was removed
    int found = 0;
    for (int i = 0; i < 3; ++i) {
        int is_link = 0, elem = -1;
        char buf[64];
        double v = 0.0;
        QCOMPARE(swmm_init_quality_get(e, i, &is_link, &elem, buf, 64, &v),
                 SWMM_OK);
        if (!is_link && elem == 0) { QCOMPARE(v, 5.0);  ++found; }
        if (!is_link && elem == 1) { QCOMPARE(v, 20.0); ++found; }
        if (is_link  && elem == 0) { QCOMPARE(v, 9.0);  ++found; }
    }
    QCOMPARE(found, 3);
    swmm_engine_destroy(e);
}

void TestInitialQualityDialog::elementScopeAddAndRemoveStayScoped()
{
    SWMM_Engine e = makeEngine();
    QCOMPARE(swmm_init_quality_set(e, 0, 0, "TSS", 5.0), SWMM_OK);   // J0

    // Add in scope: the new row is pinned to J1 (combos locked) and lands
    // on the engine as a NODE J1 row.
    {
        InitialQualityDialog dlg(e, nullptr);
        dlg.setElementScope(0, QStringLiteral("J1"));
        auto *tbl = table(dlg);
        QVERIFY(tbl);
        QCOMPARE(tbl->rowCount(), 0);
        auto *add = dlg.findChild<QPushButton *>(QStringLiteral("iq_addBtn"));
        QVERIFY(add);
        add->click();
        auto *scope = qobject_cast<QComboBox *>(tbl->cellWidget(0, 0));
        auto *elem  = qobject_cast<QComboBox *>(tbl->cellWidget(0, 1));
        auto *val   = qobject_cast<QDoubleSpinBox *>(tbl->cellWidget(0, 3));
        QVERIFY(scope && elem && val);
        QVERIFY(!scope->isEnabled());
        QVERIFY(!elem->isEnabled());
        QCOMPARE(elem->currentText(), QStringLiteral("J1"));
        val->setValue(7.5);
        clickOk(dlg);
        QCOMPARE(dlg.lastWriteCount(), 1);
        QCOMPARE(swmm_init_quality_count(e), 2);
    }
    // Remove in scope: only J1's row goes; J0's survives.
    {
        InitialQualityDialog dlg(e, nullptr);
        dlg.setElementScope(0, QStringLiteral("J1"));
        auto *tbl = table(dlg);
        QVERIFY(tbl);
        QCOMPARE(tbl->rowCount(), 1);
        tbl->setCurrentCell(0, 3);
        auto *rem = dlg.findChild<QPushButton *>(
            QStringLiteral("iq_removeBtn"));
        QVERIFY(rem);
        rem->click();
        clickOk(dlg);
        QCOMPARE(swmm_init_quality_count(e), 1);
        int is_link = 0, elem = -1;
        char buf[64];
        double v = 0.0;
        QCOMPARE(swmm_init_quality_get(e, 0, &is_link, &elem, buf, 64, &v),
                 SWMM_OK);
        QCOMPARE(is_link, 0);
        QCOMPARE(elem, 0);                      // J0's row survived
        QCOMPARE(v, 5.0);
    }
    swmm_engine_destroy(e);
}

void TestInitialQualityDialog::exactValuesSurviveRoundedDisplay_data()
{
    QTest::addColumn<bool>("editOtherRow");
    QTest::newRow("no-op") << false;
    QTest::newRow("unrelated-row") << true;
}

void TestInitialQualityDialog::exactValuesSurviveRoundedDisplay()
{
    QFETCH(bool, editOtherRow);
    SWMM_Engine e = makeEngine();
    auto cleanup = qScopeGuard([e] { swmm_engine_destroy(e); });
    const double exact = 1.234567891;
    QCOMPARE(swmm_init_quality_set(e, 0, 0, "TSS", exact), SWMM_OK);
    QCOMPARE(swmm_init_quality_set(e, 0, 1, "TSS", 8.76543219), SWMM_OK);
    InitialQualityDialog dlg(e);
    if (editOtherRow)
        qobject_cast<QDoubleSpinBox *>(table(dlg)->cellWidget(1, 3))->setValue(9.25);
    clickOk(dlg);
    int scope = -1, element = -1;
    char constituent[128] = {};
    double actual = 0;
    QCOMPARE(swmm_init_quality_get(e, 0, &scope, &element, constituent, sizeof(constituent), &actual), SWMM_OK);
    QCOMPARE(actual, exact);
    QCOMPARE(swmm_init_quality_get(e, 1, &scope, &element, constituent, sizeof(constituent), &actual), SWMM_OK);
    QCOMPARE(actual, editOtherRow ? 9.25 : 8.76543219);
    QCOMPARE(dlg.wroteAnyChanges(), editOtherRow);
    if (!editOtherRow) QCOMPARE(dlg.lastWriteCount(), 0);
}

void TestInitialQualityDialog::invalidationDiscardsDraftAndGuardsLaterSignals()
{
    SWMM_Engine e = makeEngine();
    InitialQualityDialog dlg(e);
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

QTEST_MAIN(TestInitialQualityDialog)
#include "test_initialqualitydialog.moc"
