// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/channelburndialog.h"
#include "core/unitsystem.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/spatialreferencesystem.h"
#include "mesh/inpmeshreader.h"
#include "mesh/inpmeshwriter.h"
#include "project/generatedmeshartifacts.h"
#include "project/projectserializer.h"
#include "mesh/meshcellgeom.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QProgressBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QPromise>
#include <QtConcurrent/QtConcurrentRun>
#include <QTest>
#include <QTimer>

class TestChannelBurnDialog : public QObject
{
    Q_OBJECT
    static QString outputTemplate()
    {
        const QString root=QDir::current().filePath("tests/output/channel_burn_progress");
        QDir().mkpath(root);
        return root+"/run-XXXXXX";
    }
    QTemporaryDir directory{outputTemplate()};
    OpenSWMMVisWorkspace *workspace = nullptr;
    SWMMVisProjectWindow *window = nullptr;
    SWMM2DMeshLayer *old = nullptr;
    QStringList errors;
    QTimer dismissMessages;
private slots:
    void workerProgressAndLateUpdatesRespectCancellation()
    {
        QPromise<ChannelBurnDialog::Result> promise;
        promise.start(); promise.setProgressRange(0,100);
        ChannelBurnDialog dialog(window);
        dialog.beginGuard();
        dialog.m_cancelled=std::make_shared<std::atomic_bool>(false);
        dialog.setBusy(true);
        dialog.m_watcher.setFuture(promise.future());
        auto worker=QtConcurrent::run([&promise] {
            promise.setProgressValueAndText(42,QStringLiteral("Stitching channel cavity…"));
        });
        worker.waitForFinished();
        auto *bar=dialog.findChild<QProgressBar *>("channelBurnProgress");
        QTRY_COMPARE(bar->value(),42);
        QTRY_COMPARE(dialog.m_status->text(),QString("Stitching channel cavity…"));
        dialog.show();
        dialog.grab().save(directory.filePath("progress-running.png"));
        dialog.cancelBurn();
        promise.setProgressValueAndText(95,QStringLiteral("Late worker update"));
        QCoreApplication::processEvents();
        QCOMPARE(bar->value(),42); QCOMPARE(bar->format(),QString("Stopping…"));
        QVERIFY(dialog.m_status->text().contains("Stopping"));
        promise.addResult(ChannelBurnDialog::Result{}); promise.finish();
        QTRY_VERIFY(!dialog.m_busy);
        QCOMPARE(bar->format(),QString("Cancelled")); QVERIFY(bar->value()<100);
        dialog.m_watcher.progressValueChanged(100);
        dialog.m_watcher.progressTextChanged(QStringLiteral("Stale update"));
        QCOMPARE(bar->format(),QString("Cancelled")); QVERIFY(bar->value()<100);
        QVERIFY(dialog.m_status->text().contains("cancelled"));
        QCOMPARE(window->canvas()->undoStack()->count(),0);
    }
    void progressTracksSuccessAndFailureRetry()
    {
        ChannelBurnDialog dialog(window);
        auto *bar=dialog.findChild<QProgressBar *>("channelBurnProgress");
        QVERIFY(bar); QVERIFY(bar->isHidden()); QVERIFY(!bar->accessibleName().isEmpty());
        dialog.m_maxCells->setValue(1); // Force a real worker failure.
        dialog.startBurn();
        QVERIFY(!bar->isHidden()); QCOMPARE(bar->maximum(),100); QCOMPARE(bar->value(),0);
        QTRY_VERIFY_WITH_TIMEOUT(!dialog.m_busy,30000);
        QVERIFY(bar->value()<100); QCOMPARE(bar->format(),QString("Failed"));
        QVERIFY(!errors.isEmpty()); errors.clear();
        dialog.m_maxCells->setValue(2000000);
        QSignalSpy updates(bar,&QProgressBar::valueChanged);
        dialog.startBurn();
        QCOMPARE(bar->value(),0); QCOMPARE(bar->format(),QString("%p%"));
        QTRY_VERIFY_WITH_TIMEOUT(!dialog.m_busy,30000);
        QVERIFY2(errors.isEmpty(),qPrintable(errors.join('\n')));
        QVERIFY(!updates.isEmpty()); QCOMPARE(bar->value(),100);
        QCOMPARE(bar->format(),QString("Complete — %p%"));
        QCoreApplication::processEvents(); // Late queued progress cannot undo completion.
        QCOMPARE(bar->value(),100);
        dialog.show();
        dialog.grab().save(directory.filePath("progress-complete.png"));
    }
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("openswmm-test");
        QCoreApplication::setApplicationName("channel-burn-dialog-test");
        QVERIFY(directory.isValid());
        directory.setAutoRemove(false);
        connect(&dismissMessages, &QTimer::timeout, this, [&] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget)) {
                    errors << box->text();
                    box->accept();
                }
        });
        dismissMessages.start(10);
    }
    void init()
    {
        errors.clear();
        const QString path = directory.filePath("model.inp");
        QFile::remove(path);
        QFile::remove(directory.filePath("model.oswp"));
        QVERIFY(QFile::copy(QStringLiteral(CHANNEL_BURN_FIXTURE), path));
        workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        window = new SWMMVisProjectWindow(workspace, path, nullptr);
        QList<QString> warnings, loadErrors;
        QVERIFY2(window->loadModel(warnings, loadErrors), qPrintable(loadErrors.join('\n')));
        window->modelLayer()->setSRS(new SpatialReferenceSystem("EPSG", 3857, window->modelLayer()), true);
        mesh::MeshResult grid;
        grid.ok = true;
        for (int y = 0; y <= 20; ++y) for (int x = 0; x <= 20; ++x)
            grid.vertices.append({{double(x * 2), double(y * 2)}, 10});
        for (int y = 0; y < 20; ++y) for (int x = 0; x < 20; ++x) {
            int a = y * 21 + x;
            mesh::MeshCell cell;
            cell.v0 = a; cell.v1 = a + 1; cell.v2 = a + 22;
            cell.mannings = .04; cell.initDepth = 0; cell.tag = "soil";
            grid.triangles.append(cell);
            cell.v1 = a + 22; cell.v2 = a + 21;
            grid.triangles.append(cell);
        }
        old = new SWMM2DMeshLayer(std::move(grid), path);
        old->setMeshUnitsSI(true);
        old->setSRS(new SpatialReferenceSystem("EPSG", 3857, old), true);
        old->setActiveMesh(true);
        old->setName("Original mesh");
        old->setOpacity(.6);
        old->setShowEdges(false);
        window->canvas()->addLayer(old, false);
        window->attachMeshLayer(old);
        auto settings = window->channelBurnSettings();
        settings.options.channelCellSize = 2;
        settings.options.forceHalfWidth = 4;
        window->setChannelBurnSettings(settings);
    }
    void cleanup()
    {
        delete window; window = nullptr;
        delete workspace; workspace = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void lengthsConvertedOnceAndSettingsStayInModelUnits()
    {
        window->unitSystem()->setFlowUnits(swmm_CFS);
        ChannelBurnDialog dialog(window);
        dialog.m_cellSize->setValue(10);
        dialog.m_halfWidth->setValue(6);
        mesh::ChannelMeshBurnInputs inputs;
        QString error;
        QVERIFY2(dialog.collectInputs(&inputs, &error), qPrintable(error));
        QVERIFY(std::abs(inputs.options.channelCellSize - 3.048) < 1e-10);
        QVERIFY(std::abs(inputs.options.forceHalfWidth - 1.8288) < 1e-10);
        dialog.startBurn();
        dialog.cancelBurn();
        QTRY_VERIFY_WITH_TIMEOUT(!dialog.m_busy, 30000);
        QCOMPARE(dialog.findChild<QProgressBar *>("channelBurnProgress")->format(),QString("Cancelled"));
        QVERIFY(dialog.findChild<QProgressBar *>("channelBurnProgress")->value()<100);
        QCoreApplication::processEvents();
        QVERIFY(dialog.m_status->text().contains("cancelled"));
        QCOMPARE(window->channelBurnSettings().options.channelCellSize, 10.0);
        QCOMPARE(window->channelBurnSettings().options.forceHalfWidth, 6.0);
        QCOMPARE(window->canvas()->undoStack()->count(), 0);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    }
    void importedFlagAloneDoesNotRejectAndGeographicMeshDoes()
    {
        old->setPreservesImportedSections(true);
        ChannelBurnDialog dialog(window);
        mesh::ChannelMeshBurnInputs inputs;
        QString error;
        QVERIFY2(dialog.collectInputs(&inputs, &error), qPrintable(error));
        old->setSRS(new SpatialReferenceSystem("EPSG", 4326, old), true);
        QVERIFY(!dialog.collectInputs(&inputs, &error));
        QVERIFY(error.contains("CRS"));
    }
    void modelAndMeshChangesInvalidateResults()
    {
        ChannelBurnDialog dialog(window);
        dialog.beginGuard();
        QVERIFY(dialog.ownerIsCurrent());
        old->edgeBCsMutable()[0].conveyance = .5;
        QVERIFY(!dialog.ownerIsCurrent());
        dialog.beginGuard();
        QVERIFY(dialog.ownerIsCurrent());
        window->modelLayer()->optionsChanged({QStringLiteral("FLOW_UNITS")});
        QVERIFY(!dialog.ownerIsCurrent());
    }
    void unsupportedCellSectionsAreRefused()
    {
        QFile file(old->sourcePath());
        QVERIFY(file.open(QIODevice::Append));
        file.write("\n[2D_INITIAL_VELOCITY]\n1 0.2 0.3\n");
        file.close();
        ChannelBurnDialog dialog(window);
        mesh::ChannelMeshBurnInputs inputs;
        QString error;
        QVERIFY(!dialog.collectInputs(&inputs, &error));
        QVERIFY(error.contains("2D_INITIAL_VELOCITY"));
    }
    void separateSettingsRoundTrip()
    {
        mesh::ChannelBurnSettings settings;
        settings.options.channelAspectMax = 6;
        settings.exportRaster = true;
        settings.exportDemPath = "source.tif";
        settings.exportDirectory = "terrain";
        settings.exportRasterZToSI = .3048;
        settings.maxMeshCells = 123456;
        const auto restored = ProjectSerializer::channelBurnFromJson(ProjectSerializer::channelBurnToJson(settings));
        QCOMPARE(restored.options.channelAspectMax, 6.0);
        QVERIFY(restored.exportRaster);
        QCOMPARE(restored.exportDemPath, settings.exportDemPath);
        QCOMPARE(restored.exportDirectory, settings.exportDirectory);
        QCOMPARE(restored.exportRasterZToSI, .3048);
        QCOMPARE(restored.maxMeshCells, 123456);
    }
    void rejectWhileRunningDoesNotCreateUndoEntry()
    {
        ChannelBurnDialog dialog(window);
        dialog.startBurn();
        QVERIFY(dialog.m_busy);
        dialog.reject();
        QTRY_VERIFY_WITH_TIMEOUT(!dialog.m_busy, 30000);
        QCOMPARE(window->canvas()->undoStack()->count(), 0);
        QVERIFY(window->canvas()->layers().contains(old));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    }
    void staleUndoCannotModifyReloadedEngine()
    {
        ChannelBurnDialog dialog(window);
        dialog.startBurn();
        QTRY_VERIFY_WITH_TIMEOUT(!dialog.m_busy, 30000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        auto *stack = window->canvas()->undoStack();
        QCOMPARE(stack->count(), 1);
        const int count = swmm_link_count(window->modelLayer()->engine());
        window->modelLayer()->engineAboutToClose();
        stack->undo();
        QCOMPARE(swmm_link_count(window->modelLayer()->engine()), count);
        QCOMPARE(stack->count(), 0);
    }
    void burnUndoRedoSaveAndReopen_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("inline") << 0;
        QTest::newRow("external") << 1;
        QTest::newRow("pending-import") << 2;
        QTest::newRow("pending-import-and-export") << 3;
    }
    void burnUndoRedoSaveAndReopen()
    {
        QFETCH(int, mode);
        QString why;
        if (mode) {
            const QString external = directory.filePath("mesh.2dm");
            mesh::InpMeshWriter::UnitInfo units;
            units.linearUnitName = "SI (m)";
            QVERIFY2(mesh::InpMeshWriter::write(mesh::MeshOutputMode::External,
                window->modelLayer()->modelFilePath(), external, old->mesh(), {}, .035, &why, units), qPrintable(why));
            old->setSourcePath(external);
            old->setExternalMesh(true);
            if (mode >= 2) {
                auto artifacts = GeneratedMeshArtifacts::create(window->modelLayer()->modelFilePath(), &why);
                QVERIFY2(artifacts, qPrintable(why));
                const auto stage = artifacts->reserve(external, "mesh.2dm", &why, ProjectSaveOutputs::Mesh);
                QVERIFY2(!stage.isEmpty(), qPrintable(why));
                QVERIFY2(artifacts->copySource(external, stage, &why), qPrintable(why));
                QVERIFY2(artifacts->seal(&why), qPrintable(why));
                old->setGeneratedArtifacts(artifacts);
                old->setPreservesImportedSections(true);
            }
        }
        ChannelBurnDialog dialog(window);
        QVERIFY(!dialog.m_exportRaster->isChecked());
        if (mode == 3) {
            dialog.m_exportRaster->setChecked(true);
            dialog.m_demPath->setText(QFileInfo(QStringLiteral(CHANNEL_BURN_FIXTURE)).absoluteDir().filePath("terrain.asc"));
            dialog.m_outputDirectory->setText(directory.filePath("raster"));
            dialog.show();
            QTest::qWait(20);
            dialog.grab().save(directory.filePath("channelburn-dialog.png"));
        }
        const auto readFile = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
        };
        const QString modelPath = window->modelLayer()->modelFilePath();
        const QString externalPath = mode ? old->sourcePath() : QString();
        const QByteArray modelBeforeBurn = readFile(modelPath);
        const QByteArray externalBeforeBurn = mode ? readFile(externalPath) : QByteArray{};
        QVERIFY(!modelBeforeBurn.isEmpty());
        if (mode) QVERIFY(!externalBeforeBurn.isEmpty());
        dialog.startBurn();
        QTRY_VERIFY_WITH_TIMEOUT(!dialog.m_busy, 30000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        auto *stack = window->canvas()->undoStack();
        QCOMPARE(stack->count(), 1);
        QVERIFY(!window->canvas()->layers().contains(old));
        SWMM2DMeshLayer *burned = nullptr;
        for (auto *layer : window->canvas()->layers())
            if (auto *mesh = qobject_cast<SWMM2DMeshLayer *>(layer); mesh && mesh->isActiveMesh()) burned = mesh;
        QVERIFY(burned);
        QVERIFY(burned != old);
        QCOMPARE(burned->name(), old->name());
        QCOMPARE(burned->opacity(), old->opacity());
        QCOMPARE(burned->showEdges(), old->showEdges());
        QVERIFY(burned->ownsGeneratedTopology());
        QCOMPARE(dialog.findChild<QProgressBar *>("channelBurnProgress")->value(),100);
        QCOMPARE(dialog.findChild<QProgressBar *>("channelBurnProgress")->format(),QString("Complete — %p%"));
        const int burnedCells = burned->triangleCount();
        QVERIFY(swmm_link_index(window->modelLayer()->engine(), "AB") >= 0);
        QVERIFY(swmm_link_index(window->modelLayer()->engine(), "XY") >= 0);
        QVERIFY(swmm_node_index(window->modelLayer()->engine(), "C") < 0);
        QVERIFY(swmm_link_index(window->modelLayer()->engine(), "BC") >= 0); // Outside interval survives.
        QVERIFY(swmm_link_index(window->modelLayer()->engine(), "CD") < 0); // Entirely replaced inside the mesh.
        QCOMPARE(readFile(modelPath), modelBeforeBurn);
        if (mode) QCOMPARE(readFile(externalPath), externalBeforeBurn);
        stack->undo();
        QVERIFY(window->canvas()->layers().contains(old));
        QVERIFY(old->isActiveMesh());
        QVERIFY(!window->canvas()->layers().contains(burned));
        QVERIFY(swmm_node_index(window->modelLayer()->engine(), "C") >= 0);
        QVERIFY(swmm_link_index(window->modelLayer()->engine(), "BC") >= 0);
        if (mode == 3) {
            QString error;
            QVERIFY2(window->save(&error), qPrintable(error));
        }
        stack->redo();
        QVERIFY(!window->canvas()->layers().contains(old));
        QVERIFY(window->canvas()->layers().contains(burned));
        QCOMPARE(burned->triangleCount(), burnedCells);
        if (mode == 3) { QTest::qWait(20); dialog.grab().save("/tmp/channelburn-result.png"); }
        QString error;
        const auto saved = directory.filePath("burned.inp");
        QVERIFY2(window->saveAs(saved, &error), qPrintable(error));
        const auto read = mesh::InpMeshReader::read(saved);
        QVERIFY2(read.hasMesh, qPrintable(read.errorMsg));
        QCOMPARE(read.mesh.triangles.size(), burnedCells);
        QCOMPARE(read.edgeBCs.size(), burned->edgeBCs().size());
        stack->undo();
        QVERIFY2(window->save(&error), qPrintable(error));
        const auto restored = mesh::InpMeshReader::read(saved);
        QVERIFY2(restored.hasMesh, qPrintable(restored.errorMsg));
        QCOMPARE(restored.mesh.triangles.size(), old->triangleCount());
        stack->redo();
        QVERIFY2(window->save(&error), qPrintable(error));
        const auto redone = mesh::InpMeshReader::read(saved);
        QVERIFY2(redone.hasMesh, qPrintable(redone.errorMsg));
        QCOMPARE(redone.mesh.triangles.size(), burnedCells);
    }
};
QTEST_MAIN(TestChannelBurnDialog)
#include "test_channelburndialog.moc"
