// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/models/meshsnowmodel.h"
#include <openswmm/engine/openswmm_subcatchments.h>
#include <QTest>
#include <cmath>
using namespace openswmmvis::ui;
class TestMeshSnowModel:public QObject {
    Q_OBJECT
private slots:
    void stagedInheritanceAndExactRoundTrip() {
        auto engine=swmm_engine_new();QVERIFY(engine);QVERIFY(swmm_snowpack_add(engine,"Mountain snow")>=0);
        const double exact=.12345678901234567;char error[1024];
        const QByteArray defaults=QString("* SNOWPACK \"Mountain snow\" FC %1\nforest SNOWPACK \"Mountain snow\" F 1.5").arg(exact,0,'g',17).toUtf8();
        QCOMPARE(swmm_snow2d_apply_configuration(engine,"SNOWMELT AUTO\nSNOW_STEP 00:30",defaults.constData(),"CELL 2 SNOWPACK NONE",error,sizeof(error)),SWMM_OK);
        MeshSnowModel model;QVERIFY(model.load(engine).isEmpty());QCOMPARE(model.rowCount(),3);QCOMPARE(model.step,1800.);
        const QStringList tags={"forest","forest","meadow"};QVERIFY(model.validate(tags).isEmpty());
        QString origin;auto row=model.effective(0,"forest",&origin);QCOMPARE(row.pack,QString("Mountain snow"));QCOMPARE(row.overrides[0],exact);QCOMPARE(row.overrides[1],1.5);QVERIFY(origin.contains("forest"));
        QCOMPARE(model.effective(1,"forest").pack,QString("NONE"));
        std::array<QString,3> before;QVERIFY(MeshSnowModel::readSections(engine,before).isEmpty());
        QVERIFY(model.setData(model.index(0,3),.7));std::array<QString,3> still;QVERIFY(MeshSnowModel::readSections(engine,still).isEmpty());QCOMPARE(still,before);
        QVERIFY(model.load(engine).isEmpty());QVERIFY(MeshSnowModel::applySections(engine,model.sections()).isEmpty());
        MeshSnowModel restored;QVERIFY(restored.load(engine).isEmpty());QCOMPARE(restored.effective(0,"forest").overrides[0],exact);
        swmm_engine_destroy(engine);
    }
    void invalidBatchLeavesEngineUnchanged() {
        auto engine=swmm_engine_new();QVERIFY(engine);QVERIFY(swmm_snowpack_add(engine,"SP")>=0);
        MeshSnowModel model;QVERIFY(model.load(engine).isEmpty());MeshSnowRow row;row.pack="SP";model.upsert(row);
        row.cell=0;row.tag.clear();row.overrides[4]=20;model.upsert(row);
        QVERIFY(model.validate({"forest"}).contains("both"));
        std::array<QString,3> before,after;QVERIFY(MeshSnowModel::readSections(engine,before).isEmpty());
        QVERIFY(!MeshSnowModel::applySections(engine,model.sections()).isEmpty());QVERIFY(MeshSnowModel::readSections(engine,after).isEmpty());QCOMPARE(after,before);
        QVERIFY(model.setData(model.index(1,8),180));QVERIFY(model.validate({"forest"}).isEmpty());
        QVERIFY(MeshSnowModel::applySections(engine,model.sections()).isEmpty());
        row.cell=9;row.overrides[5]=180;model.upsert(row);QVERIFY(model.validate({"forest"}).contains("no longer exists"));
        swmm_engine_destroy(engine);
    }
    void clearingOverrideRestoresInheritance() {
        MeshSnowModel model;model.packs={"NONE","SP"};MeshSnowRow row;row.pack="SP";row.overrides[0]=.2;model.upsert(row);
        row.cell=2;row.tag.clear();row.overrides[0]=.8;model.upsert(row);QCOMPARE(model.effective(2,"").overrides[0],.8);
        QVERIFY(model.setData(model.index(1,3),QString{}));QCOMPARE(model.effective(2,"").overrides[0],.2);
        QVERIFY(model.removeRows(1,1));QCOMPARE(model.effective(2,"").pack,QString("SP"));
    }
};
QTEST_MAIN(TestMeshSnowModel)
#include "test_meshsnowmodel.moc"
