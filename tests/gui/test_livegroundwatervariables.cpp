#include "io/mesh2dlivevariables.h"
#include "io/mesh2dvariableexport.h"
#include "io/mesh2dresultsexport.h"
#include "layers/swmm2dresultslayer.h"
#include "simulation/simulationrunner.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <limits>
using namespace openswmmvis::io;
namespace {
QString reviewDir() {
    return qEnvironmentVariable("SWMMVIS_LIVE_GW_OUTPUT", QFileInfo(QString::fromUtf8(__FILE__))
        .absoluteDir().absoluteFilePath("../../workplans/artifacts/phase_36_release/live-groundwater"));
}
bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
// INP CELL references are 1-based: CELL 2 is the second (dry) runtime cell.
// Aquifer thickness is 1 m beneath terrain 10 m, so base=9 m.
const char *deck = R"INP([TITLE]
Phase 35 combined section review: surface, groundwater and saturated TSS

[OPTIONS]
FLOW_UNITS CMS
FLOW_ROUTING DYNWAVE
CRS EPSG:32618
WATER_AGE ON
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
Trace UG/L 0.0 0.0 0.0 0.0 NO * 0.0 0.0

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
REPORT_2D NO
OUTPUT_FILE live-groundwater.h5
OUTPUT_PRECISION FLOAT64
TRANSPORT_POLLUTANTS NO
TRANSPORT_AGE NO
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
CELL 2 36.0 1.0 0.45 0.10 2.0 HG0 0

[GW_TRANSPORT_OPTIONS]
TRANSPORT_POLLUTANTS YES
TRANSPORT_AGE YES

[GW_INITIAL_QUALITY]
* SAT TSS 5.0
* UNSAT TSS 2.0
* SAT Trace 7.0
* SAT __WATER_AGE__ 3600
* UNSAT __WATER_AGE__ 3600
CELL 2 SAT TSS 25.0

[GW_SOURCES]
W * FLOW 0.0001 TSS CONC 2

[COORDINATES]
J1 500005.0 4500005.0
O1 500040.0 4500040.0

[REPORT]
INPUT NO
)INP";
struct Engine {
    SWMM_Engine handle = swmm_engine_create(); bool started = false;
    ~Engine() { close(); }
    void close() { if (handle) { if (started) swmm_engine_end(handle); swmm_engine_close(handle); swmm_engine_destroy(handle); handle=nullptr; } }
    QString error() const { return QString::fromUtf8(swmm_get_last_error_msg(handle)); }
};
std::unique_ptr<EngineMesh2DSource> source() {
    return std::make_unique<EngineMesh2DSource>(std::vector<double>{0,1,1,0},
        std::vector<double>{0,0,1,1},std::vector<double>{10,10,10,10},
        std::vector<std::array<int,3>>{{0,1,2},{0,2,3}});
}
Mesh2DResultVariable descriptor(const QString &species="TSS") {
    Mesh2DResultVariable d;d.dataset="Mesh2_face_gw_sat_conc";d.species=species;
    d.domain=Mesh2DResultVariable::Domain::Groundwater;d.zone=Mesh2DResultVariable::Zone::Saturated;
    d.units="MG/L";d.unitsKnown=true;return d;
}
Mesh2DLiveVariablesPtr frame(float value, bool reverse=false) {
    auto f=std::make_shared<Mesh2DLiveVariables>();f->cellCount=2;
    for(const QString &name:{QString("TSS"),QString("Other")}) {
        Mesh2DLiveVariable v;v.descriptor=descriptor(name);v.values={name=="TSS"?value:99.f,0};
        v.status={Mesh2DValueStatus::Valid,Mesh2DValueStatus::Waterless};f->variables.append(v);
    }
    if(reverse)std::reverse(f->variables.begin(),f->variables.end());return f;
}
const Mesh2DLiveVariable *find(const Mesh2DLiveVariablesPtr &f,const QString &species,Mesh2DResultVariable::Zone zone) {
    if(f)for(const auto &v:f->variables)if(v.descriptor.species==species&&v.descriptor.zone==zone)return &v;
    return nullptr;
}
}
class TestLiveGroundwaterVariables:public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QVERIFY(QDir().mkpath(reviewDir()));qRegisterMetaType<Mesh2DLiveVariablesPtr>(); }
    void actualCaptureUsesRuntimeNamesAndNativeUnits_data() {
        QTest::addColumn<bool>("reverse");QTest::newRow("pollutant-order")<<false;QTest::newRow("reversed-order")<<true;
    }
    void actualCaptureUsesRuntimeNamesAndNativeUnits() {
        QFETCH(bool,reverse);
        const QString root=reviewDir()+(reverse?"/reversed":"/original");QVERIFY(QDir().mkpath(root));
        QByteArray text(deck);
        if(reverse)text.replace("TSS MG/L 10.0 0.0 0.0 0.0 NO * 0.0 0.0\nTrace UG/L 0.0 0.0 0.0 0.0 NO * 0.0 0.0",
            "Trace UG/L 0.0 0.0 0.0 0.0 NO * 0.0 0.0\nTSS MG/L 10.0 0.0 0.0 0.0 NO * 0.0 0.0");
        const QString inp=root+"/model.inp";QVERIFY(writeFile(inp,text));Engine engine;QVERIFY(engine.handle);
        QCOMPARE(swmm_engine_open(engine.handle,inp.toUtf8().constData(),(root+"/model.rpt").toUtf8().constData(),
            (root+"/model.out").toUtf8().constData(),nullptr),SWMM_OK);
        QVERIFY2(swmm_engine_initialize(engine.handle)==SWMM_OK,qPrintable(engine.error()));
        QCOMPARE(swmm_engine_start(engine.handle,1),SWMM_OK);engine.started=true;
        QString error;const auto captured=captureGroundwaterVariables(engine.handle,2,&error);
        QVERIFY2(captured,qPrintable(error));QVERIFY(error.isEmpty());QCOMPARE(captured->variables.size(),8);
        const Mesh2DLiveVariable *table=nullptr,*base=nullptr;
        for(const auto &v:captured->variables){if(v.descriptor.dataset=="Mesh2_face_gw_table_elev")table=&v;if(v.descriptor.dataset=="Mesh2_face_gw_bed_elev")base=&v;}
        QVERIFY(table);QVERIFY(base);QCOMPARE(base->values[0],9.f);QCOMPARE(table->values[0],9.5f);
        QCOMPARE(base->descriptor.units,QString("m"));QCOMPARE(table->status[1],Mesh2DValueStatus::Valid);
        QCOMPARE(base->descriptor.temporal,Mesh2DResultVariable::Temporal::Static);
        auto late=source();const auto origin=QDateTime::fromSecsSinceEpoch(0);
        late->pushDepths({0,0},origin,0);late->pushDepths({0,0},origin.addSecs(1),1);
        QVERIFY(late->pushFaceVariables(captured,origin.addSecs(1),1));
        std::vector<float> staticValues;std::vector<Mesh2DValueStatus> staticStatus;
        QVERIFY(late->readFaceVariableAt(base->descriptor,0,staticValues,staticStatus));QCOMPARE(staticValues[0],9.f);
        for(const auto &d:late->faceVariables())if(d.dataset==base->descriptor.dataset)QCOMPARE(d.frameCount,0);
        const auto *tss=find(captured,"TSS",Mesh2DResultVariable::Zone::Saturated);QVERIFY(tss);
        QCOMPARE(tss->descriptor.units,QString("MG/L"));QVERIFY(tss->descriptor.unitsKnown);
        QCOMPARE(tss->values[0],5.f);QCOMPARE(tss->status[0],Mesh2DValueStatus::Valid);
        QCOMPARE(tss->values[1],0.f);QCOMPARE(tss->status[1],Mesh2DValueStatus::Waterless);
        const auto *trace=find(captured,"Trace",Mesh2DResultVariable::Zone::Saturated);QVERIFY(trace);
        QCOMPARE(trace->values[0],7.f);QCOMPARE(trace->descriptor.units,QString("UG/L"));
        const auto *age=find(captured,"__WATER_AGE__",Mesh2DResultVariable::Zone::Saturated);QVERIFY(age);
        QCOMPARE(age->values[0],3600.f);QCOMPARE(age->descriptor.units,QString("s"));
        QVERIFY(!captureGroundwaterVariables(engine.handle,3,&error));QVERIFY(!error.isEmpty());
        double elapsed=0;for(int i=0;i<4;++i)QCOMPARE(swmm_engine_step(engine.handle,&elapsed),SWMM_OK);
        const auto newer=captureGroundwaterVariables(engine.handle,2,&error);QVERIFY2(newer,qPrintable(error));
        const auto *newAge=find(newer,"__WATER_AGE__",Mesh2DResultVariable::Zone::Saturated);QVERIFY(newAge);
        // Age can decrease when young recharge/source water enters. This is
        // an immutable-snapshot check, not a closed-parcel ageing assertion.
        QVERIFY(newAge->values[0]!=age->values[0]); // actual engine mutated after capture
        engine.close();
        QCOMPARE(age->values[0],3600.f);QCOMPARE(tss->values[0],5.f); // survives engine destruction
        auto live=source();live->pushDepths({0,0},QDateTime::fromSecsSinceEpoch(0),0);
        QVERIFY(live->pushFaceVariables(captured,QDateTime::fromSecsSinceEpoch(0),0));live->markFinished();
        std::vector<float> values;std::vector<Mesh2DValueStatus> status;
        QVERIFY(live->readFaceVariableAt(tss->descriptor,0,values,status));QCOMPARE(status[0],Mesh2DValueStatus::Valid);
        const QString csv=root+"/retained.csv";QVERIFY2(exportMesh2DVariableCsv(*live,tss->descriptor.key(),0,csv,&error),qPrintable(error));
        QFile file(csv);QVERIFY(file.open(QIODevice::ReadOnly));const auto bytes=file.readAll();
        QVERIFY(bytes.contains(",0,5,valid,\"MG/L\""));QVERIFY(bytes.contains(",1,0,waterless,\"MG/L\""));
    }
    void actualMsxTemperatureAndCountUnitsUseDeclaredMetadata() {
        const QString root=reviewDir()+"/msx-temperature";QVERIFY(QDir().mkpath(root));
        // Exact built-in reaction grammar used by engine test_reaction_ard_binding.
        QVERIFY(writeFile(root+"/quality.rxn","[REACTION_OPTIONS]\nRATE_UNITS SEC\n"
            "[REACTION_SPECIES]\nBULK X MG\n[REACTION_PIPES]\nRATE X 0\n"
            "[REACTION_TANKS]\nRATE X 0\n[REACTION_QUALITY]\nGLOBAL X 8\n"));
        QByteArray text(deck);text.replace("WATER_AGE ON","WATER_AGE ON\nHEAT_TRANSPORT ON\nQUALITY_SOLVER EULERIAN_ARD");
        text.replace("TSS MG/L","TSS #/L");
        text.replace("[GW_TRANSPORT_OPTIONS]","[PROCESS_COMPONENTS]\norg.hydrocouple.openswmm.reactions config=quality.rxn\n\n[GW_TRANSPORT_OPTIONS]");
        text.replace("TRANSPORT_POLLUTANTS YES","TRANSPORT_POLLUTANTS YES\nTRANSPORT_MSX YES\nTRANSPORT_TEMPERATURE YES");
        text.replace("* SAT Trace 7.0","* SAT Trace 7.0\n* SAT X 8.0\n* UNSAT X 3.0\n* SAT __TEMPERATURE__ -2.5\n* UNSAT __TEMPERATURE__ 4.0");
        const QString inp=root+"/model.inp";QVERIFY(writeFile(inp,text));Engine engine;
        QVERIFY2(swmm_engine_open(engine.handle,inp.toUtf8().constData(),(root+"/model.rpt").toUtf8().constData(),
            (root+"/model.out").toUtf8().constData(),nullptr)==SWMM_OK,qPrintable(engine.error()));
        QVERIFY2(swmm_engine_initialize(engine.handle)==SWMM_OK,qPrintable(engine.error()));
        QCOMPARE(swmm_engine_start(engine.handle,1),SWMM_OK);engine.started=true;
        QString error;const auto captured=captureGroundwaterVariables(engine.handle,2,&error);QVERIFY2(captured,qPrintable(error));
        const auto *counts=find(captured,"TSS",Mesh2DResultVariable::Zone::Saturated);QVERIFY(counts);
        QCOMPARE(counts->descriptor.units,QString("#/L"));QCOMPARE(counts->values[0],5.f);
        const auto *msx=find(captured,"X",Mesh2DResultVariable::Zone::Saturated);QVERIFY(msx);
        QCOMPARE(msx->descriptor.units,QString("MG"));QVERIFY(msx->descriptor.unitsKnown);QCOMPARE(msx->values[0],8.f);
        const auto *heat=find(captured,"__TEMPERATURE__",Mesh2DResultVariable::Zone::Saturated);QVERIFY(heat);
        QCOMPARE(heat->descriptor.units,QString("degC"));QVERIFY(heat->descriptor.unitsKnown);
        QCOMPARE(heat->values[0],-2.5f);QCOMPARE(heat->status[0],Mesh2DValueStatus::Valid);
        engine.close();QCOMPARE(msx->values[0],8.f);QCOMPARE(heat->values[0],-2.5f);
    }
    void semanticIdentityMissingFramesAndLatePayloadInvalidateCache() {
        SWMM2DResultsLayer layer;auto live=source();auto *raw=live.get();layer.setSource(std::move(live));
        const QDateTime start=QDateTime::fromSecsSinceEpoch(100);
        raw->pushDepths({0,0},start,0);QVERIFY(raw->pushFaceVariables(frame(5),start,0));
        const auto old=layer.resultFrame(descriptor().key(),0);QVERIFY(old->error.isEmpty());QCOMPARE(old->values[0],5.f);
        raw->pushDepths({0,0},start.addSecs(1),1);
        const auto missing=layer.resultFrame(descriptor().key(),1);QCOMPARE(missing->status[0],Mesh2DValueStatus::Missing);
        QVERIFY(raw->pushFaceVariables(frame(12,true),start.addSecs(1),1));QCOMPARE(raw->timeCount(),2);
        const auto replaced=layer.resultFrame(descriptor().key(),1);QCOMPARE(replaced->values[0],12.f);
        QCOMPARE(old->values[0],5.f);QCOMPARE(missing->status[0],Mesh2DValueStatus::Missing);
        raw->markFinished();
        QVERIFY(!layer.resultFrame(descriptor().key(),1,true)->error.isEmpty()); // retained ticks are not whole-run extrema
        layer.closeSource(); // no source needed to retain already published frames
        QCOMPARE(old->values[0],5.f);QCOMPARE(replaced->values[0],12.f);
    }
    void historyThinsWholeSnapshotsAndCountsChemicalMemory() {
        auto live=source();live->setMaxFrames(8);live->setHistoryPinned(true);
        const auto start=QDateTime::fromSecsSinceEpoch(100);
        for(int i=0;i<20;++i){live->pushDepths({0,0},start.addSecs(i),i);QVERIFY(live->pushFaceVariables(frame(float(i),i%2),start.addSecs(i),i));}
        QCOMPARE(live->timeCount(),20);QCOMPARE(live->historyGeneration(),0);QCOMPARE(live->historyBytes(),size_t(20*(2*sizeof(float)+4*(sizeof(float)+sizeof(Mesh2DValueStatus)))));
        const int generation=live->historyGeneration();live->setHistoryPinned(false);
        QVERIFY(live->timeCount()<=8);QVERIFY(live->historyGeneration()>generation);
        std::vector<float> values;std::vector<Mesh2DValueStatus> status;
        for(int i=0;i<live->timeCount();++i){QVERIFY(live->readFaceVariableAt(descriptor(),i,values,status));QCOMPARE(values[0],float(start.secsTo(live->simTimeAt(i))));}
        QCOMPARE(values[0],19.f);
        auto budget=source();budget->setMaxFrames(0);budget->setMaxBytes(200);
        for(int i=0;i<20;++i){budget->pushDepths({0,0},start.addSecs(i),i);QVERIFY(budget->pushFaceVariables(frame(float(i)),start.addSecs(i),i));}
        QVERIFY(budget->timeCount()<20); // byte budget includes variable values AND masks
    }
    void pinnedHydraulicExportAllowsGroundwaterTicks() {
        auto live=source();live->setMaxFrames(8);const auto start=QDateTime::fromSecsSinceEpoch(100);
        for(int i=0;i<3;++i){live->pushDepths({1,1},start.addSecs(i),i);QVERIFY(live->pushFaceVariables(frame(float(i)),start.addSecs(i),i));}
        live->setHistoryPinned(true);const int generation=live->historyGeneration();
        Mesh2DExportOptions options;options.basePath=reviewDir()+"/pinned-groundwater";
        options.format=Mesh2DExportFormat::Shapefile;options.variables=Mesh2DDepth;
        options.timeSteps={0,1,2};options.includeMax=false;
        Mesh2DExportInputs inputs;inputs.source=live.get();Mesh2DExportReport report;
        int arrivals=0;
        const bool ok=exportMesh2DResults(inputs,options,[&](int,int,const QString&){
            const int t=live->timeCount();live->pushDepths({1,1},start.addSecs(t),t);
            if(!live->pushFaceVariables(frame(float(t)),start.addSecs(t),t))return false;
            ++arrivals;return true;
        },&report);
        QVERIFY2(ok,qPrintable(report.error));QVERIFY(arrivals>0);QCOMPARE(live->historyGeneration(),generation);
        std::vector<float> values;std::vector<Mesh2DValueStatus> status;
        QVERIFY(live->readFaceVariableAt(descriptor(),0,values,status));QCOMPARE(values[0],0.f);
        live->setHistoryPinned(false);
    }
    void rejectedPayloadsAreAtomicAndUnknownUnitsBlockExport() {
        auto live=source();const auto start=QDateTime::fromSecsSinceEpoch(0);
        live->pushDepths({0,0},start,0);QVERIFY(live->pushFaceVariables(frame(5),start,0));
        const int generation=live->historyGeneration();
        auto bad=std::make_shared<Mesh2DLiveVariables>(*frame(8));bad->variables[0].values.pop_back();
        QVERIFY(!live->pushFaceVariables(bad,start,0));QCOMPARE(live->historyGeneration(),generation);
        bad=std::make_shared<Mesh2DLiveVariables>(*frame(8));bad->variables[0].descriptor.units="UG/L";
        QVERIFY(!live->pushFaceVariables(bad,start,0));QCOMPARE(live->historyGeneration(),generation);
        bad=std::make_shared<Mesh2DLiveVariables>(*frame(8));bad->variables.append(bad->variables[0]);
        QVERIFY(!live->pushFaceVariables(bad,start,0));
        auto unknown=std::make_shared<Mesh2DLiveVariables>(*frame(8));
        for(auto &v:unknown->variables){v.descriptor.units.clear();v.descriptor.unitsKnown=false;}
        auto other=source();other->pushDepths({0,0},start,0);QVERIFY(other->pushFaceVariables(unknown,start,0));
        const QString path=reviewDir()+"/unknown-preserves.csv";QVERIFY(writeFile(path,"sentinel"));QString error;
        QVERIFY(!exportMesh2DVariableCsv(*other,descriptor().key(),0,path,&error));QVERIFY(error.contains("unit",Qt::CaseInsensitive));
        QFile f(path);QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),QByteArray("sentinel"));
    }
    void runnerPublishesDetachedWorkerCapture() {
        const QString root=reviewDir()+"/runner";QVERIFY(QDir().mkpath(root));const QString inp=root+"/model.inp";
        QVERIFY(writeFile(inp,deck));SimulationRunner runner(103,"live-groundwater",inp,root+"/model.rpt",root+"/model.out");
        QSignalSpy frames(&runner,&SimulationRunner::twoDVariablesAvailable),finished(&runner,&SimulationRunner::finished);
        runner.start();QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,30000);QVERIFY(finished[0][1].toBool());QVERIFY(!frames.isEmpty());
        const auto captured=qvariant_cast<Mesh2DLiveVariablesPtr>(frames[0][1]);QVERIFY(captured);
        const auto *tss=find(captured,"TSS",Mesh2DResultVariable::Zone::Saturated);QVERIFY(tss);
        QVERIFY(tss->descriptor.unitsKnown);QCOMPARE(tss->descriptor.units,QString("MG/L"));
        QVERIFY(std::isfinite(tss->values[0])); // worker engine already closed/destroyed
    }
};
QTEST_MAIN(TestLiveGroundwaterVariables)
#include "test_livegroundwatervariables.moc"
