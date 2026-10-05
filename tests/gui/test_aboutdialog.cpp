/*!
 * \file   test_aboutdialog.cpp
 * \brief  AboutDialog manifest parsing and embedded application license display.
 */

#include "ui/dialogs/aboutdialog.h"
#include "ui/dialogs/dialoglayoutwatcher.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QSplitterHandle>
#include <QTest>
#include <QTreeView>

class TestAboutDialog : public QObject
{
    Q_OBJECT
private slots:
    void parsesValidManifest();
    void emptyArrayIsEmpty();
    void rejectsNonArrayRoot();
    void skipsRowsWithoutName();
    void copiesAllExpectedFields();
    void applicationLicenseDisplayAndCopy();
    void restoredPanelFillsAndScrolls();
};

void TestAboutDialog::parsesValidManifest()
{
    const QByteArray json = R"([
        {"category":"X","name":"a","version":"1.0","spdx":"MIT"},
        {"category":"X","name":"b","version":"2.0","spdx":"BSD"}
    ])";
    QString err;
    auto rows = AboutDialog::parseManifest(json, &err);
    QVERIFY(err.isEmpty());
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0].name, QStringLiteral("a"));
    QCOMPARE(rows[1].name, QStringLiteral("b"));
}

void TestAboutDialog::emptyArrayIsEmpty()
{
    QString err;
    auto rows = AboutDialog::parseManifest("[]", &err);
    QVERIFY(err.isEmpty());
    QCOMPARE(rows.size(), 0);
}

void TestAboutDialog::rejectsNonArrayRoot()
{
    QString err;
    auto rows = AboutDialog::parseManifest("{\"name\":\"x\"}", &err);
    QVERIFY(rows.isEmpty());
    QVERIFY(!err.isEmpty());
}

void TestAboutDialog::skipsRowsWithoutName()
{
    const QByteArray json = R"([
        {"category":"X","version":"1.0"},
        {"category":"X","name":"valid"}
    ])";
    auto rows = AboutDialog::parseManifest(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].name, QStringLiteral("valid"));
}

void TestAboutDialog::copiesAllExpectedFields()
{
    const QByteArray json = R"([{
        "category":"Geospatial",
        "name":"GDAL",
        "version":"3.9.2",
        "role":"vector / raster IO",
        "homepage":"https://gdal.org",
        "source":"https://github.com/OSGeo/gdal",
        "spdx":"MIT",
        "provenance":"vcpkg",
        "licenseFile":":/licenses/gdal.txt"
    }])";
    auto rows = AboutDialog::parseManifest(json);
    QCOMPARE(rows.size(), 1);
    const auto &c = rows[0];
    QCOMPARE(c.category,    QStringLiteral("Geospatial"));
    QCOMPARE(c.name,        QStringLiteral("GDAL"));
    QCOMPARE(c.version,     QStringLiteral("3.9.2"));
    QCOMPARE(c.role,        QStringLiteral("vector / raster IO"));
    QCOMPARE(c.homepage,    QStringLiteral("https://gdal.org"));
    QCOMPARE(c.sourceUrl,   QStringLiteral("https://github.com/OSGeo/gdal"));
    QCOMPARE(c.spdx,        QStringLiteral("MIT"));
    QCOMPARE(c.provenance,  QStringLiteral("vcpkg"));
    QCOMPARE(c.licenseFile, QStringLiteral(":/licenses/gdal.txt"));
}

void TestAboutDialog::applicationLicenseDisplayAndCopy()
{
    const QDir sourceDir = QFileInfo(QString::fromUtf8(__FILE__)).dir();
    QFile canonicalLicense(sourceDir.filePath(QStringLiteral("../../LICENSE")));
    QVERIFY(canonicalLicense.open(QIODevice::ReadOnly));
    const QString expected = QString::fromUtf8(canonicalLicense.readAll());
    QVERIFY(expected.contains(QStringLiteral("GNU GENERAL PUBLIC LICENSE")));
    QVERIFY(expected.contains(QStringLiteral("END OF TERMS AND CONDITIONS")));

    AboutDialog dialog;
    auto *text = dialog.findChild<QPlainTextEdit *>();
    auto *tree = dialog.findChild<QTreeView *>();
    QVERIFY(text && tree);
    QCOMPARE(tree->currentIndex().data().toString(), QStringLiteral("SWMMVis GUI"));
    QCOMPARE(text->toPlainText(), expected);
    QVERIFY(text->isReadOnly());
    QVERIFY(!text->accessibleName().isEmpty());
    QCOMPARE(text->lineWrapMode(), QPlainTextEdit::WidgetWidth);

    QPushButton *copy = nullptr;
    for (auto *button : dialog.findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Copy License")) copy = button;
    QVERIFY(copy && copy->isEnabled());
    copy->click();
    QCOMPARE(QApplication::clipboard()->text(), expected);

    // Switching to a dependency and back must restore the complete GUI license.
    auto *model = tree->model();
    const QModelIndex application = tree->currentIndex();
    const QModelIndex engine = model->index(0, 0, model->index(1, 0));
    QVERIFY(engine.isValid());
    tree->setCurrentIndex(engine);
    QVERIFY(text->toPlainText() != expected);
    tree->setCurrentIndex(application);
    QCOMPARE(text->toPlainText(), expected);

    dialog.show();
    QTest::qWait(50);
    const QString outputDir = qEnvironmentVariable("SWMMVIS_ABOUT_TEST_OUTPUT");
    if (!outputDir.isEmpty()) {
        QVERIFY(QDir().mkpath(outputDir));
        QVERIFY(dialog.grab().save(QDir(outputDir).filePath(QStringLiteral("gui_license.png"))));
    }
}

void TestAboutDialog::restoredPanelFillsAndScrolls()
{
    const QDir sourceDir = QFileInfo(QString::fromUtf8(__FILE__)).dir();
    const QString outputDir = qEnvironmentVariable("SWMMVIS_ABOUT_TEST_OUTPUT",
        sourceDir.filePath(QStringLiteral("../../workplans/artifacts/gui_about_license_2026-10-04/panel")));
    QVERIFY(QDir().mkpath(outputDir));
    // Exercise the production Show filter without changing user settings.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, outputDir);
    QCoreApplication::setOrganizationName(QStringLiteral("OpenSWMMGuiTests"));
    QCoreApplication::setApplicationName(QStringLiteral("AboutPanel"));
    openswmmvis::ui::DialogLayoutWatcher watcher;
    QApplication::instance()->installEventFilter(&watcher);

    AboutDialog dialog;
    dialog.resize(1100, 640);
    dialog.show();
    QTest::qWait(50);
    QVERIFY(dialog.property(openswmmvis::ui::kLayoutRestoredOnceProp).toBool());
    // Saved geometry may be clamped on a smaller test screen on repeat runs.
    dialog.resize(1100, 640);
    auto *tree = dialog.findChild<QTreeView *>();
    auto *splitter = dialog.findChild<QSplitter *>();
    QVERIFY(tree && splitter);

    splitter->setSizes({480, 590});
    QTRY_VERIFY(tree->columnWidth(0) >= tree->viewport()->width());
    QCOMPARE(tree->horizontalScrollBar()->maximum(), 0);
    const int wideWidth = tree->viewport()->width();
    const QModelIndex guiEntry = tree->currentIndex();
    const QString guiName = guiEntry.data().toString();
    QVERIFY(tree->visualRect(guiEntry).width() >= tree->fontMetrics().horizontalAdvance(guiName));
    QVERIFY(dialog.grab().save(QDir(outputDir).filePath(QStringLiteral("gui_panel_wide.png"))));

    splitter->setSizes({160, 910});
    QTRY_VERIFY(tree->viewport()->width() < wideWidth);
    QTRY_VERIFY(tree->horizontalScrollBar()->maximum() > 0);
    auto *scroll = tree->horizontalScrollBar();
    const QModelIndex engineEntry = tree->model()->index(0, 0, tree->model()->index(1, 0));
    const QRect before = tree->visualRect(engineEntry);
    scroll->setValue(scroll->maximum());
    QTRY_VERIFY(tree->visualRect(engineEntry).left() < before.left());
    QVERIFY(dialog.grab().save(QDir(outputDir).filePath(QStringLiteral("gui_panel_scrolled.png"))));

    // A mouse drag must resize the panes; the handle cannot be a dead divider.
    auto *handle = splitter->handle(1);
    const int oldWidth = tree->viewport()->width();
    const QPoint start = handle->rect().center();
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(handle, start + QPoint(140, 0));
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, start + QPoint(140, 0));
    QTRY_VERIFY(tree->viewport()->width() > oldWidth);
    dialog.close();
}

QTEST_MAIN(TestAboutDialog)
#include "test_aboutdialog.moc"
