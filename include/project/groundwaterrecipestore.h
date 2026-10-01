#ifndef OPENSWMMVIS_GROUNDWATERRECIPESTORE_H
#define OPENSWMMVIS_GROUNDWATERRECIPESTORE_H
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

// Snapshot provenance, never an instruction to replay forcing automatically.
// Engine rows remain authoritative; events record accepted apply/undo/redo.
class GroundwaterRecipeStore : public QObject {
    Q_OBJECT
public:
    explicit GroundwaterRecipeStore(QObject *parent=nullptr);
    static GroundwaterRecipeStore *forOwner(QObject *,bool create=true);
    const QJsonArray &events() const { return events_; }
    QString loadError() const { return loadError_; }
    bool append(const QJsonObject &,QString *error=nullptr);
    bool restore(const QJsonValue &,const QString &baseDirectory,QString *error=nullptr);
    bool serialize(const QString &baseDirectory,QJsonArray &,QString *error=nullptr) const;
signals:
    void edited();
private:
    QJsonArray events_;
    QString loadError_;
};
#endif
