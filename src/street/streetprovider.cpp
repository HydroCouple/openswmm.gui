/*!
 * \file   streetprovider.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "street/streetprovider.h"

#include <utility>

namespace openswmmvis::street {

StreetProvider::StreetProvider(QString name, QObject *parent)
    : QObject(parent), m_name(std::move(name))
{
}

StreetProvider::~StreetProvider() = default;

void StreetProvider::setName(QString newName)
{
    if (newName == m_name || newName.isEmpty()) return;
    const QString prev = m_name;
    m_name = std::move(newName);
    emit nameChanged(prev, m_name);
}

void StreetProvider::setCrownWidth(double v)
{
    if (v == m_crownWidth) return;
    m_crownWidth = v;
    emit paramsChanged();
}

void StreetProvider::setCurbHeight(double v)
{
    if (v == m_curbHeight) return;
    m_curbHeight = v;
    emit paramsChanged();
}

void StreetProvider::setCrossSlope(double v)
{
    if (v == m_crossSlope) return;
    m_crossSlope = v;
    emit paramsChanged();
}

void StreetProvider::setRoadRoughness(double v)
{
    if (v == m_roadRoughness) return;
    m_roadRoughness = v;
    emit paramsChanged();
}

void StreetProvider::setGutterDepression(double v)
{
    if (v == m_gutterDepression) return;
    m_gutterDepression = v;
    emit paramsChanged();
}

void StreetProvider::setGutterWidth(double v)
{
    if (v == m_gutterWidth) return;
    m_gutterWidth = v;
    emit paramsChanged();
}

void StreetProvider::setSides(int v)
{
    if (v == m_sides || (v != 1 && v != 2)) return;
    m_sides = v;
    emit paramsChanged();
}

void StreetProvider::setBackingWidth(double v)
{
    if (v == m_backingWidth) return;
    m_backingWidth = v;
    emit paramsChanged();
}

void StreetProvider::setBackingSlope(double v)
{
    if (v == m_backingSlope) return;
    m_backingSlope = v;
    emit paramsChanged();
}

void StreetProvider::setBackingRoughness(double v)
{
    if (v == m_backingRoughness) return;
    m_backingRoughness = v;
    emit paramsChanged();
}

} // namespace openswmmvis::street
