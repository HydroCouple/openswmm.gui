/**
 * @file      test_pollutanteditordialog.cpp
 * @brief     Tests for pollutanteditordialog.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/dialogs/pollutanteditordialog.h"
#include "pollutant/pollutantregistry.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QTimer>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

using openswmmvis::ui::PollutantEditorDialog;
using openswmmvis::pollutant::PollutantRegistry;

class PollutantTestOwner : public QObject
{
    Q_OBJECT
signals:
    /**
     *
     * @brief Notify test registry consumers that their borrowed engine owner is closing.
     * Signal carries no handle and performs no destruction itself; cases emit it to exercise
     * invalidation before queued or modal callbacks can write.
     *
     */
    void engineAboutToClose();
};

class TestPollutantEditorDialog : public QObject
{
    Q_OBJECT
private slots:
    void ownerClosingInvalidatesExistingRegistry();
    void ownerClosingDuringDeleteRefusesLateConfirmation();
    void registryDestructionClosesEditor();
    void editsPreserveUnchangedExactValues_data();
    void editsPreserveUnchangedExactValues();
    void absentRegistryDisablesEditors();
    void listAndActionsAreAccessible();
};

/**
 *
 * @brief Register no-op, unrelated snow-checkbox and explicit rain-concentration edits.
 * Each row distinguishes unchanged full-precision storage from intended field mutation.
 *
 */
void TestPollutantEditorDialog::editsPreserveUnchangedExactValues_data()
{
    QTest::addColumn<QString>("edit");
    QTest::newRow("no-op") << QStringLiteral("none");
    QTest::newRow("unrelated-checkbox") << QStringLiteral("snow");
    QTest::newRow("explicit-rain") << QStringLiteral("rain");
}

/**
 *
 * @brief Require binding and unrelated edits not to round stored pollutant parameters through
 * display controls.
 * Checks seven exact parameter values, intended rain9.25 or snowOnly change, and zero/
 * one registry notification according to the fetched edit. No native engine/file save.
 *
 */
void TestPollutantEditorDialog::editsPreserveUnchangedExactValues()
{
    QFETCH(QString, edit);
    PollutantRegistry registry;
    auto *provider = registry.create(QStringLiteral("TSS"));
    QVERIFY(provider);
    provider->setRainConc(1.234567891);
    provider->setGwConc(2.345678912);
    provider->setRdiiConc(3.456789123);
    provider->setInitConc(4.567891234);
    provider->setKDecay(.123456789);
    provider->setMwt(5.678912345);
    provider->setCoFraction(.234567891);
    QSignalSpy changes(&registry, &PollutantRegistry::providerParamsChanged);
    PollutantEditorDialog dialog(&registry, nullptr);
    QCOMPARE(dialog.currentProvider(), provider);
    if (edit == QStringLiteral("snow"))
        dialog.findChild<QCheckBox *>()->setChecked(true);
    if (edit == QStringLiteral("rain")) {
        QDoubleSpinBox *rain = nullptr;
        for (auto *label : dialog.findChildren<QLabel *>()) {
            if (label->text().remove(QLatin1Char('&')) == QStringLiteral("Rain Concentration"))
                rain = qobject_cast<QDoubleSpinBox *>(label->buddy());
        }
        QVERIFY(rain);
        rain->setValue(9.25);
    }
    QCOMPARE(provider->rainConc(), edit == QStringLiteral("rain") ? 9.25 : 1.234567891);
    QCOMPARE(provider->gwConc(), 2.345678912);
    QCOMPARE(provider->rdiiConc(), 3.456789123);
    QCOMPARE(provider->initConc(), 4.567891234);
    QCOMPARE(provider->kDecay(), .123456789);
    QCOMPARE(provider->mwt(), 5.678912345);
    QCOMPARE(provider->coFraction(), .234567891);
    QCOMPARE(provider->snowOnly(), edit == QStringLiteral("snow"));
    QCOMPARE(changes.count(), edit == QStringLiteral("none") ? 0 : 1);
}

/**
 *
 * @brief Require missing-registry construction to disable the name edit, all double spins and
 * New/Delete buttons.
 * Does not attempt mutations through other controls or test late registry attachment.
 *
 */
void TestPollutantEditorDialog::absentRegistryDisablesEditors()
{
    PollutantEditorDialog dialog(nullptr, nullptr);
    QVERIFY(!dialog.nameEdit()->isEnabled());
    for (auto *spin : dialog.findChildren<QDoubleSpinBox *>())
        QVERIFY(!spin->isEnabled());
    for (auto *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("New") || button->text() == QStringLiteral("Delete"))
            QVERIFY(!button->isEnabled());
    }
}

/**
 *
 * @brief Require a named pollutant list and New/Delete buttons without auto-default behavior.
 * Does not test every control, full mnemonic uniqueness or a screen reader.
 *
 */
void TestPollutantEditorDialog::listAndActionsAreAccessible()
{
    PollutantRegistry registry;
    PollutantEditorDialog dialog(&registry, nullptr);
    QVERIFY(!dialog.listView()->accessibleName().isEmpty());
    for (auto *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("New") || button->text() == QStringLiteral("Delete"))
            QVERIFY(!button->autoDefault());
    }
}

/**
 *
 * @brief Require owner-close notification to hide/disable the editor and block late
 * New/name/checkbox mutations.
 * Checks no current provider, one original TSS provider, unchanged name and no registry
 * parameter notification. Registry/provider remain alive; no engine is destroyed here.
 *
 */
void TestPollutantEditorDialog::ownerClosingInvalidatesExistingRegistry()
{
    PollutantTestOwner owner;
    PollutantRegistry registry(&owner);
    auto *provider = registry.create(QStringLiteral("TSS"));
    PollutantEditorDialog dialog(&registry, nullptr);
    QSignalSpy changed(&registry, &PollutantRegistry::providerParamsChanged);
    dialog.show();
    emit owner.engineAboutToClose();
    QVERIFY(!dialog.isEnabled());
    QVERIFY(!dialog.isVisible());
    QVERIFY(!dialog.currentProvider());
    dialog.invokeNew();
    dialog.findChild<QCheckBox *>()->setChecked(true);
    dialog.nameEdit()->setText(QStringLiteral("renamed"));
    QVERIFY(QMetaObject::invokeMethod(&dialog, "onNameEdited_", Qt::DirectConnection));
    QCOMPARE(registry.providerCount(), 1);
    QCOMPARE(provider->name(), QStringLiteral("TSS"));
    QCOMPARE(changed.count(), 0);
}

/**
 *
 * @brief Require owner closure after a real Yes click but before modal unwind to prevent provider
 * deletion.
 * Queued callback borrows live owner/dialog/local flag, finds the QMessageBox Yes button,
 * clicks then emits closure. Checks prompt visited, provider count one and disabled
 * editor; absent prompt/button makes callback return without dismissing a modal loop.
 *
 */
void TestPollutantEditorDialog::ownerClosingDuringDeleteRefusesLateConfirmation()
{
    PollutantTestOwner owner;
    PollutantRegistry registry(&owner);
    registry.create(QStringLiteral("TSS"));
    PollutantEditorDialog dialog(&registry, nullptr);
    bool promptSeen = false;
    QTimer::singleShot(0, &dialog, [&] {
        auto *prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!prompt)
            return;
        auto *yes = prompt->button(QMessageBox::Yes);
        if (!yes)
            return;
        promptSeen = true;
        // Click the real button so question() returns Yes. Model closure
        // follows before this callback returns and its nested loop unwinds.
        yes->click();
        emit owner.engineAboutToClose();
    });
    QVERIFY(QMetaObject::invokeMethod(&dialog, "onDeleteClicked_", Qt::DirectConnection));
    QVERIFY(promptSeen);
    QCOMPARE(registry.providerCount(), 1);
    QVERIFY(!dialog.isEnabled());
}

/**
 *
 * @brief Require registry destruction to hide/disable its editor and leave no provider after a
 * late New request.
 * Deletes the heap registry while the local dialog survives; does not dereference the
 * destroyed registry/provider or test undo ownership.
 *
 */
void TestPollutantEditorDialog::registryDestructionClosesEditor()
{
    auto *registry = new PollutantRegistry;
    registry->create(QStringLiteral("TSS"));
    PollutantEditorDialog dialog(registry, nullptr);
    dialog.show();
    delete registry;
    QVERIFY(!dialog.isEnabled());
    QVERIFY(!dialog.isVisible());
    dialog.invokeNew();
    QVERIFY(!dialog.currentProvider());
}

QTEST_MAIN(TestPollutantEditorDialog)
#include "test_pollutanteditordialog.moc"
