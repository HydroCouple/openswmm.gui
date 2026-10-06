// Phase35/C8: actual selection -> common assignment UI -> saved engine deck ->
// reopened simulation. This test does not substitute a sampled helper for the UI.
#include <QtTest>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QQmlEngine>
#include <QTableWidget>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_gw2d.h>
#include "assignment/groundwatertransportassignment.h"
#include "core/unitsystem.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/spatialreferencesystem.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "map/tools/maptoolpick2dcells.h"
#include "mesh/meshobjectref.h"
#include "selection/selectionmanager.h"
#include "ui/dialogs/groundwaterassigndialog.h"
#include <cmath>
#include <algorithm>
#include <memory>
using namespace openswmmvis::assignment;
using namespace openswmmvis::ui;
namespace {
const QByteArray deck=R"INP([OPTIONS]
FLOW_UNITS CMS
FLOW_ROUTING DYNWAVE
START_DATE 01/01/2026
START_TIME 00:00:00
END_DATE 01/01/2026
END_TIME 00:05:00
REPORT_STEP 00:01:00
WET_STEP 00:01:00
DRY_STEP 00:01:00
ROUTING_STEP 5
ALLOW_PONDING NO
[POLLUTANTS]
TSS MG/L 0 0 0 0 NO * 0 0
[JUNCTIONS]
J1 -2 4 0 0 0
[OUTFALLS]
O1 -2.5 FREE NO
[CONDUITS]
C1 J1 O1 30 0.013 0 0 0
[XSECTIONS]
C1 CIRCULAR 0.5 0 0 0 1
[2D_OPTIONS]
INTEGRATOR EXPLICIT
LTS_TIERS 1
MAX_TIMESTEP 5
DRY_DEPTH 0.001
REPORT_2D NO
[2D_VERTICES]
0 0 0
2 0 0
2 2 0
0 2 0
4 0 0
4 2 0
[2D_TRIANGLES]
0 1 2 0.03 0
[2D_QUADS]
1 4 5 2 0.03 0
[2D_AQUIFER_OPTIONS]
CLOSURE CLOSED_FORM
NODE_ENROLMENT ROWS
[2D_AQUIFER]
* 36 1 0.45 0.10 2 HG0 0.5
[GW_TRANSPORT_OPTIONS]
TRANSPORT_POLLUTANTS YES
[COORDINATES]
J1 1 1
O1 40 40
[REPORT]
INPUT NO
)INP";
bool writeFile(const QString&path,const QByteArray&bytes){QFile f(path);return f.open(QIODevice::WriteOnly|QIODevice::Truncate)&&f.write(bytes)==bytes.size()&&f.flush();}
mesh::MeshResult geometry(){mesh::MeshResult m;for(const auto&p:{QPointF(0,0),QPointF(2,0),QPointF(2,2),QPointF(0,2),QPointF(4,0),QPointF(4,2)})m.vertices.append({p,0});m.triangles={{0,1,2},{1,4,5,2}};return m;}
struct Journey {
 SWMMModelLayer model{QString()};SWMM2DMeshLayer mesh{geometry(),QStringLiteral("forcing-journey.2dm")};MapCanvas canvas;SelectionManager selection;UnitSystem units;MapToolPick2DCells picker{&canvas,&selection};bool attached=false;
 bool open(const QString&path,QString*error){model.setModelFilePath(path);QList<QString>warnings,errors;if(!model.loadModel(warnings,errors)){*error=errors.join('\n');return false;}
  mesh.setSRS(new SpatialReferenceSystem(QStringLiteral("EPSG"),32618),true);canvas.applyCRSInternal(new SpatialReferenceSystem(QStringLiteral("EPSG"),32618),true);canvas.resize(700,400);canvas.addLayer(&mesh,false);attached=true;canvas.setExtent(MapExtent(-1,-1,5,3),false);picker.activate();return true;}
 ~Journey(){picker.deactivate();if(attached)canvas.takeLayer(canvas.layers().indexOf(&mesh),false);}
 void mouse(QEvent::Type type,double x,double y){int px=0,py=0;picker.toPixelCoords(x,y,px,py);QPointF p(px,py);QMouseEvent event(type,p,p,type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
  if(type==QEvent::MouseButtonPress)picker.mousePressEvent(&event);else if(type==QEvent::MouseMove)picker.mouseMoveEvent(&event);else if(type==QEvent::MouseButtonRelease)picker.mouseReleaseEvent(&event);else picker.mouseDoubleClickEvent(&event);}
 void selectRectangle(){picker.setMode(MapToolPick2DCells::Mode::Box);mouse(QEvent::MouseButtonPress,-.2,-.2);mouse(QEvent::MouseMove,4.2,2.2);mouse(QEvent::MouseButtonRelease,4.2,2.2);}
 void selectPolygon(){picker.setMode(MapToolPick2DCells::Mode::Lasso);mouse(QEvent::MouseButtonPress,-.2,-.2);mouse(QEvent::MouseButtonPress,4.2,-.2);mouse(QEvent::MouseButtonPress,4.2,2.2);mouse(QEvent::MouseButtonDblClick,-.2,2.2);}
 QSet<SWMMObjectRef> expected()const{return {mesh::MeshObjectRef::cell(mesh.sourcePath(),0),mesh::MeshObjectRef::cell(mesh.sourcePath(),1)};}
};
struct Engine {SWMM_Engine value=swmm_engine_create();~Engine(){if(value){swmm_engine_close(value);swmm_engine_destroy(value);}}};
struct Ledgers {double waterIn=0,waterOut=0,speciesIn=0,speciesOut=0,waterResidual=0,speciesResidual=0,waterStorage=0,speciesStorage=0;int steps=0;};
bool simulate(const QString&path,const QString&prefix,Ledgers*out,QString*error){Engine e;if(!e.value){*error="Cannot create engine";return false;}
 auto check=[&](int code,const char*operation){if(code!=SWMM_OK){*error=QString("%1 failed (%2)").arg(QString::fromLatin1(operation)).arg(code);return false;}return true;};
 if(!check(swmm_engine_open(e.value,path.toUtf8().constData(),(prefix+".rpt").toUtf8().constData(),(prefix+".out").toUtf8().constData(),nullptr),"open")||!check(swmm_engine_initialize(e.value),"initialize")||!check(swmm_engine_start(e.value,1),"start"))return false;
 int active=0;if(!check(swmm_gw2d_is_active(e.value,&active),"active")||active!=1){*error="Groundwater kernel is inactive";return false;}
 bool ended=false;for(int n=0;n<10000;++n){double elapsed=0;if(!check(swmm_engine_step(e.value,&elapsed),"step"))return false;++out->steps;if(elapsed==0){ended=true;break;}}
 if(!ended){*error="Simulation exceeded the checked step limit";return false;}if(!check(swmm_engine_end(e.value),"end"))return false;
 return check(swmm_gw2d_get_ledger(e.value,SWMM_GW2D_LED_SOURCE_IN,&out->waterIn),"water in")&&check(swmm_gw2d_get_ledger(e.value,SWMM_GW2D_LED_SOURCE_OUT,&out->waterOut),"water out")&&check(swmm_gw2d_get_species_ledger(e.value,0,SWMM_GW2D_SPL_SOURCE_IN,&out->speciesIn),"species in")&&check(swmm_gw2d_get_species_ledger(e.value,0,SWMM_GW2D_SPL_SOURCE_OUT,&out->speciesOut),"species out")&&check(swmm_gw2d_get_continuity_error(e.value,&out->waterResidual),"water residual")&&check(swmm_gw2d_get_species_ledger(e.value,0,SWMM_GW2D_SPL_RESIDUAL,&out->speciesResidual),"species residual")&&check(swmm_gw2d_get_ledger(e.value,SWMM_GW2D_LED_STORAGE,&out->waterStorage),"water storage")&&check(swmm_gw2d_get_species_ledger(e.value,0,SWMM_GW2D_SPL_STORAGE,&out->speciesStorage),"species storage");
}
QJsonObject json(const Ledgers&r){return {{"sourceWaterInM3",r.waterIn},{"sourceWaterOutM3",r.waterOut},{"sourceSpeciesInConcentrationTimesM3",r.speciesIn},{"sourceSpeciesOutConcentrationTimesM3",r.speciesOut},{"waterResidualM3",r.waterResidual},{"speciesResidualConcentrationTimesM3",r.speciesResidual},{"waterStorageM3",r.waterStorage},{"speciesStorageConcentrationTimesM3",r.speciesStorage},{"steps",r.steps}};}
}
class TestPhase35ForcingJourney:public QObject {
 Q_OBJECT
 QString output,vectorPath,rasterPath;
private slots:
 void initTestCase(){
  GDALAllRegister();qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM",1,0,"SWMMLayerQSGRenderer");qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM",1,0,"SWMM2DMeshQSGRenderer");qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM",1,0,"SWMM2DResultsQSGRenderer");
  output=qEnvironmentVariable("SWMMVIS_FORCING_JOURNEY_OUTPUT",QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().filePath("../../workplans/artifacts/phase_35_profiles_forcing/forcing_journey"));QVERIFY(QDir().mkpath(output));
  OGRSpatialReference crs;QCOMPARE(crs.importFromEPSG(32618),OGRERR_NONE);crs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
  vectorPath=output+"/constant_per_cell.geojson";QFile::remove(vectorPath);auto*vDriver=GetGDALDriverManager()->GetDriverByName("GeoJSON");QVERIFY(vDriver);auto*vector=vDriver->Create(vectorPath.toUtf8().constData(),0,0,0,GDT_Unknown,nullptr);QVERIFY(vector);auto*layer=vector->CreateLayer("coverage",&crs,wkbPolygon,nullptr);QVERIFY(layer);OGRFieldDefn field("flow",OFTReal);QCOMPARE(layer->CreateField(&field),OGRERR_NONE);
  auto*feature=OGRFeature::CreateFeature(layer->GetLayerDefn());QVERIFY(feature);feature->SetField("flow",.001);OGRLinearRing ring;ring.addPoint(-1,-1);ring.addPoint(5,-1);ring.addPoint(5,3);ring.addPoint(-1,3);ring.closeRings();OGRPolygon polygon;polygon.addRing(&ring);QCOMPARE(feature->SetGeometry(&polygon),OGRERR_NONE);QCOMPARE(layer->CreateFeature(feature),OGRERR_NONE);OGRFeature::DestroyFeature(feature);GDALClose(vector);
  rasterPath=output+"/constant_per_cell.tif";auto*rDriver=GetGDALDriverManager()->GetDriverByName("GTiff");QVERIFY(rDriver);auto*raster=rDriver->Create(rasterPath.toUtf8().constData(),8,6,1,GDT_Float64,nullptr);QVERIFY(raster);double geo[]={-2,1,0,4,0,-1};QCOMPARE(raster->SetGeoTransform(geo),CE_None);QCOMPARE(raster->SetSpatialRef(&crs),CE_None);QCOMPARE(raster->GetRasterBand(1)->Fill(.001),CE_None);QCOMPARE(raster->GetRasterBand(1)->SetNoDataValue(-9999),CE_None);GDALClose(raster);
 }
 void supportedSourcesAgreeAcrossUiRoutesAfterSaveAndRun(){
  const auto controlPath=output+"/control.inp";QVERIFY(writeFile(controlPath,deck));Ledgers control;QString error;QVERIFY2(simulate(controlPath,output+"/control",&control,&error),qPrintable(error));QCOMPARE(control.waterIn,0.);QCOMPARE(control.speciesIn,0.);QJsonObject evidence{{"control",json(control)}};Ledgers reference;
  for(int route=0;route<4;++route){const QString name=QStringList{"manual_rectangle","feature_polygon","raster_rectangle","raster_density_rectangle"}[route];const QString dir=output+"/"+name;QVERIFY(QDir().mkpath(dir));const auto input=dir+"/input.inp";QVERIFY(writeFile(input,deck));Journey j;QVERIFY2(j.open(input,&error),qPrintable(error));
   GroundwaterAssignDialog dialog(&j.model,&j.mesh,&j.canvas,&j.selection,&j.units);if(route==1)j.selectPolygon();else j.selectRectangle();QCOMPARE(j.selection.selection(),j.expected());
   auto*target=dialog.findChild<QComboBox*>("gwAssignmentTarget");QVERIFY(target);target->setCurrentIndex(target->findData("FLOW"));dialog.findChild<QDoubleSpinBox*>("gwAssignmentValue")->setValue(.001);dialog.findChild<QComboBox*>("gwAssignmentRoute")->setCurrentIndex(std::min(route,2));
   if(route){dialog.findChild<QLineEdit*>("gwAssignmentPath")->setText(route==1?vectorPath:rasterPath);if(route==1)dialog.findChild<QLineEdit*>("gwAssignmentField")->setText("flow");}
   if(route==3){
    auto*meaning=dialog.findChild<QComboBox*>("gwAssignmentRasterMeaning");QVERIFY(meaning);
    auto*units=dialog.findChild<QComboBox*>("gwAssignmentDensityUnits");QVERIFY(units);
    QVERIFY(meaning->findData("flux-density")>=0);meaning->setCurrentIndex(meaning->findData("flux-density"));
    QVERIFY(units->findData("m/s")>=0);units->setCurrentIndex(units->findData("m/s"));
   }
   QPushButton*addTerm=nullptr;for(auto*button:dialog.findChildren<QPushButton*>())if(button->text()==QStringLiteral("Add species term"))addTerm=button;QVERIFY(addTerm);addTerm->click();auto*terms=dialog.findChild<QTableWidget*>("gwAssignmentTerms");QVERIFY(terms);QCOMPARE(terms->rowCount(),1);auto*species=qobject_cast<QComboBox*>(terms->cellWidget(0,0));QVERIFY(species);QVERIFY(species->findData("TSS")>=0);species->setCurrentIndex(species->findData("TSS"));terms->item(0,2)->setText("100");
   GroundwaterTransportSnapshot before,after;QVERIFY2(readGroundwaterTransportSnapshot(j.model.engine(),&before,&error),qPrintable(error));QSignalSpy changed(&dialog,&GroundwaterAssignDialog::applied);QSignalSpy recipe(&dialog,&GroundwaterAssignDialog::recipeAccepted);
   dialog.findChild<QPushButton*>("gwAssignmentPreviewButton")->click();auto*apply=dialog.findChild<QPushButton*>("gwAssignmentApplyButton");QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(),15000);QVERIFY2(readGroundwaterTransportSnapshot(j.model.engine(),&after,&error),qPrintable(error));QVERIFY(after==before);
   QCOMPARE(dialog.findChild<QTableWidget*>("gwAssignmentPreview")->rowCount(),2);
   // Actual populated Qt widgets, retained for offscreen visual QA. Native
   // keyboard/VoiceOver/theme acceptance remains a separate manual check.
   dialog.resize(980,1100);dialog.ensurePolished();dialog.show();
   QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
   QVERIFY(dialog.grab().save(dir+"/reviewed_preview.png"));
   if(route==2){if(auto*scroll=dialog.findChild<QScrollArea*>())scroll->ensureWidgetVisible(dialog.findChild<QLineEdit*>("gwAssignmentPath"));QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);QVERIFY(dialog.grab().save(dir+"/reviewed_preview_source.png"));}
   apply->click();QTRY_COMPARE_WITH_TIMEOUT(changed.count(),1,15000);QCOMPARE(recipe.count(),1);QCOMPARE(j.canvas.undoStack()->count(),1);QVERIFY(readGroundwaterTransportSnapshot(j.model.engine(),&after,&error));QCOMPARE(after.sources.size(),2);
   for(const auto&r:after.sources){QCOMPARE(r.flow,route==3?(r.cell==0?.002:.004):.001);QCOMPARE(r.scale,1.);QCOMPARE(r.terms.size(),1);QCOMPARE(r.terms[0].species,QString("TSS"));QCOMPARE(r.terms[0].kind,QString("CONC"));QCOMPARE(r.terms[0].value,100.);}
   j.canvas.undoStack()->undo();GroundwaterTransportSnapshot undone;QVERIFY(readGroundwaterTransportSnapshot(j.model.engine(),&undone,&error));QVERIFY(undone==before);j.canvas.undoStack()->redo();QVERIFY(readGroundwaterTransportSnapshot(j.model.engine(),&undone,&error));QVERIFY(undone==after);
   const QString saved=dir+"/saved.inp";QCOMPARE(swmm_model_write(j.model.engine(),saved.toUtf8().constData()),SWMM_OK);QVERIFY(writeFile(dir+"/assignment_recipe.json",QJsonDocument(recipe.at(0).at(0).toJsonObject()).toJson()));
   {Engine reopened;QVERIFY(reopened.value);QCOMPARE(swmm_engine_open(reopened.value,saved.toUtf8().constData(),(dir+"/readback.rpt").toUtf8().constData(),nullptr,nullptr),SWMM_OK);GroundwaterTransportSnapshot restored;QVERIFY2(readGroundwaterTransportSnapshot(reopened.value,&restored,&error),qPrintable(error));QCOMPARE(restored.sources,after.sources);}
   Ledgers result;QVERIFY2(simulate(saved,dir+"/run",&result,&error),qPrintable(error));evidence[name]=json(result);QVERIFY(writeFile(output+"/ledger_comparison.json",QJsonDocument(evidence).toJson()));
   QVERIFY2(std::abs(result.waterIn-(route==3?1.8:.6))<1e-7,qPrintable(QString("water source ledger %1").arg(result.waterIn,0,'g',17)));QVERIFY2(std::abs(result.speciesIn-(route==3?180.:60.))<1e-6,qPrintable(QString("species source ledger %1").arg(result.speciesIn,0,'g',17)));QCOMPARE(result.waterOut,0.);QCOMPARE(result.speciesOut,0.);QVERIFY(std::abs(result.waterResidual)<1e-6);QVERIFY(std::abs(result.speciesResidual)<1e-6);QVERIFY(result.waterStorage>control.waterStorage);QVERIFY(result.speciesStorage>control.speciesStorage);
   if(route==0)reference=result;else if(route<3){QVERIFY(std::abs(result.waterIn-reference.waterIn)<1e-10);QVERIFY(std::abs(result.speciesIn-reference.speciesIn)<1e-8);QVERIFY(std::abs(result.waterStorage-reference.waterStorage)<1e-8);QVERIFY(std::abs(result.speciesStorage-reference.speciesStorage)<1e-6);}
  }
 }
};
QTEST_MAIN(TestPhase35ForcingJourney)
#include "test_phase35_forcingjourney.moc"
