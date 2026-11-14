/**
 * @file      streetprovider.cpp
 * @brief     One StreetProvider per `[STREETS]` entry in the project.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "street/streetprovider.h"

#include <algorithm>
#include <cmath>

#include <utility>

namespace openswmmvis::street
{

StreetProvider::StreetProvider(QString name, QObject *parent)
    : QObject(parent), m_name(std::move(name))
{
}

StreetProvider::~StreetProvider() = default;

void StreetProvider::setName(QString newName)
{
    if (newName == m_name || newName.isEmpty())
        return;
    const QString prev = m_name;
    m_name = std::move(newName);
    emit nameChanged(prev, m_name);
}

void StreetProvider::setCrownWidth(double v)
{
    if (v == m_crownWidth)
        return;
    m_crownWidth = v;
    emit paramsChanged();
}

void StreetProvider::setCurbHeight(double v)
{
    if (v == m_curbHeight)
        return;
    m_curbHeight = v;
    emit paramsChanged();
}

void StreetProvider::setCrossSlope(double v)
{
    if (v == m_crossSlope)
        return;
    m_crossSlope = v;
    emit paramsChanged();
}

void StreetProvider::setRoadRoughness(double v)
{
    if (v == m_roadRoughness)
        return;
    m_roadRoughness = v;
    emit paramsChanged();
}

void StreetProvider::setGutterDepression(double v)
{
    if (v == m_gutterDepression)
        return;
    m_gutterDepression = v;
    emit paramsChanged();
}

void StreetProvider::setGutterWidth(double v)
{
    if (v == m_gutterWidth)
        return;
    m_gutterWidth = v;
    emit paramsChanged();
}

void StreetProvider::setSides(int v)
{
    if (v == m_sides || (v != 1 && v != 2))
        return;
    m_sides = v;
    emit paramsChanged();
}

void StreetProvider::setBackingWidth(double v)
{
    if (v == m_backingWidth)
        return;
    m_backingWidth = v;
    emit paramsChanged();
}

void StreetProvider::setBackingSlope(double v)
{
    if (v == m_backingSlope)
        return;
    m_backingSlope = v;
    emit paramsChanged();
}

void StreetProvider::setBackingRoughness(double v)
{
    if (v == m_backingRoughness)
        return;
    m_backingRoughness = v;
    emit paramsChanged();
}

bool StreetProvider::Params::sameAs(const Params &o, double relTol) const
{
    const auto close = [relTol](double a, double b) {
        return std::abs(a - b) <= relTol * std::max({1.0, std::abs(a), std::abs(b)});
    };
    return sides == o.sides && close(crownWidth, o.crownWidth) && close(curbHeight, o.curbHeight) &&
           close(crossSlope, o.crossSlope) && close(roadRoughness, o.roadRoughness) &&
           close(gutterDepression, o.gutterDepression) && close(gutterWidth, o.gutterWidth) &&
           close(backingWidth, o.backingWidth) && close(backingSlope, o.backingSlope) &&
           close(backingRoughness, o.backingRoughness);
}

StreetProvider::Params StreetProvider::params() const
{
    return {m_crownWidth,  m_curbHeight, m_crossSlope,   m_roadRoughness, m_gutterDepression,
            m_gutterWidth, m_sides,      m_backingWidth, m_backingSlope,  m_backingRoughness};
}

void StreetProvider::setParams(const Params &p)
{
    if (p == params())
        return;
    m_crownWidth = p.crownWidth;
    m_curbHeight = p.curbHeight;
    m_crossSlope = p.crossSlope;
    m_roadRoughness = p.roadRoughness;
    m_gutterDepression = p.gutterDepression;
    m_gutterWidth = p.gutterWidth;
    m_sides = p.sides;
    m_backingWidth = p.backingWidth;
    m_backingSlope = p.backingSlope;
    m_backingRoughness = p.backingRoughness;
    emit paramsChanged();
}

} // namespace openswmmvis::street
