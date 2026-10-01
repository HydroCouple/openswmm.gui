#include "project/groundwaterrecipestore.h"
#include <QtTest>
class TestGroundwaterRecipeStore:public QObject {
 Q_OBJECT
 QJsonObject recipe(){return {{"version",1},{"refreshPolicy","snapshot-only"},{"target","Ks"},{"cells",QJsonArray{1,3}},{"sourcePath","/project/data/recharge.tif"},{"sourceSha256",QJsonObject{{"/project/data/recharge.tif","abc"}}}};}
private slots:
 void saveAsRebasesAndPreservesSnapshot(){
    GroundwaterRecipeStore s,t;QString error;QSignalSpy edits(&s,&GroundwaterRecipeStore::edited);
    QVERIFY(s.append(recipe(),&error));QCOMPARE(edits.count(),1);QJsonArray data;
    QVERIFY(s.serialize("/project",data,&error));QCOMPARE(data[0].toObject()["sourcePath"].toString(),QString("data/recharge.tif"));
    QSignalSpy restored(&t,&GroundwaterRecipeStore::edited);QVERIFY(t.restore(data,"/project",&error));QCOMPARE(restored.count(),0);
    QCOMPARE(t.events()[0].toObject()["sourcePath"].toString(),QString("/project/data/recharge.tif"));
    QJsonArray relocated;QVERIFY(t.serialize("/project/other",relocated,&error));
    QCOMPARE(relocated[0].toObject()["sourcePath"].toString(),QString("../data/recharge.tif"));
    QVERIFY(relocated[0].toObject()["sourceSha256"].toObject().contains("../data/recharge.tif"));
 }
 void malformedPayloadCannotBeSilentlyLost(){
    GroundwaterRecipeStore s;QString error;QVERIFY(s.append(recipe(),&error));
    auto invalid=recipe();invalid["version"]=99;
    QVERIFY(!s.restore(QJsonArray{recipe(),invalid},"/project",&error));QCOMPARE(s.events().size(),1);
    QJsonArray untouched{42};QVERIFY(!s.serialize("/project",untouched,&error));QCOMPARE(untouched,QJsonArray{42});
    QVERIFY(!s.append(recipe(),&error));
 }
 void manualSourcesStayEmptyAndOwnersStaySeparate(){
    QObject a,b;auto*s=GroundwaterRecipeStore::forOwner(&a);auto*t=GroundwaterRecipeStore::forOwner(&b);
    auto manual=recipe();manual["sourcePath"]="";manual.remove("sourceSha256");QVERIFY(s->append(manual));
    QJsonArray out;QVERIFY(s->serialize("/project",out));QVERIFY(out[0].toObject()["sourcePath"].toString().isEmpty());QVERIFY(t->events().isEmpty());
 }
};
QTEST_GUILESS_MAIN(TestGroundwaterRecipeStore)
#include "test_groundwaterrecipestore.moc"
