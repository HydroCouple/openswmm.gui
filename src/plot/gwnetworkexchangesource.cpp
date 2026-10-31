/**
 * @file      gwnetworkexchangesource.cpp
 * @brief     V3 — per-node aquifer ↔ 1D network exchange series from a saved
 *            2D results file.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "plot/gwnetworkexchangesource.h"

#include "io/mesh2dh5reader.h"

#include <QObject>

#include <algorithm>
#include <cmath>
#include <limits>

namespace openswmmvis::plot
{

bool GwNetworkExchangeSource::open(const QString &h5Path)
{
    m_available = false;
    m_error.clear();
    m_units.clear();
    m_nodes.clear();
    m_columns.clear();
    m_times.clear();
    m_frames = 0;
    m_cumulative.clear();
    m_path = h5Path;
    io::Mesh2DH5Reader reader;
    if (h5Path.isEmpty() || !reader.open(h5Path)) {
        m_error = QObject::tr("No saved 2D results file to read groundwater exchange from.");
        return false;
    }
    if (!reader.readGroundwaterExchangeNodes(m_nodes, m_units, m_frames) ||
        !reader.readTimes(m_times)) {
        m_error = reader.lastError();
        return false;
    }
    for (int column = 0; column < m_nodes.size(); ++column)
        m_columns.insert(m_nodes[column], column);
    m_available = true;
    return true;
}

QVector<ResultDescriptor> GwNetworkExchangeSource::nodeDescriptors() const
{
    const QString volume = m_units.isEmpty() ? QStringLiteral("m3") : m_units;
    return {ResultDescriptor::forMeshVariable(
                QString::fromLatin1(kRateKey),
                QObject::tr("Groundwater exchange rate, report-interval mean (+ out of aquifer)"),
                volume + QStringLiteral("/s")),
            ResultDescriptor::forMeshVariable(
                QString::fromLatin1(kCumulativeKey),
                QObject::tr("Groundwater exchange, cumulative (+ out of aquifer)"), volume)};
}

bool GwNetworkExchangeSource::isExchangeDescriptor(const ResultDescriptor &d)
{
    return d.isMeshVariable() && (d.variableKey == QLatin1String(kRateKey) ||
                                  d.variableKey == QLatin1String(kCumulativeKey));
}

void GwNetworkExchangeSource::series(const QString &node, const ResultDescriptor &d,
                                     SeriesData &out) const
{
    out.ok = false;
    out.errorMessage.clear();
    out.timesJulian.clear();
    out.values.clear();
    if (!isExchangeDescriptor(d)) {
        out.errorMessage = QObject::tr("'%1' is not a groundwater exchange series")
                               .arg(d.variableLabel.isEmpty() ? d.variableKey : d.variableLabel);
        return;
    }
    if (!m_available) {
        out.errorMessage = m_error;
        return;
    }
    const auto column = m_columns.constFind(node);
    if (column == m_columns.constEnd()) {
        out.errorMessage = QObject::tr("Node '%1' has no [2D_AQUIFER_NODE] bed in this run's "
                                       "groundwater exchange output (missing, not zero).")
                               .arg(node);
        return;
    }
    auto cached = m_cumulative.find(column.value());
    if (cached == m_cumulative.end()) {
        io::Mesh2DH5Reader reader;
        std::vector<double> cumulative;
        if (!reader.open(m_path) ||
            !reader.readGroundwaterExchangeColumn(column.value(), cumulative)) {
            out.errorMessage = reader.lastError();
            return;
        }
        cached = m_cumulative.emplace(column.value(), std::move(cumulative)).first;
    }
    const std::vector<double> &cum = cached->second;
    const int frames = std::min({m_frames, int(m_times.size()), int(cum.size())});
    out.periodCount = frames;
    const int from = std::clamp(out.firstPeriod, 0, frames);
    const bool rate = d.variableKey == QLatin1String(kRateKey);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (int t = from; t < frames; ++t) {
        const double now = std::isfinite(cum[std::size_t(t)]) ? cum[std::size_t(t)] : nan;
        if (!rate) {
            out.timesJulian.push_back(m_times[std::size_t(t)]);
            out.values.push_back(now);
            continue;
        }
        // Report-interval mean ending at frame t; the first report has none.
        if (t == 0)
            continue;
        const double seconds = (m_times[std::size_t(t)] - m_times[std::size_t(t) - 1]) * 86400.0;
        if (!(seconds > 0.0))
            continue;
        out.timesJulian.push_back(m_times[std::size_t(t)]);
        out.values.push_back((now - cum[std::size_t(t) - 1]) / seconds);
    }
    out.ok = from == frames || !out.timesJulian.empty();
    if (!out.ok)
        out.errorMessage = QObject::tr("No groundwater exchange samples for node '%1'").arg(node);
}

} // namespace openswmmvis::plot
