/**
 * @file      mesh2dgwsourcemodel.cpp
 * @brief     Staged models for the `[GW_SOURCES]` list and the species terms of one source.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/mesh2dgwsourcemodel.h"

#include "ui/dialogs/mesh2dsectionrowmodel.h"

#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_gw_transport.h>

#include <QFile>
#include <QLocale>
#include <QRegularExpression>

#include <cmath>

namespace openswmmvis::ui
{

using assignment::GroundwaterSourceRow;
using assignment::GroundwaterSpeciesTerm;

namespace
{

bool editable(SWMM_Engine engine)
{
    int state = SWMM_STATE_CREATED;
    return engine && swmm_engine_get_state(engine, &state) == SWMM_OK &&
           (state == SWMM_STATE_BUILDING || state == SWMM_STATE_OPENED);
}

QString numberText(double value)
{
    return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

QStringList scopeLabels()
{
    return {QObject::tr("All cells"), QObject::tr("Tag"), QObject::tr("Cell")};
}

/** Inactive location fields cleared, as the engine reads them back. */
GroundwaterSourceRow normalized(GroundwaterSourceRow row)
{
    if (row.scope != SWMM_GW_SCOPE_TAG)
        row.tag.clear();
    if (row.scope != SWMM_GW_SCOPE_CELL)
        row.cell = -1;
    return row;
}

QString termText(const GroundwaterSpeciesTerm &term)
{
    return QStringLiteral("%1 %2 %3")
        .arg(term.species, term.kind, term.series.isEmpty() ? numberText(term.value) : term.series);
}

} // namespace

// ---------------------------------------------------------------------------
// Mesh2DGwSourceModel
// ---------------------------------------------------------------------------

Mesh2DGwSourceModel::Mesh2DGwSourceModel(QObject *parent) : QAbstractTableModel(parent) {}

QString Mesh2DGwSourceModel::noneLabel() const
{
    return tr("(none)");
}

int Mesh2DGwSourceModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int Mesh2DGwSourceModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant Mesh2DGwSourceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows[index.row()];
    const GroundwaterSourceRow &s = r.source;
    if (role == Qt::ToolTipRole) {
        if (r.xy && index.column() <= ColFlowSeries)
            return tr("Authored by XY coordinates. The engine placed it in cell %1 and cannot "
                      "report the coordinates, so changing its location saves it as CELL %1.")
                .arg(s.cell + 1);
        switch (index.column()) {
        case ColFlow:
            return tr("Total discharge in m³/s, positive into the aquifer. A TAG or * source is "
                      "shared among its cells by area. A flow series replaces this value.");
        case ColScale:
            return tr("Multiplier on the flow and MASS terms (never CONC).");
        case ColTerms:
            return tr("Species terms; edit them in the table below.");
        default:
            return {};
        }
    }
    if (role == ChoicesRole) {
        switch (index.column()) {
        case ColScope:
            return scopeLabels();
        case ColTarget:
            if (s.scope == SWMM_GW_SCOPE_TAG) {
                QStringList tags = m_tags;
                if (!s.tag.isEmpty() && !tags.contains(s.tag))
                    tags << s.tag;
                return tags;
            }
            return {};
        case ColFlowSeries:
            return seriesChoices();
        default:
            return {};
        }
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    switch (index.column()) {
    case ColName:
        return s.name;
    case ColScope:
        return r.xy && role == Qt::DisplayRole ? tr("XY") : scopeLabels().value(s.scope);
    case ColTarget:
        if (s.scope == SWMM_GW_SCOPE_TAG)
            return s.tag;
        if (s.scope == SWMM_GW_SCOPE_CELL) {
            if (s.cell < 0)
                return QString();
            return r.xy && role == Qt::DisplayRole ? tr("cell %1").arg(s.cell + 1)
                                                   : QString::number(s.cell + 1);
        }
        return role == Qt::EditRole ? QString() : QStringLiteral("—");
    case ColFlow:
        return s.series.isEmpty() ? numberText(s.flow) : QString();
    case ColFlowSeries:
        return s.series.isEmpty() && role == Qt::EditRole ? noneLabel() : s.series;
    case ColScale:
        return numberText(s.scale);
    case ColTerms: {
        QStringList parts;
        for (const auto &term : s.terms)
            parts << termText(term);
        return parts.join(QStringLiteral("; "));
    }
    default:
        return {};
    }
}

bool Mesh2DGwSourceModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || index.row() >= m_rows.size() ||
        !(flags(index) & Qt::ItemIsEditable))
        return false;
    GroundwaterSourceRow &s = m_rows[index.row()].source;
    const QString text = value.toString().trimmed();
    bool ok = false;
    switch (index.column()) {
    case ColName:
        if (text.isEmpty())
            return false;
        s.name = text;
        break;
    case ColScope: {
        int scope = scopeLabels().indexOf(text);
        if (scope < 0) {
            scope = text.toInt(&ok);
            if (!ok)
                return false;
        }
        if (scope < SWMM_GW_SCOPE_GLOBAL || scope > SWMM_GW_SCOPE_CELL)
            return false;
        s.scope = scope;
        if (scope == SWMM_GW_SCOPE_CELL && s.cell < 0)
            s.cell = 0;
        emit dataChanged(this->index(index.row(), ColScope), this->index(index.row(), ColTarget));
        return true;
    }
    case ColTarget:
        if (s.scope == SWMM_GW_SCOPE_TAG) {
            if (text.isEmpty())
                return false;
            s.tag = text;
        } else {
            const int cell = text.toInt(&ok);
            if (!ok || cell < 1)
                return false;
            s.cell = cell - 1;
        }
        break;
    case ColFlow: {
        const double flow = text.toDouble(&ok);
        if (!ok || !std::isfinite(flow))
            return false;
        s.flow = flow;
        break;
    }
    case ColFlowSeries:
        if (text.isEmpty() || text == noneLabel()) {
            s.series.clear();
        } else {
            if (!m_series.contains(text))
                return false;
            s.series = text;
            s.flow = 0.0; // the series replaces the constant; the file keeps only one
        }
        emit dataChanged(this->index(index.row(), ColFlow),
                         this->index(index.row(), ColFlowSeries));
        return true;
    case ColScale: {
        const double scale = text.toDouble(&ok);
        if (!ok || !std::isfinite(scale) || scale < 0.0)
            return false;
        s.scale = scale;
        break;
    }
    default:
        return false;
    }
    emit dataChanged(index, index);
    return true;
}

QVariant Mesh2DGwSourceModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Vertical)
        return role == Qt::DisplayRole ? QVariant(section + 1) : QVariant();
    if (role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColName:
        return tr("Name");
    case ColScope:
        return tr("Scope");
    case ColTarget:
        return tr("Tag / cell");
    case ColFlow:
        return tr("Flow (m³/s)");
    case ColFlowSeries:
        return tr("Flow series");
    case ColScale:
        return tr("Scale");
    case ColTerms:
        return tr("Species terms");
    default:
        return {};
    }
}

Qt::ItemFlags Mesh2DGwSourceModel::flags(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return Qt::NoItemFlags;
    const Qt::ItemFlags base = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    const Row &r = m_rows[index.row()];
    switch (index.column()) {
    case ColScale:
        return base | Qt::ItemIsEditable;
    case ColName: // a rename rewrites the source, which would save an XY source as CELL
    case ColScope:
    case ColFlowSeries:
        return r.xy ? base : base | Qt::ItemIsEditable;
    case ColTarget:
        return r.xy || r.source.scope == SWMM_GW_SCOPE_GLOBAL ? base : base | Qt::ItemIsEditable;
    case ColFlow:
        return r.xy || !r.source.series.isEmpty() ? base : base | Qt::ItemIsEditable;
    default:
        return base;
    }
}

bool Mesh2DGwSourceModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid() || row < 0 || count <= 0 || row + count > m_rows.size())
        return false;
    beginRemoveRows(QModelIndex(), row, row + count - 1);
    m_rows.erase(m_rows.begin() + row, m_rows.begin() + row + count);
    endRemoveRows();
    return true;
}

QSet<QString> Mesh2DGwSourceModel::xySourceNames(const QString &inputFile)
{
    QSet<QString> names;
    QFile file(inputFile);
    if (inputFile.isEmpty() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return names;
    bool inSources = false;
    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine());
        const int comment = line.indexOf(QLatin1Char(';'));
        if (comment >= 0)
            line.truncate(comment);
        line = line.simplified();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1Char('['))) {
            inSources = line.compare(QLatin1String("[GW_SOURCES]"), Qt::CaseInsensitive) == 0;
            continue;
        }
        if (!inSources)
            continue;
        const QStringList tokens = line.split(QLatin1Char(' '));
        if (tokens.size() >= 4 && tokens[1].compare(QLatin1String("XY"), Qt::CaseInsensitive) == 0)
            names.insert(tokens[0]);
    }
    return names;
}

QString Mesh2DGwSourceModel::load(SWMM_Engine engine, const QString &inputFile)
{
    beginResetModel();
    m_rows.clear();
    m_loaded.clear();
    m_loadError.clear();
    m_species.clear();
    m_series.clear();
    m_cellCount = 0;
    m_hasWritten = false;
    if (engine) {
        int cells = 0;
        if (swmm_2d_cell_count(engine, &cells) == SWMM_OK)
            m_cellCount = cells;
        for (const auto &species : assignment::groundwaterTransportSpecies(engine))
            m_species << species.id;
        m_series = timeSeriesNames(engine);
        QVector<GroundwaterSourceRow> live;
        if (assignment::readGroundwaterSources(engine, &live, &m_loadError)) {
            const QSet<QString> xy = xySourceNames(inputFile);
            for (const auto &source : live)
                m_loaded.append({source, source.scope == SWMM_GW_SCOPE_CELL &&
                                             xy.contains(source.name)});
        }
        m_rows = m_loaded;
    }
    endResetModel();
    return m_loadError;
}

bool Mesh2DGwSourceModel::isDirty() const
{
    if (m_rows.size() != m_loaded.size())
        return true;
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].xy != m_loaded[i].xy ||
            normalized(m_rows[i].source) != normalized(m_loaded[i].source))
            return true;
    return false;
}

QString Mesh2DGwSourceModel::rowError(const Row &r) const
{
    const GroundwaterSourceRow &s = r.source;
    static const QRegularExpression namePattern(QStringLiteral("^[^\\s\\\";\\[\\]]+$"));
    if (!namePattern.match(s.name).hasMatch())
        return tr("use a name without spaces, quotes, semicolons or brackets");
    if (s.scope == SWMM_GW_SCOPE_TAG && s.tag.trimmed().isEmpty())
        return tr("choose a mesh tag");
    if (s.scope == SWMM_GW_SCOPE_CELL &&
        (s.cell < 0 || (m_cellCount > 0 && s.cell >= m_cellCount)))
        return tr("enter a cell between 1 and %1").arg(m_cellCount);
    if (!std::isfinite(s.flow) || !std::isfinite(s.scale) || s.scale < 0.0)
        return tr("flow must be finite and the scale finite and at least 0");
    if (!s.series.isEmpty() && !m_series.contains(s.series))
        return tr("the flow series %1 does not exist").arg(s.series);
    QSet<QString> species;
    for (const auto &term : s.terms) {
        if (term.species.isEmpty())
            return tr("choose a species for every term");
        if (species.contains(term.species))
            return tr("%1 has more than one term").arg(term.species);
        species.insert(term.species);
        if (term.kind != QLatin1String("CONC") && term.kind != QLatin1String("MASS"))
            return tr("a term must be CONC or MASS");
        if (!std::isfinite(term.value) ||
            (term.value < 0.0 && (term.kind == QLatin1String("MASS") ||
                                  term.species != QLatin1String("__TEMPERATURE__"))))
            return tr("the %1 term must be a finite value of at least 0").arg(term.species);
        if (!term.series.isEmpty() && !m_series.contains(term.series))
            return tr("the series %1 does not exist").arg(term.series);
    }
    return {};
}

QString Mesh2DGwSourceModel::validateCommit(SWMM_Engine engine) const
{
    if (!m_loadError.isEmpty())
        return tr("[GW_SOURCES] could not be read when the editor opened: %1").arg(m_loadError);
    if (!editable(engine))
        return tr("Reset the simulation before editing [GW_SOURCES].");
    QVector<GroundwaterSourceRow> live;
    QString error;
    if (!assignment::readGroundwaterSources(engine, &live, &error))
        return error;
    bool same = live.size() == m_loaded.size();
    for (int i = 0; same && i < live.size(); ++i)
        same = normalized(live[i]) == normalized(m_loaded[i].source);
    if (!same)
        return tr("The groundwater sources changed outside this editor. Reopen it to keep the "
                  "newer rows.");
    QSet<QString> names;
    for (int i = 0; i < m_rows.size(); ++i) {
        const QString problem = rowError(m_rows[i]);
        if (!problem.isEmpty())
            return tr("Source %1 (row %2): %3.").arg(m_rows[i].source.name).arg(i + 1).arg(problem);
        const QString folded = m_rows[i].source.name.toCaseFolded();
        if (names.contains(folded))
            return tr("Two sources are named %1; source names must be unique.")
                .arg(m_rows[i].source.name);
        names.insert(folded);
    }
    return {};
}

QString Mesh2DGwSourceModel::transition(SWMM_Engine e, const QList<Row> &target,
                                        const QSet<QString> &forceLocation) const
{
    QVector<GroundwaterSourceRow> live;
    QString error;
    if (!assignment::readGroundwaterSources(e, &live, &error))
        return error;
    QSet<QString> names;
    for (const Row &r : target)
        names.insert(r.source.name);
    for (int i = int(live.size()) - 1; i >= 0; --i) {
        if (names.contains(live[i].name))
            continue;
        if (swmm_gw_source_remove(e, i) != SWMM_OK)
            return tr("The engine refused to remove source %1.").arg(live[i].name);
        live.removeAt(i);
    }
    for (const Row &r : target) {
        const GroundwaterSourceRow s = normalized(r.source);
        int at = -1;
        for (int i = 0; i < live.size() && at < 0; ++i)
            if (live[i].name == s.name)
                at = i;
        const QString refused = tr("The engine refused source %1.").arg(s.name);
        if (at < 0) {
            if (!assignment::writeGroundwaterSource(e, int(live.size()), s))
                return refused;
            live.append(s);
            continue;
        }
        const GroundwaterSourceRow old = live[at];
        // Only a location/flow change goes through swmm_gw_source_set: it is the one call that
        // also drops an XY location the API cannot read back.
        if ((forceLocation.contains(s.name) || old.scope != s.scope || old.tag != s.tag ||
             old.cell != s.cell || old.flow != s.flow || old.series != s.series) &&
            swmm_gw_source_set(e, s.name.toUtf8().constData(), s.scope, s.tag.toUtf8().constData(),
                               s.cell, s.flow, s.series.toUtf8().constData()) != SWMM_OK)
            return refused;
        if (old.scale != s.scale && swmm_gw_source_scale_set(e, at, s.scale) != SWMM_OK)
            return refused;
        if (old.terms != s.terms) {
            for (int t = int(old.terms.size()) - 1; t >= 0; --t)
                if (swmm_gw_source_species_remove(e, at, t) != SWMM_OK)
                    return refused;
            for (const auto &term : s.terms)
                if (swmm_gw_source_species_set(e, at, term.species.toUtf8().constData(),
                                               term.kind.toUtf8().constData(), term.value,
                                               term.series.toUtf8().constData()) != SWMM_OK)
                    return refused;
        }
        live[at] = s;
    }
    QVector<GroundwaterSourceRow> back;
    if (!assignment::readGroundwaterSources(e, &back, &error))
        return error;
    bool same = back.size() == target.size();
    for (const Row &r : target) {
        bool found = false;
        for (const auto &b : back)
            if (b.name == r.source.name)
                found = normalized(b) == normalized(r.source);
        same = same && found;
    }
    return same ? QString()
                : tr("The engine stored different [GW_SOURCES] rows than were applied.");
}

QString Mesh2DGwSourceModel::commit(SWMM_Engine engine, bool advanceBaseline)
{
    if (!isDirty())
        return {};
    QString error = validateCommit(engine);
    if (!error.isEmpty())
        return error;
    QSet<QString> convert; // XY sources the user chose to save as CELL
    for (const Row &r : m_rows)
        for (const Row &l : m_loaded)
            if (!r.xy && l.xy && l.source.name == r.source.name)
                convert.insert(r.source.name);
    error = transition(engine, m_rows, convert);
    if (!error.isEmpty()) {
        const QString restore = restoreLoaded(engine);
        return restore.isEmpty() ? error + tr(" The previous sources were restored.")
                                 : error + tr(" Restoring the previous sources failed: %1")
                                               .arg(restore);
    }
    m_hasWritten = assignment::readGroundwaterSources(engine, &m_written);
    if (advanceBaseline)
        acceptChanges();
    return {};
}

QString Mesh2DGwSourceModel::restoreLoaded(SWMM_Engine engine) const
{
    QVector<GroundwaterSourceRow> live;
    if (assignment::readGroundwaterSources(engine, &live)) {
        bool same = live.size() == m_loaded.size();
        for (int i = 0; same && i < live.size(); ++i)
            same = normalized(live[i]) == normalized(m_loaded[i].source);
        if (same)
            return {};
    }
    return transition(engine, m_loaded, {});
}

void Mesh2DGwSourceModel::acceptChanges()
{
    QList<Row> rows;
    if (m_hasWritten) {
        // Renamed and new sources were appended, so adopt the engine's order.
        for (const auto &source : m_written) {
            bool xy = false;
            for (const Row &r : m_rows)
                if (r.source.name == source.name)
                    xy = r.xy;
            rows.append({source, xy});
        }
    } else {
        for (const Row &r : m_rows)
            rows.append({normalized(r.source), r.xy});
    }
    m_hasWritten = false;
    beginResetModel();
    m_rows = rows;
    m_loaded = rows;
    endResetModel();
}

int Mesh2DGwSourceModel::appendSource()
{
    QSet<QString> names;
    for (const Row &r : m_rows)
        names.insert(r.source.name.toCaseFolded());
    Row r;
    int n = int(m_rows.size()) + 1;
    do {
        r.source.name = QStringLiteral("Source%1").arg(n++);
    } while (names.contains(r.source.name.toCaseFolded()));
    r.source.scope = SWMM_GW_SCOPE_CELL;
    r.source.cell = 0;
    const int at = int(m_rows.size());
    beginInsertRows(QModelIndex(), at, at);
    m_rows.append(r);
    endInsertRows();
    return at;
}

bool Mesh2DGwSourceModel::isXy(int row) const
{
    return row >= 0 && row < m_rows.size() && m_rows[row].xy;
}

bool Mesh2DGwSourceModel::convertToCell(int row)
{
    if (!isXy(row))
        return false;
    m_rows[row].xy = false;
    emit dataChanged(index(row, 0), index(row, ColCount - 1));
    return true;
}

QVector<GroundwaterSpeciesTerm> Mesh2DGwSourceModel::terms(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows[row].source.terms
                                           : QVector<GroundwaterSpeciesTerm>();
}

bool Mesh2DGwSourceModel::setTerms(int row, const QVector<GroundwaterSpeciesTerm> &terms)
{
    if (row < 0 || row >= m_rows.size())
        return false;
    m_rows[row].source.terms = terms;
    emit dataChanged(index(row, ColTerms), index(row, ColTerms));
    return true;
}

QStringList Mesh2DGwSourceModel::speciesChoices() const
{
    QStringList names = m_species;
    for (const Row &r : m_rows)
        for (const auto &term : r.source.terms)
            if (!term.species.isEmpty() && !names.contains(term.species))
                names << term.species;
    return names;
}

QStringList Mesh2DGwSourceModel::seriesChoices() const
{
    return QStringList{noneLabel()} + m_series;
}

// ---------------------------------------------------------------------------
// Mesh2DGwSourceTermModel
// ---------------------------------------------------------------------------

Mesh2DGwSourceTermModel::Mesh2DGwSourceTermModel(Mesh2DGwSourceModel *sources, QObject *parent)
    : QAbstractTableModel(parent), m_sources(sources)
{
    if (!sources)
        return;
    const auto refresh = [this] {
        beginResetModel();
        if (!m_sources || m_row >= m_sources->rowCount())
            m_row = -1;
        endResetModel();
    };
    connect(sources, &QAbstractItemModel::modelReset, this, refresh);
    connect(sources, &QAbstractItemModel::rowsRemoved, this, refresh);
    connect(sources, &QAbstractItemModel::rowsInserted, this, refresh);
}

void Mesh2DGwSourceTermModel::setSourceRow(int row)
{
    beginResetModel();
    m_row = m_sources && row >= 0 && row < m_sources->rowCount() ? row : -1;
    endResetModel();
}

int Mesh2DGwSourceTermModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() || !m_sources ? 0 : int(m_sources->terms(m_row).size());
}

int Mesh2DGwSourceTermModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant Mesh2DGwSourceTermModel::data(const QModelIndex &index, int role) const
{
    if (!m_sources || !index.isValid())
        return {};
    const auto terms = m_sources->terms(m_row);
    if (index.row() >= terms.size())
        return {};
    const GroundwaterSpeciesTerm &t = terms[index.row()];
    if (role == Qt::ToolTipRole) {
        switch (index.column()) {
        case ColKind:
            return tr("CONC injects at this concentration (species units; temperature °C, age "
                      "s). MASS adds species mass per second (mg/s, ug/s or count/s) and is "
                      "scaled by the source's Scale.");
        case ColValue:
            return tr("Constant value; a series replaces it.");
        default:
            return {};
        }
    }
    if (role == Mesh2DGwSourceModel::ChoicesRole) {
        switch (index.column()) {
        case ColSpecies:
            return m_sources->speciesChoices();
        case ColKind:
            return QStringList{QStringLiteral("CONC"), QStringLiteral("MASS")};
        case ColSeries:
            return m_sources->seriesChoices();
        default:
            return {};
        }
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    switch (index.column()) {
    case ColSpecies:
        return t.species;
    case ColKind:
        return t.kind;
    case ColValue:
        return t.series.isEmpty() ? numberText(t.value) : QString();
    case ColSeries:
        return t.series.isEmpty() && role == Qt::EditRole ? m_sources->seriesChoices().value(0)
                                                          : t.series;
    default:
        return {};
    }
}

bool Mesh2DGwSourceTermModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!m_sources || role != Qt::EditRole || !index.isValid() ||
        !(flags(index) & Qt::ItemIsEditable))
        return false;
    auto terms = m_sources->terms(m_row);
    if (index.row() >= terms.size())
        return false;
    GroundwaterSpeciesTerm &t = terms[index.row()];
    const QString text = value.toString().trimmed();
    switch (index.column()) {
    case ColSpecies:
        if (text.isEmpty())
            return false;
        t.species = text;
        break;
    case ColKind:
        if (text != QLatin1String("CONC") && text != QLatin1String("MASS"))
            return false;
        t.kind = text;
        break;
    case ColValue: {
        bool ok = false;
        const double v = text.toDouble(&ok);
        if (!ok || !std::isfinite(v))
            return false;
        t.value = v;
        break;
    }
    case ColSeries: {
        const QStringList series = m_sources->seriesChoices();
        if (text.isEmpty() || text == series.value(0)) {
            t.series.clear();
        } else {
            if (!series.contains(text))
                return false;
            t.series = text;
            t.value = 0.0;
        }
        break;
    }
    default:
        return false;
    }
    m_sources->setTerms(m_row, terms);
    emit dataChanged(this->index(index.row(), 0), this->index(index.row(), ColCount - 1));
    return true;
}

QVariant Mesh2DGwSourceTermModel::headerData(int section, Qt::Orientation orientation,
                                             int role) const
{
    if (orientation == Qt::Vertical)
        return role == Qt::DisplayRole ? QVariant(section + 1) : QVariant();
    if (role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColSpecies:
        return tr("Species");
    case ColKind:
        return tr("Kind");
    case ColValue:
        return tr("Value");
    case ColSeries:
        return tr("Series");
    default:
        return {};
    }
}

Qt::ItemFlags Mesh2DGwSourceTermModel::flags(const QModelIndex &index) const
{
    if (!m_sources || !index.isValid() || index.row() >= rowCount())
        return Qt::NoItemFlags;
    const Qt::ItemFlags base = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == ColValue && !m_sources->terms(m_row)[index.row()].series.isEmpty())
        return base;
    return base | Qt::ItemIsEditable;
}

int Mesh2DGwSourceTermModel::appendTerm()
{
    if (!m_sources || m_row < 0)
        return -1;
    auto terms = m_sources->terms(m_row);
    QString species;
    for (const QString &name : m_sources->speciesChoices()) {
        bool used = false;
        for (const auto &term : terms)
            used = used || term.species == name;
        if (!used) {
            species = name;
            break;
        }
    }
    GroundwaterSpeciesTerm term;
    term.species = species;
    const int at = int(terms.size());
    beginInsertRows(QModelIndex(), at, at);
    terms.append(term);
    m_sources->setTerms(m_row, terms);
    endInsertRows();
    return at;
}

bool Mesh2DGwSourceTermModel::removeTerm(int term)
{
    if (!m_sources || term < 0 || term >= rowCount())
        return false;
    auto terms = m_sources->terms(m_row);
    beginRemoveRows(QModelIndex(), term, term);
    terms.removeAt(term);
    m_sources->setTerms(m_row, terms);
    endRemoveRows();
    return true;
}

} // namespace openswmmvis::ui
