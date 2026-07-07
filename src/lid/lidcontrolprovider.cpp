/*!
 * \file   lidcontrolprovider.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "lid/lidcontrolprovider.h"

namespace openswmmvis::lid {

LidControlProvider::LidControlProvider(QString name, QObject *parent)
    : QObject(parent), m_name(std::move(name))
{
}

LidControlProvider::~LidControlProvider() = default;

void LidControlProvider::setName(QString newName)
{
    if (newName == m_name) return;
    const QString prev = m_name;
    m_name = std::move(newName);
    emit nameChanged(prev, m_name);
}

void LidControlProvider::setType(int v)
{
    if (v == m_type) return;
    m_type = v;
    m_dirty = true;
    emit paramsChanged();
}

// Double-field setters: change-detect, mark dirty, notify.
#define LID_SET(method, member)                 \
void LidControlProvider::method(double v) {     \
    if (v == member) return;                    \
    member = v;                                 \
    m_dirty = true;                             \
    emit paramsChanged();                       \
}

LID_SET(setSurfStorage,   m_surfStorage)
LID_SET(setSurfRoughness, m_surfRoughness)
LID_SET(setSurfSlope,     m_surfSlope)
LID_SET(setSoilThick,     m_soilThick)
LID_SET(setSoilPorosity,  m_soilPorosity)
LID_SET(setSoilFc,        m_soilFc)
LID_SET(setSoilWp,        m_soilWp)
LID_SET(setSoilKsat,      m_soilKsat)
LID_SET(setSoilKslope,    m_soilKslope)
LID_SET(setStorThick,     m_storThick)
LID_SET(setStorVoidFrac,  m_storVoidFrac)
LID_SET(setStorKsat,      m_storKsat)
LID_SET(setDrainCoeff,    m_drainCoeff)
LID_SET(setDrainExpon,    m_drainExpon)
LID_SET(setDrainOffset,   m_drainOffset)

#undef LID_SET

} // namespace openswmmvis::lid
