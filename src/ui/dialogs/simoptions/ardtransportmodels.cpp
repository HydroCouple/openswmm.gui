/**
 * @file      ardtransportmodels.cpp
 * @brief     Staged Eulerian ARD transport rows for the Simulation Options Quality page (review
 *            G-02).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/simoptions/ardtransportmodels.h"

#include "ui/dialogs/mesh2dsectionrowmodel.h"

#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_reactions.h>
#include <openswmm/engine/openswmm_transport.h>

#include <QLocale>
#include <QPair>
#include <QSet>

#include <cmath>

namespace openswmmvis::ui
{

namespace
{
constexpr int kNameLen = 256;

static_assert(ArdTransportRowModel::ChoicesRole == Mesh2DSectionRowModel::ChoicesRole &&
                  ArdConduitDispersionModel::ChoicesRole == Mesh2DSectionRowModel::ChoicesRole,
              "Mesh2DChoiceDelegate reads Mesh2DSectionRowModel::ChoicesRole");

/** @return True while the engine accepts configuration edits (BUILDING or OPENED). */
bool canEdit(SWMM_Engine engine)
{
    int state = SWMM_STATE_CREATED;
    return engine && swmm_engine_get_state(engine, &state) == SWMM_OK &&
           (state == SWMM_STATE_BUILDING || state == SWMM_STATE_OPENED);
}

/** @return @p value as C-locale text with every digit kept. */
QString numberText(double value)
{
    return QLocale::c().toString(value, 'g', QLocale::FloatingPointShortest);
}

/** @return @p value as a number: a double as is, text parsed in the C locale. */
bool parseNumber(const QVariant &value, double *out)
{
    bool ok = false;
    *out = value.typeId() == QMetaType::Double ? value.toDouble(&ok)
                                               : QLocale::c().toDouble(value.toString().trimmed(), &ok);
    return ok;
}

QStringList nodeNames(SWMM_Engine engine)
{
    QStringList names;
    for (int i = 0; engine && i < swmm_node_count(engine); ++i)
        if (const char *id = swmm_node_id(engine, i))
            names << QString::fromUtf8(id);
    return names;
}
} // namespace

QStringList ardConduitNames(SWMM_Engine engine)
{
    QStringList names;
    for (int i = 0; engine && i < swmm_link_count(engine); ++i) {
        int type = -1;
        if (swmm_link_get_type(engine, i, &type) == SWMM_OK && type == SWMM_LINK_CONDUIT)
            if (const char *id = swmm_link_id(engine, i))
                names << QString::fromUtf8(id);
    }
    return names;
}

QStringList reactionSpeciesNames(SWMM_Engine engine)
{
    QStringList names;
    for (int i = 0; engine && i < swmm_reaction_species_count(engine); ++i) {
        // Every output is required by the getter.
        char name[kNameLen]{}, units[kNameLen]{};
        int isWall = 0;
        double atol = 0.0, rtol = 0.0;
        if (swmm_reaction_species_get(engine, i, name, sizeof name, &isWall, units, sizeof units,
                                      &atol, &rtol) == SWMM_OK &&
            name[0])
            names << QString::fromUtf8(name);
    }
    return names;
}

// ---------------------------------------------------------------------------
// Conduit dispersion
// ---------------------------------------------------------------------------

ArdConduitDispersionModel::ArdConduitDispersionModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

QString ArdConduitDispersionModel::load(SWMM_Engine engine)
{
    beginResetModel();
    m_rows.clear();
    m_conduits = ardConduitNames(engine);
    QString error;
    int count = 0;
    if (engine && swmm_transport_conduit_disp_count(engine, &count) != SWMM_OK)
        error = tr("Cannot read the conduit dispersion rows.");
    for (int i = 0; error.isEmpty() && i < count; ++i) {
        int link = -1;
        double value = 0.0;
        if (swmm_transport_get_conduit_disp(engine, i, &link, &value) != SWMM_OK) {
            error = tr("Cannot read conduit dispersion row %1.").arg(i + 1);
            break;
        }
        const char *id = swmm_link_id(engine, link);
        m_rows.append({id ? QString::fromUtf8(id) : QString(), value});
    }
    m_loaded = m_rows;
    endResetModel();
    return error;
}

void ArdConduitDispersionModel::setUnits(const QString &units)
{
    m_units = units;
    emit headerDataChanged(Qt::Horizontal, CoefficientColumn, CoefficientColumn);
}

void ArdConduitDispersionModel::addRow(const Row &row)
{
    const int at = int(m_rows.size());
    beginInsertRows({}, at, at);
    m_rows.append(row);
    endInsertRows();
}

void ArdConduitDispersionModel::removeRowAt(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
}

QString ArdConduitDispersionModel::validateCommit(SWMM_Engine engine) const
{
    if (!isDirty())
        return {};
    if (!canEdit(engine))
        return tr("Conduit dispersion can be changed only while no simulation is running.");
    const QStringList conduitList = ardConduitNames(engine);
    const QSet<QString> conduits(conduitList.cbegin(), conduitList.cend());
    QSet<QString> seen;
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &r = m_rows[i];
        if (!conduits.contains(r.conduit))
            return tr("Conduit dispersion row %1: '%2' is not a conduit of the model.")
                .arg(i + 1)
                .arg(r.conduit);
        if (!std::isfinite(r.coefficient) || r.coefficient < 0.0)
            return tr("Conduit dispersion row %1: the coefficient must be a number that is not "
                      "negative.")
                .arg(i + 1);
        if (seen.contains(r.conduit))
            return tr("Conduit dispersion row %1: '%2' already has a row.").arg(i + 1).arg(r.conduit);
        seen.insert(r.conduit);
    }
    return {};
}

QString ArdConduitDispersionModel::replaceRows(SWMM_Engine engine, const QList<Row> &rows) const
{
    int count = 0;
    if (swmm_transport_conduit_disp_count(engine, &count) != SWMM_OK)
        return tr("Cannot read the conduit dispersion rows.");
    for (int i = count - 1; i >= 0; --i)
        if (swmm_transport_remove_conduit_disp(engine, i) != SWMM_OK)
            return tr("The engine refused to remove conduit dispersion row %1.").arg(i + 1);
    for (const Row &r : rows) {
        const int link = swmm_link_index(engine, r.conduit.toUtf8().constData());
        const int rc = swmm_transport_set_conduit_disp(engine, link, r.coefficient);
        if (rc != SWMM_OK)
            return tr("The engine refused the dispersion of '%1' (error %2).").arg(r.conduit).arg(rc);
    }
    return {};
}

QString ArdConduitDispersionModel::commit(SWMM_Engine engine)
{
    if (!isDirty())
        return {};
    const QString error = replaceRows(engine, m_rows);
    if (!error.isEmpty())
        replaceRows(engine, m_loaded);
    return error;
}

int ArdConduitDispersionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int ArdConduitDispersionModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ArdConduitDispersionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows[index.row()];
    if (role == ChoicesRole)
        return index.column() == ConduitColumn ? QVariant(m_conduits) : QVariant();
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    return index.column() == ConduitColumn ? QVariant(r.conduit)
                                           : QVariant(numberText(r.coefficient));
}

bool ArdConduitDispersionModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || index.row() >= m_rows.size())
        return false;
    Row &r = m_rows[index.row()];
    if (index.column() == ConduitColumn) {
        r.conduit = value.toString().trimmed();
    } else {
        double v = 0.0;
        if (!parseNumber(value, &v))
            return false;
        r.coefficient = v;
    }
    emit dataChanged(index, index);
    return true;
}

Qt::ItemFlags ArdConduitDispersionModel::flags(const QModelIndex &index) const
{
    return index.isValid() ? Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable
                           : Qt::NoItemFlags;
}

QVariant ArdConduitDispersionModel::headerData(int section, Qt::Orientation orientation,
                                               int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);
    if (section == ConduitColumn)
        return tr("Conduit");
    if (section == CoefficientColumn)
        return m_units.isEmpty() ? tr("Coefficient") : tr("Coefficient (%1)").arg(m_units);
    return {};
}

// ---------------------------------------------------------------------------
// Boundaries and sources
// ---------------------------------------------------------------------------

ArdTransportRowModel::ArdTransportRowModel(Kind kind, QObject *parent)
    : QAbstractTableModel(parent), m_kind(kind)
{
}

QStringList ArdTransportRowModel::currentElements(SWMM_Engine engine) const
{
    return m_kind == Kind::Boundary ? nodeNames(engine) : ardConduitNames(engine);
}

QString ArdTransportRowModel::load(SWMM_Engine engine)
{
    beginResetModel();
    m_rows.clear();
    m_elements = currentElements(engine);
    m_species = reactionSpeciesNames(engine);
    m_series = engine ? timeSeriesNames(engine) : QStringList();
    const bool boundary = m_kind == Kind::Boundary;
    QString error;
    int count = 0;
    if (engine && (boundary ? swmm_transport_boundary_count(engine, &count)
                            : swmm_transport_source_count(engine, &count)) != SWMM_OK)
        error = boundary ? tr("Cannot read the transport boundaries.")
                         : tr("Cannot read the transport sources.");
    for (int i = 0; error.isEmpty() && i < count; ++i) {
        char element[kNameLen]{}, species[kNameLen]{}, series[kNameLen]{};
        int isSeries = 0;
        double value = 0.0;
        const int rc =
            boundary ? swmm_transport_get_boundary(engine, i, element, sizeof element, species,
                                                   sizeof species, &isSeries, &value, series,
                                                   sizeof series)
                     : swmm_transport_get_source(engine, i, element, sizeof element, species,
                                                 sizeof species, &isSeries, &value, series,
                                                 sizeof series);
        if (rc != SWMM_OK) {
            error = tr("Cannot read transport row %1.").arg(i + 1);
            break;
        }
        m_rows.append({QString::fromUtf8(element), QString::fromUtf8(species), isSeries != 0,
                       value, QString::fromUtf8(series)});
    }
    m_loaded = m_rows;
    endResetModel();
    return error;
}

void ArdTransportRowModel::addRow(const Row &row)
{
    const int at = int(m_rows.size());
    beginInsertRows({}, at, at);
    m_rows.append(row);
    endInsertRows();
}

void ArdTransportRowModel::removeRowAt(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
}

QString ArdTransportRowModel::validateCommit(SWMM_Engine engine) const
{
    if (!isDirty())
        return {};
    const bool boundary = m_kind == Kind::Boundary;
    if (!canEdit(engine))
        return boundary ? tr("Transport boundaries can be changed only while no simulation is "
                             "running.")
                        : tr("Transport sources can be changed only while no simulation is "
                             "running.");
    const QStringList elementList = currentElements(engine);
    const QStringList speciesList = reactionSpeciesNames(engine);
    const QStringList seriesList = timeSeriesNames(engine);
    const QSet<QString> elements(elementList.cbegin(), elementList.cend());
    const QSet<QString> species(speciesList.cbegin(), speciesList.cend());
    const QSet<QString> series(seriesList.cbegin(), seriesList.cend());
    QSet<QPair<QString, QString>> seen;
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &r = m_rows[i];
        const QString where = boundary ? tr("Boundary row %1").arg(i + 1)
                                       : tr("Source row %1").arg(i + 1);
        if (!elements.contains(r.element))
            return boundary ? tr("%1: '%2' is not a node of the model.").arg(where, r.element)
                            : tr("%1: '%2' is not a conduit of the model.").arg(where, r.element);
        if (!species.contains(r.species))
            return tr("%1: '%2' is not a Reaction System species. Transport boundaries and "
                      "sources apply to those species; pollutants enter through inflows.")
                .arg(where, r.species);
        if (r.series) {
            if (!series.contains(r.timeSeries))
                return tr("%1: choose a time series of the model.").arg(where);
        } else if (!std::isfinite(r.value)) {
            return tr("%1: the value must be a number.").arg(where);
        } else if (boundary && r.value < 0.0) {
            return tr("%1: a concentration cannot be negative.").arg(where);
        }
        if (seen.contains({r.element, r.species}))
            return tr("%1: '%2' already has a row for '%3'.").arg(where, r.element, r.species);
        seen.insert({r.element, r.species});
    }
    return {};
}

QString ArdTransportRowModel::replaceRows(SWMM_Engine engine, const QList<Row> &rows) const
{
    const bool boundary = m_kind == Kind::Boundary;
    int count = 0;
    if ((boundary ? swmm_transport_boundary_count(engine, &count)
                  : swmm_transport_source_count(engine, &count)) != SWMM_OK)
        return tr("Cannot read the transport rows.");
    for (int i = count - 1; i >= 0; --i)
        if ((boundary ? swmm_transport_remove_boundary(engine, i)
                      : swmm_transport_remove_source(engine, i)) != SWMM_OK)
            return tr("The engine refused to remove transport row %1.").arg(i + 1);
    for (const Row &r : rows) {
        const QByteArray element = r.element.toUtf8(), species = r.species.toUtf8(),
                         series = r.timeSeries.toUtf8();
        const int isSeries = r.series ? 1 : 0;
        const double value = r.series ? 0.0 : r.value;
        const char *ts = r.series ? series.constData() : nullptr;
        const int rc = boundary ? swmm_transport_set_boundary(engine, element.constData(),
                                                              species.constData(), isSeries,
                                                              value, ts)
                                : swmm_transport_set_source(engine, element.constData(),
                                                            species.constData(), isSeries, value,
                                                            ts);
        if (rc != SWMM_OK)
            return tr("The engine refused the row for '%1' and '%2' (error %3).")
                .arg(r.element, r.species)
                .arg(rc);
    }
    return {};
}

QString ArdTransportRowModel::commit(SWMM_Engine engine)
{
    if (!isDirty())
        return {};
    const QString error = replaceRows(engine, m_rows);
    if (!error.isEmpty())
        replaceRows(engine, m_loaded);
    return error;
}

int ArdTransportRowModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int ArdTransportRowModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ArdTransportRowModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows[index.row()];
    if (role == ChoicesRole) {
        switch (index.column()) {
        case ElementColumn:
            return m_elements;
        case SpeciesColumn:
            return m_species;
        case ValueFromColumn:
            return QStringList{tr("Constant"), tr("Time series")};
        case SeriesColumn:
            return m_series;
        default:
            return {};
        }
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    switch (index.column()) {
    case ElementColumn:
        return r.element;
    case SpeciesColumn:
        return r.species;
    case ValueFromColumn:
        return r.series ? tr("Time series") : tr("Constant");
    case ValueColumn:
        return r.series ? QString() : numberText(r.value);
    case SeriesColumn:
        return r.series ? r.timeSeries : QString();
    default:
        return {};
    }
}

bool ArdTransportRowModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || index.row() >= m_rows.size())
        return false;
    Row &r = m_rows[index.row()];
    switch (index.column()) {
    case ElementColumn:
        r.element = value.toString().trimmed();
        break;
    case SpeciesColumn:
        r.species = value.toString().trimmed();
        break;
    case ValueFromColumn: {
        const QString text = value.toString().trimmed();
        if (text == tr("Time series")) {
            r.series = true;
            r.value = 0.0; // what the engine stores for a series row
        } else if (text == tr("Constant")) {
            r.series = false;
            r.timeSeries.clear();
        } else {
            return false;
        }
        emit dataChanged(this->index(index.row(), ValueFromColumn),
                         this->index(index.row(), SeriesColumn));
        return true;
    }
    case ValueColumn: {
        double v = 0.0;
        if (r.series || !parseNumber(value, &v))
            return false;
        r.value = v;
        break;
    }
    case SeriesColumn:
        if (!r.series)
            return false;
        r.timeSeries = value.toString().trimmed();
        break;
    default:
        return false;
    }
    emit dataChanged(index, index);
    return true;
}

Qt::ItemFlags ArdTransportRowModel::flags(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return Qt::NoItemFlags;
    const Row &r = m_rows[index.row()];
    const bool editable = (index.column() != ValueColumn || !r.series) &&
                          (index.column() != SeriesColumn || r.series);
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled | (editable ? Qt::ItemIsEditable
                                                                : Qt::NoItemFlags);
}

QVariant ArdTransportRowModel::headerData(int section, Qt::Orientation orientation,
                                          int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);
    const bool boundary = m_kind == Kind::Boundary;
    switch (section) {
    case ElementColumn:
        return boundary ? tr("Node") : tr("Conduit");
    case SpeciesColumn:
        return tr("Species");
    case ValueFromColumn:
        return tr("Value from");
    case ValueColumn:
        return boundary ? tr("Concentration") : tr("Rate (mass/s)");
    case SeriesColumn:
        return tr("Time series");
    default:
        return {};
    }
}

} // namespace openswmmvis::ui
