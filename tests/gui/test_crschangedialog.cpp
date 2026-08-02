/*!
 * \file   test_crschangedialog.cpp
 * \brief  Slice C QtTest: CRSChangeDialog choice / disable semantics.
 *
 * Phase 0.7 contract: the dialog presents three outcomes (Reproject,
 * RenderOnly, Cancel) and disables Re-render when the source CRS is Local.
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
    void localSourceDisablesRenderOnly();
    void summaryStringsAppearInLabel();
};

namespace {

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
    CRSChangeDialog dlg(QStringLiteral("EPSG:4326"),
                        QStringLiteral("EPSG:3857"));
    QCOMPARE(dlg.choice(), CRSChangeDialog::Cancel);
}

void TestCRSChangeDialog::clickingApplyWithReprojectChosen()
{
    CRSChangeDialog dlg(QStringLiteral("EPSG:4326"),
                        QStringLiteral("EPSG:3857"));
    // Reproject is the default selection; just click Apply.
    QPushButton *apply = applyButton(&dlg);
    QVERIFY(apply);
    apply->click();
    QCOMPARE(dlg.choice(), CRSChangeDialog::Reproject);
    QCOMPARE(dlg.result(), int(QDialog::Accepted));
}

void TestCRSChangeDialog::clickingApplyWithRenderOnlyChosen()
{
    CRSChangeDialog dlg(QStringLiteral("EPSG:4326"),
                        QStringLiteral("EPSG:3857"));
    // Pick the second radio.
    auto radios = dlg.findChildren<QRadioButton *>();
    QCOMPARE(radios.size(), 2);
    radios[1]->setChecked(true);
    applyButton(&dlg)->click();
    QCOMPARE(dlg.choice(), CRSChangeDialog::RenderOnly);
}

void TestCRSChangeDialog::clickingCancelKeepsCancel()
{
    CRSChangeDialog dlg(QStringLiteral("EPSG:4326"),
                        QStringLiteral("EPSG:3857"));
    cancelButton(&dlg)->click();
    QCOMPARE(dlg.choice(), CRSChangeDialog::Cancel);
    QCOMPARE(dlg.result(), int(QDialog::Rejected));
}

void TestCRSChangeDialog::localSourceDisablesRenderOnly()
{
    CRSChangeDialog dlg(QStringLiteral("Local"),
                        QStringLiteral("EPSG:3857"),
                        /*sourceIsLocal*/ true);
    auto radios = dlg.findChildren<QRadioButton *>();
    QCOMPARE(radios.size(), 2);
    QVERIFY( radios[0]->isEnabled());   // Reproject still on
    QVERIFY(!radios[1]->isEnabled());   // Re-render disabled
    QVERIFY( radios[0]->isChecked());   // Reproject preselected
    applyButton(&dlg)->click();
    QCOMPARE(dlg.choice(), CRSChangeDialog::Reproject);
}

void TestCRSChangeDialog::summaryStringsAppearInLabel()
{
    // Sanity: the From/To strings are placed in a visible QLabel so the
    // user can see what they're about to do.
    CRSChangeDialog dlg(QStringLiteral("EPSG:6595"),
                        QStringLiteral("EPSG:4326"));
    bool found = false;
    for (auto *lbl : dlg.findChildren<QLabel *>())
    {
        const QString t = lbl->text();
        if (t.contains("EPSG:6595") && t.contains("EPSG:4326"))
        {
            found = true;
            break;
        }
    }
    QVERIFY(found);
}

void TestCRSChangeDialog::a11yInvariantsHold()
{
    CRSChangeDialog dlg(QStringLiteral("EPSG:4326"),
                        QStringLiteral("EPSG:3857"));
    swmmvis_test::assertDialogA11y(&dlg);
}

QTEST_MAIN(TestCRSChangeDialog)
#include "test_crschangedialog.moc"
