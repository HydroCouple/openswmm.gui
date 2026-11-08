/* SPDX-License-Identifier: GPL-3.0-or-later */
// Uses the existing native activity test friendship to exercise the shipping run controller.
#include "swmmvis.h"
#include "swmmvisprojectwindow.h"
#include "simulation/componentexecution.h"
#include "simulation/runpathlease.h"
#include "simulation/simulationstatusmodel.h"
#include "plugins/extensionstateregistry.h"
#include "project/projectserializer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "layers/swmm2dresultslayer.h"
#include "map/mapcanvas.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "ui/dialogs/componentexecutiondialog.h"
#include <QtTest>
#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QMdiArea>
#include <QPushButton>
#include <QQmlEngine>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStandardItemModel>
#include <QTimer>
using namespace openswmmvis;
using namespace openswmmvis::simulation;
using namespace OpenSWMM::Sdk;
class TestExtensionActivity:public QObject {
    Q_OBJECT
    std::unique_ptr<SWMMVis> app_;
    QPointer<SWMMVisProjectWindow> project_;
    ExtensionStateRegistry *registry_=nullptr;
    QString root_,work_;
    QByteArray read(const QString &p){QFile f(p);if(!f.open(QIODevice::ReadOnly))qFatal("Cannot read window fixture");return f.readAll();}
    bool open(){
        const auto reference=qEnvironmentVariable("SWMMVIS_TEST_MATERIAL_REFERENCE");if(reference.isEmpty())return false;
        QFile f(work_+"/model.inp");if(!f.open(QIODevice::WriteOnly))return false;f.write(read(reference+"/mg-positive.inp"));f.close();
        app_=std::make_unique<SWMMVis>();project_=app_->createProjectWindow(f.fileName());QList<QString>warnings,errors;
        if(!project_||!project_->loadModel(warnings,errors))return false;project_->setHasChanges(false);
        app_->findChild<QMdiArea *>("mdiAreaCentral")->setActiveSubWindow(project_);QCoreApplication::processEvents();
        return registry_->session(project_->extensionSessionId()).status==SessionStatus::Ready;
    }
    void configure(){
        bool applied=false;QObject callbackContext;QTimer::singleShot(0,&callbackContext,[&]{
            auto *dialog=qobject_cast<ComponentExecutionDialog *>(QApplication::activeModalWidget());if(!dialog)return;
            auto *mode=dialog->findChild<QComboBox *>("executionArrangement");mode->setCurrentIndex(mode->findData("split_material"));
            dialog->findChild<QSpinBox *>("materialCapacity")->setValue(6);dialog->grab().save(work_+"/settings-dialog.png");
            applied=true;dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        auto *action=app_->findChild<QAction *>("actionComponentExecution");QVERIFY(action);QVERIFY(action->isEnabled());action->trigger();QVERIFY(applied);QVERIFY(project_->hasChanges());
    }
    QUuid start(){QString error;const auto session=project_->extensionSessionId();auto id=app_->startRunAttemptForSession(session,registry_->session(session).loadGeneration,&error);if(id.isNull())qWarning()<<error;return id;}
    bool terminal(const QUuid &id){return registry_->operation(id).isTerminal();}
private slots:
    void initTestCase(){
        QStandardPaths::setTestModeEnabled(true);qputenv("QT_QUICK_BACKEND","software");qunsetenv("SWMMVIS_OPEN_ON_STARTUP");qunsetenv("SWMMVIS_GUI_EXTENSION_PATH");
        QCoreApplication::setOrganizationName("OpenSWMMComponentWindowTest");QCoreApplication::setApplicationName("ComponentWindow");
        root_=qEnvironmentVariable("SWMMVIS_COMPONENT_WINDOW_EVIDENCE",QDir::currentPath()+"/output_component_window");QDir().mkpath(root_);
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root_+"/settings");QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,root_+"/settings");
        qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM",1,0,"SWMMLayerQSGRenderer");qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM",1,0,"SWMM2DMeshQSGRenderer");qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM",1,0,"SWMM2DResultsQSGRenderer");
        registry_=ExtensionStateRegistry::instance();QVERIFY(registry_);
        QVERIFY(!qEnvironmentVariable("SWMMVIS_TEST_COMPOSER").isEmpty());QSettings().setValue("SWMMVis/Components/ComposerProgram",qEnvironmentVariable("SWMMVIS_TEST_COMPOSER"));QSettings().setValue("SWMMVis/Components/LibraryDirectory",qEnvironmentVariable("SWMMVIS_TEST_COMPONENTS"));
    }
    void init(){work_=root_+"/"+QString::fromLatin1(QTest::currentTestFunction());QDir(work_).removeRecursively();QDir().mkpath(work_);}
    void cleanup(){
        if(app_){const auto runs=app_->mActiveRunners;for(auto *r:runs)r->cancel();QTRY_VERIFY_WITH_TIMEOUT(app_->mActiveRunners.isEmpty(),20000);if(project_){project_->setHasChanges(false);delete project_.data();}app_.reset();}
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QCoreApplication::processEvents();QVERIFY(RunPathLease::activePaths().isEmpty());
    }
    void projectSaveRunInspectAndReopen(){
        QVERIFY(open());QJsonObject extensions=project_->property("extensionData").toJsonObject();extensions["other-extension"]=QJsonObject{{"keep",42}};project_->setProperty("extensionData",extensions);
        configure();auto *model=ComponentEngineRegistry::forProject(project_);const auto configured=model->settings();const auto id=start();QVERIFY(!id.isNull());QTRY_VERIFY_WITH_TIMEOUT(terminal(id),30000);QCOMPARE(registry_->operation(id).phase,ProcessPhase::Succeeded);
        QTRY_VERIFY_WITH_TIMEOUT(project_->activeResultsLayer()!=nullptr,10000);QTRY_VERIFY_WITH_TIMEOUT(project_->active2DResultsLayer()!=nullptr,10000);
        auto *one=project_->activeResultsLayer();auto *two=project_->active2DResultsLayer();QVERIFY(one->totalTimeSteps()>0);QVERIFY(!one->speciesCatalog().isEmpty());QVERIFY(dynamic_cast<HDF5Mesh2DSource *>(two->source()));
        QVERIFY(!app_->findChild<QAction *>("actionPauseExecution")->isEnabled());app_->grab().save(work_+"/completed-window.png");
        const auto sidecar=work_+"/saved.oswp";QString error;QVERIFY2(ProjectSerializer::saveToFile(sidecar,project_,&error),qPrintable(error));
        const auto saved=QJsonDocument::fromJson(read(sidecar)).object().value("sessions").toArray()[0].toObject().value("extensions").toObject();QCOMPARE(saved.value("other-extension").toObject().value("keep").toInt(),42);QCOMPARE(saved.value("org.hydrocouple.openswmm.execution").toObject(),configured);
        project_->setHasChanges(false);project_->close();QTRY_VERIFY_WITH_TIMEOUT(project_.isNull(),10000);app_->openProjectFile(sidecar);
        QTRY_VERIFY_WITH_TIMEOUT(app_->activeProjectWindow()!=nullptr,10000);project_=app_->activeProjectWindow();
        QTRY_VERIFY_WITH_TIMEOUT(registry_->session(project_->extensionSessionId()).status==SessionStatus::Ready,10000);QCOMPARE(ComponentEngineRegistry::forProject(project_)->settings(),configured);
        QTRY_VERIFY_WITH_TIMEOUT(project_->activeResultsLayer()!=nullptr,10000);QTRY_VERIFY_WITH_TIMEOUT(project_->active2DResultsLayer()!=nullptr,10000);
    }
    void failureAndCancellationRetainPreviousLayers(){
        QVERIFY(open());configure();auto first=start();QVERIFY(!first.isNull());QTRY_VERIFY_WITH_TIMEOUT(terminal(first),30000);QCOMPARE(registry_->operation(first).phase,ProcessPhase::Succeeded);
        QPointer<SWMMResultsLayer> one=project_->activeResultsLayer();QPointer<SWMM2DResultsLayer> two=project_->active2DResultsLayer();QVERIFY(one&&two);const auto bytes=read(one->resultsFilePath());
        const auto program=QSettings().value("SWMMVis/Components/ComposerProgram");QSettings().setValue("SWMMVis/Components/ComposerProgram",work_+"/missing");auto failed=start();QVERIFY(!failed.isNull());QTRY_VERIFY_WITH_TIMEOUT(terminal(failed),10000);QCOMPARE(registry_->operation(failed).phase,ProcessPhase::Failed);QCOMPARE(project_->activeResultsLayer(),one.data());QCOMPARE(project_->active2DResultsLayer(),two.data());QCOMPARE(read(one->resultsFilePath()),bytes);
        QSettings().setValue("SWMMVis/Components/ComposerProgram",program);auto cancelled=start();QVERIFY(!cancelled.isNull());ComposerSimulationRunner *runner=nullptr;for(auto *candidate:app_->mActiveRunners)if(candidate->property("extensionRunAttemptId").toUuid()==cancelled)runner=qobject_cast<ComposerSimulationRunner *>(candidate);QVERIFY(runner);connect(runner,&SimulationRunner::progressChanged,runner,[runner]{runner->cancel();});QTRY_VERIFY_WITH_TIMEOUT(terminal(cancelled),30000);QCOMPARE(registry_->operation(cancelled).phase,ProcessPhase::Cancelled);QCOMPARE(project_->activeResultsLayer(),one.data());QCOMPARE(project_->active2DResultsLayer(),two.data());
    }
    void terminalObserverReloadCannotAttachOldResults(){
        QVERIFY(open());configure();const auto session=project_->extensionSessionId();const auto generation=registry_->session(session).loadGeneration;bool reloaded=false,loaded=false;
        QObject callbackContext;
        auto hook=connect(app_->mSimStatusModel,&SimulationStatusModel::dataChanged,&callbackContext,[&](const QModelIndex &first,const QModelIndex &last){
            if(reloaded||first.parent().isValid())return;
            for(int row=first.row();row<=last.row();++row){const auto *record=app_->mSimStatusModel->jobRecord(app_->mSimStatusModel->jobIdForRow(row));if(record&&record->status==SimulationJobStatus::Success){reloaded=true;QList<QString>w,e;loaded=project_->loadModel(w,e);break;}}
        });
        auto id=start();QVERIFY(!id.isNull());QTRY_VERIFY_WITH_TIMEOUT(reloaded,30000);QVERIFY(loaded);QVERIFY(registry_->session(session).loadGeneration>generation);QVERIFY(!project_->activeResultsLayer());QVERIFY(!project_->active2DResultsLayer());disconnect(hook);
    }
    void hdfNotificationReloadCannotLeaveAnOldLayer_data(){QTest::addColumn<bool>("duringInsert");QTest::newRow("time-anchor-log")<<false;QTest::newRow("layer-added")<<true;}
    void hdfNotificationReloadCannotLeaveAnOldLayer(){
        QFETCH(bool,duringInsert);QVERIFY(open());configure();bool reloaded=false,loaded=false;
        auto reload=[&]{if(reloaded)return;reloaded=true;QList<QString>w,e;loaded=project_->loadModel(w,e);};
        QMetaObject::Connection hook;QObject callbackContext;
        if(duringInsert)hook=connect(project_->canvas(),&MapCanvas::layerAdded,&callbackContext,[&](OpenSWMMVisLayer *layer){if(qobject_cast<SWMM2DResultsLayer *>(layer))reload();});
        else hook=connect(app_->mLogMessagesModel,&QStandardItemModel::rowsInserted,&callbackContext,[&](const QModelIndex &parent,int first,int last){for(int row=first;row<=last&&!reloaded;++row)for(int column=0;column<app_->mLogMessagesModel->columnCount(parent);++column)if(app_->mLogMessagesModel->index(row,column,parent).data().toString().contains("2D layer time anchor")){reload();break;}});
        const auto id=start();QVERIFY(!id.isNull());QTRY_VERIFY_WITH_TIMEOUT(reloaded,30000);QVERIFY(loaded);QVERIFY(!project_->active2DResultsLayer());for(auto *layer:project_->canvas()->layers())QVERIFY(!qobject_cast<SWMM2DResultsLayer *>(layer));disconnect(hook);
    }
    void runningObserverCanCloseMainWindow(){
        QVERIFY(open());configure();bool closed=false;
        auto id=start();QVERIFY(!id.isNull());ComposerSimulationRunner *runner=nullptr;for(auto *candidate:app_->mActiveRunners)if(candidate->property("extensionRunAttemptId").toUuid()==id)runner=qobject_cast<ComposerSimulationRunner *>(candidate);QVERIFY(runner);
        QObject callbackContext;auto hook=connect(runner,&SimulationRunner::started,&callbackContext,[&]{closed=true;auto *window=app_.release();delete window;});
        QTRY_VERIFY_WITH_TIMEOUT(closed,10000);QVERIFY(!app_);QVERIFY(!project_);disconnect(hook);
    }
};
QTEST_MAIN(TestExtensionActivity)
#include "test_component_execution_window.moc"
