/*!
 * \file   swmmlandusepropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmlandusepropertyadapter.h"

#include <openswmm/engine/openswmm_quality.h>

int SWMMLandUsePropertyAdapter::idx() const
{
    if (!m_engine || m_name.isEmpty()) return -1;
    return swmm_landuse_index(m_engine, m_name.toUtf8().constData());
}

QString SWMMLandUsePropertyAdapter::displayLabelFor(const QString &property) const
{
    if (property == QLatin1String("name"))           return tr("Name");
    if (property == QLatin1String("sweepInterval")) return tr("Sweep Interval (days)");
    if (property == QLatin1String("sweepRemoval"))  return tr("Sweep Removal Fraction");
    return {};
}

double SWMMLandUsePropertyAdapter::sweepInterval() const
{
    const int i = idx();
    if (i < 0) return 0.0;
    double v = 0.0;
    swmm_landuse_get_sweep_interval(m_engine, i, &v);
    return v;
}

double SWMMLandUsePropertyAdapter::sweepRemoval() const
{
    const int i = idx();
    if (i < 0) return 0.0;
    double v = 0.0;
    swmm_landuse_get_sweep_removal(m_engine, i, &v);
    return v;
}

void SWMMLandUsePropertyAdapter::setSweepInterval(double v)
{
    const int i = idx();
    if (i < 0) return;
    if (swmm_landuse_set_sweep_interval(m_engine, i, v) == SWMM_OK)
        emit changed();
}

void SWMMLandUsePropertyAdapter::setSweepRemoval(double v)
{
    const int i = idx();
    if (i < 0) return;
    if (swmm_landuse_set_sweep_removal(m_engine, i, v) == SWMM_OK)
        emit changed();
}
