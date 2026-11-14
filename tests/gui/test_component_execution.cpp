/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <QtTest>
#include <QSignalSpy>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include "simulation/componentexecution.h"
#include "simulation/runpathlease.h"
#include "ui/dialogs/componentexecutiondialog.h"
using namespace openswmmvis::simulation;
namespace {
QByteArray read(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))qFatal("Cannot read fixture");return f.readAll();}
void write(const QString &path,const QByteArray &bytes){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size())qFatal("Cannot write fixture");}
const QByteArray minimal="[OPTIONS]\nFLOW_UNITS CMS\nFLOW_ROUTING DYNWAVE\nSTART_DATE 01/01/2026\nSTART_TIME 00:00:00\nEND_DATE 01/01/2026\nEND_TIME 00:00:10\nROUTING_STEP 1\nREPORT_STEP 00:00:01\n[STORAGE]\nTank 0 10 1 FUNCTIONAL 0 0 100\n[OUTFALLS]\nO -1 FREE NO\n[CONDUITS]\nC Tank O 100 .013 0 0\n[XSECTIONS]\nC CIRCULAR 1 0 0 0 1\n[REPORT]\nNODES ALL\nLINKS ALL\n";
}
class ComponentExecutionTest:public QObject {
    Q_OBJECT
    QString root_,work_;
    QJsonObject settings(const QString &mode="split_material"){
        QObject owner;ComponentEngineRegistry m(&owner);auto s=m.settings();s["mode"]=mode;s["capacity"]=6;return s;
    }
    ComponentRunRequest request(const QString &mode="split_material"){
        ComponentRunRequest r;r.settings=settings(mode);r.directory=work_+"/run";r.model=work_+"/model.inp";write(r.model,minimal);
        r.program=qEnvironmentVariable("SWMMVIS_TEST_COMPOSER");r.components=qEnvironmentVariable("SWMMVIS_TEST_COMPONENTS");return r;
    }
private slots:
    void initTestCase(){root_=qEnvironmentVariable("SWMMVIS_COMPONENT_EVIDENCE",QDir::currentPath()+"/output_component_execution");QDir().mkpath(root_);
        QCoreApplication::setOrganizationName("OpenSWMMComponentTests");QCoreApplication::setApplicationName("ComponentExecution");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root_+"/preferences");}
    void init(){work_=root_+"/"+QString::fromLatin1(QTest::currentTestFunction());if(QTest::currentDataTag()&&*QTest::currentDataTag())work_+="-"+QString::fromLatin1(QTest::currentDataTag());QDir(work_).removeRecursively();QDir().mkpath(work_);}
    void projectOwnedSettingsPreserveOtherExtensions(){
        QObject project;project.setProperty("extensionData",QJsonObject{{"other",QJsonObject{{"keep",42}}}});
        auto *m=ComponentEngineRegistry::forProject(&project);QCOMPARE(m,ComponentEngineRegistry::forProject(&project));QVERIFY(m->error().isEmpty());QVERIFY(!project.property("extensionData").toJsonObject().contains("org.hydrocouple.openswmm.execution"));
        auto s=m->settings();s["mode"]="split_material";QString error;QVERIFY(m->setSettings(s,&error));
        const auto data=project.property("extensionData").toJsonObject();QCOMPARE(data.value("other").toObject().value("keep").toInt(),42);
        QObject restored;restored.setProperty("extensionData",data);ComponentEngineRegistry copy(&restored);QCOMPARE(copy.settings(),s);
        auto bad=s;bad["required_extension"]="future-owner";QVERIFY(!copy.setSettings(bad,&error));QCOMPARE(copy.settings(),s);
        restored.setProperty("extensionData",QJsonObject{{"org.hydrocouple.openswmm.execution",bad}});copy.reload();QVERIFY(!copy.error().isEmpty());
    }
    void standardGraphsHaveStablePortsAndPrivateOutputs(){
        for(const QString mode:{"complete","split_water","split_material"}){auto r=request(mode);r.surface=mode!="complete";const auto a=r.composition(),b=r.composition();QCOMPARE(a,b);QCOMPARE(a.value("schema_version").toString(),QString("1.1"));
            QCOMPARE(a.value("components").toArray().size(),mode=="complete"?1:2);QVERIFY(r.binary().startsWith(r.directory));
            QCOMPARE(r.mesh().isEmpty(),mode=="complete");if(mode!="complete")QCOMPARE(a.value("connections").toArray()[0].toObject().value("from").toObject().value("output").toString(),mode=="split_material"?QString("openswmm.node-surface-material.v10"):QString("openswmm.node-surface-water.v1"));
        }
    }
    void dialogUsesTheSharedProjectModel(){
        QObject project;ComponentEngineRegistry model(&project);ComponentExecutionDialog dialog(&model);auto *m=dialog.findChild<QComboBox *>("executionArrangement");QVERIFY(m);m->setCurrentIndex(m->findData("split_material"));dialog.findChild<QSpinBox *>("materialCapacity")->setValue(7);
        QSignalSpy changed(&model,&ComponentEngineRegistry::settingsChanged);dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();QCOMPARE(changed.count(),1);QCOMPARE(model.mode(),QString("split_material"));QCOMPARE(model.settings().value("capacity").toInt(),7);
    }
    void missingHostAndExistingOutputAreRefused(){
        auto r=request("complete");r.program=work_+"/missing";ComposerSimulationRunner run(1,"missing",r);QSignalSpy done(&run,&SimulationRunner::finished);run.start();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);QVERIFY(!done[0][1].toBool());QVERIFY(!QFileInfo::exists(r.directory));QVERIFY(RunPathLease::activePaths().isEmpty());
    }
    void prelaunchCancellationSettlesNestedShutdown_data(){QTest::addColumn<bool>("dates");QTest::newRow("started")<<false;QTest::newRow("dates-known")<<true;}
    void prelaunchCancellationSettlesNestedShutdown(){
        QFETCH(bool,dates);auto r=request("complete");r.program="/usr/bin/true";r.components=work_;
        ComposerSimulationRunner run(1,"shutdown",r);QSignalSpy done(&run,&SimulationRunner::finished);bool entered=false;
        auto close=[&]{entered=true;QEventLoop drain;connect(&run,&SimulationRunner::finished,&drain,&QEventLoop::quit);QTimer timer;timer.setSingleShot(true);connect(&timer,&QTimer::timeout,&drain,&QEventLoop::quit);timer.start(3000);run.cancel();drain.exec();QCOMPARE(done.count(),1);};
        if(dates)connect(&run,&SimulationRunner::simulationDatesKnown,&run,close);else connect(&run,&SimulationRunner::started,&run,close);
        run.start();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,5000);QVERIFY(entered);QCOMPARE(done[0][2].toInt(),0);QVERIFY(!QFileInfo::exists(r.directory));QVERIFY(RunPathLease::activePaths().isEmpty());
    }
    // The launcher reads only the simulation period (swmm_engine_open_simulation_period), so a 2D
    // model's mesh is never parsed there: even a missing mesh file reaches dates-known, and the
    // component run itself reports it.
    void launcherReadsTheSimulationPeriodWithoutTheMesh(){
        auto r=request("complete");r.program="/usr/bin/true";r.components=work_;
        write(r.model,minimal+"[2D_OPTIONS]\nMAX_TIMESTEP 1\n[2D_MESH_FILE]\nFILE missing.2dm\n");
        ComposerSimulationRunner run(1,"period",r);QSignalSpy dates(&run,&SimulationRunner::simulationDatesKnown);QSignalSpy done(&run,&SimulationRunner::finished);
        run.start();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);QCOMPARE(dates.count(),1);QVERIFY(RunPathLease::activePaths().isEmpty());
    }
    void existingRootAndActiveInputAreNeverOverwritten(){
        auto r=request("complete");r.program="/usr/bin/true";r.components=work_;QDir().mkpath(r.directory);write(r.directory+"/sentinel","keep");
        ComposerSimulationRunner run(1,"existing",r);QSignalSpy done(&run,&SimulationRunner::finished);run.start();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);QVERIFY(!done[0][1].toBool());QCOMPARE(read(r.directory+"/sentinel"),QByteArray("keep"));
        r.directory=work_+"/another-run";int owner=0;RunPathLease lease(&owner);QString error;QVERIFY(lease.add(r.model,true,&error));ComposerSimulationRunner blocked(2,"busy",r);QSignalSpy stopped(&blocked,&SimulationRunner::finished);blocked.start();QTRY_COMPARE_WITH_TIMEOUT(stopped.count(),1,10000);QVERIFY(!stopped[0][1].toBool());QVERIFY(!QFileInfo::exists(r.directory));
    }
    void longValidProgressStreamOutlivesRetainedLog(){
        auto r=request("complete");r.components=work_;r.program=work_+"/composer.py";
        const QByteArray script="#!/usr/bin/env python3\nimport sys,json,hashlib,uuid\np=sys.argv[sys.argv.index('--run')+1];b=open(p,'rb').read();g=json.loads(b);rid=str(uuid.uuid4());n=0\ndef emit(kind,**kw):\n global n\n s='HYDROCOUPLE_EVENT '+json.dumps(dict(schema='hydrocouple.run-events/1',event=kind,run_id=rid,sequence=n,**kw));n+=1;print(s);return len(s)+1\nemit('started',composition=p,composition_sha256=hashlib.sha256(b).hexdigest());size=0\nwhile size<17*1024*1024:\n size+=emit('progress',components=[dict(id=g['components'][0]['id'],current_julian_day=2461041.5,status_code=9)])\nemit('finished',succeeded=False,message='valid terminal after large log',manifest=g['run']['manifest']);sys.stdout.flush()\n";
        write(r.program,script);QVERIFY(QFile::setPermissions(r.program,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner));
        ComposerSimulationRunner run(1,"long-log",r);QSignalSpy done(&run,&SimulationRunner::finished);run.start();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);QVERIFY(!done[0][1].toBool());QVERIFY2(done[0][3].toString().contains("valid terminal after large log"),qPrintable(done[0][3].toString()));QVERIFY(QFileInfo(r.directory+"/composer.log").size()<=16*1024*1024+128);
    }
    void malformedAndIncompleteProtocolIsRefused_data(){QTest::addColumn<QString>("fault");for(const char *fault:{"wrong-hash","bad-sequence","wrong-run","malformed","lost-final","truncated","failed-exit","unknown-component","huge-clock","early-final"})QTest::newRow(fault)<<QString::fromLatin1(fault);}
    void malformedAndIncompleteProtocolIsRefused(){
        QFETCH(QString,fault);auto r=request("complete");r.components=work_;r.program=work_+"/composer.py";
        QByteArray script="#!/usr/bin/env python3\nimport sys,json,hashlib,uuid\np=sys.argv[sys.argv.index('--run')+1]\nb=open(p,'rb').read();g=json.loads(b);rid=str(uuid.uuid4());seq=0\ndef emit(event,**kw):\n global seq\n print('HYDROCOUPLE_EVENT '+json.dumps(dict(schema='hydrocouple.run-events/1',event=event,run_id=rid,sequence=seq,**kw)),flush=True);seq+=1\nfault='"+fault.toUtf8()+"'\nif fault=='malformed':\n print('HYDROCOUPLE_EVENT {oops}',flush=True);sys.exit(0)\nemit('started',composition=p,composition_sha256=('bad' if fault=='wrong-hash' else hashlib.sha256(b).hexdigest()))\nif fault=='bad-sequence':seq+=1\nif fault=='wrong-run':rid=str(uuid.uuid4())\nif fault=='truncated':\n print('HYDROCOUPLE_EVENT {',end='',flush=True);sys.exit(0)\nif fault=='lost-final':sys.exit(0)\nif fault=='unknown-component':\n emit('initializing',component='unknown');sys.exit(0)\nemit('finished',succeeded=True,manifest=g['run']['manifest'])\nsys.exit(1 if fault=='failed-exit' else 0)\n";
        write(r.program,script);QVERIFY(QFile::setPermissions(r.program,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner));
        if(fault=="huge-clock"||fault=="early-final"){
            auto source=read(r.program);
            source.replace("emit('finished',succeeded=True",QByteArray("emit('progress',components=[dict(id=g['components'][0]['id'],current_julian_day=")+(fault=="huge-clock"?"1e300":"2461041.5")+",status_code=9)])\n"+
                "import os\ncfg=g['components'][0]['arguments']['configuration'];d=cfg['output_directory'];os.makedirs(d)\njson.dump(dict(schema='openswmm.component-metadata/1',instance_id=g['components'][0]['id'],epoch_julian_day=2461041.5,duration_s=10,result_files=dict(binary='model.out',report='model.rpt')),open(d+'/metadata.json','w'))\n"+
                "emit('finished',succeeded=True");
            write(r.program,source);
        }
        ComposerSimulationRunner run(1,"protocol",r);QSignalSpy done(&run,&SimulationRunner::finished);run.start();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);QVERIFY(!done[0][1].toBool());QVERIFY(!done[0][3].toString().isEmpty());QVERIFY(RunPathLease::activePaths().isEmpty());
    }
    void actualMaterialComposerMatchesQualifiedNative_data(){
        QTest::addColumn<QString>("tag");for(const QString b:{"mg","ug","count"})for(const QString s:{"positive","negative"})QTest::newRow(qPrintable(b+"-"+s))<<b+"-"+s;
    }
    void actualMaterialComposerMatchesQualifiedNative(){
        QFETCH(QString,tag);auto r=request();const auto reference=qEnvironmentVariable("SWMMVIS_TEST_MATERIAL_REFERENCE");
        if(r.program.isEmpty()||r.components.isEmpty()||reference.isEmpty())QSKIP("Actual Composer locations were not supplied.");
        const auto prefix=QDir(reference).filePath(tag);write(r.model,read(prefix+".inp"));r.surface=true;const auto before=read(r.model);
        ComposerSimulationRunner run(1,tag,r);SimulationRunner *base=&run;QVERIFY(!base->supportsPause());QVERIFY(!base->supportsPartialResults());
        QSignalSpy done(&run,&SimulationRunner::finished),progress(&run,&SimulationRunner::progressChanged);run.start();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);
        QVERIFY2(done[0][1].toBool(),qPrintable(done[0][3].toString()));QVERIFY(progress.count()>0);QCOMPARE(read(r.binary()),read(prefix+"-native.out"));QCOMPARE(read(r.model),before);
        QProcess diff;diff.start("h5diff",{prefix+"-native.h5",r.mesh()});QVERIFY(diff.waitForFinished(10000));QCOMPARE(diff.exitCode(),0);QVERIFY(RunPathLease::activePaths().isEmpty());
    }
    void actualWholeAndWaterArrangements_data(){QTest::addColumn<QString>("mode");QTest::newRow("complete-1d")<<QString("complete-1d");QTest::newRow("complete-2d")<<QString("complete");QTest::newRow("split-water")<<QString("split_water");}
    void actualWholeAndWaterArrangements(){
        QFETCH(QString,mode);auto r=request(mode=="complete-1d"?"complete":mode);const auto engine=qEnvironmentVariable("SWMMVIS_TEST_ENGINE_ROOT");
        if(r.program.isEmpty()||r.components.isEmpty()||engine.isEmpty())QSKIP("Actual Composer locations were not supplied.");
        QString binary,mesh;
        if(mode=="complete-1d"){
            // A commented section must not cause a 1D run to expect an HDF5 result.
            write(r.model,minimal+"\n; [2D_VERTICES]\n");binary=work_+"/native.out";
            QProcess cli;cli.start(engine+"/build/component-resume/src/cli/openswmm",{r.model,work_+"/native.rpt",binary});QVERIFY(cli.waitForFinished(10000));QCOMPARE(cli.exitCode(),0);
        }else{
            const auto ref=engine+"/tests/verification/component_split_2026-11-03/DrainingSurface/";write(r.model,read(ref+"model.inp"));binary=ref+"native.out";mesh=ref+"native.h5";
        }
        // Complete-mode mesh ownership must be discovered from metadata, not this hint.
        r.surface=mode=="split_water";ComposerSimulationRunner run(1,mode,r);QSignalSpy done(&run,&SimulationRunner::finished);run.start();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);
        QVERIFY2(done[0][1].toBool(),qPrintable(done[0][3].toString()));QCOMPARE(read(r.binary()),read(binary));QCOMPARE(run.request().mesh().isEmpty(),mode=="complete-1d");
        if(!mesh.isEmpty()){QProcess diff;diff.start("h5diff",{mesh,run.request().mesh()});QVERIFY(diff.waitForFinished(10000));QCOMPARE(diff.exitCode(),0);}
    }
    void cancelActualComposerNeverPublishesSuccess(){
        auto r=request();const auto reference=qEnvironmentVariable("SWMMVIS_TEST_MATERIAL_REFERENCE");if(r.program.isEmpty()||r.components.isEmpty()||reference.isEmpty())QSKIP("Actual Composer locations were not supplied.");
        auto deck=read(QDir(reference).filePath("mg-positive.inp"));deck.replace("END_TIME 00:00:10","END_TIME 01:00:00");write(r.model,deck);r.surface=true;
        write(work_+"/previous.out","previous results");ComposerSimulationRunner run(1,"cancel",r);QSignalSpy done(&run,&SimulationRunner::finished);
        connect(&run,&SimulationRunner::progressChanged,&run,[&run]{run.cancel();});run.start();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);QVERIFY(!done[0][1].toBool());QCOMPARE(done[0][2].toInt(),0);QCOMPARE(read(work_+"/previous.out"),QByteArray("previous results"));QVERIFY(RunPathLease::activePaths().isEmpty());
    }
    // Review E-12: a 5.x run goes to the external legacy worker, which never
    // reads the pause flag and is killed on Stop, so it must not offer Pause or
    // promise partial results.
    void legacyRunsOfferNeitherPauseNorPartialResults(){
        SimulationRunner modern(1,"modern","a.inp","a.rpt","a.out","6.0.0");
        QVERIFY(modern.supportsPause());QVERIFY(modern.supportsPartialResults());
        SimulationRunner legacy(2,"legacy","a.inp","a.rpt","a.out","5.2.4");
        QVERIFY(!legacy.supportsPause());QVERIFY(!legacy.supportsPartialResults());
    }
};
QTEST_MAIN(ComponentExecutionTest)
#include "test_component_execution.moc"
