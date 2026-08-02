// UI redesign iteration 2 (D4/D6) — dialog accessibility audit over the
// cheaply-constructible dialogs (default-constructible with a parent
// only; WMS/WMTS connection dialogs excluded — their layer headers
// drag GDAL + QtNetwork). Layer- or project-bound dialogs stay covered by their own test
// targets and the mechanical sweeps; this target guards the sweep
// invariants (mnemonic uniqueness, icon-only button naming, persistence
// naming) where construction is free.
#include <QtTest/QtTest>

#include <QSettings>
#include <QTemporaryDir>

#include "dialog_a11y_checks.h"

#include "ui/dialogs/aboutdialog.h"
#include "ui/dialogs/licenseagreementdialog.h"
#include "ui/dialogs/pluginsdialog.h"

using namespace swmmvis_test;

class TestDialogA11yStandalone : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void aboutDialog();
    void pluginsDialog();
    void licenseAgreementDialog();

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

QTEST_MAIN(TestDialogA11yStandalone)
#include "test_dialog_a11y_standalone.moc"
