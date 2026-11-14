/**
 * @file      pollutantprovider.cpp
 * @brief     One PollutantProvider per `[POLLUTANTS]` entry.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "pollutant/pollutantprovider.h"

#include <algorithm>
#include <cmath>

namespace openswmmvis::pollutant
{

PollutantProvider::PollutantProvider(QString name, QObject *parent)
    : QObject(parent), m_name(std::move(name))
{
}

PollutantProvider::~PollutantProvider() = default;

void PollutantProvider::setName(QString newName)
{
    if (newName == m_name)
        return;
    const QString prev = m_name;
    m_name = std::move(newName);
    emit nameChanged(prev, m_name);
}

void PollutantProvider::setUnits(int v)
{
    if (v == m_units)
        return;
    m_units = v;
    emit paramsChanged();
}

void PollutantProvider::setRainConc(double v)
{
    if (v == m_rainConc)
        return;
    m_rainConc = v;
    emit paramsChanged();
}

void PollutantProvider::setGwConc(double v)
{
    if (v == m_gwConc)
        return;
    m_gwConc = v;
    emit paramsChanged();
}

void PollutantProvider::setInitConc(double v)
{
    if (v == m_initConc)
        return;
    m_initConc = v;
    emit paramsChanged();
}

void PollutantProvider::setRdiiConc(double v)
{
    if (v == m_rdiiConc)
        return;
    m_rdiiConc = v;
    emit paramsChanged();
}

void PollutantProvider::setKDecay(double v)
{
    if (v == m_kDecay)
        return;
    m_kDecay = v;
    emit paramsChanged();
}

void PollutantProvider::setMwt(double v)
{
    if (v == m_mwt)
        return;
    m_mwt = v;
    emit paramsChanged();
}

void PollutantProvider::setSnowOnly(bool v)
{
    if (v == m_snowOnly)
        return;
    m_snowOnly = v;
    emit paramsChanged();
}

void PollutantProvider::setCoPollutant(QString name)
{
    if (name == m_coPollutant)
        return;
    m_coPollutant = std::move(name);
    emit paramsChanged();
}

void PollutantProvider::setCoFraction(double v)
{
    if (v == m_coFraction)
        return;
    m_coFraction = v;
    emit paramsChanged();
}

bool PollutantParams::sameAs(const PollutantParams &o, double relTol) const
{
    const auto near = [relTol](double a, double b) {
        return std::abs(a - b) <= relTol * std::max({1.0, std::abs(a), std::abs(b)});
    };
    return units == o.units && snowOnly == o.snowOnly &&
           coPollutant.compare(o.coPollutant, Qt::CaseInsensitive) == 0 &&
           near(rainConc, o.rainConc) && near(gwConc, o.gwConc) && near(initConc, o.initConc) &&
           near(rdiiConc, o.rdiiConc) && near(kDecay, o.kDecay) && near(mwt, o.mwt) &&
           near(coFraction, o.coFraction);
}

PollutantParams PollutantProvider::params() const
{
    return {m_units,  m_rainConc, m_gwConc,      m_initConc,  m_rdiiConc,
            m_kDecay, m_mwt,      m_snowOnly,    m_coPollutant, m_coFraction};
}

void PollutantProvider::setParams(const PollutantParams &v)
{
    if (v == params())
        return;
    m_units = v.units;
    m_rainConc = v.rainConc;
    m_gwConc = v.gwConc;
    m_initConc = v.initConc;
    m_rdiiConc = v.rdiiConc;
    m_kDecay = v.kDecay;
    m_mwt = v.mwt;
    m_snowOnly = v.snowOnly;
    m_coPollutant = v.coPollutant;
    m_coFraction = v.coFraction;
    emit paramsChanged();
}

} // namespace openswmmvis::pollutant
