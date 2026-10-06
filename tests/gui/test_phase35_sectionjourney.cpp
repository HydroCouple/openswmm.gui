#include "layers/swmm2dresultslayer.h"
#include "io/mesh2dvariablegisexport.h"
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include "map/mapcanvas.h"
#include "map/spatialreferencesystem.h"
#include "plot/meshprofileplotwidget.h"
#include "plot/meshprofiletrackswidget.h"
#include "plot/profilesectionseries.h"
#include "project/openswmmvisworkspace.h"
#include "project/profilesectionstore.h"
#include "project/projectserializer.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/meshprofileplotdialog.h"
#include <openswmm/engine/openswmm_engine.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QTableView>
#include <QtTest>
#include <cmath>
#include <memory>

using namespace ProfileSection;
using openswmmvis::io::Mesh2DResultVariable;
using openswmmvis::io::Mesh2DValueStatus;
namespace {
QString reviewDirectory()
{
    return QDir::cleanPath(qEnvironmentVariable("SWMMVIS_SECTION_JOURNEY_OUTPUT",
        QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().absoluteFilePath(
            "../../workplans/artifacts/phase_35_profiles_forcing/review/section-journey")));
}
QByteArray contents(const QString &path)
{
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool writeFile(const QString &path,const QByteArray &bytes)
{
    QFile file(path); return file.open(QIODevice::WriteOnly|QIODevice::Truncate) && file.write(bytes)==bytes.size();
}
// A short version of the engine's gw_transport_kernel/h5_species case, with
// a declared metre CRS, flat terrain at 10 m and an initially wet surface.
const char *deck=R"INP([TITLE]
Phase 35 combined section review: surface, groundwater and saturated TSS

[OPTIONS]
FLOW_UNITS CMS
FLOW_ROUTING DYNWAVE
CRS EPSG:32618
START_DATE 01/01/2026
START_TIME 00:00:00
END_DATE 01/01/2026
END_TIME 00:02:00
REPORT_STEP 00:01:00
WET_STEP 00:01:00
DRY_STEP 00:01:00
ROUTING_STEP 5
ALLOW_PONDING NO

[POLLUTANTS]
TSS MG/L 10.0 0.0 0.0 0.0 NO * 0.0 0.0

[JUNCTIONS]
J1 8.0 4.0 0 0 0

[OUTFALLS]
O1 7.5 FREE NO

[CONDUITS]
C1 J1 O1 30.0 0.013 0 0 0

[XSECTIONS]
C1 CIRCULAR 0.5 0 0 0 1

[RAINGAGES]
RG1 INTENSITY 1:00 1.0 TIMESERIES RAIN

[TIMESERIES]
RAIN 0:00 20.0
RAIN 0:10 0.0

[2D_OPTIONS]
INTEGRATOR EXPLICIT
LTS_TIERS 1
MAX_TIMESTEP 5
DRY_DEPTH 0.001
COUPLING_CD 0.7
REPORT_2D YES
OUTPUT_FILE section-journey.h5
OUTPUT_PRECISION FLOAT64
REPORT_2D_VARIABLES DEPTH GROUNDWATER
RAINFALL_MODE SYSTEM
EVAPORATION CLIMATE

[2D_VERTICES]
500000.0 4500000.0 10.0
500010.0 4500000.0 10.0
500010.0 4500010.0 10.0
500000.0 4500010.0 10.0

[2D_TRIANGLES]
0 1 2 0.03 0.2
0 2 3 0.03 0.2

[2D_INFILTRATION_DEFAULTS]
* CONSTANT 40.0 - - - -

[2D_AQUIFER_OPTIONS]
CLOSURE CLOSED_FORM
NODE_ENROLMENT ROWS

[2D_AQUIFER]
* 36.0 1.0 0.45 0.10 2.0 HG0 0.5
CELL 1 36.0 1.0 0.45 0.10 2.0 HG0 0.95

[GW_INITIAL_QUALITY]
* SAT TSS 5.0
* UNSAT TSS 2.0
CELL 1 SAT TSS 25.0

[GW_SOURCES]
W * FLOW 0.0001 TSS CONC 2

[COORDINATES]
J1 500005.0 4500005.0
O1 500040.0 4500040.0

[REPORT]
INPUT NO
)INP";
struct Engine {
    SWMM_Engine handle=swmm_engine_create(); bool started=false;
    ~Engine(){if(handle){if(started)swmm_engine_end(handle);swmm_engine_close(handle);swmm_engine_destroy(handle);}}
    QString error()const{return QString::fromUtf8(swmm_get_last_error_msg(handle));}
};
}

class TestPhase35SectionJourney:public QObject {
    Q_OBJECT
    QString root_,inp_,h5_;
private slots:
    void initTestCase()
    {
        root_=reviewDirectory(); QVERIFY(QDir().mkpath(root_));
        inp_=root_+"/section-journey.inp"; h5_=root_+"/section-journey.h5";
        QVERIFY(writeFile(inp_,deck));
        Engine engine; QVERIFY(engine.handle);
        QCOMPARE(swmm_engine_open(engine.handle,inp_.toUtf8().constData(),
            (root_+"/section-journey.rpt").toUtf8().constData(),
            (root_+"/section-journey.out").toUtf8().constData(),nullptr),SWMM_OK);
        const int initialized=swmm_engine_initialize(engine.handle);
        QVERIFY2(initialized==SWMM_OK,qPrintable(engine.error()));
        QCOMPARE(swmm_engine_start(engine.handle,1),SWMM_OK); engine.started=true;
        double elapsed=0;int steps=0,status=SWMM_OK;
        do {status=swmm_engine_step(engine.handle,&elapsed);++steps;}
        while(status==SWMM_OK && elapsed>0 && steps<10000);
        QVERIFY2(status==SWMM_OK,qPrintable(engine.error()));
        QVERIFY2(elapsed==0,"The short review simulation did not finish within the bounded step count.");
        QCOMPARE(swmm_engine_end(engine.handle),SWMM_OK);engine.started=false;
        QCOMPARE(swmm_engine_close(engine.handle),SWMM_OK);
        QVERIFY(QFileInfo(h5_).size()>0);
    }
    void realResultsSaveReopenAndExport()
    {
        std::unique_ptr<OpenSWMMVisWorkspace> workspace(OpenSWMMVisWorkspace::newInstance({},nullptr));
        QVERIFY(workspace);
        SWMMVisProjectWindow window(workspace.get(),inp_);
        QList<QString> warnings,errors;
        QVERIFY2(window.loadModel(warnings,errors),qPrintable(errors.join("; ")));
        QVERIFY(window.canvas());
        QVERIFY(window.canvas()->setCanvasSRSByCode("EPSG",32618));
        auto *layer=new SWMM2DResultsLayer("Combined section review",workspace.get());
        layer->setSRS(new SpatialReferenceSystem("EPSG",32618),true);
        auto source=std::make_unique<HDF5Mesh2DSource>(); QVERIFY(source->open(h5_));
        QVERIFY(source->timeCount()>=2); QCOMPARE(source->triangleCount(),2);
        layer->setSource(std::move(source));
        layer->setProperty("snoopy_h5_path",h5_);
        window.canvas()->addLayer(layer,false);
        const int frame=layer->source()->timeCount()-1;
        layer->setCurrentTimeIndex(frame);
        const QDateTime effective=layer->source()->simTimeAt(frame); QVERIFY(effective.isValid());
        const QVector<QPointF> path={{500001,-4500005},{500009,-4500005}};
        MeshProfilePlotDialog dialog(nullptr,layer,nullptr,path,&window,&window);
        auto definition=dialog.definition(); definition.title="Surface, groundwater and saturated TSS";
        definition.verticalDatum="Local review datum";
        for(auto &ref:definition.sources)ref.verticalDatum=definition.verticalDatum;
        const QStringList datasets={"Mesh2_face_gw_table_elev","Mesh2_face_gw_bed_elev","Mesh2_face_gw_sat_conc"};
        const QStringList labels={"Groundwater table","Aquifer base","Saturated TSS"};
        const QList<QColor> colors={QColor(Qt::magenta),QColor(Qt::darkGreen),QColor(Qt::darkRed)};
        for(int i=0;i<datasets.size();++i){
            Mesh2DResultVariable found;int matches=0;
            for(const auto &variable:layer->resultVariables())
                if(variable.dataset==datasets[i]&&(i!=2||variable.species=="TSS")){found=variable;++matches;}
            QCOMPARE(matches,1);
            QVERIFY2(found.unitsKnown,qPrintable(QString("Missing declared native units for %1 / %2 (dataset units: %3)")
                .arg(found.dataset,found.species,found.units)));
            SeriesDefinition series;series.id="review-series-"+QString::number(i);
            series.sourceId=definition.primarySourceId;series.variableKey=found.key();series.label=labels[i];
            series.role=i<2?SeriesRole::Elevation:SeriesRole::Scalar;
            series.pen=QPen(colors[i],2.5,i==1?Qt::DashLine:Qt::SolidLine);
            series.opacity=i==1?0.75:1.0;definition.series.append(series);
        }
        QString error; QVERIFY2(dialog.setDefinition(definition,&error),qPrintable(error));
        const auto &section=dialog.section();
        QVERIFY(section.hasResults);QVERIFY(section.exactWaterGeometry);QVERIFY(section.samples.size()>=4);
        QCOMPARE(section.effectiveTime,effective);QCOMPARE(section.series.size(),3);
        for(const auto &sample:section.samples){
            QVERIFY(std::abs(sample.ground-10)<1e-6);
            QVERIFY(sample.cellHasSurface);QVERIFY(sample.signedDepthNow>0);
        }
        const auto builtins=builtInSeries(section,dialog.definition());QCOMPARE(builtins.size(),2);
        for(const auto &point:builtins[1].points){QCOMPARE(point.status,Mesh2DValueStatus::Valid);QVERIFY(point.value>10);}
        for(int i=0;i<section.series.size();++i){
            const auto &series=section.series[i];QVERIFY2(series.error.isEmpty(),qPrintable(series.error));
            QVERIFY(series.unitsKnown);QCOMPARE(series.units,i==2?QString("MG/L"):QString("m"));
            if(i!=1)QCOMPARE(series.effectiveTime,effective); // base is static
            std::vector<float> values;std::vector<Mesh2DValueStatus> status;
            QVERIFY(layer->source()->readFaceVariableAt(series.descriptor,i==1?0:frame,values,status));
            QVERIFY(!series.points.isEmpty());
            for(const auto &point:series.points){
                QVERIFY(point.cellId>=0&&point.cellId<int(values.size()));
                QCOMPARE(point.status,Mesh2DValueStatus::Valid);
                QVERIFY(std::abs(point.value-values[size_t(point.cellId)])<1e-6);
                if(i==1)QVERIFY(std::abs(point.value-9)<1e-6);
            }
        }
        // Actual engine-produced species values must survive both GIS routes in
        // native units. The source can be released after immutable acquisition.
        using namespace openswmmvis::io;
        auto gis=captureMesh2DVariableGis(*layer->source(),section.series[2].descriptor.key(),
            frame,1.0,layer->srs()->toWkt(),&error);
        QVERIFY2(gis,qPrintable(error));QCOMPARE(gis->scalar.descriptor.units,QString("MG/L"));
        QCOMPARE(gis->cells.size(),size_t(2));
        GDALAllRegister();
        for(bool raster:{false,true}){
            Mesh2DVariableGisOptions options;options.format=raster?Mesh2DVariableGisFormat::GeoTiff:Mesh2DVariableGisFormat::GeoPackage;
            options.path=root_+(raster?"/groundwater-tss.tif":"/groundwater-tss.gpkg");options.pixelSize=1;
            // GDAL inspection can leave an optional PAM duplicate beside this
            // exact test-owned output. Remove it before repeating the journey.
            QFile::remove(options.path+".aux.xml");
            QVERIFY2(ProjectSaveOutputs::captureDestination(options.path,&options.destination,&error),qPrintable(error));
            QVERIFY2(exportMesh2DVariableGis(*gis,options,{},&error),qPrintable(error));
            auto *dataset=static_cast<GDALDataset*>(GDALOpenEx(options.path.toUtf8().constData(),
                raster?GDAL_OF_RASTER|GDAL_OF_READONLY:GDAL_OF_VECTOR|GDAL_OF_READONLY,nullptr,nullptr,nullptr));
            QVERIFY(dataset);
            if(raster){
                QCOMPARE(dataset->GetRasterCount(),2);
                QCOMPARE(QString::fromUtf8(dataset->GetRasterBand(1)->GetUnitType()),QString("MG/L"));
                const int width=dataset->GetRasterXSize(),height=dataset->GetRasterYSize();
                std::vector<double> values(size_t(width)*height),status(values.size());
                QCOMPARE(dataset->GetRasterBand(1)->RasterIO(GF_Read,0,0,width,height,values.data(),width,height,GDT_Float64,0,0),CE_None);
                QCOMPARE(dataset->GetRasterBand(2)->RasterIO(GF_Read,0,0,width,height,status.data(),width,height,GDT_Float64,0,0),CE_None);
                int valid=0;for(size_t i=0;i<values.size();++i)if(status[i]==0){
                    ++valid;QVERIFY(std::abs(values[i]-gis->scalar.values[0])<1e-6||std::abs(values[i]-gis->scalar.values[1])<1e-6);
                }QVERIFY(valid>0);
            }else{
                auto*features=dataset->GetLayerByName("scalar_cells");QVERIFY(features);QCOMPARE(features->GetFeatureCount(),GIntBig(2));
                int rows=0;while(auto*feature=features->GetNextFeature()){
                    const int cell=feature->GetFieldAsInteger("cell_index");QVERIFY(cell>=0&&cell<2);
                    QCOMPARE(QString::fromUtf8(feature->GetFieldAsString("units")),QString("MG/L"));
                    QVERIFY(std::abs(feature->GetFieldAsDouble("value")-gis->scalar.values[size_t(cell)])<1e-6);
                    ++rows;OGRFeature::DestroyFeature(feature);
                }QCOMPARE(rows,2);
            }
            GDALClose(dataset);
        }
        auto *store=ProfileSectionStore::forOwner(&window);bool saved=false;
        connect(&dialog,&MeshProfilePlotDialog::saveDefinitionRequested,store,[&](const Definition &d){saved=store->saveDefinition(d,&error);});
        window.setHasChanges(false);
        auto *save=dialog.findChild<QPushButton *>("sectionSave");QVERIFY(save);save->click();
        QVERIFY2(saved,qPrintable(error));QVERIFY(window.hasChanges());QCOMPARE(store->definitions().size(),1);
        const auto json=definitionToJson(store->definitions()[0],root_);
        QVERIFY(writeFile(root_+"/section-definition.json",QJsonDocument(json).toJson()));
        Definition decoded;QVERIFY2(definitionFromJson(json,root_,decoded,&error),qPrintable(error));
        QCOMPARE(decoded.series[1].pen,definition.series[1].pen);QCOMPARE(decoded.sources[0].path,h5_);
        MeshProfilePlotDialog reopened(nullptr,layer,nullptr,decoded.scenePolyline,&window,&window);
        QVERIFY2(reopened.setDefinition(decoded,&error),qPrintable(error));
        QCOMPARE(reopened.section().series.size(),3);
        QCOMPARE(reopened.section().series[2].points.size(),section.series[2].points.size());
        for(int i=0;i<section.series[2].points.size();++i)
            QCOMPARE(reopened.section().series[2].points[i].value,section.series[2].points[i].value);
        QVERIFY2(exportSectionCsv(reopened.section(),reopened.definition(),root_+"/combined-section.csv",&error),qPrintable(error));
        const auto csv=contents(root_+"/combined-section.csv");
        QVERIFY(csv.contains("Mesh2_face_gw_table_elev"));QVERIFY(csv.contains("Mesh2_face_gw_bed_elev"));
        QVERIFY(csv.contains("Mesh2_face_gw_sat_conc"));QVERIFY(csv.contains("MG/L"));
        QVERIFY(csv.contains(effective.toString(Qt::ISODateWithMs).toUtf8()));
        const QString project=root_+"/section-journey.oswp";
        QVERIFY2(ProjectSerializer::saveToFile(project,&window,&error),qPrintable(error));
        const auto session=QJsonDocument::fromJson(contents(project)).object()["sessions"].toArray()[0].toObject();
        QCOMPARE(session["profileSections"].toArray().size(),1);QCOMPARE(session["results2DLayers"].toArray().size(),1);
        reopened.resize(1100,850);reopened.show();QCoreApplication::processEvents();
        auto *plot=reopened.findChild<MeshProfilePlotWidget *>();QVERIFY(plot);
        auto *tracks=reopened.findChild<MeshProfileTracksWidget *>();QVERIFY(tracks);QCOMPARE(tracks->trackCount(),1);
        auto *figure=plot->parentWidget();QVERIFY(figure);QVERIFY(figure==tracks->parentWidget());
        QImage image(figure->size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);figure->render(&image);
        int magenta=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto color=image.pixelColor(x,y);if(color.red()>180&&color.blue()>180&&color.green()<80)++magenta;}
        QVERIFY2(magenta>20,"The real groundwater table line must appear in the rendered elevation figure.");
        QVERIFY(image.save(root_+"/combined-section.png"));
        auto *table=reopened.findChild<QTableView *>("sectionSamples");QVERIFY(table);
        QVERIFY(table->model()->rowCount()>section.samples.size()*2);
        plot->setFocus();QTest::keyClick(plot,Qt::Key_End);QVERIFY(plot->hasCursor());
        QVERIFY(std::abs(plot->cursorChainage()-8)<1e-6);
        // Losing the actual source must remove its terrain and values. The
        // dialog has no authored mesh to silently substitute for these results.
        layer->setSource(nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(reopened.section().samples.isEmpty(),2000);
        QVERIFY(!reopened.section().hasResults);
        for(const auto &series:reopened.section().series)QVERIFY(!series.error.isEmpty());
    }
    // The section dialog on a layer whose surface is fitted by the worker
    // (as the application configures it) must show the same current depths
    // and the same maximum as the dialog on a synchronous layer.
    void workerLayerFeedsTheSectionDialog()
    {
        std::unique_ptr<OpenSWMMVisWorkspace> workspace(OpenSWMMVisWorkspace::newInstance({},nullptr));
        QVERIFY(workspace);
        SWMMVisProjectWindow window(workspace.get(),inp_);
        QList<QString> warnings,errors;
        QVERIFY2(window.loadModel(warnings,errors),qPrintable(errors.join("; ")));
        QVERIFY(window.canvas());
        QVERIFY(window.canvas()->setCanvasSRSByCode("EPSG",32618));
        SWMM2DResultsLayer *layers[2]={nullptr,nullptr};
        for(int i=0;i<2;++i) {
            auto *layer=new SWMM2DResultsLayer(i ? "Section worker" : "Section synchronous",workspace.get());
            layer->setSRS(new SpatialReferenceSystem("EPSG",32618),true);
            layer->setAsyncSurface(i==1);
            auto source=std::make_unique<HDF5Mesh2DSource>(); QVERIFY(source->open(h5_));
            layer->setSource(std::move(source));
            window.canvas()->addLayer(layer,false);
            layers[i]=layer;
        }
        auto *sync=layers[0],*async=layers[1];
        const int last=sync->source()->timeCount()-1; QVERIFY(last>=1);
        const QVector<QPointF> path={{500001,-4500005},{500009,-4500005}};
        MeshProfilePlotDialog reference(nullptr,sync,nullptr,path,&window,&window);
        MeshProfilePlotDialog dialog(nullptr,async,nullptr,path,&window,&window);
        auto same=[](double a,double b) { return (std::isnan(a) && std::isnan(b)) || a==b; };
        for(int frame:{0,last,0}) {
            sync->setCurrentTimeIndex(frame);async->setCurrentTimeIndex(frame);
            QTRY_VERIFY_WITH_TIMEOUT(!async->surfaceBusy() && async->currentTimeIndex()==frame
                                     && async->envelopeComplete(),10000);
            const auto &actual=dialog.section(),&expected=reference.section();
            QVERIFY(expected.samples.size()>=4);
            QCOMPARE(actual.samples.size(),expected.samples.size());
            bool wet=false;
            for(int i=0;i<actual.samples.size();++i) {
                const auto &a=actual.samples[i],&e=expected.samples[i];
                QVERIFY2(same(a.signedDepthNow,e.signedDepthNow) && same(a.signedMaxDepth,e.signedMaxDepth)
                         && a.depthNow==e.depthNow && a.maxDepth==e.maxDepth && a.cellHasSurface==e.cellHasSurface,
                         qPrintable(QString("frame %1 sample %2: now %3 / %4, max %5 / %6").arg(frame).arg(i)
                             .arg(a.signedDepthNow).arg(e.signedDepthNow).arg(a.signedMaxDepth).arg(e.signedMaxDepth)));
                wet=wet || e.signedMaxDepth>0;
            }
            QVERIFY(wet);   // the maximum really reached the worker dialog
        }
    }
};
QTEST_MAIN(TestPhase35SectionJourney)
#include "test_phase35_sectionjourney.moc"
