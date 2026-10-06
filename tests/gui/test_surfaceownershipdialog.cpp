#include "ui/dialogs/surfaceownershipdialog.h"
#include "assignment/surfaceownership.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "mesh/meshcellparams.h"
#include "ui/properties/meshtrianglepropertyadapter.h"
#include "mesh/meshobjectref.h"
#include "selection/selectionmanager.h"
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <cmath>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTabWidget>
#include <QtTest>
using namespace openswmmvis::ui;using namespace openswmmvis::assignment;
namespace {
mesh::MeshResult geometry(){mesh::MeshResult m;for(auto p:{QPointF(0,0),QPointF(20,0),QPointF(20,50),QPointF(0,50)})m.vertices.append({p,0});m.triangles={{0,1,2,3}};return m;}
struct Fixture {
 SWMMModelLayer model{QString()};SWMM2DMeshLayer mesh{geometry(),QStringLiteral("r4.2dm")};MapCanvas canvas;SelectionManager selection;
 QString output=qEnvironmentVariable("SWMMVIS_R4_OUT",QDir::currentPath()+"/tests/verification/surface_r4_2026-10-05/qt")+"/"+QString::fromLatin1(QTest::currentTestFunction());
 bool open(bool lid=false,const QString& type={}){QDir().mkpath(output);const auto path=output+"/model.inp";QFile::remove(path);if(!QFile::copy(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA",QDir::currentPath()+"/tests/gui/data")+(lid?"/surface_ownership_r4_lid.inp":"/surface_ownership_r4.inp"),path))return false;
  if(!type.isEmpty()){QFile f(path);if(!f.open(QIODevice::ReadOnly))return false;auto text=f.readAll();f.close();text.replace("L1 IT\n","L1 "+type.toUtf8()+"\nL1 SURFACE 300 0.1 0.1 1 1\nL1 SOIL 100 .45 .3 .1 5 4 50\nL1 PAVEMENT 100 .2 0 5 0\nL1 DRAINMAT 100 .5 .1\n");if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate))return false;f.write(text);}
  model.setModelFilePath(path);QList<QString>w,e;if(!model.loadModel(w,e))return false;mesh.setInfiltrationModel(&model);return true;}
};
void preview(SurfaceOwnershipDialog& d){d.findChild<QCheckBox*>("surfaceOwnerUniform")->setChecked(true);d.findChild<QPushButton*>("surfaceOwnerPreview")->click();}
}
class TestSurfaceOwnershipDialog:public QObject {
 Q_OBJECT
private slots:
 void explicitAssumptionAndCancelAreNoEdit(){
  Fixture f;QVERIFY(f.open());SurfaceOwnershipDialog d(&f.model,&f.mesh,&f.canvas,&f.selection);d.findChild<QPushButton*>("surfaceOwnerPreview")->click();QVERIFY(!d.findChild<QPushButton*>("surfaceOwnerApply")->isEnabled());QVERIFY(d.findChild<QLabel*>("surfaceOwnerStatus")->text().contains("Explicitly"));preview(d);QVERIFY(d.findChild<QPushButton*>("surfaceOwnerApply")->isEnabled());d.reject();QVector<int> records;QString e;QVERIFY(readSurfaceOwners(f.model.engine(),&records,&e));QVERIFY(records.isEmpty());QCOMPARE(f.canvas.undoStack()->count(),0);
 }
 void applyUndoRedoAndSharedFields(){
  Fixture f;QVERIFY(f.open());SurfaceOwnershipDialog d(&f.model,&f.mesh,&f.canvas,&f.selection);d.show();QTest::qWait(50);preview(d);auto* apply=d.findChild<QPushButton*>("surfaceOwnerApply");QVERIFY2(apply->isEnabled(),qPrintable(d.findChild<QLabel*>("surfaceOwnerStatus")->text()));auto* objects=d.findChild<QTableWidget*>("surfaceOwnerObjects");auto* receivers=d.findChild<QTableWidget*>("surfaceOwnerReceivers");QCOMPARE(objects->rowCount(),1);QCOMPARE(objects->item(0,7)->text(),QString("0.05"));QCOMPARE(receivers->item(0,6)->text(),QString("500"));QVERIFY(d.grab().save(f.output+"/ownership-review.png"));auto* tabs=d.findChild<QTabWidget*>();tabs->setCurrentIndex(1);QVERIFY(d.grab().save(f.output+"/receiving-review.png"));tabs->setCurrentIndex(2);QVERIFY(d.grab().save(f.output+"/results-unavailable.png"));
  QSignalSpy changes(&f.model,&SWMMModelLayer::optionsChanged);apply->click();QCOMPARE(f.canvas.undoStack()->count(),1);QCOMPARE(changes.count(),1);QVector<int> records;QString e;QVERIFY(readSurfaceOwners(f.model.engine(),&records,&e));QCOMPARE(records,QVector<int>{0});QCOMPARE(mesh::cellParamValue(f.mesh.mesh(),0,"surface.meshWeather"),50.);QCOMPARE(mesh::cellParamValue(f.mesh.mesh(),0,"surface.sourcePervious"),30.);QCOMPARE(mesh::cellParamValue(f.mesh.mesh(),0,"surface.owner"),1.);QCOMPARE(f.mesh.cellAttributeValues("surface.meshWeather").front(),50.f);MeshTrianglePropertyAdapter properties(&f.mesh,0);QCOMPARE(properties.remainingMeshWeather(),QString("50%"));QCOMPARE(properties.spatialSourcePervious(),QString("30%"));QCOMPARE(properties.surfaceOwnershipReview(),QString("Subcatchment / uniform"));
  f.canvas.undoStack()->undo();QVERIFY(readSurfaceOwners(f.model.engine(),&records,&e));QVERIFY(records.isEmpty());QVERIFY(std::isnan(mesh::cellParamValue(f.mesh.mesh(),0,"surface.meshWeather")));f.canvas.undoStack()->redo();QVERIFY(readSurfaceOwners(f.model.engine(),&records,&e));QCOMPARE(records,QVector<int>{0});QCOMPARE(mesh::cellParamValue(f.mesh.mesh(),0,"surface.meshWeather"),50.);
  QFile save(f.output+"/applied.inp");QCOMPARE(swmm_model_write(f.model.engine(),save.fileName().toUtf8().constData()),SWMM_OK);QVERIFY(save.open(QIODevice::ReadOnly));QVERIFY(save.readAll().contains("[2D_SURFACE_OWNERSHIP]"));
 }
 void lidFootprintAndSealedBottom(){
  Fixture f;QVERIFY(f.open(true));SurfaceOwnershipDialog d(&f.model,&f.mesh,&f.canvas,&f.selection);d.show();preview(d);QVERIFY2(d.findChild<QPushButton*>("surfaceOwnerApply")->isEnabled(),qPrintable(d.findChild<QLabel*>("surfaceOwnerStatus")->text()));auto* objects=d.findChild<QTableWidget*>("surfaceOwnerObjects");auto* receivers=d.findChild<QTableWidget*>("surfaceOwnerReceivers");QCOMPARE(objects->item(0,4)->text(),QString("0.02"));QCOMPARE(objects->item(0,5)->text(),QString("0.048"));QCOMPARE(receivers->item(0,3)->text(),QString("240"));QCOMPARE(receivers->item(0,4)->text(),QString("100"));QCOMPARE(receivers->item(0,5)->text(),QString("100"));QVERIFY(d.grab().save(f.output+"/lid-ownership.png"));d.findChild<QTabWidget*>()->setCurrentIndex(1);QVERIFY(d.grab().save(f.output+"/lid-receiving.png"));
  QCOMPARE(swmm_lid_set_storage(f.model.engine(),0,500,.4,0),SWMM_OK);preview(d);QCOMPARE(receivers->item(0,4)->text(),QString("100"));QCOMPARE(receivers->item(0,5)->text(),QString("0"));
 }
 void geometryChangesClearResolvedFields(){
  Fixture f;QVERIFY(f.open());SurfaceOwnershipDialog d(&f.model,&f.mesh,&f.canvas,&f.selection);preview(d);d.findChild<QPushButton*>("surfaceOwnerApply")->click();QCOMPARE(mesh::cellParamValue(f.mesh.mesh(),0,"surface.meshWeather"),50.);QVERIFY(f.mesh.applyMeshVertexZ(0,1));QVERIFY(std::isnan(mesh::cellParamValue(f.mesh.mesh(),0,"surface.meshWeather")));MeshTrianglePropertyAdapter properties(&f.mesh,0);QCOMPARE(properties.remainingMeshWeather(),QString("Unavailable"));
 }
 void nativeBottomEligibilityFollowsType(){
  for(const QString type:{"BC","RG","GR","IT","PP","RB","VS","RD"}){
   Fixture f;QVERIFY2(f.open(true,type),qPrintable(type));SurfaceOwnershipDialog d(&f.model,&f.mesh,&f.canvas,&f.selection);d.show();preview(d);
   QVERIFY2(d.findChild<QPushButton*>("surfaceOwnerApply")->isEnabled(),qPrintable(d.findChild<QLabel*>("surfaceOwnerStatus")->text()));
   auto* receivers=d.findChild<QTableWidget*>("surfaceOwnerReceivers");QCOMPARE(receivers->item(0,4)->text(),QString("100"));
   const bool native=type=="BC"||type=="RG"||type=="IT"||type=="PP"||type=="VS";
   QCOMPARE(receivers->item(0,5)->text(),native?QString("100"):QString("0"));
   d.findChild<QTabWidget*>()->setCurrentIndex(1);if(type=="GR"||type=="VS")QVERIFY(d.grab().save(f.output+"/"+type+"-receiving.png"));
   QCOMPARE(swmm_lid_set_storage(f.model.engine(),0,500,.4,0),SWMM_OK);preview(d);
   QCOMPARE(receivers->item(0,5)->text(),type=="VS"?QString("100"):QString("0"));
  }
 }
 void staleEngineEditCannotApply(){
  Fixture f;QVERIFY(f.open());SurfaceOwnershipDialog d(&f.model,&f.mesh,&f.canvas,&f.selection);preview(d);QVERIFY(d.findChild<QPushButton*>("surfaceOwnerApply")->isEnabled());QCOMPARE(swmm_subcatch_set_imperv_pct(f.model.engine(),0,30),SWMM_OK);d.findChild<QPushButton*>("surfaceOwnerApply")->click();QVERIFY(d.findChild<QLabel*>("surfaceOwnerStatus")->text().contains("stale"));QVector<int> records;QString e;QVERIFY(readSurfaceOwners(f.model.engine(),&records,&e));QVERIFY(records.isEmpty());QCOMPARE(f.canvas.undoStack()->count(),0);
 }
 void selectedScopeTagAndExactReceiverSelection(){
  Fixture f;QVERIFY(f.open());SurfaceOwnershipDialog d(&f.model,&f.mesh,&f.canvas,&f.selection);auto* scope=d.findChild<QComboBox*>("surfaceOwnerScope");scope->setCurrentIndex(1);preview(d);QVERIFY(!d.findChild<QPushButton*>("surfaceOwnerApply")->isEnabled());f.selection.select({{SWMMObjectRef::Subcatchment,"S1"}},SelectionManager::Replace);preview(d);QVERIFY(d.findChild<QPushButton*>("surfaceOwnerApply")->isEnabled());d.findChild<QTableWidget*>("surfaceOwnerObjects")->selectRow(0);d.findChild<QPushButton*>("surfaceOwnerSelectReceivers")->click();QVERIFY(f.selection.contains(mesh::MeshObjectRef::cell(f.mesh.sourcePath(),0)));QVERIFY(f.selection.contains({SWMMObjectRef::Subcatchment,"S1"}));
  scope->setCurrentIndex(2);d.findChild<QLineEdit*>("surfaceOwnerTag")->setText("missing");preview(d);QVERIFY(!d.findChild<QPushButton*>("surfaceOwnerApply")->isEnabled());
 }
 void unsynchronizedMeshBlocksReview(){
  Fixture f;QVERIFY(f.open());auto other=geometry();other.vertices[0].xy.setX(1);SWMM2DMeshLayer mesh(other,"different.2dm");SurfaceOwnershipDialog d(&f.model,&mesh,&f.canvas,&f.selection);preview(d);QVERIFY(!d.findChild<QPushButton*>("surfaceOwnerApply")->isEnabled());QVERIFY(d.findChild<QLabel*>("surfaceOwnerStatus")->text().contains("differ"));
 }
 void removingRecordsRestoresAbsenceInOneTransaction(){
  Fixture f;QVERIFY(f.open());SurfaceOwnershipDialog d(&f.model,&f.mesh,&f.canvas,&f.selection);preview(d);d.findChild<QPushButton*>("surfaceOwnerApply")->click();d.findChild<QComboBox*>("surfaceOwnerRepresentation")->setCurrentIndex(0);d.findChild<QPushButton*>("surfaceOwnerPreview")->click();QVERIFY(d.findChild<QPushButton*>("surfaceOwnerApply")->isEnabled());d.findChild<QPushButton*>("surfaceOwnerApply")->click();QCOMPARE(f.canvas.undoStack()->count(),2);QVector<int> records;QString e;QVERIFY(readSurfaceOwners(f.model.engine(),&records,&e));QVERIFY(records.isEmpty());f.canvas.undoStack()->undo();QVERIFY(readSurfaceOwners(f.model.engine(),&records,&e));QCOMPARE(records,QVector<int>{0});
 }
};
QTEST_MAIN(TestSurfaceOwnershipDialog)
#include "test_surfaceownershipdialog.moc"
