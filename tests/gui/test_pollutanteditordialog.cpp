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
#include "ui/editors/dataobjectdraftsession.h"

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
#include <QUndoStack>

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
    void editsAreHeldUntilApplyAndUndoAsOneStep();
    void revertDiscardsTheDraft();
    void changeElsewhereIsAConflictWithADraftAndARefreshWithout();
    void closingWithADraftAsks();
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
    QVERIFY(dialog.draftSession()->apply());
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

namespace
{
QDoubleSpinBox *spinFor(PollutantEditorDialog &dialog, const QString &labelText)
{
    for (auto *label : dialog.findChildren<QLabel *>())
        if (label->text().remove(QLatin1Char('&')) == labelText)
            return qobject_cast<QDoubleSpinBox *>(label->buddy());
    return nullptr;
}
using State = openswmmvis::ui::DataObjectDraftSession::State;

/** Press the named button of the message box the next modal call opens. */
void answerNextPrompt(const QString &buttonText, bool *seen)
{
    QTimer::singleShot(0, [buttonText, seen] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box)
            return;
        for (auto *button : box->buttons())
            if (button->text().remove(QLatin1Char('&')) == buttonText) {
                *seen = true;
                button->click();
                return;
            }
        box->reject();
    });
}
} // namespace

/** An edit stays in the editor until Apply; Apply is one undo step; the indicator follows. */
void TestPollutantEditorDialog::editsAreHeldUntilApplyAndUndoAsOneStep()
{
    PollutantRegistry registry;
    auto *tss = registry.create(QStringLiteral("TSS"));
    auto *lead = registry.create(QStringLiteral("Lead"));
    QUndoStack undo;
    PollutantEditorDialog dialog(&registry, nullptr);
    dialog.setUndoStack(&undo);
    auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("draftApply"));
    auto *revert = dialog.findChild<QPushButton *>(QStringLiteral("draftRevert"));
    auto *status = dialog.findChild<QLabel *>(QStringLiteral("draftStatus"));
    QVERIFY(apply && revert && status);
    QCOMPARE(dialog.draftSession()->state(), State::Clean);
    QVERIFY(!apply->isEnabled() && !revert->isEnabled());

    QSignalSpy changes(&registry, &PollutantRegistry::providerParamsChanged);
    QCOMPARE(dialog.currentProvider(), tss);
    spinFor(dialog, QStringLiteral("Rain Concentration"))->setValue(9.25);
    spinFor(dialog, QStringLiteral("GW Concentration"))->setValue(3.5);
    dialog.listView()->setCurrentIndex(dialog.listView()->model()->index(1, 0));
    QCOMPARE(dialog.currentProvider(), lead);
    spinFor(dialog, QStringLiteral("Rain Concentration"))->setValue(1.5);

    // Nothing reached the model: other views still see the old values.
    QCOMPARE(tss->rainConc(), 0.0);
    QCOMPARE(lead->rainConc(), 0.0);
    QCOMPARE(changes.count(), 0);
    QCOMPARE(dialog.draftSession()->state(), State::Unapplied);
    QVERIFY(apply->isEnabled() && revert->isEnabled());
    QVERIFY(status->text().contains(QStringLiteral("2")));
    // Returning to an object shows its draft, not the model value.
    dialog.listView()->setCurrentIndex(dialog.listView()->model()->index(0, 0));
    QCOMPARE(spinFor(dialog, QStringLiteral("Rain Concentration"))->value(), 9.25);

    apply->click();
    QCOMPARE(tss->rainConc(), 9.25);
    QCOMPARE(tss->gwConc(), 3.5);
    QCOMPARE(lead->rainConc(), 1.5);
    QCOMPARE(dialog.draftSession()->state(), State::Applied);
    QVERIFY(!apply->isEnabled());
    QCOMPARE(undo.count(), 1); // both objects, all fields: one step

    undo.undo();
    QCOMPARE(tss->rainConc(), 0.0);
    QCOMPARE(tss->gwConc(), 0.0);
    QCOMPARE(lead->rainConc(), 0.0);
    QCOMPARE(spinFor(dialog, QStringLiteral("Rain Concentration"))->value(), 0.0); // form follows
    undo.redo();
    QCOMPARE(tss->rainConc(), 9.25);
    QCOMPARE(lead->rainConc(), 1.5);

    // Editing a value back to the model's value leaves nothing to apply.
    spinFor(dialog, QStringLiteral("Rain Concentration"))->setValue(4.0);
    QCOMPARE(dialog.draftSession()->state(), State::Unapplied);
    spinFor(dialog, QStringLiteral("Rain Concentration"))->setValue(9.25);
    QVERIFY(!dialog.draftSession()->hasDraft());
    QVERIFY(!apply->isEnabled());
}

void TestPollutantEditorDialog::revertDiscardsTheDraft()
{
    PollutantRegistry registry;
    auto *tss = registry.create(QStringLiteral("TSS"));
    tss->setRainConc(2.0);
    PollutantEditorDialog dialog(&registry, nullptr);
    spinFor(dialog, QStringLiteral("Rain Concentration"))->setValue(9.25);
    QVERIFY(dialog.draftSession()->hasDraft());
    dialog.findChild<QPushButton *>(QStringLiteral("draftRevert"))->click();
    QVERIFY(!dialog.draftSession()->hasDraft());
    QCOMPARE(dialog.draftSession()->state(), State::Clean);
    QCOMPARE(spinFor(dialog, QStringLiteral("Rain Concentration"))->value(), 2.0);
    QCOMPARE(tss->rainConc(), 2.0);
}

/** The same pollutant edited in another view must neither wipe the draft nor be wiped unasked. */
void TestPollutantEditorDialog::changeElsewhereIsAConflictWithADraftAndARefreshWithout()
{
    PollutantRegistry registry;
    auto *tss = registry.create(QStringLiteral("TSS"));
    PollutantEditorDialog dialog(&registry, nullptr);

    tss->setGwConc(6.0); // no draft: the form follows the model
    QCOMPARE(spinFor(dialog, QStringLiteral("GW Concentration"))->value(), 6.0);
    QCOMPARE(dialog.draftSession()->state(), State::Clean);

    spinFor(dialog, QStringLiteral("Rain Concentration"))->setValue(9.25);
    tss->setGwConc(7.0); // with a draft: kept, flagged
    QCOMPARE(dialog.draftSession()->state(), State::Conflict);
    QCOMPARE(spinFor(dialog, QStringLiteral("Rain Concentration"))->value(), 9.25);
    QCOMPARE(tss->rainConc(), 0.0);

    bool seen = false;
    answerNextPrompt(QStringLiteral("Cancel"), &seen);
    QVERIFY(!dialog.draftSession()->apply());
    QVERIFY(seen);
    QCOMPARE(tss->rainConc(), 0.0);
    QVERIFY(dialog.draftSession()->hasDraft());

    seen = false;
    answerNextPrompt(QStringLiteral("Discard Mine and Reload"), &seen);
    QVERIFY(dialog.draftSession()->apply());
    QVERIFY(seen);
    QVERIFY(!dialog.draftSession()->hasDraft());
    QCOMPARE(tss->rainConc(), 0.0);
    QCOMPARE(spinFor(dialog, QStringLiteral("GW Concentration"))->value(), 7.0);
}

void TestPollutantEditorDialog::closingWithADraftAsks()
{
    PollutantRegistry registry;
    auto *tss = registry.create(QStringLiteral("TSS"));
    PollutantEditorDialog dialog(&registry, nullptr);
    dialog.show();
    spinFor(dialog, QStringLiteral("Rain Concentration"))->setValue(9.25);
    auto *close = dialog.findChild<QPushButton *>(QStringLiteral("draftClose"));
    QVERIFY(close);

    bool seen = false;
    answerNextPrompt(QStringLiteral("Cancel"), &seen);
    close->click();
    QVERIFY(seen);
    QVERIFY(dialog.isVisible());
    QVERIFY(dialog.draftSession()->hasDraft());

    seen = false;
    answerNextPrompt(QStringLiteral("Discard"), &seen);
    dialog.close(); // title-bar close takes the same path
    QVERIFY(seen);
    QVERIFY(!dialog.isVisible());
    QCOMPARE(tss->rainConc(), 0.0);

    dialog.show();
    spinFor(dialog, QStringLiteral("Rain Concentration"))->setValue(9.25);
    seen = false;
    answerNextPrompt(QStringLiteral("Apply"), &seen);
    close->click();
    QVERIFY(seen);
    QVERIFY(!dialog.isVisible());
    QCOMPARE(tss->rainConc(), 9.25);
}

QTEST_MAIN(TestPollutantEditorDialog)
#include "test_pollutanteditordialog.moc"
