#ifndef OPENSWMMVIS_PROFILESECTIONSTORE_H
#define OPENSWMMVIS_PROFILESECTIONSTORE_H
#include "plot/profilesectionseries.h"
#include <QJsonArray>
#include <QObject>

// Project-owned saved definitions. Dialog drafts remain outside this store
// until Save Section; failed restoration retains its payload and blocks loss.
class ProfileSectionStore : public QObject {
    Q_OBJECT
public:
    explicit ProfileSectionStore(QObject *parent=nullptr);
    const QVector<ProfileSection::Definition>& definitions() const { return definitions_; }
    QString loadError() const { return loadError_; }
    bool saveDefinition(const ProfileSection::Definition&, QString *error=nullptr);
    bool removeDefinition(const QString &id);
    bool restore(const QJsonValue&, const QString &baseDirectory, QString *error=nullptr);
    bool serialize(const QString &baseDirectory, QJsonArray&, QString *error=nullptr) const;
    static ProfileSectionStore *forOwner(QObject *owner, bool create=true);
signals:
    void edited();
    void definitionsChanged();
private:
    QVector<ProfileSection::Definition> definitions_;
    QString loadError_;
    QJsonValue rejectedPayload_;
};
#endif
