// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QAbstractTableModel>
#include <QStringList>
#include <QMap>
#include <array>
#include <limits>
#include <openswmm/engine/openswmm_snow2d.h>

namespace openswmmvis::ui {
struct MeshSnowRow {
    int cell=-1; // zero-based; -1 is a tag/default
    QString tag="*",pack="NONE";
    std::array<double,6> overrides;
    MeshSnowRow(){overrides.fill(std::numeric_limits<double>::quiet_NaN());}
};
// Staged, shared table/selection model. No widget edits write to the engine.
class MeshSnowModel final : public QAbstractTableModel {
public:
    explicit MeshSnowModel(QObject* parent=nullptr):QAbstractTableModel(parent){}
    int rowCount(const QModelIndex& p={})const override{return p.isValid()?0:rows.size();}
    int columnCount(const QModelIndex& p={})const override{return p.isValid()?0:9;}
    QVariant data(const QModelIndex&,int role=Qt::DisplayRole)const override;
    QVariant headerData(int,Qt::Orientation,int role=Qt::DisplayRole)const override;
    Qt::ItemFlags flags(const QModelIndex&)const override;
    bool setData(const QModelIndex&,const QVariant&,int role=Qt::EditRole)override;
    bool removeRows(int,int,const QModelIndex& parent={})override;
    void upsert(const MeshSnowRow&);
    QString stageSamples(int property,const QVector<int>& cells,const QVector<double>& values,
        const QStringList& keys,const QMap<QString,QString>& classPacks,
        const QStringList& cellTags,int* skipped=nullptr);
    QString load(SWMM_Engine);
    QString validate(const QStringList& cellTags)const;
    MeshSnowRow effective(int cell,const QString& tag,QString* origin=nullptr)const;
    std::array<QString,3> sections()const;
    static QString readSections(SWMM_Engine,std::array<QString,3>&);
    static QString applySections(SWMM_Engine,const std::array<QString,3>&);
    QStringList packs;
    QVector<MeshSnowRow> rows;
    int mode=0;double step=0;
};
}
