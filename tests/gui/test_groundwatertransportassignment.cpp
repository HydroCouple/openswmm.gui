#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <openswmm/engine/openswmm_gw_transport.h>
#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include "assignment/groundwatertransportassignment.h"
using namespace openswmmvis::assignment;
namespace {
GroundwaterTransportRequest request()
{
    GroundwaterTransportRequest r; r.before.cellCount=2; r.before.activeCells={true,true};
    r.before.species={{"TSS","mg/L","mg/s",true,false},{"__TEMPERATURE__","degC",{},false,true},{"MSX","mol/L",{},false,false}};
    r.cells={0,1}; r.values={3,3}; r.species="TSS"; return r;
}
struct Engine {
    SWMM_Engine value=nullptr;
    ~Engine(){if(value){swmm_engine_close(value);swmm_engine_destroy(value);}}
};
bool openFixture(Engine &engine,const QString &name)
{
    const QString root=qEnvironmentVariable("SWMMVIS_GW_TRANSPORT_TEST_OUTPUT",QDir::currentPath()+"/workplans/artifacts/phase_35_profiles_forcing/transport_tests");
    QDir().mkpath(root+"/"+name); const QString path=root+"/"+name+"/model.inp";
    QFile source(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA",QDir::currentPath()+"/tests/gui/data")+"/mini_2d.inp");
    if(!source.open(QIODevice::ReadOnly))return false;
    QFile destination(path); if(!destination.open(QIODevice::WriteOnly|QIODevice::Truncate))return false;
    destination.write(source.readAll()); destination.close();
    engine.value=swmm_engine_create();
    return engine.value && swmm_engine_open(engine.value,path.toUtf8().constData(),(root+"/"+name+"/model.rpt").toUtf8().constData(),(root+"/"+name+"/model.out").toUtf8().constData(),nullptr)==SWMM_OK
        && swmm_pollutant_add(engine.value,"TSS",0)==SWMM_OK
        && swmm_gw2d_row_add(engine.value,0,"",-1,0.001,5,0.4,0.05,1)==SWMM_OK;
}
}
class TestGroundwaterTransportAssignment:public QObject {
    Q_OBJECT
private slots:
    void regionTotalPreservesFlowAndMassButNotConcentrationScaling()
    {
        auto r=request(); r.target=GroundwaterTransportTarget::Source;
        r.distribution=GroundwaterSourceDistribution::RegionTotal; r.areaWeights={1,3};
        r.terms={{"TSS","MASS",{},8},{"__TEMPERATURE__","CONC",{},-2}};
        const auto p=previewGroundwaterTransport(r); QVERIFY2(p.error.isEmpty(),qPrintable(p.error));
        QCOMPARE(p.after.sources.size(),2); QCOMPARE(p.after.sources[0].scale,0.25); QCOMPARE(p.after.sources[1].scale,0.75);
        double flow=0,mass=0; for(const auto &row:p.after.sources) {flow+=row.flow*row.scale;mass+=row.terms[0].value*row.scale;QCOMPARE(row.terms[1].value,-2.0);}
        QCOMPARE(flow,3.0); QCOMPARE(mass,8.0);
    }
    void zeroSourceIsNoOpButMassOnlySourceRemainsOperational()
    {
        auto r=request();r.target=GroundwaterTransportTarget::Source;r.values={0,0};
        auto p=previewGroundwaterTransport(r);QVERIFY(p.error.isEmpty());QVERIFY(p.after==r.before);QVERIFY(p.cells.isEmpty());
        r.terms={{"TSS","MASS",{},1}};p=previewGroundwaterTransport(r);QVERIFY(p.error.isEmpty());QCOMPARE(p.after.sources.size(),2);
    }
    void perCellAndNamedSeriesRemainExplicit()
    {
        auto r=request(); r.target=GroundwaterTransportTarget::Source; r.values={-1,2};
        r.before.timeSeries["flow"]={QPointF(0,-1),QPointF(1,4)}; r.flowSeries="flow";
        const auto p=previewGroundwaterTransport(r); QVERIFY(p.error.isEmpty());
        for(const auto &row:p.after.sources){QCOMPARE(row.scale,1.0);QCOMPARE(row.series,QString("flow"));}
    }
    void unorderedNamedSeriesRefuseBeforeAnyMutation_data()
    {
        QTest::addColumn<QString>("kind"); QTest::addColumn<bool>("duplicate");
        for (const QString &kind : {QString("flow"), QString("CONC"), QString("MASS")})
            for (bool duplicate : {false, true})
                QTest::newRow(qPrintable(kind + (duplicate ? "-duplicate" : "-decreasing"))) << kind << duplicate;
    }
    void unorderedNamedSeriesRefuseBeforeAnyMutation()
    {
        QFETCH(QString,kind); QFETCH(bool,duplicate);
        auto r=request(); r.target=GroundwaterTransportTarget::Source;
        r.before.timeSeries["ordered"]={QPointF(1,2),QPointF(duplicate?1:0,3)};
        if(kind=="flow")r.flowSeries="ordered";
        else r.terms={{"TSS",kind,"ordered",0}};
        const auto invalid=previewGroundwaterTransport(r);
        QVERIFY2(invalid.error.contains("strictly increasing"),qPrintable(invalid.error));
        QVERIFY(invalid.after==r.before);
        r.before.timeSeries["ordered"][1].setX(2);
        const auto valid=previewGroundwaterTransport(r);
        QVERIFY2(valid.error.isEmpty(),qPrintable(valid.error));
    }
    void invalidTargetsRefuseBeforeAnyMutation_data()
    {
        QTest::addColumn<int>("caseId");
        for(int i=0;i<8;++i)QTest::newRow(qPrintable(QString::number(i)))<<i;
    }
    void invalidTargetsRefuseBeforeAnyMutation()
    {
        QFETCH(int,caseId); auto r=request();
        if(caseId==0)r.cells={0,0};
        if(caseId==1)r.cells={0,2};
        if(caseId==2)r.species="unavailable";
        if(caseId==3)r.zone=2;
        if(caseId==4)r.values[0]=-1;
        if(caseId==5)r.before.qualityFile="existing.csv";
        if(caseId==6){r.target=GroundwaterTransportTarget::Source;r.terms={{"MSX","MASS",{},1}};}
        if(caseId==7)r.before.activeCells[0]=false;
        const auto p=previewGroundwaterTransport(r); QVERIFY(!p.error.isEmpty()); QVERIFY(p.before==r.before); QVERIFY(p.after==r.before);
    }
    void upsertChangesOnlyExactCellZoneSpeciesAndPreservesGlobalRows()
    {
        auto r=request(); r.before.quality={{0,-1,0,-1,{},"TSS",9},{2,0,0,-1,{},"TSS",1},{2,0,1,-1,{},"TSS",8}};
        const auto p=previewGroundwaterTransport(r); QVERIFY(p.error.isEmpty()); QCOMPARE(p.after.quality.size(),4);
        QCOMPARE(p.after.quality[0].value,9.0); QCOMPARE(p.after.quality[1].value,3.0); QCOMPARE(p.after.quality[2].value,8.0);
        QCOMPARE(r.before.quality[1].value,1.0);
    }
    void authoredTagsAndPartialAquiferCoverageBeforeInitialize()
    {
        Engine e; QVERIFY(openFixture(e,"authored-tags"));
        QCOMPARE(swmm_2d_set_triangle_tag(e.value,0,"River"),SWMM_OK);
        QCOMPARE(swmm_gw2d_row_remove(e.value,0),SWMM_OK);
        QCOMPARE(swmm_gw2d_row_add(e.value,1,"River",-1,0.001,5,0.4,0.05,1),SWMM_OK);
        GroundwaterTransportSnapshot snapshot; QString error;
        QVERIFY2(readGroundwaterTransportSnapshot(e.value,&snapshot,&error),qPrintable(error));
        QCOMPARE(snapshot.cellTags,QStringList({"River",""}));
        QCOMPARE(snapshot.activeCells,QVector<bool>({true,false}));
        QCOMPARE(swmm_gw2d_row_add(e.value,2,"",1,0.002,5,0.4,0.05,1),SWMM_OK);
        QVERIFY2(readGroundwaterTransportSnapshot(e.value,&snapshot,&error),qPrintable(error));
        QCOMPARE(snapshot.activeCells,QVector<bool>({true,true}));
        char tag[32]{};
        QCOMPARE(swmm_2d_get_triangle_tag(e.value,snapshot.cellCount,tag,sizeof tag),SWMM_ERR_BADINDEX);
    }
    void changedSeriesOrRowsRejectStaleApplyAndUndo()
    {
        Engine e; QVERIFY(openFixture(e,"stale")); GroundwaterTransportSnapshot before; QString error;
        QVERIFY2(readGroundwaterTransportSnapshot(e.value,&before,&error),qPrintable(error));
        auto r=request();r.before=before;r.target=GroundwaterTransportTarget::Source;
        const auto p=previewGroundwaterTransport(r); QVERIFY2(p.error.isEmpty(),qPrintable(p.error));
        QCOMPARE(swmm_gw_source_set(e.value,"Other",2,"",0,9,""),SWMM_OK);
        const auto applied=applyGroundwaterTransport(e.value,p); QVERIFY(!applied.success); QVERIFY(!applied.changed);
        QCOMPARE(swmm_gw_source_count(e.value),1);
    }
    void appendApplyUndoAndInjectedFailurePreserveExistingSources()
    {
        Engine e; QVERIFY(openFixture(e,"apply"));
        QCOMPARE(swmm_gw_source_set(e.value,"Existing",2,"",0,1,""),SWMM_OK);
        GroundwaterTransportSnapshot before;QString error;QVERIFY(readGroundwaterTransportSnapshot(e.value,&before,&error));
        auto r=request();r.before=before;r.target=GroundwaterTransportTarget::Source;r.terms={{"TSS","CONC",{},5}};
        const auto p=previewGroundwaterTransport(r);QVERIFY2(p.error.isEmpty(),qPrintable(p.error));
        const auto failed=applyGroundwaterTransport(e.value,p,[](int write){return write!=3;});
        QVERIFY(!failed.success); QVERIFY(failed.changed); QVERIFY(failed.rollbackComplete);
        GroundwaterTransportSnapshot actual;QVERIFY(readGroundwaterTransportSnapshot(e.value,&actual,&error)); QVERIFY(actual==before);
        const auto applied=applyGroundwaterTransport(e.value,p);QVERIFY2(applied.success,qPrintable(applied.error));QVERIFY(applied.changed);
        const auto undone=undoGroundwaterTransport(e.value,p);QVERIFY2(undone.success,qPrintable(undone.error));
        QVERIFY(readGroundwaterTransportSnapshot(e.value,&actual,&error));QVERIFY(actual==before);
    }
    void groundwaterAgeUsesSecondsWithoutConvertingAuthoredValues()
    {
        Engine e; QVERIFY(openFixture(e,"age-seconds"));
        QCOMPARE(swmm_options_set(e.value,"WATER_AGE","YES"),SWMM_OK);
        QCOMPARE(swmm_gw_transport_option_set(e.value,"TRANSPORT_AGE","YES"),SWMM_OK);
        QString error;GroundwaterTransportSnapshot before;
        QVERIFY2(readGroundwaterTransportSnapshot(e.value,&before,&error),qPrintable(error));
        int ageRows=0;
        for(const auto &entry:before.species)if(entry.id=="__WATER_AGE__"){
            ++ageRows;QCOMPARE(entry.nativeConcUnits,QString("s"));
            QVERIFY(!entry.massSupported);QVERIFY(entry.nativeMassRateUnits.isEmpty());
        }
        QCOMPARE(ageRows,1);
        auto initial=request();initial.before=before;initial.species="__WATER_AGE__";initial.values={3600,3600};
        const auto initialPreview=previewGroundwaterTransport(initial);
        QVERIFY2(initialPreview.error.isEmpty(),qPrintable(initialPreview.error));
        const auto appliedInitial=applyGroundwaterTransport(e.value,initialPreview);
        QVERIFY2(appliedInitial.success,qPrintable(appliedInitial.error));
        GroundwaterTransportSnapshot seeded;
        QVERIFY2(readGroundwaterTransportSnapshot(e.value,&seeded,&error),qPrintable(error));
        QCOMPARE(seeded.quality.size(),2);
        for(const auto &row:seeded.quality){QCOMPARE(row.species,QString("__WATER_AGE__"));QCOMPARE(row.value,3600.0);}
        auto source=request();source.before=seeded;source.target=GroundwaterTransportTarget::Source;
        source.terms={{"__WATER_AGE__","CONC",{},3600}};
        const auto sourcePreview=previewGroundwaterTransport(source);
        QVERIFY2(sourcePreview.error.isEmpty(),qPrintable(sourcePreview.error));
        const auto appliedSource=applyGroundwaterTransport(e.value,sourcePreview);
        QVERIFY2(appliedSource.success,qPrintable(appliedSource.error));
        GroundwaterTransportSnapshot actual;
        QVERIFY2(readGroundwaterTransportSnapshot(e.value,&actual,&error),qPrintable(error));
        QCOMPARE(actual.sources.size(),2);
        for(const auto &row:actual.sources){QCOMPARE(row.terms.size(),1);QCOMPARE(row.terms[0].value,3600.0);}
        const auto undoneSource=undoGroundwaterTransport(e.value,sourcePreview);
        QVERIFY2(undoneSource.success,qPrintable(undoneSource.error));
        const auto undoneInitial=undoGroundwaterTransport(e.value,initialPreview);
        QVERIFY2(undoneInitial.success,qPrintable(undoneInitial.error));
        QVERIFY(readGroundwaterTransportSnapshot(e.value,&actual,&error));QVERIFY(actual==before);
    }
    void initialQualityFailureRestoresChangedAndAppendedRows()
    {
        Engine e; QVERIFY(openFixture(e,"quality"));
        QCOMPARE(swmm_gw_init_quality_set(e.value,2,"",0,0,-1,"TSS",0.123456789),SWMM_OK);
        GroundwaterTransportSnapshot before;QString error;QVERIFY(readGroundwaterTransportSnapshot(e.value,&before,&error));
        auto r=request();r.before=before;
        const auto p=previewGroundwaterTransport(r);QVERIFY2(p.error.isEmpty(),qPrintable(p.error));
        const auto result=applyGroundwaterTransport(e.value,p,[](int write){return write!=2;});
        QVERIFY(!result.success);QVERIFY(result.changed);QVERIFY(result.rollbackComplete);
        GroundwaterTransportSnapshot actual;QVERIFY(readGroundwaterTransportSnapshot(e.value,&actual,&error));QVERIFY(actual==before);
    }
};
QTEST_MAIN(TestGroundwaterTransportAssignment)
#include "test_groundwatertransportassignment.moc"
