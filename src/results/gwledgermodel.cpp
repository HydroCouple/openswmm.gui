/**
 * @file      gwledgermodel.cpp
 * @brief     V4 — the saved run's two-zone aquifer water and species ledgers
 *            as one table model.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "results/gwledgermodel.h"

#include "core/swmmdatetime.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <limits>

namespace openswmmvis::results
{

GwLedgerModel::GwLedgerModel(QObject *parent) : QAbstractTableModel(parent) {}

void GwLedgerModel::clear()
{
    beginResetModel();
    m_rows.clear();
    m_path.clear();
    m_status.clear();
    m_times.clear();
    m_ledgers = {};
    endResetModel();
}

bool GwLedgerModel::load(const QString &h5Path)
{
    beginResetModel();
    m_rows.clear();
    m_times.clear();
    m_ledgers = {};
    m_path = h5Path;
    io::Mesh2DH5Reader reader;
    if (h5Path.isEmpty() || !reader.open(h5Path)) {
        m_status = h5Path.isEmpty()
                       ? tr("No saved 2D results file. The ledger is read from the 2D results "
                            "(.2d.h5) of the active 2D results layer once a run has finished.")
                       : tr("Cannot open %1.").arg(h5Path);
        endResetModel();
        return false;
    }
    reader.readTimes(m_times);

    // One row per stored term (per species and term in a per-species ledger, whose mass is the
    // species' concentration unit × m3), or one unavailable row with the reader's reason.
    using Read = bool (io::Mesh2DH5Reader::*)(io::GroundwaterLedger &) const;
    const auto add = [&](Source source, Read read, const QString &label) {
        io::GroundwaterLedger &l = m_ledgers[source];
        if (!(reader.*read)(l)) {
            m_rows.append({label, QString(), QString(), reader.lastError(), -1, -1, source});
            return false;
        }
        QStringList tip = {l.longName, l.layout};
        if (l.boundaries == 1)
            tip.append(tr("Sum over 1 aquifer boundary edge."));
        else if (l.boundaries > 1)
            tip.append(tr("Sum over %1 aquifer boundary edges.").arg(l.boundaries));
        tip.removeAll(QString());
        const QString tooltip = tip.join(QLatin1Char('\n'));
        if (l.species.isEmpty()) {
            for (int t = 0; t < l.terms.size(); ++t)
                m_rows.append({label, l.terms[t], l.units, tooltip, -1, t, source});
            return true;
        }
        for (int s = 0; s < l.species.size(); ++s) {
            const QString &conc = l.speciesUnits[s];
            const QString units = conc.isEmpty() ? QString() : conc + QStringLiteral("·m3");
            const QString name = source == Transport         ? tr("Transport: %1").arg(l.species[s])
                                 : source == BoundarySpecies ? tr("Boundary: %1").arg(l.species[s])
                                                             : l.species[s];
            for (int t = 0; t < l.terms.size(); ++t)
                m_rows.append({name, l.terms[t], units, tooltip, s, t, source});
        }
        return true;
    };
    bool read = add(Water, &io::Mesh2DH5Reader::readGroundwaterLedger, tr("Water"));
    read |= add(Species, &io::Mesh2DH5Reader::readGroundwaterSpeciesLedger, tr("Species"));
    // Written only for layered transport or aquifer boundaries, so an absent one adds no row
    // (hasFaceField() is the reader's cached root-dataset probe).
    if (reader.hasFaceField("groundwater_transport_ledger"))
        read |= add(Transport, &io::Mesh2DH5Reader::readGroundwaterTransportLedger,
                     tr("Transport"));
    if (reader.hasFaceField("groundwater_boundary_water"))
        read |= add(BoundaryWater, &io::Mesh2DH5Reader::readGroundwaterBoundaryWater,
                     tr("Boundary water"));
    if (reader.hasFaceField("groundwater_boundary_species"))
        read |= add(BoundarySpecies, &io::Mesh2DH5Reader::readGroundwaterBoundarySpecies,
                     tr("Boundary species"));

    const QString file = QFileInfo(h5Path).fileName();
    if (!read) {
        m_status = tr("%1 has no groundwater ledger.").arg(file);
    } else {
        const auto first = std::find_if(m_rows.cbegin(), m_rows.cend(),
                                        [](const Row &r) { return r.column >= 0; });
        const int frames = first != m_rows.cend() ? frames_(*first) : 0;
        const QDateTime last = frames > 0 ? core::swmmDateTimeToQDateTime(m_times[frames - 1])
                                          : QDateTime();
        m_status = tr("%1 — cumulative totals at %2 (report %3 of %3).")
                       .arg(file, last.isValid() ? last.toString(Qt::ISODate) : tr("no report"))
                       .arg(frames);
    }
    endResetModel();
    return read;
}

int GwLedgerModel::frames_(const Row &r) const
{
    return std::min<int>(int(m_times.size()), m_ledgers[r.source].frames);
}

double GwLedgerModel::value_(const Row &r, int frame) const
{
    if (r.column < 0 || frame < 0 || frame >= frames_(r))
        return std::numeric_limits<double>::quiet_NaN();
    const io::GroundwaterLedger &ledger = m_ledgers[r.source];
    const std::size_t terms = std::size_t(ledger.terms.size());
    const std::size_t rowsPerFrame =
        ledger.species.isEmpty() ? 1 : std::size_t(ledger.species.size());
    const std::size_t at = (std::size_t(frame) * rowsPerFrame + std::size_t(std::max(r.species, 0))) *
                               terms +
                           std::size_t(r.column);
    const double v = at < ledger.values.size() ? ledger.values[at]
                                               : std::numeric_limits<double>::quiet_NaN();
    return std::isfinite(v) ? v : std::numeric_limits<double>::quiet_NaN();
}

QString GwLedgerModel::key(int row) const
{
    if (row < 0 || row >= m_rows.size() || m_rows[row].column < 0)
        return {};
    const Row &r = m_rows[row];
    const QString name = m_ledgers[r.source].species.value(r.species);
    switch (r.source) {
    case Water:
        return QStringLiteral("gwledger:water:") + r.term;
    case Species:
        return QStringLiteral("gwledger:species:%1:%2").arg(name, r.term);
    case Transport:
        return QStringLiteral("gwledger:transport:%1:%2").arg(name, r.term);
    case BoundaryWater:
        return QStringLiteral("gwledger:boundary:water:") + r.term;
    case BoundarySpecies:
        return QStringLiteral("gwledger:boundary:species:%1:%2").arg(name, r.term);
    case SourceCount:
        break;
    }
    return {};
}

int GwLedgerModel::rowForKey(const QString &key) const
{
    if (key.isEmpty())
        return -1;
    for (int row = 0; row < m_rows.size(); ++row)
        if (this->key(row) == key)
            return row;
    return -1;
}

plot::ResultDescriptor GwLedgerModel::descriptor(int row) const
{
    const QString k = key(row);
    if (k.isEmpty())
        return {};
    const Row &r = m_rows[row];
    const QString ledger = r.source == Water ? tr("water") : r.ledger;
    return plot::ResultDescriptor::forMeshVariable(
        k, tr("Groundwater ledger — %1 — %2, cumulative").arg(ledger, r.term), r.units);
}

QVector<plot::ResultDescriptor> GwLedgerModel::descriptors() const
{
    QVector<plot::ResultDescriptor> out;
    for (int row = 0; row < m_rows.size(); ++row)
        if (m_rows[row].column >= 0)
            out.append(descriptor(row));
    return out;
}

double GwLedgerModel::finalValue(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return std::numeric_limits<double>::quiet_NaN();
    return value_(m_rows[row], frames_(m_rows[row]) - 1);
}

bool GwLedgerModel::series(int row, plot::SeriesData &out) const
{
    out.ok = false;
    out.timesJulian.clear();
    out.values.clear();
    out.errorMessage.clear();
    if (row < 0 || row >= m_rows.size() || m_rows[row].column < 0) {
        out.errorMessage = row >= 0 && row < m_rows.size()
                               ? m_rows[row].tooltip
                               : tr("Groundwater ledger term unavailable");
        return false;
    }
    const Row &r = m_rows[row];
    const int frames = frames_(r);
    out.periodCount = frames;
    const int from = std::clamp(out.firstPeriod, 0, frames);
    out.timesJulian.reserve(std::size_t(frames - from));
    out.values.reserve(std::size_t(frames - from));
    for (int t = from; t < frames; ++t) {
        out.timesJulian.push_back(m_times[std::size_t(t)]);
        out.values.push_back(value_(r, t));
    }
    out.ok = true;
    return true;
}

int GwLedgerModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int GwLedgerModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant GwLedgerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows[index.row()];
    const bool available = r.column >= 0;
    if (role == Qt::ToolTipRole)
        return r.tooltip;
    if (role == Qt::TextAlignmentRole && index.column() == ColFinal)
        return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::UserRole && index.column() == ColFinal)
        return finalValue(index.row());
    if (role != Qt::DisplayRole)
        return {};
    switch (index.column()) {
    case ColLedger:
        return r.ledger;
    case ColTerm:
        return available ? r.term : QStringLiteral("—");
    case ColFinal: {
        if (!available)
            return tr("unavailable");
        const double v = finalValue(index.row());
        return std::isnan(v) ? tr("missing") : QString::number(v, 'g', 8);
    }
    case ColUnits:
        return !available ? QString() : r.units.isEmpty() ? tr("units unknown") : r.units;
    default:
        return {};
    }
}

QVariant GwLedgerModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColLedger:
        return tr("Ledger");
    case ColTerm:
        return tr("Term");
    case ColFinal:
        return tr("Final (cumulative)");
    case ColUnits:
        return tr("Units");
    default:
        return {};
    }
}

} // namespace openswmmvis::results
