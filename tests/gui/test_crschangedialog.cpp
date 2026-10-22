/**
 * @file      test_crschangedialog.cpp
 * @brief     Slice C QtTest: CRSChangeDialog choice / disable semantics.
 *
 * Assignment keeps coordinates intact. A Local source permits assignment
 * only, because it has no geographic transform.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/dialogs/crschangedialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QTest>

#include "dialog_a11y_checks.h"

class TestCRSChangeDialog : public QObject
{
    Q_OBJECT
private slots:
    void a11yInvariantsHold();
    void defaultIsCancelBeforeShown();
    void clickingApplyWithReprojectChosen();
    void clickingApplyWithRenderOnlyChosen();
    void clickingCancelKeepsCancel();
    void clickingApplyWithAssignChosen();
    void localSourceAllowsOnlyAssignment();
    void summaryStringsAppearInLabel();
};

namespace
{

// Find the OK ("Apply") button on the dialog's button box.
QPushButton *applyButton(QDialog *dlg)
{
    auto *bb = dlg->findChild<QDialogButtonBox *>();
    return bb ? bb->button(QDialogButtonBox::Ok) : nullptr;
}

QPushButton *cancelButton(QDialog *dlg)
{
    auto *bb = dlg->findChild<QDialogButtonBox *>();
    return bb ? bb->button(QDialogButtonBox::Cancel) : nullptr;
}

} // namespace

void TestCRSChangeDialog::defaultIsCancelBeforeShown()
{
    CRSChangeDialog dlg(QStringLiteral("EPSG:4326"), QStringLiteral("EPSG:3857"));
    QCOMPARE(dlg.choice(), CRSChangeDialog::Cancel);
}

void TestCRSChangeDialog::clickingApplyWithReprojectChosen()
{
    CRSChangeDialog dlg(QStringLiteral("EPSG:4326"), QStringLiteral("EPSG:3857"));
    // Reproject is the default selection; just click Apply.
    QPushButton *apply = applyButton(&dlg);
    QVERIFY(apply);
    apply->click();
    QCOMPARE(dlg.choice(), CRSChangeDialog::Reproject);
    QCOMPARE(dlg.result(), int(QDialog::Accepted));
}

void TestCRSChangeDialog::clickingApplyWithRenderOnlyChosen()
{
    CRSChangeDialog dlg(QStringLiteral("EPSG:4326"), QStringLiteral("EPSG:3857"));
    // Pick the display-only radio.
    auto radios = dlg.findChildren<QRadioButton *>();
    QCOMPARE(radios.size(), 3);
    radios[2]->setChecked(true);
    applyButton(&dlg)->click();
    QCOMPARE(dlg.choice(), CRSChangeDialog::RenderOnly);
}

void TestCRSChangeDialog::clickingCancelKeepsCancel()
{
    CRSChangeDialog dlg(QStringLiteral("EPSG:4326"), QStringLiteral("EPSG:3857"));
    cancelButton(&dlg)->click();
    QCOMPARE(dlg.choice(), CRSChangeDialog::Cancel);
    QCOMPARE(dlg.result(), int(QDialog::Rejected));
}

void TestCRSChangeDialog::localSourceAllowsOnlyAssignment()
{
    CRSChangeDialog dlg(QStringLiteral("Local"), QStringLiteral("EPSG:3857"),
                        /*sourceIsLocal*/ true);
    auto radios = dlg.findChildren<QRadioButton *>();
    QCOMPARE(radios.size(), 3);
    QVERIFY(radios[0]->isEnabled());
    QVERIFY(!radios[1]->isEnabled());
    QVERIFY(!radios[2]->isEnabled());
    QVERIFY(radios[0]->isChecked());
    applyButton(&dlg)->click();
    QCOMPARE(dlg.choice(), CRSChangeDialog::Assign);
}

void TestCRSChangeDialog::clickingApplyWithAssignChosen()
{
    CRSChangeDialog dlg(QStringLiteral("EPSG:6595"), QStringLiteral("EPSG:4326"));
    auto radios = dlg.findChildren<QRadioButton *>();
    QCOMPARE(radios.size(), 3);
    radios[0]->setChecked(true);
    applyButton(&dlg)->click();
    QCOMPARE(dlg.choice(), CRSChangeDialog::Assign);
}

void TestCRSChangeDialog::summaryStringsAppearInLabel()
{
    // Sanity: the From/To strings are placed in a visible QLabel so the
    // user can see what they're about to do.
    CRSChangeDialog dlg(QStringLiteral("EPSG:6595"), QStringLiteral("EPSG:4326"));
    bool found = false;
    for (auto *lbl : dlg.findChildren<QLabel *>()) {
        const QString t = lbl->text();
        if (t.contains("EPSG:6595") && t.contains("EPSG:4326")) {
            found = true;
            break;
        }
    }
    QVERIFY(found);
}

void TestCRSChangeDialog::a11yInvariantsHold()
{
    CRSChangeDialog dlg(QStringLiteral("EPSG:4326"), QStringLiteral("EPSG:3857"));
    swmmvis_test::assertDialogA11y(&dlg);
}

QTEST_MAIN(TestCRSChangeDialog)
#include "test_crschangedialog.moc"
