/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHECKPOINTMODEL_H
#define CHECKPOINTMODEL_H
#include "simulation/checkpointworker.h"
#include <QAbstractTableModel>

struct CheckpointEntry {
    QString id, name, path, lineage, revision;
    quint64 publication = 0;
    double acceptedDate = qQNaN(), acceptedSeconds = qQNaN();
    bool pinned = false, compatible = false;
    QString inspection = QStringLiteral("Not checked"), recipe;
    QJsonObject metadata;
};
struct CheckpointFile {
    QString role,path,requiredBytes,currentBytes;
    bool readable = false;
};
struct CheckpointStorage {
    quint64 bytes = 0;
    bool complete = true;
    QStringList paths;
};

// One GUI-thread model/controller per project; views own no native objects.
class CheckpointModel:public QAbstractTableModel {
    Q_OBJECT
public:
    explicit CheckpointModel(const QString& catalog,QObject* parent=nullptr);
    int rowCount(const QModelIndex& parent={})const override;
    int columnCount(const QModelIndex& parent={})const override;
    QVariant data(const QModelIndex&,int role=Qt::DisplayRole)const override;
    QVariant headerData(int,Qt::Orientation,int role=Qt::DisplayRole)const override;
    bool setData(const QModelIndex&,const QVariant&,int role)override;
    Qt::ItemFlags flags(const QModelIndex&)const override;
    const CheckpointEntry* entry(int row)const;
    void setContext(const CheckpointRequest&);
    const CheckpointRequest& context()const {return m_context;}
    void setInventory(const QStringList& inputs,const QStringList& active);
    void setCapabilities(unsigned long caps) {m_capabilities=caps;emit stateChanged();}
    unsigned long capabilities()const {return m_capabilities;}
    bool importFile(const QString&);
    bool forget(int row); // Only catalog entry. Bytes remain.
    bool inspectEntry(int row);
    bool exportEntry(int row,const QString& newFolder);
    bool startRun(int row,const QString& newFolder); // row -1: initial run
    bool save(const QString& newPath);
    void cancelIO();
    void cancelRun();
    void closeProject();
    bool ioBusy()const {return m_io.isBusy();}
    bool runBusy()const {return m_run.isBusy();}
    bool busy()const {return ioBusy()||runBusy();}
    QString status()const {return m_status;}
    QStringList reservedPaths()const;
    QList<CheckpointFile> files(int row)const;
    QList<int> suggestions(int keepNewest)const;
    CheckpointStorage storage(const QList<int>& rows)const;
    static QString calendar(const QJsonValue& raw);
    static QString coverage(const QJsonObject& report);
    static QString clocks(const QJsonObject& metadata);
signals:
    void stateChanged();
    void notice(const QString&);
    void runStarted(const QString& label,const QString& input);
    void runTimeline(double startDate,double endDate);
    void runProgress(double acceptedSeconds);
    void runFinished(const CheckpointResult&);
private:
    QString m_catalog,m_status,m_ioEntry,m_recipe,m_runRecipe,m_lineage;
    CheckpointRequest m_context,m_runContext,m_ioContext;
    CheckpointWorker m_io,m_run;
    QList<CheckpointEntry> m_entries;
    quint64 m_publication=0;
    unsigned long m_capabilities=0;
    QByteArray m_catalogHash;
    bool m_writable=true,m_closed=false;
    void load();
    bool persist();
    void changed(int row);
    void recordSave(const CheckpointSaveResult&);
    static QString recipe(const CheckpointRequest&);
};
#endif
