/*!
 * \file   swmmpollutantpropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmpollutantpropertyadapter.h"

#include <openswmm/engine/openswmm_pollutants.h>

int SWMMPollutantPropertyAdapter::idx() const
{
    if (!m_engine || m_name.isEmpty()) return -1;
    return swmm_pollutant_index(m_engine, m_name.toUtf8().constData());
}

QString SWMMPollutantPropertyAdapter::displayLabelFor(const QString &property) const
{
    if (property == QLatin1String("name"))             return tr("Name");
    if (property == QLatin1String("units"))            return tr("Units (0=mg/L, 1=µg/L, 2=#/L)");
    if (property == QLatin1String("rainConc"))         return tr("Rain Conc.");
    if (property == QLatin1String("gwConc"))           return tr("GW Conc.");
    if (property == QLatin1String("initConc"))         return tr("Initial Conc.");
    if (property == QLatin1String("rdiiConc"))         return tr("RDII Conc.");
    if (property == QLatin1String("kDecay"))           return tr("Decay (1/day)");
    if (property == QLatin1String("mwt"))              return tr("Mol. Wt. (g/mol)");
    if (property == QLatin1String("snowOnly"))         return tr("Snow only");
    if (property == QLatin1String("coPollutant"))      return tr("Co-Pollutant");
    if (property == QLatin1String("coPollutantFrac")) return tr("Co-Pollutant Fraction");
    return {};
}

#define G(method, engineGet, ctype, defaultVal)        \
ctype SWMMPollutantPropertyAdapter::method() const {   \
    const int i = idx();                               \
    if (i < 0) return defaultVal;                      \
    ctype v = defaultVal;                              \
    engineGet(m_engine, i, &v);                        \
    return v;                                          \
}
G(units,    swmm_pollutant_get_units,     int,    0)
G(rainConc, swmm_pollutant_get_rain_conc, double, 0.0)
G(gwConc,   swmm_pollutant_get_gw_conc,   double, 0.0)
G(initConc, swmm_pollutant_get_init_conc, double, 0.0)
G(rdiiConc, swmm_pollutant_get_rdii_conc, double, 0.0)
G(kDecay,   swmm_pollutant_get_kdecay,    double, 0.0)
G(mwt,      swmm_pollutant_get_mwt,       double, 0.0)
#undef G

bool SWMMPollutantPropertyAdapter::snowOnly() const
{
    const int i = idx();
    if (i < 0) return false;
    int flag = 0;
    swmm_pollutant_get_snow_only(m_engine, i, &flag);
    return flag != 0;
}

QString SWMMPollutantPropertyAdapter::coPollutant() const
{
    const int i = idx();
    if (i < 0) return {};
    int co = -1;
    double f = 0.0;
    if (swmm_pollutant_get_co_pollutant(m_engine, i, &co, &f) != SWMM_OK || co < 0)
        return {};
    const char *p = swmm_pollutant_id(m_engine, co);
    return p ? QString::fromUtf8(p) : QString();
}

double SWMMPollutantPropertyAdapter::coPollutantFrac() const
{
    const int i = idx();
    if (i < 0) return 0.0;
    int co = -1;
    double f = 0.0;
    swmm_pollutant_get_co_pollutant(m_engine, i, &co, &f);
    return f;
}

#define S(method, engineSet, ctype)                    \
void SWMMPollutantPropertyAdapter::method(ctype v) {   \
    const int i = idx();                               \
    if (i < 0) return;                                 \
    if (engineSet(m_engine, i, v) == SWMM_OK) emit changed(); \
}
S(setRainConc, swmm_pollutant_set_rain_conc, double)
S(setGwConc,   swmm_pollutant_set_gw_conc,   double)
S(setInitConc, swmm_pollutant_set_init_conc, double)
S(setRdiiConc, swmm_pollutant_set_rdii_conc, double)
S(setKDecay,   swmm_pollutant_set_kdecay,    double)
S(setMwt,      swmm_pollutant_set_mwt,       double)
#undef S

void SWMMPollutantPropertyAdapter::setSnowOnly(bool v)
{
    const int i = idx();
    if (i < 0) return;
    if (swmm_pollutant_set_snow_only(m_engine, i, v ? 1 : 0) == SWMM_OK)
        emit changed();
}

void SWMMPollutantPropertyAdapter::setCoPollutant(const QString &poll)
{
    const int i = idx();
    if (i < 0) return;
    int co = -1;
    if (!poll.trimmed().isEmpty())
        co = swmm_pollutant_index(m_engine, poll.toUtf8().constData());
    // Preserve the existing fraction when changing the co-pollutant name.
    double frac = coPollutantFrac();
    if (swmm_pollutant_set_co_pollutant(m_engine, i, co, frac) == SWMM_OK)
        emit changed();
}

void SWMMPollutantPropertyAdapter::setCoPollutantFrac(double v)
{
    const int i = idx();
    if (i < 0) return;
    int co = -1;
    double currFrac = 0.0;
    swmm_pollutant_get_co_pollutant(m_engine, i, &co, &currFrac);
    if (swmm_pollutant_set_co_pollutant(m_engine, i, co, v) == SWMM_OK)
        emit changed();
}
