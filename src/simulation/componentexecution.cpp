/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "simulation/componentexecution.h"
#include "simulation/runpathlease.h"
#include "core/swmmdatetime.h"
#include <openswmm/engine/openswmm_engine.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace openswmmvis::simulation {
namespace {
const QString key = QStringLiteral("org.hydrocouple.openswmm.execution");
bool refuse(QString *error, const QString &message) { if(error)*error=message;return false; }
QByteArray read(const QString &path) { QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error(f.errorString().toStdString());return f.readAll(); }
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString absolute(const QString &path) { return QFileInfo(path).absoluteFilePath(); }
}
ComponentEngineRegistry::ComponentEngineRegistry(QObject *project):QObject(project) {
    setObjectName("componentEngineRegistry");reload();
    if(project->metaObject()->indexOfSignal("extensionStateRestored()")>=0)
        connect(project,SIGNAL(extensionStateRestored()),this,SLOT(reload()));
}
ComponentEngineRegistry *ComponentEngineRegistry::forProject(QObject *project) {
    auto *model=project->findChild<ComponentEngineRegistry *>("componentEngineRegistry",Qt::FindDirectChildrenOnly);
    return model?model:new ComponentEngineRegistry(project);
}
void ComponentEngineRegistry::reload() {
    const auto extensions=parent()->property("extensionData").toJsonObject();
    const auto saved=extensions.value(key);
    settings_=saved.isUndefined()?QJsonObject{{"schema","openswmm.execution/1"},{"mode","native"},
        {"capacity",4096},{"network_id","network-"+uuid()},{"surface_id","surface-"+uuid()}}:saved.toObject();
    validate(settings_,&error_);
}
bool ComponentEngineRegistry::validate(const QJsonObject &s,QString *error) {
    if(error)error->clear();
    const QSet<QString> keys={"schema","mode","capacity","network_id","surface_id"};
    for(auto i=s.begin();i!=s.end();++i)if(!keys.contains(i.key()))return refuse(error,tr("Unsupported execution setting: %1").arg(i.key()));
    if(s.value("schema")!="openswmm.execution/1")return refuse(error,tr("Unsupported component execution settings."));
    if(!QStringList{"native","complete","split_water","split_material"}.contains(s.value("mode").toString()))return refuse(error,tr("Choose a supported execution arrangement."));
    const double n=s.value("capacity").toDouble(-1);
    if(!s.value("capacity").isDouble()||n<1||n>65536||n!=std::floor(n))return refuse(error,tr("Material capacity must be an integer from 1 to 65536."));
    const auto a=s.value("network_id").toString(),b=s.value("surface_id").toString();
    if(a.isEmpty()||b.isEmpty()||a==b||a.size()>256||b.size()>256)return refuse(error,tr("Component identities must be distinct and nonempty."));
    return true;
}
bool ComponentEngineRegistry::setSettings(const QJsonObject &settings,QString *error) {
    if(!validate(settings,error))return false;
    if(settings_==settings&&error_.isEmpty())return true;
    settings_=settings;error_.clear();auto data=parent()->property("extensionData").toJsonObject();data[key]=settings_;
    parent()->setProperty("extensionData",data);emit settingsChanged();return true;
}
QString ComponentRunRequest::networkDirectory()const{return QDir(directory).filePath(settings.value("mode")=="complete"?"model":"network");}
QString ComponentRunRequest::report()const{return QDir(networkDirectory()).filePath("model.rpt");}
QString ComponentRunRequest::binary()const{return QDir(networkDirectory()).filePath("model.out");}
QString ComponentRunRequest::mesh()const{return surface?QDir(settings.value("mode")=="complete"?networkDirectory():QDir(directory).filePath("surface")).filePath("model.2d.h5"):QString();}
QString ComponentRunRequest::document()const{return QDir(directory).filePath("composition.json");}
QString ComponentRunRequest::manifest()const{return QDir(directory).filePath("run-manifest.json");}
QJsonObject ComponentRunRequest::composition()const {
    const auto mode=settings.value("mode").toString();const bool split=mode!="complete",material=mode=="split_material";
    const auto n=settings.value("network_id").toString(),s=settings.value("surface_id").toString();
    auto component=[&](const QString &id,const QString &role,const QString &output,bool source){
        QJsonObject cfg{{"schema","openswmm.component/1"},{"role",role},{"model",absolute(model)},{"output_directory",absolute(output)}};
        if(material)cfg["material_capacity"]=settings.value("capacity");
        if(source)cfg["surface_results"]=true;
        return QJsonObject{{"id",id},{"info",QJsonObject{{"component_info_id","org.hydrocouple.openswmm.engine"}}},
            {"arguments",QJsonObject{{"configuration",cfg}}}};
    };
    QJsonArray items{component(n,split?(material?"split_material_network":"split_network"):"complete_model",networkDirectory(),false)};
    QJsonArray links;
    if(split){items.append(component(s,material?"split_material_surface":"integrated2d",QDir(directory).filePath("surface"),true));
        const QString port=material?"openswmm.node-surface-material.v10":"openswmm.node-surface-water.v1";
        links.append(QJsonObject{{"from",QJsonObject{{"component",s},{"output",port}}},{"to",QJsonObject{{"component",n},{"input",port}}}});}
    return {{"schema_version","1.1"},{"components",items},{"connections",links},
        {"workflow",QJsonObject{{"id","workflow"},{"strategy","time_stepped"}}},{"run",QJsonObject{{"manifest",absolute(manifest())}}}};
}
ComposerSimulationRunner::ComposerSimulationRunner(int job,const QString &name,ComponentRunRequest request,QObject *parent)
    :SimulationRunner(job,name,request.model,request.report(),request.binary(),"6.0.0",parent),request_(std::move(request)) {
    connect(&process_,&QProcess::readyReadStandardOutput,this,&ComposerSimulationRunner::consume);
    connect(&process_,&QProcess::readyReadStandardError,this,[this]{
        retainLog(process_.readAllStandardError());
    });
    connect(&process_,&QProcess::finished,this,&ComposerSimulationRunner::complete);
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart)publish(false,process_.errorString());});
}
ComposerSimulationRunner::~ComposerSimulationRunner(){
    disconnect(&process_,nullptr,this,nullptr);
    if(process_.state()!=QProcess::NotRunning){process_.kill();process_.waitForFinished(-1);}
}
QStringList ComposerSimulationRunner::outputPaths()const{return {request_.directory};}
void ComposerSimulationRunner::start(){if(launched_)return;launched_=true;QTimer::singleShot(0,this,&ComposerSimulationRunner::begin);}
void ComposerSimulationRunner::begin(){
    QPointer<ComposerSimulationRunner> self(this);emit started(jobId());if(!self)return;
    try {
        if(cancelled_)throw std::runtime_error("Component run cancelled before launch.");
        QString error;if(!ComponentEngineRegistry::validate(request_.settings,&error)||request_.settings.value("mode")=="native")throw std::runtime_error(error.isEmpty()?"Choose a component arrangement.":error.toStdString());
        if(!QFileInfo(request_.program).isExecutable()||!QFileInfo(request_.components).isDir())throw std::runtime_error("Choose the Composer application and component library folder in Component execution settings.");
        if(!QFileInfo(request_.model).isFile()||!QDir::isAbsolutePath(request_.directory))throw std::runtime_error("Component run requires a saved model and an absolute fresh run directory.");
        lease_=std::make_unique<RunPathLease>(this);
        for(const auto &input:runInputPaths(request_.model)) {
            if(RunPathLease::overlaps(input,request_.directory)||!lease_->add(input,false,&error))throw std::runtime_error(error.isEmpty()?"Run directory overlaps an input.":error.toStdString());
        }
        if(!lease_->add(request_.directory,true,&error))throw std::runtime_error(error.toStdString());
        std::unique_ptr<void,decltype(&swmm_engine_destroy)> metadata(swmm_engine_create(),swmm_engine_destroy);
        // Only the start and end times are read here: the period open leaves the 2D mesh unread.
        if(!metadata||swmm_engine_open_simulation_period(metadata.get(),request_.model.toUtf8().constData())!=SWMM_OK)throw std::runtime_error("Cannot inspect the saved model for component execution.");
        double first=0,last=0;
        if(swmm_get_start_time(metadata.get(),&first)||swmm_get_end_time(metadata.get(),&last)||!std::isfinite(first)||!std::isfinite(last)||last<=first)throw std::runtime_error("Model simulation period is unavailable.");
        epoch_=first+2415018.5;
        const double duration=std::round((last-first)*86400000.)/1000.;
        end_=epoch_+duration/86400.;current_=epoch_;
        startDate_=core::swmmDateTimeToMilliseconds(first);endDate_=core::swmmDateTimeToMilliseconds(last);
        if(duration<=0||!std::isfinite(end_)||!startDate_.isValid()||!endDate_.isValid())throw std::runtime_error("Model simulation period is not supported by the component clock.");
        emit simulationDatesKnown(jobId(),startDate_,endDate_);if(!self)return;
        if(cancelled_)throw std::runtime_error("Component run cancelled before launch.");
        const auto nativeDirectory=std::filesystem::path(
#ifdef Q_OS_WIN
            request_.directory.toStdWString()
#else
            request_.directory.toStdString()
#endif
        );
        if(!QDir().mkpath(QFileInfo(request_.directory).absolutePath())||!std::filesystem::create_directory(nativeDirectory))throw std::runtime_error("Choose a fresh component run directory.");
        const auto graph=request_.composition();const auto bytes=QJsonDocument(graph).toJson();digest_=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();
        QSaveFile document(request_.document());if(!document.open(QIODevice::WriteOnly)||document.write(bytes)!=bytes.size()||!document.commit())throw std::runtime_error("Cannot save the component composition.");
        for(const auto &value:graph.value("components").toArray())ids_.insert(value.toObject().value("id").toString());
        log_.setFileName(QDir(request_.directory).filePath("composer.log"));if(!log_.open(QIODevice::WriteOnly|QIODevice::NewOnly))throw std::runtime_error("Cannot create component run log.");
        auto env=QProcessEnvironment::systemEnvironment();env.insert("OPENSWMM_2D_BACKEND","cpu");process_.setProcessEnvironment(env);
        process_.setWorkingDirectory(request_.directory);process_.setProgram(request_.program);
        process_.setArguments({"--run",request_.document(),"--components",request_.components,"--json-progress"});process_.start();
    }catch(const std::exception &e){publish(false,QString::fromUtf8(e.what()));}
}
void ComposerSimulationRunner::setPaused(bool){emit warningReceived(jobId(),0,tr("Composer does not support pausing this run."));}
void ComposerSimulationRunner::cancel(){
    if(published_)return;cancelled_=true;
    if(process_.state()!=QProcess::NotRunning){process_.terminate();QTimer::singleShot(1500,this,[this]{if(process_.state()!=QProcess::NotRunning)process_.kill();});}
    else QTimer::singleShot(0,this,[this]{if(cancelled_&&!published_&&process_.state()==QProcess::NotRunning)publish(false,tr("Component run cancelled before launch."));});
}
void ComposerSimulationRunner::stopWithError(const QString &error){if(failure_.isEmpty())failure_=error;if(process_.state()!=QProcess::NotRunning)process_.kill();}
void ComposerSimulationRunner::retainLog(const QByteArray &bytes){
    constexpr qint64 limit=16*1024*1024;
    if(logged_<limit)log_.write(bytes.constData(),qMin<qint64>(bytes.size(),limit-logged_));
    if(logged_<limit&&bytes.size()>limit-logged_)log_.write("\n[Further log text omitted; event validation continues.]\n");
    logged_=qMin<qint64>(limit,logged_+bytes.size());
}
void ComposerSimulationRunner::consume(){
    const auto bytes=process_.readAllStandardOutput();retainLog(bytes);pending_+=bytes;
    QPointer<ComposerSimulationRunner> self(this);
    while(pending_.contains('\n')){const auto n=pending_.indexOf('\n');const auto row=pending_.left(n);pending_.remove(0,n+1);
        if(row.size()>1024*1024){stopWithError(tr("Composer event exceeded its size limit."));return;}
        line(row);if(!self||!failure_.isEmpty())return;}
    if(pending_.size()>1024*1024)stopWithError(tr("Composer event exceeded its size limit."));
}
void ComposerSimulationRunner::line(const QByteArray &bytes){
    if(!bytes.startsWith("HYDROCOUPLE_EVENT "))return;
    try {
        QJsonParseError parse;const auto doc=QJsonDocument::fromJson(bytes.mid(18),&parse);
        if(parse.error!=QJsonParseError::NoError||!doc.isObject())throw std::runtime_error("Malformed Composer event.");
        const auto e=doc.object();const auto seq=e.value("sequence").toDouble(-1);const auto run=e.value("run_id").toString();const auto kind=e.value("event").toString();
        if(e.value("schema")!="hydrocouple.run-events/1"||!e.value("sequence").isDouble()||seq!=sequence_+1||seq>9007199254740991.||QUuid(run).isNull()||final_)throw std::runtime_error("Invalid Composer event identity or sequence.");
        if(sequence_<0){if(kind!="started"||absolute(e.value("composition").toString())!=absolute(request_.document())||e.value("composition_sha256").toString().toLatin1()!=digest_)throw std::runtime_error("Composer started a different composition.");runId_=run;}
        else if(run!=runId_||kind=="started")throw std::runtime_error("Composer run identity changed.");
        sequence_=static_cast<qint64>(seq);
        if(kind=="progress"){
            const auto items=e.value("components").toArray();QSet<QString> seen;double time=0;
            for(const auto &v:items){const auto c=v.toObject();const auto id=c.value("id").toString();const double t=c.value("current_julian_day").toDouble(qQNaN());
                if(!ids_.contains(id)||seen.contains(id)||!std::isfinite(t)||t<current_||t<epoch_||t>end_||(!seen.isEmpty()&&t!=time))throw std::runtime_error("Composer component clocks or identities disagree.");seen.insert(id);time=t;}
            if(seen!=ids_)throw std::runtime_error("Composer progress omits a component.");current_=time;
            if(!progressTimer_.isValid()||progressTimer_.elapsed()>=100||time==end_){
                progressTimer_.start();const double fraction=qBound(0.,(time-epoch_)/(end_-epoch_),1.);
                emit progressChanged(jobId(),fraction,startDate_.addMSecs(std::llround((time-epoch_)*86400000.)),qQNaN(),qQNaN(),qQNaN(),qQNaN());}
        }else if(kind=="initializing"){
            if(!ids_.contains(e.value("component").toString()))throw std::runtime_error("Composer initialized an unknown component.");
        }else if(kind=="finished"){
            if(!e.value("succeeded").isBool())throw std::runtime_error("Composer final event has no outcome.");
            final_=true;succeeded_=e.value("succeeded").toBool();finalManifest_=e.value("manifest").toString();
            if(!succeeded_)failure_=e.value("message").toString()+" "+QString::fromUtf8(QJsonDocument(e.value("diagnostics").toArray()).toJson(QJsonDocument::Compact));
        }else if(kind!="started")throw std::runtime_error("Unsupported Composer event.");
    }catch(const std::exception &e){stopWithError(QString::fromUtf8(e.what()));}
}
bool ComposerSimulationRunner::verifyResults(QString *error) {
    if(absolute(finalManifest_)!=absolute(request_.manifest()))return refuse(error,tr("Composer reported a different run manifest."));
    const auto root=QDir::cleanPath(QFileInfo(request_.directory).canonicalFilePath())+'/';
    const auto inside=[&root](const QString &path){
        return QFileInfo(path).canonicalFilePath().startsWith(root,
#ifdef Q_OS_WIN
            Qt::CaseInsensitive
#else
            Qt::CaseSensitive
#endif
        );
    };
    const auto metadataPath=QDir(request_.networkDirectory()).filePath("metadata.json");
    if(!inside(metadataPath))return refuse(error,tr("Component metadata is outside its run directory."));
    QJsonParseError metadataError;const auto metadata=QJsonDocument::fromJson(read(metadataPath),&metadataError).object();
    if(metadataError.error!=QJsonParseError::NoError||metadata.value("schema")!="openswmm.component-metadata/1"||metadata.value("instance_id")!=request_.settings.value("network_id"))return refuse(error,tr("Component metadata identity does not match the run."));
    const double epoch=metadata.value("epoch_julian_day").toDouble(qQNaN()),duration=metadata.value("duration_s").toDouble(qQNaN());
    if(!std::isfinite(epoch)||!std::isfinite(duration)||duration<=0||epoch!=epoch_||current_!=epoch+duration/86400.)return refuse(error,tr("Component run did not reach its declared synchronized endpoint."));
    if(request_.settings.value("mode")=="complete"){
        const auto surface=metadata.value("result_files").toObject().value("surface");
        if(!surface.isUndefined()&&surface!="model.2d.h5")return refuse(error,tr("Unsupported component surface result declaration."));
        request_.surface=!surface.isUndefined();
    }else request_.surface=true;
    QStringList files{request_.manifest(),metadataPath,request_.binary(),request_.report()};if(!request_.mesh().isEmpty())files<<request_.mesh();
    for(const auto &f:files){const QFileInfo info(f);if(!info.isFile()||info.size()==0||!inside(f))return refuse(error,tr("Component result is missing or outside its run directory: %1").arg(f));}
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(read(request_.manifest()),&parse);const auto manifest=doc.object();
    if(parse.error!=QJsonParseError::NoError||manifest.value("run").toObject().value("status")!="completed"||
        manifest.value("composition").toObject().value("embedded").toObject()!=request_.composition()||
        absolute(manifest.value("composition").toObject().value("path").toString())!=absolute(request_.document()))return refuse(error,tr("Component run manifest does not match the launched composition."));
    QSet<QString> seen;for(const auto &v:manifest.value("components").toArray()){const auto c=v.toObject();const auto id=c.value("id").toString();if(!ids_.contains(id)||seen.contains(id)||c.value("final_status")!="Finished")return refuse(error,tr("Component final status is incomplete."));seen.insert(id);}
    return seen==ids_||refuse(error,tr("Run manifest omits a component."));
}
void ComposerSimulationRunner::complete(int code,QProcess::ExitStatus status){
    QPointer<ComposerSimulationRunner> self(this);consume();if(!self)return;
    retainLog(process_.readAllStandardError());
    if(!pending_.isEmpty()&&failure_.isEmpty())failure_=tr("Composer output ended with an incomplete line.");
    QString error;
    try{if(!cancelled_&&failure_.isEmpty()&&status==QProcess::NormalExit&&code==0&&final_&&succeeded_&&verifyResults(&error)){publish(true,{});return;}}
    catch(const std::exception &e){error=QString::fromUtf8(e.what());}
    publish(false,cancelled_?tr("Component run cancelled. Interrupted files were retained without loading them."):
        !failure_.isEmpty()?failure_:!error.isEmpty()?error:tr("Composer exited without a verified successful result."));
}
void ComposerSimulationRunner::publish(bool ok,const QString &message){
    if(published_)return;published_=true;log_.flush();log_.close();lease_.reset();
    // The controller records cancellation; supportsPartialResults prevents residue adoption.
    emit finished(jobId(),ok,(ok||cancelled_)?0:SWMM_ERR_PLUGIN,message,qQNaN(),qQNaN(),qQNaN());
}
} // namespace openswmmvis::simulation
