/*!
 * \file test_fileopendialog.cpp
 * \brief Persistent per-type open folders, fallbacks and real file picker outcomes.
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

static QString outputRoot()
{
    return QDir::cleanPath(qEnvironmentVariable("SWMMVIS_FILE_OPEN_TEST_OUTPUT",
        QFileInfo(QString::fromUtf8(__FILE__)).dir().absoluteFilePath(
            QStringLiteral("../../workplans/artifacts/file_open_directories_2026-10-04/tests"))));
}

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
    QString file(const QString &path) const { return QDir(outputRoot()).filePath(path); }
    void createFile(const QString &path)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile fixture(path);
        QVERIFY(fixture.open(QIODevice::WriteOnly));
        QCOMPARE(fixture.write("picker fixture\n"), qint64(15));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(QDir().mkpath(outputRoot()));
        createFile(file("vectors/a.gpkg"));
        createFile(file("rasters/b.tif"));
        createFile(file("project/model.inp"));
        createFile(file("series/one.csv"));
        createFile(file("series/two.csv"));
    }
    void init()
    {
        QSettings settings;
        settings.clear();
        settings.sync();
    }
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
    void cancelOrInvalidSelectionPreservesHistory()
    {
        FileOpenDialog::rememberSelection("vector", {file("vectors/a.gpkg")});
        FileOpenDialog::rememberSelection("vector", {});
        FileOpenDialog::rememberSelection("vector", {QString(), file("missing.gpkg"), file("project")});
        QCOMPARE(FileOpenDialog::startDirectory("vector"), file("vectors"));
    }
    void missingFolderFallsBack()
    {
        createFile(file("removed-folder/a.tif"));
        FileOpenDialog::rememberSelection("raster", {file("removed-folder/a.tif")});
        const QString moved = file("removed-folder-moved-%1").arg(QDateTime::currentMSecsSinceEpoch());
        QVERIFY(QDir().rename(file("removed-folder"), moved));
        QCOMPARE(FileOpenDialog::startDirectory("raster", file("project")), file("project"));
        QCOMPARE(FileOpenDialog::startDirectory("raster", file("project/new.inp")), file("project"));
        QCOMPARE(FileOpenDialog::startDirectory("raster", file("no-folder/invalid/file.inp")), QDir::homePath());
    }
    void missingFileRetainsExistingFolder()
    {
        createFile(file("existing-folder/removed.csv"));
        FileOpenDialog::rememberSelection("timeseries", {file("existing-folder/removed.csv")});
        const QString moved = file("existing-folder/removed-%1.csv").arg(QDateTime::currentMSecsSinceEpoch());
        QVERIFY(QFile::rename(file("existing-folder/removed.csv"), moved));
        QCOMPARE(FileOpenDialog::startDirectory("timeseries", file("project")), file("existing-folder"));
    }
    void multipleSelectionUsesLastValidFolder()
    {
        FileOpenDialog::rememberSelection("delimited",
            {file("series/one.csv"), file("vectors/a.gpkg"), file("does-not-exist.csv")});
        QCOMPARE(FileOpenDialog::startDirectory("delimited"), file("vectors"));
    }
    void filterKeysIgnoreLabelsAndOrder()
    {
        QCOMPARE(FileOpenDialog::typeForFilter("Rasters (*.tiff)"), QStringLiteral("raster"));
        QCOMPARE(FileOpenDialog::typeForFilter("Results (*.OUT)"), QStringLiteral("results-1d"));
        QCOMPARE(FileOpenDialog::typeForFilter("Mesh (*.2dm)"), QStringLiteral("mesh"));
        QCOMPARE(FileOpenDialog::typeForFilter("Series (*.tsv *.csv)"), QStringLiteral("timeseries"));
        QCOMPARE(FileOpenDialog::typeForFilter("Layer (*.geojson)"), QStringLiteral("vector"));
        QCOMPARE(FileOpenDialog::typeForFilter("Styles (*.swmm-style.json *.qml)"), QStringLiteral("styles"));
        QCOMPARE(FileOpenDialog::typeForFilter("Plugin (*.FOO *.bar);;All files (*)"),
                 FileOpenDialog::typeForFilter("Données (*.bar *.foo *.foo);;Tout (*)"));
        QVERIFY(FileOpenDialog::typeForFilter("Plugin (*.foo)")
             != FileOpenDialog::typeForFilter("Plugin (*.bar)"));
    }
    void singlePickerStartsAtHistoryAndRemembersAcceptedFile()
    {
        FileOpenDialog::rememberSelection("vector", {file("vectors/a.gpkg")});
        QString observed;
        QTimer::singleShot(0, this, [&] {
            auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (!picker) return;
            observed = picker->directory().absolutePath();
            picker->selectFile(file("rasters/b.tif"));
            QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection);
        });
        const QString chosen = FileOpenDialog::getOpenFileName("vector", nullptr,
            "Test accepted selection", file("project"), "All files (*)", nullptr,
            QFileDialog::DontUseNativeDialog);
        QCOMPARE(observed, file("vectors"));
        QCOMPARE(chosen, file("rasters/b.tif"));
        QCOMPARE(FileOpenDialog::startDirectory("vector"), file("rasters"));
    }
    void multiplePickerAndCancellation()
    {
        FileOpenDialog::rememberSelection("timeseries", {file("series/one.csv")});
        QString observed;
        QTimer::singleShot(0, this, [&] {
            auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (!picker) return;
            observed = picker->directory().absolutePath();
            auto *names = picker->findChild<QLineEdit *>("fileNameEdit");
            if (!names) { picker->reject(); return; }
            names->setText(QStringLiteral("\"one.csv\" \"two.csv\""));
            QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection);
        });
        const QStringList chosen = FileOpenDialog::getOpenFileNames("timeseries", nullptr,
            "Test multiple selection", file("project"), "CSV (*.csv)", nullptr,
            QFileDialog::DontUseNativeDialog);
        QCOMPARE(observed, file("series"));
        QCOMPARE(chosen, QStringList({file("series/one.csv"), file("series/two.csv")}));
        QTimer::singleShot(0, this, [&] {
            if (auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()))
                picker->reject();
        });
        QVERIFY(FileOpenDialog::getOpenFileName("timeseries", nullptr, "Test cancellation",
            file("project"), "CSV (*.csv)", nullptr, QFileDialog::DontUseNativeDialog).isEmpty());
        QCOMPARE(FileOpenDialog::startDirectory("timeseries"), file("series"));
    }
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
