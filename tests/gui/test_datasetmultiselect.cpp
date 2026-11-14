// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      test_datasetmultiselect.cpp
 * @brief     Tests for datasetmultiselect.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "swmmvis.h"
#include "swmmvisprojectwindow.h"
#include "map/mapcanvas.h"
#include "layers/gisvectorlayer.h"
#include "layers/gisrasterlayer.h"
#include "layers/tabulardatalayer.h"
#include "layers/swmmresultslayer.h"
#include "layers/swmm2dresultslayer.h"
#include "ui/dialogs/sublayerselectiondialog.h"
#include <QtTest>
#include <QQmlEngine>
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include <QFileDialog>
#include <QLineEdit>
#include <QMdiArea>
#include <QTimer>
#include <QThreadPool>
#include <QSettings>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <openswmm/engine/openswmm_engine.h>

class TestMainWindow : public SWMMVis
{
public:
    /**
     *
     * @brief Suppress unsaved prompts and close every descendant project before base-window
     * destruction.
     * Sends pending DeferredDelete events globally afterward; does not wait for all worker
     * jobs here. Project pointers are borrowed during iteration, and Qt owns their deletion.
     *
     */
    ~TestMainWindow()
    {
        for (auto *window : findChildren<SWMMVisProjectWindow *>()) {
            window->setHasChanges(false);
            window->close();
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
};

class TestDatasetMultiselect : public QObject
{
    Q_OBJECT
    QString root;
    /**
     *
     * @brief Write a complete byte fixture to a file, truncating existing content.
     * @param path Borrowed destination path; parent directory must already exist.
     * @param bytes Borrowed content copied synchronously to QFile.
     * @return True only if open succeeds and byte count exactly matches; closes on return.
     * May leave truncated/partial content on failure, without atomic replacement or cleanup.
     *
     */
    bool write(const QString &path, const QByteArray &bytes)
    {
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
    }
    /**
     *
     * @brief Join the initialized fixture root with a slash and supplied name.
     * @param name Borrowed suffix copied verbatim; expected fixture-relative, not sanitized.
     * @return Owned string, without canonicalization, existence check or directory creation.
     *
     */
    QString path(const QString &name) const { return root + "/" + name; }
    /**
     *
     * @brief Invoke project opening and poll briefly for its editing engine.
     * @param app Borrowed live main window that owns the resulting project window.
     * @return Borrowed first descendant project with an engine, or null on invocation/
     * lookup/load timeout. Polls up to200 times at10ms while delivering Qt events; not proof
     * all background loads finished. Caller retains app/window lifetime during later actions.
     *
     */
    SWMMVisProjectWindow *open(SWMMVis &app)
    {
        if (!QMetaObject::invokeMethod(&app, "onOpenProject", Qt::DirectConnection,
                                       Q_ARG(QString, path("model.inp"))))
            return nullptr;
        auto *window = app.findChild<SWMMVisProjectWindow *>();
        if (!window)
            return nullptr;
        for (int i = 0; i < 200 && !window->modelLayer()->engine(); ++i)
            QTest::qWait(10);
        return window->modelLayer()->engine() ? window : nullptr;
    }
    /**
     *
     * @brief Drive an application add-dataset slot through a real nonnative multi-file picker.
     * @param app Borrowed live main window and Qt connection context.
     * @param slot Borrowed null-terminated Qt method name invoked synchronously.
     * @param files Borrowed selection paths; must be nonempty unless cancel is true.
     * @param cancel If true, reject the encountered picker without reading files.first().
     * @return True only if invocation succeeded and a visited picker used ExistingFiles;
     * this does not prove acceptance, successful imports or all worker completion. A10ms
     * local timer fills quoted basenames; absent fileNameEdit rejects. A10-second watchdog
     * rejects the active modal dialog once. Callbacks borrow locals during modal execution,
     * and both timers are destroyed on return; logs selected dataset paths.
     *
     */
    bool select(SWMMVis &app, const char *slot, const QStringList &files, bool cancel = false)
    {
        bool visited = false, multi = false;
        QTimer timer;
        timer.setInterval(10);
        connect(&timer, &QTimer::timeout, &app, [&] {
            auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            visited = true;
            multi = dialog->fileMode() == QFileDialog::ExistingFiles;
            timer.stop();
            if (cancel) {
                dialog->reject();
                return;
            }
            dialog->setDirectory(QFileInfo(files.first()).absolutePath());
            QString quoted;
            for (const auto &file : files)
                quoted += QStringLiteral("\"") + QFileInfo(file).fileName() + QStringLiteral("\" ");
            auto *editor = dialog->findChild<QLineEdit *>("fileNameEdit");
            if (!editor) {
                dialog->reject();
                return;
            }
            editor->setText(quoted.trimmed());
            qInfo() << "Selected datasets" << dialog->selectedFiles();
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        });
        timer.start();
        QTimer watchdog;
        watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, &app, [] {
            if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                d->reject();
        });
        watchdog.start(10000);
        const bool invoked = QMetaObject::invokeMethod(&app, slot, Qt::DirectConnection);
        return invoked && visited && multi;
    }
    /**
     *
     * @brief Count currently attached canvas layers of the requested QObject-derived type.
     * @tparam T Layer class used for qobject_cast, including derived instances.
     * @param window Borrowed non-null project with a live canvas.
     * @return Current matching layer count; no wait, mutation, ownership transfer or
     * background-job completion guarantee.
     *
     */
    template <class T> int count(SWMMVisProjectWindow *window)
    {
        int n = 0;
        for (auto *layer : window->canvas()->layers())
            if (qobject_cast<T *>(layer))
                ++n;
        return n;
    }
private slots:
    /**
     *
     * @brief Create persistent valid/broken dataset fixtures and2D/1D result copies for
     * batch-open cases.
     * Registers QML renderer types/GDAL, chooses environment or source-relative evidence
     * root and redirects process-wide INI settings path/format without restoration. Writes
     * a mini2D deck, runs the engine, copies outputs, disables automatic2D sibling load,
     * creates vector/CSV/raster/multilayer GeoPackage data and invalid placeholders.
     * Existing fixture names are overwritten/removed and generated evidence remains.
     *
     */
    void initTestCase()
    {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM", 1, 0, "SWMMLayerQSGRenderer");
        qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DMeshQSGRenderer");
        qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DResultsQSGRenderer");
        root = qEnvironmentVariable(
            "SWMMVIS_DATASET_MULTISELECT_OUTPUT",
            QFileInfo(QString::fromUtf8(__FILE__))
                .absoluteDir()
                .filePath("../../workplans/artifacts/dataset_multiselect_2026-10-04/fixtures"));
        QVERIFY(QDir().mkpath(root));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, path("settings"));
        GDALAllRegister();
        QByteArray deck =
            "[TITLE]\nMultiple dataset fixture\n[OPTIONS]\nFLOW_UNITS CMS\nFLOW_ROUTING DYNWAVE\nCRS EPSG:32618\nSTART_DATE 01/01/2026\nEND_DATE 01/01/2026\nEND_TIME 00:02:00\nREPORT_STEP 00:01:00\nROUTING_STEP 5\n[JUNCTIONS]\nJ 8 4 0 0 0\n[OUTFALLS]\nO 7.5 FREE NO\n[CONDUITS]\nC J O 30 .013 0 0 0\n[XSECTIONS]\nC CIRCULAR .5 0 0 0 1\n[COORDINATES]\nJ 0 0\nO 10 10\n[2D_OPTIONS]\nINTEGRATOR EXPLICIT\nLTS_TIERS 1\nMAX_TIMESTEP 5\nDRY_DEPTH .001\nREPORT_2D YES\nOUTPUT_FILE generated.h5\nOUTPUT_PRECISION FLOAT64\nREPORT_2D_VARIABLES DEPTH\n[2D_VERTICES]\n0 0 10\n10 0 10\n0 10 10\n[2D_TRIANGLES]\n0 1 2 .03 .2\n";
        QVERIFY(write(path("model.inp"), deck));
        QCOMPARE(swmm_engine_run(path("model.inp").toUtf8(), path("generated.rpt").toUtf8(),
                                 path("generated.out").toUtf8(), nullptr),
                 SWMM_OK);
        for (const auto &name : {"a.out", "b.out", "a.h5", "b.h5"}) {
            QFile::remove(path(name));
            QVERIFY(
                QFile::copy(path(QString(name).endsWith(".out") ? "generated.out" : "generated.h5"),
                            path(name)));
        }
        // Avoid auto-loading siblings: the result tests explicitly add copies.
        QVERIFY(write(path("model.inp"),
                      deck.replace("OUTPUT_FILE generated.h5", "OUTPUT_FILE absent.h5")));
        for (const auto &name : {"a.geojson", "b.geojson"})
            QVERIFY(write(
                path(name),
                R"({"type":"FeatureCollection","crs":{"type":"name","properties":{"name":"EPSG:32618"}},"features":[{"type":"Feature","geometry":{"type":"Point","coordinates":[2,3]},"properties":{"id":1}}]})"));
        for (const auto &name : {"a.csv", "b.csv"})
            QVERIFY(write(path(name), "x,y,value\n2,3,1\n"));
        for (const auto &name : {"broken.geojson", "broken.tif", "broken.out", "broken.h5"})
            QVERIFY(write(path(name), "not a dataset"));
        for (const auto &name : {"a.tif", "b.tif"}) {
            auto *d = GetGDALDriverManager()->GetDriverByName("GTiff")->Create(
                path(name).toUtf8(), 2, 2, 1, GDT_Byte, nullptr);
            QVERIFY(d);
            double transform[] = {0, 1, 0, 2, 0, -1};
            d->SetGeoTransform(transform);
            OGRSpatialReference s;
            s.SetFromUserInput("EPSG:32618");
            d->SetSpatialRef(&s);
            QCOMPARE(d->GetRasterBand(1)->Fill(4), CE_None);
            GDALClose(d);
        }
        QFile::remove(path("multi.gpkg"));
        auto *d = GetGDALDriverManager()->GetDriverByName("GPKG")->Create(
            path("multi.gpkg").toUtf8(), 0, 0, 0, GDT_Unknown, nullptr);
        QVERIFY(d);
        QVERIFY(d->CreateLayer("first", nullptr, wkbPoint));
        QVERIFY(d->CreateLayer("second", nullptr, wkbPoint));
        GDALClose(d);
    }
    /**
     *
     * @brief Require two valid vector files around one broken input to load as two layers.
     * Checks attached filenames a/b.geojson, clears project dirty state and waits for the
     * global pool; no exact diagnostic or layer-order assertion.
     *
     */
    void vectorBatchContinuesAfterBadFile()
    {
        TestMainWindow app;
        auto *window = open(app);
        QVERIFY(window);
        QVERIFY(select(app, "onAddVectorLayer",
                       {path("a.geojson"), path("broken.geojson"), path("b.geojson")}));
        QTRY_COMPARE_WITH_TIMEOUT(count<GISVectorLayer>(window), 2, 10000);
        QStringList paths;
        for (auto *l : window->canvas()->layers())
            if (auto *v = qobject_cast<GISVectorLayer *>(l))
                paths << QFileInfo(v->filePath()).fileName();
        QVERIFY(paths.contains("a.geojson"));
        QVERIFY(paths.contains("b.geojson"));
        window->setHasChanges(false);
        QThreadPool::globalInstance()->waitForDone();
    }
    /**
     *
     * @brief Require a raster batch to continue after a broken middle file and attach two valid
     * rasters.
     * Waits up to10seconds for layer count, clears dirty state and drains the global worker
     * pool; no raster pixel/CRS or exact error-message comparison.
     *
     */
    void rasterBatchContinuesAfterBadFile()
    {
        TestMainWindow app;
        auto *window = open(app);
        QVERIFY(window);
        QVERIFY(
            select(app, "onAddRasterLayer", {path("a.tif"), path("broken.tif"), path("b.tif")}));
        QTRY_COMPARE_WITH_TIMEOUT(count<GISRasterLayer>(window), 2, 10000);
        window->setHasChanges(false);
        QThreadPool::globalInstance()->waitForDone();
    }
    /**
     *
     * @brief Require canceled multi-selection to add no tabular layers and a subsequent two-file
     * selection to add both.
     * Uses placeholder CSV fixtures and Qt retry polling; no field mapping or cell-value
     * assertion and no explicit worker-pool drain in this case.
     *
     */
    void delimitedBatchAndCancel()
    {
        TestMainWindow app;
        auto *window = open(app);
        QVERIFY(window);
        QVERIFY(select(app, "onAddDelimitedData", {}, true));
        QCOMPARE(count<TabularDataLayer>(window), 0);
        QVERIFY(select(app, "onAddDelimitedData", {path("a.csv"), path("b.csv")}));
        QTRY_COMPARE(count<TabularDataLayer>(window), 2);
    }
    /**
     *
     * @brief Require rejecting a multilayer GeoPackage's sublayer dialog to permit the next
     * vector file to load.
     * A10ms timer detects/rejects the modal sublayer picker, checks it was visited and only
     * one vector layer attaches, then stops it and drains workers. Callback borrows live
     * local state during the synchronous add action.
     *
     */
    void skippedVectorSublayersContinueBatch()
    {
        TestMainWindow app;
        auto *window = open(app);
        QVERIFY(window);
        bool skipped = false;
        QTimer picker;
        picker.setInterval(10);
        connect(&picker, &QTimer::timeout, &app, [&] {
            if (auto *d =
                    qobject_cast<SublayerSelectionDialog *>(QApplication::activeModalWidget())) {
                skipped = true;
                d->reject();
            }
        });
        picker.start();
        QVERIFY(select(app, "onAddVectorLayer", {path("multi.gpkg"), path("b.geojson")}));
        QTRY_COMPARE_WITH_TIMEOUT(count<GISVectorLayer>(window), 1, 10000);
        QVERIFY(skipped);
        picker.stop();
        window->setHasChanges(false);
        QThreadPool::globalInstance()->waitForDone();
    }
    /**
     *
     * @brief Require1D/2D result batches to skip broken inputs, activate the last valid1D run and
     * deduplicate repeats.
     * Checks three1D layers including the project-loaded result, active b.out with two
     * periods, and two2D layers after replay. Mixes retry polling, worker drains and short
     * waits; no numerical sample or detailed load-error assertion.
     *
     */
    void resultBatchesAndDeduplication()
    {
        TestMainWindow app;
        auto *window = open(app);
        QVERIFY(window);
        QVERIFY(select(app, "onAddSWMMResultsLayer",
                       {path("a.out"), path("broken.out"), path("b.out")}));
        QTRY_COMPARE_WITH_TIMEOUT(count<SWMMResultsLayer>(window), 3, 10000);
        QThreadPool::globalInstance()->waitForDone();
        QTest::qWait(100);
        QVERIFY(window->activeResultsLayer());
        QCOMPARE(QFileInfo(window->activeResultsLayer()->resultsFilePath()).fileName(),
                 QString("b.out"));
        QCOMPARE(window->activeResultsLayer()->totalTimeSteps(), 2);
        QVERIFY(select(app, "onAddSWMMResultsLayer", {path("a.out"), path("b.out")}));
        QThreadPool::globalInstance()->waitForDone();
        QTest::qWait(100);
        QCOMPARE(count<SWMMResultsLayer>(window), 3);
        QVERIFY(
            select(app, "onAdd2DResultsLayer", {path("a.h5"), path("broken.h5"), path("b.h5")}));
        QTRY_COMPARE_WITH_TIMEOUT(count<SWMM2DResultsLayer>(window), 2, 10000);
        QVERIFY(select(app, "onAdd2DResultsLayer", {path("a.h5"), path("b.h5")}));
        QTest::qWait(100);
        QCOMPARE(count<SWMM2DResultsLayer>(window), 2);
    }
    /**
     *
     * @brief Require deleting the main window after two tabular imports to destroy its project
     * child.
     * Uses QPointer to observe deletion; does not separately inspect all layers, engine
     * handles or worker lifetimes after teardown.
     *
     */
    void mainWindowTeardownAfterBatch()
    {
        auto *app = new SWMMVis;
        auto *window = open(*app);
        QVERIFY(window);
        QVERIFY(select(*app, "onAddDelimitedData", {path("a.csv"), path("b.csv")}));
        QTRY_COMPARE(count<TabularDataLayer>(window), 2);
        QPointer<SWMMVisProjectWindow> target(window);
        delete app;
        QVERIFY(target.isNull());
    }
    /**
     *
     * @brief Require deleting a raster batch's target project to leave no descendant raster
     * layers after workers finish.
     * Observes target deletion through QPointer, drains the global pool and delivers events;
     * does not count canceled jobs or assert exact cancellation diagnostics.
     *
     */
    void closedTargetStopsBatch()
    {
        TestMainWindow app;
        auto *window = open(app);
        QVERIFY(window);
        QPointer<SWMMVisProjectWindow> target(window);
        QVERIFY(select(app, "onAddRasterLayer", {path("a.tif"), path("b.tif")}));
        delete window;
        QTRY_VERIFY(target.isNull());
        QThreadPool::globalInstance()->waitForDone();
        QTest::qWait(100);
        QVERIFY(app.findChildren<GISRasterLayer *>().isEmpty());
    }
};
QTEST_MAIN(TestDatasetMultiselect)
#include "test_datasetmultiselect.moc"
