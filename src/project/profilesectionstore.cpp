#include "project/profilesectionstore.h"
#include <QSet>

ProfileSectionStore::ProfileSectionStore(QObject *parent):QObject(parent)
{ setObjectName(QStringLiteral("savedProfileSections")); }
ProfileSectionStore *ProfileSectionStore::forOwner(QObject *owner,bool create)
{
    if(!owner)return nullptr;
    auto *store=owner->findChild<ProfileSectionStore*>(QStringLiteral("savedProfileSections"),Qt::FindDirectChildrenOnly);
    return store || !create ? store : new ProfileSectionStore(owner);
}
bool ProfileSectionStore::saveDefinition(const ProfileSection::Definition &definition,QString *error)
{
    if(!loadError_.isEmpty()) { if(error)*error=loadError_; return false; }
    if(!ProfileSection::validateDefinition(definition,error))return false;
    for(auto &existing:definitions_)if(existing.id==definition.id){
        if(ProfileSection::definitionToJson(existing,{})==ProfileSection::definitionToJson(definition,{}))return true;
        existing=definition; emit definitionsChanged(); emit edited(); return true;
    }
    if (definitions_.size() >= 1000) {
        if (error) *error = tr("A project can contain at most 1,000 saved sections.");
        return false;
    }
    definitions_.append(definition); emit definitionsChanged(); emit edited(); return true;
}
bool ProfileSectionStore::removeDefinition(const QString &id)
{
    if(!loadError_.isEmpty())return false;
    for(qsizetype i=0;i<definitions_.size();++i)if(definitions_[i].id==id){
        definitions_.removeAt(i);emit definitionsChanged();emit edited();return true;
    }
    return false;
}
bool ProfileSectionStore::restore(const QJsonValue &payload,const QString &baseDirectory,QString *error)
{
    QVector<ProfileSection::Definition> decoded;QSet<QString> ids;QString reason;
    const QJsonArray array=payload.toArray();
    if(!payload.isArray())reason=tr("The saved section list is not an array.");
    if(array.size()>1000)reason=tr("The saved section list exceeds 1,000 entries.");
    for(const auto &entry:array){
        if(!reason.isEmpty())break;
        ProfileSection::Definition definition;
        if(!entry.isObject())reason=tr("A saved section is not an object.");
        else if(!ProfileSection::definitionFromJson(entry.toObject(),baseDirectory,definition,&reason)){
            if(reason.isEmpty())reason=tr("A saved section is invalid.");
        }else if(ids.contains(definition.id))reason=tr("Saved sections contain duplicate identifiers.");
        else {ids.insert(definition.id);decoded.append(definition);}
    }
    if(!reason.isEmpty()){
        rejectedPayload_=payload;loadError_=tr("Saved sections could not be restored: %1").arg(reason);
        if(error)*error=loadError_;return false;
    }
    definitions_=std::move(decoded);rejectedPayload_={};loadError_.clear();emit definitionsChanged();return true;
}
bool ProfileSectionStore::serialize(const QString &baseDirectory,QJsonArray &array,QString *error) const
{
    if(!loadError_.isEmpty()){if(error)*error=loadError_;return false;}
    QJsonArray encoded;
    for(const auto &definition:definitions_){
        if(!ProfileSection::validateDefinition(definition,error))return false;
        encoded.append(ProfileSection::definitionToJson(definition,baseDirectory));
    }
    array=encoded;return true;
}
