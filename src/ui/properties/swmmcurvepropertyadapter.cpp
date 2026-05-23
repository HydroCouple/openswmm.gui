/*!
 * \file   swmmcurvepropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmcurvepropertyadapter.h"

#include <openswmm/engine/openswmm_tables.h>

#include <algorithm>
#include <limits>

int SWMMCurvePropertyAdapter::idx() const
{
    if (!m_engine || m_name.isEmpty()) return -1;
    return swmm_table_index(m_engine, m_name.toUtf8().constData());
}

QString SWMMCurvePropertyAdapter::displayLabelFor(const QString &property) const
{
    if (property == QLatin1String("name"))         return tr("Name");
    if (property == QLatin1String("curveType"))    return tr("Curve Type");
    if (property == QLatin1String("pointCount"))   return tr("Point Count");
    if (property == QLatin1String("pointSummary")) return tr("X/Y Grid");
    return {};
}

int SWMMCurvePropertyAdapter::curveType() const
{
    const int i = idx();
    if (i < 0) return 0;
    int t = 0;
    swmm_table_get_type(m_engine, i, &t);
    return t;
}

int SWMMCurvePropertyAdapter::pointCount() const
{
    const int i = idx();
    if (i < 0) return 0;
    int n = 0;
    swmm_table_get_point_count(m_engine, i, &n);
    return n;
}

QString SWMMCurvePropertyAdapter::pointSummary() const
{
    const int i = idx();
    if (i < 0) return {};
    int n = 0;
    swmm_table_get_point_count(m_engine, i, &n);
    if (n <= 0) return tr("(empty)");

    double xMin = std::numeric_limits<double>::max();
    double xMax = std::numeric_limits<double>::lowest();
    double yMin = std::numeric_limits<double>::max();
    double yMax = std::numeric_limits<double>::lowest();
    for (int p = 0; p < n; ++p) {
        double x = 0, y = 0;
        if (swmm_table_get_point(m_engine, i, p, &x, &y) != SWMM_OK) continue;
        xMin = std::min(xMin, x); xMax = std::max(xMax, x);
        yMin = std::min(yMin, y); yMax = std::max(yMax, y);
    }
    return tr("%1 rows, X: %2–%3, Y: %4–%5")
            .arg(n)
            .arg(xMin, 0, 'g', 4).arg(xMax, 0, 'g', 4)
            .arg(yMin, 0, 'g', 4).arg(yMax, 0, 'g', 4);
}
