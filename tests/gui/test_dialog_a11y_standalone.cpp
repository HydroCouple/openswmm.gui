// UI redesign iteration 2 (D4/D6) — dialog accessibility audit over the
// cheaply-constructible dialogs (default-constructible with a parent
// only; WMS/WMTS connection dialogs excluded — their layer headers
// drag GDAL + QtNetwork). Layer- or project-bound dialogs stay covered by their own test
// targets and the mechanical sweeps; this target guards the sweep
// invariants (mnemonic uniqueness, textless button naming, persistence
// naming) where construction is free.

/**
 * @file      test_dialog_a11y_standalone.cpp
 * @brief     Tests for dialog a11y standalone.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QtTest/QtTest>

#include <QSettings>
#include <QAccessible>
#include <QComboBox>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTemporaryDir>

#include "dialog_a11y_checks.h"
#include "ui/widgets/colorbutton.h"

#include "ui/dialogs/aboutdialog.h"
#include "ui/dialogs/licenseagreementdialog.h"
#include "ui/dialogs/pluginsdialog.h"

using namespace swmmvis_test;

namespace
{
// A drawn glyph/swatch is meaningful visually without a QIcon or button text.
class PaintedButton : public QPushButton
{
public:
    using QPushButton::QPushButton;

protected:
    /**
     *
     * @brief Paint the normal button then overlay a blue interior swatch without text or QIcon.
     * @param event Borrowed paint event forwarded to QPushButton; valid for this callback.
     * Uses local painter lifetime and does not assign any accessible name itself.
     *
     */
    void paintEvent(QPaintEvent *event) override
    {
        QPushButton::paintEvent(event);
        QPainter painter(this);
        painter.fillRect(rect().adjusted(4, 4, -4, -4), Qt::blue);
    }
};
}

class TestDialogA11yStandalone : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void aboutDialog();
    void pluginsDialog();
    void licenseAgreementDialog();
    void unnamedPaintedButtonIsDetected();
    void tooltipOnlyIconButtonIsDetected();
    void namedColorButtonPasses_data();
    void namedColorButtonPasses();
    void nativeComboNameIsUntouched();

private:
    QTemporaryDir mSettingsDir;
};

/**
 *
 * @brief Isolate process-wide settings identity, INI format and user path in a member-owned
 * temporary directory.
 * Requires valid directory retained for the suite; does not restore prior global values.
 *
 */
void TestDialogA11yStandalone::initTestCase()
{
    QVERIFY(mSettingsDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(QStringLiteral("dialoga11y-test"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, mSettingsDir.path());
}

/**
 *
 * @brief Run shared dialog accessibility assertions on a local About dialog.
 * No modal acceptance, native engine or screenshot is involved.
 *
 */
void TestDialogA11yStandalone::aboutDialog()
{
    AboutDialog dlg;
    assertDialogA11y(&dlg);
}

/**
 *
 * @brief Run shared dialog accessibility assertions on a local Plugins dialog.
 * Does not install/uninstall a plugin or exercise external services.
 *
 */
void TestDialogA11yStandalone::pluginsDialog()
{
    PluginsDialog dlg;
    assertDialogA11y(&dlg);
}

/**
 *
 * @brief Run shared accessibility assertions on a local license-agreement dialog.
 * Does not accept/reject a modal license flow or modify licensing preferences explicitly.
 *
 */
void TestDialogA11yStandalone::licenseAgreementDialog()
{
    LicenseAgreementDialog dlg;
    assertDialogA11y(&dlg);
}

/**
 *
 * @brief Require the shared button-name assertion to detect a textless painted swatch despite its
 * tooltip.
 * Checks Qt's accessible Name is empty, then registers an expected failure before the
 * shared assertion. Does not render/grab the button or test a screen reader.
 *
 */
void TestDialogA11yStandalone::unnamedPaintedButtonIsDetected()
{
    QDialog dialog;
    PaintedButton button(&dialog);
    button.setObjectName("unnamedPaintedControl");
    button.setToolTip("Choose a colour");
    auto *accessible = QAccessible::queryAccessibleInterface(&button);
    QVERIFY(accessible);
    QVERIFY(accessible->text(QAccessible::Name).trimmed().isEmpty());
    QEXPECT_FAIL("", "The shared assertion must reject a painted button without an exposed name.",
                 Continue);
    assertIconButtonsNamed(&dialog);
}

/**
 *
 * @brief Require a tooltip-only icon button to fail the shared accessible-name assertion as
 * expected.
 * Checks Qt's Name role is empty despite icon/tooltip; the expected failure is deliberate
 * regression coverage, not an unhandled suite error.
 *
 */
void TestDialogA11yStandalone::tooltipOnlyIconButtonIsDetected()
{
    QDialog dialog;
    QPushButton button(&dialog);
    button.setObjectName("tooltipOnlyControl");
    QPixmap icon(8, 8);
    icon.fill(Qt::blue);
    button.setIcon(QIcon(icon));
    button.setToolTip("Choose a colour");
    auto *accessible = QAccessible::queryAccessibleInterface(&button);
    QVERIFY(accessible);
    QVERIFY(accessible->text(QAccessible::Name).trimmed().isEmpty());
    QEXPECT_FAIL("", "A tooltip is not a substitute for the accessible Name role.", Continue);
    assertIconButtonsNamed(&dialog);
}

/**
 *
 * @brief Register explicit accessible-name and label-buddy naming paths for a drawn ColorButton.
 * Supplies a boolean consumed by namedColorButtonPasses().
 *
 */
void TestDialogA11yStandalone::namedColorButtonPasses_data()
{
    QTest::addColumn<bool>("buddyName");
    QTest::newRow("explicit-context") << false;
    QTest::newRow("label-buddy-context") << true;
}

/**
 *
 * @brief Require a textless/iconless color swatch to expose its contextual name and pass shared
 * assertions.
 * Checks exact Corridor colour before/after validation for both explicit and mnemonic
 * buddy naming. Local dialog owns child controls; no color picker is opened.
 *
 */
void TestDialogA11yStandalone::namedColorButtonPasses()
{
    QFETCH(bool, buddyName);
    QDialog dialog;
    auto *layout = new QVBoxLayout(&dialog);
    auto *button = new openswmmvis::ui::ColorButton(QColor(30, 60, 90), &dialog);
    if (buddyName) {
        auto *label = new QLabel("&Corridor colour", &dialog);
        label->setBuddy(button);
        layout->addWidget(label);
    } else
        button->setAccessibleName("Corridor colour");
    layout->addWidget(button);
    auto *accessible = QAccessible::queryAccessibleInterface(button);
    QVERIFY(accessible);
    QCOMPARE(accessible->text(QAccessible::Name), QString("Corridor colour"));
    QVERIFY(button->text().isEmpty());
    QVERIFY(button->icon().isNull());
    assertIconButtonsNamed(&dialog);
    QCOMPARE(accessible->text(QAccessible::Name), QString("Corridor colour"));
}

/**
 *
 * @brief Require shared icon-button auditing not to mutate a native combo's Name role,
 * accessibleName or selection.
 * Checks both item selections while explicit name remains Quantity; no key navigation
 * or spoken-output assertion.
 *
 */
void TestDialogA11yStandalone::nativeComboNameIsUntouched()
{
    QDialog dialog;
    QComboBox combo(&dialog);
    combo.addItems({"Depth", "Water level"});
    combo.setAccessibleName("Quantity");
    auto *accessible = QAccessible::queryAccessibleInterface(&combo);
    QVERIFY(accessible);
    for (int index = 0; index < combo.count(); ++index) {
        combo.setCurrentIndex(index);
        const QString before = accessible->text(QAccessible::Name);
        assertIconButtonsNamed(&dialog);
        QCOMPARE(accessible->text(QAccessible::Name), before);
        QCOMPARE(combo.accessibleName(), QString("Quantity"));
        QCOMPARE(combo.currentIndex(), index);
    }
}

QTEST_MAIN(TestDialogA11yStandalone)
#include "test_dialog_a11y_standalone.moc"
