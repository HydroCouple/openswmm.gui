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

void TestPollutantEditorDialog::editsPreserveUnchangedExactValues_data()
{
    QTest::addColumn<QString>("edit");
    QTest::newRow("no-op") << QStringLiteral("none");
    QTest::newRow("unrelated-checkbox") << QStringLiteral("snow");
    QTest::newRow("explicit-rain") << QStringLiteral("rain");
}

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

void TestPollutantEditorDialog::ownerClosingDuringDeleteRefusesLateConfirmation()
{
    PollutantTestOwner owner;
    PollutantRegistry registry(&owner);
    registry.create(QStringLiteral("TSS"));
    PollutantEditorDialog dialog(&registry, nullptr);
    bool promptSeen = false;
    QTimer::singleShot(0, &dialog, [&] {
        auto *prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!prompt) return;
        auto *yes = prompt->button(QMessageBox::Yes);
        if (!yes) return;
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
