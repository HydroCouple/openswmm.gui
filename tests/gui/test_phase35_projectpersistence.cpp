#include "project/profilesectionstore.h"
#include "project/groundwaterrecipestore.h"
#include "project/projectserializer.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"
#include "core/unitsystem.h"
#include <QFile>
#include <QJsonDocument>
#include <QtTest>
#include <memory>
#include <QQmlEngine>
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
class TestPhase35ProjectPersistence:public QObject {
 Q_OBJECT
 QString output(const QString &name){const QString dir=QDir::cleanPath(QStringLiteral(PHASE35_TEST_OUTPUT));QDir().mkpath(dir);return QDir(dir).filePath(name);}
 bool initialize(SWMMVisProjectWindow &window){SWMMModelLayer::NewProjectSpec spec;spec.name="Phase 35";spec.startDateTime=QDateTime(QDate(2026,1,1),QTime(0,0));spec.endDateTime=spec.startDateTime.addSecs(3600);QList<QString>w,e;return window.initializeBlankModel(spec,w,e);}
 ProfileSection::Definition definition(){ProfileSection::Definition d;d.id="section-1";d.title="Groundwater and surface";d.scenePolyline={{0,0},{10,0}};d.sceneCRS="EPSG:32618";d.horizontalUnits="m";d.primarySourceId="source-1";d.sources={{"source-1",output("unavailable-run.h5"),{}}};return d;}
 QJsonObject recipe(){return {{"version",1},{"refreshPolicy","snapshot-only"},{"target","Ks"},{"sourcePath",output("unavailable-input.tif")},{"cells",QJsonArray{0,2}}};}
 QByteArray bytes(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return file.readAll();}
private slots:
 void initTestCase(){qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM",1,0,"SWMMLayerQSGRenderer");qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM",1,0,"SWMM2DMeshQSGRenderer");qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM",1,0,"SWMM2DResultsQSGRenderer");}
 void engineVersionOnlyRecordsActualUserChanges(){
    std::unique_ptr<OpenSWMMVisWorkspace> workspace(OpenSWMMVisWorkspace::newInstance({},nullptr));
    SWMMVisProjectWindow window(workspace.get(),{});QVERIFY(initialize(window));
    window.setHasChanges(false);QSignalSpy edits(&window,&SWMMVisProjectWindow::hasChangesChanged);
    window.setEngineVersion(window.engineVersion());QVERIFY(!window.hasChanges());QCOMPARE(edits.count(),0);
    const QString changed=window.engineVersion()=="5.2.4"?QString("6.0.0"):QString("5.2.4");
    window.setEngineVersion(changed);QCOMPARE(window.engineVersion(),changed);QVERIFY(window.hasChanges());QCOMPARE(edits.count(),1);
 }
 void engineVersionHydrationPreservesDirtyState_data(){
    QTest::addColumn<bool>("dirty");QTest::addColumn<bool>("differentVersion");
    QTest::newRow("clean-same-version")<<false<<false;
    QTest::newRow("clean-different-version")<<false<<true;
    QTest::newRow("dirty-different-version")<<true<<true;
 }
 void engineVersionHydrationPreservesDirtyState(){
    QFETCH(bool,dirty);QFETCH(bool,differentVersion);
    std::unique_ptr<OpenSWMMVisWorkspace> workspace(OpenSWMMVisWorkspace::newInstance({},nullptr));
    SWMMVisProjectWindow window(workspace.get(),{});QVERIFY(initialize(window));
    const QString original=window.engineVersion();
    const QString restored=differentVersion?(original=="5.2.4"?QString("6.0.0"):QString("5.2.4")):original;
    QJsonObject session{{"inpPath",""},{"engineVersion",restored}};
    const auto path=output(QString("engine-version-%1.oswp").arg(QTest::currentDataTag()));
    QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));
    const auto data=QJsonDocument(QJsonObject{{"schemaVersion",5},{"sessions",QJsonArray{session}}}).toJson();
    QCOMPARE(file.write(data),qint64(data.size()));file.close();
    window.setHasChanges(dirty);QSignalSpy edits(&window,&SWMMVisProjectWindow::hasChangesChanged);
    QString error;QStringList warnings;
    QVERIFY2(ProjectSerializer::applyFromFile(path,&window,&error,&warnings),qPrintable(error));
    QCOMPARE(window.engineVersion(),restored);QCOMPARE(window.hasChanges(),dirty);QCOMPARE(edits.count(),0);
 }
 void terrainHydrationPreservesDirtyState_data(){
    QTest::addColumn<bool>("dirty");QTest::newRow("clean")<<false;QTest::newRow("existing-edit")<<true;
 }
 void terrainHydrationPreservesDirtyState(){
    QFETCH(bool,dirty);
    std::unique_ptr<OpenSWMMVisWorkspace> workspace(OpenSWMMVisWorkspace::newInstance({},nullptr));
    SWMMVisProjectWindow window(workspace.get(),{});QVERIFY(initialize(window));
    window.setTerrainVerticalUnit("ft");window.setHasChanges(dirty);
    QSignalSpy dirtyChanges(&window,&SWMMVisProjectWindow::hasChangesChanged);
    QSignalSpy terrainChanges(&window,&SWMMVisProjectWindow::activeTerrainChanged);
    window.restoreTerrainState({},1.25,2.5,"m");QCoreApplication::processEvents();
    QCOMPARE(window.terrainVerticalUnit(),QString("m"));
    QCOMPARE(window.terrainVertFactor(),window.unitSystem()->isSI()?1.0:1.0/0.3048);
    QCOMPARE(window.terrainNodeOffset(),1.25);QCOMPARE(window.terrainLinkOffset(),2.5);
    QCOMPARE(window.hasChanges(),dirty);QCOMPARE(dirtyChanges.count(),0);QVERIFY(terrainChanges.count()>0);
    window.setHasChanges(false);dirtyChanges.clear();window.setTerrainVerticalUnit("ft");
    QVERIFY(window.hasChanges());QCOMPARE(dirtyChanges.count(),1);
 }
 void savedSectionsAndAssignmentHistoryRoundTrip(){
    std::unique_ptr<OpenSWMMVisWorkspace> workspace(OpenSWMMVisWorkspace::newInstance({},nullptr));
    SWMMVisProjectWindow first(workspace.get(),{}),second(workspace.get(),{});QVERIFY(initialize(first));QVERIFY(initialize(second));
    first.setHasChanges(false);QString error;
    QVERIFY(ProfileSectionStore::forOwner(&first)->saveDefinition(definition(),&error));QVERIFY(first.hasChanges());
    QVERIFY(GroundwaterRecipeStore::forOwner(&first)->append(recipe(),&error));
    const auto path=output("sections-and-assignment.oswp");QVERIFY2(ProjectSerializer::saveToFile(path,&first,&error),qPrintable(error));
    const auto root=QJsonDocument::fromJson(bytes(path)).object();const auto session=root["sessions"].toArray()[0].toObject();
    QCOMPARE(session["profileSections"].toArray().size(),1);QCOMPARE(session["groundwaterAssignmentHistory"].toArray().size(),1);
    second.setHasChanges(false);QStringList warnings;
    QSignalSpy sectionEdits(ProfileSectionStore::forOwner(&second),&ProfileSectionStore::edited);
    QSignalSpy assignmentEdits(GroundwaterRecipeStore::forOwner(&second),&GroundwaterRecipeStore::edited);
    QVERIFY2(ProjectSerializer::applyFromFile(path,&second,&error,&warnings),qPrintable(error));
    const auto restored=ProfileSectionStore::forOwner(&second)->definitions();QCOMPARE(restored.size(),1);QCOMPARE(restored[0].sources[0].path,definition().sources[0].path);
    QCOMPARE(GroundwaterRecipeStore::forOwner(&second)->events()[0].toObject()["sourcePath"].toString(),recipe()["sourcePath"].toString());
    // Metadata and store hydration are read-only, including engine selection.
    QCoreApplication::processEvents();
    QVERIFY(!second.hasChanges());
    QCOMPARE(sectionEdits.count(),0);QCOMPARE(assignmentEdits.count(),0);
    auto legacy=root;auto sessions=legacy["sessions"].toArray();auto old=sessions[0].toObject();
    old.remove("profileSections");old.remove("groundwaterAssignmentHistory");sessions[0]=old;legacy["sessions"]=sessions;
    const auto legacyPath=output("legacy-no-sections.oswp");QFile legacyFile(legacyPath);QVERIFY(legacyFile.open(QIODevice::WriteOnly));legacyFile.write(QJsonDocument(legacy).toJson());legacyFile.close();
    QVERIFY(ProjectSerializer::applyFromFile(legacyPath,&second,&error,&warnings));
    QVERIFY(ProfileSectionStore::forOwner(&second)->definitions().isEmpty());QVERIFY(GroundwaterRecipeStore::forOwner(&second)->events().isEmpty());
    QCoreApplication::processEvents();
    QVERIFY(!second.hasChanges());
    QCOMPARE(sectionEdits.count(),0);QCOMPARE(assignmentEdits.count(),0);
 }
 void malformedSidecarWarnsAndCannotOverwriteOriginal(){
    std::unique_ptr<OpenSWMMVisWorkspace> workspace(OpenSWMMVisWorkspace::newInstance({},nullptr));
    SWMMVisProjectWindow window(workspace.get(),{});QVERIFY(initialize(window));QString error;
    const auto path=output("malformed-retained.oswp");
    QJsonObject session{{"inpPath",""},{"profileSections",QJsonObject{{"future",true}}}};
    const QByteArray original=QJsonDocument(QJsonObject{{"schemaVersion",5},{"sessions",QJsonArray{session}}}).toJson();
    QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(original),qint64(original.size()));file.close();
    QStringList warnings;QVERIFY(ProjectSerializer::applyFromFile(path,&window,&error,&warnings));QVERIFY(!warnings.isEmpty());
    QVERIFY(!ProjectSerializer::saveToFile(path,&window,&error));QVERIFY(!error.isEmpty());QCOMPARE(bytes(path),original);
 }
};
QTEST_MAIN(TestPhase35ProjectPersistence)
#include "test_phase35_projectpersistence.moc"
