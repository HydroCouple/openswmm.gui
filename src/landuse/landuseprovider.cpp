/*!
 * \file   landuseprovider.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "landuse/landuseprovider.h"

namespace openswmmvis::landuse {

LandUseProvider::LandUseProvider(QString name, QObject *parent)
    : QObject(parent), m_name(std::move(name))
{
}

LandUseProvider::~LandUseProvider() = default;

void LandUseProvider::setName(QString newName)
{
    if (newName == m_name) return;
    const QString prev = m_name;
    m_name = std::move(newName);
    emit nameChanged(prev, m_name);
}

void LandUseProvider::setSweepInterval(double v)
{
    if (v == m_sweepInterval) return;
    m_sweepInterval = v;
    emit paramsChanged();
}

void LandUseProvider::setSweepRemoval(double v)
{
    if (v == m_sweepRemoval) return;
    m_sweepRemoval = v;
    emit paramsChanged();
}

} // namespace openswmmvis::landuse
