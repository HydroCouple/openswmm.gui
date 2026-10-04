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
        l = {}; l.kind = kind;
    } else {
        const int p = parameter(l.kind, i.column()); bool ok = false; const double value = v.toDouble(&ok);
        if (p < 0 || !ok || !std::isfinite(value) || value < 0) return false;
        l.params[p] = value;
    }
    emit dataChanged(index(i.row(), 0), index(i.row(), 9)); return true;
}
void LidNodeLayerModel::setLayers(QVector<SWMM_LidNodeLayer> value) { beginResetModel(); layers = std::move(value); endResetModel(); }
void LidNodeLayerModel::append(int kind, bool si) {
    const int r = layers.size(); beginInsertRows({}, r, r);
    SWMM_LidNodeLayer l{}; l.kind = kind;
    if (kind == 0) l.params[0] = 100;
    if (kind == 1) { l.params[0] = 300; l.params[1] = .45; l.params[2] = .2; l.params[3] = .08; l.params[4] = 25; l.params[5] = 10; l.params[6] = 90; }
    if (kind == 2) { l.params[0] = 100; l.params[1] = .4; l.params[2] = 500; }
    if (!si) {
        if (kind != 3) l.params[0] /= 25.4;
        if (kind == 1) { l.params[4] /= 25.4; l.params[6] /= 25.4; }
        if (kind == 2) l.params[2] /= 25.4;
    }
    layers.append(l); endInsertRows();
}
void LidNodeLayerModel::remove(int row) { if (row < 0 || row >= layers.size()) return; beginRemoveRows({}, row, row); layers.removeAt(row); endRemoveRows(); }
void LidNodeLayerModel::move(int row, int delta) {
    const int to = row + delta; if (row < 0 || row >= layers.size() || to < 0 || to >= layers.size()) return;
    beginMoveRows({}, row, row, {}, to > row ? to + 1 : to); layers.move(row, to); endMoveRows();
}
}
