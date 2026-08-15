/*!
 * \file   mapextent.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 */

#include "map/mapextent.h"

#include <QRectF>
#include <QString>
#include <cmath>
#include <limits>

MapExtent::MapExtent(double xMin, double yMin, double xMax, double yMax)
    : m_xMin(xMin), m_yMin(yMin), m_xMax(xMax), m_yMax(yMax)
{
}

MapExtent::MapExtent(const QRectF &rect)
    : m_xMin(rect.left()),
      m_yMin(rect.top()),
      m_xMax(rect.right()),
      m_yMax(rect.bottom())
{
}

bool MapExtent::isValid() const
{
    return std::isfinite(m_xMin) && std::isfinite(m_yMin) &&
           std::isfinite(m_xMax) && std::isfinite(m_yMax) &&
           m_xMin < m_xMax && m_yMin < m_yMax;
}

bool MapExtent::isNull() const
{
    return m_xMin == 0.0 && m_yMin == 0.0 && m_xMax == 0.0 && m_yMax == 0.0;
}

QRectF MapExtent::toRectF() const
{
    return QRectF(m_xMin, m_yMin, width(), height());
}

QString MapExtent::toString() const
{
    return QStringLiteral("[%1, %2] — [%3, %4]")
        .arg(m_xMin).arg(m_yMin).arg(m_xMax).arg(m_yMax);
}

void MapExtent::expandToInclude(double x, double y)
{
    if (isNull())
    {
        m_xMin = m_xMax = x;
        m_yMin = m_yMax = y;
        return;
    }
    if (x < m_xMin) m_xMin = x;
    if (x > m_xMax) m_xMax = x;
    if (y < m_yMin) m_yMin = y;
    if (y > m_yMax) m_yMax = y;
}

void MapExtent::expandToInclude(const MapExtent &other)
{
    if (!other.isValid()) return;
    expandToInclude(other.m_xMin, other.m_yMin);
    expandToInclude(other.m_xMax, other.m_yMax);
}

void MapExtent::growByFactor(double factor)
{
    const double dx = width()  * factor;
    const double dy = height() * factor;
    m_xMin -= dx;
    m_xMax += dx;
    m_yMin -= dy;
    m_yMax += dy;
}

MapExtent MapExtent::scaled(double factor) const
{
    MapExtent e = *this;
    e.growByFactor((factor - 1.0) * 0.5);
    return e;
}

MapExtent MapExtent::panned(double dx, double dy) const
{
    return MapExtent(m_xMin + dx, m_yMin + dy, m_xMax + dx, m_yMax + dy);
}

MapExtent MapExtent::intersected(const MapExtent &other) const
{
    double xMin = std::max(m_xMin, other.m_xMin);
    double yMin = std::max(m_yMin, other.m_yMin);
    double xMax = std::min(m_xMax, other.m_xMax);
    double yMax = std::min(m_yMax, other.m_yMax);
    if (xMin >= xMax || yMin >= yMax) return MapExtent();
    return MapExtent(xMin, yMin, xMax, yMax);
}

MapExtent MapExtent::united(const MapExtent &other) const
{
    return MapExtent(
        std::min(m_xMin, other.m_xMin),
        std::min(m_yMin, other.m_yMin),
        std::max(m_xMax, other.m_xMax),
        std::max(m_yMax, other.m_yMax));
}

bool MapExtent::contains(double x, double y) const
{
    return x >= m_xMin && x <= m_xMax && y >= m_yMin && y <= m_yMax;
}

bool MapExtent::contains(const MapExtent &other) const
{
    return other.m_xMin >= m_xMin && other.m_xMax <= m_xMax &&
           other.m_yMin >= m_yMin && other.m_yMax <= m_yMax;
}

bool MapExtent::intersects(const MapExtent &other) const
{
    return !(other.m_xMin > m_xMax || other.m_xMax < m_xMin ||
             other.m_yMin > m_yMax || other.m_yMax < m_yMin);
}

bool MapExtent::operator==(const MapExtent &other) const
{
    return m_xMin == other.m_xMin && m_yMin == other.m_yMin &&
           m_xMax == other.m_xMax && m_yMax == other.m_yMax;
}

bool MapExtent::operator!=(const MapExtent &other) const
{
    return !(*this == other);
}
