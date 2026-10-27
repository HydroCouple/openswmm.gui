/**
 * @file test_timeserieslayer.cpp
 * @brief Native observation-layer persistence, lazy tree, selection, links and paged browser acceptance.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "layers/timeserieslayer.h"
#include "layers/gisobjectref.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "project/openswmmvisworkspace.h"
#include "project/projectserializer.h"
#include "swmmvisprojectwindow.h"
#include "timeseries/timeseriesstore.h"
#include "timeseries/timeseriesregistry.h"
#include "timeseries/timeseriesassignment.h"
#include "ui/dialogs/timeseriesassignmentdialog.h"
#include "plugins/extensionmanager.h"
#include "plugins/extensionmodelaccess.h"
#include "plugins/extensiontimeseriesaccess.h"
#include "plugins/extensionstateregistry.h"
#include "plugins/guiextensionhost.h"
#include "sdk/ITimeSeriesLayersV1.hpp"
#include "ui/panels/layertreecategories.h"
#include "ui/panels/layertreepanel.h"
#include "ui/panels/timeserieslayerbrowser.h"
#include <QComboBox>
#include <QFile>
#include <QJsonDocument>
#include <QPushButton>
#include <QQmlEngine>
#include <QTableView>
#include <QTimeZone>
#include <QMainWindow>
#include <QMdiArea>
#include <QSettings>
#include <openswmm/engine/openswmm_tables.h>
#include <QtTest>
#include <ogrsf_frmts.h>

using namespace OpenSWMM::TimeSeries;
/** @brief Real retained source fixtures; no network or temporary directory dependencies. */
class TestTimeSeriesLayer : public QObject
{
    Q_OBJECT
    QString m_dir, m_package, m_primary, m_secondary, m_unlocated;
    /** @brief Write a fixture atomically enough for test setup; output remains reviewable. */
    bool write(const QString &path, const QByteArray &bytes)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    /** @brief Create a blank engine context only for selection/sidecar integration tests. */
    bool initialize(SWMMVisProjectWindow &window)
    {
        SWMMModelLayer::NewProjectSpec spec;
        spec.name = "Observation layer acceptance";
        spec.startDateTime = QDateTime(QDate(2024, 12, 1), QTime(0, 0));
        spec.endDateTime = spec.startDateTime.addDays(1);
        QList<QString> warnings, errors;
        return window.initializeBlankModel(spec, warnings, errors);
    }
private slots:
    /** @brief Prepare one located multi-series collection with a separately unlocated record. */
    void initTestCase()
    {
        qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM", 1, 0, "SWMMLayerQSGRenderer");
        qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DMeshQSGRenderer");
        qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DResultsQSGRenderer");
        m_dir = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath())
                    .absoluteFilePath("../verification/timeseries_layers_2026-10-27/native");
        QVERIFY(QDir().mkpath(m_dir));
        m_package = m_dir + "/observations.gpkg";
        QByteArray csv = "series,time,value\n";
        const auto start = QDateTime(QDate(2024, 12, 1), QTime(0, 0), QTimeZone::UTC);
        for (int i = 0; i < 700; ++i)
            csv += "primary," + start.addSecs(i * 60).toString(Qt::ISODate).toUtf8() + ',' +
                   (i == 4 ? QByteArray{} : QByteArray::number(i)) + '\n';
        csv += "secondary,2024-12-01T00:00:00Z,12.5\n";
        QVERIFY(write(m_dir + "/located.csv", csv));
        QVERIFY(write(m_dir + "/unlocated.csv", "time,value\n2024-12-01T00:00:00Z,3.25\n"));
        const QJsonObject common{{"provider", "fixture"},
                                 {"collection", "continuous"},
                                 {"siteId", "00123"},
                                 {"parameter", "00060"},
                                 {"unit", "m3/s"},
                                 {"timeBasis", "instant"},
                                 {"locationId", "station-a"},
                                 {"title", "Streamflow"}};
        QJsonObject unlocated = common;
        unlocated["locationId"] = "";
        unlocated["sourceSeriesId"] = "unlocated";
        QJsonObject descriptor{
            {"schema", "openswmm.timeseries-import/1"},
            {"datasetId", "b813c35f-3fca-462f-aa55-fac90114bc62"},
            {"title", "Acceptance observations"},
            {"locations",
             QJsonArray{QJsonObject{
                 {"id", "station-a"},
                 {"title", "Station A"},
                 {"attributes",
                  QJsonObject{{"station_number", "00123"}, {"agency", "Synthetic fixture"}}},
                 {"geometry",
                  QJsonObject{{"type", "Point"}, {"coordinates", QJsonArray{-77.0, 39.0}}}}}}},
            {"sources",
             QJsonArray{QJsonObject{{"path", "located.csv"},
                                    {"columns", QJsonObject{{"sourceSeriesId", "series"},
                                                            {"time", "time"},
                                                            {"value", "value"}}},
                                    {"defaults", common}},
                        QJsonObject{{"path", "unlocated.csv"},
                                    {"columns", QJsonObject{{"time", "time"}, {"value", "value"}}},
                                    {"defaults", unlocated}}}}};
        QVERIFY(write(m_dir + "/import.json", QJsonDocument(descriptor).toJson()));
        QFile::remove(m_package);
        QString error;
        QVERIFY2(TimeSeriesStore::importDescriptor(m_dir + "/import.json", m_package, &error),
                 qPrintable(error));
        const auto dataset = TimeSeriesStore::open(m_package, &error);
        QVERIFY2(bool(dataset), qPrintable(error));
        QCOMPARE(dataset->series.size(), 3);
        for (const auto &s : dataset->series) {
            if (s.sourceSeriesId == "primary")
                m_primary = s.id;
            if (s.sourceSeriesId == "secondary")
                m_secondary = s.id;
            if (s.sourceSeriesId == "unlocated")
                m_unlocated = s.id;
        }
        QVERIFY(!m_primary.isEmpty());
        QVERIFY(!m_unlocated.isEmpty());
    }
    /** @brief Standalone layer opens without an engine and marker selection expands exact station series. */
    void standaloneAndQualifiedSelection()
    {
        TimeSeriesLayer layer;
        QString error;
        QVERIFY2(layer.openTimeSeries(m_package, {}, &error), qPrintable(error));
        QCOMPARE(layer.layerType(), OpenSWMMVisLayer::SWMMTimeSeriesLayer);
        QCOMPARE(layer.seriesIds().size(), 3);
        QVERIFY(layer.labelConfig().enabled);
        QVERIFY(layer.ogrLayer());
        layer.ogrLayer()->ResetReading();
        auto *feature = layer.ogrLayer()->GetNextFeature();
        QVERIFY(feature);
        const auto fid = feature->GetFID();
        OGRFeature::DestroyFeature(feature);
        layer.setSelectedFeatureIds({fid});
        QCOMPARE(layer.selectedSeriesIds().size(), 2);
        QVERIFY(layer.selectedSeriesIds().contains(m_primary));
        QVERIFY(layer.selectedSeriesIds().contains(m_secondary));
        layer.setSelectedSeriesIds({m_unlocated});
        QVERIFY(layer.selectedFeatureIds().isEmpty());
        QCOMPARE(layer.seriesIdForRef(layer.seriesRef(m_primary)), m_primary);
        TimeSeriesLayer other;
        QVERIFY(other.openTimeSeries(m_package));
        QVERIFY(other.seriesIdForRef(layer.seriesRef(m_primary)).isEmpty());
        QCOMPARE(layer.seriesRef(m_primary).objectType, SWMMObjectRef::ObservedSeries);
        QVERIFY(layer.seriesRef(m_primary).objectType != SWMMObjectRef::TimeSeries);
    }
    /** @brief Metadata children are lazy and carry stable IDs instead of observation row positions. */
    void lazyTreeAndCategoryMigration()
    {
        MapCanvas canvas;
        auto *layer = new TimeSeriesLayer;
        layer->setParent(&canvas);
        QVERIFY(layer->openTimeSeries(m_package));
        canvas.addLayer(layer, false);
        LayerTreeModel model(&canvas);
        const auto parent = model.indexForLayer(layer);
        QVERIFY(parent.isValid());
        QCOMPARE(model.rowCount(parent), 0);
        QVERIFY(model.canFetchMore(parent));
        QVERIFY(model.hasChildren(parent));
        model.fetchMore(parent);
        QCOMPARE(model.rowCount(parent), 3);
        QVERIFY(!model.canFetchMore(parent));
        QCOMPARE(model.seriesIdForIndex(model.index(0, 0, parent)), layer->seriesIds().first());
        QCOMPARE(model.parent(model.index(0, 0, parent)), parent);
        QVERIFY(!(model.flags(model.index(0, 0, parent)) & Qt::ItemIsDragEnabled));
        QVector<int> legacy{7, 6, 5, 4, 3, 2, 1, 0};
        canvas.setLayerGroupOrder(legacy);
        auto expected = legacy;
        expected.append(openswmmvis::ui::CatTimeSeries);
        QCOMPARE(canvas.layerGroupOrder(), expected);
        const auto order = MapCanvas::layerOrderKey(layer);
        layer->setName("Renamed collection");
        QCOMPARE(MapCanvas::layerOrderKey(layer), order);
    }
    /** @brief Source UUID mismatch never rebinds to another collection. */
    void unavailableAndWrongIdentity()
    {
        TimeSeriesLayer layer;
        QString error;
        QVERIFY(!layer.openTimeSeries(m_package, "wrong-dataset", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!layer.dataset());
        QCOMPARE(layer.datasetId(), QString("wrong-dataset"));
        TimeSeriesLayer missing;
        QVERIFY(!missing.openTimeSeries(m_dir + "/not-present.gpkg", "expected", &error));
        QCOMPARE(missing.sourcePath(), QDir::cleanPath(m_dir + "/not-present.gpkg"));
        QCOMPARE(missing.datasetId(), QString("expected"));
    }
    /** @brief Saved observations and assignment provenance reopen independently, including missing placeholders. */
    void saveReopenAndMissingSource()
    {
        std::unique_ptr<OpenSWMMVisWorkspace> workspace(
            OpenSWMMVisWorkspace::newInstance({}, nullptr));
        SWMMVisProjectWindow first(workspace.get(), {});
        QVERIFY(initialize(first));
        auto *layer = new TimeSeriesLayer;
        layer->setParent(&first);
        QVERIFY(layer->openTimeSeries(m_package));
        first.canvas()->addLayer(layer, false);
        const auto id = layer->layerId();
        first.setProperty(
            "timeSeriesAssignments",
            QJsonArray{QJsonObject{{"seriesId", m_primary}, {"modelSeries", "TestFlow"}}});
        const auto sidecar = m_dir + "/native-layers.oswp";
        QString error;
        QVERIFY2(ProjectSerializer::saveToFile(sidecar, &first, &error), qPrintable(error));
        QFile file(sidecar);
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto json = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        auto gis = json["gisLayers"].toArray();
        QCOMPARE(gis.size(), 1);
        QCOMPARE(gis.first().toObject()["type"].toString(), QString("timeseries"));
        QVERIFY(!QDir::isAbsolutePath(gis.first().toObject()["path"].toString()));
        SWMMVisProjectWindow second(workspace.get(), {});
        QVERIFY(initialize(second));
        QStringList warnings;
        QVERIFY2(ProjectSerializer::applyFromFile(sidecar, &second, &error, &warnings),
                 qPrintable(error));
        TimeSeriesLayer *restored = nullptr;
        for (auto *candidate : second.canvas()->layers())
            if (auto *found = qobject_cast<TimeSeriesLayer *>(candidate))
                restored = found;
        QVERIFY(restored);
        QVERIFY(restored->dataset());
        QCOMPARE(restored->layerId(), id);
        QCOMPARE(restored->seriesIds(), layer->seriesIds());
        QCOMPARE(second.property("timeSeriesAssignments").toJsonArray(),
                 first.property("timeSeriesAssignments").toJsonArray());
        auto record = gis[0].toObject();
        record["path"] = "not-present.gpkg";
        gis[0] = record;
        json["gisLayers"] = gis;
        const auto missingPath = m_dir + "/missing-layers.oswp";
        QVERIFY(write(missingPath, QJsonDocument(json).toJson()));
        SWMMVisProjectWindow third(workspace.get(), {});
        QVERIFY(initialize(third));
        QVERIFY(ProjectSerializer::applyFromFile(missingPath, &third, &error, &warnings));
        TimeSeriesLayer *missing = nullptr;
        for (auto *candidate : third.canvas()->layers())
            if (auto *found = qobject_cast<TimeSeriesLayer *>(candidate))
                missing = found;
        QVERIFY(missing);
        QVERIFY(!missing->dataset());
        QCOMPARE(missing->datasetId(), layer->datasetId());
        QCOMPARE(missing->layerId(), id);
        const auto resavedMissing = m_dir + "/missing-layers-resaved.oswp";
        QVERIFY(ProjectSerializer::saveToFile(resavedMissing, &third, &error));
        QFile missingFile(resavedMissing);
        QVERIFY(missingFile.open(QIODevice::ReadOnly));
        const auto retained =
            QJsonDocument::fromJson(missingFile.readAll()).object()["gisLayers"].toArray();
        QCOMPARE(retained.size(), 1);
        QCOMPARE(retained.first().toObject()["datasetId"].toString(), layer->datasetId());
        QVERIFY(missing->openTimeSeries(m_package, missing->datasetId(), &error));
    }
    /** @brief A durable GIS link is undoable; removed source leaves observations and own marker plot usable. */
    void stableFeatureLinkAndUndo()
    {
        std::unique_ptr<OpenSWMMVisWorkspace> workspace(
            OpenSWMMVisWorkspace::newInstance({}, nullptr));
        SWMMVisProjectWindow window(workspace.get(), {});
        QVERIFY(initialize(window));
        const auto path = m_dir + "/linked-feature.geojson";
        QVERIFY(write(
            path,
            R"({"type":"FeatureCollection","features":[{"type":"Feature","properties":{"id":"permanent-site"},"geometry":{"type":"Point","coordinates":[-77,39]}}]})"));
        auto *feature = new GISVectorLayer(path);
        feature->setParent(&window);
        QVERIFY(feature->ogrLayer());
        window.canvas()->addLayer(feature, false);
        auto *layer = new TimeSeriesLayer;
        layer->setParent(&window);
        QVERIFY(layer->openTimeSeries(m_package));
        window.canvas()->addLayer(layer, false);
        feature->ogrLayer()->ResetReading();
        auto *row = feature->ogrLayer()->GetNextFeature();
        QVERIFY(row);
        const auto fid = row->GetFID();
        OGRFeature::DestroyFeature(row);
        const auto ref = GisObjectRef::feature(feature->layerId(), fid);
        window.selectionManager()->select(ref);
        QString error;
        QVERIFY2(layer->attachToSelectedFeature({m_primary}, &error), qPrintable(error));
        QCOMPARE(layer->associationStatus(m_primary), QString("Linked"));
        QCOMPARE(layer->seriesForFeature(ref), QStringList{m_primary});
        window.canvas()->undoStack()->undo();
        QCOMPARE(layer->associationStatus(m_primary), QString("Not linked"));
        window.canvas()->undoStack()->redo();
        QCOMPARE(layer->associationStatus(m_primary), QString("Linked"));
        window.canvas()->takeLayer(window.canvas()->layers().indexOf(feature), false);
        QCOMPARE(layer->associationStatus(m_primary), QString("Unresolved link"));
        QVERIFY(layer->dataset());
        window.canvas()->setCanvasSRSByCode("EPSG", 4326);
        const auto hits = timeSeriesHitsAt(window.canvas(), -77, 39, 0.01);
        QVERIFY(!hits.isEmpty());
        QVERIFY(hits.first().seriesIds.contains(m_primary));
        window.canvas()->addLayer(feature, false);
        QCOMPARE(layer->associationStatus(m_primary), QString("Linked"));
        feature->setTemporary(true);
        window.selectionManager()->select(ref);
        QVERIFY(!layer->attachToSelectedFeature({m_secondary}, &error));
        QVERIFY(!error.isEmpty());
        feature->setTemporary(false);
        QFile changed(path);
        QVERIFY(changed.open(QIODevice::Append));
        QCOMPARE(changed.write("\n"), qint64(1));
        changed.close();
        QCOMPARE(layer->associationStatus(m_primary), QString("Unresolved link"));
        QVERIFY(layer->seriesForFeature(ref).isEmpty());
        QVERIFY(layer->dataset());
        QVERIFY(!timeSeriesHitsAt(window.canvas(), -77, 39, 0.01).isEmpty());
        layer->setVisible(false);
        QVERIFY(timeSeriesHitsAt(window.canvas(), -77, 39, 0.01).isEmpty());
    }
    /** @brief Native browser pages 700 observations without caching all samples in its GUI model. */
    void browserPagingAndSharedSelection()
    {
        TimeSeriesLayer layer;
        QVERIFY(layer.openTimeSeries(m_package));
        auto *browser = new TimeSeriesLayerBrowser(&layer);
        QPointer<TimeSeriesLayerBrowser> guard(browser);
        auto *selector = browser->findChild<QComboBox *>("timeSeriesSampleSelector");
        QVERIFY(selector);
        auto *table = browser->findChild<QTableView *>("timeSeriesObservationsTable");
        QVERIFY(table);
        layer.setSelectedSeriesIds({m_primary});
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 500, 10000);
        QVERIFY(table->model()->index(4, 1).data().toString() == "Missing");
        QPushButton *next = nullptr;
        for (auto *button : browser->findChildren<QPushButton *>())
            if (button->text() == "Next")
                next = button;
        QVERIFY(next);
        QVERIFY(next->isEnabled());
        next->click();
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 200, 10000);
        layer.setSelectedSeriesIds({m_unlocated});
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 1, 10000);
        QCOMPARE(selector->currentData().toString(), m_unlocated);
        browser->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!guard);
    }
    /** @brief Native import publishes once, targets the captured generation and cancels held delivery. */
    void sdkImportAndHeldCancellation()
    {
        using namespace OpenSWMM::Sdk;
        const QString directory = m_dir + "/sdk-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(QDir().mkpath(directory));
        const auto input = directory + "/model.inp";
        QVERIFY(QFile::copy(QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath())
                               .absoluteFilePath("../extensions/data/model.inp"), input));
        auto workspace = std::unique_ptr<OpenSWMMVisWorkspace>(OpenSWMMVisWorkspace::newInstance({}, nullptr));
        QMainWindow main;
        auto *mdi = new QMdiArea(&main);
        main.setCentralWidget(mdi);
        QSettings settings(directory + "/host.ini", QSettings::IniFormat);
        auto host = std::make_unique<openswmmvis::GuiExtensionHost>(
            "org.openswmm.timeseries-test", openswmmvis::ExtensionManager::buildManifest(),
            &main, mdi, openswmmvis::ExtensionUiBridge{}, &settings);
        QPointer<SWMMVisProjectWindow> window;
        openswmmvis::installExtensionModelAccess(host.get(), [&] {
            return window ? QList<SWMMVisProjectWindow *>{window} : QList<SWMMVisProjectWindow *>{};
        }, [&] { return window.data(); });
        openswmmvis::installExtensionTimeSeriesAccess(host.get());
        window = new SWMMVisProjectWindow(workspace.get(), input, mdi);
        mdi->addSubWindow(window);
        QList<QString> warnings, errors;
        QVERIFY(window->loadModel(warnings, errors));
        auto *capability = qobject_cast<ITimeSeriesLayersV1 *>(host->lifetime());
        QVERIFY(capability);
        const auto state = host->stateRegistry()->session(window->extensionSessionId());
        TimeSeriesImportRequest request{state.sessionId, state.loadGeneration, QUuid::createUuid(),
                                        m_dir + "/import.json", directory + "/native.gpkg"};
        QString error;
        TimeSeriesImportResult result;
        int completed = 0;
        auto *job = capability->importTimeSeries(request, this, [&](const auto &value) {
            result = value; ++completed;
        }, &error);
        QVERIFY2(job, qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(completed, 1, 20000);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(!result.layerId.isEmpty());
        QCOMPARE(result.seriesCount, 3);
        QCOMPARE(capability->timeSeriesLayers(state.sessionId).size(), 1);
        const auto firstId = result.layerId;
        QVERIFY(capability->importTimeSeries(request, this, [&](const auto &value) {
            result = value; ++completed;
        }, &error));
        QTRY_COMPARE_WITH_TIMEOUT(completed, 2, 10000);
        QCOMPARE(result.layerId, firstId);
        QCOMPARE(capability->timeSeriesLayers(state.sessionId).size(), 1);
        ++request.loadGeneration;
        QVERIFY(!capability->importTimeSeries(request, this, [](const auto &) {}, &error));
        QVERIFY(!error.isEmpty());
        request.loadGeneration = state.loadGeneration;
        request.importId = QUuid::createUuid();
        request.packagePath = directory + "/cancelled.gpkg";
        completed = 0;
        job = capability->importTimeSeries(request, this, [&](const auto &value) {
            result = value; ++completed;
        }, &error);
        QVERIFY2(job, qPrintable(error));
        // Hold GUI delivery until worker settlement; Cancel must still revoke publication.
        QElapsedTimer timer;
        timer.start();
        while (!job->isFinished() && timer.elapsed() < 20000)
            QTest::qSleep(1);
        QVERIFY(job->isFinished());
        job->cancel();
        QTRY_COMPARE_WITH_TIMEOUT(completed, 1, 10000);
        QVERIFY(result.cancelled);
        QVERIFY(result.layerId.isEmpty());
        QVERIFY(!QFileInfo::exists(request.packagePath));
        QCOMPARE(capability->timeSeriesLayers(state.sessionId).size(), 1);
        // A completed worker cannot acquire a new load generation while its callback waits.
        completed = 0;
        request.importId = QUuid::createUuid();
        request.packagePath = directory + "/stale.gpkg";
        job = capability->importTimeSeries(request, this, [&](const auto &value) {
            result = value; ++completed;
        }, &error);
        QVERIFY(job);
        timer.restart();
        while (!job->isFinished() && timer.elapsed() < 20000)
            QTest::qSleep(1);
        QVERIFY(job->isFinished());
        auto *registry = host->stateRegistry();
        const auto newer = registry->beginLoad(state.sessionId);
        registry->finishLoad(state.sessionId, newer, true);
        QTRY_COMPARE_WITH_TIMEOUT(completed, 1, 10000);
        QVERIFY(result.layerId.isEmpty());
        QVERIFY(result.error.contains("Saved, not added"));
        QVERIFY(QFileInfo::exists(request.packagePath));
        delete window.data();
        host.reset();
    }
    /** @brief Model copies and provenance commit/rollback/Undo/Redo as one checked operation. */
    void modelAssignmentTransaction()
    {
        auto workspace = std::unique_ptr<OpenSWMMVisWorkspace>(OpenSWMMVisWorkspace::newInstance({}, nullptr));
        SWMMVisProjectWindow window(workspace.get());
        QVERIFY(initialize(window));
        const QString directory = m_dir + "/assignment-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(QDir().mkpath(directory));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory + "/settings");
        QString error;
        QVERIFY2(window.saveAs(directory + "/model.inp", &error), qPrintable(error));
        auto dataset = TimeSeriesStore::open(m_package, &error);
        QVERIFY(dataset);
        AssignmentOptions options;
        options.seriesId = m_primary;
        options.modelSeriesName = "OBS_NATIVE";
        options.targetUnit = "CMS";
        options.clockConfirmed = true;
        options.unitInterpretationConfirmed = true;
        options.allowIrregularInterpolation = true;
        options.fromKey = dataset->series.first().firstKey;
        // Use a contiguous range before the fixture's deliberate missing sample.
        options.toKey = QDateTime(QDate(2024,12,1), QTime(0,3), QTimeZone::UTC).toMSecsSinceEpoch();
        const auto prepared = prepareAssignment(*dataset, options);
        QVERIFY2(prepared.ok(), qPrintable(prepared.error));
        const int before = window.canvas()->undoStack()->count();
        QVERIFY2(openswmmvis::ui::applyPreparedTimeSeries(&window, prepared, &error), qPrintable(error));
        QCOMPARE(window.canvas()->undoStack()->count(), before + 1);
        QVERIFY(swmm_table_index(window.modelLayer()->engine(), "OBS_NATIVE") >= 0);
        auto *registry = qobject_cast<openswmmvis::timeseries::TimeseriesRegistry *>(
            window.modelLayer()->ensureTimeseriesRegistry());
        QVERIFY(registry);
        QVERIFY(registry->findByName("OBS_NATIVE"));
        QCOMPARE(window.property("timeSeriesAssignments").toJsonArray().size(), 1);
        window.canvas()->undoStack()->undo();
        QCOMPARE(swmm_table_index(window.modelLayer()->engine(), "OBS_NATIVE"), -1);
        QVERIFY(window.property("timeSeriesAssignments").toJsonArray().isEmpty());
        window.canvas()->undoStack()->redo();
        QVERIFY(swmm_table_index(window.modelLayer()->engine(), "OBS_NATIVE") >= 0);
        QCOMPARE(window.property("timeSeriesAssignments").toJsonArray().size(), 1);
        QVERIFY(!openswmmvis::ui::applyPreparedTimeSeries(&window, prepared, &error));
        QCOMPARE(window.property("timeSeriesAssignments").toJsonArray().size(), 1);
        QVERIFY(swmm_table_index(window.modelLayer()->engine(), "OBS_NATIVE") >= 0);
        QVERIFY2(window.save(&error), qPrintable(error));
        SWMMVisProjectWindow reopened(workspace.get(), directory + "/model.inp");
        QList<QString> warnings, errors;
        QVERIFY(reopened.loadModel(warnings, errors));
        QVERIFY2(ProjectSerializer::applyFromFile(directory + "/model.oswp", &reopened,
                                                  &error, &warnings), qPrintable(error));
        QVERIFY(swmm_table_index(reopened.modelLayer()->engine(), "OBS_NATIVE") >= 0);
        QCOMPARE(reopened.property("timeSeriesAssignments").toJsonArray().size(), 1);
    }
    /** @brief Later model insertion wires rename/deletion and link changes immediately reconcile owned highlights. */
    void lateLoadedModelLinkLifecycle()
    {
        auto workspace = std::unique_ptr<OpenSWMMVisWorkspace>(
            OpenSWMMVisWorkspace::newInstance({}, nullptr));
        SWMMVisProjectWindow window(workspace.get());
        QVERIFY(initialize(window));
        auto *model = window.modelLayer();
        auto *canvas = window.canvas();
        QVERIFY(model->applyNodeAdd("OBS_LINK", 0, -77.0, 39.0));
        QCOMPARE(canvas->takeLayer(canvas->layers().indexOf(model), false), model);
        auto *layer = new TimeSeriesLayer;
        layer->setParent(&window);
        QVERIFY(layer->openTimeSeries(m_package));
        canvas->addLayer(layer, false);
        canvas->addLayer(model, false);
        const SWMMObjectRef original{SWMMObjectRef::Node, "OBS_LINK"};
        const SWMMObjectRef renamed{SWMMObjectRef::Node, "OBS_RENAMED"};
        window.selectionManager()->select(original);
        QString error;
        QVERIFY2(layer->attachToSelectedFeature({m_primary}, &error), qPrintable(error));
        QCOMPARE(layer->associationStatus(m_primary), QString("Linked"));

        // Clear user-owned spatial selection, then let the collection own its
        // linked highlight so detach must remove exactly that contribution.
        window.selectionManager()->select(QSet<SWMMObjectRef>{});
        layer->setSelectedSeriesIds({m_primary});
        QVERIFY(window.selectionManager()->selection().contains(original));
        layer->detachFeatures({m_primary});
        QVERIFY(!window.selectionManager()->selection().contains(original));
        QVERIFY(window.selectionManager()->selection().contains(layer->seriesRef(m_primary)));
        canvas->undoStack()->undo();
        QCOMPARE(layer->associationStatus(m_primary), QString("Linked"));
        QVERIFY(window.selectionManager()->selection().contains(original));

        QVERIFY(model->applyRename(original.name, renamed.name, SWMMModelLayer::kKindNode));
        QCOMPARE(layer->associationStatus(m_primary), QString("Linked"));
        QCOMPARE(layer->seriesForFeature(renamed, model), QStringList{m_primary});
        QVERIFY(layer->seriesForFeature(original, model).isEmpty());
        QVERIFY(window.selectionManager()->selection().contains(renamed));
        QVERIFY(!window.selectionManager()->selection().contains(original));

        canvas->undoStack()->push(new DeleteObjectCommand(
            model, renamed.name, DeleteObjectCommand::DeleteNode, canvas));
        QCOMPARE(layer->associationStatus(m_primary), QString("Unresolved link"));
        QVERIFY(!window.selectionManager()->selection().contains(renamed));
        canvas->undoStack()->undo();
        QCOMPARE(layer->associationStatus(m_primary), QString("Linked"));
        QVERIFY(window.selectionManager()->selection().contains(renamed));
        canvas->undoStack()->redo();
        QCOMPARE(layer->associationStatus(m_primary), QString("Unresolved link"));
        QVERIFY(model->applyNodeAdd(renamed.name, 0, -76.0, 38.0));
        QCOMPARE(layer->associationStatus(m_primary), QString("Unresolved link"));
        QVERIFY(layer->seriesForFeature(renamed, model).isEmpty());
        QVERIFY(layer->dataset());
    }
};
/** @brief Execute the independently declared target-binding cases in this QApplication. */
int runTimeSeriesAssignmentTransactionTests(int argc, char **argv);
/** @brief Share the full native application linkage across both observation acceptance groups. */
int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    TestTimeSeriesLayer layer;
    const int native = QTest::qExec(&layer, argc, argv);
    return native | runTimeSeriesAssignmentTransactionTests(argc, argv);
}
#include "test_timeserieslayer.moc"
