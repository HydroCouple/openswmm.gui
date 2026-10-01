#include "project/groundwaterrecipestore.h"
#include <QDir>
#include <QDateTime>
#include <QUuid>
#include <cmath>
namespace {
bool fail(QString *error,const QString &text){if(error)*error=text;return false;}
bool valid(const QJsonObject &recipe,QString *error){
    if(recipe.value("version").toInt(-1)!=1)
        return fail(error,QObject::tr("Unsupported groundwater assignment recipe version."));
    if(recipe.value("refreshPolicy").toString()!=QStringLiteral("snapshot-only"))
        return fail(error,QObject::tr("Unsupported groundwater assignment refresh policy."));
    if(!recipe.value("target").isString() || recipe.value("target").toString().isEmpty()
       || !recipe.value("cells").isArray() || !recipe.value("sourcePath").isString())
        return fail(error,QObject::tr("Groundwater assignment provenance is incomplete."));
    for(const auto &cell:recipe.value("cells").toArray())
        if(!cell.isDouble() || cell.toDouble()<0 || cell.toDouble()!=std::floor(cell.toDouble()))
            return fail(error,QObject::tr("Groundwater assignment has an invalid cell identifier."));
    const auto hashes=recipe.value("sourceSha256");
    if(!hashes.isUndefined()&&!hashes.isObject())
        return fail(error,QObject::tr("Groundwater source fingerprints are invalid."));
    return true;
}
QString path(const QString &value,const QString &base,bool writing){
    if(value.isEmpty()||base.isEmpty())return value;
    return writing?QDir(base).relativeFilePath(value):QDir(base).absoluteFilePath(value);
}
QJsonObject rebase(QJsonObject recipe,const QString &base,bool writing){
    recipe["sourcePath"]=path(recipe.value("sourcePath").toString(),base,writing);
    if(recipe.value("sourceSha256").isObject()){
        QJsonObject mapped;const auto hashes=recipe.value("sourceSha256").toObject();
        for(auto it=hashes.begin();it!=hashes.end();++it)mapped[path(it.key(),base,writing)]=it.value();
        recipe["sourceSha256"]=mapped;
    }
    return recipe;
}
}
GroundwaterRecipeStore::GroundwaterRecipeStore(QObject *parent):QObject(parent){setObjectName("groundwaterAssignmentHistory");}
GroundwaterRecipeStore *GroundwaterRecipeStore::forOwner(QObject *owner,bool create){
    if(!owner)return nullptr;
    auto *store=owner->findChild<GroundwaterRecipeStore*>("groundwaterAssignmentHistory",Qt::FindDirectChildrenOnly);
    return store||!create?store:new GroundwaterRecipeStore(owner);
}
bool GroundwaterRecipeStore::append(const QJsonObject &recipe,QString *error){
    if(!loadError_.isEmpty())return fail(error,loadError_);
    if(!valid(recipe,error))return false;
    QJsonObject event=recipe;
    if(!event.contains("id"))event["id"]=QUuid::createUuid().toString(QUuid::WithoutBraces);
    if(!event.contains("event"))event["event"]="applied";
    event["recordedAt"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    events_.append(event);emit edited();return true;
}
bool GroundwaterRecipeStore::restore(const QJsonValue &payload,const QString &base,QString *error){
    QString reason;QJsonArray decoded;
    if(!payload.isArray())reason=tr("The groundwater assignment history is not an array.");
    else for(const auto &entry:payload.toArray()){
        if(!entry.isObject()){reason=tr("A groundwater assignment record is invalid.");break;}
        if(!valid(entry.toObject(),&reason))break;
        decoded.append(rebase(entry.toObject(),base,false));
    }
    if(!reason.isEmpty()){
        loadError_=tr("Groundwater assignment history could not be restored: %1").arg(reason);
        return fail(error,loadError_);
    }
    events_=decoded;loadError_.clear();return true;
}
bool GroundwaterRecipeStore::serialize(const QString &base,QJsonArray &out,QString *error)const{
    if(!loadError_.isEmpty())return fail(error,loadError_);
    QJsonArray encoded;
    for(const auto &entry:events_){if(!valid(entry.toObject(),error))return false;encoded.append(rebase(entry.toObject(),base,true));}
    out=encoded;return true;
}
