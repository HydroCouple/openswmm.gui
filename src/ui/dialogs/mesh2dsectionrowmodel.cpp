/**
 * @file      mesh2dsectionrowmodel.cpp
 * @brief     Staged table model for the scoped 2D surface-quality and groundwater-transport
 *            authoring sections.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/mesh2dsectionrowmodel.h"

#include "assignment/groundwatertransportassignment.h"

#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_gw_transport.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_quality.h>
#include <openswmm/engine/openswmm_reactions.h>
#include <openswmm/engine/openswmm_sq2d.h>
#include <openswmm/engine/openswmm_tables.h>

#include <QByteArray>
#include <QLocale>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace openswmmvis::ui
{

namespace
{

constexpr double kBlank = std::numeric_limits<double>::quiet_NaN();
constexpr int kTextLen = 4096;

/** True for a BUILDING or OPENED engine, the only states the row setters accept. */
bool editable(SWMM_Engine engine)
{
    int state = SWMM_STATE_CREATED;
    return engine && swmm_engine_get_state(engine, &state) == SWMM_OK &&
           (state == SWMM_STATE_BUILDING || state == SWMM_STATE_OPENED);
}

/** True when an [OPTIONS] key reads back exactly YES. */
bool optionOn(SWMM_Engine engine, const char *key)
{
    char value[32]{};
    return swmm_options_get(engine, key, value, sizeof value) == SWMM_OK &&
           std::strcmp(value, "YES") == 0;
}

/** Shortest text that parses back to the same double (blank for NaN). */
QString numberText(double value)
{
    return std::isnan(value) ? QString()
                             : QString::number(value, 'g', QLocale::FloatingPointShortest);
}

/** Exact equality that treats two NaNs (blank optional values) as equal. */
bool sameValues(const QVector<double> &a, const QVector<double> &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i)
        if (!(a[i] == b[i] || (std::isnan(a[i]) && std::isnan(b[i]))))
            return false;
    return true;
}

QStringList scopeLabels()
{
    return {QObject::tr("All cells"), QObject::tr("Tag"), QObject::tr("Cell")};
}

/**
 * @brief Surface species registry: pollutants, then aqueous reaction species, then the reserved
 * water-age / temperature names when that transport is on and @p reserved is set.
 */
QStringList surfaceSpecies(SWMM_Engine engine, bool reserved)
{
    QStringList names;
    for (int i = 0; i < swmm_pollutant_count(engine); ++i)
        if (const char *id = swmm_pollutant_id(engine, i))
            names << QString::fromUtf8(id);
    for (int i = 0; i < swmm_reaction_species_count(engine); ++i) {
        char name[kTextLen]{}, units[kTextLen]{};
        int wall = 0;
        double atol = 0.0, rtol = 0.0;
        if (swmm_reaction_species_get(engine, i, name, sizeof name, &wall, units, sizeof units,
                                      &atol, &rtol) == SWMM_OK &&
            !wall && name[0] && !names.contains(QString::fromUtf8(name)))
            names << QString::fromUtf8(name);
    }
    if (reserved && optionOn(engine, "WATER_AGE"))
        names << QStringLiteral("__WATER_AGE__");
    if (reserved && optionOn(engine, "HEAT_TRANSPORT"))
        names << QStringLiteral("__TEMPERATURE__");
    return names;
}

} // namespace

QStringList surfaceSpeciesNames(SWMM_Engine engine, bool reserved)
{
    return engine ? surfaceSpecies(engine, reserved) : QStringList();
}

QStringList meshCellTags(SWMM_Engine engine)
{
    int cells = 0;
    if (!engine || swmm_2d_cell_count(engine, &cells) != SWMM_OK)
        return {};
    QSet<QString> tags;
    for (int c = 0; c < cells; ++c) {
        char tag[kTextLen]{};
        if (swmm_2d_get_triangle_tag(engine, c, tag, sizeof tag) != SWMM_OK)
            break;
        if (tag[0])
            tags.insert(QString::fromUtf8(tag));
    }
    QStringList ordered = tags.values();
    std::sort(ordered.begin(), ordered.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return ordered;
}

QStringList timeSeriesNames(SWMM_Engine engine)
{
    QStringList names;
    if (!engine)
        return names;
    for (int i = 0; i < swmm_table_count(engine); ++i) {
        int type = -1;
        if (swmm_table_get_type(engine, i, &type) == SWMM_OK && type == 0)
            if (const char *id = swmm_table_id(engine, i))
                names << QString::fromUtf8(id);
    }
    return names;
}

bool Mesh2DSectionRowModel::Row::operator==(const Row &o) const
{
    return scope == o.scope && tag == o.tag && cell == o.cell && name == o.name &&
           zone == o.zone && layer == o.layer && percents == o.percents &&
           sameValues(values, o.values);
}

Mesh2DSectionRowModel::Mesh2DSectionRowModel(Section section, QObject *parent)
    : QAbstractTableModel(parent), m_section(section)
{
    rebuildColumns();
}

QString Mesh2DSectionRowModel::sectionName() const
{
    switch (m_section) {
    case Section::Coverages:
        return QStringLiteral("[2D_COVERAGES]");
    case Section::Loadings:
        return QStringLiteral("[2D_LOADINGS]");
    case Section::CurbLength:
        return QStringLiteral("[2D_CURB_LENGTH]");
    case Section::SurfaceInitialQuality:
        return QStringLiteral("[2D_INITIAL_QUALITY]");
    case Section::GwParams:
        return QStringLiteral("[GW_TRANSPORT_PARAMS]");
    case Section::GwSorption:
        return QStringLiteral("[GW_SORPTION]");
    case Section::GwInitialQuality:
        return QStringLiteral("[GW_INITIAL_QUALITY]");
    case Section::AquiferLinks:
        return QStringLiteral("[2D_AQUIFER_LINKS]");
    }
    return {};
}

void Mesh2DSectionRowModel::rebuildColumns()
{
    const QString length = m_si ? QStringLiteral("m") : QStringLiteral("ft");
    m_columns.clear();
    if (scoped()) {
        m_columns.append({Kind::Scope, tr("Scope"), -1, {}, false, false,
                          tr("All cells (*), every cell carrying a tag, or one cell. More specific "
                             "rows override broader ones.")});
        m_columns.append({Kind::Target, tr("Tag / cell"), -1, {}, false, false,
                          tr("A mesh cell tag for Tag rows, a 1-based cell number for Cell "
                             "rows.")});
    }
    auto value = [this](const QString &header, int index, const QString &tip,
                        bool optional = false, bool signedValue = false) {
        m_columns.append({Kind::Value, header, index, {}, optional, signedValue, tip});
    };
    switch (m_section) {
    case Section::Coverages:
        for (const QString &use : m_landUses)
            m_columns.append({Kind::Percent, tr("%1 (%)").arg(use), -1, use, true, false,
                              tr("Percent of the cell area covered by %1. Leave blank when the "
                                 "land use is not part of this row; a row's percents add to at "
                                 "most 100.")
                                  .arg(use)});
        break;
    case Section::Loadings:
        m_columns.append({Kind::Name, tr("Species"), -1, {}, false, false,
                          tr("A pollutant or reaction species that builds up.")});
        value(tr("Initial buildup (%1)")
                  .arg(m_si ? QStringLiteral("kg/ha") : QStringLiteral("lb/ac")),
              0,
              tr("Buildup on the cell's land at the start of the run, divided among its covered "
                 "land uses."));
        break;
    case Section::CurbLength:
        value(tr("Curb length (%1)").arg(length), 0,
              tr("Curb length on the cell, for land uses whose buildup is per curb length."));
        break;
    case Section::SurfaceInitialQuality:
        m_columns.append({Kind::Name, tr("Species"), -1, {}, false, false,
                          tr("A pollutant, reaction species, water age or temperature.")});
        value(tr("Initial concentration"), 0,
              tr("Concentration in the species' units in the water standing on the cell at the "
                 "start; water age in hours, temperature in °C."));
        break;
    case Section::GwParams:
        value(tr("Grain density (kg/m³)"), 0, tr("Default 2650."));
        value(tr("Grain specific heat (J/kg/K)"), 1, tr("Default 880."));
        value(tr("Grain conductivity (W/m/K)"), 2, tr("Default 2.0."));
        value(tr("Specific surface (m²/kg)"), 3, tr("Default 0."));
        value(tr("Longitudinal dispersivity (m)"), 4, tr("Default 1.0."));
        value(tr("Transverse dispersivity (m)"), 5, tr("Default 0.1."));
        value(tr("Molecular diffusivity (m²/s)"), 6, tr("Default 1e-9."));
        value(tr("Vapor diffusivity (m²/s)"), 7, tr("Default 1e-9."));
        value(tr("Geothermal flux (W/m²)"), 8,
              tr("Default 0.065. The deep temperature in °C when DEEP_THERMAL_BC is FIXED_TEMP. "
                 "The only signed column."),
              false, true);
        break;
    case Section::GwSorption:
        m_columns.append({Kind::Name, tr("Species"), -1, {}, false, false,
                          tr("An enabled groundwater species.")});
        value(tr("Kd (L/kg)"), 0, tr("Linear partition coefficient."));
        value(tr("Decay (1/day)"), 1,
              tr("First-order decay. Leave blank to use the pollutant's [POLLUTANTS] decay."),
              true);
        break;
    case Section::GwInitialQuality:
        m_columns.append({Kind::Zone, tr("Zone"), -1, {}, false, false,
                          tr("SAT is the saturated zone, UNSAT the unsaturated zone.")});
        m_columns.append({Kind::Name, tr("Species"), -1, {}, false, false,
                          tr("An enabled groundwater species.")});
        value(tr("Initial value"), 0,
              tr("Concentration in the species' units; water age in seconds and temperature in "
                 "°C (only those two may be negative)."),
              false, true);
        break;
    case Section::AquiferLinks:
        m_columns.append({Kind::Name, tr("Conduit"), -1, {}, false, false,
                          tr("The conduit whose aquifer exchange this row overrides.")});
        value(tr("KC (%1)").arg(m_si ? QStringLiteral("mm/hr") : QStringLiteral("in/hr")), 0,
              tr("Bed conductivity for TWO_WAY seepage; 0 uses the conduit's [LOSSES] seepage "
                 "rate."));
        value(tr("DC (%1)").arg(length), 1,
              tr("Bed thickness for TWO_WAY seepage; 0 uses the conduit's full depth."));
        m_columns.append({Kind::Flag, tr("Exchange"), 2, {}, false, false,
                          tr("Clear to keep the conduit's seepage a system loss (EXCHANGE NO).")});
        break;
    }
}

int Mesh2DSectionRowModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int Mesh2DSectionRowModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_columns.size());
}

QStringList Mesh2DSectionRowModel::nameChoices(const Row &row) const
{
    QStringList names = m_names;
    if (!row.name.isEmpty() && !names.contains(row.name))
        names << row.name;
    return names;
}

QVariant Mesh2DSectionRowModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size() || index.column() >= m_columns.size())
        return {};
    const Row &r = m_rows[index.row()];
    const Column &c = m_columns[index.column()];
    if (role == Qt::ToolTipRole)
        return c.tip;
    if (role == ChoicesRole) {
        switch (c.kind) {
        case Kind::Scope:
            return scopeLabels();
        case Kind::Target:
            if (r.scope == SWMM_SQ2D_SCOPE_TAG) {
                QStringList tags = m_tags;
                if (!r.tag.isEmpty() && !tags.contains(r.tag))
                    tags << r.tag;
                return tags;
            }
            return {};
        case Kind::Name:
            return nameChoices(r);
        case Kind::Zone: {
            QStringList zones{QStringLiteral("SAT"), QStringLiteral("UNSAT")};
            if (r.zone == SWMM_GW_ZONE_LAYER)
                zones << QStringLiteral("LAYER %1").arg(r.layer);
            return zones;
        }
        default:
            return {};
        }
    }
    if (c.kind == Kind::Flag) {
        if (role == Qt::CheckStateRole)
            return r.values.value(c.value) != 0.0 ? Qt::Checked : Qt::Unchecked;
        return {};
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    switch (c.kind) {
    case Kind::Scope:
        return scopeLabels().value(r.scope);
    case Kind::Target:
        if (r.scope == SWMM_SQ2D_SCOPE_TAG)
            return r.tag;
        if (r.scope == SWMM_SQ2D_SCOPE_CELL)
            return r.cell >= 0 ? QString::number(r.cell + 1) : QString();
        return role == Qt::EditRole ? QString() : QStringLiteral("—");
    case Kind::Name:
        return r.name;
    case Kind::Zone:
        return r.zone == SWMM_GW_ZONE_LAYER     ? QStringLiteral("LAYER %1").arg(r.layer)
               : r.zone == SWMM_GW_ZONE_UNSAT ? QStringLiteral("UNSAT")
                                              : QStringLiteral("SAT");
    case Kind::Value: {
        const double v = r.values.value(c.value, kBlank);
        if (std::isnan(v) && role == Qt::DisplayRole && m_section == Section::GwSorption)
            return tr("(pollutant decay)");
        return numberText(v);
    }
    case Kind::Percent:
        return r.percents.contains(c.landUse) ? numberText(r.percents.value(c.landUse)) : QString();
    case Kind::Flag:
        break;
    }
    return {};
}

bool Mesh2DSectionRowModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() >= m_rows.size() || index.column() >= m_columns.size())
        return false;
    Row &r = m_rows[index.row()];
    const Column &c = m_columns[index.column()];
    if (c.kind == Kind::Flag) {
        if (role != Qt::CheckStateRole && role != Qt::EditRole)
            return false;
        const bool on = role == Qt::CheckStateRole ? value.toInt() == Qt::Checked : value.toBool();
        r.values[c.value] = on ? 1.0 : 0.0;
        emit dataChanged(index, index, {Qt::CheckStateRole});
        return true;
    }
    if (role != Qt::EditRole)
        return false;
    const QString text = value.toString().trimmed();
    switch (c.kind) {
    case Kind::Scope: {
        int scope = scopeLabels().indexOf(text);
        if (scope < 0) {
            bool ok = false;
            scope = text.toInt(&ok);
            if (!ok)
                return false;
        }
        if (scope < SWMM_SQ2D_SCOPE_GLOBAL || scope > SWMM_SQ2D_SCOPE_CELL)
            return false;
        r.scope = scope;
        emit dataChanged(this->index(index.row(), 0), this->index(index.row(), 1));
        return true;
    }
    case Kind::Target:
        if (r.scope == SWMM_SQ2D_SCOPE_TAG) {
            if (text.isEmpty())
                return false;
            r.tag = text;
        } else if (r.scope == SWMM_SQ2D_SCOPE_CELL) {
            bool ok = false;
            const int cell = text.toInt(&ok);
            if (!ok || cell < 1)
                return false;
            r.cell = cell - 1;
        } else {
            return false;
        }
        break;
    case Kind::Name:
        if (text.isEmpty())
            return false;
        r.name = text;
        break;
    case Kind::Zone: {
        const QString zone = text.toUpper();
        if (zone == QLatin1String("SAT") || zone == QLatin1String("UNSAT")) {
            r.zone = zone == QLatin1String("SAT") ? SWMM_GW_ZONE_SAT : SWMM_GW_ZONE_UNSAT;
            r.layer = -1;
        } else {
            const QStringList parts = zone.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            bool ok = false;
            const int layer = parts.size() == 2 ? parts[1].toInt(&ok) : 0;
            if (parts.size() != 2 || parts[0] != QLatin1String("LAYER") || !ok || layer < 1)
                return false;
            r.zone = SWMM_GW_ZONE_LAYER;
            r.layer = layer;
        }
        break;
    }
    case Kind::Value:
    case Kind::Percent: {
        double v = kBlank;
        if (!text.isEmpty()) {
            bool ok = false;
            v = text.toDouble(&ok);
            if (!ok || !std::isfinite(v) || (!c.signedValue && v < 0.0) ||
                (c.kind == Kind::Percent && v > 100.0))
                return false;
        } else if (!c.optional) {
            return false;
        }
        if (c.kind == Kind::Percent) {
            if (std::isnan(v))
                r.percents.remove(c.landUse);
            else
                r.percents[c.landUse] = v;
        } else {
            r.values[c.value] = v;
        }
        break;
    }
    case Kind::Flag:
        return false;
    }
    emit dataChanged(index, index);
    return true;
}

QVariant Mesh2DSectionRowModel::headerData(int section, Qt::Orientation orientation,
                                           int role) const
{
    if (orientation == Qt::Vertical)
        return role == Qt::DisplayRole ? QVariant(section + 1) : QVariant();
    if (section < 0 || section >= m_columns.size())
        return {};
    if (role == Qt::DisplayRole)
        return m_columns[section].header;
    if (role == Qt::ToolTipRole)
        return m_columns[section].tip;
    return {};
}

Qt::ItemFlags Mesh2DSectionRowModel::flags(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= m_rows.size() || index.column() >= m_columns.size())
        return Qt::NoItemFlags;
    const Column &c = m_columns[index.column()];
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (c.kind == Kind::Flag)
        return f | Qt::ItemIsUserCheckable;
    if (c.kind == Kind::Target && m_rows[index.row()].scope == SWMM_SQ2D_SCOPE_GLOBAL)
        return f;
    return f | Qt::ItemIsEditable;
}

bool Mesh2DSectionRowModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid() || row < 0 || count <= 0 || row + count > m_rows.size())
        return false;
    beginRemoveRows(QModelIndex(), row, row + count - 1);
    m_rows.erase(m_rows.begin() + row, m_rows.begin() + row + count);
    endRemoveRows();
    return true;
}

int Mesh2DSectionRowModel::appendRow()
{
    Row r;
    switch (m_section) {
    case Section::Coverages:
        if (!m_landUses.isEmpty())
            r.percents[m_landUses.first()] = 100.0;
        break;
    case Section::GwParams:
        r.values = {2650.0, 880.0, 2.0, 0.0, 1.0, 0.1, 1.0e-9, 1.0e-9, 0.065};
        break;
    case Section::GwSorption:
        r.values = {0.0, kBlank};
        break;
    case Section::AquiferLinks:
        r.values = {0.0, 0.0, 1.0};
        break;
    default:
        r.values = {0.0};
        break;
    }
    if (m_section != Section::Coverages && m_section != Section::CurbLength &&
        m_section != Section::GwParams)
        r.name = m_names.value(0);
    const int at = int(m_rows.size());
    beginInsertRows(QModelIndex(), at, at);
    m_rows.append(r);
    endInsertRows();
    return at;
}

void Mesh2DSectionRowModel::setTagChoices(const QStringList &tags)
{
    m_tags = tags;
}

void Mesh2DSectionRowModel::setProjectUnits(bool si)
{
    if (si == m_si)
        return;
    m_si = si;
    rebuildColumns();
    if (!m_columns.isEmpty())
        emit headerDataChanged(Qt::Horizontal, 0, int(m_columns.size()) - 1);
}

Mesh2DSectionRowModel::Row Mesh2DSectionRowModel::normalized(Row row) const
{
    if (!scoped())
        row.scope = SWMM_SQ2D_SCOPE_GLOBAL;
    if (row.scope != SWMM_SQ2D_SCOPE_TAG)
        row.tag.clear();
    if (row.scope != SWMM_SQ2D_SCOPE_CELL)
        row.cell = -1;
    if (m_section != Section::GwInitialQuality) {
        row.zone = 0;
        row.layer = -1;
    } else if (row.zone != SWMM_GW_ZONE_LAYER) {
        row.layer = -1;
    }
    return row;
}

QList<Mesh2DSectionRowModel::Row> Mesh2DSectionRowModel::normalized(const QList<Row> &rows) const
{
    QList<Row> out;
    out.reserve(rows.size());
    for (const Row &row : rows)
        out.append(normalized(row));
    return out;
}

QString Mesh2DSectionRowModel::key(const Row &row) const
{
    const Row r = normalized(row);
    const QString target = r.scope == SWMM_SQ2D_SCOPE_TAG    ? r.tag
                           : r.scope == SWMM_SQ2D_SCOPE_CELL ? QString::number(r.cell)
                                                             : QString();
    switch (m_section) {
    case Section::Coverages:
    case Section::CurbLength:
    case Section::GwParams:
        return QStringLiteral("%1|%2").arg(r.scope).arg(target);
    case Section::GwInitialQuality:
        return QStringLiteral("%1|%2|%3|%4|%5")
            .arg(r.scope)
            .arg(target)
            .arg(r.zone)
            .arg(r.layer)
            .arg(r.name.toCaseFolded());
    case Section::AquiferLinks:
        return r.name.toCaseFolded();
    default:
        return QStringLiteral("%1|%2|%3").arg(r.scope).arg(target, r.name.toCaseFolded());
    }
}

QString Mesh2DSectionRowModel::rowError(const Row &r) const
{
    if (scoped()) {
        if (r.scope == SWMM_SQ2D_SCOPE_TAG && r.tag.trimmed().isEmpty())
            return tr("choose a mesh tag");
        if (r.scope == SWMM_SQ2D_SCOPE_CELL) {
            if (r.cell < 0)
                return tr("enter a cell number");
            if (m_cellCount > 0 && r.cell >= m_cellCount)
                return tr("cell %1 is beyond the mesh (%2 cells)").arg(r.cell + 1).arg(m_cellCount);
        }
    }
    for (const Column &c : m_columns) {
        if (c.kind == Kind::Name && r.name.trimmed().isEmpty())
            return tr("choose a %1").arg(c.header.toLower());
        if (c.kind != Kind::Value)
            continue;
        const double v = r.values.value(c.value, kBlank);
        if (std::isnan(v)) {
            if (!c.optional)
                return tr("enter %1").arg(c.header);
        } else if (!std::isfinite(v) || (!c.signedValue && v < 0.0)) {
            return tr("%1 must be a finite number of at least 0").arg(c.header);
        }
    }
    if (m_section == Section::Coverages) {
        if (r.percents.isEmpty())
            return tr("give at least one land use a percent");
        double sum = 0.0;
        for (double p : r.percents)
            sum += p;
        if (sum > 100.0 + 1e-9)
            return tr("the percents add to %1, more than 100").arg(sum);
    }
    if (m_section == Section::GwInitialQuality) {
        const double v = r.values.value(0, kBlank);
        if (v < 0.0 && r.name != QLatin1String("__TEMPERATURE__") &&
            r.name != QLatin1String("__WATER_AGE__"))
            return tr("only water age and temperature may be negative");
        if (r.zone == SWMM_GW_ZONE_LAYER && r.layer < 1)
            return tr("a LAYER row needs a layer of at least 1");
    }
    return {};
}

int Mesh2DSectionRowModel::liveCount(SWMM_Engine e) const
{
    int n = -1;
    switch (m_section) {
    case Section::Coverages:
        return swmm_2d_coverage_count(e, &n) == SWMM_OK ? n : -1;
    case Section::Loadings:
        return swmm_2d_loading_count(e, &n) == SWMM_OK ? n : -1;
    case Section::CurbLength:
        return swmm_2d_curb_length_count(e, &n) == SWMM_OK ? n : -1;
    case Section::SurfaceInitialQuality:
        return swmm_2d_initial_quality_count(e, &n) == SWMM_OK ? n : -1;
    case Section::GwParams:
        return swmm_gw_params_count(e);
    case Section::GwSorption:
        return swmm_gw_sorption_count(e);
    case Section::GwInitialQuality:
        return swmm_gw_init_quality_count(e);
    case Section::AquiferLinks:
        return swmm_gw2d_link_count(e, &n) == SWMM_OK ? n : -1;
    }
    return -1;
}

int Mesh2DSectionRowModel::removeLive(SWMM_Engine e, int index) const
{
    switch (m_section) {
    case Section::Coverages:
        return swmm_2d_coverage_remove(e, index);
    case Section::Loadings:
        return swmm_2d_loading_remove(e, index);
    case Section::CurbLength:
        return swmm_2d_curb_length_remove(e, index);
    case Section::SurfaceInitialQuality:
        return swmm_2d_remove_initial_quality(e, index);
    case Section::GwParams:
        return swmm_gw_params_remove(e, index);
    case Section::GwSorption:
        return swmm_gw_sorption_remove(e, index);
    case Section::GwInitialQuality:
        return swmm_gw_init_quality_remove(e, index);
    case Section::AquiferLinks:
        return swmm_gw2d_link_remove(e, index);
    }
    return SWMM_ERR_BADPARAM;
}

int Mesh2DSectionRowModel::setLive(SWMM_Engine e, const Row &row) const
{
    const Row r = normalized(row);
    const QByteArray tag = r.tag.toUtf8(), name = r.name.toUtf8();
    const char *tagArg = r.scope == SWMM_SQ2D_SCOPE_TAG ? tag.constData() : nullptr;
    const double v0 = r.values.value(0, kBlank);
    switch (m_section) {
    case Section::Coverages: {
        // Land-use column order, so a rewritten row reads like the [LANDUSES] list.
        QList<QByteArray> uses;
        std::vector<const char *> names;
        std::vector<double> percents;
        for (const QString &use : m_landUses)
            if (r.percents.contains(use)) {
                uses.append(use.toUtf8());
                percents.push_back(r.percents.value(use));
            }
        for (const QByteArray &use : uses)
            names.push_back(use.constData());
        return swmm_2d_coverage_set(e, r.scope, tagArg, r.cell, names.data(), percents.data(),
                                    int(percents.size()));
    }
    case Section::Loadings:
        return swmm_2d_loading_set(e, r.scope, tagArg, r.cell, name.constData(), v0);
    case Section::CurbLength:
        return swmm_2d_curb_length_set(e, r.scope, tagArg, r.cell, v0);
    case Section::SurfaceInitialQuality:
        return swmm_2d_set_initial_quality(e, r.scope, tagArg, r.cell, name.constData(), v0);
    case Section::GwParams: {
        SWMM_GwParams p{};
        p.scope = r.scope;
        p.cell = r.cell;
        double *fields[] = {&p.rho_s,   &p.c_s,     &p.lambda_s, &p.a_s,     &p.alpha_L,
                            &p.alpha_T, &p.D_m,     &p.D_v,      &p.geo_flux};
        for (int i = 0; i < 9; ++i)
            *fields[i] = r.values.value(i);
        return swmm_gw_params_set(e, &p, tagArg);
    }
    case Section::GwSorption: {
        const double decay = r.values.value(1, kBlank);
        return swmm_gw_sorption_set(e, r.scope, tagArg, r.cell, name.constData(), v0,
                                    std::isnan(decay) ? -1.0 : decay);
    }
    case Section::GwInitialQuality: {
        assignment::GroundwaterQualityRow q;
        q.scope = r.scope;
        q.tag = r.tag;
        q.cell = r.cell;
        q.zone = r.zone;
        q.layer = r.layer;
        q.species = r.name;
        q.value = v0;
        return assignment::writeGroundwaterQualityRow(e, q) ? SWMM_OK : SWMM_ERR_BADPARAM;
    }
    case Section::AquiferLinks:
        return swmm_gw2d_link_add(e, name.constData(), v0, r.values.value(1),
                                  r.values.value(2) != 0.0 ? 1 : 0);
    }
    return SWMM_ERR_BADPARAM;
}

QString Mesh2DSectionRowModel::read(SWMM_Engine e, QList<Row> &rows, int *fileRows,
                                    QString *file) const
{
    rows.clear();
    if (fileRows)
        *fileRows = 0;
    const QString failed = tr("Cannot read the %1 rows.").arg(sectionName());
    if (m_section == Section::GwInitialQuality) {
        QVector<assignment::GroundwaterQualityRow> quality;
        QString error;
        if (!assignment::readGroundwaterQualityRows(e, &quality, &error))
            return error;
        for (const auto &q : quality) {
            if (q.layer == -2) { // loaded from the FILE reference, owned by that file
                if (fileRows)
                    ++*fileRows;
                continue;
            }
            Row r;
            r.scope = q.scope;
            r.tag = q.tag;
            r.cell = q.cell;
            r.zone = q.zone;
            r.layer = q.layer;
            r.name = q.species;
            r.values = {q.value};
            rows.append(normalized(r));
        }
        if (file) {
            char path[32768]{};
            if (swmm_gw_init_quality_file_get(e, path, sizeof path) != SWMM_OK)
                return failed;
            *file = QString::fromUtf8(path);
        }
        return {};
    }
    const int n = liveCount(e);
    if (n < 0)
        return failed;
    for (int i = 0; i < n; ++i) {
        Row r;
        char tag[kTextLen]{}, name[kTextLen]{};
        double v = 0.0;
        int status = SWMM_OK;
        switch (m_section) {
        case Section::Coverages: {
            int pairs = 0;
            status = swmm_2d_coverage_row_size(e, i, &pairs);
            for (int k = 0; status == SWMM_OK && k < pairs; ++k) {
                status = swmm_2d_coverage_get(e, i, k, &r.scope, tag, sizeof tag, &r.cell, name,
                                              sizeof name, &v);
                r.percents[QString::fromUtf8(name)] = v;
            }
            break;
        }
        case Section::Loadings:
            status = swmm_2d_loading_get(e, i, &r.scope, tag, sizeof tag, &r.cell, name,
                                         sizeof name, &v);
            r.values = {v};
            break;
        case Section::CurbLength:
            status = swmm_2d_curb_length_get(e, i, &r.scope, tag, sizeof tag, &r.cell, &v);
            r.values = {v};
            break;
        case Section::SurfaceInitialQuality:
            status = swmm_2d_get_initial_quality(e, i, &r.scope, tag, sizeof tag, &r.cell, name,
                                                 sizeof name, &v);
            r.values = {v};
            break;
        case Section::GwParams: {
            SWMM_GwParams p{};
            status = swmm_gw_params_get(e, i, &p, tag, sizeof tag);
            r.scope = p.scope;
            r.cell = p.cell;
            r.values = {p.rho_s, p.c_s,     p.lambda_s, p.a_s,     p.alpha_L,
                        p.alpha_T, p.D_m, p.D_v,      p.geo_flux};
            break;
        }
        case Section::GwSorption: {
            double decay = -1.0;
            status = swmm_gw_sorption_get(e, i, &r.scope, tag, sizeof tag, &r.cell, name,
                                          sizeof name, &v, &decay);
            r.values = {v, decay < 0.0 ? kBlank : decay};
            break;
        }
        case Section::AquiferLinks: {
            double kc = 0.0, dc = 0.0;
            int exchange = 1;
            status = swmm_gw2d_link_get(e, i, name, sizeof name, &kc, &dc, &exchange);
            r.values = {kc, dc, exchange ? 1.0 : 0.0};
            break;
        }
        case Section::GwInitialQuality:
            break;
        }
        if (status != SWMM_OK)
            return failed;
        r.tag = QString::fromUtf8(tag);
        r.name = QString::fromUtf8(name);
        if (m_section == Section::Coverages)
            r.name.clear();
        rows.append(normalized(r));
    }
    return {};
}

QString Mesh2DSectionRowModel::write(SWMM_Engine e, const QList<Row> &rows) const
{
    const int n = liveCount(e);
    if (n < 0)
        return tr("Cannot read the %1 rows.").arg(sectionName());
    QSet<int> keep; // [GW_INITIAL_QUALITY] rows that came from its FILE reference
    if (m_section == Section::GwInitialQuality) {
        QVector<assignment::GroundwaterQualityRow> live;
        QString error;
        if (!assignment::readGroundwaterQualityRows(e, &live, &error))
            return error;
        for (int i = 0; i < live.size(); ++i)
            if (live[i].layer == -2)
                keep.insert(i);
    }
    for (int i = n - 1; i >= 0; --i)
        if (!keep.contains(i) && removeLive(e, i) != SWMM_OK)
            return tr("The engine refused to remove a %1 row.").arg(sectionName());
    for (int i = 0; i < rows.size(); ++i)
        if (setLive(e, rows[i]) != SWMM_OK)
            return tr("The engine refused %1 row %2.").arg(sectionName()).arg(i + 1);
    QList<Row> back;
    const QString error = read(e, back);
    if (!error.isEmpty())
        return error;
    if (back != normalized(rows))
        return tr("The engine stored different %1 rows than were applied.").arg(sectionName());
    return {};
}

QString Mesh2DSectionRowModel::load(SWMM_Engine engine)
{
    beginResetModel();
    m_rows.clear();
    m_loaded.clear();
    m_loadError.clear();
    m_file.clear();
    m_fileRows = 0;
    m_cellCount = 0;
    m_names.clear();
    m_landUses.clear();
    if (engine) {
        int cells = 0;
        if (swmm_2d_cell_count(engine, &cells) == SWMM_OK)
            m_cellCount = cells;
        switch (m_section) {
        case Section::Coverages:
            for (int i = 0; i < swmm_landuse_count(engine); ++i)
                if (const char *id = swmm_landuse_id(engine, i))
                    m_landUses << QString::fromUtf8(id);
            break;
        case Section::Loadings:
            m_names = surfaceSpecies(engine, false);
            break;
        case Section::SurfaceInitialQuality:
            m_names = surfaceSpecies(engine, true);
            break;
        case Section::GwSorption:
        case Section::GwInitialQuality:
            for (const auto &s : assignment::groundwaterTransportSpecies(engine))
                m_names << s.id;
            break;
        case Section::AquiferLinks:
            for (int i = 0; i < swmm_link_count(engine); ++i) {
                int type = -1;
                if (swmm_link_get_type(engine, i, &type) == SWMM_OK && type == SWMM_LINK_CONDUIT)
                    if (const char *id = swmm_link_id(engine, i))
                        m_names << QString::fromUtf8(id);
            }
            break;
        default:
            break;
        }
        m_loadError = read(engine, m_loaded, &m_fileRows, &m_file);
        if (m_section == Section::Coverages) // keep land uses the registry no longer lists
            for (const Row &r : m_loaded)
                for (auto it = r.percents.cbegin(); it != r.percents.cend(); ++it)
                    if (!m_landUses.contains(it.key()))
                        m_landUses << it.key();
        m_rows = m_loaded;
    }
    rebuildColumns();
    endResetModel();
    return m_loadError;
}

bool Mesh2DSectionRowModel::isDirty() const
{
    return normalized(m_rows) != normalized(m_loaded);
}

QString Mesh2DSectionRowModel::validateCommit(SWMM_Engine engine) const
{
    if (!m_loadError.isEmpty())
        return tr("%1 could not be read when the editor opened: %2")
            .arg(sectionName(), m_loadError);
    if (!editable(engine))
        return tr("Reset the simulation before editing %1.").arg(sectionName());
    QList<Row> live;
    const QString error = read(engine, live);
    if (!error.isEmpty())
        return error;
    if (live != normalized(m_loaded))
        return tr("The %1 rows changed outside this editor. Reopen it to keep the newer rows.")
            .arg(sectionName());
    QMap<QString, int> keys;
    for (int i = 0; i < m_rows.size(); ++i) {
        const QString problem = rowError(m_rows[i]);
        if (!problem.isEmpty())
            return tr("%1 row %2: %3.").arg(sectionName()).arg(i + 1).arg(problem);
        const QString k = key(m_rows[i]);
        if (keys.contains(k))
            return tr("%1 rows %2 and %3 repeat the same %4; keep one of them.")
                .arg(sectionName())
                .arg(keys.value(k) + 1)
                .arg(i + 1)
                .arg(m_section == Section::AquiferLinks ? tr("conduit")
                     : m_section == Section::GwInitialQuality
                         ? tr("scope, zone and species")
                     : m_section == Section::Coverages || m_section == Section::CurbLength ||
                             m_section == Section::GwParams
                         ? tr("scope")
                         : tr("scope and species"));
        keys.insert(k, i);
    }
    return {};
}

QString Mesh2DSectionRowModel::commit(SWMM_Engine engine, bool advanceBaseline)
{
    if (!isDirty())
        return {};
    QString error = validateCommit(engine);
    if (!error.isEmpty())
        return error;
    error = write(engine, m_rows);
    if (!error.isEmpty()) {
        const QString restore = write(engine, m_loaded);
        return restore.isEmpty() ? error + tr(" The previous rows were restored.")
                                 : error + tr(" Restoring the previous rows failed: %1")
                                               .arg(restore);
    }
    if (advanceBaseline)
        acceptChanges();
    return {};
}

QString Mesh2DSectionRowModel::restoreLoaded(SWMM_Engine engine) const
{
    QList<Row> live;
    if (read(engine, live).isEmpty() && live == normalized(m_loaded))
        return {};
    return write(engine, m_loaded);
}

void Mesh2DSectionRowModel::setStagedRows(const QList<Row> &rows)
{
    beginResetModel();
    m_rows = normalized(rows);
    endResetModel();
}

void Mesh2DSectionRowModel::acceptChanges()
{
    m_rows = normalized(m_rows);
    m_loaded = m_rows;
}

} // namespace openswmmvis::ui
