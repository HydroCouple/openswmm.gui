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
/**
 *
 * @brief Find the first button box's Ok action used as Apply.
 * @param dlg Borrowed non-null live dialog.
 * @return Borrowed Ok button, or null when box/action is absent. No click or ownership
 * transfer; caller retains dialog lifetime and must check before dereferencing.
 *
 */
QPushButton *applyButton(QDialog *dlg)
{
    auto *bb = dlg->findChild<QDialogButtonBox *>();
    return bb ? bb->button(QDialogButtonBox::Ok) : nullptr;
}

/**
 *
 * @brief Find the first button box's Cancel action.
 * @param dlg Borrowed non-null live dialog.
 * @return Borrowed Cancel button or null for absent box/action; no click or retained ref.
 *
 */
QPushButton *cancelButton(QDialog *dlg)
{
    auto *bb = dlg->findChild<QDialogButtonBox *>();
    return bb ? bb->button(QDialogButtonBox::Cancel) : nullptr;
}

} // namespace

/**
 *
 * @brief Require choice to remain Cancel immediately after construction even with reproject radio
 * defaulted.
 * Does not show/accept the dialog or transform any coordinates.
 *
 */
void TestCRSChangeDialog::defaultIsCancelBeforeShown()
{
    CRSChangeDialog dlg(QStringLiteral("EPSG:4326"), QStringLiteral("EPSG:3857"));
    QCOMPARE(dlg.choice(), CRSChangeDialog::Cancel);
}

/**
 *
 * @brief Require clicking Apply with the default nonlocal selection to return Reproject and
 * Accepted.
 * Uses EPSG4326/3857 labels only; no actual CRS transformation or model edit.
 *
 */
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

/**
 *
 * @brief Require selecting the third radio and clicking Apply to return RenderOnly.
 * Checks radio count three but not dialog result explicitly; no map rendering occurs.
 *
 */
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

/**
 *
 * @brief Require Cancel to retain Cancel choice and return Rejected.
 * No modal execution, engine mutation or settings persistence is involved.
 *
 */
void TestCRSChangeDialog::clickingCancelKeepsCancel()
{
    CRSChangeDialog dlg(QStringLiteral("EPSG:4326"), QStringLiteral("EPSG:3857"));
    cancelButton(&dlg)->click();
    QCOMPARE(dlg.choice(), CRSChangeDialog::Cancel);
    QCOMPARE(dlg.result(), int(QDialog::Rejected));
}

/**
 *
 * @brief Require Local source mode to enable/select only assignment and return Assign on Apply.
 * Checks reprojection/render-only radios disabled; no coordinate or CRS object changes.
 *
 */
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

/**
 *
 * @brief Require selecting the first radio and clicking Apply to return Assign.
 * Uses EPSG6595/4326 labels without creating or validating those CRS definitions.
 *
 */
void TestCRSChangeDialog::clickingApplyWithAssignChosen()
{
    CRSChangeDialog dlg(QStringLiteral("EPSG:6595"), QStringLiteral("EPSG:4326"));
    auto radios = dlg.findChildren<QRadioButton *>();
    QCOMPARE(radios.size(), 3);
    radios[0]->setChecked(true);
    applyButton(&dlg)->click();
    QCOMPARE(dlg.choice(), CRSChangeDialog::Assign);
}

/**
 *
 * @brief Require at least one descendant label to contain both supplied source/target CRS
 * strings.
 * Checks text presence, not actual widget exposure, visibility or pixel layout.
 *
 */
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

/**
 *
 * @brief Run shared accessibility assertions on a constructed nonlocal CRS-change dialog.
 * No choice is committed or transformed.
 *
 */
void TestCRSChangeDialog::a11yInvariantsHold()
{
    CRSChangeDialog dlg(QStringLiteral("EPSG:4326"), QStringLiteral("EPSG:3857"));
    swmmvis_test::assertDialogA11y(&dlg);
}

QTEST_MAIN(TestCRSChangeDialog)
#include "test_crschangedialog.moc"
