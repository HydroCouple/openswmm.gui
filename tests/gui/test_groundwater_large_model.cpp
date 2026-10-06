// Real-cell authoring acceptance. Large sizes are opt-in and run one per process.
// This exercises production assignment services, not a simulated row-count loop.
#include "assignment/groundwaterassignment.h"
#include "assignment/groundwatertransportassignment.h"
#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_model.h>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#include <QtTest>
#include <cmath>
#include <functional>
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#endif
using namespace openswmmvis::assignment;
namespace {
struct Engine {
    SWMM_Engine value=swmm_engine_create();
    ~Engine(){if(value){swmm_engine_close(value);swmm_engine_destroy(value);}}
};
qint64 peakRss(){
#ifdef Q_OS_UNIX
    rusage r{};if(getrusage(RUSAGE_SELF,&r)!=0)return -1;
#ifdef Q_OS_MACOS
    return r.ru_maxrss;
#else
    return qint64(r.ru_maxrss)*1024;
#endif
#else
    return -1;
#endif
}
qint64 currentRss(){
#ifdef Q_OS_MACOS
    mach_task_basic_info_data_t info{};mach_msg_type_number_t count=MACH_TASK_BASIC_INFO_COUNT;
    return task_info(mach_task_self(),MACH_TASK_BASIC_INFO,reinterpret_cast<task_info_t>(&info),&count)==KERN_SUCCESS?qint64(info.resident_size):-1;
#else
    return -1; // unavailable rather than incorrectly calling peak RSS current
#endif
}
const char *prefix=R"INP([TITLE]
Large connected quad groundwater authoring acceptance
[OPTIONS]
FLOW_UNITS CMS
FLOW_ROUTING DYNWAVE
START_DATE 01/01/2026
START_TIME 00:00:00
END_DATE 01/01/2026
END_TIME 00:05:00
REPORT_STEP 00:01:00
ROUTING_STEP 5
[POLLUTANTS]
TSS MG/L 0 0 0 0 NO * 0 0
[JUNCTIONS]
J1 -2 4 0 0 0
[OUTFALLS]
O1 -2.5 FREE NO
[CONDUITS]
C1 J1 O1 30 .013 0 0 0
[XSECTIONS]
C1 CIRCULAR .5 0 0 0 1
[2D_OPTIONS]
INTEGRATOR EXPLICIT
MAX_TIMESTEP 5
REPORT_2D NO
[2D_AQUIFER_OPTIONS]
CLOSURE CLOSED_FORM
NODE_ENROLMENT ROWS
[2D_AQUIFER]
* 36 1 .45 .10 2 HG0 .5
[GW_TRANSPORT_OPTIONS]
TRANSPORT_POLLUTANTS YES
[2D_VERTICES]
)INP";
}
class TestGroundwaterLargeModel:public QObject {
    Q_OBJECT
    QString root,error;int cells=400;QJsonArray stages;qint64 largestSavedBytes=0;
    bool report(bool complete=false){
        qint64 retained=0;for(const auto &file:QDir(root).entryInfoList(QDir::Files))retained+=file.size();
        QJsonObject document{{ "schema",1},{"cells",cells},{"complete",complete},{"stages",stages},
            {"retained_disk_bytes",double(retained)},{"largest_saved_inp_bytes",double(largestSavedBytes)},
            {"estimated_peak_disk_with_atomic_output_bytes",double(retained+largestSavedBytes)},
            {"process_peak_rss_bytes",double(peakRss())},
            {"scope","Connected valid mesh; production snapshot/preview/Apply/Undo; INP write/fresh parse. No solver initialize/run or million-row widget rendering claim."},
            {"memory","Current RSS where available; peak RSS is cumulative process high-water, not per-stage allocation."},
            {"cancellation","Cooperative preview and injected checked-write abort/rollback measured. Engine parse/write have no cooperative cancellation API."},
            {"error",error}};
        QSaveFile f(root+"/metrics.json");return f.open(QIODevice::WriteOnly)&&f.write(QJsonDocument(document).toJson())>0&&f.commit();
    }
    bool stage(const QString &name,const std::function<bool()> &action){
        QElapsedTimer timer;const qint64 before=currentRss();timer.start();bool ok=false;
        try{ok=action();}catch(const std::exception&e){error=QString::fromUtf8(e.what());}
        const double ms=double(timer.nsecsElapsed())/1e6;
        stages.append(QJsonObject{{"stage",name},{"success",ok},{"elapsed_ms",ms},{"rss_before_bytes",double(before)},
            {"rss_after_bytes",double(currentRss())},{"process_peak_rss_bytes",double(peakRss())}});
        qInfo().noquote()<<name<<"cells="<<cells<<"ms="<<ms<<"peak_RSS="<<peakRss()<<"success="<<ok;
        if(!report()){error="Could not save stage evidence.";return false;}return ok;
    }
    bool cancellationStage(const QString &name,int requestAfter,
                           const std::function<bool(std::function<bool()>)> &action){
        return stage(name,[&]{
            int polls=0;QElapsedTimer timer;timer.start();qint64 requested=-1;
            const bool ok=action([&]{
                if(++polls<requestAfter)return false;
                if(requested<0)requested=timer.nsecsElapsed();return true;
            });
            const qint64 returned=timer.nsecsElapsed();
            stages.append(QJsonObject{{"stage",name+"_cancellation_timing"},{"polls",polls},
                {"request_after_polls",requestAfter},{"cancel_requested_ms",double(requested)/1e6},
                {"returned_ms",double(returned)/1e6},
                {"cancel_return_latency_ms",requested>=0?double(returned-requested)/1e6:-1.0}});
            return ok&&requested>=0;
        });
    }
    bool check(int code,SWMM_Engine engine){
        if(code==SWMM_OK)return true;
        error=QString("Engine error %1: %2").arg(code).arg(QString::fromUtf8(swmm_get_last_error_msg(engine)));return false;
    }
    bool generate(){
        int nx=int(std::sqrt(cells));while(cells%nx)--nx;const int ny=cells/nx;
        QSaveFile file(root+"/input.inp");if(!file.open(QIODevice::WriteOnly)){error=file.errorString();return false;}
        QTextStream out(&file);out<<prefix;
        for(int y=0;y<=ny;++y)for(int x=0;x<=nx;++x)out<<500000+x<<' '<<4500000+y<<" 0\n";
        out<<"[2D_QUADS]\n";
        for(int y=0;y<ny;++y)for(int x=0;x<nx;++x){const int a=y*(nx+1)+x;out<<a<<' '<<a+1<<' '<<a+nx+2<<' '<<a+nx+1<<" .03 0\n";}
        out.flush();if(out.status()!=QTextStream::Ok){error="Fixture stream failed.";return false;}
        return file.commit();
    }
    bool open(Engine&e,const QString &path,const QString &name){
        if(!e.value){error="Cannot create engine.";return false;}
        if(!check(swmm_engine_open(e.value,path.toUtf8().constData(),(root+"/"+name+".rpt").toUtf8().constData(),nullptr,nullptr),e.value))return false;
        int actual=0;if(!check(swmm_2d_cell_count(e.value,&actual),e.value))return false;
        if(actual!=cells){error="Real engine mesh cell count differs from the requested acceptance size.";return false;}return true;
    }
    bool fileEvidence(const QString &target){
        QFile f(root+"/assigned.inp");if(!f.open(QIODevice::ReadOnly)){error=f.errorString();return false;}
        QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&f)){error="Cannot hash saved INP.";return false;}
        largestSavedBytes=qMax(largestSavedBytes,f.size());
        stages.append(QJsonObject{{"stage",target+"_saved_file"},{"bytes",double(f.size())},{"sha256",QString::fromLatin1(hash.result().toHex())}});return report();
    }
    bool verifyAquifer(SWMM_Engine engine,const AquiferPreview&p){
        int count=0;if(!check(swmm_gw2d_row_count(engine,&count),engine)||count!=cells+1){error="Aquifer row count did not round trip.";return false;}
        for(int i=0;i<count;++i){
            const auto &expected=i==0?p.before.rows.front():p.appended[i-1];
            int scope=-1,cell=-1;char tag[4096]{};double ks=0,zs=0,ts=0,tr=0,alpha=0;
            if(!check(swmm_gw2d_row_get(engine,i,&scope,tag,sizeof tag,&cell,&ks,&zs,&ts,&tr,&alpha),engine))return false;
            if(scope!=expected.scope||cell!=expected.cell||QString::fromUtf8(tag)!=expected.tag||ks!=expected.ks||zs!=expected.zs||ts!=expected.thetaS||tr!=expected.thetaR||alpha!=expected.alpha){error=QString("Aquifer row %1 differs.").arg(i);return false;}
            for(auto it=expected.optional.cbegin();it!=expected.optional.cend();++it){double value=0;
                if(!check(swmm_gw2d_row_get_property(engine,i,it.key().toUtf8().constData(),&value),engine))return false;
                if(value!=it.value()){error=QString("Aquifer optional %1 differs in row %2.").arg(it.key()).arg(i);return false;}
            }
        }return true;
    }
private slots:
    void validMeshTransactionsAndRoundtrip(){
        bool valid=false;const QByteArray requested=qgetenv("SWMMVIS_LARGE_GW_CELLS");
        if(!requested.isEmpty()){cells=requested.toInt(&valid);QVERIFY2(valid&&cells>=4&&cells<=1000000,"Set 4..1000000 cells; run each size in a separate process.");}
        root=qEnvironmentVariable("SWMMVIS_LARGE_GW_OUTPUT",QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().absoluteFilePath("../../workplans/artifacts/phase_36_large_model"))+"/"+QString::number(cells);
        QVERIFY(QDir().mkpath(root));QVERIFY(report());
        QVERIFY2(stage("generate_valid_grid",[&]{return generate();}),qPrintable(error));
        Engine engine;QVERIFY2(stage("parse_input",[&]{return open(engine,root+"/input.inp","input");}),qPrintable(error));
        QVector<int> selected;selected.reserve(cells);for(int i=0;i<cells;++i)selected.append(i);
        {
            AquiferRequest r;r.target="KS";r.cells=selected;r.values.fill(48,cells);r.cellTags.fill(QString(),cells);
            QVERIFY2(stage("aquifer_snapshot",[&]{return readAquiferSnapshot(engine.value,&r.before,&error);}),qPrintable(error));
            AquiferPreview cancelled;
            QVERIFY2(cancellationStage("aquifer_cancel_preview",cells/2,[&](auto cancel){cancelled=previewAquiferAssignment(r,cancel);return cancelled.error=="Cancelled."&&cancelled.appended.isEmpty()&&cancelled.cells.isEmpty();}),qPrintable(error));
            AquiferPreview p;QVERIFY2(stage("aquifer_preview",[&]{p=previewAquiferAssignment(r);error=p.error;return error.isEmpty()&&p.appended.size()==cells;}),qPrintable(error));
            QVERIFY2(cancellationStage("aquifer_abort_apply_rollback",cells,[&](auto cancel){QString aborted;const bool applied=applyAquiferPreview(engine.value,p,&aborted,[&](int){return !cancel();});AquiferSnapshot now;return !applied&&readAquiferSnapshot(engine.value,&now,&error)&&now==r.before;}),qPrintable(error));
            QVERIFY2(stage("aquifer_apply",[&]{return applyAquiferPreview(engine.value,p,&error);}),qPrintable(error));
            QVERIFY2(stage("aquifer_verify_all_rows",[&]{return verifyAquifer(engine.value,p);}),qPrintable(error));
            QVERIFY2(stage("aquifer_write",[&]{return check(swmm_model_write(engine.value,(root+"/assigned.inp").toUtf8().constData()),engine.value);}),qPrintable(error));
            QVERIFY2(fileEvidence("aquifer"),qPrintable(error));
            {Engine reopened;QVERIFY2(stage("aquifer_parse_saved",[&]{return open(reopened,root+"/assigned.inp","aquifer_reopened");}),qPrintable(error));QVERIFY2(stage("aquifer_verify_saved",[&]{return verifyAquifer(reopened.value,p);}),qPrintable(error));}
            QVERIFY2(stage("aquifer_undo",[&]{return undoAquiferPreview(engine.value,p,&error);}),qPrintable(error));
            AquiferSnapshot restored;QVERIFY(readAquiferSnapshot(engine.value,&restored,&error));QVERIFY(restored==r.before);
        }
        for(const auto target:{GroundwaterTransportTarget::InitialQuality,GroundwaterTransportTarget::Source}){
            const QString name=target==GroundwaterTransportTarget::InitialQuality?"quality":"source";
            GroundwaterTransportRequest r;r.target=target;r.cells=selected;r.species="TSS";r.zone=0;r.sourceName="Acceptance";
            r.values.fill(target==GroundwaterTransportTarget::InitialQuality?7:.001,cells);
            if(target==GroundwaterTransportTarget::Source)r.terms={{"TSS","CONC",{},100}};
            QVERIFY2(stage(name+"_snapshot",[&]{return readGroundwaterTransportSnapshot(engine.value,&r.before,&error);}),qPrintable(error));
            QCOMPARE(r.before.cellCount,cells);QCOMPARE(r.before.activeCells.count(true),cells);
            GroundwaterTransportPreview cancelled;
            QVERIFY2(cancellationStage(name+"_cancel_preview",cells+cells/2,[&](auto cancel){cancelled=previewGroundwaterTransport(r,cancel);return cancelled.error=="Cancelled."&&cancelled.cells.isEmpty()&&cancelled.after==r.before;}),qPrintable(error));
            GroundwaterTransportPreview p;QVERIFY2(stage(name+"_preview",[&]{p=previewGroundwaterTransport(r);error=p.error;return error.isEmpty()&&p.cells.size()==cells;}),qPrintable(error));
            QVERIFY2(cancellationStage(name+"_abort_apply_rollback",cells/2,[&](auto cancel){const auto result=applyGroundwaterTransport(engine.value,p,[&](int){return !cancel();});GroundwaterTransportSnapshot now;error=result.error;return !result.success&&result.rollbackComplete&&readGroundwaterTransportSnapshot(engine.value,&now,&error)&&now==r.before;}),qPrintable(error));
            QVERIFY2(stage(name+"_apply",[&]{const auto result=applyGroundwaterTransport(engine.value,p);error=result.error;return result.success&&result.changed;}),qPrintable(error));
            QVERIFY2(stage(name+"_write",[&]{return check(swmm_model_write(engine.value,(root+"/assigned.inp").toUtf8().constData()),engine.value);}),qPrintable(error));
            QVERIFY2(fileEvidence(name),qPrintable(error));
            {Engine reopened;QVERIFY2(stage(name+"_parse_saved",[&]{return open(reopened,root+"/assigned.inp",name+"_reopened");}),qPrintable(error));
                GroundwaterTransportSnapshot saved;QVERIFY2(stage(name+"_verify_saved",[&]{if(!readGroundwaterTransportSnapshot(reopened.value,&saved,&error))return false;if(saved!=p.after){error="Saved transport snapshot differs from the reviewed applied snapshot.";return false;}return true;}),qPrintable(error));}
            QVERIFY2(stage(name+"_undo",[&]{const auto result=undoGroundwaterTransport(engine.value,p);error=result.error;return result.success&&result.changed;}),qPrintable(error));
            GroundwaterTransportSnapshot restored;QVERIFY(readGroundwaterTransportSnapshot(engine.value,&restored,&error));QVERIFY(restored==r.before);
        }
        error.clear();QVERIFY(report(true));
    }
};
QTEST_APPLESS_MAIN(TestGroundwaterLargeModel)
#include "test_groundwater_large_model.moc"
