// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "layers/featurelayer.h"
#include "ui/panels/swmmattributetablemodel.h"
#include <QAbstractTableModel>
#include <QPointer>
class MapCanvas;
class FeatureAttributeModel final : public QAbstractTableModel
{
public:
    FeatureAttributeModel(FeatureLayer *layer, QVector<qint64> ids, MapCanvas *canvas,
                          QObject *parent);
    QList<openswmmvis::ColumnSpec> columnSpecs() const;
    int rowCount(const QModelIndex &p = {}) const override;
    int columnCount(const QModelIndex &p = {}) const override;
    QVariant data(const QModelIndex &i, int role = Qt::DisplayRole) const override;
    QVariant headerData(int n, Qt::Orientation o, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &i) const override;
    bool apply(int column, const QVariant &value, QString *error);
    bool active() const;
    QPointer<FeatureLayer> layer;
    QVector<qint64> ids;
    openswmmvis::feature::Schema capturedSchema;
    QPointer<MapCanvas> canvas;
    bool valid = true;
};
class FeatureAggregateSelection final : public QObject
{
public:
    struct Field {
        bool mixed = false;
        QVariant display, edit;
        int editable = 0;
        QString reason;
    };
    FeatureAggregateSelection(FeatureLayer *layer, QVector<qint64> ids, MapCanvas *canvas,
                              QObject *parent);
    FeatureAttributeModel *source() const { return m_source; }
    QList<int> rows() const;
    Field field(int column) const;
    bool setValue(int column, const QVariant &value);
    bool isStaged(int) const { return false; }
    QVariant stagedValue(int) const { return {}; }
    std::function<void(QString)> finished;

private:
    FeatureAttributeModel *m_source;
};
Q_DECLARE_METATYPE(FeatureAggregateSelection *)
bool applyFeatureAttributeValues(FeatureLayer *layer, const QVector<qint64> &ids,
                                 const QString &key, const QVariant &value, MapCanvas *canvas,
                                 QString *error = nullptr);
