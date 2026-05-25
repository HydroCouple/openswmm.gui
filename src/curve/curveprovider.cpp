/*!
 * \file   curveprovider.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "curve/curveprovider.h"

#include <algorithm>

namespace openswmmvis::curve {

QString CurveProvider::xLabel(CurveType t)
{
    switch (t) {
    case CurveType::Storage:   return tr("Depth");
    case CurveType::Diversion: return tr("Inflow");
    case CurveType::Rating:    return tr("Head");
    case CurveType::Shape:     return tr("Depth/Full");
    case CurveType::Control:   return tr("Variable");
    case CurveType::Tidal:     return tr("Hour");
    case CurveType::Pump1:     return tr("Volume");
    case CurveType::Pump2:     return tr("Depth");
    case CurveType::Pump3:     return tr("Head");
    case CurveType::Pump4:     return tr("Depth");
    case CurveType::Pump5:     return tr("Depth");
    }
    return tr("X");
}

QString CurveProvider::yLabel(CurveType t)
{
    switch (t) {
    case CurveType::Storage:   return tr("Surface Area");
    case CurveType::Diversion: return tr("Diverted Flow");
    case CurveType::Rating:    return tr("Flow");
    case CurveType::Shape:     return tr("Width/Full");
    case CurveType::Control:   return tr("Setting");
    case CurveType::Tidal:     return tr("Stage");
    case CurveType::Pump1:     return tr("Flow");
    case CurveType::Pump2:     return tr("Flow");
    case CurveType::Pump3:     return tr("Flow");
    case CurveType::Pump4:     return tr("Flow");
    case CurveType::Pump5:     return tr("Flow");
    }
    return tr("Y");
}

QString CurveProvider::typeLabel(CurveType t)
{
    switch (t) {
    case CurveType::Storage:   return tr("Storage (Depth → Surface Area)");
    case CurveType::Diversion: return tr("Diversion (Inflow → Diverted Flow)");
    case CurveType::Rating:    return tr("Rating (Head → Flow)");
    case CurveType::Shape:     return tr("Shape (Depth → Width, custom xsect)");
    case CurveType::Control:   return tr("Control (Variable → Setting)");
    case CurveType::Tidal:     return tr("Tidal (Hour → Stage)");
    case CurveType::Pump1:     return tr("Pump 1 (Volume → Flow)");
    case CurveType::Pump2:     return tr("Pump 2 (Depth → Flow, on/off)");
    case CurveType::Pump3:     return tr("Pump 3 (Head → Flow, continuous)");
    case CurveType::Pump4:     return tr("Pump 4 (Depth → Flow, continuous)");
    case CurveType::Pump5:     return tr("Pump 5 (Depth → Flow, variable speed)");
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────

CurveProvider::CurveProvider(QString name, CurveType type, QObject *parent)
    : QObject(parent)
    , m_name(std::move(name))
    , m_type(type)
{
}

CurveProvider::~CurveProvider() = default;

bool CurveProvider::isStrictlyMonotoneX_(const QVector<CurvePoint> &pts)
{
    for (int i = 1; i < pts.size(); ++i) {
        if (!(pts[i].x > pts[i - 1].x)) return false;
    }
    return true;
}

bool CurveProvider::setAllPoints(QVector<CurvePoint> newPoints, QString *reasonOut)
{
    if (!isStrictlyMonotoneX_(newPoints)) {
        const auto reason = tr("Curve points must be strictly ascending in X.");
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    const int oldN = m_points.size();
    const int newN = newPoints.size();
    m_points = std::move(newPoints);

    // Emit the most specific signal: replaced common prefix + maybe insert / remove.
    const int common = std::min(oldN, newN);
    if (common > 0) emit pointsChanged(0, common);
    if (newN > oldN) emit pointsInserted(common, newN - oldN);
    if (newN < oldN) emit pointsRemoved(common, oldN - newN);
    return true;
}

bool CurveProvider::setYAt(int i, double newY, QString *reasonOut)
{
    if (i < 0 || i >= m_points.size()) {
        const auto reason = tr("Point index %1 out of range.").arg(i);
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    if (m_points[i].y == newY) return true;
    m_points[i].y = newY;
    emit pointsChanged(i, 1);
    return true;
}

bool CurveProvider::setPointAt(int i, double newX, double newY, QString *reasonOut)
{
    if (i < 0 || i >= m_points.size()) {
        const auto reason = tr("Point index %1 out of range.").arg(i);
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    // Check left + right neighbour bounds (strict).
    if (i > 0 && !(newX > m_points[i - 1].x)) {
        const auto reason = tr("Point %1: X must be strictly greater than the previous point's X.")
                              .arg(i);
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    if (i + 1 < m_points.size() && !(newX < m_points[i + 1].x)) {
        const auto reason = tr("Point %1: X must be strictly less than the next point's X.")
                              .arg(i);
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    if (m_points[i].x == newX && m_points[i].y == newY) return true;
    m_points[i].x = newX;
    m_points[i].y = newY;
    emit pointsChanged(i, 1);
    return true;
}

int CurveProvider::insertPoint(double x, double y, QString *reasonOut)
{
    // Find sorted-insert position; reject equal-X collisions.
    int pos = 0;
    for (; pos < m_points.size(); ++pos) {
        if (x == m_points[pos].x) {
            const auto reason = tr("A point with X=%1 already exists.").arg(x);
            if (reasonOut) *reasonOut = reason;
            emit mutationRejected(reason);
            return -1;
        }
        if (x < m_points[pos].x) break;
    }
    m_points.insert(pos, {x, y});
    emit pointsInserted(pos, 1);
    return pos;
}

void CurveProvider::removePointsAt(QVector<int> indices)
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

void CurveProvider::setName(QString newName)
{
    if (newName == m_name) return;
    const QString prev = m_name;
    m_name = std::move(newName);
    emit nameChanged(prev, m_name);
}

void CurveProvider::setType(CurveType t)
{
    if (t == m_type) return;
    const CurveType prev = m_type;
    m_type = t;
    emit typeChanged(prev, m_type);
}

} // namespace openswmmvis::curve
