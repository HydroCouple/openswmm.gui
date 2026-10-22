/**
 * @file      snowpackprovider.cpp
 * @brief     One SnowpackProvider per `[SNOWPACKS]` entry.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "snowpack/snowpackprovider.h"

namespace openswmmvis::snowpack
{

SnowpackProvider::SnowpackProvider(QString name, QObject *parent)
    : QObject(parent), m_name(std::move(name))
{
    m_param[UebDrift] = 1;
    m_param[UebExtinctionDepth] = .1;
}

SnowpackProvider::~SnowpackProvider() = default;

double SnowpackProvider::param(int k) const noexcept
{
    if (k < 0 || k >= ParamCount)
        return 0.0;
    return m_param[k];
}

void SnowpackProvider::setName(QString newName)
{
    if (newName == m_name)
        return;
    const QString prev = m_name;
    m_name = std::move(newName);
    emit nameChanged(prev, m_name);
}

void SnowpackProvider::setParam(int k, double v)
{
    if (k < 0 || k >= ParamCount)
        return;
    if (v == m_param[k])
        return;
    m_param[k] = v;
    emit paramsChanged();
}

void SnowpackProvider::setRemovalSubcatch(QString name)
{
    if (name == m_removalSubcatch)
        return;
    m_removalSubcatch = std::move(name);
    emit paramsChanged();
}

void SnowpackProvider::setModelKind(int kind)
{
    if (kind < -1 || kind > 1 || kind == m_modelKind)
        return;
    m_modelKind = kind;
    emit paramsChanged();
}

void SnowpackProvider::setDefaultModel(int kind)
{
    if (kind < 0 || kind > 1 || kind == m_defaultModel)
        return;
    m_defaultModel = kind;
    emit paramsChanged();
}

} // namespace openswmmvis::snowpack
