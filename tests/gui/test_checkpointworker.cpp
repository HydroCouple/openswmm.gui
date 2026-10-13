/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <QtTest>
#include <QSignalSpy>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QFutureWatcher>
#include <filesystem>
#include <openswmm/engine/openswmm_checkpoint.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_surface_ownership.h>
#include "simulation/checkpointworker.h"

namespace {
QString root() {
    const auto path=QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA",QStringLiteral("."))).filePath("output_checkpointworker");
    QDir().mkpath(path);return path;
}
std::filesystem::path nativePath(const QString& p) {
#ifdef Q_OS_WIN
    return std::filesystem::path(p.toStdWString());
#else
    return std::filesystem::path(p.toUtf8().constData());
#endif
}
QByteArray bytes(const QString& path) {QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
QString put(const QString& name,const QByteArray& text) {
    const auto path=QDir(root()).filePath(name);QFile f(path);if(!f.open(QIODevice::WriteOnly|QIODevice::NewOnly))return {};f.write(text);return path;
}
const char* deck="[OPTIONS]\nFLOW_UNITS CMS\nFLOW_ROUTING DYNWAVE\nSTART_DATE 10/12/2026\nEND_DATE 10/12/2026\nEND_TIME 01:00:00\nROUTING_STEP .01\nVARIABLE_STEP 0\nREPORT_STEP 00:00:01\nIGNORE_QUALITY YES\nIGNORE_2D YES\nIGNORE_SNOWMELT YES\nIGNORE_GROUNDWATER YES\n[STORAGE]\nS 0 2 1 FUNCTIONAL 0 0 10 0 0\n[OUTFALLS]\nO -1 FREE NO\n[CONDUITS]\nC S O 10 .013 .1 0 0 0\n[XSECTIONS]\nC CIRCULAR .3 0 0 0\n";
struct Fixture {
    QString model,cp,staging;
    Fixture(const QString& name) {
        model=put(name+".inp",deck);cp=QDir(root()).filePath(name+".chk");staging=QDir(root()).filePath(name+"-staging");QDir().mkpath(staging);
        auto e=swmm_engine_create();const auto rpt=QDir(root()).filePath(name+".rpt");
        if(!e||swmm_engine_open(e,model.toUtf8().constData(),rpt.toUtf8().constData(),nullptr,nullptr))qFatal("fixture open failed");
        const SWMM_CheckpointSessionOptions opts{sizeof opts,1,0,nullptr,0,nullptr};
        if(swmm_checkpoint_start_session(e,&opts))qFatal("fixture start failed: %s",swmm_checkpoint_last_error_msg());
        double elapsed=0;for(int n=0;n<3;++n)if(swmm_engine_step(e,&elapsed))qFatal("fixture step failed");
        if(swmm_checkpoint_save(e,cp.toUtf8().constData()))qFatal("fixture Save failed");swmm_engine_close(e);swmm_engine_destroy(e);
    }
    CheckpointRequest request() const {
        CheckpointRequest r;r.projectRevision="revision-7";r.modelPath=model;r.checkpointPath=cp;r.modelSha256=QCryptographicHash::hash(bytes(model),QCryptographicHash::Sha256);r.stagingRoot=staging;return r;
    }
};
CheckpointResult finish(CheckpointWorker& worker,const CheckpointRequest& request) {
    QSignalSpy spy(&worker,&CheckpointWorker::finished);if(!worker.start(request))qFatal("dispatch failed");
    if(spy.isEmpty()&&!spy.wait(30000))qFatal("checkpoint worker did not drain");return qvariant_cast<CheckpointResult>(spy.last().at(0));
}
#ifdef OPENSWMM_HAS_2D
struct SurfaceFixture {
    QString model,cp,staging,token,authoredOutput;
    SurfaceFixture() {
        authoredOutput=put("original-final.h5","protected authored result");
        QByteArray d="[OPTIONS]\nFLOW_UNITS CMS\nFLOW_ROUTING DYNWAVE\nSTART_DATE 10/12/2026\nEND_DATE 10/12/2026\nEND_TIME 00:00:08\nROUTING_STEP .31\nVARIABLE_STEP 0\nWET_STEP 00:00:01\nDRY_STEP 00:00:01\nREPORT_STEP 00:00:02\nIGNORE_QUALITY YES\nIGNORE_SNOWMELT YES\nIGNORE_GROUNDWATER YES\n[STORAGE]\nSINK 0 100 0 FUNCTIONAL 0 0 10 0 0\n[RAINGAGES]\nR INTENSITY 0:00:01 1 TIMESERIES Rain\n[TIMESERIES]\nRain 10/12/2026 00:00:00 8\nRain 10/12/2026 00:00:04 0\n[SUBCATCHMENTS]\nA R SINK .0002 45 1 .5 0\n[SUBAREAS]\nA .015 .12 .03 .05 35 PERVIOUS 55\n[INFILTRATION]\nA 75 5 .3\n[EVAPORATION]\nCONSTANT 0\n[POLYGONS]\nA 1 0\nA 3 0\nA 3 1\nA 1 1\n[2D_OPTIONS]\nGROUNDWATER AUTO\nREPORT_2D_STEP 00:00:03\nMAX_TIMESTEP .12\nCOUPLING_SYNC .7\nOUTPUT_FILE ";
        d+='"'+authoredOutput.toUtf8()+"\"\n[2D_VERTICES]\n0 0 0\n2 0 0\n2 2 0\n0 2 0\n[2D_QUADS]\n0 1 2 3 .03\n[2D_TRIANGLE_NODE_MAP]\n0 SINK 0 1\n[2D_AQUIFER_OPTIONS]\nMODE MESH\nNODE_ENROLMENT ROWS\nLINK_SEEPAGE NONE\nGW_ET BOUNDARY_ET\nCLOSURE CLOSED_FORM\n[2D_AQUIFER]\n* 3.6 2 .45 .05 2 HG0 1\n[2D_SURFACE_OWNERSHIP]\nA SUBCATCH UNIFORM\n";
        model=put("surface.inp",d);cp=QDir(root()).filePath("surface.chk");staging=QDir(root()).filePath("surface-staging");QDir().mkpath(staging);
        auto e=swmm_engine_create();const QDir dir(root());
        if(swmm_engine_open(e,model.toUtf8().constData(),dir.filePath("surface.rpt").toUtf8().constData(),dir.filePath("surface.out").toUtf8().constData(),nullptr))qFatal("surface fixture open: %s",swmm_get_last_error_msg(e));
        if(swmm_file_path_set(e,SWMM_FILE_OUTPUT_2D,nullptr,dir.filePath("surface.h5").toUtf8().constData()))qFatal("surface path failed");
        int objects=0,shares=0,cells=0,valid=0;char t[128]={},diagnostic[4096]={};
        if(swmm_surface_owner_preview(e,nullptr,-1,nullptr,0,&objects,nullptr,0,&shares,nullptr,0,&cells,&valid,t,sizeof t,diagnostic,sizeof diagnostic,nullptr,nullptr)||!valid)qFatal("surface preview: %s",diagnostic);
        token=QString::fromUtf8(t);const auto source=dir.filePath("surface.history").toUtf8();const SWMM_CheckpointSessionOptions opts{sizeof opts,1,4,source.constData(),1,t};
        if(swmm_checkpoint_start_session(e,&opts))qFatal("surface session: %s",swmm_checkpoint_last_error_msg());
        double elapsed=0;for(int n=0;n<14;++n)if(swmm_engine_step(e,&elapsed))qFatal("surface step");
        if(swmm_checkpoint_save(e,cp.toUtf8().constData()))qFatal("surface Save: %s",swmm_checkpoint_last_error_msg());
        swmm_engine_end(e);swmm_engine_close(e);swmm_engine_destroy(e);
    }
    CheckpointRequest request() const {
        CheckpointRequest r;r.projectRevision="surface-1";r.modelPath=model;r.checkpointPath=cp;r.modelSha256=QCryptographicHash::hash(bytes(model),QCryptographicHash::Sha256);r.stagingRoot=staging;r.outputProfile=4;r.sourceHistory=true;r.detailedSources=true;r.ownershipToken=token;return r;
    }
};
#endif
bool emptyFolder(const QString& path) {return QDir(path).entryList(QDir::AllEntries|QDir::NoDotAndDotDot).isEmpty();}
}
class TestCheckpointWorker:public QObject {
    Q_OBJECT
private slots:
    void inspectOwnsEngineAndReturnsQueuedValues() {
        Fixture f("inspect");CheckpointWorker worker;bool guiThread=false;
        connect(&worker,&CheckpointWorker::finished,this,[&](const auto&) {guiThread=QThread::currentThread()==thread();});
        const auto saved=bytes(f.cp);auto result=finish(worker,f.request());
        QVERIFY2(result.success,qPrintable(result.error));QVERIFY(guiThread);QVERIFY(!worker.isBusy());QVERIFY(!result.stale);
        QCOMPARE(result.projectRevision,QString("revision-7"));QCOMPARE(result.metadata.value("schema_version").toInt(),1);
        QVERIFY(result.acceptedSeconds>0);QCOMPARE(bytes(f.cp),saved);QVERIFY(emptyFolder(f.staging));
        auto metadata=result.metadata;QCOMPARE(metadata.value("binary").toObject().value("enabled").toBool(),false);
        QVERIFY(!worker.queueSave(QDir(root()).filePath("not-running.chk")));
    }
    void refusesChangedSnapshotAndCorruptionAndLegacy() {
        Fixture f("refusals");CheckpointWorker worker;auto request=f.request();request.modelSha256=QByteArray(32,'0');
        auto result=finish(worker,request);QVERIFY(!result.success);QVERIFY(result.error.contains("changed"));QVERIFY(emptyFolder(f.staging));
        request=f.request();request.engineVersion="5.2.4";result=finish(worker,request);QVERIFY(!result.success);QVERIFY(emptyFolder(f.staging));
        request=f.request();request.checkpointPath=put("corrupt.chk","unrelated");result=finish(worker,request);QVERIFY(!result.success);QVERIFY(result.metadata.isEmpty());QVERIFY(emptyFolder(f.staging));
        request=f.request();request.outputProfile=1;result=finish(worker,request);QVERIFY(!result.success);QVERIFY(result.metadata.isEmpty());QVERIFY(emptyFolder(f.staging));
    }
    void cancelAndGenerationFenceDiscardInspectionAndDrain() {
        Fixture f("cancel");CheckpointWorker worker;QSignalSpy spy(&worker,&CheckpointWorker::finished);
        QVERIFY(worker.start(f.request()));QVERIFY(!worker.start(f.request()));worker.invalidate();
        if(spy.isEmpty())QVERIFY(spy.wait(30000));auto r=qvariant_cast<CheckpointResult>(spy.last().at(0));
        QVERIFY(r.stale);QVERIFY(r.metadata.isEmpty());QVERIFY(emptyFolder(f.staging));
        CheckpointWorker cancelled;QSignalSpy other(&cancelled,&CheckpointWorker::finished);QVERIFY(cancelled.start(f.request()));cancelled.cancel();
        if(other.isEmpty())QVERIFY(other.wait(30000));r=qvariant_cast<CheckpointResult>(other.last().at(0));QVERIFY(!r.success);QVERIFY(r.metadata.isEmpty());QVERIFY(emptyFolder(f.staging));
    }
    void rejectsExternalRowsBeforeNativeOpen() {
        Fixture f("external");CheckpointWorker worker;
        for(const auto& section:QStringList{"PLUGINS","PROCESS_COMPONENTS"}) {
            auto request=f.request();request.modelPath=put("external-"+section+".inp",QByteArray(deck)+"["+section.toUtf8()+"]\nUnavailable RemoteProvider\n");
            request.modelSha256=QCryptographicHash::hash(bytes(request.modelPath),QCryptographicHash::Sha256);
            const auto result=finish(worker,request);QVERIFY(!result.success);QCOMPARE(result.errorCode,int(SWMM_ERR_LIFECYCLE));
            QVERIFY(result.error.contains("cannot open plugin"));QVERIFY(emptyFolder(f.staging));
        }
    }
    void controllerDestructionWaitsForOwnedResources() {
        Fixture f("close");{CheckpointWorker worker;QVERIFY(worker.start(f.request()));}
        QVERIFY(emptyFolder(f.staging));QVERIFY(QFileInfo::exists(f.cp));
    }
    void cancellationAfterNativeCompletionStillDiscardsQueuedInspection() {
        Fixture f("late-cancel");CheckpointWorker worker;QSignalSpy spy(&worker,&CheckpointWorker::finished);QVERIFY(worker.start(f.request()));
        auto* base=worker.findChild<QFutureWatcherBase*>();QVERIFY(base);auto* watcher=static_cast<QFutureWatcher<CheckpointResult>*>(base);
        // Keep GUI delivery parked while the native task finishes and releases
        // its engine. Cancel must fence the still-queued compatibility result.
        QElapsedTimer timer;timer.start();while(!watcher->future().isFinished()&&timer.elapsed()<30000)QThread::msleep(1);
        QVERIFY(watcher->future().isFinished());QVERIFY(spy.isEmpty());worker.cancel();QVERIFY(spy.wait(30000));
        const auto result=qvariant_cast<CheckpointResult>(spy.last().at(0));QVERIFY(result.cancelled);QVERIFY(!result.success);QVERIFY(result.metadata.isEmpty());QVERIFY(emptyFolder(f.staging));
    }
    void protectsActiveHardlinksAndDirectoryAliases() {
        Fixture f("paths");CheckpointWorker worker;auto request=f.request();
        const auto alias=QDir(root()).filePath("active-alias.chk");std::filesystem::create_hard_link(nativePath(f.cp),nativePath(alias));
        request.activePaths={alias};auto result=finish(worker,request);QVERIFY(!result.success);QVERIFY(emptyFolder(f.staging));
        request=f.request();request.operation=CheckpointRequest::Operation::Resume;request.destination=f.staging;result=finish(worker,request);QVERIFY(!result.success);QVERIFY(emptyFolder(f.staging));
        const auto folder=QDir(root()).filePath("protected-parent"),link=QDir(root()).filePath("protected-link");QDir().mkpath(folder);std::filesystem::create_directory_symlink(nativePath(folder),nativePath(link));
        request.destination=QDir(link).filePath("run");request.protectedInputs={folder};result=finish(worker,request);QVERIFY(!result.success);QVERIFY(!QFileInfo::exists(request.destination));
        QCOMPARE(bytes(alias),bytes(f.cp));
    }
    void resumeQueuesExclusiveSaveAtAcceptedBoundaryAndKeepsPartialOutputs() {
        Fixture f("resume");CheckpointWorker worker;auto request=f.request();request.operation=CheckpointRequest::Operation::Resume;request.destination=QDir(root()).filePath("resumed-run");
        const auto saved=QDir(request.destination).filePath("accepted.chk");QSignalSpy spy(&worker,&CheckpointWorker::finished);
        QVERIFY(worker.start(request));QVERIFY(worker.queueSave(saved));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(saved),30000);worker.cancel();if(spy.isEmpty())QVERIFY(spy.wait(30000));
        const auto result=qvariant_cast<CheckpointResult>(spy.last().at(0));QVERIFY(result.cancelled);QVERIFY(!result.success);QVERIFY(result.acceptedSeconds>=.03);
        QCOMPARE(result.saves.size(),1);QVERIFY2(result.saves[0].success,qPrintable(result.saves[0].error));QVERIFY(QFileInfo::exists(QDir(request.destination).filePath("run.rpt")));
        CheckpointWorker inspector;request=f.request();request.checkpointPath=saved;const auto inspected=finish(inspector,request);QVERIFY2(inspected.success,qPrintable(inspected.error));
        QVERIFY(inspected.acceptedSeconds>=.03);QVERIFY(emptyFolder(f.staging));
    }
    void concurrentWorkersCannotUseTheSameRunFolder() {
        Fixture f("concurrent");CheckpointWorker first,second;auto request=f.request();request.operation=CheckpointRequest::Operation::Resume;request.destination=QDir(root()).filePath("concurrent-run");
        QSignalSpy spy(&first,&CheckpointWorker::finished);QVERIFY(first.start(request));const auto cp=QDir(request.destination).filePath("ready.chk");QVERIFY(first.queueSave(cp));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(cp),30000);auto result=finish(second,request);QVERIFY(!result.success);QVERIFY(result.error.contains("conflict")||result.error.contains("new"));
        first.cancel();if(spy.isEmpty())QVERIFY(spy.wait(30000));QVERIFY(QFileInfo::exists(cp));
    }
#ifdef OPENSWMM_HAS_2D
    void surfaceInspectionExportAndResumeUseReviewedRecipeAndPrivateWriters() {
        auto engine=swmm_engine_create();unsigned long caps=0;swmm_checkpoint_capabilities(engine,&caps);swmm_engine_destroy(engine);
        if(!(caps&SWMM_CHECKPOINT_FILE_PROFILE))QSKIP("File profile unavailable in this build");
        SurfaceFixture f;CheckpointWorker worker;auto result=finish(worker,f.request());QVERIFY2(result.success,qPrintable(result.error));
        QVERIFY(result.metadata.value("file_outputs").toBool());QVERIFY(result.metadata.value("detailed_sources").toBool());QCOMPARE(result.metadata.value("companions").toArray().size(),3);
        QCOMPARE(bytes(f.authoredOutput),QByteArray("protected authored result"));QVERIFY(emptyFolder(f.staging));
        auto request=f.request();request.operation=CheckpointRequest::Operation::Export;request.destination=QDir(root()).filePath("surface-bundle");QSignalSpy spy(&worker,&CheckpointWorker::finished);QVERIFY(worker.start(request));
        QElapsedTimer timer;timer.start();while(!QFileInfo::exists(QDir(request.destination).filePath("session.chk"))&&timer.elapsed()<30000)QThread::msleep(5);
        QVERIFY(QFileInfo::exists(QDir(request.destination).filePath("session.chk")));worker.invalidate();if(spy.isEmpty())QVERIFY(spy.wait(30000));result=qvariant_cast<CheckpointResult>(spy.last().at(0));
        QVERIFY2(result.success,qPrintable(result.error));QVERIFY(result.stale);QCOMPARE(result.publishedPath,request.destination);
        request=f.request();request.checkpointPath=QDir(result.publishedPath).filePath("session.chk");result=finish(worker,request);QVERIFY2(result.success,qPrintable(result.error));QVERIFY(result.metadata.value("relocatable_bundle").toBool());
        request.operation=CheckpointRequest::Operation::Resume;request.destination=QDir(root()).filePath("surface-resumed");QSignalSpy resumed(&worker,&CheckpointWorker::finished);QVERIFY(worker.start(request));QVERIFY(worker.queueSave(QDir(request.destination).filePath("resumed.chk")));
        if(resumed.isEmpty())QVERIFY(resumed.wait(30000));result=qvariant_cast<CheckpointResult>(resumed.last().at(0));QVERIFY2(result.success,qPrintable(result.error));QVERIFY(!result.saves.isEmpty());QVERIFY2(result.saves[0].success,qPrintable(result.saves[0].error));
        QVERIFY(result.acceptedSeconds>=8);QCOMPARE(bytes(f.authoredOutput),QByteArray("protected authored result"));QVERIFY(emptyFolder(f.staging));
    }
#endif
    void exportFailureDoesNotAdoptOrRemoveInputs() {
        Fixture f("export");CheckpointWorker worker;auto request=f.request();request.operation=CheckpointRequest::Operation::Export;request.destination=QDir(root()).filePath("non-file-bundle");
        const auto original=bytes(f.cp);const auto result=finish(worker,request);QVERIFY(!result.success);QVERIFY(result.publishedPath.isEmpty());QVERIFY(!QFileInfo::exists(request.destination));QCOMPARE(bytes(f.cp),original);
    }
};
QTEST_GUILESS_MAIN(TestCheckpointWorker)
#include "test_checkpointworker.moc"
