/*!
 * \file   swmmtimeseriespropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmtimeseriespropertyadapter.h"

#include <openswmm/engine/openswmm_tables.h>

int SWMMTimeSeriesPropertyAdapter::idx() const
{
    if (!m_engine || m_name.isEmpty()) return -1;
    return swmm_table_index(m_engine, m_name.toUtf8().constData());
}

QString SWMMTimeSeriesPropertyAdapter::displayLabelFor(
        const QString &property) const
{
    if (property == QLatin1String("name"))         return tr("Name");
    if (property == QLatin1String("pointCount"))   return tr("Point Count");
    if (property == QLatin1String("pointSummary")) return tr("Data Grid");
    return {};
}

int SWMMTimeSeriesPropertyAdapter::pointCount() const
{
    const int i = idx();
    if (i < 0) return 0;
    int n = 0;
    swmm_table_get_point_count(m_engine, i, &n);
    return n;
}

QString SWMMTimeSeriesPropertyAdapter::pointSummary() const
{
    const int i = idx();
    if (i < 0) return {};
    int n = 0;
    swmm_table_get_point_count(m_engine, i, &n);
    if (n <= 0) return tr("(empty)");
    return tr("%1 sample%2").arg(n).arg(n == 1 ? QString() : QStringLiteral("s"));
}
