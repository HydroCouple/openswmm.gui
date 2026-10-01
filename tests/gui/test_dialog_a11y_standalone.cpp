// UI redesign iteration 2 (D4/D6) — dialog accessibility audit over the
// cheaply-constructible dialogs (default-constructible with a parent
// only; WMS/WMTS connection dialogs excluded — their layer headers
// drag GDAL + QtNetwork). Layer- or project-bound dialogs stay covered by their own test
// targets and the mechanical sweeps; this target guards the sweep
// invariants (mnemonic uniqueness, textless button naming, persistence
// naming) where construction is free.
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

namespace {
// A drawn glyph/swatch is meaningful visually without a QIcon or button text.
class PaintedButton : public QPushButton {
public:
    using QPushButton::QPushButton;
protected:
    void paintEvent(QPaintEvent *event) override {
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

void TestDialogA11yStandalone::initTestCase()
{
    QVERIFY(mSettingsDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(QStringLiteral("dialoga11y-test"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       mSettingsDir.path());
}

void TestDialogA11yStandalone::aboutDialog()
{
    AboutDialog dlg;
    assertDialogA11y(&dlg);
}

void TestDialogA11yStandalone::pluginsDialog()
{
    PluginsDialog dlg;
    assertDialogA11y(&dlg);
}

void TestDialogA11yStandalone::licenseAgreementDialog()
{
    LicenseAgreementDialog dlg;
    assertDialogA11y(&dlg);
}


void TestDialogA11yStandalone::unnamedPaintedButtonIsDetected()
{
    QDialog dialog;
    PaintedButton button(&dialog);
    button.setObjectName("unnamedPaintedControl");
    button.setToolTip("Choose a colour");
    auto *accessible = QAccessible::queryAccessibleInterface(&button);
    QVERIFY(accessible); QVERIFY(accessible->text(QAccessible::Name).trimmed().isEmpty());
    QEXPECT_FAIL("", "The shared assertion must reject a painted button without an exposed name.", Continue);
    assertIconButtonsNamed(&dialog);
}

void TestDialogA11yStandalone::tooltipOnlyIconButtonIsDetected()
{
    QDialog dialog;
    QPushButton button(&dialog);
    button.setObjectName("tooltipOnlyControl");
    QPixmap icon(8, 8); icon.fill(Qt::blue); button.setIcon(QIcon(icon));
    button.setToolTip("Choose a colour");
    auto *accessible = QAccessible::queryAccessibleInterface(&button);
    QVERIFY(accessible); QVERIFY(accessible->text(QAccessible::Name).trimmed().isEmpty());
    QEXPECT_FAIL("", "A tooltip is not a substitute for the accessible Name role.", Continue);
    assertIconButtonsNamed(&dialog);
}

void TestDialogA11yStandalone::namedColorButtonPasses_data()
{
    QTest::addColumn<bool>("buddyName");
    QTest::newRow("explicit-context") << false;
    QTest::newRow("label-buddy-context") << true;
}

void TestDialogA11yStandalone::namedColorButtonPasses()
{
    QFETCH(bool, buddyName);
    QDialog dialog;
    auto *layout = new QVBoxLayout(&dialog);
    auto *button = new openswmmvis::ui::ColorButton(QColor(30, 60, 90), &dialog);
    if (buddyName) {
        auto *label = new QLabel("&Corridor colour", &dialog);
        label->setBuddy(button); layout->addWidget(label);
    } else button->setAccessibleName("Corridor colour");
    layout->addWidget(button);
    auto *accessible = QAccessible::queryAccessibleInterface(button);
    QVERIFY(accessible); QCOMPARE(accessible->text(QAccessible::Name), QString("Corridor colour"));
    QVERIFY(button->text().isEmpty()); QVERIFY(button->icon().isNull());
    assertIconButtonsNamed(&dialog);
    QCOMPARE(accessible->text(QAccessible::Name), QString("Corridor colour"));
}

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
