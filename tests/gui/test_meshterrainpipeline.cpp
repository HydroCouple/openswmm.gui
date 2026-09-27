// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/meshgenerationdialog.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"
#include "layers/swmmmodellayer.h"
#include "mesh/channelburnprofile.h"
#include "project/generatedmeshartifacts.h"
#include "ui/widgets/corridorsourceswidget.h"
#include "mesh/meshcellgeom.h"
#include "map/mapcanvas.h"
#include "map/spatialreferencesystem.h"
#include "layers/featurelayer.h"
#include <QApplication>
#include <QMessageBox>
#include <QLineF>
#include <QTimer>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QPromise>
#include <QSet>
#include <QTest>
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <vector>
#include <memory>

class TestMeshTerrainPipeline : public QObject
{
    Q_OBJECT
    using Inputs = MeshGenerationDialog::PipelineInputs;
    using Result = MeshGenerationDialog::PipelineResult;

    static Result run(Inputs inputs)
    {
        QPromise<Result> promise;
        auto future = promise.future();
        promise.start();
        MeshGenerationDialog::runMeshPipeline(promise, std::move(inputs));
        promise.finish();
        return future.resultCount() ? future.result() : Result{};
    }

    static bool writeBytes(const QString &path, const QByteArray &bytes)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }

    static bool prepareBurnFixture(const QDir &dir, Inputs &inputs, bool existing)
    {
        if (!QDir().mkpath(dir.path())) return false;
        inputs.inpPath = dir.absoluteFilePath("model.inp");
        inputs.dtmPath = dir.absoluteFilePath("source.tif");
        inputs.burnOutputDir = dir.absoluteFilePath("terrain");
        inputs.burnFingerprint = "fixture23";
        // Only this owned fixture's terrain outputs are reset between runs.
        if (QFileInfo::exists(inputs.burnOutputDir) &&
            !QDir(inputs.burnOutputDir).removeRecursively()) return false;
        if (existing) {
            if (!QDir().mkpath(inputs.burnOutputDir) ||
                !writeBytes(QDir(inputs.burnOutputDir).filePath("source_burned_fixture23.tif"),
                            "saved DEM sentinel\n") ||
                !writeBytes(QDir(inputs.burnOutputDir).filePath("source_burned_fixture23_burn_report.csv"),
                            "saved report sentinel\n")) return false;
        }
        if (!writeBytes(inputs.inpPath, "[TITLE]\nBurn staging fixture\n[OPTIONS]\nFLOW_UNITS CMS\n"))
            return false;
        GDALAllRegister();
        auto *driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        if (!driver) return false;
        auto *dataset = driver->Create(inputs.dtmPath.toUtf8().constData(), 40, 40, 1, GDT_Float32, nullptr);
        if (!dataset) return false;
        double transform[] = {-4, 1, 0, 36, 0, -1};
        const auto geoResult = dataset->SetGeoTransform(transform);
        std::vector<float> values(1600, 10.0f);
        const auto writeResult = dataset->GetRasterBand(1)->RasterIO(
            GF_Write, 0, 0, 40, 40, values.data(), 40, 40, GDT_Float32, 0, 0);
        GDALClose(dataset);
        if (geoResult != CE_None || writeResult != CE_None) return false;
        inputs.modelExtent = MapExtent(0, 0, 32, 32);
        inputs.domains = {QPolygonF(QVector<QPointF>{{0,0}, {32,0}, {32,32}, {0,32}})};
        inputs.auxPoints = {{{16, 28}, 10.0, true}};
        inputs.genOpts.maxArea = 64;
        inputs.meshLinearUnitName = "metre";
        inputs.mapNodesAfterGen = false;
        inputs.thinnerOpts.gridSpacing = 2;
        inputs.burnEnabled = true;
        inputs.burnOptions.forceHalfWidth = 1.0;
        inputs.burnOptions.maxHalfWidth = 4.0;
        inputs.burnOptions.clipToBanks = false;
        inputs.burnOptions.chainageStep = 4.0;
        inputs.burnOptions.stringCount = 1;
        inputs.burnOptions.quadCorridor = false;
        inputs.burnOptions.emitStrings = false;
        mesh::ChannelInput channel;
        channel.conduitId = "CREEK";
        channel.centerline = {{4, 16}, {28, 16}};
        channel.zUp = 8.0;
        channel.zDn = 7.0;
        channel.section = mesh::sectionFromWidths({0.0, 1.0, 2.0}, {4.0, 6.0, 8.0});
        auto profile = mesh::buildBurnProfile(channel, inputs.burnOptions);
        if (!profile.isValid()) return false;
        inputs.burnProfiles = {profile};
        return true;
    }

    static bool prepareCorridorFixture(const QDir &dir, Inputs &inputs)
    {
        if (!prepareBurnFixture(dir, inputs, true)) return false;
        inputs.burnEnabled = false;
        inputs.dtmPath.clear();
        inputs.genOpts.minAngle = 0;
        OGRSpatialReference srs;
        if (srs.importFromEPSG(3857) != OGRERR_NONE) return false;
        char *wkt = nullptr;
        srs.exportToWkt(&wkt);
        inputs.meshCRSWkt = QString::fromUtf8(wkt);
        CPLFree(wkt);
        mesh::CorridorSource source;
        source.path = dir.absoluteFilePath("corridor.geojson");
        source.layerName = "corridors";
        source.sourceCRSWkt = inputs.meshCRSWkt;
        source.meshCRSWkt = inputs.meshCRSWkt;
        source.featureIds = {7};
        source.width = 4;
        source.across = 2;
        source.along = 6;
        source.tag = "river";
        if (!writeBytes(source.path, R"({"type":"FeatureCollection","name":"corridors","crs":{"type":"name","properties":{"name":"EPSG:3857"}},"features":[{"type":"Feature","id":7,"properties":{"width":4},"geometry":{"type":"LineString","coordinates":[[4,16],[28,16]]}}]})")) return false;
        inputs.corridorSources = {source};
        return true;
    }

private slots:
    void directionalMappedLayer_data()
    {
        QTest::addColumn<QString>("attributes");
        QTest::addColumn<bool>("valid");
        QTest::newRow("directional") << QString("\"quad_h_along\":6,\"quad_h_across\":2,\"quad_axis\":0") << true;
        QTest::newRow("partial") << QString("\"quad_h_along\":6") << false;
        QTest::newRow("text-spacing") << QString("\"quad_h_along\":\"six\",\"quad_h_across\":2,\"quad_axis\":0") << false;
        QTest::newRow("null-spacing") << QString("\"quad_h_along\":6,\"quad_h_across\":null,\"quad_axis\":0") << false;
        QTest::newRow("ambiguous-axis") << QString("\"quad_h_along\":6,\"quad_h_across\":2,\"quad_axis\":45") << false;
        for (const char *name : {"missing-source-crs", "missing-source-file", "quad-background", "empty-multipart"})
            QTest::newRow(name) << QString("\"quad_h_along\":6,\"quad_h_across\":2,\"quad_axis\":0")
                               << (QString::fromLatin1(name) == "quad-background");
        QTest::newRow("all-null") << QString("\"quad_h_along\":null,\"quad_h_across\":null,\"quad_axis\":null") << true;
    }

    void directionalMappedLayer()
    {
        QFETCH(QString, attributes);
        QFETCH(bool, valid);
        Inputs inputs;
        const QDir dir(qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT") + "/directional_mapped/" + QTest::currentDataTag());
        QVERIFY(prepareCorridorFixture(dir, inputs));
        inputs.corridorSources.clear();
        const QString path = dir.absoluteFilePath("mapped.geojson");
        const QString scenario = QString::fromLatin1(QTest::currentDataTag());
        QByteArray json = QString(R"({"type":"FeatureCollection","name":"regions","crs":{"type":"name","properties":{"name":"EPSG:3857"}},"features":[{"type":"Feature","properties":{"quad_mode":"mapped","tag":"directed",%1},"geometry":{"type":"Polygon","coordinates":[[[4,12],[28,12],[28,20],[4,20],[4,12]]]}}]})").arg(attributes).toUtf8();
        if (scenario == "empty-multipart")
            json.replace("\"type\":\"Polygon\",\"coordinates\":[[[4,12],[28,12],[28,20],[4,20],[4,12]]]",
                         "\"type\":\"MultiPolygon\",\"coordinates\":[]");
        QVERIFY(writeBytes(path, json));
        inputs.quadRegionDefaults.spacing = 2;
        inputs.quadRegionLayers = {{path, "regions", inputs.meshCRSWkt}};
        if (scenario == "missing-source-crs") inputs.quadRegionLayers[0].crsWkt.clear();
        if (scenario == "missing-source-file") inputs.quadRegionLayers[0].path += ".missing";
        if (scenario == "quad-background") {
            inputs.quadEverywhere = true;
            inputs.quadEverywhereSpacing = 8;
        }
        const auto generated = run(inputs);
        if (!valid) {
            QVERIFY2(!generated.ok, "An explicit invalid directional request must not silently generate an isotropic mesh.");
            QVERIFY(!generated.errorMsg.isEmpty());
            QVERIFY(!QFileInfo::exists(dir.absoluteFilePath("model.2dm")));
            return;
        }
        QVERIFY2(generated.ok, qPrintable(generated.errorMsg));
        int cells = 0;
        double totalArea = 0;
        for (const auto &cell : generated.meshResult.triangles) {
            totalArea += mesh::cellGeom(generated.meshResult.vertices, cell).area;
            if (cell.tag != "directed") continue;
            QVERIFY(cell.isQuad());
            ++cells;
            const QPointF a = generated.meshResult.vertices[cell.v0].xy;
            const QPointF b = generated.meshResult.vertices[cell.v1].xy;
            const QPointF c = generated.meshResult.vertices[cell.v2].xy;
            const double ab = QLineF(a,b).length(), bc = QLineF(b,c).length();
            QVERIFY(std::abs(std::max(ab,bc) - (scenario == "all-null" ? 2.0 : 6.0)) < 1e-8);
            QVERIFY(std::abs(std::min(ab,bc) - 2.0) < 1e-8);
        }
        QCOMPARE(cells, scenario == "all-null" ? 48 : 16);
        QVERIFY(std::abs(totalArea - 1024.0) < 1e-7);
        QVERIFY(!QFileInfo::exists(generated.meshPath));
    }

    void corridorDraftBindsCurrentMeshCRSWithoutChangingSavedRecipe()
    {
        Inputs fixtureInputs;
        const QDir dir(qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT") + "/gis_draft");
        QVERIFY(prepareCorridorFixture(dir, fixtureInputs));
        auto workspace = std::unique_ptr<OpenSWMMVisWorkspace>(OpenSWMMVisWorkspace::newInstance(QString(), nullptr));
        const QString fixture = QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", ".")).filePath("typed_selection_fixture.inp");
        auto window = std::make_unique<SWMMVisProjectWindow>(workspace.get(), fixture, nullptr);
        QList<QString> warnings, errors;
        QVERIFY2(window->loadModel(warnings, errors), qPrintable(errors.join('\n')));
        window->modelLayer()->setSRS(SpatialReferenceSystem::fromWktOrProj(fixtureInputs.meshCRSWkt), true);
        MeshGenerationDialog dialog(window.get(), nullptr);
        auto draft = fixtureInputs.corridorSources;
        draft[0].meshCRSWkt.clear();
        dialog.m_corridorSources->setSources(draft);
        Inputs collected;
        QString error;
        QVERIFY2(dialog.collectInputs(&collected, &error), qPrintable(error));
        QCOMPARE(collected.corridorSources.size(), 1);
        QVERIFY(!collected.meshCRSWkt.isEmpty());
        QCOMPARE(collected.corridorSources.first().meshCRSWkt, collected.meshCRSWkt);
        QVERIFY(window->corridorSources().isEmpty());
        dialog.reject();
        QVERIFY(window->corridorSources().isEmpty());
    }

    void selectedCorridorReachesWorkerWithoutBurn_data()
    {
        QTest::addColumn<bool>("backgroundQuads");
        QTest::newRow("triangular-background") << false;
        QTest::newRow("quad-background") << true;
    }

    void selectedCorridorReachesWorkerWithoutBurn()
    {
        QFETCH(bool, backgroundQuads);
        const QString root = qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT");
        const QDir dir(root + "/gis_corridor/" + QTest::currentDataTag());
        Inputs inputs;
        QVERIFY(prepareCorridorFixture(dir, inputs));
        inputs.quadEverywhere = backgroundQuads;
        inputs.quadEverywhereSpacing = 8;
        const auto generated = run(inputs);
        QVERIFY2(generated.ok, qPrintable(generated.errorMsg));
        QVERIFY(!generated.burnRan);
        QCOMPARE(generated.corridorSources.size(), 1);
        QVERIFY(!generated.corridorSources.first().geometryDigest.isEmpty());
        QVERIFY(!generated.corridorSourceStamps.isEmpty());
        QVERIFY(mesh::corridorSourceFilesUnchanged(generated.corridorSourceStamps));
        int corridorCells = 0;
        double area = 0;
        for (const auto &cell : generated.meshResult.triangles) {
            area += mesh::cellGeom(generated.meshResult.vertices, cell).area;
            if (cell.tag == "river") {
                QVERIFY(cell.isQuad());
                ++corridorCells;
                const QPointF a = generated.meshResult.vertices[cell.v0].xy;
                const QPointF b = generated.meshResult.vertices[cell.v1].xy;
                const QPointF c = generated.meshResult.vertices[cell.v2].xy;
                const double first = QLineF(a,b).length(), second = QLineF(b,c).length();
                QVERIFY(std::abs(std::max(first, second) - 6.0) < 1e-8);
                QVERIFY(std::abs(std::min(first, second) - 2.0) < 1e-8);
            }
        }
        QCOMPARE(corridorCells, 8);
        QVERIFY(std::abs(area - 1024.0) < 1e-7);
        QVERIFY(!QFileInfo::exists(generated.meshPath));
        inputs.corridorSources = generated.corridorSources;
        inputs.corridorSources[0].featureIds = {999};
        const auto missing = run(inputs);
        QVERIFY(!missing.ok);
        QVERIFY(!missing.errorMsg.isEmpty());
        QVERIFY(missing.corridorSources.isEmpty());
    }

    void corridorRecipeAdoptedOnlyOnSuccess_data()
    {
        QTest::addColumn<QString>("outcome");
        QTest::newRow("success") << QString("success");
        QTest::newRow("stale-project") << QString("stale");
        QTest::newRow("source-changed-after-worker") << QString("source");
        QTest::newRow("failed-worker") << QString("failure");
        QTest::newRow("cancel-dialog") << QString("cancel");
    }

    void corridorRecipeAdoptedOnlyOnSuccess()
    {
        QFETCH(QString, outcome);
        Inputs inputs;
        const QDir dir(qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT") + "/gis_adoption/" + outcome);
        QVERIFY(prepareCorridorFixture(dir, inputs));
        auto result = run(inputs);
        QVERIFY2(result.ok, qPrintable(result.errorMsg));
        auto workspace = std::unique_ptr<OpenSWMMVisWorkspace>(OpenSWMMVisWorkspace::newInstance(QString(), nullptr));
        const QString fixture = QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", ".")).filePath("typed_selection_fixture.inp");
        auto window = std::make_unique<SWMMVisProjectWindow>(workspace.get(), fixture, nullptr);
        QList<QString> warnings, errors;
        QVERIFY2(window->loadModel(warnings, errors), qPrintable(errors.join('\n')));
        auto old = inputs.corridorSources.first();
        old.tag = "previous";
        window->setCorridorSources({old});
        window->setCorridorRecipeLoadError("unsupported test recipe");
        MeshGenerationDialog dialog(window.get(), nullptr);
        dialog.m_corridorSources->setSources(inputs.corridorSources);
        if (outcome == "cancel") {
            dialog.reject();
            QCOMPARE(window->corridorSources().first().tag, QString("previous"));
            QVERIFY(!window->corridorRecipeLoadError().isEmpty());
            return;
        }
        dialog.beginGenerationGuard();
        if (outcome == "stale") window->modelLayer()->markEdited();
        if (outcome == "source") {
            QFile file(inputs.corridorSources.first().path);
            QVERIFY(file.open(QIODevice::Append));
            QCOMPARE(file.write("\n"), 1);
            file.close();
        }
        if (outcome == "failure") { result.ok = false; result.errorMsg = "test refusal"; }
        QPromise<Result> promise;
        promise.start();
        promise.addResult(result);
        promise.finish();
        dialog.m_watcher = new QFutureWatcher<Result>(&dialog);
        dialog.m_watcher->setFuture(promise.future());
        if (outcome != "success")
            QTimer::singleShot(0, [] {
                for (auto *widget : QApplication::topLevelWidgets())
                    if (auto *box = qobject_cast<QMessageBox *>(widget)) box->accept();
            });
        dialog.onMeshFinished();
        QCOMPARE(window->corridorSources().first().tag, outcome == "success" ? QString("river") : QString("previous"));
        if (outcome == "success") QVERIFY(!window->corridorSources().first().geometryDigest.isEmpty());
        QCOMPARE(window->corridorRecipeLoadError().isEmpty(), outcome == "success");
    }

    void requestedCorridorFailureStopsGeneration_data()
    {
        QTest::addColumn<bool>("invalidLattice");
        QTest::newRow("invalid-lattice") << true;
        QTest::newRow("degenerate-quad-corridor") << false;
    }
    void requestedCorridorFailureStopsGeneration()
    {
        QFETCH(bool, invalidLattice);
        const QString root = qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT",
            QDir::current().filePath("terrain_pipeline_output"));
        const QDir dir(root + "/corridor_safety/" + QString::fromLatin1(QTest::currentDataTag()));
        Inputs inputs;
        QVERIFY(prepareBurnFixture(dir, inputs, true));
        // No elevation burn is needed to reproduce the corridor fallback.
        inputs.dtmPath.clear();
        inputs.burnOptions.quadCorridor = true;
        if (invalidLattice) inputs.burnProfiles[0].offsets.clear();
        else inputs.burnProfiles[0].offsets.fill(0.0);
        QFile model(inputs.inpPath);
        QVERIFY(model.open(QIODevice::ReadOnly));
        const auto saved = model.readAll();
        model.close();
        const auto result = run(inputs);
        QVERIFY2(!result.ok, "An explicitly requested invalid corridor must stop generation.");
        QVERIFY2(result.errorMsg.contains("CREEK"), qPrintable(result.errorMsg));
        QVERIFY(model.open(QIODevice::ReadOnly));
        QCOMPARE(model.readAll(), saved);
    }
    void burnKeepsSavedFilesUntilSave_data()
    {
        QTest::addColumn<bool>("existing");
        QTest::addColumn<bool>("laterFailure");
        QTest::newRow("existing-success") << true << false;
        QTest::newRow("new-success") << false << false;
        QTest::newRow("existing-later-failure") << true << true;
        QTest::newRow("new-later-failure") << false << true;
    }

    void burnKeepsSavedFilesUntilSave()
    {
        QFETCH(bool, existing);
        QFETCH(bool, laterFailure);
        const QString root = qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT",
            QDir::current().filePath("terrain_pipeline_output"));
        const QDir dir(root + "/burn_staging/" + QString::fromLatin1(QTest::currentDataTag()));
        Inputs inputs;
        QVERIFY(prepareBurnFixture(dir, inputs, existing));
        const QString finalDem = QDir(inputs.burnOutputDir).filePath("source_burned_fixture23.tif");
        const QString finalReport = QDir(inputs.burnOutputDir).filePath("source_burned_fixture23_burn_report.csv");
        QFile sourceBefore(inputs.dtmPath);
        QVERIFY(sourceBefore.open(QIODevice::ReadOnly));
        const auto originalSource = sourceBefore.readAll();
        sourceBefore.close();
        if (laterFailure) {
            // The burn uses its own raster resolution. Terrain thinning later
            // rejects this width before allocation, after both burn outputs exist.
            inputs.doThinning = true;
            inputs.thinnerOpts.gridSpacing = 1e-12;
        }
        auto generated = run(inputs);
        if (laterFailure) {
            QVERIFY(!generated.ok);
            QVERIFY2(generated.errorMsg.contains("grid is too wide"), qPrintable(generated.errorMsg));
            QVERIFY(!generated.generatedArtifacts);
            QVERIFY(dir.entryList({".openswmm-generation-*"},
                                  QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
        } else {
            QVERIFY2(generated.ok, qPrintable(generated.errorMsg));
            QVERIFY(generated.burnRan);
            QVERIFY(generated.burnStats.pixelsLowered > 0);
            QCOMPARE(generated.burnedDemPath, finalDem);
            QCOMPARE(generated.burnReportPath, finalReport);
        }
        if (existing) {
            QFile dem(finalDem), report(finalReport);
            QVERIFY(dem.open(QIODevice::ReadOnly));
            QVERIFY(report.open(QIODevice::ReadOnly));
            QCOMPARE(dem.readAll(), QByteArray("saved DEM sentinel\n"));
            QCOMPARE(report.readAll(), QByteArray("saved report sentinel\n"));
        } else {
            QVERIFY2(!QFileInfo::exists(finalDem), "Burn published a DEM before project Save");
            QVERIFY2(!QFileInfo::exists(finalReport), "Burn published a report before project Save");
            QVERIFY2(!QFileInfo::exists(inputs.burnOutputDir), "Burn created the final terrain directory before Save");
        }
        QFile sourceAfter(inputs.dtmPath);
        QVERIFY(sourceAfter.open(QIODevice::ReadOnly));
        QCOMPARE(sourceAfter.readAll(), originalSource);
        if (!laterFailure) {
            QVERIFY(generated.generatedArtifacts);
            const QString jobDirectory = generated.generatedArtifacts->directoryPath();
            QVERIFY(QFileInfo(jobDirectory).isDir());
            QCOMPARE(generated.generatedArtifacts->entries().size(), 2);
            QString physicalDem, physicalReport;
            for (const auto &entry : generated.generatedArtifacts->entries()) {
                QVERIFY(QFileInfo(entry.stagedPath).isFile());
                QVERIFY(QFileInfo(entry.stagedPath).size() > 0);
                QCOMPARE(QFileInfo(entry.stagedPath).absolutePath(), jobDirectory);
                QVERIFY(entry.finalPath != entry.stagedPath);
                if (entry.finalPath == finalDem) physicalDem = entry.stagedPath;
                else if (entry.finalPath == finalReport) physicalReport = entry.stagedPath;
                else QFAIL("Unexpected generated artifact destination");
            }
            QVERIFY(!physicalDem.isEmpty());
            QVERIFY(!physicalReport.isEmpty());
            QFile report(physicalReport);
            QVERIFY(report.open(QIODevice::ReadOnly));
            const auto reportBytes = report.readAll();
            report.close();
            QVERIFY(reportBytes.contains(("# burned DEM," + finalDem + "\n").toUtf8()));
            QVERIFY(!reportBytes.contains(jobDirectory.toUtf8()));
            QVERIFY(!reportBytes.contains(".openswmm-generation-"));
            auto *burned = static_cast<GDALDataset *>(GDALOpen(physicalDem.toUtf8().constData(), GA_ReadOnly));
            QVERIFY(burned);
            double bed = 0;
            const auto sampled = burned->GetRasterBand(1)->RasterIO(
                GF_Read, 20, 19, 1, 1, &bed, 1, 1, GDT_Float64, 0, 0);
            GDALClose(burned);
            QCOMPARE(sampled, CE_None);
            QVERIFY(bed < 9.0); // proves the physical stage is the burned DEM

            // Keep review copies outside the final terrain destination while
            // deliberately testing deletion of the private generation job.
            const QString reviewDem = dir.filePath("generated_dem_for_review.tif");
            if (QFileInfo::exists(reviewDem)) QVERIFY(QFile::remove(reviewDem));
            QVERIFY(QFile::copy(physicalDem, reviewDem));
            QVERIFY(writeBytes(dir.filePath("generated_report_for_review.csv"), reportBytes));
            std::weak_ptr<GeneratedMeshArtifacts> observer = generated.generatedArtifacts;
            Result queuedCopy = generated;
            generated = Result{};
            QVERIFY(!observer.expired());
            QVERIFY(QFileInfo::exists(physicalDem));
            queuedCopy = Result{};
            QVERIFY(observer.expired());
            QVERIFY(!QFileInfo::exists(jobDirectory));
            QVERIFY(dir.entryList({".openswmm-generation-*"},
                                  QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
        }
    }

    void canceledBurnDropsOwnedStage_data()
    {
        QTest::addColumn<bool>("existing");
        QTest::addColumn<bool>("afterComputation");
        QTest::newRow("early-existing") << true << false;
        QTest::newRow("early-new") << false << false;
        QTest::newRow("queued-result-existing") << true << true;
        QTest::newRow("queued-result-new") << false << true;
    }

    void canceledBurnDropsOwnedStage()
    {
        QFETCH(bool, existing);
        QFETCH(bool, afterComputation);
        const QString root = qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT",
            QDir::current().filePath("terrain_pipeline_output"));
        const QDir dir(root + "/burn_cancellation/" + QString::fromLatin1(QTest::currentDataTag()));
        Inputs inputs;
        QVERIFY(prepareBurnFixture(dir, inputs, existing));
        const QString finalDem = QDir(inputs.burnOutputDir).filePath("source_burned_fixture23.tif");
        const QString finalReport = QDir(inputs.burnOutputDir).filePath("source_burned_fixture23_burn_report.csv");
        QString jobDirectory;
        std::weak_ptr<GeneratedMeshArtifacts> observer;
        {
            QPromise<Result> promise;
            auto future = promise.future();
            promise.start();
            if (!afterComputation) future.cancel();
            MeshGenerationDialog::runMeshPipeline(promise, inputs);
            if (afterComputation) {
                QCOMPARE(future.resultCount(), 1);
                // Inspect by reference: no GUI-adopted copy owns the outputs.
                // Cancel occurs after computation but before queued completion.
                const Result &pending = *future.begin();
                QVERIFY2(pending.ok, qPrintable(pending.errorMsg));
                QVERIFY(pending.generatedArtifacts);
                jobDirectory = pending.generatedArtifacts->directoryPath();
                observer = pending.generatedArtifacts;
                QVERIFY(QFileInfo(jobDirectory).isDir());
                future.cancel();
            } else {
                QCOMPARE(future.resultCount(), 0);
            }
            QVERIFY(future.isCanceled());
            promise.finish();
        }
        QVERIFY(observer.expired());
        QVERIFY(jobDirectory.isEmpty() || !QFileInfo::exists(jobDirectory));
        QVERIFY(dir.entryList({".openswmm-generation-*"},
                              QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
        if (existing) {
            QFile dem(finalDem), report(finalReport);
            QVERIFY(dem.open(QIODevice::ReadOnly));
            QVERIFY(report.open(QIODevice::ReadOnly));
            QCOMPARE(dem.readAll(), QByteArray("saved DEM sentinel\n"));
            QCOMPARE(report.readAll(), QByteArray("saved report sentinel\n"));
        } else {
            QVERIFY(!QFileInfo::exists(finalDem));
            QVERIFY(!QFileInfo::exists(finalReport));
            QVERIFY(!QFileInfo::exists(inputs.burnOutputDir));
        }
    }

    void generationOwnershipGuard_data()
    {
        QTest::addColumn<QString>("change");
        QTest::newRow("already-dirty-model-edit") << QString("edit");
        QTest::newRow("options-edit") << QString("options");
        QTest::newRow("feature-edit") << QString("features");
        QTest::newRow("feature-schema-edit") << QString("schema");
        QTest::newRow("owner-destroyed") << QString("destroy");
        QTest::newRow("owner-closing") << QString("owner-close");
        QTest::newRow("reject-cancels") << QString("reject");
        QTest::newRow("close-cancels") << QString("close");
    }

    void generationOwnershipGuard()
    {
        QFETCH(QString, change);
        auto workspace = std::unique_ptr<OpenSWMMVisWorkspace>(
            OpenSWMMVisWorkspace::newInstance(QString(), nullptr));
        const QString fixture = QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", "."))
            .filePath("typed_selection_fixture.inp");
        auto window = std::make_unique<SWMMVisProjectWindow>(workspace.get(), fixture, nullptr);
        QList<QString> warnings, errors;
        QVERIFY2(window->loadModel(warnings, errors), qPrintable(errors.join('\n')));
        window->setHasChanges(true);
        auto *features = new FeatureLayer(workspace.get());
        window->canvas()->addLayer(features);
        MeshGenerationDialog dialog(window.get(), nullptr);
        dialog.beginGenerationGuard();
        QVERIFY(dialog.generationOwnerIsCurrent());
        if (change == "edit") {
            window->modelLayer()->markEdited();
        } else if (change == "options") {
            QVERIFY(window->modelLayer()->setOption("ALLOW_PONDING", "YES"));
        } else if (change == "features") {
            features->featuresChanged({7});
        } else if (change == "schema") {
            features->schemaChanged();
        } else if (change == "destroy") {
            window.reset();
            QVERIFY(dialog.m_pw.isNull());
        } else {
            QPromise<Result> promise;
            promise.start();
            auto future = promise.future();
            dialog.m_watcher = new QFutureWatcher<Result>(&dialog);
            dialog.m_watcher->setFuture(future);
            if (change == "owner-close") window->aboutToClose();
            else if (change == "reject") dialog.reject();
            else {
                QCloseEvent event;
                dialog.closeEvent(&event);
            }
            QVERIFY(future.isCanceled());
            promise.finish();
        }
        QVERIFY(!dialog.generationOwnerIsCurrent());
    }

    void generationKeepsSavedFilesUntilSave_data()
    {
        QTest::addColumn<bool>("external");
        QTest::addColumn<bool>("existingMesh");
        QTest::addColumn<bool>("defaultDestination");
        QTest::newRow("inline") << false << true << false;
        QTest::newRow("external-existing") << true << true << false;
        QTest::newRow("external-new") << true << false << false;
        QTest::newRow("external-default-new") << true << false << true;
    }

    void generationKeepsSavedFilesUntilSave()
    {
        QFETCH(bool, external);
        QFETCH(bool, existingMesh);
        QFETCH(bool, defaultDestination);
        const QString root = qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT",
            QDir::current().filePath("terrain_pipeline_output"));
        const QDir dir(root + "/generation_staging/" + QString::fromLatin1(QTest::currentDataTag()));
        QVERIFY(QDir().mkpath(dir.path()));
        Inputs inputs;
        inputs.inpPath = dir.absoluteFilePath("model.inp");
        const QString finalMesh = dir.absoluteFilePath(defaultDestination ? "model.2dm" : "mesh.2dm");
        inputs.meshOutputPath = defaultDestination ? QString() : finalMesh;
        inputs.outputMode = external ? mesh::MeshOutputMode::External : mesh::MeshOutputMode::Inline;
        inputs.modelExtent = MapExtent(0, 0, 32, 32);
        inputs.domains = {QPolygonF(QVector<QPointF>{{0,0}, {32,0}, {32,32}, {0,32}})};
        inputs.auxPoints = {{{16, 16}, 3.0, true}};
        inputs.genOpts.maxArea = 64;
        inputs.meshLinearUnitName = "metre";
        inputs.mapNodesAfterGen = false;
        inputs.includeSubcatch = true;
        inputs.subcatchSeeds = {{"J1", {16, 16}}};
        inputs.manningsN = 0.045;
        inputs.initDepth = 0.7;
        const QByteArray modelBytes("[TITLE]\nSaved model sentinel\n[OPTIONS]\nFLOW_UNITS CMS\n");
        const QByteArray meshBytes("saved mesh sentinel\n");
        {
            QFile model(inputs.inpPath);
            QVERIFY(model.open(QIODevice::WriteOnly));
            QCOMPARE(model.write(modelBytes), modelBytes.size());
        }
        if (QFileInfo::exists(finalMesh)) QVERIFY(QFile::remove(finalMesh));
        if (existingMesh) {
            QFile file(finalMesh);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(meshBytes), meshBytes.size());
        }
        const auto generated = run(inputs);
        QVERIFY2(generated.ok, qPrintable(generated.errorMsg));
        QVERIFY(!generated.meshResult.vertices.isEmpty());
        QVERIFY(!generated.meshResult.triangles.isEmpty());
        QCOMPARE(generated.meshResult.cellCouplings.size(), generated.meshResult.triangles.size());
        QVERIFY(generated.coupling.triangleToNode.isEmpty());
        QSet<int> coupledCells;
        for (const auto &coupling : generated.meshResult.cellCouplings) {
            QCOMPARE(coupling.nodeId, QString("J1"));
            QVERIFY(!coupledCells.contains(coupling.tri));
            coupledCells.insert(coupling.tri);
        }
        for (const auto &cell : generated.meshResult.triangles) {
            QCOMPARE(cell.mannings, inputs.manningsN);
            QCOMPARE(cell.initDepth, inputs.initDepth);
        }
        QFile model(inputs.inpPath);
        QVERIFY(model.open(QIODevice::ReadOnly));
        QCOMPARE(model.readAll(), modelBytes);
        if (existingMesh) {
            QFile file(finalMesh);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), meshBytes);
        } else {
            QVERIFY2(!QFileInfo::exists(finalMesh), "Generate published a mesh before project Save");
        }
        QCOMPARE(generated.meshPath, external ? finalMesh : QString());
    }

    void retainedTerrainControlsQuadSizing_data()
    {
        QTest::addColumn<bool>("thinning");
        QTest::newRow("raw-sampling") << false;
        QTest::newRow("thinner-sampling") << true;
    }

    void retainedTerrainControlsQuadSizing()
    {
        QFETCH(bool, thinning);
        const QString root = qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT",
            QDir::current().filePath("terrain_pipeline_output"));
        const QDir dir(root + "/" + QString::fromLatin1(QTest::currentDataTag()));
        QVERIFY(QDir().mkpath(dir.path()));
        GDALAllRegister();
        auto *driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        QVERIFY(driver);
        const QString raster = dir.filePath("flat.tif");
        auto *dataset = driver->Create(raster.toUtf8().constData(), 40, 40, 1, GDT_Float32, nullptr);
        QVERIFY(dataset);
        double transform[] = {-4, 1, 0, 36, 0, -1};
        QCOMPARE(dataset->SetGeoTransform(transform), CE_None);
        std::vector<float> values(1600, 0.0f);
        const auto writeResult = dataset->GetRasterBand(1)->RasterIO(
            GF_Write, 0, 0, 40, 40, values.data(), 40, 40, GDT_Float32, 0, 0);
        GDALClose(dataset);
        QCOMPARE(writeResult, CE_None);

        Inputs inputs;
        inputs.inpPath = dir.filePath("model.inp");
        QFile model(inputs.inpPath);
        QVERIFY(model.open(QIODevice::WriteOnly));
        const QByteArray deck("[TITLE]\nTerrain handoff fixture\n[OPTIONS]\nFLOW_UNITS CMS\n");
        QCOMPARE(model.write(deck), deck.size());
        model.close();
        inputs.modelExtent = MapExtent(0, 0, 32, 32);
        inputs.domains = {QPolygonF(QVector<QPointF>{{0,0}, {32,0}, {32,32}, {0,32}})};
        inputs.auxPoints = {{{16, 16}, 0.0, true}}; // valid flat elevation fallback without a DEM
        inputs.meshOutputPath = dir.filePath("coarse.2dm");
        inputs.meshLinearUnitName = "metre";
        inputs.quadEverywhere = true;
        inputs.sizeGradation = 0.25;
        inputs.genOpts.maxArea = 64;
        inputs.mapNodesAfterGen = false;
        inputs.doThinning = thinning;
        inputs.thinnerOpts.gridSpacing = 1;
        inputs.thinnerOpts.useMinSpacing = false;
        inputs.thinnerOpts.normalDotThreshold = 2; // retain the flat sampling lattice
        const auto coarse = run(inputs);
        QVERIFY2(coarse.ok, qPrintable(coarse.errorMsg));
        inputs.dtmPath = raster;
        inputs.meshOutputPath = dir.filePath("terrain.2dm");
        const auto terrain = run(inputs);
        QVERIFY2(terrain.ok, qPrintable(terrain.errorMsg));
        QVERIFY2(terrain.meshResult.triangles.size() > 2 * coarse.meshResult.triangles.size(),
            qPrintable(QString("Terrain handoff did not refine quads: coarse=%1 terrain=%2")
                .arg(coarse.meshResult.triangles.size()).arg(terrain.meshResult.triangles.size())));
        QVERIFY(terrain.meshResult.quadCount() > coarse.meshResult.quadCount());

        // Same project/raster/options exercises the Stage-B cache hit. It
        // must still feed accepted terrain to sizing after candidate filtering.
        inputs.meshOutputPath = dir.filePath("cached.2dm");
        const auto cached = run(inputs);
        QVERIFY2(cached.ok, qPrintable(cached.errorMsg));
        QCOMPARE(cached.meshResult.triangles.size(), terrain.meshResult.triangles.size());
        QCOMPARE(cached.meshResult.quadCount(), terrain.meshResult.quadCount());
        QCOMPARE(cached.meshResult.vertices.size(), terrain.meshResult.vertices.size());
        for (qsizetype i = 0; i < terrain.meshResult.vertices.size(); ++i)
            QCOMPARE(cached.meshResult.vertices[i].xy, terrain.meshResult.vertices[i].xy);
    }
};

QTEST_MAIN(TestMeshTerrainPipeline)
#include "test_meshterrainpipeline.moc"
