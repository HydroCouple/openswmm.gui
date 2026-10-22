/**
 * @file      test_fileopendialog.cpp
 * @brief     Persistent per-type open folders, fallbacks and real file picker outcomes.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/util/fileopendialog.h"
#include "ui/widgets/relativepathpicker.h"

#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QLineEdit>
#include <QProcess>
#include <QTest>
#include <QTimer>

using openswmmvis::ui::FileOpenDialog;

/**
 *
 * @brief Resolve the persistent evidence/settings directory without creating it.
 * @return Cleaned SWMMVIS_FILE_OPEN_TEST_OUTPUT path or source-relative workplans
 * artifact directory. An environment-relative path remains relative; no canonicalization,
 * existence check or temporary cleanup.
 *
 */
static QString outputRoot()
{
    return QDir::cleanPath(qEnvironmentVariable(
        "SWMMVIS_FILE_OPEN_TEST_OUTPUT",
        QFileInfo(QString::fromUtf8(__FILE__))
            .dir()
            .absoluteFilePath(QStringLiteral(
                "../../workplans/artifacts/file_open_directories_2026-10-04/tests"))));
}

/**
 *
 * @brief Configure process-wide INI user-scope settings under outputRoot() and fixed test app
 * identity.
 * Changes organization/application names, format and path without restoring prior values;
 * called by main before normal cases and directory-probe subprocess handling.
 *
 */
static void configureSettings()
{
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, outputRoot());
    QCoreApplication::setOrganizationName(QStringLiteral("OpenSWMMGuiTests"));
    QCoreApplication::setApplicationName(QStringLiteral("FileOpenFolders"));
}

class TestFileOpenDialog : public QObject
{
    Q_OBJECT
    /**
     *
     * @brief Resolve a fixture-relative path against outputRoot() without creating it.
     * @param path Borrowed path forwarded to QDir::filePath; may itself be absolute.
     * @return Owned joined path, without canonicalization, existence/type check or write.
     *
     */
    QString file(const QString &path) const { return QDir(outputRoot()).filePath(path); }
    /**
     *
     * @brief Create parent directories and write a15-byte picker fixture at the supplied path.
     * @param path Borrowed target filename, opened WriteOnly and truncated if it exists.
     * QVERIFY/QCOMPARE failures return from this helper after recording failure, not from
     * the caller. QFile closes on return; generated files remain in the evidence directory.
     *
     */
    void createFile(const QString &path)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile fixture(path);
        QVERIFY(fixture.open(QIODevice::WriteOnly));
        QCOMPARE(fixture.write("picker fixture\n"), qint64(15));
    }

private slots:
    /**
     *
     * @brief Create persistent vector/raster/project/series placeholder files for picker
     * selection tests.
     * Creates the output root and five15-byte fixtures; files are selection placeholders,
     * not valid GeoPackage, raster or engine data. Existing same-name files are overwritten.
     *
     */
    void initTestCase()
    {
        QVERIFY(QDir().mkpath(outputRoot()));
        createFile(file("vectors/a.gpkg"));
        createFile(file("rasters/b.tif"));
        createFile(file("project/model.inp"));
        createFile(file("series/one.csv"));
        createFile(file("series/two.csv"));
    }
    /**
     *
     * @brief Clear and sync the isolated configured settings store before each case.
     * Does not remove fixture files or restore global settings identity/path.
     *
     */
    void init()
    {
        QSettings settings;
        settings.clear();
        settings.sync();
    }
    /**
     *
     * @brief Require vector/raster folder history to remain separate and persist into a fresh
     * process.
     * Checks project fallback for an unknown type, launches this executable in directory-probe
     * mode and requires normal zero exit within a5-second wait; does not run a file dialog.
     *
     */
    void separatesTypesAndPersistsAcrossProcesses()
    {
        FileOpenDialog::rememberSelection("vector", {file("vectors/a.gpkg")});
        FileOpenDialog::rememberSelection("raster", {file("rasters/b.tif")});
        QCOMPARE(FileOpenDialog::startDirectory("vector", file("project")), file("vectors"));
        QCOMPARE(FileOpenDialog::startDirectory("raster", file("project")), file("rasters"));
        QCOMPARE(FileOpenDialog::startDirectory("models", file("project")), file("project"));
        QProcess freshProcess;
        freshProcess.start(QCoreApplication::applicationFilePath(),
                           {"--directory-probe", "vector", file("vectors")});
        QVERIFY(freshProcess.waitForFinished(5000));
        QCOMPARE(freshProcess.exitStatus(), QProcess::NormalExit);
        QCOMPARE(freshProcess.exitCode(), 0);
    }
    /**
     *
     * @brief Require empty and invalid selections not to replace valid vector folder history.
     * Supplies empty filename, missing file and directory, then checks the original vector
     * folder remains; no modal picker is opened.
     *
     */
    void cancelOrInvalidSelectionPreservesHistory()
    {
        FileOpenDialog::rememberSelection("vector", {file("vectors/a.gpkg")});
        FileOpenDialog::rememberSelection("vector", {});
        FileOpenDialog::rememberSelection("vector",
                                          {QString(), file("missing.gpkg"), file("project")});
        QCOMPARE(FileOpenDialog::startDirectory("vector"), file("vectors"));
    }
    /**
     *
     * @brief Require missing remembered folders to fall back to existing project directory, file
     * parent or home.
     * Creates/remembers a raster, renames its directory with a millisecond suffix and checks
     * all three fallback paths. The moved evidence directory remains; no cleanup/restoration.
     *
     */
    void missingFolderFallsBack()
    {
        createFile(file("removed-folder/a.tif"));
        FileOpenDialog::rememberSelection("raster", {file("removed-folder/a.tif")});
        const QString moved =
            file("removed-folder-moved-%1").arg(QDateTime::currentMSecsSinceEpoch());
        QVERIFY(QDir().rename(file("removed-folder"), moved));
        QCOMPARE(FileOpenDialog::startDirectory("raster", file("project")), file("project"));
        QCOMPARE(FileOpenDialog::startDirectory("raster", file("project/new.inp")),
                 file("project"));
        QCOMPARE(FileOpenDialog::startDirectory("raster", file("no-folder/invalid/file.inp")),
                 QDir::homePath());
    }
    /**
     *
     * @brief Require remembered folder history to survive removal of its selected file when the
     * folder still exists.
     * Renames the fixture file with a timestamp suffix and checks existing-folder selection;
     * leaves renamed evidence on disk.
     *
     */
    void missingFileRetainsExistingFolder()
    {
        createFile(file("existing-folder/removed.csv"));
        FileOpenDialog::rememberSelection("timeseries", {file("existing-folder/removed.csv")});
        const QString moved =
            file("existing-folder/removed-%1.csv").arg(QDateTime::currentMSecsSinceEpoch());
        QVERIFY(QFile::rename(file("existing-folder/removed.csv"), moved));
        QCOMPARE(FileOpenDialog::startDirectory("timeseries", file("project")),
                 file("existing-folder"));
    }
    /**
     *
     * @brief Require a mixed list of valid and missing paths to remember the last valid file's
     * directory.
     * Selects series then vector then missing file; checks vector history without opening
     * a picker or asserting source file contents.
     *
     */
    void multipleSelectionUsesLastValidFolder()
    {
        FileOpenDialog::rememberSelection(
            "delimited",
            {file("series/one.csv"), file("vectors/a.gpkg"), file("does-not-exist.csv")});
        QCOMPARE(FileOpenDialog::startDirectory("delimited"), file("vectors"));
    }
    /**
     *
     * @brief Pin canonical type keys for common extensions and normalized custom-filter identity.
     * Checks uppercase, reordered/duplicate extensions and translated labels share keys,
     * while differing single extensions differ; no path lookup or modal dialog.
     *
     */
    void filterKeysIgnoreLabelsAndOrder()
    {
        QCOMPARE(FileOpenDialog::typeForFilter("Rasters (*.tiff)"), QStringLiteral("raster"));
        QCOMPARE(FileOpenDialog::typeForFilter("Results (*.OUT)"), QStringLiteral("results-1d"));
        QCOMPARE(FileOpenDialog::typeForFilter("Mesh (*.2dm)"), QStringLiteral("mesh"));
        QCOMPARE(FileOpenDialog::typeForFilter("Series (*.tsv *.csv)"),
                 QStringLiteral("timeseries"));
        QCOMPARE(FileOpenDialog::typeForFilter("Layer (*.geojson)"), QStringLiteral("vector"));
        QCOMPARE(FileOpenDialog::typeForFilter("Styles (*.swmm-style.json *.qml)"),
                 QStringLiteral("styles"));
        QCOMPARE(FileOpenDialog::typeForFilter("Plugin (*.FOO *.bar);;All files (*)"),
                 FileOpenDialog::typeForFilter("Données (*.bar *.foo *.foo);;Tout (*)"));
        QVERIFY(FileOpenDialog::typeForFilter("Plugin (*.foo)") !=
                FileOpenDialog::typeForFilter("Plugin (*.bar)"));
    }
    /**
     *
     * @brief Require a real nonnative single-file picker to start in history and remember the
     * accepted file's folder.
     * Queued callback borrows live local state during modal execution, records directory,
     * selects the raster placeholder and queues accept. Checks chosen path and changed
     * vector history; a missing modal picker causes callback return without dismissing it.
     *
     */
    void singlePickerStartsAtHistoryAndRemembersAcceptedFile()
    {
        FileOpenDialog::rememberSelection("vector", {file("vectors/a.gpkg")});
        QString observed;
        QTimer::singleShot(0, this, [&] {
            auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (!picker)
                return;
            observed = picker->directory().absolutePath();
            picker->selectFile(file("rasters/b.tif"));
            QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection);
        });
        const QString chosen = FileOpenDialog::getOpenFileName(
            "vector", nullptr, "Test accepted selection", file("project"), "All files (*)", nullptr,
            QFileDialog::DontUseNativeDialog);
        QCOMPARE(observed, file("vectors"));
        QCOMPARE(chosen, file("rasters/b.tif"));
        QCOMPARE(FileOpenDialog::startDirectory("vector"), file("rasters"));
    }
    /**
     *
     * @brief Require a nonnative multi-file picker to return two named CSV fixtures and
     * cancellation to retain history.
     * Queued callbacks run during modal execution; the first sets fileNameEdit and accepts,
     * rejecting if that control is absent. Checks initial directory, exact returned order,
     * empty canceled single selection and retained series directory.
     *
     */
    void multiplePickerAndCancellation()
    {
        FileOpenDialog::rememberSelection("timeseries", {file("series/one.csv")});
        QString observed;
        QTimer::singleShot(0, this, [&] {
            auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (!picker)
                return;
            observed = picker->directory().absolutePath();
            auto *names = picker->findChild<QLineEdit *>("fileNameEdit");
            if (!names) {
                picker->reject();
                return;
            }
            names->setText(QStringLiteral("\"one.csv\" \"two.csv\""));
            QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection);
        });
        const QStringList chosen = FileOpenDialog::getOpenFileNames(
            "timeseries", nullptr, "Test multiple selection", file("project"), "CSV (*.csv)",
            nullptr, QFileDialog::DontUseNativeDialog);
        QCOMPARE(observed, file("series"));
        QCOMPARE(chosen, QStringList({file("series/one.csv"), file("series/two.csv")}));
        QTimer::singleShot(0, this, [&] {
            if (auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()))
                picker->reject();
        });
        QVERIFY(FileOpenDialog::getOpenFileName("timeseries", nullptr, "Test cancellation",
                                                file("project"), "CSV (*.csv)", nullptr,
                                                QFileDialog::DontUseNativeDialog)
                    .isEmpty());
        QCOMPARE(FileOpenDialog::startDirectory("timeseries"), file("series"));
    }
    /**
     *
     * @brief Require shared open browsing to use type history and display an accepted external
     * file relative to the project.
     * Checks absolute selected path and ../series/two.csv display; then canceled AcceptSave
     * browsing must start at project rather than history and leave history unchanged.
     * Callbacks borrow locals during modal execution; no new saved file or overwrite occurs.
     *
     */
    void sharedPathPickerRetainsRelativePathsAndSaveBehavior()
    {
        FileOpenDialog::rememberSelection("timeseries", {file("series/one.csv")});
        openswmmvis::ui::RelativePathPicker editor;
        editor.setProjectAnchor(file("project"));
        editor.setPath(QStringLiteral("new.csv"));
        editor.setFileFilter(QStringLiteral("CSV (*.csv)"));
        QString observed;
        QTimer::singleShot(0, this, [&] {
            if (auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                observed = picker->directory().absolutePath();
                picker->selectFile(file("series/two.csv"));
                QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection);
            }
        });
        QVERIFY(QMetaObject::invokeMethod(&editor, "onBrowseClicked", Qt::DirectConnection));
        QCOMPARE(observed, file("series"));
        QCOMPARE(editor.absolutePath(), file("series/two.csv"));
        QCOMPARE(editor.displayPath(), QStringLiteral("../series/two.csv"));
        editor.setPath(QStringLiteral("new.csv"));
        editor.setAcceptMode(QFileDialog::AcceptSave);
        QTimer::singleShot(0, this, [&] {
            if (auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                observed = picker->directory().absolutePath();
                picker->reject();
            }
        });
        QVERIFY(QMetaObject::invokeMethod(&editor, "onBrowseClicked", Qt::DirectConnection));
        QCOMPARE(observed, file("project"));
        QCOMPARE(FileOpenDialog::startDirectory("timeseries"), file("series"));
    }
};

/**
 *
 * @brief Run the Qt file-picker suite or a settings-only directory probe in a fresh GUI process.
 * @param argc Process argument count passed through QApplication/QtTest.
 * @param argv Borrowed process argument vector; QApplication may rewrite argc/argv.
 * @return Probe0 for matching remembered folder or1 for mismatch when exactly four
 * arguments select --directory-probe; otherwise QtTest result. Disables native dialogs
 * and configures persistent test settings before either path; app lifetime is scoped.
 *
 */
int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    configureSettings();
    const QStringList args = app.arguments();
    if (args.size() == 4 && args[1] == QStringLiteral("--directory-probe"))
        return FileOpenDialog::startDirectory(args[2]) == args[3] ? 0 : 1;
    TestFileOpenDialog test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_fileopendialog.moc"
