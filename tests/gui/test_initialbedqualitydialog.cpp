/* SPDX-License-Identifier: Apache-2.0 */
#include "ui/dialogs/initialbedqualitydialog.h"

#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_pollutants.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTest>
#include <QScopeGuard>

using OpenSWMMVis::InitialBedQualityDialog;

namespace {

SWMM_Engine makeEngine() {
    SWMM_Engine engine = swmm_engine_new();
    if (engine) swmm_pollutant_add(engine, "TSS", 0);
    return engine;
}

void clickOk(InitialBedQualityDialog& dialog) {
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    Q_ASSERT(buttons);
    buttons->button(QDialogButtonBox::Ok)->click();
}

} // namespace

class TestInitialBedQualityDialog : public QObject {
    Q_OBJECT
private slots:
    void hydratesAndWritesScopedBedRows();
    void noOpOkDoesNotWrite();
};

void TestInitialBedQualityDialog::hydratesAndWritesScopedBedRows() {
    SWMM_Engine engine = makeEngine();
    QVERIFY(engine);
    auto cleanup = qScopeGuard([engine] { swmm_engine_destroy(engine); });
    QCOMPARE(swmm_2d_set_initial_bed_quality(engine, SWMM_2D_QUAL_SCOPE_GLOBAL,
                                               nullptr, -1, "TSS", 2.0), SWMM_OK);

    InitialBedQualityDialog dialog(engine);
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("ibq_table"));
    QVERIFY(table);
    QCOMPARE(table->rowCount(), 1);
    auto* initialSpecies = qobject_cast<QComboBox*>(table->cellWidget(0, 2));
    QVERIFY(initialSpecies);
    QCOMPARE(initialSpecies->currentData().toString(),
             QStringLiteral("TSS"));

    QSignalSpy changed(&dialog, &InitialBedQualityDialog::changesApplied);
    dialog.findChild<QPushButton*>(QStringLiteral("ibq_add"))->click();
    auto* scope = qobject_cast<QComboBox*>(table->cellWidget(1, 0));
    QVERIFY(scope);
    scope->setCurrentIndex(scope->findData(SWMM_2D_QUAL_SCOPE_TAG));
    auto* target = qobject_cast<QComboBox*>(table->cellWidget(1, 1));
    QVERIFY(target);
    target->setEditText(QStringLiteral("channel:burned"));
    auto* species = qobject_cast<QComboBox*>(table->cellWidget(1, 2));
    QVERIFY(species);
    species->setCurrentIndex(species->findData(QStringLiteral("TSS")));
    auto* value = qobject_cast<QDoubleSpinBox*>(table->cellWidget(1, 3));
    QVERIFY(value);
    value->setValue(3.25);
    clickOk(dialog);

    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QVERIFY(dialog.wroteAnyChanges());
    QVERIFY(dialog.lastWriteCount() > 0);
    QCOMPARE(changed.count(), 1);
    int count = 0;
    QCOMPARE(swmm_2d_initial_bed_quality_count(engine, &count), SWMM_OK);
    QCOMPARE(count, 2);
    int scopeValue = -1;
    char tag[64]{}, speciesName[64]{};
    double concentration = 0.0;
    QCOMPARE(swmm_2d_get_initial_bed_quality(engine, 1, &scopeValue, tag, sizeof tag,
        nullptr, speciesName, sizeof speciesName, &concentration), SWMM_OK);
    QCOMPARE(scopeValue, SWMM_2D_QUAL_SCOPE_TAG);
    QCOMPARE(QString::fromUtf8(tag), QStringLiteral("channel:burned"));
    QCOMPARE(QString::fromUtf8(speciesName), QStringLiteral("TSS"));
    QCOMPARE(concentration, 3.25);
}

void TestInitialBedQualityDialog::noOpOkDoesNotWrite() {
    SWMM_Engine engine = makeEngine();
    QVERIFY(engine);
    auto cleanup = qScopeGuard([engine] { swmm_engine_destroy(engine); });
    QCOMPARE(swmm_2d_set_initial_bed_quality(engine, SWMM_2D_QUAL_SCOPE_GLOBAL,
                                               nullptr, -1, "TSS", 2.0), SWMM_OK);
    InitialBedQualityDialog dialog(engine);
    QSignalSpy changed(&dialog, &InitialBedQualityDialog::changesApplied);
    clickOk(dialog);
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QVERIFY(!dialog.wroteAnyChanges());
    QCOMPARE(dialog.lastWriteCount(), 0);
    QCOMPARE(changed.count(), 0);
}

QTEST_MAIN(TestInitialBedQualityDialog)
#include "test_initialbedqualitydialog.moc"
