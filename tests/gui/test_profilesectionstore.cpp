#include "project/profilesectionstore.h"
#include <QtTest>
class TestProfileSectionStore:public QObject {
 Q_OBJECT
 ProfileSection::Definition definition() {
    ProfileSection::Definition d;d.id="section-a";d.title="Combined levels";
    d.scenePolyline={{0,0},{10,0}};d.sceneCRS="EPSG:32618";d.horizontalUnits="m";
    d.primarySourceId="source-a";d.sources.append({"source-a","/models/run.h5",{}});
    return d;
 }
private slots:
 void saveReplaceNoOpAndOwnerIsolation(){
    QObject a,b;auto *s=ProfileSectionStore::forOwner(&a);auto *t=ProfileSectionStore::forOwner(&b);
    QSignalSpy edits(s,&ProfileSectionStore::edited);QString error;auto d=definition();
    QVERIFY2(s->saveDefinition(d,&error),qPrintable(error));QCOMPARE(edits.count(),1);
    QVERIFY(s->saveDefinition(d,&error));QCOMPARE(edits.count(),1);QVERIFY(t->definitions().isEmpty());
    d.title="Revised";QVERIFY(s->saveDefinition(d,&error));QCOMPARE(s->definitions().size(),1);QCOMPARE(edits.count(),2);
    QVERIFY(!s->removeDefinition("unknown"));QCOMPARE(edits.count(),2);
    QVERIFY(s->removeDefinition(d.id));QCOMPARE(edits.count(),3);
 }
 void restoreDoesNotMarkDirtyAndRetainsUnavailableSource(){
    ProfileSectionStore original,restored;QString error;QVERIFY(original.saveDefinition(definition(),&error));
    QJsonArray data;QVERIFY(original.serialize("/models",data,&error));
    QSignalSpy edits(&restored,&ProfileSectionStore::edited);QVERIFY2(restored.restore(data,"/models",&error),qPrintable(error));
    QCOMPARE(edits.count(),0);QCOMPARE(restored.definitions()[0].sources[0].path,QString("/models/run.h5"));
 }
 void malformedRestoreCannotEraseOrResavePriorSections(){
    ProfileSectionStore s;QString error;QVERIFY(s.saveDefinition(definition(),&error));
    QVERIFY(!s.restore(QJsonArray{42},"/models",&error));QCOMPARE(s.definitions().size(),1);
    QJsonArray out{QString("sentinel")};QVERIFY(!s.serialize("/models",out,&error));QCOMPARE(out,QJsonArray{QString("sentinel")});
    QVERIFY(!s.saveDefinition(definition(),&error));QVERIFY(!s.removeDefinition("section-a"));
 }
 void saveLimitMatchesRestoreLimit(){
    ProfileSectionStore store;QString error;auto d=definition();
    for(int i=0;i<1000;++i){d.id=QString::number(i);QVERIFY(store.saveDefinition(d,&error));}
    QSignalSpy edits(&store,&ProfileSectionStore::edited);d.id="overflow";QVERIFY(!store.saveDefinition(d,&error));QCOMPARE(edits.count(),0);
    QJsonArray data;QVERIFY(store.serialize("/models",data,&error));ProfileSectionStore restored;QVERIFY(restored.restore(data,"/models",&error));QCOMPARE(restored.definitions().size(),1000);
 }
 void duplicateIdentifiersRefusedAtomically(){
    ProfileSectionStore s;QString error;auto d=definition();auto j=ProfileSection::definitionToJson(d,"/models");
    QVERIFY(!s.restore(QJsonArray{j,j},"/models",&error));QVERIFY(s.definitions().isEmpty());
 }
};
QTEST_GUILESS_MAIN(TestProfileSectionStore)
#include "test_profilesectionstore.moc"
