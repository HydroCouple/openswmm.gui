/*!
 * \file   timeseriesprovider.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "timeseries/timeseriesprovider.h"

#include <QObject>

#include <algorithm>

namespace openswmmvis::timeseries {

TimeseriesProvider::TimeseriesProvider(QString name, QObject *parent)
    : QObject(parent)
    , m_name(std::move(name))
{
}

TimeseriesProvider::~TimeseriesProvider() = default;

bool TimeseriesProvider::isStrictMonotone_(const QVector<TimeseriesPoint>& pts)
{
    for (int i = 1; i < pts.size(); ++i) {
        if (!(pts.at(i - 1).time < pts.at(i).time))
            return false;
    }
    return true;
}

bool TimeseriesProvider::setAllPoints(QVector<TimeseriesPoint> newPoints, QString *reasonOut)
{
    if (!isStrictMonotone_(newPoints)) {
        const QString reason = tr("Time values must be strictly ascending.");
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    const int prevCount = m_points.size();
    m_points = std::move(newPoints);
    if (prevCount > 0)
        emit pointsRemoved(0, prevCount);
    if (!m_points.isEmpty())
        emit pointsInserted(0, m_points.size());
    return true;
}

bool TimeseriesProvider::setValueAt(int i, double newValue, QString *reasonOut)
{
    if (i < 0 || i >= m_points.size()) {
        const QString reason = tr("Index %1 out of range [0, %2).").arg(i).arg(m_points.size());
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    if (m_points[i].value == newValue)
        return true;
    m_points[i].value = newValue;
    emit pointsChanged(i, 1);
    return true;
}

bool TimeseriesProvider::setValueLive(int i, double newValue)
{
    if (i < 0 || i >= m_points.size()) return false;
    if (m_points[i].value == newValue) return true;
    m_points[i].value = newValue;
    emit pointsChanged(i, 1);
    return true;
}

bool TimeseriesProvider::setPointAt(int i, QDateTime newTime, double newValue, QString *reasonOut)
{
    if (i < 0 || i >= m_points.size()) {
        const QString reason = tr("Index %1 out of range [0, %2).").arg(i).arg(m_points.size());
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    if (i > 0 && !(m_points.at(i - 1).time < newTime)) {
        const QString reason = tr("New time at index %1 must be strictly after the previous point.").arg(i);
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    if (i + 1 < m_points.size() && !(newTime < m_points.at(i + 1).time)) {
        const QString reason = tr("New time at index %1 must be strictly before the next point.").arg(i);
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    m_points[i].time = std::move(newTime);
    m_points[i].value = newValue;
    emit pointsChanged(i, 1);
    return true;
}

int TimeseriesProvider::insertPoint(QDateTime time, double value, QString *reasonOut)
{
    // Find insertion position via std::lower_bound on time.
    auto it = std::lower_bound(m_points.begin(), m_points.end(), time,
        [](const TimeseriesPoint& p, const QDateTime& t) { return p.time < t; });

    const int at = static_cast<int>(it - m_points.begin());
    // Reject duplicate times outright — strict monotonicity is non-negotiable.
    if (it != m_points.end() && it->time == time) {
        const QString reason = tr("A point already exists at time %1.").arg(time.toString(Qt::ISODate));
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return -1;
    }
    m_points.insert(it, TimeseriesPoint{std::move(time), value});
    emit pointsInserted(at, 1);
    return at;
}

void TimeseriesProvider::removePointsAt(QVector<int> indices)
{
    if (indices.isEmpty()) return;
    // Dedup + sort descending so erases don't invalidate later indices.
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    for (int idx : indices) {
        if (idx < 0 || idx >= m_points.size()) continue;
        m_points.remove(idx);
        emit pointsRemoved(idx, 1);
    }
}

void TimeseriesProvider::setName(QString newName)
{
    if (newName == m_name) return;
    const QString prev = m_name;
    m_name = std::move(newName);
    emit nameChanged(prev, m_name);
    emit metadataChanged();
}

void TimeseriesProvider::setUnitsLabel(QString units)
{
    if (units == m_unitsLabel) return;
    m_unitsLabel = std::move(units);
    emit metadataChanged();
}

void TimeseriesProvider::setDescription(QString d)
{
    if (d == m_description) return;
    m_description = std::move(d);
    emit metadataChanged();
}

void TimeseriesProvider::setSourceMode(SourceMode mode)
{
    if (mode == m_sourceMode) return;
    const SourceMode prev = m_sourceMode;
    m_sourceMode = mode;
    emit sourceModeChanged(prev, m_sourceMode);
}

void TimeseriesProvider::setFileSource(QString path, QString columnSelector, QDateTime mtime)
{
    const bool changed = (path != m_filePath)
                      || (columnSelector != m_columnSelector)
                      || (mtime != m_fileMTime);
    m_filePath = std::move(path);
    m_columnSelector = std::move(columnSelector);
    m_fileMTime = std::move(mtime);
    if (changed)
        emit metadataChanged();
}

} // namespace openswmmvis::timeseries
