/*!
 * \file   transectprovider.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "transect/transectprovider.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace openswmmvis::transect {

namespace {
constexpr double kStationEps = 1e-9;
} // namespace

TransectProvider::TransectProvider(QString name, QObject *parent)
    : QObject(parent)
    , m_name(std::move(name))
{
}

TransectProvider::~TransectProvider() = default;

bool TransectProvider::isStrictlyMonotone_(const QVector<TransectPoint> &pts)
{
    for (int i = 1; i < pts.size(); ++i) {
        if (!(pts[i].station > pts[i - 1].station)) return false;
    }
    return true;
}

QVector<QPair<double,double>> TransectProvider::allPoints() const
{
    QVector<QPair<double,double>> out;
    out.reserve(m_points.size());
    for (const auto &p : m_points)
        out.push_back({p.station, p.elevation});
    return out;
}

// ── Identity ────────────────────────────────────────────────────────────────

void TransectProvider::setName(QString newName)
{
    if (newName == m_name) return;
    const QString prev = m_name;
    m_name = std::move(newName);
    emit nameChanged(prev, m_name);
}

void TransectProvider::setComments(QString newComments)
{
    if (newComments == m_comments) return;
    m_comments = std::move(newComments);
    emit commentsChanged();
}

// ── Triple setters ──────────────────────────────────────────────────────────

void TransectProvider::setRoughness(double nLeft, double nRight, double nChannel)
{
    if (nLeft == m_nLeft && nRight == m_nRight && nChannel == m_nChannel) return;
    m_nLeft    = nLeft;
    m_nRight   = nRight;
    m_nChannel = nChannel;
    emit roughnessChanged();
}

void TransectProvider::setBankStations(double xLeft, double xRight)
{
    if (xLeft == m_xLeftBank && xRight == m_xRightBank) return;
    m_xLeftBank  = xLeft;
    m_xRightBank = xRight;
    emit bankStationsChanged();
}

void TransectProvider::setEncroachmentStations(double xLeft, double xRight)
{
    if (xLeft == m_xLeftEncroach && xRight == m_xRightEncroach) return;
    m_xLeftEncroach  = xLeft;
    m_xRightEncroach = xRight;
    emit encroachmentStationsChanged();
}

void TransectProvider::setModifiers(double xFactor, double yFactor, double lengthFactor)
{
    if (xFactor == m_xFactor && yFactor == m_yFactor && lengthFactor == m_lengthFactor)
        return;
    m_xFactor      = xFactor;
    m_yFactor      = yFactor;
    m_lengthFactor = lengthFactor;
    emit modifiersChanged();
}

// ── Points ──────────────────────────────────────────────────────────────────

bool TransectProvider::setAllPoints(QVector<TransectPoint> newPoints, QString *reasonOut)
{
    if (!isStrictlyMonotone_(newPoints)) {
        const auto reason = tr("Transect stations must be strictly ascending.");
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    const int oldN = m_points.size();
    const int newN = newPoints.size();
    m_points = std::move(newPoints);

    const int common = std::min(oldN, newN);
    if (common > 0) emit pointsChanged(0, common);
    if (newN > oldN) emit pointsInserted(common, newN - oldN);
    if (newN < oldN) emit pointsRemoved(common, oldN - newN);
    return true;
}

bool TransectProvider::setElevationAt(int i, double newElev, QString *reasonOut)
{
    if (i < 0 || i >= m_points.size()) {
        const auto reason = tr("Station index %1 out of range.").arg(i);
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    if (m_points[i].elevation == newElev) return true;
    m_points[i].elevation = newElev;
    emit pointsChanged(i, 1);
    return true;
}

bool TransectProvider::setPointAt(int i, double newStation, double newElev,
                                    QString *reasonOut)
{
    if (i < 0 || i >= m_points.size()) {
        const auto reason = tr("Station index %1 out of range.").arg(i);
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    if (i > 0 && !(newStation > m_points[i - 1].station)) {
        const auto reason = tr("Station %1: must be strictly greater than the previous station.")
                              .arg(i);
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    if (i + 1 < m_points.size() && !(newStation < m_points[i + 1].station)) {
        const auto reason = tr("Station %1: must be strictly less than the next station.")
                              .arg(i);
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    if (m_points[i].station == newStation && m_points[i].elevation == newElev) return true;
    m_points[i].station   = newStation;
    m_points[i].elevation = newElev;
    emit pointsChanged(i, 1);
    return true;
}

bool TransectProvider::setPointLive(int i, double newStation, double newElev,
                                      bool *clamped)
{
    if (clamped) *clamped = false;
    if (i < 0 || i >= m_points.size()) return false;

    double lo = -std::numeric_limits<double>::infinity();
    double hi =  std::numeric_limits<double>::infinity();
    if (i > 0)               lo = m_points[i - 1].station + kStationEps;
    if (i + 1 < m_points.size()) hi = m_points[i + 1].station - kStationEps;

    if (newStation < lo) { newStation = lo; if (clamped) *clamped = true; }
    if (newStation > hi) { newStation = hi; if (clamped) *clamped = true; }

    if (m_points[i].station == newStation && m_points[i].elevation == newElev) return true;
    m_points[i].station   = newStation;
    m_points[i].elevation = newElev;
    emit pointsChanged(i, 1);
    return true;
}

int TransectProvider::insertPoint(double station, double elev, QString *reasonOut)
{
    int pos = 0;
    for (; pos < m_points.size(); ++pos) {
        if (station == m_points[pos].station) {
            const auto reason = tr("A station at %1 already exists.").arg(station);
            if (reasonOut) *reasonOut = reason;
            emit mutationRejected(reason);
            return -1;
        }
        if (station < m_points[pos].station) break;
    }
    m_points.insert(pos, {station, elev});
    emit pointsInserted(pos, 1);
    return pos;
}

void TransectProvider::removePointsAt(QVector<int> indices)
{
    if (indices.isEmpty()) return;
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    for (int i : indices) {
        if (i >= 0 && i < m_points.size()) {
            m_points.removeAt(i);
            emit pointsRemoved(i, 1);
        }
    }
}

} // namespace openswmmvis::transect
