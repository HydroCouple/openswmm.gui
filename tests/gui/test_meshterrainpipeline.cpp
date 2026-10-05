// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/meshgenerationdialog.h"
#include "ui/dialogs/channelburndialog.h"
#include "map/channelmeshadoptioncommand.h"
#include "mesh/burnedrasterwriter.h"
#include "mesh/channelburnexport.h"
#include "mesh/sms2dmreader.h"
#include "mesh/inpmeshreader.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"
#include "layers/swmmmodellayer.h"
#include "mesh/channelburnprofile.h"
#include "project/generatedmeshartifacts.h"
#include "ui/widgets/corridorsourceswidget.h"
#include "mesh/meshcellgeom.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/gisvectorlayer.h"
#include "layers/gisrasterlayer.h"
#include "project/projectserializer.h"
#include "core/unitsystem.h"
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>
#include "map/spatialreferencesystem.h"
#include "layers/featurelayer.h"
#include <QApplication>
#include <QSpinBox>
#include <QTextStream>
#include <QDoubleSpinBox>
#include <QCryptographicHash>
#include <QMessageBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLineF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QCloseEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPromise>
#include <QSet>
#include <QScopeGuard>
#include <QTest>
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <cmath>
#include <vector>
#include <memory>
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif

class TestMeshTerrainPipeline : public QObject
{
    Q_OBJECT
    using GenerationInputs = MeshGenerationDialog::PipelineInputs;
    using GenerationResult = MeshGenerationDialog::PipelineResult;
    // Exercise the public stages in their real order: ordinary generation,
    // then a separate burn of the completed mesh. No burn inputs reach generation.
    struct Inputs : GenerationInputs {
        bool burnEnabled=false,exportRaster=false;
        mesh::BurnOptions burnOptions;
        QVector<mesh::BurnProfile> burnProfiles;
        mesh::BurnNetwork burnNetwork;
        QString burnOutputDir,burnDemCRSWkt;
    };
    struct Result : GenerationResult {
        mesh::ChannelBurnSurgery burnSurgery;
        QStringList burnWarnings;
        bool burnRan=false;
        mesh::ChannelMeshQuality burnBefore,burnAfter;
        int burnCavities=0,burnFallbackCavities=0;
        std::shared_ptr<GeneratedMeshArtifacts> generatedArtifacts;
        QString burnedDemPath,burnReportPath;
    };

    static Result burn(Result result,const Inputs &inputs,const std::function<bool()> &cancelled={})
    {
        if(!result.ok || !inputs.burnEnabled) return result;
        mesh::ChannelMeshBurnInputs burnInputs;
        burnInputs.profiles=inputs.burnProfiles;
        burnInputs.options=inputs.burnOptions;
        burnInputs.network=inputs.burnNetwork;
        burnInputs.nodes=inputs.couplingNodes;
        burnInputs.verticalUnitToSI=inputs.verticalUnitToSI;
        burnInputs.maxCells=inputs.genOpts.maxCells;
        QElapsedTimer burnClock;burnClock.start();
        auto progress=[&](int percent,const QString &message) {
            if(qEnvironmentVariableIsSet("SWMMVIS_REPRO_INP"))
                qInfo().noquote()<<QStringLiteral("[postburn %1% %2s] %3").arg(percent).arg(burnClock.elapsed()/1000.0,0,'f',2).arg(message);
        };
        auto burned=mesh::burnChannelsIntoMesh(result.meshResult,std::move(burnInputs),progress,cancelled);
        result.ok=burned.ok;
        result.errorMsg=burned.error;
        result.burnWarnings=burned.warnings;
        result.burnBefore=burned.before;result.burnAfter=burned.after;
        result.burnCavities=burned.cavities;result.burnFallbackCavities=burned.fallbackCavities;
        if(!burned.ok) return result;
        if(inputs.exportRaster) {
            mesh::ChannelBurnExportRequest request;
            request.projectPath=inputs.inpPath;request.sourcePath=inputs.dtmPath;
            request.outputDirectory=inputs.burnOutputDir;request.meshCRSWkt=inputs.meshCRSWkt;
            request.meshZToSI=inputs.verticalUnitToSI;
            request.rasterZToSI=inputs.verticalUnitToSI*inputs.zConversionFactor;
            request.options=inputs.burnOptions;
            const auto exported=mesh::prepareChannelBurnExport(burned,request,cancelled);
            if(!exported.ok) {result.ok=false;result.errorMsg=exported.error;return result;}
            result.generatedArtifacts=exported.artifacts;
            result.burnedDemPath=exported.rasterPath;result.burnReportPath=exported.reportPath;
            result.burnWarnings+=exported.warnings;
        }
        result.meshResult=std::move(burned.mesh);
        result.burnSurgery=std::move(burned.surgery);
        result.burnRan=!burned.profiles.isEmpty();
        result.coupling.vertexToNode.clear();
        for(int v=0;v<result.meshResult.vertices.size();++v)
            if(!result.meshResult.vertices[v].coupledNode.isEmpty())
                result.coupling.vertexToNode.insert(v,result.meshResult.vertices[v].coupledNode);
        return result;
    }

    static Result run(Inputs inputs)
    {
        QPromise<GenerationResult> promise;
        auto future = promise.future();
        promise.start();
        MeshGenerationDialog::runMeshPipeline(promise, inputs);
        promise.finish();
        Result result;
        if(future.resultCount()) static_cast<GenerationResult &>(result)=future.result();
        return burn(std::move(result),inputs);
    }

    static bool adoptBurn(SWMMVisProjectWindow *window,const Result &result,QString *error)
    {
        auto *layer=new SWMM2DMeshLayer(result.meshResult,result.meshPath);
        layer->setOwnsGeneratedTopology(true);
        layer->setMeshUnitsSI(result.meshUnitsSI);
        layer->setExternalMesh(result.outputMode==mesh::MeshOutputMode::External);
        layer->setGeneratedArtifacts(result.generatedArtifacts);
        if(window->modelLayer()->srs())
            layer->setSRS(new SpatialReferenceSystem(*window->modelLayer()->srs(),layer),true);
        auto command=std::make_unique<ChannelMeshAdoptionCommand>(window,layer,result.corridorSources);
        if(!command->prepare(result.burnSurgery,error)) return false;
        window->canvas()->undoStack()->push(command.release());
        window->attachMeshLayer(layer);
        return true;
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
        OGRSpatialReference projected;projected.importFromEPSG(3857);
        char *wkt=nullptr;projected.exportToWkt(&wkt);
        inputs.meshCRSWkt=QString::fromUtf8(wkt);inputs.burnDemCRSWkt=inputs.meshCRSWkt;
        dataset->SetProjection(wkt);CPLFree(wkt);
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

    static bool prepareBankFixture(const QDir &dir, Inputs &inputs, bool variable = false)
    {
        if (!prepareCorridorFixture(dir, inputs)) return false;
        auto &source = inputs.corridorSources[0];
        source.bankPair = true;
        source.featureIds = {7, 8};
        // Width is now controlled by the actual bank geometry, not this old value.
        source.width = 1;
        const QByteArray upper = variable ? "[[28,20],[16,21],[4,18]]" : "[[28,18],[4,18]]";
        return writeBytes(source.path, QByteArray(R"({"type":"FeatureCollection","name":"corridors","crs":{"type":"name","properties":{"name":"EPSG:3857"}},"features":[{"type":"Feature","id":7,"properties":{},"geometry":{"type":"LineString","coordinates":[[4,14],[28,14]]}},{"type":"Feature","id":8,"properties":{},"geometry":{"type":"LineString","coordinates":)")
            + upper + "}}]}");
    }

private slots:
    void bundledChannelBoundaryExample()
    {
        const QDir source(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA","."))
            .absoluteFilePath("../../../examples/channel_burn_boundary"));
        const QDir dir(qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT")+"/bundled_channel_example");
        QVERIFY(QDir().mkpath(dir.path()));
        QMap<QString,QByteArray> originals;
        for(const auto *name:{"channel_burn_boundary.inp","channel_burn_boundary.oswp",
                            "terrain.asc","terrain.prj","study_domain.geojson"}) {
            QFile file(source.filePath(name));QVERIFY2(file.open(QIODevice::ReadOnly),qPrintable(file.fileName()));
            const auto bytes=file.readAll();originals.insert(name,bytes);
            QVERIFY(writeBytes(dir.filePath(name),bytes));
        }
        auto workspace=std::unique_ptr<OpenSWMMVisWorkspace>(OpenSWMMVisWorkspace::newInstance(QString(),nullptr));
        auto window=std::make_unique<SWMMVisProjectWindow>(workspace.get(),dir.filePath("channel_burn_boundary.inp"),nullptr);
        QList<QString> warnings,errors;QString error;
        QVERIFY2(window->loadModel(warnings,errors),qPrintable(errors.join('\n')));
        QVERIFY2(ProjectSerializer::applyFromFile(dir.filePath("channel_burn_boundary.oswp"),window.get(),&error,&warnings),qPrintable(error));
        const auto restoreUnits=qScopeGuard([previous=UnitSystem::activeProject()]{UnitSystem::setActiveProject(previous);});
        UnitSystem::setActiveProject(window->unitSystem());
        const auto outsideSectionIntact=[](SWMM_Engine engine) {
            int shape=-1;double a=0,b=0,c=0,d=0;
            return swmm_link_get_xsect(engine,swmm_link_index(engine,"XY"),&shape,&a,&b,&c,&d)==SWMM_OK
                && shape==SWMM_XSECT_TRAPEZOIDAL && a==2 && b==2 && c==1 && d==1;
        };
        QVERIFY(outsideSectionIntact(window->modelLayer()->engine()));
        auto terrain=[&]() -> GISRasterLayer * {
            for(auto *layer:window->canvas()->layers())
                if(auto *raster=qobject_cast<GISRasterLayer *>(layer);
                   raster && QFileInfo(raster->filePath()).fileName()=="terrain.asc") return raster;
            return nullptr;
        };
        QTRY_VERIFY_WITH_TIMEOUT(terrain()!=nullptr,10000);
        auto boundary=[&]() -> GISVectorLayer * {
            for(auto *layer:window->canvas()->layers())
                if(auto *vector=qobject_cast<GISVectorLayer *>(layer);
                   vector && QFileInfo(vector->filePath()).fileName()=="study_domain.geojson") return vector;
            return nullptr;
        };
        QTRY_VERIFY_WITH_TIMEOUT(boundary()!=nullptr,10000);
        MeshGenerationDialog dialog(window.get(),nullptr);
        const int terrainIndex=dialog.m_dtmCombo->findData(QVariant::fromValue<void *>(terrain()));
        QVERIFY(terrainIndex>=0);dialog.m_dtmCombo->setCurrentIndex(terrainIndex);
        const int index=dialog.m_boundaryLayerCombo->findData(QVariant::fromValue<void *>(boundary()));
        QVERIFY(index>=0);dialog.m_boundaryLayerCombo->setCurrentIndex(index);
        const auto terrainFilesBefore=QDir(dir.filePath("terrain")).entryList(QDir::Files);
        Inputs in;QVERIFY2(dialog.collectInputs(&in,&error),qPrintable(error));
        QCOMPARE(in.boundaryKind,Inputs::BoundaryKind::VectorFile);
        QCOMPARE(in.cellSize,4.);QCOMPARE(in.terrainTolerance,.1);
        auto generated=run(in);QVERIFY2(generated.ok,qPrintable(generated.errorMsg));
        QVERIFY(!generated.burnRan);
        QVERIFY2(adoptBurn(window.get(),generated,&error),qPrintable(error));
        ChannelBurnDialog burnDialog(window.get(),nullptr);
        mesh::ChannelMeshBurnInputs selected;
        QVERIFY2(burnDialog.collectInputs(&selected,&error),qPrintable(error));
        QCOMPARE(selected.options.geometryTolerance,.05);
        QCOMPARE(selected.profiles.size(),3); // XY is eligible, but wholly outside.
        in.burnEnabled=true;in.burnOptions=selected.options;
        in.burnProfiles=selected.profiles;in.burnNetwork=selected.network;
        in.couplingNodes=selected.nodes;
        auto result=burn(std::move(generated),in);QVERIFY2(result.ok,qPrintable(result.errorMsg));
        for(const auto &w:result.burnWarnings) qInfo("burn warning: %s",qPrintable(w));
        for(const auto &w:result.alignmentWarnings) qInfo("alignment warning: %s",qPrintable(w));
        QVERIFY(result.burnRan);QCOMPARE(result.burnSurgery.splits.size(),1);
        QCOMPARE(result.burnSurgery.burnedConduits.size(),2);
        QVERIFY(!result.burnSurgery.burnedConduits.contains("XY"));
        QVERIFY(!result.meshResult.triangles.isEmpty());
        // Burn adoption remains independently undoable after mesh generation.
        auto *stack=window->canvas()->undoStack();const int before=stack->count();
        QVERIFY2(adoptBurn(window.get(),result,&error),qPrintable(error));
        QCOMPARE(stack->count(),before+1);
        const auto engine=window->modelLayer()->engine();
        QVERIFY(outsideSectionIntact(engine));
        QCOMPARE(swmm_link_count(engine),4);QCOMPARE(swmm_node_count(engine),7);
        QVERIFY(swmm_link_index(engine,"CD")<0);QVERIFY(swmm_node_index(engine,"C")<0);
        for(const auto *name:{"AB","BC","DE","XY"}) QVERIFY(swmm_link_index(engine,name)>=0);
        int type=-1;double invert=0,offset=0,length=0;
        swmm_node_get_type(engine,swmm_node_index(engine,"D"),&type);QCOMPARE(type,int(SWMM_NODE_OUTFALL));
        swmm_node_get_invert_elev(engine,swmm_node_index(engine,"D"),&invert);
        swmm_link_get_offset_up(engine,swmm_link_index(engine,"DE"),&offset);
        QVERIFY(std::abs(invert+offset-8.1)<1e-10);
        swmm_link_get_length(engine,swmm_link_index(engine,"BC"),&length);
        QVERIFY(std::abs(length-2)<1e-10);
        swmm_link_get_length(engine,swmm_link_index(engine,"XY"),&length);QCOMPARE(length,10.);
        const auto &split=result.burnSurgery.splits.first();
        swmm_node_get_type(engine,swmm_node_index(engine,split.nodeId.toUtf8().constData()),&type);
        QCOMPARE(type,int(SWMM_NODE_OUTFALL));
        for(const auto &v:result.meshResult.vertices) {
            QVERIFY(v.xy.x()>=-1e-8 && v.xy.x()<=32+1e-8 && v.xy.y()>=-1e-8 && v.xy.y()<=32+1e-8);
            QVERIFY(std::isfinite(v.z));
        }
        stack->undo();QCOMPARE(swmm_link_count(engine),5);QCOMPARE(swmm_node_count(engine),7);
        QVERIFY(outsideSectionIntact(engine));
        QVERIFY(swmm_node_index(engine,"C")>=0);QVERIFY(swmm_link_index(engine,"CD")>=0);
        stack->redo();QVERIFY(swmm_node_index(engine,"C")<0);
        QVERIFY(outsideSectionIntact(engine));
        const QString saved=dir.filePath("adopted.inp");
        QVERIFY2(window->saveAs(saved,&error),qPrintable(error));
        SWMMModelLayer reopened(saved);QVERIFY2(reopened.loadModel(warnings,errors),qPrintable(errors.join('\n')));
        QCOMPARE(swmm_validate_model(reopened.engine()),SWMM_OK);
        QVERIFY(outsideSectionIntact(reopened.engine()));
        QCOMPARE(swmm_link_count(reopened.engine()),4);QCOMPARE(swmm_node_count(reopened.engine()),7);
        // Raster export is optional; the default burn writes no terrain copy.
        QCOMPARE(QDir(dir.filePath("terrain")).entryList(QDir::Files),terrainFilesBefore);
        for(auto it=originals.cbegin();it!=originals.cend();++it) {
            QFile file(source.filePath(it.key()));QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),it.value());
        }
    }

    // One channel whose corridor folds at a hairpin must not fail the mesh or
    // the other channels: it stays unburned (a 1D conduit) with a warning.
    void foldingChannelIsLeftUnburnedNotFatal()
    {
        const QDir dir(qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT")+"/folding_channel");
        Inputs in; QVERIFY(prepareBurnFixture(dir,in,false));
        mesh::ChannelInput hairpin;
        hairpin.conduitId="HAIRPIN";
        hairpin.centerline={{6,24},{26,26},{6,28}};
        hairpin.zUp=8.0; hairpin.zDn=7.5;
        hairpin.section=mesh::sectionFromWidths({0.0,1.0,2.0},{4.0,6.0,8.0});
        const auto profile=mesh::buildBurnProfile(hairpin,in.burnOptions);
        QVERIFY(profile.isValid());
        QString err;
        QVERIFY2(!mesh::buildCorridorLattice(profile,in.burnOptions.chainageStep,0.0,nullptr,&err).isValid(),
                 "fixture must fold");
        in.burnProfiles.append(profile);
        // The hairpin is a 1D link too: being unburnable, it must stay in 1D.
        const int h0=in.burnNetwork.nodes.size();
        in.burnNetwork.nodes.append({"H0"}); in.burnNetwork.nodes.append({"H1"});
        in.burnNetwork.links.append({"HAIRPIN",h0,h0+1});
        const auto result=run(in);
        QVERIFY2(result.ok,qPrintable(result.errorMsg));
        QVERIFY(result.burnRan);
        bool warned=false;
        for(const auto &w:result.burnWarnings) warned=warned || (w.contains("HAIRPIN") && w.contains("1D"));
        QVERIFY2(warned,qPrintable(result.burnWarnings.join('\n')));
        QVERIFY(!result.burnSurgery.burnedConduits.contains("HAIRPIN"));
        for(const auto &node:result.burnSurgery.nodePlans) QVERIFY(node.nodeId!="H0" && node.nodeId!="H1");
    }

    void geographicDemUsesPhysicalChannelSpacing()
    {
        Inputs in;
        const QDir dir(qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT")+"/geographic_channel");
        QVERIFY(prepareBurnFixture(dir,in,false));
        OGRSpatialReference geographic,projected;
        geographic.importFromEPSG(4326);projected.importFromEPSG(3857);
        char *wkt=nullptr;geographic.exportToWkt(&wkt);in.burnDemCRSWkt=QString::fromUtf8(wkt);CPLFree(wkt);
        projected.exportToWkt(&wkt);in.meshCRSWkt=QString::fromUtf8(wkt);CPLFree(wkt);
        auto *ds=static_cast<GDALDataset *>(GDALOpen(in.dtmPath.toUtf8().constData(),GA_Update));QVERIFY(ds);
        double gt[6];QCOMPARE(ds->GetGeoTransform(gt),CE_None);
        for(double &value:gt) value/=111319.49079327358;
        QCOMPARE(ds->SetGeoTransform(gt),CE_None);QCOMPARE(ds->SetProjection(in.burnDemCRSWkt.toUtf8().constData()),CE_None);
        GDALClose(ds);in.exportRaster=true;
        in.burnOptions.chainageStep=0;in.cellSize=2;in.minCellSize=.05;in.genOpts.maxArea=2;
        const auto result=run(in);QVERIFY2(result.ok,qPrintable(result.errorMsg));
        QVERIFY(result.burnRan);QVERIFY(result.meshResult.triangles.size()<10000);
        QVERIFY(result.meshResult.quadCount()>0 || !result.meshResult.triangles.isEmpty());
    }

    void channelReplacementDomainAndNodes_data()
    {
        QTest::addColumn<QString>("scenario");
        for(const auto *name:{"outside","crossing","crossing-quads","interior","interior-quads","incision","nodata","rectangle","rectangle-quads"})
            QTest::newRow(name)<<QString(name);
    }
    void channelReplacementDomainAndNodes()
    {
        QFETCH(QString,scenario);
        Inputs in;
        const QDir dir(qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT")+"/channel_replacement/"+scenario);
        QVERIFY(prepareBurnFixture(dir,in,false));
        in.cellSize=2;in.minCellSize=.05;in.genOpts.maxArea=2;
        in.terrainAdaptive=true;in.terrainTolerance=.1;
        in.burnProfiles.clear();in.burnOptions.chainageStep=0;
        in.burnOptions.quadCorridor=scenario.contains("quads");
        in.burnOptions.channelCellSize=2;
        in.burnNetwork.nodes={{"A"},{"B"},{"C"}};
        auto add=[&](QString id,QPointF a,QPointF b,int from,int to) {
            mesh::ChannelInput c;c.conduitId=id;c.centerline={a,b};c.zUp=c.zDn=8;
            c.section=mesh::sectionFromWidths({0,1,2},{2,3,4});
            if(scenario.startsWith("rectangle")) c.section=mesh::sectionFromWidths({0,2},{4,4});
            in.burnProfiles.append(mesh::buildBurnProfile(c,in.burnOptions));
            in.burnNetwork.links.append({id,from,to});
        };
        if(scenario=="outside") add("AB",{34,10},{35,10},0,1);
        else if(scenario.startsWith("crossing")) add("AB",{-2,16},{16,16},0,1);
        else {
            add("AB",{8,16},{16,16},0,1);add("BC",{16,16},{24,16},1,2);
            in.includeJunctions=true;in.nodesUseRim=true;in.includeConduits=true;
            in.candidateNodes={{"A",{8,16},10,true},{"B",{16,16},10,true},{"C",{24,16},10,true}};
            in.couplingNodes={{"A",{8,16}},{"B",{16,16}},{"C",{24,16}}};
            in.candidateLinks={{"AB",{{8,16},{16,16}}},{"BC",{{16,16},{24,16}}}};
        }
        if(scenario=="incision") in.burnOptions.maxIncision=.5;
        if(scenario=="nodata") {
            auto *ds=static_cast<GDALDataset *>(GDALOpen(in.dtmPath.toUtf8().constData(),GA_Update));QVERIFY(ds);
            auto *band=ds->GetRasterBand(1);band->SetNoDataValue(-9999);
            double missing=-9999;
            QCOMPARE(band->RasterIO(GF_Write,20,19,1,1,&missing,1,1,GDT_Float64,0,0),CE_None);
            GDALClose(ds);
        }
        const auto result=run(in);
        if(scenario=="outside") {
            QVERIFY(!result.ok);QVERIFY(result.burnSurgery.burnedConduits.isEmpty());
            QVERIFY2(result.errorMsg.contains("No eligible"),qPrintable(result.errorMsg));return;
        }
        if(scenario=="incision") {
            QVERIFY2(result.ok,qPrintable(result.errorMsg));
            for(const auto &vertex:result.meshResult.vertices) QVERIFY(vertex.z>=9.5-1e-8);
            QVERIFY(!result.burnWarnings.isEmpty());return;
        }
        // The authored channel supplies bathymetry at a missing source pixel;
        // this is valid when the complete section passes channel verification.
        QVERIFY2(result.ok,qPrintable(result.errorMsg));
        if(scenario=="outside") {
            QVERIFY(!result.burnRan);QVERIFY(result.burnSurgery.burnedConduits.isEmpty());return;
        }
        QVERIFY(result.burnRan);
        if(scenario.startsWith("crossing")) {
            QCOMPARE(result.burnSurgery.splits.size(),1);
            QVERIFY(!result.burnSurgery.burnedConduits.contains("AB"));
            QCOMPARE(result.burnSurgery.burnedConduits.size(),1);
        } else {
            QCOMPARE(result.burnSurgery.burnedConduits.size(),2);
            QVERIFY(result.coupling.vertexToNode.isEmpty());QVERIFY(result.meshResult.cellCouplings.isEmpty());
            bool bedFound=false;
            for(const auto &v:result.meshResult.vertices) if(std::abs(v.xy.y()-16)<1e-7 && v.xy.x()>=8 && v.xy.x()<=24) {
                QVERIFY2(std::abs(v.z-8)<.05,qPrintable(QString::number(v.z)));bedFound=true;
            }
            QVERIFY(bedFound);
        }
    }

    // A concave bend of the outer boundary that cuts a channel between lattice
    // rows (every lattice vertex inside) must not leave a quad patch outside
    // the domain: that band falls back to clipped lines.
    void boundaryNotchBetweenLatticeRowsKeepsQuadsInside()
    {
        Inputs in;
        const QDir dir(qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT")+"/channel_replacement/boundary_notch");
        QVERIFY(prepareBurnFixture(dir,in,false));
        in.domains={QPolygonF(QVector<QPointF>{{0,0},{32,0},{32,32},{17.1,32},{17,17},{16.9,32},{0,32}})};
        in.cellSize=2;in.minCellSize=.05;in.genOpts.maxArea=2;
        in.burnProfiles.clear();in.burnOptions.chainageStep=0;
        in.burnOptions.quadCorridor=true;in.burnOptions.channelCellSize=2;
        in.burnNetwork.nodes={{"A"},{"B"}};
        mesh::ChannelInput c;c.conduitId="AB";c.centerline={{8,16},{24,16}};c.zUp=c.zDn=8;
        c.section=mesh::sectionFromWidths({0,2},{4,4});
        in.burnProfiles.append(mesh::buildBurnProfile(c,in.burnOptions));
        in.burnNetwork.links.append({"AB",0,1});
        const auto result=run(in);
        QVERIFY2(result.ok,qPrintable(result.errorMsg));
        QVERIFY(result.burnRan);
        int quads=0;
        for(const auto &cell:result.meshResult.triangles) if(cell.isQuad()) ++quads;
        QVERIFY(quads>0);   // the bands clear of the notch stay quads
    }

    // No dangling outfalls: a headwater with an inflow whose only link would
    // be burned keeps that link in 1D; the next node down becomes the coupled
    // interface outfall with exactly one 1D link.
    void headwaterWithInflowNeverBecomesALinklessOutfall()
    {
        Inputs in;
        const QDir dir(qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT")+"/channel_replacement/headwater_inflow");
        QVERIFY(prepareBurnFixture(dir,in,false));
        in.cellSize=2;in.minCellSize=.05;in.genOpts.maxArea=2;
        in.terrainAdaptive=true;in.terrainTolerance=.1;
        in.burnProfiles.clear();in.burnOptions.chainageStep=0;
        in.burnOptions.quadCorridor=false;in.burnOptions.channelCellSize=2;
        in.burnNetwork.nodes={{"A"},{"B"},{"C"}};
        in.burnNetwork.nodes[0].hasExternalInflow=true;   // runoff arrives at the headwater
        auto add=[&](QString id,QPointF a,QPointF b,int from,int to) {
            mesh::ChannelInput c;c.conduitId=id;c.centerline={a,b};c.zUp=c.zDn=8;
            c.section=mesh::sectionFromWidths({0,1,2},{2,3,4});
            in.burnProfiles.append(mesh::buildBurnProfile(c,in.burnOptions));
            in.burnNetwork.links.append({id,from,to});
        };
        add("AB",{8,16},{16,16},0,1);add("BC",{16,16},{24,16},1,2);
        in.includeJunctions=true;in.nodesUseRim=true;in.includeConduits=true;
        in.candidateNodes={{"A",{8,16},10,true},{"B",{16,16},10,true},{"C",{24,16},10,true}};
        in.couplingNodes={{"A",{8,16}},{"B",{16,16}},{"C",{24,16}}};
        in.candidateLinks={{"AB",{{8,16},{16,16}}},{"BC",{{16,16},{24,16}}}};
        const auto result=run(in);
        QVERIFY2(result.ok,qPrintable(result.errorMsg));
        QVERIFY(result.burnRan);
        QCOMPARE(result.burnSurgery.burnedConduits,QStringList{"BC"});
        bool bIsOutfall=false;
        for(const auto &node:result.burnSurgery.nodePlans) {
            if(node.role==mesh::BurnNodeRole::Outfall) QCOMPARE(node.survivingLinks,1);
            if(node.nodeId=="B") bIsOutfall=node.role==mesh::BurnNodeRole::Outfall;
            QVERIFY(node.nodeId!="A");   // the headwater is untouched
        }
        QVERIFY(bIsOutfall);
        bool warned=false;
        for(const auto &w:result.burnWarnings) warned=warned||(w.contains("AB")&&w.contains("surviving link"));
        QVERIFY2(warned,qPrintable(result.burnWarnings.join('\n')));
    }

    void channelAdoptionRestoresNetworkAndPreviousMesh_data()
    {
        QTest::addColumn<bool>("crossing");
        QTest::addColumn<bool>("invalidPlan");
        QTest::newRow("inside")<<false<<false;QTest::newRow("crossing")<<true<<false;
        QTest::newRow("rollback")<<true<<true;
    }
    void channelAdoptionRestoresNetworkAndPreviousMesh()
    {
        QFETCH(bool,crossing);
        QFETCH(bool,invalidPlan);
        Inputs in;
        const QDir dir(qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT")+"/channel_undo/"+QTest::currentDataTag());
        QVERIFY(prepareBurnFixture(dir,in,false));
        QByteArray authored=R"([TITLE]
Channel replacement undo fixture
[OPTIONS]
FLOW_UNITS CMS
FLOW_ROUTING DYNWAVE
START_DATE 01/01/2026
END_DATE 01/02/2026
[JUNCTIONS]
A 9 2 0 0 0
B 8.5 2 0 0 0
C 8 2 0 0 0
D 8.25 2 0 0 0
[OUTFALLS]
E 7 FREE NO
[CONDUITS]
AB A B 80 .013 0 .4 0 0
BC B C 80 .027 .2 .1 0 0
CD C D 80 .029 .1 .2 0 0
DE D E 80 .014 .3 0 0 0
[XSECTIONS]
AB CIRCULAR 1 0 0 0 1
BC TRAPEZOIDAL 2 2 1 1 1
CD TRAPEZOIDAL 2 2 1 1 1
DE CIRCULAR 1 0 0 0 1
[LOSSES]
BC .1 .2 .3 NO .002
[COORDINATES]
A 2 16
B 8 16
C 16 16
D 24 16
E 30 16
[TAGS]
LINK BC creek
NODE C interior
)";
        if(crossing) authored.replace("B 8 16","B -2 16");
        QVERIFY(writeBytes(in.inpPath,authored));
        auto workspace=std::unique_ptr<OpenSWMMVisWorkspace>(OpenSWMMVisWorkspace::newInstance(QString(),nullptr));
        auto window=std::make_unique<SWMMVisProjectWindow>(workspace.get(),in.inpPath,nullptr);
        QList<QString> warnings,errors;
        QVERIFY2(window->loadModel(warnings,errors),qPrintable(errors.join('\n')));
        in.burnProfiles.clear();in.burnOptions.chainageStep=0;in.burnOptions.channelCellSize=2;
        in.cellSize=2;in.minCellSize=.05;in.genOpts.maxArea=2;
        in.couplingNodes={{"A",{2,16}},{"B",{crossing?-2.:8.,16}},
                          {"C",{16,16}},{"D",{24,16}},{"E",{30,16}}};
        in.burnNetwork.nodes={{"A"},{"B"},{"C"},{"D"},{"E",false,false}};
        in.burnNetwork.links={{"AB",0,1},{"BC",1,2},{"CD",2,3},{"DE",3,4}};
        for(int i=0;i<2;++i) {
            mesh::ChannelInput c;c.conduitId=i?"CD":"BC";
            c.centerline={{crossing && i==0?-2.:8.+i*8,16},{16.+i*8,16}};
            c.zUp=i?8.1:8.7;c.zDn=i?8.45:8.1;
            c.section=mesh::sectionFromWidths({0,2},{2,6});
            in.burnProfiles.append(mesh::buildBurnProfile(c,in.burnOptions));
        }
        auto result=run(in);QVERIFY2(result.ok,qPrintable(result.errorMsg));
        QCOMPARE(result.burnSurgery.burnedConduits.size(),2);
        if(invalidPlan) {
            mesh::BurnNodePlan missing;missing.nodeId="MISSING_INTERFACE";missing.role=mesh::BurnNodeRole::Outfall;
            result.burnSurgery.nodePlans.append(missing);
        }
        auto *oldMesh=new SWMM2DMeshLayer(result.meshResult,"old.2dm");
        oldMesh->setActiveMesh(true);window->canvas()->addLayer(oldMesh,false);
        auto *stack=window->canvas()->undoStack();const int before=stack->count();
        QString adoptionError;
        const bool adopted=adoptBurn(window.get(),result,&adoptionError);
        QCOMPARE(adopted,!invalidPlan);
        if(invalidPlan) {
            QCOMPARE(stack->count(),before);
            QVERIFY(oldMesh->isActiveMesh());
            const auto engine=window->modelLayer()->engine();
            QCOMPARE(swmm_node_count(engine),5);QCOMPARE(swmm_link_count(engine),4);
            QVERIFY(swmm_node_index(engine,"C")>=0);QVERIFY(swmm_link_index(engine,"CD")>=0);
            int type=-1;swmm_node_get_type(engine,swmm_node_index(engine,"D"),&type);
            QCOMPARE(type,int(SWMM_NODE_JUNCTION));
            QFile source(in.inpPath);QVERIFY(source.open(QIODevice::ReadOnly));QCOMPARE(source.readAll(),authored);
            return;
        }
        QCOMPARE(stack->count(),before+1);
        const auto engine=window->modelLayer()->engine();
        QCOMPARE(swmm_link_index(engine,"BC")>=0,crossing);QVERIFY(swmm_node_index(engine,"C")<0);
        int type=-1;double invert=0,offset=0;
        QCOMPARE(swmm_node_get_type(engine,swmm_node_index(engine,"B"),&type),SWMM_OK);
        QCOMPARE(type,int(crossing?SWMM_NODE_JUNCTION:SWMM_NODE_OUTFALL));
        swmm_node_get_invert_elev(engine,swmm_node_index(engine,"B"),&invert);
        swmm_link_get_offset_dn(engine,swmm_link_index(engine,"AB"),&offset);
        QVERIFY(std::abs(invert+offset-8.9)<1e-10);
        QVERIFY(!oldMesh->isActiveMesh());
        stack->undo();
        QVERIFY(oldMesh->isActiveMesh());QVERIFY(swmm_node_index(engine,"C")>=0);
        const int link=swmm_link_index(engine,"BC");QVERIFY(link>=0);
        int shape=-1;double g1=0,g2=0,g3=0,g4=0,n=0,a=0,b=0,c=0;
        swmm_link_get_xsect(engine,link,&shape,&g1,&g2,&g3,&g4);
        QCOMPARE(shape,int(SWMM_XSECT_TRAPEZOIDAL));QCOMPARE(g1,2.);QCOMPARE(g2,2.);
        swmm_link_get_roughness(engine,link,&n);QVERIFY(std::abs(n-.027)<1e-12);
        swmm_link_get_loss_coeff(engine,link,&a,&b,&c);
        QCOMPARE(a,.1);QCOMPARE(b,.2);QCOMPARE(c,.3);
        char tag[100]{};swmm_node_get_tag(engine,swmm_node_index(engine,"C"),tag,100);
        QCOMPARE(QString::fromUtf8(tag),QString("interior"));
        stack->redo();QCOMPARE(swmm_link_index(engine,"BC")>=0,crossing);QVERIFY(!oldMesh->isActiveMesh());
        QString saveError;
        const QString saved=dir.absoluteFilePath("adopted.inp");
        QVERIFY2(window->saveAs(saved,&saveError),qPrintable(saveError));
        SWMMModelLayer reopened(saved);
        QVERIFY2(reopened.loadModel(warnings,errors),qPrintable(errors.join('\n')));
        QVERIFY(reopened.engine());
        QCOMPARE(swmm_link_count(reopened.engine()),crossing?3:2);
        QCOMPARE(swmm_node_count(reopened.engine()),crossing?5:4);
        QCOMPARE(swmm_validate_model(reopened.engine()),SWMM_OK);
        stack->undo();
        QFile source(in.inpPath);QVERIFY(source.open(QIODevice::ReadOnly));QCOMPARE(source.readAll(),authored);
    }

    void coverageFillDoesNotCertifyTerrainOutsideTheDEM()
    {
        const QDir dir(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA","."))
            .absoluteFilePath("../../output/terrain_adaptive_mesh_2026-10/coverage"));
        QVERIFY(QDir().mkpath(dir.path()));
        Inputs in; in.inpPath=dir.filePath("model.inp");
        QVERIFY(writeBytes(in.inpPath,"[TITLE]\nPartial DEM coverage\n[OPTIONS]\nFLOW_UNITS CMS\n"));
        in.dtmPath=dir.filePath("partial.tif"); GDALAllRegister();
        auto *ds=GetGDALDriverManager()->GetDriverByName("GTiff")->Create(in.dtmPath.toUtf8().constData(),20,20,1,GDT_Float32,nullptr);
        QVERIFY(ds); double gt[6]={10,1,0,30,0,-1}; ds->SetGeoTransform(gt);
        QVector<float> values(400,10);
        QCOMPARE(ds->GetRasterBand(1)->RasterIO(GF_Write,0,0,20,20,values.data(),20,20,GDT_Float32,0,0),CE_None);
        GDALClose(ds);
        in.modelExtent=MapExtent(0,0,40,40);
        in.domains={QPolygonF(QVector<QPointF>{{0,0},{40,0},{40,40},{0,40}})};
        in.auxPoints={{{2,2},12,true},{{20,20},10,true}};
        in.cellSize=5; in.coarsenFactor=1; in.minCellSize=1;
        in.genOpts.maxArea=.4330127018922193*25; in.genOpts.minCellSize=1;
        in.terrainTolerance=.1; in.terrainAdaptive=true; in.mapNodesAfterGen=false;
        const auto result=run(in); QVERIFY2(result.ok,qPrintable(result.errorMsg));
        QVERIFY(result.generationStats.terrainUnknown>0);
        QVERIFY(!result.alignmentWarnings.isEmpty());
        for(const auto &v:result.meshResult.vertices) QVERIFY(std::isfinite(v.z));
    }

    // "Use node rim elevation": a rim lowers the ground to the node where the
    // terrain stands above it; where the terrain is at or below the rim the
    // node keeps the terrain. The rim never raises the ground.
    void nodeRimOnlyLowersTerrain()
    {
        const QString root=qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT",QDir::current().filePath("terrain_pipeline_output"));
        const QDir dir(root+"/node_rim_lowers_only");
        QVERIFY(QDir().mkpath(dir.path()));
        Inputs in; in.inpPath=dir.filePath("model.inp");
        QVERIFY(writeBytes(in.inpPath,"[TITLE]\nRim rule\n[OPTIONS]\nFLOW_UNITS CMS\n"));
        in.dtmPath=dir.filePath("flat.tif");
        GDALAllRegister();
        auto *ds=GetGDALDriverManager()->GetDriverByName("GTiff")->Create(in.dtmPath.toUtf8().constData(),110,110,1,GDT_Float32,nullptr);
        QVERIFY(ds); double gt[6]={-5,1,0,105,0,-1}; QCOMPARE(ds->SetGeoTransform(gt),CE_None);
        QVector<float> z(110*110,10.f);
        QCOMPARE(ds->GetRasterBand(1)->RasterIO(GF_Write,0,0,110,110,z.data(),110,110,GDT_Float32,0,0),CE_None);
        GDALClose(ds);
        in.modelExtent=MapExtent(0,0,100,100);
        in.domains={QPolygonF(QVector<QPointF>{{0,0},{100,0},{100,100},{0,100}})};
        in.meshLinearUnitName="metre";
        in.cellSize=10; in.minCellSize=2.5; in.coarsenFactor=1;
        in.genOpts.minCellSize=in.minCellSize; in.genOpts.maxArea=.4330127018922193*100;
        in.terrainAdaptive=false; in.terrainBreaklines=false; in.mapNodesAfterGen=false;
        in.includeJunctions=true; in.nodesUseRim=true;
        in.candidateNodes={{"LOW",{30,50},8,true},{"HIGH",{70,50},12,true}};
        const auto r=run(in);
        QVERIFY2(r.ok,qPrintable(r.errorMsg));
        const auto zAt=[&](QPointF p) {
            double best=1e30,zz=qQNaN();
            for(const auto &v:r.meshResult.vertices) { const double d=QLineF(v.xy,p).length(); if(d<best) {best=d;zz=v.z;} }
            return best<1e-6?zz:qQNaN();
        };
        QCOMPARE(zAt({30,50}),8.0);    // terrain 10 above the rim: lowered to it
        QCOMPARE(zAt({70,50}),10.0);   // terrain 10 below the rim 12: terrain kept
    }

    void largeTerrainPipelineBenchmark()
    {
        const int target=qEnvironmentVariableIntValue("SWMMVIS_TERRAIN_SCALE_CELLS");
        const bool channel=qEnvironmentVariableIsSet("SWMMVIS_CHANNEL_SCALE");
        if(target<100000) QSKIP("Opt-in 1/5/10 million-cell terrain pipeline benchmark.");
        const QString root=qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT");
        QVERIFY(!root.isEmpty());
        const QDir dir(root+QStringLiteral("/scale-%1").arg(target));
        QVERIFY(QDir().mkpath(dir.path()));
        Inputs inputs; inputs.inpPath=dir.filePath("model.inp");
        QVERIFY(writeBytes(inputs.inpPath,"[TITLE]\nTerrain scale benchmark\n[OPTIONS]\nFLOW_UNITS CMS\n"));
        inputs.dtmPath=dir.filePath("plane.tif");
        const int pixels=int(std::ceil(std::sqrt(4.*target)))+4;
        GDALAllRegister();
        auto *ds=GetGDALDriverManager()->GetDriverByName("GTiff")->Create(inputs.dtmPath.toUtf8().constData(),pixels,pixels,1,GDT_Float32,nullptr);
        QVERIFY(ds); const double pitch=1000./(pixels-4);
        double gt[6]={-2*pitch,pitch,0,1000+2*pitch,0,-pitch};
        QCOMPARE(ds->SetGeoTransform(gt),CE_None);
        QVector<float> row(pixels);
        for(int r=0;r<pixels;++r) {
            for(int c=0;c<pixels;++c) row[c]=channel?12.f:float(10+.004*(c+.5)*pitch-.008*(r+.5)*pitch);
            QCOMPARE(ds->GetRasterBand(1)->RasterIO(GF_Write,0,r,pixels,1,row.data(),pixels,1,GDT_Float32,0,0),CE_None);
        }
        GDALClose(ds);
        inputs.modelExtent=MapExtent(0,0,1000,1000);
        inputs.domains={QPolygonF(QVector<QPointF>{{0,0},{1000,0},{1000,1000},{0,1000}})};
        inputs.meshLinearUnitName="metre";
        inputs.cellSize=std::sqrt(3.34e6/target)/1.22;
        inputs.coarsenFactor=1; inputs.minCellSize=inputs.cellSize/4;
        inputs.genOpts.minCellSize=inputs.minCellSize;
        inputs.genOpts.maxArea=.4330127018922193*inputs.cellSize*inputs.cellSize;
        inputs.genOpts.maxCells=20'000'000;
        inputs.terrainTolerance=.01; inputs.terrainAdaptive=true; inputs.terrainCacheMiB=16;
        inputs.terrainBreaklines=true; inputs.mapNodesAfterGen=false;
        if(channel) {
            inputs.burnEnabled=true;inputs.burnOutputDir=dir.filePath("terrain");
            inputs.burnOptions.clipToBanks=false;inputs.burnOptions.channelCellSize=inputs.cellSize*4;
            mesh::ChannelInput creek;creek.conduitId="channel";creek.centerline={{-1,500},{1001,500}};
            creek.zUp=creek.zDn=10;creek.section=mesh::sectionFromWidths({0,2},{4,12});
            inputs.burnProfiles={mesh::buildBurnProfile(creek,inputs.burnOptions)};
        }
        QElapsedTimer timer; timer.start();
        const auto result=run(inputs); const qint64 ms=timer.elapsed();
        QVERIFY2(result.ok,qPrintable(result.errorMsg));
        qint64 rss=0;
#ifdef Q_OS_UNIX
        struct rusage usage{}; getrusage(RUSAGE_SELF,&usage); rss=usage.ru_maxrss;
#ifndef Q_OS_MACOS
        rss*=1024;
#endif
#endif
        qInfo("pipeline scale: target=%d cells=%lld DEM_samples=%lld ms=%lld peakRSS=%lld",target,
            (long long)result.meshResult.triangles.size(),(long long)pixels*pixels,(long long)ms,(long long)rss);
        if(!channel) QCOMPARE(result.generationStats.terrainInserted,0);
        QCOMPARE(result.generationStats.terrainUnresolved,0);
        QCOMPARE(result.generationStats.terrainUnknown,0);
        QVERIFY(!result.generationStats.refineCapped);
        QVERIFY(result.meshResult.triangles.size()>target*.85);
    }
    // Opt-in reproduction of a real project through the dialog's own input
    // collection: SWMMVIS_REPRO_INP, SWMMVIS_REPRO_DEM, SWMMVIS_REPRO_BOUNDARY
    // (vector file), SWMMVIS_REPRO_OUT (review folder). Optional:
    // SWMMVIS_REPRO_CELL / _MINCELL (model length units), _COARSEN, _BURN=0,
    // _QUADCORRIDOR=0|1, _MAXCELLS (cell budget).
    // Writes the final mesh.2dm and before/after timings in report.txt under _OUT.
    void projectReproduction()
    {
        const QString inp=qEnvironmentVariable("SWMMVIS_REPRO_INP");
        if(inp.isEmpty()) QSKIP("Opt-in project reproduction.");
        const QString demPath=qEnvironmentVariable("SWMMVIS_REPRO_DEM");
        const QString boundaryPath=qEnvironmentVariable("SWMMVIS_REPRO_BOUNDARY");
        const QDir out(qEnvironmentVariable("SWMMVIS_REPRO_OUT"));
        QVERIFY(!demPath.isEmpty() && !boundaryPath.isEmpty() && !out.path().isEmpty());
        QVERIFY(QDir().mkpath(out.path()));
        QStringList report;
        QElapsedTimer clock; clock.start();
        auto workspace=std::unique_ptr<OpenSWMMVisWorkspace>(OpenSWMMVisWorkspace::newInstance(QString(),nullptr));
        auto window=std::make_unique<SWMMVisProjectWindow>(workspace.get(),inp,nullptr);
        QList<QString> warnings,errors; QString error;
        QVERIFY2(window->loadModel(warnings,errors),qPrintable(errors.join('\n')));
        report << QStringLiteral("model load: %1 s").arg(clock.restart()/1000.0);
        const auto restoreUnits=qScopeGuard([previous=UnitSystem::activeProject()]{UnitSystem::setActiveProject(previous);});
        UnitSystem::setActiveProject(window->unitSystem());
        auto *dem=new GISRasterLayer(demPath);
        auto *boundary=new GISVectorLayer(boundaryPath,QFileInfo(boundaryPath).completeBaseName());
        window->canvas()->addLayer(dem,false);
        window->canvas()->addLayer(boundary,false);
        MeshGenerationDialog dialog(window.get(),nullptr);
        const int demIndex=dialog.m_dtmCombo->findData(QVariant::fromValue<void *>(dem));
        QVERIFY(demIndex>=0); dialog.m_dtmCombo->setCurrentIndex(demIndex);
        const int bIndex=dialog.m_boundaryLayerCombo->findData(QVariant::fromValue<void *>(boundary));
        QVERIFY(bIndex>=0); dialog.m_boundaryLayerCombo->setCurrentIndex(bIndex);
        auto spin=[&](const char *name,const char *env){
            if(!qEnvironmentVariableIsSet(env)) return;
            auto *w=dialog.findChild<QDoubleSpinBox *>(QString::fromLatin1(name)); QVERIFY(w);
            w->setValue(qEnvironmentVariable(env).toDouble());
        };
        spin("meshCellSizeSpin","SWMMVIS_REPRO_CELL");
        spin("meshMinCellSizeSpin","SWMMVIS_REPRO_MINCELL");
        spin("meshCoarsenSpin","SWMMVIS_REPRO_COARSEN");
        spin("meshTerrainTolSpin","SWMMVIS_REPRO_TERRAINTOL");
        if(qEnvironmentVariableIsSet("SWMMVIS_REPRO_QUADMODE"))
            if(auto *mode=dialog.findChild<QComboBox *>(QStringLiteral("meshQuadModeCombo")))
                mode->setCurrentIndex(qEnvironmentVariableIntValue("SWMMVIS_REPRO_QUADMODE"));
        if(qEnvironmentVariableIsSet("SWMMVIS_REPRO_TERRAINREF"))
            if(auto *ref=dialog.findChild<QComboBox *>(QStringLiteral("meshTerrainReferenceCombo")))
                ref->setCurrentIndex(qEnvironmentVariableIntValue("SWMMVIS_REPRO_TERRAINREF"));
        if(qEnvironmentVariableIsSet("SWMMVIS_REPRO_CONDUITS"))
            if(auto *conduits=dialog.findChild<QCheckBox *>(QStringLiteral("meshConduitsBox")))
                conduits->setChecked(qEnvironmentVariable("SWMMVIS_REPRO_CONDUITS")!="0");
        if(qEnvironmentVariableIsSet("SWMMVIS_REPRO_MAXCELLS"))
            if(auto *cap=dialog.findChild<QSpinBox *>(QStringLiteral("meshMaxCellsSpin")))
                cap->setValue(qEnvironmentVariableIntValue("SWMMVIS_REPRO_MAXCELLS"));
        Inputs in; QVERIFY2(dialog.collectInputs(&in,&error),qPrintable(error));
        // Every writable generation artifact, including its terrain cache,
        // stays in the reproduction folder even when source files are external.
        QFile sourceModel(inp);QVERIFY(sourceModel.open(QIODevice::ReadOnly));
        in.inpPath=out.filePath("benchmark.inp");
        QVERIFY(writeBytes(in.inpPath,sourceModel.readAll()));
        in.meshOutputPath=out.filePath("benchmark.2dm");
        in.burnOutputDir=out.filePath("terrain");
        report << QStringLiteral("inputs: cell %1, min cell %2, coarsen %3, terrain tol %4 (auto %5, adaptive %6), breaklines %7, burn %8 (%9 profiles), cache %10 MiB")
            .arg(in.cellSize).arg(in.minCellSize).arg(in.coarsenFactor).arg(in.terrainTolerance)
            .arg(in.terrainAutoTolerance).arg(in.terrainAdaptive).arg(in.terrainBreaklines)
            .arg(in.burnEnabled).arg(in.burnProfiles.size()).arg(in.terrainCacheMiB);
        report << QStringLiteral("collect inputs: %1 s").arg(clock.restart()/1000.0);
        Result result;
        const QString preparedMesh=qEnvironmentVariable("SWMMVIS_REPRO_MESH");
        if(preparedMesh.isEmpty()) result=run(in);
        else {
            result.meshResult=mesh::Sms2dmReader::read(preparedMesh);
            result.ok=result.meshResult.ok;result.errorMsg=result.meshResult.errorMsg;
            result.meshPath=in.meshOutputPath;result.outputMode=in.outputMode;
            result.meshUnitsSI=mesh::unitsHeaderIsSI(in.meshLinearUnitName);
            result.verticalUnitToSI=in.verticalUnitToSI;
            for(auto &cell:result.meshResult.triangles) {cell.mannings=in.manningsN;cell.initDepth=in.initDepth;}
            report << QStringLiteral("diagnostic ambient mesh imported from %1 (SMS contains geometry only)").arg(preparedMesh);
        }
        const double generationSeconds=clock.restart()/1000.0;
        report << QStringLiteral("generation: %1 s").arg(generationSeconds);
        auto peakRSS=[]() -> qint64 {
#ifdef Q_OS_UNIX
            struct rusage usage{}; getrusage(RUSAGE_SELF,&usage);
#ifdef Q_OS_MACOS
            return usage.ru_maxrss;
#else
            return qint64(usage.ru_maxrss)*1024;
#endif
#else
            return 0;
#endif
        };
        qint64 productionPeakRSS=peakRSS();
        if(result.ok && qEnvironmentVariable("SWMMVIS_REPRO_BURN")!="0") {
            QVERIFY2(adoptBurn(window.get(),result,&error),qPrintable(error));
            ChannelBurnDialog burnDialog(window.get(),nullptr);
            if(qEnvironmentVariableIsSet("SWMMVIS_REPRO_MAXCELLS"))
                burnDialog.m_maxCells->setValue(qEnvironmentVariableIntValue("SWMMVIS_REPRO_MAXCELLS"));
            if(qEnvironmentVariableIsSet("SWMMVIS_REPRO_QUADCORRIDOR"))
                burnDialog.m_quads->setChecked(qEnvironmentVariable("SWMMVIS_REPRO_QUADCORRIDOR")!="0");
            mesh::ChannelMeshBurnInputs selected;
            QVERIFY2(burnDialog.collectInputs(&selected,&error),qPrintable(error));
            in.burnEnabled=true;in.burnOptions=selected.options;
            in.burnProfiles=selected.profiles;in.burnNetwork=selected.network;in.couplingNodes=selected.nodes;
            QElapsedTimer kernelClock;kernelClock.start();
            result=burn(std::move(result),in);
            const double kernelSeconds=kernelClock.elapsed()/1000.0;
            productionPeakRSS=peakRSS();
            report << QStringLiteral("post-mesh burn kernel: %1 s; process peak RSS before diagnostic serialization: %2 GB")
                .arg(kernelSeconds).arg(productionPeakRSS/1e9,0,'f',2);
            QJsonArray profiles;
            auto numbers=[](const QVector<double> &values){QJsonArray array;for(double value:values)array.append(value);return array;};
            for(const auto &profile:selected.profiles) {
                QJsonArray centerline,relZ;
                for(const auto &point:profile.centerline)centerline.append(QJsonArray{point.x(),point.y()});
                for(const auto &row:profile.relZ)relZ.append(numbers(row));
                const auto &section=profile.section;
                profiles.append(QJsonObject{{"id",profile.conduitId},{"centerline",centerline},
                    {"chainage",numbers(profile.chainage)},{"bedZ",numbers(profile.bedZ)},
                    {"offsets",numbers(profile.offsets)},{"relZ",relZ},
                    {"section",QJsonObject{{"station",numbers(section.station)},{"relZ",numbers(section.relZ)},
                        {"sMin",section.sMin},{"sMax",section.sMax},{"leftBank",section.leftBank},{"rightBank",section.rightBank},
                        {"nLeft",section.nLeft},{"nChannel",section.nChannel},{"nRight",section.nRight}}}});
            }
            QVERIFY(writeBytes(out.filePath("burn_profiles.json"),QJsonDocument(QJsonObject{{"profiles",profiles},
                {"verticalUnitToSI",in.verticalUnitToSI},{"forceHalfWidth",in.burnOptions.forceHalfWidth},
                {"aspectMax",in.burnOptions.channelAspectMax},{"channelCellSize",in.burnOptions.channelCellSize}}).toJson()));
            QJsonArray splits,coupledNodes,nodePlans;
            for(const auto &split:std::as_const(result.burnSurgery.splits))
                splits.append(QJsonObject{{"node",split.nodeId},{"link",split.linkId},{"x",split.xy.x()},{"y",split.xy.y()},{"t",split.t}});
            QSet<QString> coupledIds;
            for(const auto &vertex:std::as_const(result.meshResult.vertices))
                if(!vertex.coupledNode.isEmpty())coupledIds.insert(vertex.coupledNode);
            for(const auto &cell:std::as_const(result.meshResult.cellCouplings))coupledIds.insert(cell.nodeId);
            QStringList sortedCoupledIds=coupledIds.values();sortedCoupledIds.sort();
            for(const auto &id:std::as_const(sortedCoupledIds))coupledNodes.append(id);
            for(const auto &node:std::as_const(result.burnSurgery.nodePlans))
                nodePlans.append(QJsonObject{{"node",node.nodeId},{"role",int(node.role)}});
            QVERIFY(writeBytes(out.filePath("burn_surgery.json"),QJsonDocument(QJsonObject{{"splits",splits},
                {"nodePlans",nodePlans},{"coupledNodes",coupledNodes}}).toJson()));
            report << QStringLiteral("post-mesh burn: %1 s").arg(clock.elapsed()/1000.0);
            report << QStringLiteral("touched cells before: %1 cells, %2 quads, min angle %3, min edge %4, below25 %5, below10 %6")
                .arg(result.burnBefore.cells).arg(result.burnBefore.quads).arg(result.burnBefore.minAngle)
                .arg(result.burnBefore.minEdge).arg(result.burnBefore.below25).arg(result.burnBefore.below10);
            report << QStringLiteral("touched cells after: %1 cells, %2 quads, min angle %3, min edge %4, below25 %5, below10 %6; cavities %7, fallbacks %8")
                .arg(result.burnAfter.cells).arg(result.burnAfter.quads).arg(result.burnAfter.minAngle)
                .arg(result.burnAfter.minEdge).arg(result.burnAfter.below25).arg(result.burnAfter.below10)
                .arg(result.burnCavities).arg(result.burnFallbackCavities);
        }
        const double workerSeconds=generationSeconds+clock.restart()/1000.0;
        qint64 rss=0;
#ifdef Q_OS_UNIX
        struct rusage usage{}; getrusage(RUSAGE_SELF,&usage); rss=usage.ru_maxrss;
#ifndef Q_OS_MACOS
        rss*=1024;
#endif
#endif
        report << QStringLiteral("worker: %1 s, ok %2, peak RSS including diagnostics %3 GB%4").arg(workerSeconds).arg(result.ok)
            .arg(rss/1e9,0,'f',2).arg(result.ok?QString():QStringLiteral(", error: ")+result.errorMsg);
        if(!result.burnWarnings.isEmpty()) {
            QFile warnings(out.filePath("burn_warnings.txt"));
            QVERIFY(warnings.open(QIODevice::WriteOnly|QIODevice::Text));
            warnings.write(result.burnWarnings.join('\n').toUtf8()+'\n');
        }
        if(!result.meshResult.triangles.isEmpty()) {
            const auto &m=result.meshResult;
            int quads=0,tris=0,below20=0,below10=0; double minEdge=1e300,maxEdge=0,minAngle=180;
            for(const auto &c:m.triangles) {
                const int nv=c.vertexCount(); (nv==4?quads:tris)++;
                double cellMin=180;
                for(int k=0;k<nv;++k) {
                    const QPointF a=m.vertices[c.vertex(k)].xy,b=m.vertices[c.vertex((k+1)%nv)].xy,
                                  p=m.vertices[c.vertex((k+nv-1)%nv)].xy;
                    const double e=QLineF(a,b).length(); minEdge=std::min(minEdge,e); maxEdge=std::max(maxEdge,e);
                    const QPointF u=b-a,v=p-a; const double lu=std::hypot(u.x(),u.y()),lv=std::hypot(v.x(),v.y());
                    if(lu>0 && lv>0) cellMin=std::min(cellMin,std::acos(std::clamp((u.x()*v.x()+u.y()*v.y())/(lu*lv),-1.0,1.0))*180/M_PI);
                }
                minAngle=std::min(minAngle,cellMin); if(cellMin<20) ++below20; if(cellMin<10) ++below10;
            }
            const auto &g=result.generationStats;
            report << QStringLiteral("mesh: %1 cells (%2 tri, %3 quad), %4 vertices; edge %5..%6; min angle %7 deg; cells <20 deg %8, <10 deg %9")
                .arg(m.triangles.size()).arg(tris).arg(quads).arg(m.vertices.size()).arg(minEdge).arg(maxEdge)
                .arg(minAngle).arg(below20).arg(below10);
            report << QStringLiteral("generation: strips %1 conduit / %2 breakline / %3 region, dropped %4; inserted size %5 quality %6 terrain %7 splits %8; below-angle %9; capped %10; terrain unresolved %11 unknown %12")
                .arg(g.conduitStrips).arg(g.breaklineStrips).arg(g.regionPatches).arg(g.stripsDropped)
                .arg(g.sizeInserted).arg(g.qualityInserted).arg(g.terrainInserted).arg(g.segmentSplits)
                .arg(g.trianglesBelowAngle).arg(g.refineCapped).arg(g.terrainUnresolved).arg(g.terrainUnknown);
            report << QStringLiteral("burn: ran %1, conduits burned %2, warnings %3").arg(result.burnRan)
                .arg(result.burnSurgery.burnedConduits.size()).arg(result.burnWarnings.size());
            for(const auto &w:result.burnWarnings.mid(0,20)) report << QStringLiteral("  burn warning: ")+w;
            QFile mesh(out.filePath(result.ok?"mesh.2dm":"mesh_before_failed_burn.2dm"));
            QVERIFY(mesh.open(QIODevice::WriteOnly|QIODevice::Text));
            QTextStream ts(&mesh); ts.setRealNumberPrecision(17);
            ts << "MESH2D\n";
            for(int i=0;i<m.triangles.size();++i) {
                const auto &c=m.triangles[i];
                if(c.isQuad()) ts << "E4Q " << i+1 << ' ' << c.v0+1 << ' ' << c.v1+1 << ' ' << c.v2+1 << ' ' << c.v3+1 << " 1\n";
                else ts << "E3T " << i+1 << ' ' << c.v0+1 << ' ' << c.v1+1 << ' ' << c.v2+1 << " 1\n";
            }
            for(int i=0;i<m.vertices.size();++i)
                ts << "ND " << i+1 << ' ' << m.vertices[i].xy.x() << ' ' << m.vertices[i].xy.y() << ' ' << m.vertices[i].z << '\n';
        }
        QFile f(out.filePath("report.txt"));
        QVERIFY(f.open(QIODevice::WriteOnly|QIODevice::Text));
        f.write(report.join('\n').toUtf8()+'\n');
        qInfo("%s",qPrintable(report.join('\n')));
        QVERIFY2(result.ok,qPrintable(result.errorMsg));
    }
    // Opt-in whole-worker timing on a real DEM (SWMMVIS_MESH_LARGEDEM=<GeoTIFF>)
    // over its full extent. Run with QT_LOGGING_RULES="openswmm.mesh.perf=true"
    // for per-stage times. Optional: _TOL (m, default .5), _CACHE_MIB (default 1024),
    // _CELL (max cell size, default extent/64), _BREAKLINES=0.
    void largeDemPipelineBenchmark()
    {
        const QString dem=qEnvironmentVariable("SWMMVIS_MESH_LARGEDEM");
        if(dem.isEmpty()) QSKIP("Opt-in large-DEM worker benchmark.");
        const QString root=qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT");
        QVERIFY(!root.isEmpty());
        const QDir dir(root+"/largedem-"+QFileInfo(dem).completeBaseName());
        QVERIFY(QDir().mkpath(dir.path()));
        GDALAllRegister();
        auto *ds=static_cast<GDALDataset *>(GDALOpen(dem.toUtf8().constData(),GA_ReadOnly));
        QVERIFY(ds);
        double gt[6]; QCOMPARE(ds->GetGeoTransform(gt),CE_None);
        const QRectF extent=QRectF(QPointF(gt[0],gt[3]),
            QPointF(gt[0]+ds->GetRasterXSize()*gt[1],gt[3]+ds->GetRasterYSize()*gt[5])).normalized();
        const QString crs=QString::fromUtf8(ds->GetProjectionRef());
        const qint64 samples=qint64(ds->GetRasterXSize())*ds->GetRasterYSize();
        GDALClose(ds);
        const double pixel=std::abs(gt[1]);
        const QRectF d=extent.adjusted(pixel,pixel,-pixel,-pixel);
        Inputs inputs; inputs.inpPath=dir.filePath("model.inp");
        QVERIFY(writeBytes(inputs.inpPath,"[TITLE]\nLarge DEM benchmark\n[OPTIONS]\nFLOW_UNITS CMS\n"));
        inputs.dtmPath=dem;
        inputs.meshCRSWkt=crs;
        inputs.modelExtent=MapExtent(d.left(),d.top(),d.right(),d.bottom());
        inputs.domains={QPolygonF(d)};
        inputs.meshLinearUnitName="metre";
        const double cell=qEnvironmentVariableIsSet("SWMMVIS_MESH_LARGEDEM_CELL")
            ? qEnvironmentVariable("SWMMVIS_MESH_LARGEDEM_CELL").toDouble() : std::max(d.width(),d.height())/64;
        inputs.cellSize=cell; inputs.coarsenFactor=1; inputs.minCellSize=2*pixel;
        inputs.genOpts.minCellSize=inputs.minCellSize;
        inputs.genOpts.maxArea=.4330127018922193*cell*cell;
        inputs.genOpts.maxCells=20'000'000;
        inputs.terrainTolerance=qEnvironmentVariableIsSet("SWMMVIS_MESH_LARGEDEM_TOL")
            ? qEnvironmentVariable("SWMMVIS_MESH_LARGEDEM_TOL").toDouble() : .5;
        inputs.terrainAdaptive=true;
        inputs.terrainCacheMiB=qEnvironmentVariableIsSet("SWMMVIS_MESH_LARGEDEM_CACHE_MIB")
            ? qEnvironmentVariableIntValue("SWMMVIS_MESH_LARGEDEM_CACHE_MIB") : 1024;
        inputs.terrainBreaklines=qEnvironmentVariable("SWMMVIS_MESH_LARGEDEM_BREAKLINES")!="0";
        inputs.mapNodesAfterGen=false;
        QElapsedTimer timer; timer.start();
        const auto result=run(inputs); const qint64 ms=timer.elapsed();
        QVERIFY2(result.ok,qPrintable(result.errorMsg));
        qint64 rss=0;
#ifdef Q_OS_UNIX
        struct rusage usage{}; getrusage(RUSAGE_SELF,&usage); rss=usage.ru_maxrss;
#ifndef Q_OS_MACOS
        rss*=1024;
#endif
#endif
        // Exact mesh fingerprint, so cached and uncached runs can be compared.
        QCryptographicHash hash(QCryptographicHash::Sha256);
        for(const auto &v:result.meshResult.vertices) {
            const double xyz[3]={v.xy.x(),v.xy.y(),v.z};
            hash.addData(QByteArrayView(reinterpret_cast<const char *>(xyz),sizeof xyz));
        }
        for(const auto &t:result.meshResult.triangles) {
            const int ids[4]={t.v0,t.v1,t.v2,t.v3};
            hash.addData(QByteArrayView(reinterpret_cast<const char *>(ids),sizeof ids));
        }
        qInfo("largedem pipeline: dem=%s samples=%lld cells=%lld terrainInserted=%d ms=%lld peakRSS=%lld cache=%d sha256=%s",
            qPrintable(QFileInfo(dem).fileName()),(long long)samples,(long long)result.meshResult.triangles.size(),
            result.generationStats.terrainInserted,(long long)ms,(long long)rss,inputs.terrainCacheMiB,
            hash.result().toHex().left(16).constData());
    }

    void bankPairReachesWorker_data()
    {
        QTest::addColumn<bool>("variable");
        QTest::newRow("straight") << false;
        QTest::newRow("varying") << true;
    }

    void bankPairReachesWorker()
    {
        QFETCH(bool, variable);
        const QDir dir(qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT") + "/bank_pair/" + QTest::currentDataTag());
        Inputs inputs;
        QVERIFY(prepareBankFixture(dir, inputs, variable));
        inputs.outputMode = mesh::MeshOutputMode::External;
        inputs.meshOutputPath = dir.filePath("pending.2dm");
        QFile saved(inputs.inpPath); QVERIFY(saved.open(QIODevice::ReadOnly));
        const QByteArray before = saved.readAll(); saved.close();
        const auto generated = run(inputs);
        QVERIFY2(generated.ok, qPrintable(generated.errorMsg));
        QVERIFY(!generated.burnRan);
        QCOMPARE(generated.corridorSources.size(), 1);
        QVERIFY(generated.corridorSources.first().bankPair);
        QVERIFY(!generated.corridorSources.first().geometryDigest.isEmpty());
        double domainArea = 0, corridorArea = 0;
        int cells = 0;
        QSet<int> corridorVertices;
        for (const auto &cell : generated.meshResult.triangles) {
            const double area = mesh::cellGeom(generated.meshResult.vertices, cell).area;
            domainArea += area;
            if (cell.tag != "river") continue;
            QVERIFY(cell.isQuad());
            ++cells; corridorArea += area;
            for (int id : {cell.v0, cell.v1, cell.v2, cell.v3}) corridorVertices.insert(id);
            if (!variable) {
                const auto &v = generated.meshResult.vertices;
                const double a = QLineF(v[cell.v0].xy, v[cell.v1].xy).length();
                const double b = QLineF(v[cell.v1].xy, v[cell.v2].xy).length();
                QVERIFY(std::abs(std::max(a,b) - 6) < 1e-8);
                QVERIFY(std::abs(std::min(a,b) - 2) < 1e-8);
            }
        }
        if (!variable) QCOMPARE(cells, 8);
        QVERIFY(std::abs(corridorArea - (variable ? 144.0 : 96.0)) < 1e-7);
        QVERIFY(std::abs(domainArea - 1024) < 1e-7);
        if (variable) {
            bool bankVertexRetained = false;
            for (int id : corridorVertices)
                bankVertexRetained |= QLineF(generated.meshResult.vertices[id].xy, QPointF(16,21)).length() < 1e-9;
            QVERIFY(bankVertexRetained);
        }
        QVERIFY(!QFileInfo::exists(inputs.meshOutputPath));
        QVERIFY(saved.open(QIODevice::ReadOnly)); QCOMPARE(saved.readAll(), before);
        inputs.corridorSources = generated.corridorSources;
        inputs.corridorSources[0].featureIds = {7,999};
        const auto refused = run(inputs);
        QVERIFY(!refused.ok); QVERIFY(refused.corridorSources.isEmpty());
        QVERIFY(!QFileInfo::exists(inputs.meshOutputPath));
    }

    void quadRegionLayer_data()
    {
        QTest::addColumn<QString>("attributes");
        QTest::addColumn<bool>("tagged");
        QTest::newRow("spacing-and-angle") << QString("\"quad_spacing\":2,\"quad_angle\":0") << true;
        QTest::newRow("spacing-only") << QString("\"quad_spacing\":2") << true;
        QTest::newRow("text-spacing") << QString("\"quad_spacing\":\"two\"") << true;   // unreadable spacing = no override
        QTest::newRow("all-null") << QString("\"quad_spacing\":null,\"quad_angle\":null") << true;
        QTest::newRow("missing-source-crs") << QString("\"quad_spacing\":2") << true;    // layer already in mesh CRS
        QTest::newRow("missing-source-file") << QString("\"quad_spacing\":2") << false;
        QTest::newRow("empty-multipart") << QString("\"quad_spacing\":2") << false;
    }

    /*! A quad-region layer carries a spacing and a tag (an angle attribute
     *  is ignored: a four-sided ring's quads follow its sides,
     *  MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md D12.1): cells inside the ring
     *  take the tag, a readable spacing refines them, and an unreadable or
     *  missing layer leaves the mesh untagged rather than failing it. */
    void quadRegionLayer()
    {
        QFETCH(QString, attributes);
        QFETCH(bool, tagged);
        Inputs inputs;
        const QDir dir(qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT") + "/quad_region_layer/" + QTest::currentDataTag());
        QVERIFY(prepareCorridorFixture(dir, inputs));
        inputs.corridorSources.clear();
        const QString path = dir.absoluteFilePath("regions.geojson");
        const QString scenario = QString::fromLatin1(QTest::currentDataTag());
        QByteArray json = QString(R"({"type":"FeatureCollection","name":"regions","crs":{"type":"name","properties":{"name":"EPSG:3857"}},"features":[{"type":"Feature","properties":{"tag":"directed",%1},"geometry":{"type":"Polygon","coordinates":[[[4,12],[28,12],[28,20],[4,20],[4,12]]]}}]})").arg(attributes).toUtf8();
        if (scenario == "empty-multipart")
            json.replace("\"type\":\"Polygon\",\"coordinates\":[[[4,12],[28,12],[28,20],[4,20],[4,12]]]",
                         "\"type\":\"MultiPolygon\",\"coordinates\":[]");
        QVERIFY(writeBytes(path, json));
        inputs.quadRegionLayers = {{path, "regions", inputs.meshCRSWkt}};
        if (scenario == "missing-source-crs") inputs.quadRegionLayers[0].crsWkt.clear();
        if (scenario == "missing-source-file") inputs.quadRegionLayers[0].path += ".missing";
        const auto generated = run(inputs);
        QVERIFY2(generated.ok, qPrintable(generated.errorMsg));
        int cells = 0;
        double totalArea = 0, taggedArea = 0;
        for (const auto &cell : generated.meshResult.triangles) {
            const double area = mesh::cellGeom(generated.meshResult.vertices, cell).area;
            totalArea += area;
            if (cell.tag != "directed") continue;
            ++cells;
            taggedArea += area;
        }
        QVERIFY(std::abs(totalArea - 1024.0) < 1e-7);
        if (!tagged) {
            QCOMPARE(cells, 0);
        } else {
            QVERIFY(cells > 0);
            QVERIFY2(std::abs(taggedArea - 192.0) < 1e-7,
                     qPrintable(QString("tagged area %1, expected the 24x8 ring").arg(taggedArea, 0, 'g', 17)));
            // A readable spacing of 2 refines the ring well below the
            // background (maxArea 64): mean tagged cell area under 8.
            const bool refined = scenario != "text-spacing" && scenario != "all-null";
            const double meanArea = taggedArea / cells;
            if (refined) QVERIFY2(meanArea < 8.0, qPrintable(QString("mean tagged area %1").arg(meanArea)));
            else QVERIFY2(meanArea > 8.0, qPrintable(QString("mean tagged area %1").arg(meanArea)));
        }
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

    void selectedCorridorReachesWorkerWithoutBurn()
    {
        const QString root = qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT");
        const QDir dir(root + "/gis_corridor");
        Inputs inputs;
        QVERIFY(prepareCorridorFixture(dir, inputs));
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
        QTest::addColumn<bool>("bankPair");
        for (bool banks : {false, true})
            for (const char *outcome : {"success", "stale", "source", "failure", "cancel"})
                QTest::newRow(qPrintable(QString(banks ? "banks-" : "centerline-") + outcome)) << QString(outcome) << banks;
    }

    void corridorRecipeAdoptedOnlyOnSuccess()
    {
        QFETCH(QString, outcome);
        QFETCH(bool, bankPair);
        Inputs inputs;
        const QDir dir(qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT") + "/gis_adoption/" + QTest::currentDataTag());
        QVERIFY(bankPair ? prepareBankFixture(dir, inputs) : prepareCorridorFixture(dir, inputs));
        auto result = run(inputs);
        QVERIFY2(result.ok, qPrintable(result.errorMsg));
        auto workspace = std::unique_ptr<OpenSWMMVisWorkspace>(OpenSWMMVisWorkspace::newInstance(QString(), nullptr));
        const QString fixture = QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", ".")).filePath("typed_selection_fixture.inp");
        auto window = std::make_unique<SWMMVisProjectWindow>(workspace.get(), fixture, nullptr);
        QList<QString> warnings, errors;
        QVERIFY2(window->loadModel(warnings, errors), qPrintable(errors.join('\n')));
        auto old = inputs.corridorSources.first();
        old.bankPair = false;
        old.featureIds = {7};
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
        QPromise<GenerationResult> promise;
        promise.start();
        promise.addResult(result);
        promise.finish();
        dialog.m_watcher = new QFutureWatcher<GenerationResult>(&dialog);
        dialog.m_watcher->setFuture(promise.future());
        if (outcome != "success")
            QTimer::singleShot(0, [] {
                for (auto *widget : QApplication::topLevelWidgets())
                    if (auto *box = qobject_cast<QMessageBox *>(widget)) box->accept();
            });
        dialog.onMeshFinished();
        QCOMPARE(window->corridorSources().first().tag, outcome == "success" ? QString("river") : QString("previous"));
        QCOMPARE(window->corridorSources().first().bankPair, outcome == "success" && bankPair);
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
        // Burn validates the requested profile against a completed terrain mesh.
        inputs.burnOptions.quadCorridor = true;
        if (invalidLattice) inputs.burnProfiles[0].offsets.clear();
        else inputs.burnProfiles[0].offsets.fill(0.0);
        QFile model(inputs.inpPath);
        QVERIFY(model.open(QIODevice::ReadOnly));
        const auto saved = model.readAll();
        model.close();
        const auto result = run(inputs);
        QVERIFY2(!result.ok, "An explicitly requested invalid corridor must stop generation.");
        QVERIFY2(!result.errorMsg.isEmpty(), qPrintable(result.errorMsg));
        QVERIFY(model.open(QIODevice::ReadOnly));
        QCOMPARE(model.readAll(), saved);
    }
    void burnKeepsSavedFilesUntilSave_data()
    {
        QTest::addColumn<bool>("existing");
        QTest::addColumn<bool>("laterFailure");
        QTest::newRow("existing-success") << true << false;
        QTest::newRow("new-success") << false << false;
        QTest::newRow("existing-generation-failure") << true << true;
        QTest::newRow("new-generation-failure") << false << true;
    }

    void burnKeepsSavedFilesUntilSave()
    {
        QFETCH(bool,existing);QFETCH(bool,laterFailure);
        const QDir dir(qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT")+
                       "/burn_staging/"+QString::fromLatin1(QTest::currentDataTag()));
        Inputs inputs;QVERIFY(prepareBurnFixture(dir,inputs,existing));inputs.exportRaster=true;
        const auto savedFiles=QDir(inputs.burnOutputDir).entryList(QDir::Files);
        QFile source(inputs.dtmPath);QVERIFY(source.open(QIODevice::ReadOnly));
        const auto originalSource=source.readAll();source.close();
        if(laterFailure) inputs.genOpts.maxArea=0;
        auto generated=run(inputs);
        QCOMPARE(generated.ok,!laterFailure);
        QCOMPARE(QDir(inputs.burnOutputDir).entryList(QDir::Files),savedFiles);
        QVERIFY(source.open(QIODevice::ReadOnly));QCOMPARE(source.readAll(),originalSource);
        if(laterFailure) {
            QVERIFY2(generated.errorMsg.contains("no cell size"),qPrintable(generated.errorMsg));
            QVERIFY(!generated.generatedArtifacts);
        } else {
            QVERIFY(generated.burnRan);QVERIFY(generated.generatedArtifacts);
            QVERIFY(!QFileInfo::exists(generated.burnedDemPath));
            QVERIFY(!QFileInfo::exists(generated.burnReportPath));
            const auto artifacts=generated.generatedArtifacts;
            QCOMPARE(artifacts->entries().size(),3);
            QString physicalDem,physicalReport;
            for(const auto &entry:artifacts->entries()) {
                QVERIFY(QFileInfo(entry.stagedPath).size()>0);
                QCOMPARE(QFileInfo(entry.stagedPath).absolutePath(),artifacts->directoryPath());
                if(entry.finalPath==generated.burnedDemPath) physicalDem=entry.stagedPath;
                if(entry.finalPath==generated.burnReportPath) physicalReport=entry.stagedPath;
            }
            QVERIFY(!physicalDem.isEmpty());QVERIFY(!physicalReport.isEmpty());
            QFile report(physicalReport);QVERIFY(report.open(QIODevice::ReadOnly));
            const auto reportBytes=report.readAll();
            QVERIFY(reportBytes.contains(("# burned DEM,"+generated.burnedDemPath+"\n").toUtf8()));
            QVERIFY(!reportBytes.contains(artifacts->directoryPath().toUtf8()));
            auto *raster=static_cast<GDALDataset *>(GDALOpen(physicalDem.toUtf8().constData(),GA_ReadOnly));
            QVERIFY(raster);double bed=0;
            const auto sampled=raster->GetRasterBand(1)->RasterIO(GF_Read,20,19,1,1,&bed,1,1,GDT_Float64,0,0);
            GDALClose(raster);QCOMPARE(sampled,CE_None);QVERIFY(bed<9);
        }
        const QString stage=generated.generatedArtifacts?generated.generatedArtifacts->directoryPath():QString();
        std::weak_ptr<GeneratedMeshArtifacts> observer=generated.generatedArtifacts;
        Result queuedCopy=generated;generated=Result{};
        if(!laterFailure) {QVERIFY(!observer.expired());QVERIFY(QFileInfo(stage).isDir());}
        queuedCopy=Result{};QVERIFY(observer.expired());
        QVERIFY(stage.isEmpty() || !QFileInfo::exists(stage));
        QVERIFY(dir.entryList({".openswmm-generation-*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty());
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
        QFETCH(bool,existing);QFETCH(bool,afterComputation);
        const QDir dir(qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT")+
                       "/burn_cancellation/"+QString::fromLatin1(QTest::currentDataTag()));
        Inputs inputs;QVERIFY(prepareBurnFixture(dir,inputs,existing));
        const auto savedFiles=QDir(inputs.burnOutputDir).entryList(QDir::Files);
        inputs.burnEnabled=false;auto generated=run(inputs);QVERIFY2(generated.ok,qPrintable(generated.errorMsg));
        inputs.burnEnabled=true;inputs.exportRaster=true;
        std::weak_ptr<GeneratedMeshArtifacts> observer;QString stage;
        {
            auto result=burn(std::move(generated),inputs,[&]{return !afterComputation;});
            QCOMPARE(result.ok,afterComputation);
            if(afterComputation) {
                QVERIFY(result.generatedArtifacts);observer=result.generatedArtifacts;
                stage=result.generatedArtifacts->directoryPath();QVERIFY(QFileInfo(stage).isDir());
            } else QVERIFY2(result.errorMsg.contains("Cancelled",Qt::CaseInsensitive),qPrintable(result.errorMsg));
            // Discarding an asynchronously completed result owns no live layer.
        }
        QVERIFY(observer.expired());QVERIFY(stage.isEmpty() || !QFileInfo::exists(stage));
        QCOMPARE(QDir(inputs.burnOutputDir).entryList(QDir::Files),savedFiles);
        QVERIFY(dir.entryList({".openswmm-generation-*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty());
    }

    void cancelledRasterExportDropsStage()
    {
        const QDir dir(qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT")+"/raster_export_cancel");
        Inputs inputs;QVERIFY(prepareBurnFixture(dir,inputs,false));
        inputs.burnEnabled=false;const auto generated=run(inputs);
        QVERIFY2(generated.ok,qPrintable(generated.errorMsg));
        mesh::ChannelMeshBurnInputs burnInputs;
        burnInputs.profiles=inputs.burnProfiles;burnInputs.options=inputs.burnOptions;
        const auto burned=mesh::burnChannelsIntoMesh(generated.meshResult,burnInputs);
        QVERIFY2(burned.ok,qPrintable(burned.error));
        mesh::ChannelBurnExportRequest request;
        request.projectPath=inputs.inpPath;request.sourcePath=inputs.dtmPath;
        request.outputDirectory=inputs.burnOutputDir;request.meshCRSWkt=inputs.meshCRSWkt;
        request.options=inputs.burnOptions;
        int cancellationChecks=0;
        const auto result=mesh::prepareChannelBurnExport(burned,request,[&]{return ++cancellationChecks>1;});
        QVERIFY(!result.ok);QVERIFY(!result.artifacts);QVERIFY(cancellationChecks>1);
        QVERIFY2(result.error.contains("Cancelled",Qt::CaseInsensitive),qPrintable(result.error));
        QVERIFY(!QFileInfo::exists(inputs.burnOutputDir));
        QVERIFY(dir.entryList({".openswmm-generation-*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty());
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
            QPromise<GenerationResult> promise;
            promise.start();
            auto future = promise.future();
            dialog.m_watcher = new QFutureWatcher<GenerationResult>(&dialog);
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

    void terrainToleranceControlsSizing_data()
    {
        QTest::addColumn<bool>("toleranceOn");
        QTest::newRow("tolerance-off") << false;
        QTest::newRow("tolerance-on") << true;
    }

    /*! The terrain-error size term (MESH_OVERHAUL_PLAN_2026-09-29.md Stage
     *  2): with a tolerance set, a rippled DEM refines the mesh; with the
     *  tolerance off the same DEM leaves the cell count alone. The cached
     *  boundary/terrain stages must reproduce the refined mesh exactly. */
    void terrainToleranceControlsSizing()
    {
        QFETCH(bool, toleranceOn);
        const QString root = qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT",
            QDir::current().filePath("terrain_pipeline_output"));
        const QDir dir(root + "/" + QString::fromLatin1(QTest::currentDataTag()));
        QVERIFY(QDir().mkpath(dir.path()));
        GDALAllRegister();
        auto *driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        QVERIFY(driver);
        const QString raster = dir.filePath("ripple.tif");
        auto *dataset = driver->Create(raster.toUtf8().constData(), 40, 40, 1, GDT_Float32, nullptr);
        QVERIFY(dataset);
        double transform[] = {-4, 1, 0, 36, 0, -1};
        QCOMPARE(dataset->SetGeoTransform(transform), CE_None);
        std::vector<float> values(1600, 0.0f);
        for (int r = 0; r < 40; ++r)
            for (int c = 0; c < 40; ++c)
                values[r * 40 + c] = 2.0f * std::sin(0.8 * c) * std::sin(0.8 * r);   // ~4 px ripples
        const auto writeResult = dataset->GetRasterBand(1)->RasterIO(
            GF_Write, 0, 0, 40, 40, values.data(), 40, 40, GDT_Float32, 0, 0);
        GDALClose(dataset);
        QCOMPARE(writeResult, CE_None);

        Inputs inputs;
        inputs.inpPath = dir.filePath("model.inp");
        QFile model(inputs.inpPath);
        QVERIFY(model.open(QIODevice::WriteOnly));
        const QByteArray deck("[TITLE]\nTerrain tolerance fixture\n[OPTIONS]\nFLOW_UNITS CMS\n");
        QCOMPARE(model.write(deck), deck.size());
        model.close();
        inputs.modelExtent = MapExtent(0, 0, 32, 32);
        inputs.domains = {QPolygonF(QVector<QPointF>{{0,0}, {32,0}, {32,32}, {0,32}})};
        inputs.auxPoints = {{{16, 16}, 0.0, true}}; // valid flat elevation fallback without a DEM
        inputs.meshOutputPath = dir.filePath("coarse.2dm");
        inputs.meshLinearUnitName = "metre";
        inputs.cellSize = 8.0;
        inputs.coarsenFactor = 1.0;
        inputs.sizeRatio = 1.5;
        inputs.minCellSize = 1.0;
        inputs.terrainTolerance = toleranceOn ? 0.2 : 0.0;
        inputs.genOpts.maxArea = 0.4330127018922193 * 64.0;
        inputs.genOpts.minCellSize = 1.0;
        inputs.mapNodesAfterGen = false;
        const auto coarse = run(inputs);
        QVERIFY2(coarse.ok, qPrintable(coarse.errorMsg));
        inputs.dtmPath = raster;
        inputs.meshOutputPath = dir.filePath("terrain.2dm");
        const auto terrain = run(inputs);
        QVERIFY2(terrain.ok, qPrintable(terrain.errorMsg));
        if (toleranceOn)
            QVERIFY2(terrain.meshResult.triangles.size() > 2 * coarse.meshResult.triangles.size(),
                qPrintable(QString("Terrain tolerance did not refine: coarse=%1 terrain=%2")
                    .arg(coarse.meshResult.triangles.size()).arg(terrain.meshResult.triangles.size())));
        else
            QCOMPARE(terrain.meshResult.triangles.size(), coarse.meshResult.triangles.size());

        // Same project/raster/options exercises the stage-cache hit.
        inputs.meshOutputPath = dir.filePath("cached.2dm");
        const auto cached = run(inputs);
        QVERIFY2(cached.ok, qPrintable(cached.errorMsg));
        QCOMPARE(cached.meshResult.triangles.size(), terrain.meshResult.triangles.size());
        QCOMPARE(cached.meshResult.quadCount(), terrain.meshResult.quadCount());
        QCOMPARE(cached.meshResult.vertices.size(), terrain.meshResult.vertices.size());
        for (qsizetype i = 0; i < terrain.meshResult.vertices.size(); ++i)
            QCOMPARE(cached.meshResult.vertices[i].xy, terrain.meshResult.vertices[i].xy);
    }

    /*! Phase 6b + triangle engine (MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md
     *  D12.3): a synthetic GeoTIFF street (0.2 m curbs, 20 m carriageway)
     *  through the real worker. With a terrain tolerance the curbs come back
     *  as mesh edges (vertices exactly on the pixel-centre rows the
     *  extractor traces); with street quads the carriageway between them is
     *  a strip of aligned quads; without a tolerance no line is laid. */
    void streetCurbsBecomeMeshEdges()
    {
        const QString root = qEnvironmentVariable("SWMMVIS_TERRAIN_PIPELINE_OUTPUT",
            QDir::current().filePath("terrain_pipeline_output"));
        const QDir dir(root + "/street_curbs");
        QVERIFY(QDir().mkpath(dir.path()));
        GDALAllRegister();
        auto *driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        QVERIFY(driver);
        const QString raster = dir.filePath("street.tif");
        const int cols = 160, rows = 120;   // 80 m × 60 m at 0.5 m
        auto *dataset = driver->Create(raster.toUtf8().constData(), cols, rows, 1, GDT_Float32, nullptr);
        QVERIFY(dataset);
        // North-up, origin (0, 60.3): the 0.3 m shift keeps the curbs off
        // any round coordinate, so only a traced line can put vertices
        // exactly on them.
        double transform[] = {0.0, 0.5, 0.0, 60.3, 0.0, -0.5};
        QCOMPARE(dataset->SetGeoTransform(transform), CE_None);
        std::vector<float> values(size_t(cols) * rows);
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
                values[size_t(r) * cols + c] = (r >= 40 && r < 80) ? 0.0f : 0.2f;   // road rows 40..79
        const auto writeResult = dataset->GetRasterBand(1)->RasterIO(
            GF_Write, 0, 0, cols, rows, values.data(), cols, rows, GDT_Float32, 0, 0);
        GDALClose(dataset);
        QCOMPARE(writeResult, CE_None);

        Inputs inputs;
        inputs.inpPath = dir.filePath("model.inp");
        QFile model(inputs.inpPath);
        QVERIFY(model.open(QIODevice::WriteOnly));
        const QByteArray deck("[TITLE]\nStreet curb fixture\n[OPTIONS]\nFLOW_UNITS CMS\n");
        QCOMPARE(model.write(deck), deck.size());
        model.close();
        inputs.modelExtent = MapExtent(0, 0.3, 80, 60.3);
        inputs.domains = {QPolygonF(QVector<QPointF>{{1, 1}, {79, 1}, {79, 59}, {1, 59}})};
        inputs.auxPoints = {{{40, 30}, 0.0, true}};
        inputs.meshLinearUnitName = "metre";
        inputs.mapNodesAfterGen = false;
        inputs.cellSize = 4.0;
        inputs.coarsenFactor = 4.0;
        inputs.sizeRatio = 1.5;
        inputs.minCellSize = 0.5;
        inputs.genOpts.maxArea = 0.4330127018922193 * 16.0;
        inputs.genOpts.minCellSize = 0.5;
        inputs.genOpts.quadsBetweenBreaklines = false;
        inputs.dtmPath = raster;

        // The extractor places each line on its step: the boundary between
        // rows 39|40 and 79|80, i.e. y = 60.3 − 0.5·row.
        const double curbA = 60.3 - 0.5 * 40.0, curbB = 60.3 - 0.5 * 80.0;
        auto onCurb = [&](const QPointF &p) {
            return std::abs(p.y() - curbA) < 1e-9 || std::abs(p.y() - curbB) < 1e-9;
        };

        inputs.terrainTolerance = 0.1;
        inputs.meshOutputPath = dir.filePath("with_lines.2dm");
        const auto withLines = run(inputs);
        QVERIFY2(withLines.ok, qPrintable(withLines.errorMsg));
        const auto &m = withLines.meshResult;
        int onA = 0, onB = 0;
        for (const auto &v : m.vertices)
        {
            if (std::abs(v.xy.y() - curbA) < 1e-9) ++onA;
            if (std::abs(v.xy.y() - curbB) < 1e-9) ++onB;
        }
        // Curbs captured as edges need no fine cells along them (the size
        // field ignores their step), so a 78 m curb holds a handful.
        QVERIFY2(onA >= 4 && onB >= 4, qPrintable(QStringLiteral("curb vertices %1 / %2").arg(onA).arg(onB)));
        QCOMPARE(m.quadCount(), 0);
        double area = 0.0;
        for (const auto &c : m.triangles) area += mesh::cellGeom(m.vertices, c).area;
        QVERIFY2(std::abs(area - 78.0 * 58.0) < 1e-6, qPrintable(QString::number(area, 'g', 17)));

        // Street quads on: the carriageway (lower than both sides) becomes
        // rows of quads along the street, its sides on the curbs. (The
        // fixture's elevation point sits in the road; a node inside a strip
        // would keep it out, so this run goes without it.)
        inputs.genOpts.quadsBetweenBreaklines = true;
        const auto auxPoints = inputs.auxPoints;
        inputs.auxPoints.clear();
        inputs.meshOutputPath = dir.filePath("street_quads.2dm");
        const auto street = run(inputs);
        QVERIFY2(street.ok, qPrintable(street.errorMsg));
        const auto &ms = street.meshResult;
        int streetQuads = 0;
        for (const auto &c : ms.triangles)
        {
            if (!c.isQuad()) continue;
            ++streetQuads;
            const QPointF ctr = mesh::cellGeom(ms.vertices, c).centroid;
            QVERIFY2(ctr.y() > curbB && ctr.y() < curbA, qPrintable(QStringLiteral("quad at %1,%2").arg(ctr.x()).arg(ctr.y())));
        }
        QVERIFY(streetQuads >= 4);
        area = 0.0;
        for (const auto &c : ms.triangles) area += mesh::cellGeom(ms.vertices, c).area;
        QVERIFY2(std::abs(area - 78.0 * 58.0) < 1e-6, qPrintable(QString::number(area, 'g', 17)));
        inputs.auxPoints = auxPoints;
        inputs.genOpts.quadsBetweenBreaklines = false;

        inputs.terrainTolerance = 0.0;
        inputs.meshOutputPath = dir.filePath("without_lines.2dm");
        const auto withoutLines = run(inputs);
        QVERIFY2(withoutLines.ok, qPrintable(withoutLines.errorMsg));
        int onLine = 0;
        for (const auto &v : withoutLines.meshResult.vertices) if (onCurb(v.xy)) ++onLine;
        QVERIFY2(onLine < 4, qPrintable(QStringLiteral("%1 vertices on the curb rows without a tolerance").arg(onLine)));
    }
};

QTEST_MAIN(TestMeshTerrainPipeline)
#include "test_meshterrainpipeline.moc"
