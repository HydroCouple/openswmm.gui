#include "ui/dialogs/groundwaterassigndialog.h"
#include "assignment/groundwaterassignment.h"
#include "assignment/groundwatertransportassignment.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "mesh/meshobjectref.h"
#include "selection/selectionmanager.h"
#include "core/unitsystem.h"
#include "mesh/meshcellparams.h"
#include <QAccessible>
#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_infil2d.h>
#include "ui/properties/meshtrianglepropertyadapter.h"
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QtTest>
using namespace openswmmvis::ui;
using namespace openswmmvis::assignment;
namespace {
mesh::MeshResult geometry(){mesh::MeshResult m;for(const QPointF p:{QPointF(0,0),QPointF(2,0),QPointF(2,2),QPointF(0,2),QPointF(4,0),QPointF(4,2)})m.vertices.append({p,0});m.triangles={{0,1,2},{1,4,5,2}};return m;}
struct Fixture{
 SWMMModelLayer model{QString()};SWMM2DMeshLayer mesh{geometry(),QStringLiteral("assignment-test.2dm")};MapCanvas canvas;SelectionManager selection;UnitSystem units;
 bool open(){
  const QString output=qEnvironmentVariable("SWMMVIS_FORCING_TEST_OUTPUT",QDir::currentPath()+"/workplans/artifacts/phase_35_profiles_forcing/ui_tests")+"/"+QString::fromLatin1(QTest::currentTestFunction());if(!QDir().mkpath(output))return false;
  QFile source(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA",QDir::currentPath()+"/tests/gui/data")+"/mini_2d.inp");if(!source.open(QIODevice::ReadOnly))return false;auto content=source.readAll();const auto cut=content.indexOf("[2D_VERTICES]");if(cut<0)return false;content.truncate(cut);
  content+="[2D_VERTICES]\n0 0 10\n2 0 10\n2 2 10\n0 2 10\n4 0 10\n4 2 10\n\n[2D_TRIANGLES]\n0 1 2 0.03\n\n[2D_QUADS]\n1 4 5 2 0.03\n";
  const auto path=output+"/mixed_assignment.inp";QFile target(path);if(!target.open(QIODevice::WriteOnly|QIODevice::Truncate)||target.write(content)!=content.size())return false;target.close();model.setModelFilePath(path);QList<QString>w,e;if(!model.loadModel(w,e))return false;
  return swmm_gw2d_row_add(model.engine(),0,nullptr,-1,.123456789,5,.45,.1,2)==SWMM_OK;
 }
 void select(){selection.select({mesh::MeshObjectRef::cell(mesh.sourcePath(),0),mesh::MeshObjectRef::cell(mesh.sourcePath(),1)},SelectionManager::Replace);}
};
}
class TestGroundwaterAssignDialog:public QObject{
 Q_OBJECT
private slots:
 void ownershipMigrationIsReviewedAndUndoable(){
  Fixture f;QVERIFY(f.open());f.select();
  SWMM_Infil2DRow row{};row.has_method=1;row.method=SWMM_INFIL2D_CONSTANT;row.p[0]=2;row.dest=SWMM_INFIL2D_DEST_AQUIFER_2D;
  SWMM_Infil2DAuthoredRow authored{};authored.cell=0;authored.row=row;authored.dest_explicit=1;
  QCOMPARE(swmm_infil2d_replace_authored_rows(f.model.engine(),&authored,1),SWMM_OK);
  mesh::InfilRow local;local.method=mesh::InfilMethod::Constant;local.p[0]=2;local.dest=mesh::InfilDest::Aquifer2D;f.mesh.applyMeshTriangleInfil(0,local);
  GroundwaterAssignDialog dialog(&f.model,&f.mesh,&f.canvas,&f.selection,&f.units);
  auto* target=dialog.findChild<QComboBox*>("gwAssignmentTarget");target->setCurrentIndex(target->findData("INFILTRATION_OWNERSHIP"));
  auto* preview=dialog.findChild<QPushButton*>("gwAssignmentPreviewButton");auto* apply=dialog.findChild<QPushButton*>("gwAssignmentApplyButton");
  preview->click();QVERIFY(!apply->isEnabled());
  auto* table=dialog.findChild<QTableWidget*>("gwAssignmentPreview");QCOMPARE(table->columnCount(),4);QCOMPARE(table->item(0,1)->text(),QString("Aquifer"));
  dialog.findChild<QCheckBox*>("gwOwnershipRemoveOverrides")->setChecked(true);preview->click();QVERIFY2(apply->isEnabled(),qPrintable(dialog.findChild<QLabel*>("gwAssignmentStatus")->text()));
  dialog.resize(1100,800);dialog.show();QTest::qWait(200);
  const QString out=QDir::currentPath()+"/tests/gui/data/surface_r2_out";QDir().mkpath(out);QVERIFY(dialog.grab().save(out+"/ownership-review.png"));
  int count=0;QCOMPARE(swmm_infil2d_get_authored_rows(f.model.engine(),nullptr,0,&count),SWMM_OK);QCOMPARE(count,1);
  apply->click();QCOMPARE(f.canvas.undoStack()->count(),1);QCOMPARE(swmm_infil2d_get_authored_rows(f.model.engine(),nullptr,0,&count),SWMM_OK);QCOMPARE(count,0);QVERIFY(f.mesh.mesh().infilOverrides.isEmpty());
  f.canvas.undoStack()->undo();QCOMPARE(swmm_infil2d_get_authored_rows(f.model.engine(),nullptr,0,&count),SWMM_OK);QCOMPARE(count,1);QVERIFY(f.mesh.mesh().infilOverrides.contains(0));
  f.canvas.undoStack()->redo();QCOMPARE(swmm_infil2d_get_authored_rows(f.model.engine(),nullptr,0,&count),SWMM_OK);QCOMPARE(count,0);
  MeshTrianglePropertyAdapter property(&f.mesh,0);QCOMPARE(property.infiltrationOwner(),QString("Aquifer"));QVERIFY(property.infiltrationSource().contains("row 1"));
  QCOMPARE(mesh::cellParamValue(f.mesh.mesh(),0,"infil.owner"),2.);QCOMPARE(mesh::cellParamValue(f.mesh.mesh(),0,"infil.conflict"),0.);
 }
 void ownershipCancellationDoesNotWrite(){
  Fixture f;QVERIFY(f.open());f.select();QCOMPARE(swmm_options_set_ext(f.model.engine(),"INFIL_DESTINATION","AQUIFER_2D"),SWMM_OK);
  GroundwaterAssignDialog dialog(&f.model,&f.mesh,&f.canvas,&f.selection,&f.units);
  auto* target=dialog.findChild<QComboBox*>("gwAssignmentTarget");target->setCurrentIndex(target->findData("INFILTRATION_OWNERSHIP"));
  dialog.findChild<QPushButton*>("gwAssignmentPreviewButton")->click();QVERIFY(dialog.findChild<QPushButton*>("gwAssignmentApplyButton")->isEnabled());
  dialog.reject();char destination[100]{};QCOMPARE(swmm_options_get_ext(f.model.engine(),"INFIL_DESTINATION",destination,sizeof destination),SWMM_OK);QCOMPARE(QString(destination),QString("AQUIFER_2D"));QCOMPARE(f.canvas.undoStack()->count(),0);
 }

 void ownershipStalePreviewRefusesWholeMigration(){
  Fixture f;QVERIFY(f.open());f.select();QCOMPARE(swmm_options_set_ext(f.model.engine(),"INFIL_DESTINATION","AQUIFER_2D"),SWMM_OK);
  GroundwaterAssignDialog dialog(&f.model,&f.mesh,&f.canvas,&f.selection,&f.units);
  auto* target=dialog.findChild<QComboBox*>("gwAssignmentTarget");target->setCurrentIndex(target->findData("INFILTRATION_OWNERSHIP"));
  dialog.findChild<QPushButton*>("gwAssignmentPreviewButton")->click();auto*apply=dialog.findChild<QPushButton*>("gwAssignmentApplyButton");QVERIFY(apply->isEnabled());
  SWMM_Infil2DRow row{};row.has_method=1;row.method=SWMM_INFIL2D_CONSTANT;row.p[0]=10;
  QCOMPARE(swmm_infil2d_set_default(f.model.engine(),"*",&row),SWMM_OK);apply->click();
  char destination[100]{};QCOMPARE(swmm_options_get_ext(f.model.engine(),"INFIL_DESTINATION",destination,sizeof destination),SWMM_OK);QCOMPARE(QString(destination),QString("AQUIFER_2D"));
  int count=0;QCOMPARE(swmm_infil2d_get_authored_rows(f.model.engine(),nullptr,0,&count),SWMM_OK);QCOMPARE(count,1);QCOMPARE(f.canvas.undoStack()->count(),0);
 }

 void legacyGroundwaterTargetsPointToSupportedAssignment(){
  int found=0;for(const auto&spec:mesh::cellParamSpecs())if(spec.key.startsWith("gw.")){
   ++found;QVERIFY(!spec.enabled);QVERIFY(spec.tooltip.contains("Model"));QVERIFY(spec.tooltip.contains("Assign Groundwater"));QVERIFY(!spec.tooltip.contains("not yet available"));
  }QVERIFY(found>0);
 }
 void sourceFileLabelFocusesTheEditor(){Fixture f;QVERIFY(f.open());GroundwaterAssignDialog dialog(&f.model,&f.mesh,&f.canvas,&f.selection,&f.units);
  auto*path=dialog.findChild<QLineEdit*>("gwAssignmentPath");QVERIFY(path);QLabel*caption=nullptr;
  for(auto*label:dialog.findChildren<QLabel*>())if(label->text()=="Source &file:")caption=label;
  QVERIFY(caption);QCOMPARE(caption->buddy(),path);
  auto*accessible=QAccessible::queryAccessibleInterface(path);QVERIFY(accessible);QCOMPARE(accessible->text(QAccessible::Name),QString("Source file"));
 }
 void speciesTermControlsAnnounceRowAndFieldAfterRemoval(){Fixture f;QVERIFY(f.open());GroundwaterAssignDialog dialog(&f.model,&f.mesh,&f.canvas,&f.selection,&f.units);
  auto*target=dialog.findChild<QComboBox*>("gwAssignmentTarget");target->setCurrentIndex(target->findData("FLOW"));
  auto*terms=dialog.findChild<QTableWidget*>("gwAssignmentTerms");QVERIFY(terms);
  auto*preview=dialog.findChild<QTableWidget*>("gwAssignmentPreview");QVERIFY(preview);
  QVERIFY(!terms->accessibleName().isEmpty());QVERIFY(!preview->accessibleName().isEmpty());
  QPushButton*add=nullptr;QPushButton*remove=nullptr;
  for(auto*button:dialog.findChildren<QPushButton*>()){if(button->text()=="Add species term")add=button;if(button->text()=="Remove selected term")remove=button;}
  QVERIFY(add);QVERIFY(remove);add->click();add->click();QCOMPARE(terms->rowCount(),2);
  const auto verifyRow=[terms](int row){
   for(int column:{0,1,3,4}){auto*editor=terms->cellWidget(row,column);if(!editor)return false;
    auto*accessible=QAccessible::queryAccessibleInterface(editor);if(!accessible)return false;
    const auto description=accessible->text(QAccessible::Description);
    if(!description.contains(QString("row %1").arg(row+1))||!description.contains(terms->horizontalHeaderItem(column)->text()))return false;
   }return true;
  };
  QVERIFY(verifyRow(0));QVERIFY(verifyRow(1));terms->setCurrentCell(0,2);remove->click();QCOMPARE(terms->rowCount(),1);QVERIFY(verifyRow(0));
 }
 void auxiliaryButtonsCannotBecomeEnterDefaults(){Fixture f;QVERIFY(f.open());GroundwaterAssignDialog dialog(&f.model,&f.mesh,&f.canvas,&f.selection,&f.units);
  int checked=0;for(auto*button:dialog.findChildren<QPushButton*>())if(button->text()=="Add species term"||button->text()=="Remove selected term"||button->text()=="Browse…"){
   ++checked;QVERIFY2(!button->autoDefault(),qPrintable(button->text()));QVERIFY(!button->isDefault());
  }QCOMPARE(checked,3);
 }
 void previewDoesNotMutateAndApplyIsOneUndo(){Fixture f;QVERIFY(f.open());f.select();GroundwaterAssignDialog dialog(&f.model,&f.mesh,&f.canvas,&f.selection,&f.units);QSignalSpy applied(&dialog,&GroundwaterAssignDialog::applied);QSignalSpy recipe(&dialog,&GroundwaterAssignDialog::recipeAccepted);
  AquiferSnapshot before;QString error;QVERIFY(readAquiferSnapshot(f.model.engine(),&before,&error));
  dialog.findChild<QDoubleSpinBox*>("gwAssignmentValue")->setValue(2);
  dialog.findChild<QPushButton*>("gwAssignmentPreviewButton")->click();auto*apply=dialog.findChild<QPushButton*>("gwAssignmentApplyButton");QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(),10000);
  AquiferSnapshot now;QVERIFY(readAquiferSnapshot(f.model.engine(),&now,&error));QCOMPARE(now,before);
  auto*table=dialog.findChild<QTableWidget*>("gwAssignmentPreview");QCOMPARE(table->rowCount(),2);QVERIFY(table->isSortingEnabled());
  QMap<QString,QString> oldByCell;for(int row=0;row<table->rowCount();++row)oldByCell.insert(table->item(row,0)->text(),table->item(row,1)->text());
  table->sortItems(0,Qt::DescendingOrder);QCOMPARE(table->item(0,0)->text(),QStringLiteral("2"));
  dialog.findChild<QDoubleSpinBox*>("gwAssignmentValue")->setValue(3);dialog.findChild<QPushButton*>("gwAssignmentPreviewButton")->click();QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(),10000);
  QCOMPARE(table->rowCount(),2);QCOMPARE(table->item(0,0)->text(),QStringLiteral("2"));
  for(int row=0;row<table->rowCount();++row){QCOMPARE(table->item(row,1)->text(),oldByCell.value(table->item(row,0)->text()));QCOMPARE(table->item(row,2)->text().toDouble(),3.0);}
  apply->click();QCOMPARE(applied.count(),1);QCOMPARE(recipe.count(),1);QCOMPARE(f.canvas.undoStack()->count(),1);
  f.canvas.undoStack()->undo();QVERIFY(readAquiferSnapshot(f.model.engine(),&now,&error));QCOMPARE(now,before);f.canvas.undoStack()->redo();QVERIFY(readAquiferSnapshot(f.model.engine(),&now,&error));QCOMPARE(now.rows.size(),3);
 }
 void regionTotalSourcePreviewApplyUndo(){Fixture f;QVERIFY(f.open());f.select();GroundwaterAssignDialog dialog(&f.model,&f.mesh,&f.canvas,&f.selection,&f.units);
  auto*target=dialog.findChild<QComboBox*>("gwAssignmentTarget");QVERIFY(target->findData("FLOW")>=0);target->setCurrentIndex(target->findData("FLOW"));
  auto*distribution=dialog.findChild<QComboBox*>("gwAssignmentDistribution");QVERIFY(distribution);distribution->setCurrentIndex(1);dialog.findChild<QDoubleSpinBox*>("gwAssignmentValue")->setValue(-.003);
  GroundwaterTransportSnapshot before,now;QString error;QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(),&before,&error));
  QSignalSpy events(dialog.assignmentEvents(),&GroundwaterAssignmentEvents::changed);
  dialog.findChild<QPushButton*>("gwAssignmentPreviewButton")->click();auto*apply=dialog.findChild<QPushButton*>("gwAssignmentApplyButton");QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(),10000);
  QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(),&now,&error));QCOMPARE(now,before);apply->click();QCOMPARE(f.canvas.undoStack()->count(),1);QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(),&now,&error));QCOMPARE(now.sources.size(),2);
  double total=0;for(const auto&r:now.sources)total+=r.flow*r.scale;QVERIFY(qAbs(total+.003)<1e-12);QCOMPARE(now.sources[0].scale,1./3.);QCOMPARE(now.sources[1].scale,2./3.);
  f.canvas.undoStack()->undo();QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(),&now,&error));QCOMPARE(now,before);f.canvas.undoStack()->redo();QCOMPARE(events.count(),3);
  QCOMPARE(events.at(0).at(0).toJsonObject().value("event").toString(),QStringLiteral("applied"));QCOMPARE(events.at(1).at(0).toJsonObject().value("event").toString(),QStringLiteral("undone"));
 }
 void spatialRegionTotalRefusesAmbiguousFlux(){Fixture f;QVERIFY(f.open());f.select();GroundwaterAssignDialog dialog(&f.model,&f.mesh,&f.canvas,&f.selection,&f.units);
  auto*t=dialog.findChild<QComboBox*>("gwAssignmentTarget");t->setCurrentIndex(t->findData("FLOW"));auto*d=dialog.findChild<QComboBox*>("gwAssignmentDistribution");QVERIFY(d);d->setCurrentIndex(1);dialog.findChild<QComboBox*>("gwAssignmentRoute")->setCurrentIndex(2);
  dialog.findChild<QPushButton*>("gwAssignmentPreviewButton")->click();QVERIFY(dialog.findChild<QLabel*>("gwAssignmentStatus")->text().contains("total",Qt::CaseInsensitive));QVERIFY(!dialog.findChild<QPushButton*>("gwAssignmentApplyButton")->isEnabled());QCOMPARE(f.canvas.undoStack()->count(),0);
 }
 void editedScopeInvalidatesReviewedValues(){Fixture f;QVERIFY(f.open());f.select();GroundwaterAssignDialog dialog(&f.model,&f.mesh,&f.canvas,&f.selection,&f.units);
  dialog.findChild<QPushButton*>("gwAssignmentPreviewButton")->click();auto*apply=dialog.findChild<QPushButton*>("gwAssignmentApplyButton");QTRY_VERIFY(apply->isEnabled());
  f.selection.select(mesh::MeshObjectRef::cell(f.mesh.sourcePath(),0),SelectionManager::Replace);QVERIFY(!apply->isEnabled());QCOMPARE(f.canvas.undoStack()->count(),0);
 }
 void cancelQueuedWorkerAndOwnerClose(){Fixture f;QVERIFY(f.open());f.select();GroundwaterAssignDialog dialog(&f.model,&f.mesh,&f.canvas,&f.selection,&f.units);QSignalSpy applied(&dialog,&GroundwaterAssignDialog::applied);
  dialog.findChild<QPushButton*>("gwAssignmentPreviewButton")->click();dialog.reject();QTRY_VERIFY(dialog.findChild<QPushButton*>("gwAssignmentPreviewButton")->isEnabled());QVERIFY(!dialog.findChild<QPushButton*>("gwAssignmentApplyButton")->isEnabled());QCOMPARE(applied.count(),0);
  f.model.closeEngine();QVERIFY(!dialog.isEnabled());QCOMPARE(f.canvas.undoStack()->count(),0);
 }
 void typedCellsAndPolygonScope(){Fixture f;QVERIFY(f.open());GroundwaterAssignDialog dialog(&f.model,&f.mesh,&f.canvas,&f.selection,&f.units);
  auto*scope=dialog.findChild<QComboBox*>("gwAssignmentScope");auto*text=dialog.findChild<QLineEdit*>("gwAssignmentScopeText");auto*preview=dialog.findChild<QPushButton*>("gwAssignmentPreviewButton");auto*apply=dialog.findChild<QPushButton*>("gwAssignmentApplyButton");
  scope->setCurrentIndex(3);text->setText("2");preview->click();QTRY_VERIFY(apply->isEnabled());QCOMPARE(dialog.findChild<QTableWidget*>("gwAssignmentPreview")->item(0,0)->text(),QStringLiteral("2"));
  scope->setCurrentIndex(4);text->setText("POLYGON ((-1 -1, 2 -1, 2 3, -1 3, -1 -1))");preview->click();QTRY_VERIFY(apply->isEnabled());QCOMPARE(dialog.findChild<QTableWidget*>("gwAssignmentPreview")->rowCount(),1);QCOMPARE(dialog.findChild<QTableWidget*>("gwAssignmentPreview")->item(0,0)->text(),QStringLiteral("1"));
 }
 void staleEngineRowsRefuseWholeCommit(){Fixture f;QVERIFY(f.open());f.select();GroundwaterAssignDialog dialog(&f.model,&f.mesh,&f.canvas,&f.selection,&f.units);dialog.findChild<QPushButton*>("gwAssignmentPreviewButton")->click();auto*apply=dialog.findChild<QPushButton*>("gwAssignmentApplyButton");QTRY_VERIFY(apply->isEnabled());
  QCOMPARE(swmm_gw2d_row_set_property(f.model.engine(),0,"L",.123),SWMM_OK);apply->click();QCOMPARE(f.canvas.undoStack()->count(),0);int rows=0;swmm_gw2d_row_count(f.model.engine(),&rows);QCOMPARE(rows,1);QVERIFY(dialog.findChild<QLabel*>("gwAssignmentStatus")->text().contains("changed"));
 }
};
QTEST_MAIN(TestGroundwaterAssignDialog)
#include "test_groundwaterassigndialog.moc"
