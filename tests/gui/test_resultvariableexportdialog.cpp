/**
 * @file      test_resultvariableexportdialog.cpp
 * @brief     Tests for resultvariableexportdialog.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/dialogs/resultvariableexportdialog.h"
#include <QTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPointer>
#include <QSemaphore>
#include <QThreadPool>
#include <QScopeGuard>
#include <QtConcurrent>
using namespace openswmmvis;
class TestResultVariableExportDialog : public QObject
{
    Q_OBJECT
    QString root;
    /**
     *
     * @brief Build the immutable one-cell scalar export fixture.
     * @return Shared owned square geometry in EPSG:32618/metres with TSS saturated-zone
     * concentration descriptor MG/L, scalar 42 and Valid status. Contains no live source/
     * engine/QObject pointers; no file creation or physical validity checks here.
     *
     */
    io::Mesh2DVariableGisSnapshotPtr snapshot()
    {
        auto s = std::make_shared<io::Mesh2DVariableGisSnapshot>();
        s->crsWkt = "EPSG:32618";
        s->scalar.descriptor.dataset = "Mesh2_face_gw_sat_conc";
        s->scalar.descriptor.species = "TSS";
        s->scalar.descriptor.units = "MG/L";
        s->scalar.descriptor.unitsKnown = true;
        s->vertices = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        s->cells = {{0, 1, 2, 3}};
        s->scalar.values = {42};
        s->scalar.status = {io::Mesh2DValueStatus::Valid};
        return s;
    }
private slots:
    /**
     *
     * @brief Require creation of persistent scalar GIS output directory selected by
     * SWMMVIS_SCALAR_GIS_OUTPUT or source-relative acceptance fallback.
     * Existing artifacts remain until individual cases remove their named files.
     *
     */
    void initTestCase()
    {
        root = qEnvironmentVariable(
            "SWMMVIS_SCALAR_GIS_OUTPUT",
            QFileInfo(QString::fromUtf8(__FILE__))
                .absoluteDir()
                .filePath("../../workplans/artifacts/phase_36_acceptance/scalar-gis"));
        QVERIFY(QDir().mkpath(root));
    }
    /**
     *
     * @brief Require two immediate export-start calls to produce one successful completion and
     * one GeoPackage within ten seconds.
     * Removes/replaces dialog.gpkg under fixture root and deletes the standalone dialog
     * after completion; requires native widgets, GDAL and event processing.
     *
     */
    void asyncImmutableExportAndDuplicateStart()
    {
        auto *dialog = new ui::ResultVariableExportDialog(snapshot());
        const QString path = QDir(root).filePath("dialog.gpkg");
        QFile::remove(path);
        dialog->findChild<QLineEdit *>("scalarGisPath")->setText(path);
        QSignalSpy done(dialog, &ui::ResultVariableExportDialog::exportFinished);
        dialog->startExport();
        dialog->startExport();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
        QVERIFY2(done[0][0].toBool(), qPrintable(done[0][1].toString()));
        QVERIFY(QFileInfo::exists(path));
        delete dialog;
    }
    /**
     *
     * @brief Queue export behind a blocked global worker, delete dialog owner, then require no
     * destination after releasing the worker.
     * Removes closed.gpkg, drains/restricts the process-global pool to one thread and
     * uses a scope guard to release it, wait and restore its previous maximum on exits.
     * Requires isolated serialized pool use and GUI event support; fixture snapshot is owned.
     *
     */
    void ownerDestructionCancelsWithoutSourcePointers()
    {
        auto *pool = QThreadPool::globalInstance();
        pool->waitForDone();
        const int maximum = pool->maxThreadCount();
        pool->setMaxThreadCount(1);
        QSemaphore entered, release;
        bool released = false;
        const auto cleanup = qScopeGuard([&] {
            if (!released)
                release.release();
            pool->waitForDone();
            pool->setMaxThreadCount(maximum);
        });
        auto blocker = QtConcurrent::run(pool, [&] {
            entered.release();
            release.acquire();
        });
        entered.acquire();
        const QString path = QDir(root).filePath("closed.gpkg");
        QFile::remove(path);
        auto *owner = new QWidget;
        auto *dialog = new ui::ResultVariableExportDialog(snapshot(), owner);
        dialog->findChild<QLineEdit *>("scalarGisPath")->setText(path);
        QPointer<ui::ResultVariableExportDialog> guard = dialog;
        dialog->startExport();
        delete owner;
        QVERIFY(!guard);
        release.release();
        released = true;
        blocker.waitForFinished();
        pool->waitForDone();
        QVERIFY(!QFileInfo::exists(path));
    }
};
QTEST_MAIN(TestResultVariableExportDialog)
#include "test_resultvariableexportdialog.moc"
