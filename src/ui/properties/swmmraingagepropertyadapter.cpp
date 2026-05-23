/*!
 * \file   swmmraingagepropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmraingagepropertyadapter.h"

#include <openswmm/engine/openswmm_gages.h>

int SWMMRainGagePropertyAdapter::idx() const
{
    if (!m_engine || m_name.isEmpty()) return -1;
    return swmm_gage_index(m_engine, m_name.toUtf8().constData());
}

QString SWMMRainGagePropertyAdapter::displayLabelFor(const QString &property) const
{
    if (property == QLatin1String("name"))             return tr("Name");
    if (property == QLatin1String("rainType"))         return tr("Rain Type");
    if (property == QLatin1String("dataSource"))       return tr("Data Source");
    if (property == QLatin1String("currentRainfall"))  return tr("Current Rainfall");
    return {};
}

int SWMMRainGagePropertyAdapter::rainType() const
{
    const int i = idx();
    if (i < 0) return 0;
    int v = 0;
    swmm_gage_get_rain_type(m_engine, i, &v);
    return v;
}

int SWMMRainGagePropertyAdapter::dataSource() const
{
    const int i = idx();
    if (i < 0) return 0;
    int v = 0;
    swmm_gage_get_data_source(m_engine, i, &v);
    return v;
}

double SWMMRainGagePropertyAdapter::currentRainfall() const
{
    const int i = idx();
    if (i < 0) return 0.0;
    double v = 0.0;
    swmm_gage_get_rainfall(m_engine, i, &v);
    return v;
}

void SWMMRainGagePropertyAdapter::setRainType(int v)
{
    const int i = idx();
    if (i < 0) return;
    if (swmm_gage_set_rain_type(m_engine, i, v) == SWMM_OK) emit changed();
}

void SWMMRainGagePropertyAdapter::setDataSource(int v)
{
    const int i = idx();
    if (i < 0) return;
    if (swmm_gage_set_data_source(m_engine, i, v) == SWMM_OK) emit changed();
}
