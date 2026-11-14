/**
 * @file      aquiferprovider.cpp
 * @brief     One AquiferProvider per `[AQUIFERS]` entry.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "aquifer/aquiferprovider.h"

#include <algorithm>
#include <cmath>

namespace openswmmvis::aquifer
{

AquiferProvider::AquiferProvider(QString name, QObject *parent)
    : QObject(parent), m_name(std::move(name))
{
}

AquiferProvider::~AquiferProvider() = default;

double AquiferProvider::param(int k) const noexcept
{
    if (k < 0 || k >= ParamCount)
        return 0.0;
    return m_param[k];
}

void AquiferProvider::setName(QString newName)
{
    if (newName == m_name)
        return;
    const QString prev = m_name;
    m_name = std::move(newName);
    emit nameChanged(prev, m_name);
}

void AquiferProvider::setParam(int k, double v)
{
    if (k < 0 || k >= ParamCount)
        return;
    if (v == m_param[k])
        return;
    m_param[k] = v;
    emit paramsChanged();
}

void AquiferProvider::setEvapPattern(QString pattern)
{
    if (pattern == m_evapPattern)
        return;
    m_evapPattern = std::move(pattern);
    emit paramsChanged();
}

bool AquiferProvider::Params::sameAs(const Params &other, double relTol) const
{
    if (evapPattern != other.evapPattern)
        return false;
    for (int k = 0; k < ParamCount; ++k) {
        const double a = values[k], b = other.values[k];
        if (std::abs(a - b) > relTol * std::max({1.0, std::abs(a), std::abs(b)}))
            return false;
    }
    return true;
}

AquiferProvider::Params AquiferProvider::params() const
{
    Params p;
    for (int k = 0; k < ParamCount; ++k)
        p.values[k] = m_param[k];
    p.evapPattern = m_evapPattern;
    return p;
}

void AquiferProvider::setParams(const Params &p)
{
    if (p == params())
        return;
    for (int k = 0; k < ParamCount; ++k)
        m_param[k] = p.values[k];
    m_evapPattern = p.evapPattern;
    emit paramsChanged();
}

} // namespace openswmmvis::aquifer
