// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/models/lidnodelayermodel.h"
#include <cmath>
namespace openswmmvis::ui {
int LidNodeLayerModel::parameter(int kind, int column) {
    static const int map[4][10] = {
        {-1, 0, -1, -1, -1, -1, -1, -1, 1, -1},
        {-1, 0, 1, 2, 3, 4, 5, 6, -1, -1},
        {-1, 0, 1, -1, -1, 2, -1, -1, -1, -1},
        {-1, -1, -1, -1, -1, 0, -1, -1, -1, 1}};
    return kind >= 0 && kind < 4 && column >= 0 && column < 10 ? map[kind][column] : -1;
}
QVariant LidNodeLayerModel::data(const QModelIndex& i, int role) const {
    if (!i.isValid() || i.row() >= layers.size() || (role != Qt::DisplayRole && role != Qt::EditRole)) return {};
    const auto& l = layers[i.row()];
    if (i.column() == 0) return QStringList{"SURFACE", "MEDIA", "AGGREGATE", "BOTTOM"}.value(l.kind);
    const int p = parameter(l.kind, i.column());
    return p >= 0 ? QVariant(l.params[p]) : QVariant{};
}
QVariant LidNodeLayerModel::headerData(int section, Qt::Orientation o, int role) const {
    if (role != Qt::DisplayRole) return {};
    if (o == Qt::Vertical) return section + 1;
    return QStringList{tr("Layer"), tr("Thickness"), tr("Porosity"), tr("Field capacity"), tr("Wilting point"),
        tr("Conductivity / seepage"), tr("Conductivity slope"), tr("Suction"), tr("Vegetation fraction"), tr("Clog factor")}.value(section);
}
Qt::ItemFlags LidNodeLayerModel::flags(const QModelIndex& i) const {
    auto f = QAbstractTableModel::flags(i);
    if (i.isValid() && i.row() < layers.size() && (i.column() == 0 || parameter(layers[i.row()].kind, i.column()) >= 0)) f |= Qt::ItemIsEditable;
    return f;
}
bool LidNodeLayerModel::setData(const QModelIndex& i, const QVariant& v, int role) {
    if (!i.isValid() || i.row() >= layers.size() || role != Qt::EditRole) return false;
    auto& l = layers[i.row()];
    if (i.column() == 0) {
        const int kind = QStringList{"SURFACE", "MEDIA", "AGGREGATE", "BOTTOM"}.indexOf(v.toString().toUpper());
        if (kind < 0) return false;
        if (kind == l.kind) return true;
        // Boundary rows remain at the ends; use Add/Remove to change them.
        if (kind == 0 || kind == 3 || l.kind == 0 || l.kind == 3) return false;
        l.kind = kind; // preserve thickness, porosity and the treatment attached to this layer
        const double conductivity = l.params[kind == 2 ? 4 : 2];
        l.params[2] = kind == 2 ? conductivity : .2;
        l.params[3] = kind == 1 ? .08 : 0;
        l.params[4] = kind == 1 ? conductivity : 0;
        l.params[5] = kind == 1 ? 10 : 0;
        l.params[6] = 0;
    } else {
        const int p = parameter(l.kind, i.column()); bool ok = false; const double value = v.toDouble(&ok);
        if (p < 0 || !ok || !std::isfinite(value) || value < 0) return false;
        l.params[p] = value;
    }
    emit dataChanged(index(i.row(), 0), index(i.row(), 9)); return true;
}
void LidNodeLayerModel::setLayers(QVector<SWMM_LidNodeLayer> value) { beginResetModel(); layers = std::move(value); treatments.clear(); treatments.resize(layers.size()); endResetModel(); }
void LidNodeLayerModel::append(int kind, bool si) {
    if (kind < 0 || kind > 3) return;
    if (kind == 0 || kind == 3)
        for (const auto& layer : layers) if (layer.kind == kind) return;
    const int r = kind == 0 ? 0 : kind != 3 && !layers.isEmpty() && layers.back().kind == 3
        ? layers.size() - 1 : layers.size();
    beginInsertRows({}, r, r);
    SWMM_LidNodeLayer l{}; l.kind = kind;
    if (kind == 0) l.params[0] = 100;
    if (kind == 1) { l.params[0] = 300; l.params[1] = .45; l.params[2] = .2; l.params[3] = .08; l.params[4] = 25; l.params[5] = 10; l.params[6] = 90; }
    if (kind == 2) { l.params[0] = 100; l.params[1] = .4; l.params[2] = 500; }
    if (!si) {
        if (kind != 3) l.params[0] /= 25.4;
        if (kind == 1) { l.params[4] /= 25.4; l.params[6] /= 25.4; }
        if (kind == 2) l.params[2] /= 25.4;
    }
    layers.insert(r, l); treatments.insert(r, QVector<openswmmvis::lid::LidLayerTreatment>{}); endInsertRows();
}
int LidNodeLayerModel::mediaCount() const {
    int count = 0;
    for (const auto& l : layers) if (l.kind == 1 || l.kind == 2) ++count;
    return count;
}
void LidNodeLayerModel::setMediaCount(int count, bool si) {
    if (count < 1) return;
    while (mediaCount() < count) append(1, si);
    for (int i = layers.size() - 1; i >= 0 && mediaCount() > count; --i)
        if (layers[i].kind == 1 || layers[i].kind == 2) remove(i);
}
void LidNodeLayerModel::remove(int row) { if (row < 0 || row >= layers.size()) return; beginRemoveRows({}, row, row); layers.removeAt(row); treatments.removeAt(row); endRemoveRows(); }
void LidNodeLayerModel::move(int row, int delta) {
    const int to = row + delta; if (row < 0 || row >= layers.size() || to < 0 || to >= layers.size()) return;
    beginMoveRows({}, row, row, {}, to > row ? to + 1 : to); layers.move(row, to); treatments.move(row, to); endMoveRows();
}
}

namespace openswmmvis::ui {
QVector<openswmmvis::lid::LidLayerTreatment> LidNodeLayerModel::treatmentRows() const {
    QVector<openswmmvis::lid::LidLayerTreatment> result;
    for (int i=0;i<treatments.size();++i) for(auto rule:treatments[i]) {rule.layer=i+1;result.append(rule);}
    return result;
}
void LidNodeLayerModel::setTreatments(const QVector<openswmmvis::lid::LidLayerTreatment>& rows) {
    treatments.clear();treatments.resize(layers.size());
    for(const auto& rule:rows)if(rule.layer>0&&rule.layer<=layers.size())treatments[rule.layer-1].append(rule);
}
}
