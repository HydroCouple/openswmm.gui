/**
 * @file      frozengroundrowmodel.cpp
 * @brief     Staged [FROZEN_GROUND] / [2D_FROZEN_GROUND] rows with a read-back commit.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/frozengroundrowmodel.h"

#include "ui/dialogs/mesh2dsectionrowmodel.h"

#include <QLocale>
#include <QSet>

#include <openswmm/engine/openswmm_subcatchments.h>

#include <cmath>

namespace openswmmvis::ui
{

namespace
{
constexpr int kNameLen = 512;

struct Column {
    const char *title;
    const char *help;
};
// Titles carry the unit; every value is SI whatever the project's flow units.
const Column kColumns[FrozenGroundRowModel::ValueCount] = {
    {QT_TRANSLATE_NOOP("FrozenGroundRowModel", "Thickness (m)"),
     QT_TRANSLATE_NOOP("FrozenGroundRowModel", "Thickness of the frost-active ground layer.")},
    {QT_TRANSLATE_NOOP("FrozenGroundRowModel", "Conductivity (W/m·K)"),
     QT_TRANSLATE_NOOP("FrozenGroundRowModel", "Bulk thermal conductivity of the ground.")},
    {QT_TRANSLATE_NOOP("FrozenGroundRowModel", "Heat capacity (J/m³·K)"),
     QT_TRANSLATE_NOOP("FrozenGroundRowModel", "Bulk volumetric heat capacity of the ground.")},
    {QT_TRANSLATE_NOOP("FrozenGroundRowModel", "Water content"),
     QT_TRANSLATE_NOOP("FrozenGroundRowModel",
                       "Volume fraction of water in the layer that can freeze (0 to 1).")},
    {QT_TRANSLATE_NOOP("FrozenGroundRowModel", "Deep temperature (°C)"),
     QT_TRANSLATE_NOOP("FrozenGroundRowModel", "Ground temperature at the deep depth.")},
    {QT_TRANSLATE_NOOP("FrozenGroundRowModel", "Deep depth (m)"),
     QT_TRANSLATE_NOOP("FrozenGroundRowModel",
                       "Depth at which the deep temperature holds; more than half the "
                       "thickness.")},
    {QT_TRANSLATE_NOOP("FrozenGroundRowModel", "Initial temperature (°C)"),
     QT_TRANSLATE_NOOP("FrozenGroundRowModel",
                       "Layer temperature at the start; below zero starts fully frozen.")},
    {QT_TRANSLATE_NOOP("FrozenGroundRowModel", "Exponent"),
     QT_TRANSLATE_NOOP("FrozenGroundRowModel",
                       "Exponent m of the infiltration factor (1 − frozen fraction)^m; at "
                       "least 1.")},
    {QT_TRANSLATE_NOOP("FrozenGroundRowModel", "Snow conductivity (W/m·K)"),
     QT_TRANSLATE_NOOP("FrozenGroundRowModel", "Thermal conductivity of snow.")},
    {QT_TRANSLATE_NOOP("FrozenGroundRowModel", "Snow density (kg/m³)"),
     QT_TRANSLATE_NOOP("FrozenGroundRowModel",
                       "Density of snow, used to turn its water equivalent into depth.")},
};

/** Shortest text that parses back to the same double. */
QString numberText(double value)
{
    return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

bool canEdit(SWMM_Engine engine)
{
    int state = SWMM_STATE_CREATED;
    return engine && swmm_engine_get_state(engine, &state) == SWMM_OK &&
           (state == SWMM_STATE_BUILDING || state == SWMM_STATE_OPENED);
}

QStringList subcatchmentNames(SWMM_Engine engine)
{
    QStringList names;
    if (!engine)
        return names;
    const int count = swmm_subcatch_count(engine);
    for (int i = 0; i < count; ++i)
        if (const char *id = swmm_subcatch_id(engine, i))
            names << QString::fromUtf8(id);
    return names;
}
} // namespace

FrozenGroundRowModel::FrozenGroundRowModel(Target target, QObject *parent)
    : QAbstractTableModel(parent), m_target(target)
{
}

QString FrozenGroundRowModel::sectionName() const
{
    return m_target == Target::Mesh ? QStringLiteral("[2D_FROZEN_GROUND]")
                                    : QStringLiteral("[FROZEN_GROUND]");
}

std::array<double, FrozenGroundRowModel::ValueCount> FrozenGroundRowModel::defaultValues()
{
    return {0.15, 1.5, 2.4e6, 0.30, 8.0, 3.0, 5.0, 11.0, 0.25, 250.0};
}

int FrozenGroundRowModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int FrozenGroundRowModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 1 + ValueCount;
}

QVariant FrozenGroundRowModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows[index.row()];
    const int column = index.column();
    if (role == Qt::DisplayRole || role == Qt::EditRole)
        return column == 0 ? row.name
                           : numberText(row.values[static_cast<std::size_t>(column - 1)]);
    if (role == ChoicesRole && column == 0) {
        QStringList choices{QStringLiteral("*")};
        choices << m_names;
        return choices;
    }
    if (role == Qt::ToolTipRole)
        return headerData(column, Qt::Horizontal, Qt::ToolTipRole);
    return {};
}

bool FrozenGroundRowModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || index.row() >= m_rows.size())
        return false;
    Row &row = m_rows[index.row()];
    if (index.column() == 0) {
        const QString name = value.toString().trimmed();
        if (name.isEmpty() || name == row.name)
            return false;
        row.name = name;
    } else {
        bool ok = false;
        const double number = value.userType() == QMetaType::Double
                                  ? (ok = true, value.toDouble())
                                  : QLocale::c().toDouble(value.toString().trimmed(), &ok);
        auto &slot = row.values[static_cast<std::size_t>(index.column() - 1)];
        if (!ok || !std::isfinite(number) || number == slot)
            return false;
        slot = number;
    }
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    return true;
}

QVariant FrozenGroundRowModel::headerData(int section, Qt::Orientation orientation,
                                          int role) const
{
    if (orientation != Qt::Horizontal || section < 0 || section > ValueCount)
        return QAbstractTableModel::headerData(section, orientation, role);
    if (role == Qt::DisplayRole) {
        if (section == 0)
            return m_target == Target::Mesh ? tr("Mesh tag") : tr("Subcatchment");
        return tr(kColumns[section - 1].title);
    }
    if (role == Qt::ToolTipRole) {
        if (section == 0)
            return m_target == Target::Mesh
                       ? tr("A mesh tag, or * for every cell. A tag row wins over the * row.")
                       : tr("A subcatchment, or * for every subcatchment without its own row.");
        return tr(kColumns[section - 1].help);
    }
    return {};
}

Qt::ItemFlags FrozenGroundRowModel::flags(const QModelIndex &index) const
{
    return index.isValid() ? Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable
                           : Qt::NoItemFlags;
}

bool FrozenGroundRowModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid() || row < 0 || count <= 0 || row + count > m_rows.size())
        return false;
    beginRemoveRows({}, row, row + count - 1);
    m_rows.remove(row, count);
    endRemoveRows();
    return true;
}

int FrozenGroundRowModel::appendRow()
{
    QSet<QString> used;
    for (const Row &row : m_rows)
        used.insert(row.name);
    QString name;
    if (!used.contains(QStringLiteral("*"))) {
        name = QStringLiteral("*");
    } else {
        for (const QString &candidate : m_names)
            if (!used.contains(candidate)) {
                name = candidate;
                break;
            }
    }
    if (name.isEmpty())
        return -1;
    const int at = static_cast<int>(m_rows.size());
    beginInsertRows({}, at, at);
    m_rows.append({name, defaultValues()});
    endInsertRows();
    return at;
}

QString FrozenGroundRowModel::rowProblem(const Row &row)
{
    const auto &v = row.values;
    for (double value : v)
        if (!std::isfinite(value))
            return tr("%1: every value must be a number.").arg(row.name);
    if (!(v[0] > 0.0))
        return tr("%1: thickness must be positive.").arg(row.name);
    if (!(v[1] > 0.0))
        return tr("%1: conductivity must be positive.").arg(row.name);
    if (!(v[2] > 0.0))
        return tr("%1: heat capacity must be positive.").arg(row.name);
    if (v[3] < 0.0 || v[3] > 1.0)
        return tr("%1: water content must lie between 0 and 1.").arg(row.name);
    if (!(v[5] > 0.5 * v[0]))
        return tr("%1: deep depth must exceed half the thickness.").arg(row.name);
    if (v[7] < 1.0)
        return tr("%1: exponent must be at least 1.").arg(row.name);
    if (!(v[8] > 0.0))
        return tr("%1: snow conductivity must be positive.").arg(row.name);
    if (!(v[9] > 0.0))
        return tr("%1: snow density must be positive.").arg(row.name);
    return {};
}

QString FrozenGroundRowModel::read(SWMM_Engine engine, Target target, QList<Row> &rows)
{
    rows.clear();
    int count = 0;
    if (swmm_frozen_ground_count(engine, static_cast<int>(target), &count) != SWMM_OK)
        return tr("The frozen-ground rows could not be read.");
    for (int i = 0; i < count; ++i) {
        char name[kNameLen]{};
        Row row;
        if (swmm_frozen_ground_get(engine, static_cast<int>(target), i, name, sizeof name,
                                   row.values.data()) != SWMM_OK)
            return tr("Frozen-ground row %1 could not be read.").arg(i + 1);
        row.name = QString::fromUtf8(name);
        rows.append(row);
    }
    return {};
}

QString FrozenGroundRowModel::write(SWMM_Engine engine, Target target, const QList<Row> &rows)
{
    const int code = static_cast<int>(target);
    QList<Row> live;
    if (const QString error = read(engine, target, live); !error.isEmpty())
        return error;
    for (const Row &row : live)
        if (swmm_frozen_ground_remove(engine, code, row.name.toUtf8().constData()) != SWMM_OK)
            return tr("Row %1 could not be removed.").arg(row.name);
    for (const Row &row : rows)
        if (swmm_frozen_ground_set(engine, code, row.name.toUtf8().constData(),
                                   row.values.data()) != SWMM_OK)
            return tr("Row %1 was refused by the engine.").arg(row.name);
    if (const QString error = read(engine, target, live); !error.isEmpty())
        return error;
    return live == rows ? QString() : tr("The engine did not keep the rows as written.");
}

QString FrozenGroundRowModel::load(SWMM_Engine engine)
{
    beginResetModel();
    m_rows.clear();
    m_loaded.clear();
    m_names.clear();
    QString error;
    if (engine) {
        m_names = m_target == Target::Mesh ? meshCellTags(engine) : subcatchmentNames(engine);
        error = read(engine, m_target, m_loaded);
        if (error.isEmpty())
            m_rows = m_loaded;
        else
            m_loaded.clear();
    }
    endResetModel();
    return error;
}

QString FrozenGroundRowModel::validateCommit(SWMM_Engine engine) const
{
    if (!canEdit(engine))
        return tr("The model cannot be edited while a simulation is running.");
    QList<Row> live;
    if (const QString error = read(engine, m_target, live); !error.isEmpty())
        return error;
    if (live != m_loaded)
        return tr("%1 changed since this editor read it. Reopen the editor.").arg(sectionName());
    QSet<QString> seen;
    for (const Row &row : m_rows) {
        if (seen.contains(row.name))
            return tr("%1 has more than one row.").arg(row.name);
        seen.insert(row.name);
        if (row.name != QLatin1String("*") && !m_names.contains(row.name))
            return m_target == Target::Mesh
                       ? tr("%1 is not a tag of any mesh cell.").arg(row.name)
                       : tr("%1 is not a subcatchment.").arg(row.name);
        if (const QString problem = rowProblem(row); !problem.isEmpty())
            return problem;
    }
    return {};
}

QString FrozenGroundRowModel::commit(SWMM_Engine engine, bool advanceBaseline)
{
    if (!isDirty())
        return {};
    QString error = write(engine, m_target, m_rows);
    if (!error.isEmpty()) {
        const QString restore = restoreLoaded(engine);
        if (!restore.isEmpty())
            error += QLatin1Char(' ') + restore;
        return error;
    }
    if (advanceBaseline)
        acceptChanges();
    return {};
}

QString FrozenGroundRowModel::restoreLoaded(SWMM_Engine engine) const
{
    QList<Row> live;
    if (const QString error = read(engine, m_target, live); !error.isEmpty())
        return error;
    return live == m_loaded ? QString() : write(engine, m_target, m_loaded);
}

} // namespace openswmmvis::ui
