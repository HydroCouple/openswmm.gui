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
class TestResultVariableExportDialog:public QObject {
 Q_OBJECT
 QString root;
 io::Mesh2DVariableGisSnapshotPtr snapshot() {
  auto s=std::make_shared<io::Mesh2DVariableGisSnapshot>();s->crsWkt="EPSG:32618";s->scalar.descriptor.dataset="Mesh2_face_gw_sat_conc";s->scalar.descriptor.species="TSS";s->scalar.descriptor.units="MG/L";s->scalar.descriptor.unitsKnown=true;
  s->vertices={{0,0},{1,0},{1,1},{0,1}};s->cells={{0,1,2,3}};s->scalar.values={42};s->scalar.status={io::Mesh2DValueStatus::Valid};return s;
 }
private slots:
 void initTestCase(){root=qEnvironmentVariable("SWMMVIS_SCALAR_GIS_OUTPUT",QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().filePath("../../workplans/artifacts/phase_36_acceptance/scalar-gis"));QVERIFY(QDir().mkpath(root));}
 void asyncImmutableExportAndDuplicateStart(){auto *dialog=new ui::ResultVariableExportDialog(snapshot());const QString path=QDir(root).filePath("dialog.gpkg");QFile::remove(path);dialog->findChild<QLineEdit *>("scalarGisPath")->setText(path);QSignalSpy done(dialog,&ui::ResultVariableExportDialog::exportFinished);dialog->startExport();dialog->startExport();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));QVERIFY(QFileInfo::exists(path));delete dialog;}
 void ownerDestructionCancelsWithoutSourcePointers(){
  auto *pool=QThreadPool::globalInstance();pool->waitForDone();const int maximum=pool->maxThreadCount();pool->setMaxThreadCount(1);
  QSemaphore entered,release;bool released=false;
  const auto cleanup=qScopeGuard([&]{if(!released)release.release();pool->waitForDone();pool->setMaxThreadCount(maximum);});
  auto blocker=QtConcurrent::run(pool,[&]{entered.release();release.acquire();});entered.acquire();
  const QString path=QDir(root).filePath("closed.gpkg");QFile::remove(path);auto *owner=new QWidget;
  auto *dialog=new ui::ResultVariableExportDialog(snapshot(),owner);dialog->findChild<QLineEdit *>("scalarGisPath")->setText(path);
  QPointer<ui::ResultVariableExportDialog> guard=dialog;dialog->startExport();delete owner;QVERIFY(!guard);
  release.release();released=true;blocker.waitForFinished();pool->waitForDone();QVERIFY(!QFileInfo::exists(path));
 }
};
QTEST_MAIN(TestResultVariableExportDialog)
#include "test_resultvariableexportdialog.moc"
