/*!
 * \file   pollutantprovider.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "pollutant/pollutantprovider.h"

namespace openswmmvis::pollutant {

PollutantProvider::PollutantProvider(QString name, QObject *parent)
    : QObject(parent), m_name(std::move(name))
{
}

PollutantProvider::~PollutantProvider() = default;

void PollutantProvider::setName(QString newName)
{
    if (newName == m_name) return;
    const QString prev = m_name;
    m_name = std::move(newName);
    emit nameChanged(prev, m_name);
}

void PollutantProvider::setUnits(int v)
{
    if (v == m_units) return;
    m_units = v;
    emit paramsChanged();
}

void PollutantProvider::setRainConc(double v)
{
    if (v == m_rainConc) return;
    m_rainConc = v;
    emit paramsChanged();
}

void PollutantProvider::setGwConc(double v)
{
    if (v == m_gwConc) return;
    m_gwConc = v;
    emit paramsChanged();
}

void PollutantProvider::setInitConc(double v)
{
    if (v == m_initConc) return;
    m_initConc = v;
    emit paramsChanged();
}

void PollutantProvider::setRdiiConc(double v)
{
    if (v == m_rdiiConc) return;
    m_rdiiConc = v;
    emit paramsChanged();
}

void PollutantProvider::setKDecay(double v)
{
    if (v == m_kDecay) return;
    m_kDecay = v;
    emit paramsChanged();
}

void PollutantProvider::setMwt(double v)
{
    if (v == m_mwt) return;
    m_mwt = v;
    emit paramsChanged();
}

void PollutantProvider::setSnowOnly(bool v)
{
    if (v == m_snowOnly) return;
    m_snowOnly = v;
    emit paramsChanged();
}

void PollutantProvider::setCoPollutant(QString name)
{
    if (name == m_coPollutant) return;
    m_coPollutant = std::move(name);
    emit paramsChanged();
}

void PollutantProvider::setCoFraction(double v)
{
    if (v == m_coFraction) return;
    m_coFraction = v;
    emit paramsChanged();
}

} // namespace openswmmvis::pollutant
