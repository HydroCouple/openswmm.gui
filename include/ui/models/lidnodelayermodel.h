// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENSWMMVIS_LIDNODELAYERMODEL_H
#define OPENSWMMVIS_LIDNODELAYERMODEL_H
#include <QAbstractTableModel>
#include <QVector>
#include "lid/lidcontrolprovider.h"
#include <openswmm/engine/openswmm_infrastructure.h>
namespace openswmmvis::ui {
// Draft model: Apply commits the complete stack atomically through the engine.
class LidNodeLayerModel : public QAbstractTableModel {
public:
    using QAbstractTableModel::QAbstractTableModel;
    QVector<SWMM_LidNodeLayer> layers;
    QVector<QVector<openswmmvis::lid::LidLayerTreatment>> treatments;
    QVector<openswmmvis::lid::LidLayerTreatment> treatmentRows() const;
    void setTreatments(const QVector<openswmmvis::lid::LidLayerTreatment>& rows);
    int rowCount(const QModelIndex& p = {}) const override { return p.isValid() ? 0 : layers.size(); }
    int columnCount(const QModelIndex& p = {}) const override { return p.isValid() ? 0 : 10; }
    QVariant data(const QModelIndex&, int role = Qt::DisplayRole) const override;
    QVariant headerData(int, Qt::Orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex&) const override;
    bool setData(const QModelIndex&, const QVariant&, int role = Qt::EditRole) override;
    void setLayers(QVector<SWMM_LidNodeLayer> value);
    void append(int kind, bool si = true);
    void remove(int row);
    void move(int row, int delta);
    int mediaCount() const;
    void setMediaCount(int count, bool si = true);
private:
    static int parameter(int kind, int column);
};
}
#endif
