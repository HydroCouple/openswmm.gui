/**
 * @file      mesh2dboundaryqualitymodel.cpp
 * @brief     Staged [2D_BOUNDARY_QUALITY] rows for the 2D Surface Quality dialog (review G-03).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/mesh2dboundaryqualitymodel.h"

#include "ui/dialogs/mesh2dsectionrowmodel.h"

#include <openswmm/engine/openswmm_2d.h>

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace openswmmvis::ui
{

namespace
{

constexpr int kSpeciesLen = 128;

/*! Vertex pair of local edge @p edge: the convention the aquifer edge model and the engine use
 *  (vertices e+1 and e+2, wrapping). */
quint64 edgeKey(const int *vertices, int nv, int edge)
{
    int a = vertices[(edge + 1) % nv], b = vertices[(edge + 2) % nv];
    if (a > b)
        std::swap(a, b);
    return (quint64(quint32(a)) << 32) | quint32(b);
}

/*! True when (cell, edge) names a local edge of a cell of the mesh. */
bool isMeshEdge(SWMM_Engine engine, int cell, int edge)
{
    int cells = 0, vertices[4]{}, nv = 0;
    return swmm_2d_cell_count(engine, &cells) == SWMM_OK && cell >= 0 && cell < cells &&
           swmm_2d_cell_get_vertices(engine, cell, vertices, &nv) == SWMM_OK && edge >= 0 &&
           edge < nv;
}

/*! True while the engine accepts configuration edits (not running). */
bool editable(SWMM_Engine engine)
{
    int state = SWMM_STATE_CREATED;
    return engine && swmm_engine_get_state(engine, &state) == SWMM_OK &&
           (state == SWMM_STATE_BUILDING || state == SWMM_STATE_OPENED);
}

/*! The engine's rows, in its order. */
QString readRows(SWMM_Engine engine, QList<Mesh2DBoundaryQualityModel::Row> *rows)
{
    int count = 0;
    if (swmm_2d_boundary_quality_count(engine, &count) != SWMM_OK)
        return QObject::tr("Cannot read the boundary inflow quality rows.");
    for (int i = 0; i < count; ++i) {
        Mesh2DBoundaryQualityModel::Row row;
        char species[kSpeciesLen]{};
        if (swmm_2d_get_boundary_quality(engine, i, &row.cell, &row.edge, species, kSpeciesLen,
                                         &row.concentration) != SWMM_OK)
            return QObject::tr("Cannot read boundary inflow quality row %1.").arg(i + 1);
        row.species = QString::fromUtf8(species);
        rows->append(row);
    }
    return {};
}

/*! Replace the engine's rows with @p rows. */
QString writeRows(SWMM_Engine engine, const QList<Mesh2DBoundaryQualityModel::Row> &rows)
{
    int count = 0;
    if (swmm_2d_boundary_quality_count(engine, &count) != SWMM_OK)
        return QObject::tr("Cannot read the boundary inflow quality rows.");
    for (int i = count - 1; i >= 0; --i)
        if (swmm_2d_remove_boundary_quality(engine, i) != SWMM_OK)
            return QObject::tr("The engine refused to remove boundary inflow quality row %1.")
                .arg(i + 1);
    for (const auto &row : rows)
        if (swmm_2d_set_boundary_quality(engine, row.cell, row.edge,
                                         row.species.toUtf8().constData(),
                                         row.concentration) != SWMM_OK)
            return QObject::tr("The engine refused %1 on cell %2, edge %3.")
                .arg(row.species)
                .arg(row.cell)
                .arg(row.edge);
    return {};
}

} // namespace

Mesh2DBoundaryQualityModel::Mesh2DBoundaryQualityModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

QString Mesh2DBoundaryQualityModel::load(SWMM_Engine engine)
{
    beginResetModel();
    m_rows.clear();
    m_loaded.clear();
    m_species.clear();
    QString error;
    if (engine) {
        error = readRows(engine, &m_rows);
        m_loaded = m_rows;
        m_species = surfaceSpeciesNames(engine, true);
    }
    endResetModel();
    return error;
}

void Mesh2DBoundaryQualityModel::addRow(const Row &row)
{
    beginInsertRows(QModelIndex(), int(m_rows.size()), int(m_rows.size()));
    m_rows.append(row);
    endInsertRows();
}

void Mesh2DBoundaryQualityModel::removeRowAt(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_rows.removeAt(row);
    endRemoveRows();
}

Mesh2DBoundaryQualityModel::EdgeScope
Mesh2DBoundaryQualityModel::classifyEdges(SWMM_Engine engine, const QList<QPair<int, int>> &edges,
                                          QString *error)
{
    EdgeScope scope;
    int cells = 0;
    if (!engine || swmm_2d_cell_count(engine, &cells) != SWMM_OK) {
        if (error && engine)
            *error = tr("Cannot read the mesh.");
        return scope;
    }
    QList<QPair<int, int>> requested;
    QHash<QPair<int, int>, quint64> keys;
    QHash<quint64, int> occurrences;
    for (const auto &edge : edges) {
        if (keys.contains(edge))
            continue;
        int vertices[4]{}, nv = 0;
        if (edge.first < 0 || edge.first >= cells ||
            swmm_2d_cell_get_vertices(engine, edge.first, vertices, &nv) != SWMM_OK ||
            edge.second < 0 || edge.second >= nv) {
            ++scope.invalid;
            keys.insert(edge, 0);
            continue;
        }
        const quint64 key = edgeKey(vertices, nv, edge.second);
        keys.insert(edge, key);
        occurrences.insert(key, 0);
        requested.append(edge);
    }
    for (int cell = 0; cell < cells && !occurrences.isEmpty(); ++cell) {
        int vertices[4]{}, nv = 0;
        if (swmm_2d_cell_get_vertices(engine, cell, vertices, &nv) != SWMM_OK) {
            if (error)
                *error = tr("Cannot read cell connectivity.");
            return EdgeScope();
        }
        for (int e = 0; e < nv; ++e) {
            auto found = occurrences.find(edgeKey(vertices, nv, e));
            if (found != occurrences.end())
                ++found.value();
        }
    }
    for (const auto &edge : requested) {
        if (occurrences.value(keys.value(edge)) == 1)
            scope.exterior.append(edge);
        else
            ++scope.interior;
    }
    return scope;
}

int Mesh2DBoundaryQualityModel::setForEdges(const QList<QPair<int, int>> &edges,
                                            const QString &species, double concentration)
{
    int changed = 0;
    for (const auto &edge : edges) {
        bool found = false;
        for (int i = 0; i < m_rows.size(); ++i) {
            Row &row = m_rows[i];
            if (row.cell != edge.first || row.edge != edge.second || row.species != species)
                continue;
            found = true;
            if (row.concentration != concentration) {
                row.concentration = concentration;
                emit dataChanged(index(i, ConcentrationColumn), index(i, ConcentrationColumn));
                ++changed;
            }
            break;
        }
        if (!found) {
            addRow({edge.first, edge.second, species, concentration});
            ++changed;
        }
    }
    return changed;
}

QString Mesh2DBoundaryQualityModel::validateCommit(SWMM_Engine engine) const
{
    if (!editable(engine))
        return tr("A simulation is running; boundary inflow quality can change only before it "
                  "starts.");
    const QStringList species = surfaceSpeciesNames(engine, true);
    QList<QPair<int, int>> edges;
    QSet<QString> keys;
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &row = m_rows[i];
        const QString where = tr("Boundary inflow row %1").arg(i + 1);
        if (!species.contains(row.species))
            return tr("%1: %2 is not a species of this model.").arg(where, row.species);
        const bool temperature = row.species == QLatin1String("__TEMPERATURE__");
        if (!std::isfinite(row.concentration) || (row.concentration < 0.0 && !temperature))
            return tr("%1: the concentration must be a number that is not negative.").arg(where);
        const QString key = QStringLiteral("%1/%2/%3").arg(row.cell).arg(row.edge).arg(row.species);
        if (keys.contains(key))
            return tr("%1: cell %2, edge %3 already has a %4 row.")
                .arg(where)
                .arg(row.cell)
                .arg(row.edge)
                .arg(row.species);
        keys.insert(key);
        edges.append({row.cell, row.edge});
    }
    QString error;
    const EdgeScope scope = classifyEdges(engine, edges, &error);
    if (!error.isEmpty())
        return error;
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &row = m_rows[i];
        if (!scope.exterior.contains({row.cell, row.edge}))
            return tr("Boundary inflow row %1: cell %2, edge %3 is %4. Water enters the mesh only "
                      "through its outer edges.")
                .arg(i + 1)
                .arg(row.cell)
                .arg(row.edge)
                .arg(isMeshEdge(engine, row.cell, row.edge) ? tr("an interior edge")
                                                             : tr("not an edge of the mesh"));
    }
    return {};
}

QString Mesh2DBoundaryQualityModel::commit(SWMM_Engine engine)
{
    if (!engine)
        return tr("No model is open.");
    return writeRows(engine, m_rows);
}

QString Mesh2DBoundaryQualityModel::restoreLoaded(SWMM_Engine engine)
{
    if (!engine)
        return tr("No model is open.");
    return writeRows(engine, m_loaded);
}

int Mesh2DBoundaryQualityModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int Mesh2DBoundaryQualityModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant Mesh2DBoundaryQualityModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows[index.row()];
    if (role == ChoicesRole && index.column() == SpeciesColumn)
        return m_species;
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    switch (index.column()) {
    case CellColumn:
        return row.cell;
    case EdgeColumn:
        return row.edge;
    case SpeciesColumn:
        return row.species;
    case ConcentrationColumn:
        return row.concentration;
    default:
        return {};
    }
}

bool Mesh2DBoundaryQualityModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || index.row() >= m_rows.size())
        return false;
    Row &row = m_rows[index.row()];
    bool ok = true;
    switch (index.column()) {
    case CellColumn:
        row.cell = value.toInt(&ok);
        break;
    case EdgeColumn:
        row.edge = value.toInt(&ok);
        break;
    case SpeciesColumn:
        row.species = value.toString().trimmed();
        break;
    case ConcentrationColumn:
        row.concentration = value.toDouble(&ok);
        break;
    default:
        return false;
    }
    if (!ok)
        return false;
    emit dataChanged(index, index);
    return true;
}

Qt::ItemFlags Mesh2DBoundaryQualityModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
}

QVariant Mesh2DBoundaryQualityModel::headerData(int section, Qt::Orientation orientation,
                                                int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);
    switch (section) {
    case CellColumn:
        return tr("Cell");
    case EdgeColumn:
        return tr("Edge");
    case SpeciesColumn:
        return tr("Species");
    case ConcentrationColumn:
        return tr("Concentration");
    default:
        return {};
    }
}

} // namespace openswmmvis::ui
