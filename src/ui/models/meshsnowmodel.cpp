// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      meshsnowmodel.cpp
 * @brief     Table model for mesh snow pack assignments and overrides.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/models/meshsnowmodel.h"
#include <openswmm/engine/openswmm_subcatchments.h>
#include <QColor>
#include <QSet>
#include <cmath>
#include <algorithm>

namespace openswmmvis::ui
{
namespace
{
/**
 * @brief Surround a trusted section token with double quotes without escaping it.
 * @param[in] s Token, expected to pass validName() before serialization.
 * @return Quoted string.
 */
QString quoted(const QString &s)
{
    return '"' + s + '"';
}
/**
 * @brief Check the minimal safety shape of a mesh-snow section token.
 * @param[in] s Token; empty, quotes and CR/LF are rejected. Whitespace-only text is otherwise accepted.
 * @return True for a nonempty token without prohibited characters; engine identity validation is separate.
 */
bool validName(const QString &s)
{
    return !s.isEmpty() && !s.contains('"') && !s.contains('\n') && !s.contains('\r');
}
}
QVariant MeshSnowModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Vertical)
        return section + 1;
    static const QStringList labels = {tr("Scope"),
                                       tr("Cell / tag"),
                                       tr("Snowpack"),
                                       tr("Forest fraction"),
                                       tr("Drift factor"),
                                       tr("Ground heat (W/m²)"),
                                       tr("Extinction depth (m)"),
                                       tr("Slope (°)"),
                                       tr("Aspect (°)")};
    return section >= 0 && section < labels.size() ? labels[section] : QVariant{};
}
QVariant MeshSnowModel::data(const QModelIndex &i, int role) const
{
    if (!i.isValid() || i.row() >= rows.size())
        return {};
    const auto &row = rows[i.row()];
    const int col = i.column();
    if (role == Qt::ToolTipRole)
        return col >= 3
                   ? tr("Blank inherits from a lower-priority assignment or the shared snowpack. Slope and aspect inherit cell geometry unless overridden together.")
                   : tr("Cell overrides take priority over region tags, then the whole-mesh default. NONE explicitly disables snow.");
    if (role == Qt::ForegroundRole && col >= 3 && std::isnan(row.overrides[col - 3]))
        return QColor(Qt::gray);
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    if (col == 0)
        return row.cell >= 0 ? tr("Cell") : row.tag == "*" ? tr("Whole mesh") : tr("Region tag");
    if (col == 1)
        return row.cell >= 0 ? QVariant(row.cell + 1) : QVariant(row.tag);
    if (col == 2)
        return row.pack;
    const double value = row.overrides[col - 3];
    return std::isnan(value) ? QVariant(role == Qt::DisplayRole ? tr("Inherited") : QString{})
                             : QVariant(value);
}
Qt::ItemFlags MeshSnowModel::flags(const QModelIndex &i) const
{
    return i.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                             (i.column() > 0 ? Qt::ItemIsEditable : Qt::NoItemFlags)
                       : Qt::NoItemFlags;
}
bool MeshSnowModel::setData(const QModelIndex &i, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !i.isValid() || i.row() >= rows.size() || i.column() < 1)
        return false;
    auto &row = rows[i.row()];
    const int c = i.column();
    if (c == 1) {
        if (row.cell >= 0) {
            bool ok = false;
            int n = value.toString().toInt(&ok);
            if (!ok || n <= 0)
                return false;
            row.cell = n - 1;
        } else {
            if (!validName(value.toString().trimmed()))
                return false;
            row.tag = value.toString().trimmed();
        }
    } else if (c == 2) {
        if (!validName(value.toString()))
            return false;
        row.pack = value.toString();
    } else {
        double n = std::numeric_limits<double>::quiet_NaN();
        if (!value.toString().trimmed().isEmpty()) {
            bool ok = false;
            n = value.toDouble(&ok);
            if (!ok || !std::isfinite(n))
                return false;
        }
        row.overrides[c - 3] = n;
    }
    emit dataChanged(index(i.row(), 0), index(i.row(), 8));
    return true;
}
bool MeshSnowModel::removeRows(int first, int count, const QModelIndex &parent)
{
    if (parent.isValid() || first < 0 || count <= 0 || first > rows.size() - count)
        return false;
    beginRemoveRows({}, first, first + count - 1);
    rows.remove(first, count);
    endRemoveRows();
    return true;
}
void MeshSnowModel::upsert(const MeshSnowRow &row)
{
    for (int i = 0; i < rows.size(); ++i)
        if (rows[i].cell == row.cell && (row.cell >= 0 || rows[i].tag == row.tag)) {
            rows[i] = row;
            emit dataChanged(index(i, 0), index(i, 8));
            return;
        }
    beginInsertRows({}, rows.size(), rows.size());
    rows.append(row);
    endInsertRows();
}
QString MeshSnowModel::load(SWMM_Engine engine)
{
    if (!engine)
        return tr("No model is open.");
    int nextMode = 0;
    double nextStep = 0;
    if (swmm_snow2d_get_options(engine, &nextMode, &nextStep) != SWMM_OK)
        return tr("Cannot read mesh snow options.");
    QVector<MeshSnowRow> next;
    QStringList names = {"NONE"};
    for (int i = 0; i < swmm_snowpack_count(engine); ++i)
        if (const char *name = swmm_snowpack_id(engine, i))
            names.append(QString::fromUtf8(name));
    for (int section : {1, 2}) {
        const int count =
            section == 1 ? swmm_snow2d_defaults_count(engine) : swmm_snow2d_cells_count(engine);
        if (count < 0)
            return tr("Cannot read mesh snow assignments.");
        for (int i = 0; i < count; ++i) {
            char tag[4096], pack[4096];
            int cell = -1;
            SWMM_MeshSnowOverrides v;
            if (swmm_snow2d_get_row(engine, section, i, &cell, tag, sizeof(tag), pack, sizeof(pack),
                                    &v) != SWMM_OK)
                return tr("Cannot read a mesh snow assignment.");
            MeshSnowRow row;
            row.cell = cell;
            row.tag = QString::fromUtf8(tag);
            row.pack = QString::fromUtf8(pack);
            row.overrides = {v.forest,    v.drift,     v.ground_heat_wm2, v.extinction_depth_m,
                             v.slope_deg, v.aspect_deg};
            next.append(row);
        }
    }
    beginResetModel();
    rows = std::move(next);
    packs = names;
    mode = nextMode;
    step = nextStep;
    endResetModel();
    return {};
}
QString MeshSnowModel::stageSamples(int property, const QVector<int> &cells,
                                    const QVector<double> &values, const QStringList &keys,
                                    const QMap<QString, QString> &classPacks,
                                    const QStringList &tags, int *skipped)
{
    if (skipped)
        *skipped = 0;
    if (property < -1 || property > 5 ||
        (property < 0 ? keys.size() : values.size()) != cells.size())
        return tr("Invalid sampled snow values.");
    auto next = rows;
    for (int i = 0; i < cells.size(); ++i) {
        const int cell = cells[i];
        if (cell < 0 || cell >= tags.size())
            return tr("A sampled cell no longer exists.");
        MeshSnowRow row;
        row.cell = cell;
        row.tag.clear();
        row.pack = effective(cell, tags[cell]).pack;
        int found = -1;
        for (int k = 0; k < next.size(); ++k)
            if (next[k].cell == cell) {
                row = next[k];
                found = k;
                break;
            }
        if (property < 0) {
            const auto pack = classPacks.value(keys[i]);
            const auto canonical =
                std::find_if(packs.cbegin(), packs.cend(), [&](const QString &name) {
                    return name.compare(pack, Qt::CaseInsensitive) == 0;
                });
            if (pack.isEmpty() || canonical == packs.cend()) {
                if (skipped)
                    ++*skipped;
                continue;
            }
            row.pack = *canonical;
        } else {
            const double v = values[i];
            if (!std::isfinite(v) || (property != 2 && v < 0) || (property == 0 && v > 1) ||
                (property == 3 && v <= 0) || (property == 4 && v >= 90) ||
                (property == 5 && v > 360)) {
                if (skipped)
                    ++*skipped;
                continue;
            }
            row.overrides[property] = v;
        }
        if (found < 0)
            next.append(row);
        else
            next[found] = row;
    }
    beginResetModel();
    rows = std::move(next);
    endResetModel();
    return {};
}

QString MeshSnowModel::validate(const QStringList &tags) const
{
    if (mode < 0 || mode > 2 || !std::isfinite(step) || step < 0 || step > 3600)
        return tr("Snow step must be 0 (inherit) or at most 3600 seconds.");
    QSet<QString> keys;
    for (const auto &row : rows) {
        const QString key = row.cell >= 0 ? "cell:" + QString::number(row.cell) : "tag:" + row.tag;
        if (keys.contains(key))
            return tr("Duplicate assignment: %1.").arg(key);
        keys.insert(key);
        if (row.cell >= tags.size() || row.cell < -1)
            return tr("Cell %1 no longer exists.").arg(row.cell + 1);
        if (row.cell < 0 && row.tag != "*" && !tags.contains(row.tag))
            return tr("Region tag %1 matches no cells.").arg(row.tag);
        if (!validName(row.pack) || !packs.contains(row.pack, Qt::CaseInsensitive))
            return tr("Snowpack %1 was not found.").arg(row.pack);
        for (int k = 0; k < 6; ++k) {
            const double v = row.overrides[k];
            if (std::isnan(v))
                continue;
            if (!std::isfinite(v) || (k != 2 && v < 0) || (k == 0 && v > 1) || (k == 3 && v <= 0) ||
                (k == 4 && v >= 90) || (k == 5 && v > 360))
                return tr("Invalid %1 for %2.")
                    .arg(headerData(k + 3, Qt::Horizontal).toString(), key);
        }
        if (std::isnan(row.overrides[4]) != std::isnan(row.overrides[5]))
            return tr("Set both slope and aspect, or leave both inherited.");
    }
    bool active = false;
    for (int i = 0; i < tags.size(); ++i) {
        const auto r = effective(i, tags[i]);
        active |= !r.pack.isEmpty() && r.pack.compare("NONE", Qt::CaseInsensitive) != 0;
    }
    if (mode == 1 && !active)
        return tr("Enabled snowmelt requires at least one assigned cell.");
    return {};
}
MeshSnowRow MeshSnowModel::effective(int cell, const QString &tag, QString *origin) const
{
    MeshSnowRow result;
    result.pack = "NONE";
    if (origin)
        *origin = tr("Unassigned");
    for (int pass = 0; pass < 3; ++pass)
        for (const auto &row : rows) {
            const bool selected = pass == 0   ? (row.cell < 0 && row.tag == "*")
                                  : pass == 1 ? (row.cell < 0 && row.tag == tag && tag != "*")
                                              : row.cell == cell;
            if (!selected)
                continue;
            result.pack = row.pack;
            for (int k = 0; k < 6; ++k)
                if (!std::isnan(row.overrides[k]))
                    result.overrides[k] = row.overrides[k];
            if (origin)
                *origin = pass == 0   ? tr("Whole mesh")
                          : pass == 1 ? tr("Region %1").arg(tag)
                                      : tr("Cell override");
        }
    return result;
}
std::array<QString, 3> MeshSnowModel::sections() const
{
    static const char *modes[] = {"AUTO", "YES", "NO"};
    std::array<QString, 3> text;
    text[0] = "SNOWMELT " + QString::fromLatin1(modes[std::clamp(mode, 0, 2)]) + "\nSNOW_STEP " +
              (step > 0 ? QString::number(step, 'g', 17) : "DEFAULT") + "\n";
    const QStringList keys = {"FC", "F", "QG", "AEP", "SLOPE", "ASPECT"};
    for (const auto &row : rows) {
        QString line = row.cell >= 0 ? "CELL " + QString::number(row.cell + 1) : quoted(row.tag);
        line += " SNOWPACK " + quoted(row.pack);
        for (int k = 0; k < 6; ++k)
            if (!std::isnan(row.overrides[k]))
                line += ' ' + keys[k] + ' ' + QString::number(row.overrides[k], 'g', 17);
        text[row.cell >= 0 ? 2 : 1] += line + '\n';
    }
    return text;
}
QString MeshSnowModel::readSections(SWMM_Engine engine, std::array<QString, 3> &text)
{
    std::array<QString, 3> next;
    for (int section = 0; section < 3; ++section) {
        int size = 0;
        if (swmm_snow2d_get_configuration(engine, section, nullptr, 0, &size) != SWMM_OK ||
            size <= 0)
            return tr("Cannot read mesh snow configuration.");
        QByteArray data(size, '\0');
        if (swmm_snow2d_get_configuration(engine, section, data.data(), size, &size) != SWMM_OK)
            return tr("Cannot read mesh snow configuration.");
        next[section] = QString::fromUtf8(data.constData());
    }
    text = std::move(next);
    return {};
}
QString MeshSnowModel::applySections(SWMM_Engine engine, const std::array<QString, 3> &text)
{
    char error[2048] = {};
    if (swmm_snow2d_apply_configuration(engine, text[0].toUtf8().constData(),
                                        text[1].toUtf8().constData(), text[2].toUtf8().constData(),
                                        error, sizeof(error)) != SWMM_OK)
        return *error
                   ? QString::fromUtf8(error)
                   : tr("Mesh snow changes are unavailable while the engine is initialized or running.");
    return {};
}
}
